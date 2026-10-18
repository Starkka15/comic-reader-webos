// Service for the Comic Reader app (node 0.4.12 on webOS 3 / webOS CE, ES5 only).
//
// A web app can neither look through the TouchPad's storage, nor open an
// archive, nor fetch a file from a server to keep. This service does those:
// it finds the comics on the TouchPad, takes single pages out of them as they
// are wanted, and downloads comics and covers from an OPDS catalog.
//
// Every answer carries "ok". A failure has ok: false and "error", a sentence
// the app can show. (returnValue cannot carry a failure back to an app.)

if (typeof require === "undefined") {
	require = IMPORTS.require;
}

var child_process = require("child_process");
var fs = require("fs");

var STORAGE = "/media/internal";
var COMICS = STORAGE + "/comics";               // where downloads go
var DATA = STORAGE + "/.comicreader";
var PAGES = DATA + "/pages";                    // pages taken out of the comic being read
var COVERS = DATA + "/covers";                  // first pages, kept
var SERVICE = "palm://org.webos.comicreader.service/";
// 7za from p7zip with every format built in (native/build-7z.sh). Started
// through the system's program loader, which does not need the file to be
// marked as a program: installing a package does not keep that mark.
var SEVENZ = "/media/cryptofs/apps/usr/palm/services/org.webos.comicreader.service/bin/7za";
var LOADER = "/lib/ld-linux.so.3";
var CURL = "/usr/bin/curl";
// How this app names itself to servers.
var AGENT = "ComicReader-webOS/0.1 (OPDS)";

var COMIC = /\.(cbz|cbr|cb7|cbt)$/i;
var IMAGE = /\.(jpe?g|png|gif|bmp|webp)$/i;
var MAX_DEPTH = 6;
var KEEP_PAGES = 10;       // pages of the open comic kept on disk at once
var KEEP_COMICS = 2;       // comics with pages on disk at once

function log(msg) {
	try { console.log("[crsvc] " + msg); } catch (e) {}
}

function fail(future, text) {
	future.result = {returnValue: true, ok: false, error: text};
	return future;
}

function stat(path) {
	try {
		return fs.statSync(path);
	} catch (e) {
		return null;
	}
}

function mkdirs(path) {
	var parts = path.split("/");
	var at = "";
	for (var i = 1; i < parts.length; i++) {
		at += "/" + parts[i];
		try { fs.mkdirSync(at, 493 /* 0755 */); } catch (e) {}
	}
}

// A tidy path on the TouchPad's storage, or null.
function clean(path) {
	path = String(path || "");
	if (path.indexOf(STORAGE + "/") !== 0 || /\/\.\.(\/|$)/.test(path)) {
		return null;
	}
	return path.replace(/\/+/g, "/").replace(/\/$/, "");
}

// A short name for one version of a file.
function keyFor(path, st) {
	var a = 5381;
	var b = 52711;
	var text = path + "|" + st.size + "|" + st.mtime.getTime();
	for (var i = 0; i < text.length; i++) {
		var c = text.charCodeAt(i);
		a = ((a * 33) ^ c) >>> 0;
		b = ((b * 31) + c) >>> 0;
	}
	return a.toString(36) + b.toString(36);
}

// "Page 2" before "Page 10".
function natural(a, b) {
	var ra = /(\d+)|(\D+)/g;
	var rb = /(\d+)|(\D+)/g;
	for (;;) {
		var ma = ra.exec(a);
		var mb = rb.exec(b);
		if (!ma || !mb) {
			return ma ? 1 : (mb ? -1 : 0);
		}
		if (ma[1] && mb[1]) {
			var d = Number(ma[1]) - Number(mb[1]);
			if (d) {
				return d;
			}
		} else if (ma[0] !== mb[0]) {
			return ma[0] < mb[0] ? -1 : 1;
		}
	}
}

// Run a program with arguments (never through a shell). done(code, out, err)
function run(program, args, input, done) {
	var out = "";
	var err = "";
	var child;
	try {
		child = child_process.spawn(program, args);
	} catch (e) {
		done(255, "", String(e));
		return null;
	}
	child.stdout.on("data", function(d) { out += d.toString(); });
	child.stderr.on("data", function(d) { err += d.toString(); });
	child.on("exit", function(code) {
		done(code === null || code === undefined ? 255 : code, out, err);
	});
	try {
		child.stdin.end(input || "");
	} catch (e2) {}
	return child;
}

function sevenZip(args, done) {
	return run(LOADER, [SEVENZ].concat(args), "", done);
}

// ---- the comics on the TouchPad -----------------------------------------------

// The cover kept for a comic, if it has been made: its path, or "".
function coverOf(key) {
	var exts = [".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp"];
	for (var i = 0; i < exts.length; i++) {
		if (stat(COVERS + "/" + key + exts[i])) {
			return COVERS + "/" + key + exts[i];
		}
	}
	return "";
}

// {} -> {comics: [{path, name, folder, size, modified, key, cover}]}
// Every comic on the storage's comics folder, and any lying elsewhere on it.
var scanAssistant = function() {};
scanAssistant.prototype.run = function(future) {
	var found = [];

	function walk(dir, depth) {
		var names;
		try {
			names = fs.readdirSync(dir);
		} catch (e) {
			return;
		}
		for (var i = 0; i < names.length; i++) {
			var name = names[i];
			if (name.charAt(0) === ".") {
				continue;
			}
			var full = dir + "/" + name;
			var st = stat(full);
			if (!st) {
				continue;
			}
			if (st.isDirectory()) {
				if (depth < MAX_DEPTH) {
					walk(full, depth + 1);
				}
			} else if (COMIC.test(name)) {
				var key = keyFor(full, st);
				found.push({
					path: full, name: name.replace(COMIC, ""), folder: dir.substring(dir.lastIndexOf("/") + 1),
					size: st.size, modified: st.mtime.getTime(), key: key, cover: coverOf(key)
				});
			}
		}
	}

	mkdirs(COMICS);
	walk(STORAGE, 0);
	future.result = {returnValue: true, ok: true, comics: found};
	return future;
};

// The comics whose pages are listed, by key: {path, pages: [names in the archive], dir}
var opened = {};
var openOrder = [];

// List the pages of a comic. done(error, book)
function openBook(path, done) {
	var st = stat(path);
	if (!st || !st.isFile()) {
		done("That comic could not be found. It may have been moved or deleted.");
		return;
	}
	var key = keyFor(path, st);
	if (opened[key]) {
		done("", opened[key]);
		return;
	}
	sevenZip(["l", "-slt", "-sccUTF-8", path], function(code, out, err) {
		if (code >= 2) {
			log("list " + path + ": " + (out + err).replace(/\s+/g, " ").substring(0, 300));
			done(/password|encrypted/i.test(out + err) ? "This comic is protected with a password." :
				"That comic could not be opened. It may be damaged, or not really a comic archive.");
			return;
		}
		// Blocks of "Name = value" lines, one block per thing in the archive.
		var blocks = out.split(/\n\s*\n/);
		var pages = [];
		for (var i = 0; i < blocks.length; i++) {
			var m = /^Path = (.*)$/m.exec(blocks[i]);
			if (!m || /^Folder = \+/m.test(blocks[i]) || !/^Size = /m.test(blocks[i])) {
				continue;
			}
			var name = m[1].replace(/\r$/, "");
			// Leave out the bits some computers add beside each picture.
			if (IMAGE.test(name) && !/(^|\/)(__MACOSX\/|\._)/.test(name)) {
				pages.push(name);
			}
		}
		pages.sort(function(a, b) {
			return natural(a.toLowerCase(), b.toLowerCase());
		});
		if (!pages.length) {
			done("There are no pictures in that comic.");
			return;
		}
		var book = {key: key, path: path, pages: pages, dir: PAGES + "/" + key, busy: {}, waiting: {}};
		opened[key] = book;
		openOrder.push(key);
		// Only the comics read most recently keep pages on the disk.
		while (openOrder.length > KEEP_COMICS) {
			var old = openOrder.shift();
			child_process.spawn("/bin/rm", ["-rf", PAGES + "/" + old]);
			delete opened[old];
		}
		mkdirs(book.dir);
		done("", book);
	});
}

function pageFile(book, index) {
	var name = book.pages[index];
	return book.dir + "/" + index + name.substring(name.lastIndexOf(".")).toLowerCase();
}

// The size of a picture, read from the start of its file: {w, h}, or zeros.
function pictureSize(path) {
	var out = {w: 0, h: 0};
	var fd;
	try {
		fd = fs.openSync(path, "r");
		var buf = new Buffer(65536);
		var n = fs.readSync(fd, buf, 0, buf.length, 0);
		if (n > 24 && buf[0] === 0x89 && buf[1] === 0x50) {
			// PNG: the size follows the first chunk's name.
			out.w = buf.readUInt32BE ? buf.readUInt32BE(16) : ((buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19]);
			out.h = buf.readUInt32BE ? buf.readUInt32BE(20) : ((buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23]);
		} else if (n > 4 && buf[0] === 0xFF && buf[1] === 0xD8) {
			// JPEG: walk the segments to the one that starts the picture.
			var i = 2;
			while (i + 9 < n) {
				if (buf[i] !== 0xFF) {
					i++;
					continue;
				}
				var kind = buf[i + 1];
				if (kind >= 0xC0 && kind <= 0xCF && kind !== 0xC4 && kind !== 0xC8 && kind !== 0xCC) {
					out.h = (buf[i + 5] << 8) | buf[i + 6];
					out.w = (buf[i + 7] << 8) | buf[i + 8];
					break;
				}
				i += 2 + ((buf[i + 2] << 8) | buf[i + 3]);
			}
		} else if (n > 10 && buf[0] === 0x47 && buf[1] === 0x49) {
			// GIF
			out.w = buf[6] | (buf[7] << 8);
			out.h = buf[8] | (buf[9] << 8);
		}
	} catch (e) {}
	if (fd !== undefined) {
		try { fs.closeSync(fd); } catch (e2) {}
	}
	return out;
}

// Take one page out of a comic onto the disk. done(error, file)
function takePage(book, index, done) {
	var file = pageFile(book, index);
	if (stat(file)) {
		done("", file);
		return;
	}
	if (book.busy[index]) {
		// Already on its way out: answer when it lands.
		book.waiting[index] = book.waiting[index] || [];
		book.waiting[index].push(done);
		return;
	}
	book.busy[index] = true;
	// 7za writes the page under its own name, so each page gets a folder of
	// its own to arrive in, and is then given its number.
	var tmp = book.dir + "/t" + index;
	mkdirs(tmp);
	sevenZip(["e", "-y", "-o" + tmp, book.path, "--", book.pages[index]], function(code, out, err) {
		var error = "";
		var names = [];
		try {
			names = fs.readdirSync(tmp);
		} catch (e) {}
		if (code >= 2 || !names.length) {
			log("page " + index + " of " + book.path + ": " + (out + err).replace(/\s+/g, " ").substring(0, 300));
			error = "That page could not be read from the comic.";
		} else {
			try {
				fs.renameSync(tmp + "/" + names[0], file);
			} catch (e2) {
				error = "That page could not be read from the comic.";
			}
		}
		child_process.spawn("/bin/rm", ["-rf", tmp]);
		delete book.busy[index];
		var waiting = book.waiting[index] || [];
		delete book.waiting[index];
		done(error, file);
		for (var i = 0; i < waiting.length; i++) {
			waiting[i](error, file);
		}
	});
}

// Remove the pages far from the one being read.
function trimPages(book, index) {
	var names;
	try {
		names = fs.readdirSync(book.dir);
	} catch (e) {
		return;
	}
	for (var i = 0; i < names.length; i++) {
		var n = parseInt(names[i], 10);
		if (!isNaN(n) && /^\d+\./.test(names[i]) && Math.abs(n - index) > KEEP_PAGES / 2) {
			try { fs.unlinkSync(book.dir + "/" + names[i]); } catch (e2) {}
		}
	}
}

// {path} -> {key, count}: get a comic ready to read.
var openAssistant = function() {};
openAssistant.prototype.run = function(future) {
	var path = clean((this.controller.args || {}).path);
	if (!path) {
		return fail(future, "That comic is not on the TouchPad's storage.");
	}
	openBook(path, function(error, book) {
		if (error) {
			fail(future, error);
			return;
		}
		future.result = {returnValue: true, ok: true, key: book.key, count: book.pages.length};
	});
	return future;
};

// {path, index, ahead} -> {file, w, h, index}: one page as a picture file.
// ahead: how many of the following pages to get ready meanwhile.
var pageAssistant = function() {};
pageAssistant.prototype.run = function(future) {
	var args = this.controller.args || {};
	var path = clean(args.path);
	var index = Number(args.index) || 0;
	if (!path) {
		return fail(future, "That comic is not on the TouchPad's storage.");
	}
	openBook(path, function(error, book) {
		if (error) {
			fail(future, error);
			return;
		}
		index = Math.max(0, Math.min(book.pages.length - 1, index));
		takePage(book, index, function(error2, file) {
			if (error2) {
				fail(future, error2);
				return;
			}
			var size = pictureSize(file);
			future.result = {returnValue: true, ok: true, file: file, w: size.w, h: size.h, index: index,
				count: book.pages.length};
			trimPages(book, index);
			// The pages likely to be wanted next, one after another.
			var next = index + 1;
			var last = Math.min(book.pages.length - 1, index + (Number(args.ahead) || 0));
			(function more() {
				if (next <= last) {
					takePage(book, next++, more);
				}
			})();
		});
	});
	return future;
};

// {path} -> {cover}: a comic's first page, kept for the library.
var coverAssistant = function() {};
coverAssistant.prototype.run = function(future) {
	var path = clean((this.controller.args || {}).path);
	var st = path && stat(path);
	if (!st) {
		return fail(future, "That comic could not be found.");
	}
	var key = keyFor(path, st);
	var have = coverOf(key);
	if (have) {
		future.result = {returnValue: true, ok: true, cover: have, key: key};
		return future;
	}
	openBook(path, function(error, book) {
		if (error) {
			fail(future, error);
			return;
		}
		takePage(book, 0, function(error2, file) {
			if (error2) {
				fail(future, error2);
				return;
			}
			mkdirs(COVERS);
			var cover = COVERS + "/" + key + file.substring(file.lastIndexOf("."));
			// A copy, so the cover outlives the page cache.
			run("/bin/cp", [file, cover], "", function(code) {
				if (code !== 0) {
					fail(future, "The cover could not be kept.");
					return;
				}
				future.result = {returnValue: true, ok: true, cover: cover, key: key};
			});
		});
	});
	return future;
};

// {path} -> {}: delete a comic from the TouchPad, with its cover.
var removeAssistant = function() {};
removeAssistant.prototype.run = function(future) {
	var path = clean((this.controller.args || {}).path);
	var st = path && stat(path);
	if (!st || !st.isFile() || !COMIC.test(path)) {
		return fail(future, "That comic could not be found.");
	}
	var cover = coverOf(keyFor(path, st));
	try {
		fs.unlinkSync(path);
	} catch (e) {
		return fail(future, "The comic could not be deleted.");
	}
	if (cover) {
		try { fs.unlinkSync(cover); } catch (e2) {}
	}
	future.result = {returnValue: true, ok: true};
	return future;
};

// ---- the server ---------------------------------------------------------------

// What curl is told on its input, so the password is never on a command line.
function curlConfig(user, password) {
	if (!user && !password) {
		return "";
	}
	return "user = \"" + String(user || "").replace(/[\\"]/g, "\\$&") + ":" +
		String(password || "").replace(/[\\"]/g, "\\$&") + "\"\n";
}

function curlError(code, err) {
	if (/error: 401|error: 403/.test(err)) {
		return "The server refused the sign-in.";
	}
	if (/error: 404/.test(err)) {
		return "The server does not have that any more.";
	}
	if (code === 6 || code === 7 || code === 28) {
		return "The server could not be reached.";
	}
	if (code === 23) {
		return "The TouchPad's storage is full.";
	}
	return "That could not be fetched from the server.";
}

// {url, user, password} -> {text}: fetch a catalog page. (The app's own way of
// fetching cannot sign in to some servers.)
var feedAssistant = function() {};
feedAssistant.prototype.run = function(future) {
	var args = this.controller.args || {};
	var url = String(args.url || "");
	if (!/^https?:\/\//i.test(url)) {
		return fail(future, "That is not a web address.");
	}
	run(CURL, ["-K", "-", "-sS", "-L", "--fail", "-A", AGENT, "--max-time", "60", "-H", "Accept: application/atom+xml,application/xml;q=0.9,*/*;q=0.8", url],
		curlConfig(args.user, args.password), function(code, out, err) {
			if (code !== 0) {
				log("feed " + url + ": curl " + code + " " + err.replace(/\s+/g, " ").substring(0, 200));
				fail(future, curlError(code, err));
				return;
			}
			future.result = {returnValue: true, ok: true, text: out};
		});
	return future;
};

// {url, user, password} -> {cover}: fetch a cover picture, kept by its address.
var serverCoverAssistant = function() {};
serverCoverAssistant.prototype.run = function(future) {
	var args = this.controller.args || {};
	var url = String(args.url || "");
	if (!/^https?:\/\//i.test(url)) {
		return fail(future, "That is not a web address.");
	}
	var key = "s" + keyFor(url, {size: 0, mtime: new Date(0)});
	var file = COVERS + "/" + key + ".jpg";
	if (stat(file)) {
		future.result = {returnValue: true, ok: true, cover: file};
		return future;
	}
	mkdirs(COVERS);
	run(CURL, ["-K", "-", "-sS", "-L", "--fail", "-A", AGENT, "--max-time", "60", "-o", file + ".part", url],
		curlConfig(args.user, args.password), function(code, out, err) {
			if (code !== 0) {
				try { fs.unlinkSync(file + ".part"); } catch (e) {}
				fail(future, curlError(code, err));
				return;
			}
			try {
				fs.renameSync(file + ".part", file);
			} catch (e2) {
				fail(future, "The cover could not be kept.");
				return;
			}
			future.result = {returnValue: true, ok: true, cover: file};
		});
	return future;
};

// One download at a time; the app asks "progress" while it runs.
var download = null;
var downloadCount = 0;
var keepAwake = null;

function safeName(text) {
	// The storage is FAT: these cannot be in a name, nor a dot or space at its end.
	return String(text || "").replace(/[\\\/:*?"<>|\x00-\x1f]/g, "_").replace(/[. ]+$/, "").replace(/^\s+/, "").substring(0, 120) || "Untitled";
}

// {url, user, password, folder, name, ext} -> {id}: start fetching a comic into
// comics/<folder>/<name>.<ext>.
var downloadAssistant = function() {};
downloadAssistant.prototype.run = function(future) {
	var args = this.controller.args || {};
	var url = String(args.url || "");
	if (download && download.running) {
		return fail(future, "Another comic is still downloading.");
	}
	if (!/^https?:\/\//i.test(url)) {
		return fail(future, "That is not a web address.");
	}
	var dir = COMICS + (args.folder ? "/" + safeName(args.folder) : "");
	var ext = /^(cbz|cbr|cb7|cbt)$/.test(String(args.ext)) ? String(args.ext) : "cbz";
	var dest = dir + "/" + safeName(args.name) + "." + ext;
	mkdirs(dir);
	var part = dest + ".part";
	var heads = part + ".head";
	var job = {id: ++downloadCount, running: true, dest: dest, part: part, heads: heads, total: 0, error: "",
		cancelled: false, name: String(args.name || "")};
	download = job;
	log("download started: " + dest);
	job.child = run(CURL, ["-K", "-", "-sS", "-L", "--fail", "-A", AGENT, "-D", heads, "-o", part, url],
		curlConfig(args.user, args.password), function(code, out, err) {
			job.running = false;
			job.child = null;
			if (keepAwake) {
				clearInterval(keepAwake);
				keepAwake = null;
			}
			try { fs.unlinkSync(heads); } catch (e) {}
			if (code !== 0 || job.cancelled) {
				try { fs.unlinkSync(part); } catch (e2) {}
				job.error = job.cancelled ? "" : curlError(code, err);
				log("download ended: curl " + code + " " + err.replace(/\s+/g, " ").substring(0, 200));
				return;
			}
			try {
				fs.renameSync(part, dest);
				job.done = true;
			} catch (e3) {
				job.error = "The comic could not be saved.";
			}
			log("download finished: " + dest);
		});
	// The service is shut down when nobody has called it for a while; a long
	// download must outlive the app's card being closed, so it calls itself.
	keepAwake = setInterval(function() {
		try {
			IMPORTS.foundations.Comms.PalmCall.call(SERVICE, "progress", {});
		} catch (e) {}
	}, 30000);
	future.result = {returnValue: true, ok: true, id: job.id, dest: dest};
	return future;
};

// -> {id, running, bytes, total, dest, done, error, cancelled}
var progressAssistant = function() {};
progressAssistant.prototype.run = function(future) {
	if (!download) {
		future.result = {returnValue: true, ok: true, id: 0, running: false};
		return future;
	}
	var job = download;
	if (job.running && !job.total) {
		// The size the server announced, once its answer has begun.
		try {
			var m = fs.readFileSync(job.heads, "utf8").match(/content-length:\s*(\d+)/gi);
			if (m) {
				job.total = Number(/(\d+)/.exec(m[m.length - 1])[1]);
			}
		} catch (e) {}
	}
	var st = job.running ? stat(job.part) : null;
	future.result = {returnValue: true, ok: true, id: job.id, running: job.running, bytes: st ? st.size : 0,
		total: job.total, dest: job.dest, done: !!job.done, error: job.error, cancelled: job.cancelled,
		name: job.name};
	return future;
};

var cancelAssistant = function() {};
cancelAssistant.prototype.run = function(future) {
	if (download && download.running) {
		download.cancelled = true;
		try { download.child.kill(); } catch (e) {}
	}
	future.result = {returnValue: true, ok: true};
	return future;
};

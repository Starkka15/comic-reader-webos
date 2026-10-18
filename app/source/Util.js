/* Shared helpers: logging, settings, reading progress, picture addresses. */
var CR = CR || {};

CR.log = function(msg) {
	try { console.log("[comics] " + msg); } catch (e) {}
};

CR.banner = function(text) {
	try {
		enyo.windows.addBannerMessage(text, "{}");
	} catch (e) {
		CR.log(text);
	}
};

// ---- settings ----

CR.pref = function(key, fallback) {
	try {
		var v = localStorage.getItem("cr." + key);
		return v === null || v === undefined ? fallback : v;
	} catch (e) {
		return fallback;
	}
};

CR.setPref = function(key, value) {
	try {
		localStorage.setItem("cr." + key, String(value));
	} catch (e) {}
};

// A settings entry that holds an object.
CR.table = function(key) {
	try {
		return enyo.json.parse(CR.pref(key, "{}")) || {};
	} catch (e) {
		return {};
	}
};

CR.setTable = function(key, table) {
	CR.setPref(key, enyo.json.stringify(table));
};

// ---- how far each comic has been read: path -> {page, count, when} ----

CR.progress = null;

CR.progressOf = function(path) {
	if (!CR.progress) {
		CR.progress = CR.table("progress");
	}
	return CR.progress[path] || null;
};

CR.setProgress = function(path, page, count) {
	if (!CR.progress) {
		CR.progress = CR.table("progress");
	}
	CR.progress[path] = {page: page, count: count, when: new Date().getTime()};
	CR.setTable("progress", CR.progress);
};

CR.forgetProgress = function(path) {
	if (CR.progressOf(path)) {
		delete CR.progress[path];
		CR.setTable("progress", CR.progress);
	}
};

// ---- pictures ----

CR.fileUrl = function(path) {
	return "file://" + encodeURI(path).replace(/#/g, "%23").replace(/\?/g, "%3F");
};

// A picture file scaled by the system to fit a box, as an address. Scaling
// keeps a large scan from being loaded whole.
CR.scaledUrl = function(path, width, height) {
	return "/var/luna/data/extractfs" + encodeURIComponent(path).replace(/'/g, "%27") +
		":0:0:" + Math.round(width) + ":" + Math.round(height) + ":3";
};

// ---- watching the system's picture scaler ----
// Every cover and every large page comes through it. It can stop answering
// (seen once, after the Photos app had kept it busy): pictures then never
// arrive and no error is given, until the TouchPad is restarted.

CR.SCALER_WAIT = 30000;  // longer than a screen of large covers takes
CR.scalerStuck = null;   // set by the app: called once if the scaler gives no answer
CR.scalerTold = false;
CR.scalerProbe = null;

// A scaled picture was asked for. Returns what CR.scalerAnswered takes.
CR.scalerAsked = function() {
	return setTimeout(function() {
		if (!CR.scalerTold) {
			CR.scalerTold = true;
			CR.log("the picture scaler gave no answer in " + CR.SCALER_WAIT + " ms");
			if (CR.scalerStuck) {
				CR.scalerStuck();
			}
		}
	}, CR.SCALER_WAIT);
};

CR.scalerAnswered = function(timer) {
	clearTimeout(timer);
};

// Watch a scaled picture that is shown as a background, which has no events
// of its own, by loading it here as well. One at a time.
CR.scalerCheck = function(url) {
	if (CR.scalerProbe || CR.scalerTold) {
		return;
	}
	var img = new Image();
	var timer = CR.scalerAsked();
	CR.scalerProbe = img;
	img.onload = img.onerror = function() {
		img.onload = img.onerror = null;
		CR.scalerAnswered(timer);
		// It works: no need to look again for a while.
		setTimeout(function() {
			CR.scalerProbe = null;
		}, 10000);
	};
	img.src = url;
};

// ---- words ----

CR.count = function(n, one, many) {
	return n + " " + (n === 1 ? one : (many || one + "s"));
};

CR.size = function(bytes) {
	if (bytes < 1024 * 1024) {
		return Math.max(1, Math.round(bytes / 1024)) + " KB";
	}
	var mb = bytes / (1024 * 1024);
	return (mb >= 100 ? Math.round(mb) : mb.toFixed(1)) + " MB";
};

// Order names the way people do: "Issue 2" before "Issue 10".
CR.natural = function(a, b) {
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
};

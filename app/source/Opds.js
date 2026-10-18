/* Reading OPDS catalogs: the feeds of entries that comic and book servers
	(this project's own server, Komga, Kavita, Calibre and others) publish for
	reader apps. Version 1.2, which is Atom XML. */
CR.Opds = {
	ACQUIRE: "http://opds-spec.org/acquisition",
	// Kinds of file this app can read, best first: [what the server calls it, the ending to save it with]
	TYPES: [
		[/comicbook\+zip|x-cbz/i, "cbz"],
		[/comicbook-rar|x-cbr/i, "cbr"],
		[/x-cb7/i, "cb7"],
		[/x-cbt/i, "cbt"],
		[/^application\/zip/i, "cbz"],
		[/x-rar|vnd\.rar/i, "cbr"]
	],

	// An address found in a feed, made whole against the feed's own address.
	resolve: function(base, href) {
		href = String(href || "");
		if (/^[a-z][a-z0-9+.\-]*:/i.test(href)) {
			return href;
		}
		var origin = /^([a-z]+:\/\/[^\/?#]+)/i.exec(base);
		origin = origin ? origin[1] : "";
		if (href.substring(0, 2) === "//") {
			return base.substring(0, base.indexOf(":") + 1) + href;
		}
		if (href.charAt(0) === "/") {
			return origin + href;
		}
		var path = base.replace(/[?#].*$/, "");
		path = path.substring(0, path.lastIndexOf("/") + 1);
		if (path.length <= origin.length) {
			path = origin + "/";
		}
		return path + href;
	},

	// The text of the first child element with a name, or "".
	childText: function(node, name) {
		for (var c = node.firstChild; c; c = c.nextSibling) {
			if (c.nodeType === 1 && CR.Opds.localName(c) === name) {
				return (c.textContent || "").replace(/^\s+|\s+$/g, "");
			}
		}
		return "";
	},

	// An element's name without any "prefix:" in front.
	localName: function(node) {
		var n = node.localName || node.nodeName || "";
		return n.substring(n.indexOf(":") + 1);
	},

	// The links directly inside a feed or an entry: [{rel, type, href, title}]
	links: function(node, base) {
		var out = [];
		for (var c = node.firstChild; c; c = c.nextSibling) {
			if (c.nodeType === 1 && CR.Opds.localName(c) === "link" && c.getAttribute("href")) {
				out.push({
					rel: c.getAttribute("rel") || "",
					type: c.getAttribute("type") || "",
					href: CR.Opds.resolve(base, c.getAttribute("href")),
					title: c.getAttribute("title") || ""
				});
			}
		}
		return out;
	},

	// Turn a feed's text into {title, entries, next, search}, or null when it is not a feed.
	//   entry that leads on:  {kind: "more", title, sub, url}
	//   entry that is a comic: {kind: "comic", id, title, author, sub, tags, cover, url, ext}
	//   (a book this app cannot read has url: "" and says why in sub)
	parse: function(text, base) {
		var doc;
		try {
			doc = new DOMParser().parseFromString(text, "text/xml");
		} catch (e) {
			return null;
		}
		var root = doc && doc.documentElement;
		if (!root || CR.Opds.localName(root) !== "feed") {
			return null;
		}
		var feed = {title: CR.Opds.childText(root, "title"), entries: [], next: "", search: ""};
		var links = CR.Opds.links(root, base);
		var i;
		for (i = 0; i < links.length; i++) {
			if (links[i].rel === "next") {
				feed.next = links[i].href;
			} else if (links[i].rel === "search") {
				feed.search = links[i].href;
				feed.searchType = links[i].type;
			}
		}
		for (var c = root.firstChild; c; c = c.nextSibling) {
			if (c.nodeType === 1 && CR.Opds.localName(c) === "entry") {
				var entry = CR.Opds.entry(c, base);
				if (entry) {
					feed.entries.push(entry);
				}
			}
		}
		return feed;
	},

	entry: function(node, base) {
		var links = CR.Opds.links(node, base);
		var title = CR.Opds.childText(node, "title") || "Untitled";
		var summary = CR.Opds.childText(node, "content") || CR.Opds.childText(node, "summary");
		var cover = "";
		var thumb = "";
		var follow = "";
		var files = [];
		var i;
		for (i = 0; i < links.length; i++) {
			var l = links[i];
			if (l.rel.indexOf(CR.Opds.ACQUIRE) === 0) {
				files.push(l);
			} else if (l.rel === "http://opds-spec.org/image/thumbnail" || l.rel === "http://opds-spec.org/thumbnail") {
				thumb = l.href;
			} else if (l.rel === "http://opds-spec.org/image" || l.rel === "http://opds-spec.org/cover") {
				cover = l.href;
			} else if (!follow && (/opds-catalog|atom\+xml/i.test(l.type) || l.rel === "subsection")) {
				follow = l.href;
			}
		}
		if (!files.length) {
			return follow ? {kind: "more", title: title, sub: summary, url: follow} : null;
		}
		// A comic: pick the kind of file this app reads best.
		var url = "";
		var ext = "";
		for (var t = 0; t < CR.Opds.TYPES.length && !url; t++) {
			for (i = 0; i < files.length; i++) {
				if (CR.Opds.TYPES[t][0].test(files[i].type)) {
					url = files[i].href;
					ext = CR.Opds.TYPES[t][1];
					break;
				}
			}
		}
		var author = "";
		var tags = [];
		for (var c = node.firstChild; c; c = c.nextSibling) {
			if (c.nodeType !== 1) {
				continue;
			}
			if (CR.Opds.localName(c) === "author" && !author) {
				author = CR.Opds.childText(c, "name");
			} else if (CR.Opds.localName(c) === "category") {
				var term = c.getAttribute("label") || c.getAttribute("term");
				if (term) {
					tags.push(term);
				}
			}
		}
		return {
			kind: "comic", id: CR.Opds.childText(node, "id") || url, title: title, author: author,
			sub: url ? author : "Not a comic this app can read", summary: summary, tags: tags,
			cover: thumb || cover, url: url, ext: ext
		};
	},

	// The address to search with, from a search description document's text.
	searchTemplate: function(text, base) {
		var doc;
		try {
			doc = new DOMParser().parseFromString(text, "text/xml");
		} catch (e) {
			return "";
		}
		var urls = doc ? doc.getElementsByTagName("Url") : [];
		var best = "";
		for (var i = 0; i < urls.length; i++) {
			var template = urls[i].getAttribute("template");
			if (template && (!best || /atom/i.test(urls[i].getAttribute("type") || ""))) {
				best = CR.Opds.resolve(base, template);
			}
		}
		return best;
	}
};

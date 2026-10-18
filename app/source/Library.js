/* The comics on the TouchPad. */
enyo.kind({
	name: "CR.Library",
	kind: enyo.VFlexBox,
	className: "cr-library",
	events: {
		// A comic was tapped: (comic) with {path, name, folder, ...}
		onRead: "",
		// The list of comics was read afresh: (comics)
		onScanned: ""
	},
	components: [
		{name: "svc", kind: "CR.Service"},
		{kind: "HFlexBox", align: "center", className: "cr-tools", components: [
			{name: "search", kind: "SearchInput", hint: "Search your comics", flex: 1,
				onchange: "searchChanged", onCancel: "searchChanged",
				autoCapitalize: "lowercase", autocorrect: false, spellcheck: false, changeOnInput: true},
			{name: "sort", kind: "Button", caption: "Sort", className: "enyo-button-dark", onclick: "sortClick"}
		]},
		{name: "note", className: "cr-note", showing: false},
		{name: "shelf", kind: "CR.Shelf", flex: 1, onPick: "picked", onHold: "held", onNeedCover: "needCover"},
		// lazy: false, so the menus' items exist before they are first opened.
		{name: "sortMenu", kind: "Menu", lazy: false, components: [
			{caption: "By artist", order: "artist", onclick: "sortPicked"},
			{caption: "By title", order: "title", onclick: "sortPicked"},
			{caption: "Recently read", order: "read", onclick: "sortPicked"},
			{caption: "Recently added", order: "added", onclick: "sortPicked"}
		]},
		{name: "heldMenu", kind: "Menu", lazy: false, components: [
			{caption: "Start From the First Page", onclick: "restart"},
			{caption: "Mark as Unread", onclick: "markUnread"},
			{caption: "Delete From This TouchPad", onclick: "askDelete"}
		]},
		{name: "deleteDialog", kind: "ModalDialog", lazy: false, caption: "Delete this comic?", components: [
			{name: "deleteText", className: "cr-dialog-text"},
			{kind: "HFlexBox", components: [
				{kind: "Button", caption: "Cancel", flex: 1, onclick: "cancelDelete"},
				{kind: "Button", caption: "Delete", flex: 1, className: "enyo-button-negative", onclick: "confirmDelete"}
			]}
		]}
	],

	create: function() {
		this.inherited(arguments);
		this.all = [];
		this.order = CR.pref("order", "artist");
		this.wanted = [];        // comics waiting for a cover
		this.fetching = false;
	},

	// Read the list of comics from the storage again.
	rescan: function() {
		var self = this;
		if (!this.all.length) {
			this.showNote("Looking for comics…");
		}
		this.$.svc.ask("scan", {}, function(r) {
			if (!r.ok) {
				self.showNote(r.error);
				return;
			}
			var comics = r.comics;
			for (var i = 0; i < comics.length; i++) {
				var c = comics[i];
				c.title = c.name;
				c.sub = c.folder === "comics" || c.folder === "internal" ? "" : c.folder;
				c.key2 = (c.sub + " " + c.name).toLowerCase();
			}
			self.all = comics;
			self.arrange(true);
			self.doScanned(comics);
		});
	},

	showNote: function(text) {
		this.$.note.setContent(text);
		this.$.note.setShowing(!!text);
	},

	// The words under a cover that say how far it has been read.
	badgeFor: function(c) {
		var p = CR.progressOf(c.path);
		if (!p) {
			return "";
		}
		return p.page >= p.count - 1 ? "Read" : "Page " + (p.page + 1) + " of " + p.count;
	},

	// Filter, sort and show.
	arrange: function(toTop) {
		var words = String(this.$.search.getValue() || "").toLowerCase();
		var order = this.order;
		var out = [];
		for (var i = 0; i < this.all.length; i++) {
			var c = this.all[i];
			if (!words || c.key2.indexOf(words) >= 0) {
				c.badge = this.badgeFor(c);
				var p = CR.progressOf(c.path);
				c.readWhen = p ? p.when : 0;
				out.push(c);
			}
		}
		out.sort(function(a, b) {
			if (order === "read" && a.readWhen !== b.readWhen) {
				return b.readWhen - a.readWhen;
			}
			if (order === "added" && a.modified !== b.modified) {
				return b.modified - a.modified;
			}
			if (order === "title") {
				return CR.natural(a.name.toLowerCase(), b.name.toLowerCase());
			}
			return CR.natural(a.key2, b.key2);
		});
		this.showNote(out.length ? "" : (this.all.length ? "No comics match." :
			"No comics on this TouchPad yet. Copy .cbz or .cbr files to the \"comics\" folder, or get some from a server."));
		this.$.shelf.setItems(out, !toTop);
	},

	// Show reading progress afresh, after the reader was used.
	updateBadges: function() {
		this.arrange(this.order === "read");
	},

	searchChanged: function() {
		this.arrange(true);
	},

	sortClick: function() {
		this.$.sortMenu.openAroundControl(this.$.sort);
	},

	sortPicked: function(inSender) {
		this.order = inSender.order;
		CR.setPref("order", this.order);
		this.arrange(true);
	},

	picked: function(inSender, comic) {
		this.doRead(comic);
	},

	// ---- covers, made one at a time ----

	needCover: function(inSender, comic) {
		this.wanted.push(comic);
		this.nextCover();
	},

	nextCover: function() {
		var self = this;
		if (this.fetching || !this.wanted.length) {
			return;
		}
		// The newest asked for first: those are the ones on screen now.
		var comic = this.wanted.pop();
		this.fetching = true;
		this.$.svc.ask("cover", {path: comic.path}, function(r) {
			self.fetching = false;
			if (r.ok) {
				comic.cover = r.cover;
				self.$.shelf.redrawSoon();
			} else {
				comic.noCover = true;
			}
			self.nextCover();
		});
	},

	// ---- holding a comic ----

	held: function(inSender, comic) {
		this.heldComic = comic;
		this.$.heldMenu.openAtCenter();
	},

	restart: function() {
		if (this.heldComic) {
			CR.forgetProgress(this.heldComic.path);
			this.doRead(this.heldComic);
		}
	},

	markUnread: function() {
		if (this.heldComic) {
			CR.forgetProgress(this.heldComic.path);
			this.updateBadges();
		}
	},

	askDelete: function() {
		if (this.heldComic) {
			this.$.deleteText.setContent("\"" + this.heldComic.name + "\" will be removed from this TouchPad. This cannot be undone.");
			this.$.deleteDialog.openAtCenter();
		}
	},

	cancelDelete: function() {
		this.$.deleteDialog.close();
	},

	confirmDelete: function() {
		var self = this;
		var comic = this.heldComic;
		this.$.deleteDialog.close();
		this.$.svc.ask("remove", {path: comic.path}, function(r) {
			if (!r.ok) {
				CR.banner(r.error);
				return;
			}
			CR.forgetProgress(comic.path);
			self.rescan();
		});
	}
});

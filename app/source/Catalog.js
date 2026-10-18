/* A comic server, browsed through its OPDS catalog: sign in, follow its lists,
	search, and pick comics to download. */
enyo.kind({
	name: "CR.Catalog",
	kind: enyo.VFlexBox,
	className: "cr-catalog",
	published: {
		// The comics already on the TouchPad, by path: {path: true}
		local: null
	},
	events: {
		// Download was tapped for a comic: (entry)
		onDownload: "",
		// Read was tapped for a comic that is on the TouchPad: (path)
		onReadLocal: ""
	},
	components: [
		{name: "svc", kind: "CR.Service"},
		{name: "views", kind: "Pane", flex: 1, transitionKind: "enyo.transitions.Simple", components: [
			// Signing in.
			{name: "signin", kind: "Scroller", accelerated: false, components: [
				{className: "cr-signin", components: [
					{content: "Connect to a comic server", className: "cr-signin-title"},
					{content: "Any server with an OPDS catalog works: this project's own server, Komga, Kavita, Calibre and others.",
						className: "cr-signin-text"},
					{kind: "RowGroup", caption: "Catalog address", components: [
						{name: "address", kind: "Input", hint: "https://example.com/opds", inputType: "url",
							autoCapitalize: "lowercase", autocorrect: false, spellcheck: false}
					]},
					{kind: "RowGroup", caption: "Sign-in, if the server asks for one", components: [
						{name: "user", kind: "Input", hint: "Name", autoCapitalize: "lowercase", autocorrect: false, spellcheck: false},
						{name: "password", kind: "PasswordInput", hint: "Password"}
					]},
					{name: "problem", className: "cr-problem", showing: false},
					{name: "connect", kind: "Button", caption: "Connect", className: "enyo-button-affirmative", onclick: "connectClick"}
				]}
			]},
			// Browsing.
			{name: "browse", kind: "VFlexBox", components: [
				{kind: "HFlexBox", align: "center", className: "cr-tools", components: [
					{name: "up", kind: "Button", caption: "Back", className: "enyo-button-dark", onclick: "goUp"},
					{name: "where", flex: 1, className: "cr-where"},
					{name: "search", kind: "SearchInput", hint: "Search the server", className: "cr-server-search",
						onchange: "searchChanged", onCancel: "searchCancelled", changeOnInput: false,
						autoCapitalize: "lowercase", autocorrect: false, spellcheck: false, showing: false}
				]},
				{name: "note", className: "cr-note", showing: false},
				{name: "kinds", kind: "Pane", flex: 1, transitionKind: "enyo.transitions.Simple", components: [
					{name: "list", kind: "VirtualList", onSetupRow: "setupRow", components: [
						{name: "row", kind: "Item", layoutKind: "HFlexLayout", align: "center", tapHighlight: true,
							className: "cr-list-row", onclick: "rowClick", components: [
								{name: "rowTitle", flex: 1, className: "cr-list-title"},
								{name: "rowSub", className: "cr-list-sub"}
							]}
					]},
					{name: "shelf", kind: "CR.Shelf", onPick: "comicPicked", onNeedCover: "needCover", onEnd: "moreWanted"}
				]}
			]}
		]},
		{name: "detail", kind: "ModalDialog", lazy: false, caption: "", components: [
			{kind: "HFlexBox", components: [
				{name: "detailArt", className: "cr-detail-art"},
				{kind: "VFlexBox", flex: 1, components: [
					{name: "detailAuthor", className: "cr-detail-author"},
					{name: "detailSummary", className: "cr-detail-text"},
					{name: "detailTags", className: "cr-detail-tags"}
				]}
			]},
			{kind: "HFlexBox", className: "cr-dialog-buttons", components: [
				{kind: "Button", caption: "Close", flex: 1, onclick: "closeDetail"},
				{name: "detailGet", kind: "Button", caption: "Download", flex: 1, className: "enyo-button-affirmative", onclick: "detailAction"}
			]}
		]}
	],

	create: function() {
		this.inherited(arguments);
		this.local = this.local || {};
		this.trail = [];         // the feeds gone through: [{url, title, entries, next, comics}]
		this.wanted = [];        // entries waiting for a cover
		this.fetching = 0;
		this.loads = 0;
		this.searchUrl = "";
		// Software scrolling, as on the shelf (see Shelf.js).
		this.$.list.$.scroller.setAccelerated(false);
	},

	rendered: function() {
		this.inherited(arguments);
		if (!this.begun) {
			this.begun = true;
			this.$.address.setValue(CR.pref("server", ""));
			this.$.user.setValue(CR.pref("user", ""));
			this.$.password.setValue(CR.pref("password", ""));
		}
	},

	// ---- for the app ----

	// Called when the server side is shown: carry on where it was, or sign in.
	begin: function() {
		if (this.trail.length) {
			return;
		}
		if (CR.pref("server", "")) {
			this.connect(CR.pref("server", ""), CR.pref("user", ""), CR.pref("password", ""), true);
		} else {
			this.$.views.selectViewByName("signin");
		}
	},

	// Forget the server and show the sign-in again.
	signOut: function() {
		CR.setPref("server", "");
		CR.setPref("password", "");
		this.$.password.setValue("");
		this.trail = [];
		this.searchUrl = "";
		this.$.views.selectViewByName("signin");
	},

	isSignedIn: function() {
		return this.trail.length > 0;
	},

	// Go back one list. Returns false when there is nowhere to go back to.
	goBack: function() {
		if (this.trail.length > 1) {
			this.goUp();
			return true;
		}
		return false;
	},

	localChanged: function() {
		this.local = this.local || {};
	},

	// The path a comic was downloaded to, if it is still on the TouchPad.
	pathOf: function(entry) {
		var path = CR.table("downloads")[entry.id];
		return path && this.local[path] ? path : "";
	},

	// ---- signing in ----

	showProblem: function(text) {
		this.$.problem.setContent(text);
		this.$.problem.setShowing(!!text);
	},

	connectClick: function() {
		var address = String(this.$.address.getValue() || "").replace(/^\s+|\s+$/g, "");
		if (!address) {
			this.showProblem("Type the catalog's address first.");
			return;
		}
		if (!/^[a-z]+:\/\//i.test(address)) {
			address = "https://" + address;
		}
		this.connect(address, this.$.user.getValue(), this.$.password.getValue(), false);
	},

	// Fetch the catalog's first page; on success remember the server.
	connect: function(address, user, password, quiet) {
		var self = this;
		this.auth = {user: user || "", password: password || ""};
		this.showProblem("");
		this.$.connect.setCaption("Connecting…");
		this.$.connect.setDisabled(true);
		function done(feed, url, error) {
			self.$.connect.setCaption("Connect");
			self.$.connect.setDisabled(false);
			if (!feed) {
				self.$.views.selectViewByName("signin");
				self.showProblem(error);
				return;
			}
			CR.setPref("server", url);
			CR.setPref("user", self.auth.user);
			CR.setPref("password", self.auth.password);
			self.trail = [];
			self.searchUrl = "";
			self.$.views.selectViewByName("browse");
			self.showFeed(feed, url);
			self.findSearch(feed, url);
		}
		this.fetchFeed(address, function(feed, error) {
			if (feed || /\/opds\/?$/i.test(address) || quiet) {
				done(feed, address, error);
				return;
			}
			// Many servers keep their catalog at /opds.
			var guess = address.replace(/\/+$/, "") + "/opds";
			self.fetchFeed(guess, function(feed2) {
				done(feed2, guess, error);
			});
		});
	},

	// ---- feeds ----

	// Fetch and read a feed. done(feed or null, error)
	fetchFeed: function(url, done) {
		this.$.svc.ask("feed", {url: url, user: this.auth.user, password: this.auth.password}, function(r) {
			if (!r.ok) {
				done(null, r.error);
				return;
			}
			var feed = CR.Opds.parse(r.text, url);
			done(feed, feed ? "" : "That address did not answer with a catalog.");
		});
	},

	// Learn how this server is searched, from its first page.
	findSearch: function(feed, url) {
		var self = this;
		if (!feed.search) {
			return;
		}
		if (/atom/i.test(feed.searchType || "") && feed.search.indexOf("{searchTerms}") >= 0) {
			this.searchUrl = feed.search;
			this.$.search.show();
			return;
		}
		this.$.svc.ask("feed", {url: feed.search, user: this.auth.user, password: this.auth.password}, function(r) {
			var template = r.ok ? CR.Opds.searchTemplate(r.text, feed.search) : "";
			if (template) {
				self.searchUrl = template;
				self.$.search.show();
			}
		});
	},

	// Add a feed to the trail and show it.
	showFeed: function(feed, url) {
		var comics = false;
		for (var i = 0; i < feed.entries.length; i++) {
			if (feed.entries[i].kind === "comic") {
				comics = true;
				break;
			}
		}
		this.trail.push({url: url, title: feed.title, entries: feed.entries, next: feed.next, comics: comics});
		this.showTop(true);
	},

	// Show the feed at the end of the trail.
	showTop: function(toTop) {
		var top = this.trail[this.trail.length - 1];
		this.$.up.setShowing(this.trail.length > 1);
		this.$.where.setContent(top.title || "Catalog");
		this.$.note.setContent(top.entries.length ? "" : "Nothing here.");
		this.$.note.setShowing(!top.entries.length);
		this.$.kinds.selectViewByName(top.comics ? "shelf" : "list");
		if (top.comics) {
			this.dress(top.entries);
			this.$.shelf.setItems(top.entries, !toTop);
		} else if (this.$.list.hasNode()) {
			if (toTop) {
				this.$.list.punt();
			} else {
				this.$.list.refresh();
			}
		}
	},

	// Say under each comic whether it is already on the TouchPad.
	dress: function(entries) {
		for (var i = 0; i < entries.length; i++) {
			if (entries[i].kind === "comic") {
				entries[i].badge = this.pathOf(entries[i]) ? "On this TouchPad" : "";
				// The shelf shows a picture file; the cover's address is kept beside it.
				if (entries[i].coverUrl === undefined) {
					entries[i].coverUrl = entries[i].cover;
					entries[i].cover = "";
					entries[i].noCover = !entries[i].coverUrl;
				}
			}
		}
	},

	// Show afresh which comics are on the TouchPad (after a download or a delete).
	updateBadges: function() {
		var top = this.trail[this.trail.length - 1];
		if (top && top.comics) {
			this.dress(top.entries);
			this.$.shelf.redrawSoon();
		}
	},

	openFeed: function(url) {
		var self = this;
		var turn = ++this.loads;
		this.$.note.setContent("Loading…");
		this.$.note.show();
		this.fetchFeed(url, function(feed, error) {
			if (turn !== self.loads) {
				return;
			}
			if (!feed) {
				self.$.note.hide();
				CR.banner(error);
				return;
			}
			self.showFeed(feed, url);
		});
	},

	goUp: function() {
		if (this.trail.length > 1) {
			this.loads++;
			this.trail.pop();
			this.showTop(true);
		}
	},

	// The end of a list of comics is on screen: fetch its next page.
	moreWanted: function() {
		var self = this;
		var top = this.trail[this.trail.length - 1];
		if (!top || !top.next || top.loading) {
			return;
		}
		top.loading = true;
		this.fetchFeed(top.next, function(feed) {
			top.loading = false;
			if (!feed) {
				return;
			}
			top.entries = top.entries.concat(feed.entries);
			top.next = feed.next;
			if (top === self.trail[self.trail.length - 1]) {
				self.dress(top.entries);
				self.$.shelf.setItems(top.entries, true);
			}
		});
	},

	searchChanged: function() {
		var words = String(this.$.search.getValue() || "").replace(/^\s+|\s+$/g, "");
		if (words && this.searchUrl) {
			// One search at a time on the trail: a new one replaces the last.
			if (this.trail.length > 1 && this.trail[this.trail.length - 1].search) {
				this.trail.pop();
			}
			var self = this;
			var turn = ++this.loads;
			this.$.note.setContent("Searching…");
			this.$.note.show();
			this.fetchFeed(this.searchUrl.replace("{searchTerms}", encodeURIComponent(words)).replace(/\{[^}]*\??\}/g, ""), function(feed, error) {
				if (turn !== self.loads) {
					return;
				}
				if (!feed) {
					self.$.note.hide();
					CR.banner(error);
					return;
				}
				feed.title = "Search: " + words;
				self.showFeed(feed, self.searchUrl);
				self.trail[self.trail.length - 1].search = true;
			});
		}
	},

	searchCancelled: function() {
		if (this.trail.length > 1 && this.trail[this.trail.length - 1].search) {
			this.goUp();
		}
	},

	// ---- rows of a list that leads on ----

	setupRow: function(inSender, inIndex) {
		var top = this.trail[this.trail.length - 1];
		var e = top && !top.comics ? top.entries[inIndex] : null;
		if (!e) {
			return false;
		}
		this.$.rowTitle.setContent(e.title);
		this.$.rowSub.setContent(e.sub || "");
		return true;
	},

	rowClick: function(inSender, inEvent) {
		var top = this.trail[this.trail.length - 1];
		var e = top ? top.entries[inEvent.rowIndex] : null;
		if (e && e.url) {
			this.openFeed(e.url);
		}
	},

	// ---- covers, fetched two at a time ----

	needCover: function(inSender, entry) {
		this.wanted.push(entry);
		this.nextCover();
	},

	nextCover: function() {
		var self = this;
		while (this.fetching < 2 && this.wanted.length) {
			// The newest asked for first: those are the ones on screen now.
			(function(entry) {
				self.fetching++;
				self.$.svc.ask("serverCover", {url: entry.coverUrl, user: self.auth.user, password: self.auth.password}, function(r) {
					self.fetching--;
					if (r.ok) {
						entry.cover = r.cover;
						self.$.shelf.redrawSoon();
					} else {
						entry.noCover = true;
					}
					self.nextCover();
				});
			})(this.wanted.pop());
		}
	},

	// ---- one comic ----

	comicPicked: function(inSender, entry) {
		if (entry.kind !== "comic") {
			if (entry.url) {
				this.openFeed(entry.url);
			}
			return;
		}
		this.picked = entry;
		var have = this.pathOf(entry);
		this.$.detail.setCaption(entry.title);
		this.$.detailArt.applyStyle("background-image", entry.cover ? "url('" + CR.scaledUrl(entry.cover, 360, 504) + "')" : null);
		this.$.detailAuthor.setContent(entry.author || "");
		this.$.detailSummary.setContent(entry.summary && entry.summary !== entry.author ? entry.summary : "");
		this.$.detailTags.setContent(entry.tags.length ? entry.tags.join(" · ") : "");
		this.$.detailGet.setCaption(have ? "Read" : "Download");
		this.$.detailGet.setDisabled(!have && !entry.url);
		this.$.detail.openAtCenter();
	},

	closeDetail: function() {
		this.$.detail.close();
	},

	detailAction: function() {
		var entry = this.picked;
		var have = entry ? this.pathOf(entry) : "";
		this.$.detail.close();
		if (have) {
			this.doReadLocal(have);
		} else if (entry && entry.url) {
			this.doDownload(entry);
		}
	}
});

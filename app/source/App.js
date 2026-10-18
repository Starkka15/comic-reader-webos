/* The app: the comics on the TouchPad or a server's, and the reader. */
enyo.kind({
	name: "CR.App",
	kind: enyo.VFlexBox,
	className: "cr-app",
	components: [
		{kind: "ApplicationEvents", onBack: "backGesture", onWindowParamsChange: "windowParamsChanged"},
		{name: "svc", kind: "CR.Service"},
		{name: "pane", kind: "Pane", flex: 1, transitionKind: "enyo.transitions.Simple", components: [
			{name: "main", kind: "VFlexBox", components: [
				{kind: "HFlexBox", align: "center", className: "cr-bar", components: [
					{content: "Comic Reader", className: "cr-bar-title"},
					{kind: "RadioGroup", name: "side", className: "cr-sides", value: "library", onChange: "sideChanged", components: [
						{caption: "On This TouchPad", value: "library"},
						{caption: "Server", value: "server"}
					]},
					{flex: 1}
				]},
				{name: "sides", kind: "Pane", flex: 1, transitionKind: "enyo.transitions.Simple", components: [
					{name: "library", kind: "CR.Library", onRead: "readComic", onScanned: "scanned"},
					{name: "catalog", kind: "CR.Catalog", onDownload: "queueDownload", onReadLocal: "readPath"}
				]},
				// Shown while a comic is downloading.
				{name: "getting", kind: "HFlexBox", align: "center", className: "cr-getting", showing: false, components: [
					{kind: "VFlexBox", flex: 1, components: [
						{name: "gettingText", className: "cr-getting-text"},
						{name: "gettingBar", kind: "ProgressBar", animatePosition: false}
					]},
					{kind: "Button", caption: "Cancel", className: "enyo-button-dark", onclick: "cancelDownload"}
				]}
			]},
			{name: "reader", kind: "CR.Reader", onClose: "closeReader"}
		]},
		{kind: "AppMenu", lazy: false, components: [
			{caption: "Look for Comics Again", onclick: "rescan"},
			{name: "signOutItem", caption: "Sign Out of the Server", onclick: "signOut"}
		]},
		{name: "stuckDialog", kind: "ModalDialog", lazy: false, caption: "Restart Your TouchPad", components: [
			{className: "cr-dialog-text", content: "The part of the TouchPad that prepares pictures has stopped responding, " +
				"so covers and pages cannot be shown. Restarting the TouchPad fixes this: open Device Info in Settings, " +
				"tap Reset Options, then Restart."},
			{kind: "Button", caption: "OK", onclick: "closeStuck"}
		]}
	],

	create: function() {
		this.inherited(arguments);
		CR.scalerStuck = enyo.bind(this, "scalerStuck");
		this.queue = [];         // comics waiting to be downloaded
		this.getting = null;     // the one downloading now
		this.local = {};
	},

	rendered: function() {
		this.inherited(arguments);
		if (!this.begun) {
			this.begun = true;
			this.$.library.rescan();
			this.launched(enyo.windowParams || {});
			// A download may still be running from before the app was closed.
			this.watchDownload();
		}
	},

	windowParamsChanged: function() {
		this.launched(enyo.windowParams || {});
	},

	// Launched with a comic to open, as the system does when one is tapped in
	// another app: {target: "file:///media/internal/comics/a.cbz"}. A launch can
	// also name the server to use: {server: "https://...", user: "...", password: "..."}.
	launched: function(params) {
		if (params.server) {
			CR.setPref("server", String(params.server));
			CR.setPref("user", String(params.user || ""));
			CR.setPref("password", String(params.password || ""));
		}
		if (params.side === "server") {
			this.$.side.setValue("server");
			this.sideChanged();
		}
		var target = params.target || params.file;
		if (!target) {
			return;
		}
		var path = String(target).replace(/^file:\/\//, "");
		if (/%[0-9a-f]{2}/i.test(path) && path.indexOf(" ") < 0) {
			try {
				path = decodeURIComponent(path);
			} catch (e) {}
		}
		this.readPath(this, path);
	},

	// Pictures have stopped arriving; only a restart of the TouchPad brings them back.
	scalerStuck: function() {
		this.$.reader.scalerStuck();
		this.$.stuckDialog.openAtCenter();
	},

	closeStuck: function() {
		this.$.stuckDialog.close();
	},

	// ---- the two sides ----

	sideChanged: function() {
		var server = this.$.side.getValue() === "server";
		this.$.sides.selectViewByName(server ? "catalog" : "library");
		if (server) {
			this.$.catalog.begin();
		}
	},

	rescan: function() {
		this.$.library.rescan();
	},

	signOut: function() {
		this.$.catalog.signOut();
	},

	// The library read the storage: tell the server side what is on the TouchPad.
	scanned: function(inSender, comics) {
		var local = {};
		for (var i = 0; i < comics.length; i++) {
			local[comics[i].path] = true;
		}
		this.local = local;
		this.$.catalog.setLocal(local);
		this.$.catalog.updateBadges();
	},

	// ---- reading ----

	readComic: function(inSender, comic) {
		this.openReader(comic.path, comic.name);
	},

	readPath: function(inSender, path) {
		var name = path.substring(path.lastIndexOf("/") + 1).replace(/\.[a-z0-9]+$/i, "");
		this.openReader(path, name);
	},

	openReader: function(path, name) {
		enyo.setFullScreen(true);
		this.$.pane.selectViewByName("reader");
		this.$.reader.openComic(path, name);
	},

	closeReader: function() {
		enyo.setFullScreen(false);
		this.$.reader.closeComic();
		this.$.pane.selectViewByName("main");
		this.$.library.updateBadges();
	},

	backGesture: function(inSender, inEvent) {
		if (this.$.pane.getViewName() === "reader") {
			this.closeReader();
		} else if (this.$.side.getValue() === "server" && this.$.catalog.goBack()) {
			// went back one list
		} else {
			return;
		}
		inEvent.preventDefault();
		return true;
	},

	// ---- downloads, one at a time ----

	queueDownload: function(inSender, entry) {
		for (var i = 0; i < this.queue.length; i++) {
			if (this.queue[i].id === entry.id) {
				return;
			}
		}
		if (this.getting && this.getting.entry.id === entry.id) {
			return;
		}
		this.queue.push(entry);
		CR.banner("\"" + entry.title + "\" will be downloaded");
		this.nextDownload();
	},

	nextDownload: function() {
		var self = this;
		if (this.getting || !this.queue.length) {
			return;
		}
		var entry = this.queue.shift();
		var auth = this.$.catalog.auth || {};
		this.getting = {entry: entry, dest: ""};
		this.showGetting(entry.title, 0, 0);
		this.$.svc.ask("download", {url: entry.url, user: auth.user, password: auth.password,
			folder: entry.author, name: entry.title, ext: entry.ext}, function(r) {
				if (!r.ok) {
					CR.banner(r.error);
					self.getting = null;
					self.$.getting.hide();
					self.nextDownload();
					return;
				}
				self.getting.dest = r.dest;
				self.getting.id = r.id;
				self.watchDownload();
			});
	},

	showGetting: function(title, bytes, total) {
		var waiting = this.queue.length ? "  ·  " + this.queue.length + " more waiting" : "";
		this.$.getting.show();
		this.$.gettingText.setContent("Downloading \"" + title + "\"" +
			(total ? "  ·  " + CR.size(bytes) + " of " + CR.size(total) : (bytes ? "  ·  " + CR.size(bytes) : "")) + waiting);
		this.$.gettingBar.setPosition(total ? Math.round(bytes * 100 / total) : 0);
	},

	// Ask how the download is doing until it is over.
	watchDownload: function() {
		var self = this;
		this.$.svc.ask("progress", {}, function(s) {
			if (!s.ok || !s.id) {
				return;
			}
			if (s.running) {
				self.showGetting(self.getting ? self.getting.entry.title : s.name, s.bytes, s.total);
				setTimeout(function() {
					self.watchDownload();
				}, 700);
				return;
			}
			var got = self.getting;
			if (!got || got.id !== s.id) {
				// Finished while the app was closed, or already dealt with.
				self.$.getting.hide();
				return;
			}
			self.getting = null;
			self.$.getting.hide();
			if (s.done) {
				var downloads = CR.table("downloads");
				downloads[got.entry.id] = s.dest;
				CR.setTable("downloads", downloads);
				CR.banner("\"" + got.entry.title + "\" is on this TouchPad");
				self.$.library.rescan();
			} else if (s.error) {
				CR.banner(s.error);
			}
			self.nextDownload();
		});
	},

	cancelDownload: function() {
		this.queue = [];
		this.$.svc.ask("cancel", {}, function() {});
	}
});

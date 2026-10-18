/* Reading a comic: one page filling the screen. Tap the right or left side (or
	swipe) to turn the page, tap the middle for the bars. Pages are taken out of
	the comic one at a time, and large scans are scaled by the system, so even a
	very large comic uses little memory. */

// A page with more picture than this is scaled down by the system before it
// is shown; smaller ones are shown as they are, which is quicker.
CR.SCALE_ABOVE = 3200000;

enyo.kind({
	name: "CR.Reader",
	kind: enyo.VFlexBox,
	className: "cr-reader",
	events: {
		onClose: ""
	},
	components: [
		{name: "svc", kind: "CR.Service"},
		// accelerated: false, so a tall page never becomes one large graphics layer.
		{name: "scroller", kind: "Scroller", flex: 1, accelerated: false, className: "cr-stage", onclick: "stageClick", components: [
			{name: "page", kind: "Image", className: "cr-page"}
		]},
		{name: "wait", className: "cr-wait", showing: false, onclick: "stageClick"},
		// The bars lie over the page, so the page keeps the whole screen and does
		// not move when they come and go.
		{name: "top", kind: "HFlexBox", align: "center", className: "cr-bar cr-bar-over-top", components: [
			{kind: "Button", caption: "Back", className: "enyo-button-dark", onclick: "doClose"},
			{name: "title", flex: 1, className: "cr-bar-title"},
			{name: "fit", kind: "Button", caption: "Fit Width", className: "enyo-button-dark", onclick: "fitClick"}
		]},
		{name: "bottom", kind: "HFlexBox", align: "center", className: "cr-bar cr-bar-over-bottom", components: [
			{name: "prev", kind: "Button", caption: "Previous", className: "enyo-button-dark", onclick: "prevClick"},
			{name: "slider", kind: "Slider", flex: 1, className: "cr-slider", minimum: 0, maximum: 1, position: 0,
				onChanging: "sliding", onChange: "slid"},
			{name: "count", className: "cr-count"},
			{name: "next", kind: "Button", caption: "Next", className: "enyo-button-dark", onclick: "nextClick"}
		]}
	],

	create: function() {
		this.inherited(arguments);
		this.path = "";
		this.index = 0;
		this.count = 0;
		this.size = {w: 0, h: 0};
		this.fitWidth = CR.pref("fit", "page") === "width";
		this.bars = true;
		this.turns = 0;
	},

	rendered: function() {
		this.inherited(arguments);
		// Enyo does not pass on a picture's own events; listen on the element.
		var node = this.$.page.hasNode();
		if (node && node !== this.wired) {
			this.wired = node;
			node.addEventListener("load", enyo.bind(this, "pageShown"), false);
			node.addEventListener("error", enyo.bind(this, "pageFailed"), false);
		}
	},

	// ---- for the app ----

	// Open a comic at the page it was left on. name: what to call it.
	openComic: function(path, name) {
		var self = this;
		var turn = ++this.turns;
		var left = CR.progressOf(path);
		this.path = path;
		this.count = 0;
		this.index = 0;
		this.$.title.setContent(name);
		this.$.page.setSrc("");
		this.$.page.hide();
		this.opens = (this.opens || 0) + 1;
		this.firstPage = true;
		this.barsTouched = false;
		this.setBars(true);
		this.showWait("Opening…");
		this.$.fit.setCaption(this.fitWidth ? "Fit Page" : "Fit Width");
		this.$.svc.ask("open", {path: path}, function(r) {
			if (turn !== self.turns) {
				return;
			}
			if (!r.ok) {
				self.showWait(r.error);
				return;
			}
			self.count = r.count;
			self.$.slider.setMaximum(Math.max(1, r.count - 1));
			// A finished comic opens at its start again.
			var start = left && left.page < r.count - 1 ? left.page : 0;
			self.showPage(start);
		});
	},

	// Let go of the comic when the reader is left.
	closeComic: function() {
		CR.scalerAnswered(this.scalerTimer);
		this.turns++;
		this.path = "";
		this.$.page.setSrc("");
		this.$.page.hide();
	},

	// ---- pages ----

	// The system's picture scaler gave no answer: the page will not arrive.
	scalerStuck: function() {
		if (this.path && !this.shown) {
			this.shown = true;
			this.showWait("This page cannot be shown until the TouchPad is restarted.");
		}
	},

	showWait: function(text) {
		this.$.wait.setContent(text);
		this.$.wait.setShowing(!!text);
	},

	showPage: function(index) {
		var self = this;
		if (!this.path || index < 0 || index >= this.count) {
			return;
		}
		var turn = ++this.turns;
		var path = this.path;
		this.index = index;
		this.showCount();
		// Say so only if the page takes a moment.
		setTimeout(function() {
			if (turn === self.turns && !self.shown) {
				self.showWait("Page " + (index + 1) + "…");
			}
		}, 500);
		this.shown = false;
		this.$.svc.ask("page", {path: path, index: index, ahead: 2}, function(r) {
			if (turn !== self.turns) {
				return;
			}
			if (!r.ok) {
				self.shown = true;
				self.showWait(r.error);
				return;
			}
			self.file = r.file;
			self.size = {w: r.w, h: r.h};
			self.fitPage(true);
			CR.setProgress(path, index, self.count);
		});
	},

	showCount: function() {
		this.$.count.setContent((this.index + 1) + " of " + this.count);
		this.$.slider.setPosition(this.index);
		this.$.prev.setDisabled(this.index <= 0);
		this.$.next.setDisabled(this.index >= this.count - 1);
	},

	// Size the page for the screen and the way of fitting. load: also (re)load the picture.
	fitPage: function(load) {
		var node = this.$.scroller.hasNode();
		if (!node || !this.file) {
			return;
		}
		var boxW = node.clientWidth;
		var boxH = node.clientHeight;
		var w = this.size.w || boxW;
		var h = this.size.h || boxH;
		var scale = this.fitWidth ? boxW / w : Math.min(boxW / w, boxH / h);
		var showW = Math.max(1, Math.round(w * scale));
		var showH = Math.max(1, Math.round(h * scale));
		this.$.page.applyStyle("width", showW + "px");
		this.$.page.applyStyle("height", showH + "px");
		this.$.page.applyStyle("margin-left", Math.max(0, Math.round((boxW - showW) / 2)) + "px");
		this.$.page.applyStyle("margin-top", Math.max(0, Math.round((boxH - showH) / 2)) + "px");
		if (load) {
			// A large scan is scaled by the system to the size it is shown at.
			var big = !this.size.w || this.size.w * this.size.h > CR.SCALE_ABOVE;
			var src = big ? CR.scaledUrl(this.file, showW, showH) : CR.fileUrl(this.file);
			this.scaledFor = big ? showW : 0;
			CR.scalerAnswered(this.scalerTimer);
			this.scalerTimer = big ? CR.scalerAsked() : null;
			this.$.page.setSrc(src);
			this.$.page.show();
			this.$.scroller.setScrollTop(0);
			this.$.scroller.setScrollLeft(0);
		} else if (this.scaledFor && Math.abs(this.scaledFor - showW) > 40) {
			// Scaled for another size (the TouchPad was turned, or the fit changed).
			this.fitPage(true);
		}
	},

	pageShown: function() {
		var self = this;
		CR.scalerAnswered(this.scalerTimer);
		this.shown = true;
		this.showWait("");
		if (this.firstPage) {
			// The bars show what was opened, then leave the page to itself.
			this.firstPage = false;
			var turn = this.opens;
			setTimeout(function() {
				if (turn === self.opens && !self.barsTouched) {
					self.setBars(false);
				}
			}, 2500);
		}
	},

	pageFailed: function() {
		CR.scalerAnswered(this.scalerTimer);
		if (this.path && this.file) {
			this.shown = true;
			this.showWait("This page could not be shown.");
		}
	},

	resizeHandler: function() {
		this.inherited(arguments);
		this.fitPage(false);
	},

	// ---- turning pages ----

	nextClick: function() {
		if (this.index >= this.count - 1) {
			CR.banner("That was the last page");
			return;
		}
		this.showPage(this.index + 1);
	},

	prevClick: function() {
		this.showPage(this.index - 1);
	},

	sliding: function(inSender, position) {
		this.$.count.setContent((Math.round(position) + 1) + " of " + this.count);
	},

	slid: function(inSender, position) {
		this.showPage(Math.round(position));
	},

	fitClick: function() {
		this.fitWidth = !this.fitWidth;
		CR.setPref("fit", this.fitWidth ? "width" : "page");
		this.$.fit.setCaption(this.fitWidth ? "Fit Page" : "Fit Width");
		this.fitPage(false);
		this.$.scroller.setScrollTop(0);
	},

	setBars: function(on) {
		this.bars = on;
		this.$.top.setShowing(on);
		this.$.bottom.setShowing(on);
	},

	// A tap on the page: the sides turn it, the middle shows or hides the bars.
	stageClick: function(inSender, inEvent) {
		if (!this.count) {
			return;
		}
		var node = this.hasNode();
		var width = node ? node.clientWidth : 1024;
		var x = inEvent.pageX || 0;
		if (x < width * 0.3) {
			this.prevClick();
		} else if (x > width * 0.7) {
			this.nextClick();
		} else {
			this.barsTouched = true;
			this.setBars(!this.bars);
		}
		return true;
	},

	// A sideways swipe turns the page.
	dragfinishHandler: function(inSender, inEvent) {
		var dx = inEvent.dx || 0;
		var dy = inEvent.dy || 0;
		if (this.count && Math.abs(dx) > 70 && Math.abs(dx) > Math.abs(dy) * 2) {
			if (dx < 0) {
				this.nextClick();
			} else {
				this.prevClick();
			}
			// The lifted finger is not also a tap.
			return true;
		}
	}
});

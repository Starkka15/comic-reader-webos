/* A shelf of comics: rows of covers with a title under each. Only the rows on
	screen exist. Used for the comics on the TouchPad and for a server's. */

CR.MAX_COLS = 8;         // covers across the widest screen
CR.TILE_WIDTH = 170;     // matches .cr-tile
CR.HOLD_MS = 450;        // how long a touch must rest to count as holding

// The tiles of one row.
CR.tiles = function() {
	var out = [];
	for (var i = 0; i < CR.MAX_COLS; i++) {
		out.push({name: "tile" + i, className: "cr-tile", tileIndex: i, onclick: "tileClick", components: [
			{name: "art" + i, className: "cr-art", components: [
				{name: "letter" + i, className: "cr-letter"}
			]},
			{name: "title" + i, className: "cr-title"},
			{name: "sub" + i, className: "cr-sub"},
			{name: "badge" + i, className: "cr-badge"}
		]});
	}
	return out;
};

enyo.kind({
	name: "CR.Shelf",
	kind: enyo.VFlexBox,
	className: "cr-shelf",
	events: {
		// A comic was tapped: (item)
		onPick: "",
		// A comic was held: (item)
		onHold: "",
		// A comic without a cover is about to be shown: (item)
		onNeedCover: "",
		// The last row is on screen.
		onEnd: ""
	},
	components: [
		{name: "list", kind: "VirtualList", flex: 1, onSetupRow: "setupRow", components: [
			{name: "row", kind: "HFlexBox", className: "cr-shelf-row", components: CR.tiles()}
		]}
	],

	create: function() {
		this.inherited(arguments);
		// An item: {title, sub, badge, cover: picture file or "", noCover: none to be had}
		this.items = [];
		this.cols = 5;
		this.endSeen = -1;
		// Software scrolling: the list's default moves each page of rows as a
		// graphics layer, which has hung the TouchPad's graphics chip.
		this.$.list.$.scroller.setAccelerated(false);
	},

	rendered: function() {
		this.inherited(arguments);
		this.measure();
	},

	// Show these items. keepPlace: the list grew or changed in place; stay where it is.
	setItems: function(items, keepPlace) {
		this.items = items;
		if (!keepPlace) {
			this.endSeen = -1;
		}
		this.redraw(!keepPlace);
	},

	redraw: function(toTop) {
		if (!this.$.list.hasNode()) {
			return;
		}
		if (toTop) {
			this.$.list.punt();
		} else {
			this.$.list.refresh();
		}
	},

	// Draw again soon; many calls close together draw once.
	redrawSoon: function() {
		enyo.job(this.id + "-redraw", enyo.bind(this, "redraw"), 250);
	},

	measure: function() {
		var node = this.hasNode();
		var width = node ? node.clientWidth : 0;
		if (width) {
			this.cols = Math.max(1, Math.min(CR.MAX_COLS, Math.floor(width / CR.TILE_WIDTH)));
		}
	},

	resizeHandler: function() {
		this.inherited(arguments);
		var before = this.cols;
		this.measure();
		if (before !== this.cols) {
			this.redraw(true);
		}
	},

	setupRow: function(inSender, inIndex) {
		var first = inIndex * this.cols;
		if (inIndex < 0 || first >= this.items.length) {
			return false;
		}
		for (var i = 0; i < CR.MAX_COLS; i++) {
			var item = i < this.cols ? this.items[first + i] : null;
			var tile = this.$["tile" + i];
			tile.setShowing(i < this.cols);
			tile.setClassName("cr-tile" + (item ? "" : " cr-tile-empty"));
			if (!item) {
				continue;
			}
			// The tiles are reused for every row, so each one sets all of this.
			var art = this.$["art" + i];
			var url = item.cover ? CR.scaledUrl(item.cover, 280, 392) : "";
			art.applyStyle("background-image", url ? "url('" + url + "')" : null);
			if (url) {
				CR.scalerCheck(url);
			}
			this.$["letter" + i].setContent(item.cover ? "" : (item.title || "?").charAt(0).toUpperCase());
			this.$["title" + i].setContent(item.title);
			this.$["sub" + i].setContent(item.sub || "");
			this.$["badge" + i].setContent(item.badge || "");
			if (!item.cover && !item.noCover && !item.coverAsked) {
				item.coverAsked = true;
				this.doNeedCover(item);
			}
		}
		if (first + this.cols >= this.items.length && this.endSeen !== this.items.length) {
			this.endSeen = this.items.length;
			// After this drawing is done: the answer may change the list.
			enyo.asyncMethod(this, "doEnd");
		}
		return true;
	},

	// Which item an event landed on, or null.
	itemFromEvent: function(inEvent) {
		if (inEvent.rowIndex === undefined || inEvent.rowIndex === null) {
			return null;
		}
		var c = inEvent.dispatchTarget;
		while (c && c !== this) {
			if (c.tileIndex !== undefined) {
				return c.tileIndex < this.cols ? (this.items[inEvent.rowIndex * this.cols + c.tileIndex] || null) : null;
			}
			c = c.parent;
		}
		return null;
	},

	// A touch that rests on a comic.
	mouseholdpulseHandler: function(inSender, inEvent) {
		if (!inEvent || inEvent.holdTime < CR.HOLD_MS || this.heldStart === inEvent.holdStart) {
			return;
		}
		var item = this.itemFromEvent(inEvent);
		if (!item) {
			return;
		}
		this.heldStart = inEvent.holdStart;
		// The tap that follows lifting the finger is part of this hold.
		this.swallowTap = true;
		this.doHold(item);
	},

	mousereleaseHandler: function() {
		if (this.swallowTap) {
			var self = this;
			setTimeout(function() {
				self.swallowTap = false;
			}, 350);
		}
	},

	tileClick: function(inSender, inEvent) {
		var item = inSender.tileIndex < this.cols ? this.items[inEvent.rowIndex * this.cols + inSender.tileIndex] : null;
		if (item && !this.swallowTap) {
			this.doPick(item);
		}
	}
});

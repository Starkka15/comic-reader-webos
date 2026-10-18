/* Calls to the app's service, each with a plain callback.

	this.$.svc.ask("scan", {}, function(r) { if (r.ok) ... else show(r.error) });

	The callback always runs, with ok: false and an error sentence on failure. */
enyo.kind({
	name: "CR.Service",
	kind: enyo.Component,
	components: [
		{name: "comics", kind: "PalmService", service: "palm://org.webos.comicreader.service/",
			onSuccess: "answered", onFailure: "failed"}
	],

	ask: function(method, params, callback) {
		if (!window.PalmSystem) {
			callback({ok: false, error: "This only works on a webOS device."});
			return;
		}
		// Extra properties given here end up on the request the handlers receive.
		this.$.comics.call(params || {}, {method: method, crCallback: callback, crMethod: method});
	},

	answered: function(inSender, inResponse, inRequest) {
		var r = inResponse || {};
		if (r.ok === undefined) {
			r.ok = r.returnValue !== false;
		}
		if (!r.ok && !r.error) {
			r.error = r.errorText || "That did not work.";
		}
		if (inRequest.crCallback) {
			inRequest.crCallback(r);
		}
	},

	failed: function(inSender, inResponse, inRequest) {
		CR.log(inRequest.crMethod + " failed: " + enyo.json.stringify(inResponse));
		if (inRequest.crCallback) {
			inRequest.crCallback({ok: false, error: (inResponse && inResponse.errorText) || "That did not work."});
		}
	}
});

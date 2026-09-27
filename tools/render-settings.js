// Write the Clay settings page to an HTML file, without a phone or emulator,
// by running the built phone JS against stub Pebble and localStorage objects.
//
//   pebble build
//   node tools/render-settings.js 12|24 out.html [on|off]
//
// The first argument is the watch's clock style as the phone last heard it;
// the third sets "Update every second". Render the result headless at phone
// width, e.g. with Playwright's chromium-headless-shell:
//
//   chrome-headless-shell --headless --no-sandbox --hide-scrollbars \
//     --force-device-scale-factor=2 --window-size=390,2600 \
//     --virtual-time-budget=3000 --screenshot=out.png file://$PWD/out.html
//
// The stub watch info must carry a firmware version: Clay reads
// firmware.major, and without it the page stops rendering at the color picker.
var fs = require("fs");
var path = require("path");
var h24 = process.argv[2] === "24";
var out = process.argv[3];
var store = {
  clock24: h24 ? "1" : "0",
  "clay-settings": JSON.stringify({
    TickSeconds: process.argv[4] !== "off",
    SecondsFrom: 7,
    SecondsUntil: 23,
  }),
};
global.localStorage = {
  getItem: function (k) { return k in store ? store[k] : null; },
  setItem: function (k, v) { store[k] = String(v); },
  removeItem: function (k) { delete store[k]; },
};
var listeners = {};
global.Pebble = {
  addEventListener: function (e, f) { (listeners[e] = listeners[e] || []).push(f); },
  openURL: function (u) {
    fs.writeFileSync(out, decodeURIComponent(u.replace(/^data:text\/html;charset=utf-8,/, "")));
  },
  // Fails at once, as an unreachable watch would, so the page opens without
  // waiting for a clock style and uses the stored one.
  sendAppMessage: function (msg, ok, fail) { if (fail) fail(); },
  getActiveWatchInfo: function () {
    return { platform: "emery", model: "pt2", firmware: { major: 4, minor: 9, patch: 0, suffix: "" } };
  },
  getAccountToken: function () { return ""; },
  getWatchToken: function () { return ""; },
};
// Newer Node has a read-only navigator of its own, which plain assignment
// silently leaves in place.
Object.defineProperty(global, "navigator", {
  value: { geolocation: { getCurrentPosition: function () {} } },
});
global.XMLHttpRequest = function () {};
global.window = global;
require(path.join(__dirname, "..", "build", "pebble-js-app.js"));
// "ready" first: with Clay's own event handling off, that is where it reads the
// watch info the page needs.
listeners.ready && listeners.ready.forEach(function (f) { f(); });
listeners.showConfiguration.forEach(function (f) { f(); });

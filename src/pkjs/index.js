// Phone side: find the location, compute the sun events the watch needs, and
// push them over.
//
// The watch has a hard memory ceiling, so geolocation, retries, caching and
// all the solar arithmetic deliberately live here where memory is cheap. The
// watch receives a window of timestamps and does nothing but pick the pair
// that brackets the current moment.
//
// The window covers about three days, so the watch keeps working correctly
// offline for roughly that long -- losing the phone should not stop the face.

var solar = require("./solar");

var REFRESH_MS = 21600000; // 6 h; the window is far longer, this just tops up
var MAX_FAILURES = 3;
var WINDOW_DAYS = 3;

// Same options TimeStyle uses: 15s is generous for a cold fix, and a 60s
// maximumAge avoids re-fixing when something already has one.
var GEO_OPTIONS = { timeout: 15000, maximumAge: 60000 };

var failures = 0;

// "r1754123456,s1754167890,t1754169999,..." covering the last event before now
// through the next WINDOW_DAYS. Kind is r(ise), s(et) or t(zeit); seconds
// rather than milliseconds keep the payload short.
function buildWindow(lat, lon) {
  var now = Date.now();
  var from = now - solar.MS_PER_DAY;
  var until = now + WINDOW_DAYS * solar.MS_PER_DAY;
  var seen = {};
  var out = [];

  for (var d = -1; d <= WINDOW_DAYS; d++) {
    var t = now + d * solar.MS_PER_DAY;
    var events = solar.sunEvents(t, lat, lon).map(function (e) {
      return [e.t, e.rising ? "r" : "s"];
    });
    solar.sunEvents(t, lat, lon, solar.TZEIT_ANGLE).forEach(function (e) {
      if (!e.rising) events.push([e.t, "t"]);
    });
    for (var i = 0; i < events.length; i++) {
      var ms = events[i][0];
      if (ms < from || ms > until) continue;
      var token = events[i][1] + Math.round(ms / 1000);
      if (seen[token]) continue; // overlapping day ranges repeat events
      seen[token] = true;
      out.push([ms, token]);
    }
  }

  out.sort(function (a, b) { return a[0] - b[0]; });
  return out.map(function (e) { return e[1]; }).join(",");
}

function sendWindow(lat, lon) {
  var win = buildWindow(lat, lon);
  localStorage.setItem("sun", win);
  Pebble.sendAppMessage({ SUN: win });
}

function sendCached() {
  var win = localStorage.getItem("sun");
  if (win) Pebble.sendAppMessage({ SUN: win });
}

function onSuccess(pos) {
  failures = 0;
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  localStorage.setItem("lat", lat);
  localStorage.setItem("lon", lon);
  sendWindow(lat, lon);
}

function onError() {
  failures += 1;
  if (failures <= MAX_FAILURES) {
    update();
    return;
  }
  failures = 0;
  // No fix available: roll the window forward from the last known coordinates
  // if we have them, so a dead GPS does not freeze the watch's sun times.
  var lat = parseFloat(localStorage.getItem("lat"));
  var lon = parseFloat(localStorage.getItem("lon"));
  if (lat === lat && lon === lon) sendWindow(lat, lon);
  else sendCached();
}

function update() {
  navigator.geolocation.getCurrentPosition(onSuccess, onError, GEO_OPTIONS);
}

Pebble.addEventListener("ready", function () {
  sendCached(); // show something immediately, then refine
  update();
  setInterval(update, REFRESH_MS);
});

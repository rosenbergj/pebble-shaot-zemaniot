// Phone side: find the user's location and push it to the watch.
//
// The watch has a hard memory ceiling, so all of this -- geolocation, retries,
// caching -- deliberately lives here where memory is cheap. The watch just
// receives two numbers.

var REFRESH_MS = 1800000; // 30 min: enough for travel, easy on the battery
var MAX_FAILURES = 3;

// Same options TimeStyle uses; a 15s timeout is generous enough for a cold GPS
// fix, and a 60s maximumAge avoids re-fixing when something already has one.
var GEO_OPTIONS = { timeout: 15000, maximumAge: 60000 };

var failures = 0;

function send(lat, lon) {
  // AppMessage has no float type, so send strings and parse on the watch.
  Pebble.sendAppMessage({ LAT: String(lat), LON: String(lon) });
}

function sendCached() {
  var lat = localStorage.getItem("lat");
  var lon = localStorage.getItem("lon");
  if (lat && lon) send(lat, lon);
}

function onSuccess(pos) {
  failures = 0;
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  localStorage.setItem("lat", lat);
  localStorage.setItem("lon", lon);
  send(lat, lon);
}

function onError() {
  failures += 1;
  if (failures <= MAX_FAILURES) {
    update();
  } else {
    failures = 0;
    sendCached(); // fall back to the last known fix
  }
}

function update() {
  navigator.geolocation.getCurrentPosition(onSuccess, onError, GEO_OPTIONS);
}

Pebble.addEventListener("ready", function () {
  sendCached(); // show something immediately, then refine
  update();
  setInterval(update, REFRESH_MS);
});

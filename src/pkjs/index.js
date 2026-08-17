// Phone side: supply a location, and let Clay deliver the settings.
//
// The watch computes its own sun times now, so the phone no longer sends a
// window of solar events -- only the coordinates to compute them from. That
// keeps the face correct offline indefinitely rather than for the length of a
// transmitted window.
//
// Coordinates travel as integers scaled by 1e6, because AppMessage carries no
// floating point. Philadelphia is about 39950000 / -75170000, comfortably
// inside int32.

var Clay = require("@rebble/clay");
var clayConfig = require("./config");

// Auto-handling on: Clay sends one message key per setting, which src/c/main.c
// reads by key. The old build had to pack everything into a single string
// because the JavaScript runtime could not afford the per-key memory.
var clay = new Clay(clayConfig);

var REFRESH_MS = 21600000; // 6 h; a fix this old is still fine for solar maths
var MAX_FAILURES = 3;

// Same options TimeStyle uses: 15s is generous for a cold fix, and a 60s
// maximumAge avoids re-fixing when something already has one.
var GEO_OPTIONS = { timeout: 15000, maximumAge: 60000 };

var failures = 0;

function sendLocation(lat, lon) {
  Pebble.sendAppMessage({
    LAT: Math.round(lat * 1e6),
    LON: Math.round(lon * 1e6),
  });
}

function sendCached() {
  var lat = parseFloat(localStorage.getItem("lat"));
  var lon = parseFloat(localStorage.getItem("lon"));
  // NaN-safe: NaN is the only value not equal to itself.
  if (lat === lat && lon === lon) {
    sendLocation(lat, lon);
    return true;
  }
  return false;
}

function onSuccess(pos) {
  failures = 0;
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  localStorage.setItem("lat", lat);
  localStorage.setItem("lon", lon);
  sendLocation(lat, lon);
}

function onError() {
  failures += 1;
  if (failures <= MAX_FAILURES) {
    update();
    return;
  }
  failures = 0;
  // No fix available: fall back to the last known coordinates so a dead GPS
  // does not leave the watch without a location.
  sendCached();
}

function update() {
  navigator.geolocation.getCurrentPosition(onSuccess, onError, GEO_OPTIONS);
}

Pebble.addEventListener("ready", function () {
  sendCached(); // something immediate, then refine
  update();
  setInterval(update, REFRESH_MS);
});

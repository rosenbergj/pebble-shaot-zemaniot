// Phone side for the location probe.
//
// A deliberate mirror of the location half of src/pkjs/index.js, not a symlink:
// the real one pulls in Clay and the whole settings surface, none of which this
// app has any use for. The point is that the *schedule* matches -- when a fix is
// taken, how stale a cached one may be, and what wakes this side at all -- so if
// any of that changes over there it has to change here too, or the probe stops
// reporting on the thing it exists to report on.

var REFRESH_MS = 21600000; // 6 h, the fallback interval
var MAX_FAILURES = 3;

// Startup: the answer has to be about here, so barely any cache is tolerated.
var GEO_OPTIONS = { timeout: 15000, maximumAge: 60000 };
// The hourly top-up: a fix taken for some other app is what makes this cheap.
var GEO_OPTIONS_CHEAP = { timeout: 15000, maximumAge: 1800000 };

var failures = 0;

function sendLocation(lat, lon) {
  Pebble.sendAppMessage({
    LAT: Math.round(lat * 1e6),
    LON: Math.round(lon * 1e6),
  });
}

function storedCoords() {
  var lat = parseFloat(localStorage.getItem("lat"));
  var lon = parseFloat(localStorage.getItem("lon"));
  if (lat === lat && lon === lon) return { lat: lat, lon: lon };
  return null;
}

function sendCached() {
  var c = storedCoords();
  if (!c) return false;
  sendLocation(c.lat, c.lon);
  return true;
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
  sendCached();
}

function update() {
  navigator.geolocation.getCurrentPosition(onSuccess, onError, GEO_OPTIONS);
}

// No error path, exactly as in the real one: a refusal is answered by the next
// hour coming round, and escalating would turn a top-up into a retry storm.
function topUpLocation() {
  navigator.geolocation.getCurrentPosition(onSuccess, function () {},
                                           GEO_OPTIONS_CHEAP);
}

// The watch sends nothing but the hourly request. On the real face that request
// is for weather and the top-up rides along; here there is no weather, so the
// request *is* the top-up, and it keeps the same cadence so the probe sees what
// the face would have seen.
Pebble.addEventListener("appmessage", function () {
  topUpLocation();
});

Pebble.addEventListener("ready", function () {
  sendCached(); // something immediate, then refine
  update();
  setInterval(update, REFRESH_MS);
});

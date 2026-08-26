// Phone side for the location probe.
//
// A deliberate mirror of the location half of src/pkjs/index.js, not a symlink:
// the real one pulls in Clay and the whole settings surface, none of which this
// app has any use for. The point is that the *schedule* matches -- when a fix is
// taken, how stale a cached one may be, and what wakes this side at all -- so if
// any of that changes over there it has to change here too, or the probe stops
// reporting on the thing it exists to report on.

var MAX_FAILURES = 3;

// Startup: the answer has to be about here, so barely any cache is tolerated.
var GEO_OPTIONS = { timeout: 15000, maximumAge: 60000 };
// The hourly top-up: a fix taken for some other app is what makes this cheap.
var GEO_OPTIONS_CHEAP = { timeout: 15000, maximumAge: 1800000 };

var failures = 0;

// FixAge is the probe's own addition, not part of the face's protocol: how old
// the fix already was when the phone handed it over, in seconds.
//
// It is the only way to tell whether GEO_OPTIONS_CHEAP's 30-minute maximumAge is
// earning anything. Nothing on this side refreshes position between the hourly
// wakes, so a fix is only ever fresher than an hour because *some other app* on
// the phone asked for one and the OS handed us theirs. Whether that happens is a
// fact about how the phone is used, not about this code -- so measure it rather
// than guess.
function sendLocation(lat, lon, ageSecs) {
  Pebble.sendAppMessage({
    LAT: Math.round(lat * 1e6),
    LON: Math.round(lon * 1e6),
    FixAge: ageSecs,
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
  sendLocation(c.lat, c.lon, -1); // no fix behind it; age is not a number here
  return true;
}

function onSuccess(pos) {
  failures = 0;
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  localStorage.setItem("lat", lat);
  localStorage.setItem("lon", lon);
  // pos.timestamp is when the fix was taken, which is not when it was handed
  // over: a cached one can be up to maximumAge old already.
  var age = 0;
  if (pos.timestamp) age = Math.round((Date.now() - pos.timestamp) / 1000);
  if (age < 0) age = 0;
  sendLocation(lat, lon, age);
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
// The real one sequences a weather fetch behind the fix when WantWx is set;
// there is no weather here, so this is that function with its tail removed.
function topUpLocation() {
  navigator.geolocation.getCurrentPosition(onSuccess, function () {},
                                           GEO_OPTIONS_CHEAP);
}

// The watch sends nothing but its hourly wake, carrying WantWx -- always 0 here.
// The top-up is ungated on the real face too, which is the point: position is
// refreshed whether or not a weather box exists.
Pebble.addEventListener("appmessage", function () {
  topUpLocation();
});

Pebble.addEventListener("ready", function () {
  sendCached(); // something immediate, then refine
  update();
  // No interval of this side's own: the watch's hourly wake is the schedule.
});

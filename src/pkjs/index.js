// Phone side: supply a location, fetch the weather, and let Clay deliver the
// settings.
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
var messageKeys = require("message_keys");

// Auto-handling on: Clay sends one message key per setting, which src/c/main.c
// reads by key. The old build had to pack everything into a single string
// because the JavaScript runtime could not afford the per-key memory.
var clay = new Clay(clayConfig);

var MAX_FAILURES = 3;

// Same options TimeStyle uses: 15s is generous for a cold fix, and a 60s
// maximumAge avoids re-fixing when something already has one. Used where the
// answer has to be about *here*: at startup, which is what a flight looks like
// from this side -- a 30-minute-old fix could still be the departure gate.
var GEO_OPTIONS = { timeout: 15000, maximumAge: 60000 };

// ...and used where being roughly right matters more than being minutes
// fresher. A generous maximumAge is the whole point: the phone hands back a fix
// some other app already paid for rather than powering up its receiver, which
// is what makes asking this often affordable at all.
//
// Deliberately left at 30 minutes when the wake went half-hourly, so it now
// equals the interval rather than sitting at half of it. Every scheduled top-up
// can therefore be answered from cache if anything on the phone -- this app
// included -- has fixed within the interval, which is the affordability the
// cheap options exist for. The cost is that a position can be a whole interval
// behind; that is the same bet as before, and 10km of movement inside 30
// minutes is a car journey, which the startup path's tight options already
// cover on the next launch.
var GEO_OPTIONS_CHEAP = { timeout: 15000, maximumAge: 1800000 }; // 30 min

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
  // NaN-safe: NaN is the only value not equal to itself.
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
    initialFix();
    return;
  }
  failures = 0;
  // No fix available: fall back to the last known coordinates so a dead GPS
  // does not leave the watch without a location, and fetch against them rather
  // than skipping weather entirely. They are the best answer available.
  sendCached();
  updateWeather();
}

// The startup fix, and the weather that follows it.
//
// Weather waits for the fix rather than going out beside it. Racing them is
// what used to put the departure city on screen after a flight: the fetch went
// against the coordinates already in storage while the fix that would replace
// them was still being taken, and the reply arrived stamped with the current
// time -- so it was not faded, and nothing on the face suggested it was wrong.
// The cost of waiting is the seconds the fix takes.
//
// The tight maximumAge belongs here and nowhere else. A half-hour-old fix could
// still be the departure gate, and this is the one path that runs on landing.
//
// This side cannot know whether a weather box is configured -- only the watch
// can, which is what WantWx is for -- so the startup fetch is unconditional. It
// costs one fetch per JS start on a face displaying no weather; that is
// accepted, since nothing starts this but the watchface.
function initialFix() {
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      onSuccess(pos);
      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    onError,
    GEO_OPTIONS
  );
}

// The answer to the watch's scheduled wake: take a fix, send it, and only then
// fetch weather with it.
//
// Sequencing is the whole point. The two used to run side by side, so the fetch
// went out against whatever coordinates were already stored while the fix that
// would replace them was still being taken -- and after a flight that meant a
// reading for the airport left behind. Waiting costs the seconds the fix takes
// and removes any offset between position and weather worth reasoning about.
//
// Position is what the shaot arithmetic is built on, and the error is larger
// than it looks: a 10km move east or west slides sunrise and sunset together by
// about 28 seconds, and against a proportional hour that is eight chalakim --
// visible on a face that displays them. That applies at rest as much as in
// transit, since the face runs on wherever the last fix landed.
//
// Asking twice an hour costs much less than it sounds. It rides a wake the
// watch is making anyway, and the generous maximumAge means most calls are
// answered from a fix some other app already paid for rather than by powering
// up the receiver.
//
// No error path for the fix itself: a refusal is answered by the next wake
// coming round, and escalating through onError would turn a top-up into a retry
// storm. A failed fix must not cost the weather, though, so that falls back to
// the stored coordinates.
function topUpLocation(wantWeather) {
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      onSuccess(pos);
      if (wantWeather) fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    function () {
      if (wantWeather) updateWeather();
    },
    GEO_OPTIONS_CHEAP
  );
}

// --- weather ----------------------------------------------------------------
//
// Open-Meteo: free, no API key, and the source TimeStyle settled on. The watch
// asks for weather rather than the phone pushing it on a timer, because only
// the watch knows whether any slot is currently showing weather -- there is no
// point spending a radio wake and an HTTP fetch on a face that is not
// displaying it.
//
// Temperature travels in Celsius and is converted on the watch, so changing the
// units setting does not have to wait for the next fetch.

// Icon ids, matching src/c/weather.h. The mapping from WMO codes is
// TimeStyle's (MIT), whose icons these are.
var ICON = {
  CLEAR_DAY: 0,
  CLEAR_NIGHT: 1,
  CLOUDY: 2,
  HEAVY_RAIN: 3,
  HEAVY_SNOW: 4,
  LIGHT_RAIN: 5,
  LIGHT_SNOW: 6,
  PARTLY_CLOUDY_NIGHT: 7,
  PARTLY_CLOUDY: 8,
  RAINING_AND_SNOWING: 9,
  THUNDERSTORM: 10,
  GENERIC: 11,
};

function iconForCode(code, isNight) {
  switch (code) {
    case 0:
      return isNight ? ICON.CLEAR_NIGHT : ICON.CLEAR_DAY;
    case 1:
    case 2:
      return isNight ? ICON.PARTLY_CLOUDY_NIGHT : ICON.PARTLY_CLOUDY;
    case 3:
    case 45:
    case 48:
      return ICON.CLOUDY;
    case 51:
    case 53:
    case 55:
    case 61:
    case 80:
      return ICON.LIGHT_RAIN;
    case 63:
    case 65:
    case 81:
    case 82:
      return ICON.HEAVY_RAIN;
    case 56:
    case 57:
    case 66:
    case 67:
      return ICON.RAINING_AND_SNOWING;
    case 71:
    case 77:
    case 85:
      return ICON.LIGHT_SNOW;
    case 73:
    case 75:
    case 86:
      return ICON.HEAVY_SNOW;
    case 95:
    case 96:
    case 99:
      return ICON.THUNDERSTORM;
    default:
      return ICON.GENERIC;
  }
}

// "2026-08-18" -> 20260818. The watch compares these to the date it wants, so
// a payload that has gone stale across local midnight is rejected rather than
// shown as today's.
function ymdFromISO(s) {
  if (!s || s.length < 10) return 0;
  var y = parseInt(s.substr(0, 4), 10);
  var m = parseInt(s.substr(5, 2), 10);
  var d = parseInt(s.substr(8, 2), 10);
  if (!(y && m && d)) return 0;
  return y * 10000 + m * 100 + d;
}

// Which fetch is the current one. A reply from a superseded fetch is dropped
// rather than shown: at `ready` the unprompted push goes out against the stored
// coordinates while the fix that may replace them is still being taken, so
// after a flight two fetches can be in the air at once -- one for the airport
// left behind, one for the airport arrived at -- and without this the answer on
// screen is whichever the network happened to return last.
var wxSeq = 0;

function fetchWeather(lat, lon) {
  var seq = ++wxSeq;
  var url =
    "https://api.open-meteo.com/v1/forecast?latitude=" + lat +
    "&longitude=" + lon +
    "&current_weather=true" +
    "&daily=temperature_2m_max,temperature_2m_min,weathercode" +
    "&timezone=auto&forecast_days=3";

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (seq !== wxSeq) return; // a later fetch has already superseded this one
    var json;
    try {
      json = JSON.parse(this.responseText);
    } catch (e) {
      return; // a malformed reply leaves the watch on its last good data
    }
    if (!json || !json.current_weather || !json.daily) return;

    var msg = {
      WxTemp: Math.round(json.current_weather.temperature),
      WxCond: iconForCode(json.current_weather.weathercode,
                          json.current_weather.is_day !== 1),
    };

    // Three days, though the box only ever shows two. The third is what keeps
    // it right through a day with no phone; see WEATHER_DAYS in weather.h for
    // why two runs out around lunchtime the following day.
    var days = json.daily.time || [];
    for (var i = 0; i < 3 && i < days.length; i++) {
      var ymd = ymdFromISO(days[i]);
      if (!ymd) continue;
      msg["WxDay" + i + "Ymd"] = ymd;
      msg["WxDay" + i + "High"] = Math.round(json.daily.temperature_2m_max[i]);
      msg["WxDay" + i + "Low"] = Math.round(json.daily.temperature_2m_min[i]);
      msg["WxDay" + i + "Cond"] = iconForCode(json.daily.weathercode[i], false);
    }

    Pebble.sendAppMessage(msg);
  };
  xhr.open("GET", url);
  xhr.send();
}

function updateWeather() {
  var c = storedCoords();
  if (c) {
    fetchWeather(c.lat, c.lon);
    return;
  }
  // No coordinates yet: take a fix, which also seeds the solar maths.
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      onSuccess(pos);
      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    function () {},
    GEO_OPTIONS
  );
}

// The watch sends one kind of message: its half-hourly wake. WantWx says whether a
// weather box is on the face, which is the one thing this side cannot know --
// there is no point spending a fetch on a face that displays no weather. The
// position top-up happens either way, because every face runs on the sun.
Pebble.addEventListener("appmessage", function (e) {
  var p = (e && e.payload) || {};
  topUpLocation(p.WantWx === 1);
});

// Clay keeps the settings on the phone. The watch keeps its own copy as a single
// struct and throws the whole thing away whenever that struct's size changes,
// which is every build that adds a setting -- so after such an install the phone
// still shows the wearer's real choices while the watch has fallen back to
// defaults, and the two only re-agree when the settings page is opened and
// saved. Re-sending what Clay has stored, on every launch, heals that before
// anyone notices. It costs one message and is idempotent.
function resendSettings() {
  var stored;
  try {
    stored = JSON.parse(localStorage.getItem("clay-settings") || "{}");
  } catch (e) {
    return;
  }

  // Only keys this build still declares. A setting that has since been removed
  // -- ClockStyle was, once -- is left behind in Clay's store, and it would
  // otherwise map to an undefined message key and travel as a junk entry.
  var known = {};
  var any = false;
  Object.keys(stored).forEach(function (k) {
    if (messageKeys[k] !== undefined) {
      known[k] = stored[k];
      any = true;
    }
  });
  // Nothing configured yet: the watch's own defaults are the right answer, and
  // sending an empty dictionary would only reset the AppMessage buffers.
  if (!any) return;

  try {
    Pebble.sendAppMessage(Clay.prepareSettingsForAppMessage(known));
  } catch (e) {
    // A stale or malformed store must not take the rest of startup with it.
  }
}

// Weather is pushed here without being asked, which is the one place that is
// right to do: the watch asks the instant the link comes up, but this JavaScript
// is started by the phone app and may not be listening yet, so the request that
// matters most -- the one after a night with Bluetooth off -- is the one most
// likely to be lost. Only this side knows when it began running. initialFix()
// carries that push, behind the fix.
//
// No interval of this side's own. The watch wakes it twice an hour, and that wake
// is ungated -- it arrives whether or not a weather box is configured -- so a
// second schedule here would only duplicate fixes.
Pebble.addEventListener("ready", function () {
  resendSettings();
  sendCached(); // in case the watch has no location at all; a no-op if it has
  initialFix();
});

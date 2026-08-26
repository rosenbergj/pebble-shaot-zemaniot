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

// Any message from the watch is a request for weather; it sends nothing else.
Pebble.addEventListener("appmessage", function () {
  updateWeather();
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

Pebble.addEventListener("ready", function () {
  resendSettings();
  sendCached(); // something immediate, then refine
  update();
  setInterval(update, REFRESH_MS);
  // Push weather without being asked, which is the one place that is right to
  // do. The watch requests weather the instant the link comes up, but this
  // JavaScript is started by the phone app and may not have been listening yet
  // -- so the request that matters most, the one after a night with Bluetooth
  // off, is the one most likely to be lost. Only this side knows when it began
  // running. It costs a fetch per start even on a face carrying no weather
  // slot; that is accepted, since nothing starts this but the watchface.
  updateWeather();
});

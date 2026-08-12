// The watchface: layout, slot content, location, and the tick loop.
//
// Everything watch-side lives in this one module on purpose. Each module
// carries memory overhead in the XS runtime, the mod budget is tight, and the
// official Moddable guidance is to minimise module count. Pure calculation
// lives in core.js, which stays free of watch APIs so node can test it.
//
// Emery is 200x228. Font constraints found by rendering specimens:
//   Leco  - digits, "." and ":" -- the only large face that can show shaot.
//   Roboto-Bold 49 - digits and ":" but NO "." (drops it silently); largest
//                    face available, so civil-only.
// Worst-case widths at 8 chars: Roboto-Bold 49 = 196, Leco-Regular 42 = 176.
//
// MEMORY: over the mod's ceiling the watch hangs outright -- no error, no log,
// and the emulator stops answering. Suspect it first if a small addition makes
// the face stop appearing, and check the mod size at
// build/mods/emery/mcrun/bin/pebble/release/embeddedjs/mc.xsa.

import Poco from "commodetto/Poco";
import Message from "pebble/message";
import Battery from "embedded:sensor/Battery";
import {
	bracket, nextEvent, TZEIT_ANGLE,
	chalakimNow, formatShaot,
	hebrewForNow, monthName,
} from "core";

// Settings arrive from the phone (Clay page -> AppMessage) and are held as a
// flat array: cheaper in mod memory than an object with named properties, and
// it serialises to localStorage as one short string.
//
//   0 offset6      1 = sunrise is 6.00, noon 12.00; 0 = sunrise 0.00
//   1 withMinutes  1 = H.MM.CC; 0 = H.CCCC raw chalakim
//   2 tickSeconds  1 = tick every second; 0 = every minute (battery)
//   3 band  4 left  5 mid  6 right   slot content, see SLOT_* below
//   7 accent       0xRRGGBB
//   8 civilFont    0 = Roboto 49, 1 = Leco 42 (matches the shaot face)
const CFG_KEY = "cfg";
const cfg = [0, 1, 1, 1, 3, 2, 5, 0x007882, 0];

// Slot content kinds. Integers rather than strings to keep the mod small.
const SLOT_HEBREW = 1, SLOT_SECDATE = 2;
const SLOT_SUNSET = 3, SLOT_TZEIT = 4, SLOT_BATTERY = 5;

const settings = { offset6: false, withMinutes: true, hebrewScript: false };

const WDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];
const GMONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

const BAND_H = 38;
const FOOTER_TOP = 170;

const render = new Poco(screen);
const style = {
	bg: render.makeColor(16, 16, 18),
	fg: render.makeColor(255, 255, 255),
	dim: render.makeColor(140, 140, 145),
	accent: render.makeColor(0, 120, 130),
	onAccent: render.makeColor(255, 255, 255),
	rule: render.makeColor(60, 60, 64),
};
const fonts = {
	civil: new render.Font("Roboto-Bold", 49),
	shaot: new render.Font("Leco-Regular", 42),
	band: new render.Font("Gothic-Bold", 24),
	slotValue: new render.Font("Gothic-Bold", 18),
	slotLabel: new render.Font("Gothic-Regular", 14),
};

let br = null;
let heb = null;
let sunsetStr = "--:--";
let tzeitStr = "--:--";
let lastHebDay = -1;
let brLat = 0;
let brLon = 0;

// --- location ---------------------------------------------------------------
// The phone does the geolocation and the retrying (src/pkjs/index.js): the
// conventional Pebble arrangement, and the right one here because the mod has
// a hard memory ceiling while the phone side effectively does not. The face
// must keep working with no phone nearby, so the last fix is cached.

const LOC_KEY = "loc";
let here = { lat: 39.95, lon: -75.17 }; // coarse default until the phone reports

// "lat,lon" -> here. Rejects NaN, which would silently poison every later
// sunrise/sunset calculation instead of failing visibly.
function applyLocation(s) {
	const parts = s.split(",");
	const lat = parseFloat(parts[0]);
	const lon = parseFloat(parts[1]);
	if (lat !== lat || lon !== lon) return false;
	here = { lat, lon };
	return true;
}

// --- settings ---------------------------------------------------------------
// One channel carries both location and settings. The phone packs every
// setting into a single comma-separated string: nine separate message keys
// cost over a kilobyte of mod memory, which we do not have, and the phone has
// memory to spare for the assembly.

let civilFont = null;
let tickEvent = "";

function setTick(seconds) {
	const ev = seconds ? "secondchange" : "minutechange";
	if (ev === tickEvent) return;
	if (tickEvent) watch.removeEventListener(tickEvent, draw);
	watch.addEventListener(ev, draw);
	tickEvent = ev;
}

// "0,1,1,1,3,2,5,30850,0" -> cfg. Ignores NaN so a truncated or older string
// leaves the affected settings at their defaults.
function parseCfg(s) {
	const parts = s.split(",");
	for (let i = 0; i < cfg.length; i++) {
		const n = parseInt(parts[i]);
		if (n === n) cfg[i] = n;
	}
}

function applyCfg() {
	settings.offset6 = !!cfg[0];
	settings.withMinutes = !!cfg[1];
	style.accent = render.makeColor((cfg[7] >> 16) & 255, (cfg[7] >> 8) & 255, cfg[7] & 255);
	// Reusing the shaot face costs no extra font object.
	civilFont = cfg[8] ? fonts.shaot : fonts.civil;
	setTick(cfg[2]);
}

function startChannel() {
	const cached = localStorage.getItem(LOC_KEY);
	if (cached) applyLocation(cached);

	const saved = localStorage.getItem(CFG_KEY);
	if (saved) parseCfg(saved);
	applyCfg();

	return new Message({
		keys: ["LAT", "LON", "CFG"],
		onReadable() {
			const m = this.read();

			const lat = m.get("LAT");
			const lon = m.get("LON");
			if (undefined !== lat && undefined !== lon) {
				const s = lat + "," + lon;
				if (applyLocation(s)) localStorage.setItem(LOC_KEY, s);
			}

			const c = m.get("CFG");
			if (undefined !== c) {
				parseCfg(c);
				localStorage.setItem(CFG_KEY, c);
				applyCfg();
				draw();
			}
		},
	});
}

// --- battery ----------------------------------------------------------------
// The sensor pushes a sample on change and can also be polled, so read once at
// startup and then let onSample keep it current.

let batteryPct = 0;

function startBattery() {
	const b = new Battery({
		onSample() {
			batteryPct = this.sample().percent;
		},
	});
	batteryPct = b.sample().percent;
	return b;
}

// --- drawing ----------------------------------------------------------------

function pad2(n) {
	return n < 10 ? "0" + n : "" + n;
}

function hhmm(ms) {
	const t = new Date(ms);
	return ((t.getHours() % 12) || 12) + ":" + pad2(t.getMinutes());
}

function center(str, font, color, y, x0 = 0, w = render.width) {
	const tw = render.getTextWidth(str, font);
	render.drawText(str, font, color, x0 + ((w - tw) >> 1), y);
}

// Returns [label, value]. The band draws the value only, so content whose label
// carries meaning (the weekday) folds it into the value when forBand is set.
function slotContent(kind, d, forBand) {
	if (kind === SLOT_HEBREW) {
		return ["hebrew",
			heb.day + " " + monthName(heb.year, heb.month, settings.hebrewScript)];
	}
	if (kind === SLOT_SECDATE) {
		const md = GMONTHS[d.getMonth()] + " " + d.getDate();
		return forBand ? ["", WDAYS[d.getDay()] + " " + md] : [WDAYS[d.getDay()], md];
	}
	if (kind === SLOT_SUNSET) return ["sunset", sunsetStr];
	if (kind === SLOT_TZEIT) return ["tzeit", tzeitStr];
	if (kind === SLOT_BATTERY) return ["batt", batteryPct + "%"];
	return ["", ""];
}

function draw() {
	const now = Date.now();
	const d = new Date();

	// Recompute when the half-day ends, or when the phone moves us elsewhere.
	if (!br || now >= br.end || now < br.start ||
			here.lat !== brLat || here.lon !== brLon) {
		brLat = here.lat;
		brLon = here.lon;
		br = bracket(now, brLat, brLon);
		lastHebDay = -1; // a bracket flip can roll the Hebrew date
		if (br) {
			const sunsetMs = br.isDay ? br.end : br.start;
			sunsetStr = hhmm(sunsetMs);
			const tz = nextEvent(sunsetMs - 1000, brLat, brLon, TZEIT_ANGLE, false);
			tzeitStr = tz ? hhmm(tz) : "--:--";
		}
	}
	if (!br) return;

	if (d.getDate() !== lastHebDay) {
		lastHebDay = d.getDate();
		heb = hebrewForNow({
			year: d.getFullYear(),
			month: d.getMonth() + 1,
			day: d.getDate(),
			hour: d.getHours(),
			sunIsUp: br.isDay,
		});
	}

	const band = slotContent(cfg[3], d, true);
	const cells = [
		slotContent(cfg[4], d),
		slotContent(cfg[5], d),
		slotContent(cfg[6], d),
	];

	render.begin();
	render.fillRectangle(style.bg, 0, 0, render.width, render.height);
	render.fillRectangle(style.accent, 0, 0, render.width, BAND_H);
	center(band[1], fonts.band, style.onAccent, 5);
	center(((d.getHours() % 12) || 12) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds()),
		civilFont, style.fg, 46);
	center(formatShaot(chalakimNow(now, br.start, br.end), settings),
		fonts.shaot, style.fg, 112);

	// The accent fill belongs to the outer slot positions, not to their
	// content: every slot is user-configurable, only the fill colour is.
	const cw = render.width / 3;
	const h = render.height - FOOTER_TOP - 1;
	render.fillRectangle(style.rule, 0, FOOTER_TOP, render.width, 1);
	render.fillRectangle(style.accent, 0, FOOTER_TOP + 1, Math.round(cw), h);
	render.fillRectangle(style.accent, Math.round(2 * cw), FOOTER_TOP + 1,
		render.width - Math.round(2 * cw), h);

	for (let i = 0; i < 3; i++) {
		const onFill = i !== 1;
		center(cells[i][0], fonts.slotLabel, onFill ? style.onAccent : style.dim,
			FOOTER_TOP + 10, i * cw, cw);
		center(cells[i][1], fonts.slotValue, onFill ? style.onAccent : style.fg,
			FOOTER_TOP + 30, i * cw, cw);
	}
	render.end();
}

// Held in bindings so these are not collected while the face runs.
// startChannel() applies the saved settings, which registers the tick listener.
const batterySensor = startBattery();
const channel = startChannel();

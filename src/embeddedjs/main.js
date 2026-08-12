import Poco from "commodetto/Poco";
import { bracket, nextEvent, TZEIT_ANGLE } from "zmanim";
import { chalakimNow, formatShaot } from "shaot";
import { hebrewForNow, monthName } from "hebdate";
import { draw as drawLayout, makeStyle } from "layouts";

// Hardcoded until phone-side location plumbing lands; likewise settings.
const LAT = 39.95;
const LON = -75.17;
const settings = { offset6: false, withMinutes: true };

const render = new Poco(screen);
const style = makeStyle(render);

let br = null;
let heb = null;
let sunsetStr = "--:--";
let tzeitStr = "--:--";
let lastHebDay = -1;

const WDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];
const GMONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

// Four configurable areas: the band plus the three footer slots. Any area can
// show any content; these are the defaults until the settings page lands.
const slots = { band: "hebrew", left: "sunset", mid: "secdate", right: "battery" };

function pad2(n) {
	return n < 10 ? "0" + n : "" + n;
}

// Returns [label, value]. The band draws the value only, so content whose label
// carries meaning (the weekday) folds it into the value when forBand is set.
function slotContent(kind, d, heb, forBand) {
	if (kind === "hebrew") {
		return ["hebrew", heb.day + " " + monthName(heb.year, heb.month)];
	}
	if (kind === "secdate") {
		const md = GMONTHS[d.getMonth()] + " " + d.getDate();
		return forBand ? ["", WDAYS[d.getDay()] + " " + md] : [WDAYS[d.getDay()], md];
	}
	if (kind === "sunset") return ["sunset", sunsetStr];
	if (kind === "tzeit") return ["tzeit", tzeitStr];
	if (kind === "battery") return ["batt", "78%"];
	return ["", ""];
}

function hhmm(ms) {
	const t = new Date(ms);
	return ((t.getHours() % 12) || 12) + ":" + pad2(t.getMinutes());
}

function draw() {
	const now = Date.now();
	const d = new Date();

	if (!br || now >= br.end || now < br.start) {
		br = bracket(now, LAT, LON);
		lastHebDay = -1; // a bracket flip can roll the Hebrew date
		if (br) {
			const sunsetMs = br.isDay ? br.end : br.start;
			sunsetStr = hhmm(sunsetMs);
			const tz = nextEvent(sunsetMs - 1000, LAT, LON, TZEIT_ANGLE, false);
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

	const data = {
		shaot: formatShaot(chalakimNow(now, br.start, br.end), settings),
		civilSec: ((d.getHours() % 12) || 12) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds()),
		band: slotContent(slots.band, d, heb, true),
		cells: [
			slotContent(slots.left, d, heb),
			slotContent(slots.mid, d, heb),
			slotContent(slots.right, d, heb),
		],
	};

	render.begin();
	drawLayout(render, style, data);
	render.end();
}

watch.addEventListener("secondchange", draw);

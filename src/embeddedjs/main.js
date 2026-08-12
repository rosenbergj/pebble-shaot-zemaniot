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

function pad2(n) {
	return n < 10 ? "0" + n : "" + n;
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

	const hebMonth = monthName(heb.year, heb.month);
	const data = {
		shaot: formatShaot(chalakimNow(now, br.start, br.end), settings),
		hebDay: "" + heb.day,
		hebMonth,
		hebFull: heb.day + " " + hebMonth,
		civilSec: ((d.getHours() % 12) || 12) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds()),
		sunset: sunsetStr,
		tzeit: tzeitStr,
		wday: WDAYS[d.getDay()],
		secDate: GMONTHS[d.getMonth()] + " " + d.getDate(),
		battery: "78",
	};

	render.begin();
	drawLayout(render, style, data);
	render.end();
}

watch.addEventListener("secondchange", draw);

import Poco from "commodetto/Poco";
import { bracket, nextEvent, TZEIT_ANGLE } from "zmanim";
import { chalakimNow, formatShaot } from "shaot";
import { hebrewForNow, monthName } from "hebdate";
import { LAYOUTS, makeStyle } from "layouts";

// Hardcoded until phone-side location plumbing lands; likewise settings.
const LAT = 39.95;
const LON = -75.17;
const settings = { offset6: false, withMinutes: true };

// Layout under review; see layouts.js.
const LAYOUT = 3;

const render = new Poco(screen);
const style = makeStyle(render);
const layout = LAYOUTS[LAYOUT];

let br = null;
let heb = null;
let sunsetStr = "--:--";
let lastHebDay = -1;

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
		if (br) sunsetStr = hhmm(br.isDay ? br.end : br.start);
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

	// Countdown to nightfall, shown only between sunset and tzeit.
	let tilNight = "--:--";
	if (!br.isDay) {
		const tzeit = nextEvent(br.start, LAT, LON, TZEIT_ANGLE, false);
		if (tzeit && tzeit > now) {
			const secs = Math.round((tzeit - now) / 1000);
			tilNight = Math.floor(secs / 60) + ":" + pad2(secs % 60);
		}
	}

	const data = {
		shaot: formatShaot(chalakimNow(now, br.start, br.end), settings),
		hebDay: "" + heb.day,
		hebMonth: monthName(heb.year, heb.month),
		civil: ((d.getHours() % 12) || 12) + ":" + pad2(d.getMinutes()),
		sunset: sunsetStr,
		tilNight,
		battery: "78",
	};

	render.begin();
	layout.draw(render, style, data);
	render.end();
}

watch.addEventListener("secondchange", draw);

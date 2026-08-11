import Poco from "commodetto/Poco";
import { bracket } from "zmanim";
import { chalakimNow, formatShaot } from "shaot";
import { hebrewForNow, monthName } from "hebdate";

// Hardcoded until phone-side location plumbing lands; likewise settings.
const LAT = 39.95;
const LON = -75.17;
const settings = { offset6: false, withMinutes: true, hebrewScript: false };

const render = new Poco(screen);
const bg = render.makeColor(0, 0, 0);
const fg = render.makeColor(255, 255, 255);
const dim = render.makeColor(170, 170, 170);

// Family+size must match the system font table exactly (xsHost.c gFonts);
// an unlisted combination aborts the script rather than throwing.
const shaotFont = new render.Font("Bitham-Black", 30);
const smallFont = new render.Font("Gothic-Regular", 24);

let br = null;
let hebLine = "";
let lastHebDay = -1;

function pad2(n) {
	return n < 10 ? "0" + n : "" + n;
}

function centerText(str, font, color, y) {
	const w = render.getTextWidth(str, font);
	render.drawText(str, font, color, (render.width - w) >> 1, y);
}

function draw() {
	const now = Date.now();
	const d = new Date();

	if (!br || now >= br.end || now < br.start) {
		br = bracket(now, LAT, LON);
		lastHebDay = -1; // a bracket flip can roll the Hebrew date
	}

	if (br && d.getDate() !== lastHebDay) {
		lastHebDay = d.getDate();
		const h = hebrewForNow({
			year: d.getFullYear(),
			month: d.getMonth() + 1,
			day: d.getDate(),
			hour: d.getHours(),
			sunIsUp: br.isDay,
		});
		hebLine = h.day + " " + monthName(h.year, h.month, settings.hebrewScript);
	}

	render.begin();
	render.fillRectangle(bg, 0, 0, render.width, render.height);

	if (!br) {
		centerText("no sun data", smallFont, dim, 100);
	} else {
		centerText(hebLine, smallFont, dim, 40);
		centerText(
			formatShaot(chalakimNow(now, br.start, br.end), settings),
			shaotFont, fg, 94
		);
		centerText(
			((d.getHours() % 12) || 12) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds()),
			smallFont, dim, 158
		);
	}

	render.end();
}

watch.addEventListener("secondchange", draw);

import Poco from "commodetto/Poco";
import { bracket } from "zmanim";
import { chalakimNow, formatShaot } from "shaot";
import { hebrewForNow, monthName } from "hebdate";

// Hardcoded until phone-side location plumbing lands; likewise settings.
const LAT = 39.95;
const LON = -75.17;
const settings = { offset6: false, withMinutes: true };

const render = new Poco(screen);
const bg = render.makeColor(0, 0, 0);
const fg = render.makeColor(255, 255, 255);
const dim = render.makeColor(150, 150, 150);

// Family+size must match the system font table exactly (xsHost.c gFonts);
// an unlisted pair aborts the script rather than throwing. The Leco faces are
// digit subsets but do carry "." and ":".
const shaotFont = new render.Font("Leco-Bold", 38);
const civilFont = new render.Font("Leco-Bold", 20);
const textFont = new render.Font("Gothic-Regular", 24);

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
		hebLine = h.day + " " + monthName(h.year, h.month);
	}

	render.begin();
	render.fillRectangle(bg, 0, 0, render.width, render.height);

	if (!br) {
		centerText("no sun data", textFont, dim, 100);
	} else {
		centerText(hebLine, textFont, dim, 44);
		centerText(
			formatShaot(chalakimNow(now, br.start, br.end), settings),
			shaotFont, fg, 92
		);
		centerText(
			((d.getHours() % 12) || 12) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds()),
			civilFont, dim, 152
		);
	}

	render.end();
}

watch.addEventListener("secondchange", draw);

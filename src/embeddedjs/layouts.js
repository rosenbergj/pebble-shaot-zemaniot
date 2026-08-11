// Candidate layouts for design review. Each entry draws one full screen from
// the same data so the variants can be compared directly. Once a direction is
// chosen the rest of this file goes away.
//
// Emery is 200x228. Font constraints found by rendering specimens:
//   Leco  - digits, "." and ":" -- the only large face that can show shaot.
//   Roboto-Bold 49 - digits and ":" but NO "." (drops it silently); largest
//                    face available, so civil-only.
//   Bitham-Medium 42/34 - also no ".".
// Worst-case widths at 8 chars: Roboto-Bold 49 = 196, Leco-Regular 42 = 176,
// Leco-Bold 38 = 162, Leco-Bold 32 = ~146.

export function makeStyle(render) {
	return {
		bg: render.makeColor(16, 16, 18),
		fg: render.makeColor(255, 255, 255),
		dim: render.makeColor(140, 140, 145),
		accent: render.makeColor(0, 120, 130),
		onAccent: render.makeColor(255, 255, 255),
		rule: render.makeColor(60, 60, 64),
		fonts: {
			roboto49: new render.Font("Roboto-Bold", 49),
			leco42: new render.Font("Leco-Regular", 42),
			leco38: new render.Font("Leco-Bold", 38),
			leco32: new render.Font("Leco-Bold", 32),
			leco20: new render.Font("Leco-Bold", 20),
			bold24: new render.Font("Gothic-Bold", 24),
			bold18: new render.Font("Gothic-Bold", 18),
			text18: new render.Font("Gothic-Regular", 18),
			text14: new render.Font("Gothic-Regular", 14),
		},
	};
}

function center(render, str, font, color, y, x0 = 0, w = render.width) {
	const tw = render.getTextWidth(str, font);
	render.drawText(str, font, color, x0 + ((w - tw) >> 1), y);
}

// 1: both numbers in Leco, one family, civil slightly larger.
function twinLeco(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	center(render, d.hebFull, s.fonts.text18, s.dim, 18);
	center(render, d.civilSec, s.fonts.leco42, s.fg, 58);
	center(render, d.shaot, s.fonts.leco38, s.fg, 128);
}

// 2: civil as large as the hardware allows (Roboto), shaot just under (Leco).
function twinMax(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	center(render, d.hebFull, s.fonts.text18, s.dim, 14);
	center(render, d.civilSec, s.fonts.roboto49, s.fg, 48);
	center(render, d.shaot, s.fonts.leco42, s.fg, 124);
}

// 3: max-size twins over a footer row of three configurable slots.
function twinSlots(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	center(render, d.civilSec, s.fonts.roboto49, s.fg, 10);
	center(render, d.shaot, s.fonts.leco38, s.fg, 74);

	render.fillRectangle(s.rule, 0, 140, render.width, 1);
	const cw = render.width / 3;
	const cells = [["date", d.hebFull], ["sunset", d.sunset], ["batt", d.battery + "%"]];
	for (let i = 0; i < 3; i++) {
		center(render, cells[i][0], s.fonts.text14, s.dim, 152, i * cw, cw);
		center(render, cells[i][1], s.fonts.text18, s.fg, 172, i * cw, cw);
	}
}

// 4: accent header carries the date; twins fill the rest.
function bandTwin(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	render.fillRectangle(s.accent, 0, 0, render.width, 40);
	center(render, d.hebFull, s.fonts.bold24, s.onAccent, 6);
	center(render, d.civilSec, s.fonts.leco42, s.fg, 66);
	center(render, d.shaot, s.fonts.leco38, s.fg, 136);
}

// 5: labelled halves, so which number is which is unambiguous.
function halves(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	center(render, "time", s.fonts.text14, s.dim, 14);
	center(render, d.civilSec, s.fonts.roboto49, s.fg, 30);
	render.fillRectangle(s.rule, 12, 98, render.width - 24, 1);
	center(render, "shaot", s.fonts.text14, s.dim, 110);
	center(render, d.shaot, s.fonts.leco42, s.fg, 126);
	center(render, d.hebFull, s.fonts.text18, s.dim, 190);
}

// 6: sidebar rail -- shows what the rail costs the two numbers.
function railTwin(render, s, d) {
	const SB = 44;
	const main = render.width - SB;
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	render.fillRectangle(s.accent, main, 0, SB, render.height);

	center(render, d.civilSec, s.fonts.leco32, s.fg, 56, 0, main);
	center(render, d.shaot, s.fonts.leco32, s.fg, 122, 0, main);

	center(render, d.hebDay, s.fonts.bold18, s.onAccent, 16, main, SB);
	center(render, d.hebMonth, s.fonts.text14, s.onAccent, 36, main, SB);
	center(render, "set", s.fonts.text14, s.onAccent, 104, main, SB);
	center(render, d.sunset, s.fonts.bold18, s.onAccent, 120, main, SB);
	center(render, d.battery + "%", s.fonts.text14, s.onAccent, 198, main, SB);
}

export const LAYOUTS = [
	{ name: "twin-leco", draw: twinLeco },
	{ name: "twin-max", draw: twinMax },
	{ name: "twin-slots", draw: twinSlots },
	{ name: "band-twin", draw: bandTwin },
	{ name: "halves", draw: halves },
	{ name: "rail-twin", draw: railTwin },
];

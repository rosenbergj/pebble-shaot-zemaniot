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

const BAND_H = 38;

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
			bold24: new render.Font("Gothic-Bold", 24),
			bold18: new render.Font("Gothic-Bold", 18),
			text14: new render.Font("Gothic-Regular", 14),
		},
	};
}

function center(render, str, font, color, y, x0 = 0, w = render.width) {
	const tw = render.getTextWidth(str, font);
	render.drawText(str, font, color, x0 + ((w - tw) >> 1), y);
}

// Shared skeleton: accent band with the Hebrew date, then the two times.
function bandAndTimes(render, s, d, shaotFont, civilY = 48, shaotY = 106) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	render.fillRectangle(s.accent, 0, 0, render.width, BAND_H);
	center(render, d.hebFull, s.fonts.bold24, s.onAccent, 5);
	center(render, d.civilSec, s.fonts.roboto49, s.fg, civilY);
	center(render, d.shaot, shaotFont, s.fg, shaotY);
}

// Footer of n equal slots; dividers optional.
function footer(render, s, cells, dividers, top = 158) {
	render.fillRectangle(s.rule, 0, top, render.width, 1);
	const cw = render.width / cells.length;
	for (let i = 0; i < cells.length; i++) {
		if (dividers && i > 0) {
			render.fillRectangle(s.rule, Math.round(i * cw), top + 6, 1, render.height - top - 12);
		}
		center(render, cells[i][0], s.fonts.text14, s.dim, top + 10, i * cw, cw);
		center(render, cells[i][1], s.fonts.bold18, s.fg, top + 30, i * cw, cw);
	}
}

function band3(render, s, d) {
	bandAndTimes(render, s, d, s.fonts.leco42);
	footer(render, s, [["sunset", d.sunset], ["tzeit", d.tzeit], ["batt", d.battery + "%"]], false);
}

function band2(render, s, d) {
	bandAndTimes(render, s, d, s.fonts.leco42);
	footer(render, s, [["sunset", d.sunset], ["tzeit", d.tzeit]], false);
}

function band3Divided(render, s, d) {
	bandAndTimes(render, s, d, s.fonts.leco42);
	footer(render, s, [["sunset", d.sunset], ["tzeit", d.tzeit], ["batt", d.battery + "%"]], true);
}

// Same as band3 but spending the unused bottom margin on separating the two
// times, which sit only ~9px apart in the tighter versions.
function band3Roomy(render, s, d) {
	bandAndTimes(render, s, d, s.fonts.leco42, 46, 112);
	footer(render, s,
		[["sunset", d.sunset], ["tzeit", d.tzeit], ["batt", d.battery + "%"]],
		false, 170);
}

export const LAYOUTS = [
	{ name: "band-3slot", draw: band3 },
	{ name: "band-2slot", draw: band2 },
	{ name: "band-3slot-div", draw: band3Divided },
	{ name: "band-3slot-roomy", draw: band3Roomy },
];

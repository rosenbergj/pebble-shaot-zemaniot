// Candidate layouts for design review. Each entry draws one full screen from
// the same data so the variants can be compared directly. Once a direction is
// chosen the rest of this file goes away.
//
// Emery is 200x228. Leco faces carry digits, "." and ":" only -- anything with
// letters must use Gothic/Bitham.

export function makeStyle(render) {
	return {
		bg: render.makeColor(16, 16, 18),
		fg: render.makeColor(255, 255, 255),
		dim: render.makeColor(140, 140, 145),
		accent: render.makeColor(0, 120, 130),
		onAccent: render.makeColor(255, 255, 255),
		rule: render.makeColor(60, 60, 64),
		fonts: {
			num38: new render.Font("Leco-Bold", 38),
			num32: new render.Font("Leco-Bold", 32),
			num26: new render.Font("Leco-Bold", 26),
			num20: new render.Font("Leco-Bold", 20),
			big42: new render.Font("Leco-Regular", 42),
			text24: new render.Font("Gothic-Regular", 24),
			bold24: new render.Font("Gothic-Bold", 24),
			text18: new render.Font("Gothic-Regular", 18),
			text14: new render.Font("Gothic-Regular", 14),
			bold14: new render.Font("Gothic-Bold", 14),
		},
	};
}

function center(render, str, font, color, y, x0 = 0, w = render.width) {
	const tw = render.getTextWidth(str, font);
	render.drawText(str, font, color, x0 + ((w - tw) >> 1), y);
}

// A: TimeStyle-style vertical sidebar holding the secondary items.
function sidebar(render, s, d) {
	const SB = 56;
	const main = render.width - SB;
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	render.fillRectangle(s.accent, main, 0, SB, render.height);

	center(render, d.shaot, s.fonts.num32, s.fg, 84, 0, main);
	center(render, d.civil, s.fonts.num20, s.dim, 132, 0, main);

	center(render, d.hebDay, s.fonts.bold24, s.onAccent, 18, main, SB);
	center(render, d.hebMonth, s.fonts.text14, s.onAccent, 44, main, SB);
	center(render, "sunset", s.fonts.text14, s.onAccent, 100, main, SB);
	center(render, d.sunset, s.fonts.num20, s.onAccent, 116, main, SB);
	center(render, d.battery + "%", s.fonts.text14, s.onAccent, 196, main, SB);
}

// B: horizontal bands -- accent header, hero number, footer row of three.
function bands(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	render.fillRectangle(s.accent, 0, 0, render.width, 44);
	center(render, d.hebDay + " " + d.hebMonth, s.fonts.bold24, s.onAccent, 10);

	center(render, d.shaot, s.fonts.num38, s.fg, 84);

	const y = 168;
	render.fillRectangle(s.rule, 0, y - 12, render.width, 1);
	const cw = render.width / 3;
	const cells = [["time", d.civil], ["sunset", d.sunset], ["batt", d.battery + "%"]];
	for (let i = 0; i < 3; i++) {
		center(render, cells[i][0], s.fonts.text14, s.dim, y, i * cw, cw);
		center(render, cells[i][1], s.fonts.text18, s.fg, y + 18, i * cw, cw);
	}
}

// C: dense grid -- hero number over a 2x2 block of labelled cells.
function grid(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	center(render, d.shaot, s.fonts.num38, s.fg, 18);

	const top = 78;
	render.fillRectangle(s.rule, 0, top, render.width, 1);
	render.fillRectangle(s.rule, render.width >> 1, top, 1, render.height - top);
	const midY = top + ((render.height - top) >> 1);
	render.fillRectangle(s.rule, 0, midY, render.width, 1);

	const cw = render.width >> 1;
	const ch = (render.height - top) >> 1;
	const cells = [
		["hebrew", d.hebDay + " " + d.hebMonth],
		["time", d.civil],
		["sunset", d.sunset],
		["till night", d.tilNight],
	];
	for (let i = 0; i < 4; i++) {
		const x = (i % 2) * cw;
		const y = top + Math.floor(i / 2) * ch;
		center(render, cells[i][0], s.fonts.text14, s.dim, y + 14, x, cw);
		center(render, cells[i][1], s.fonts.text24, s.fg, y + 34, x, cw);
	}
}

// D: minimal -- hero number, date above, civil time below, nothing else.
function minimal(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	center(render, d.hebDay + " " + d.hebMonth, s.fonts.text24, s.dim, 52);
	center(render, d.shaot, s.fonts.big42, s.fg, 94);
	center(render, d.civil, s.fonts.num20, s.dim, 156);
}

export const LAYOUTS = [
	{ name: "sidebar", draw: sidebar },
	{ name: "bands", draw: bands },
	{ name: "grid", draw: grid },
	{ name: "minimal", draw: minimal },
];

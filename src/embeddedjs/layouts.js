// The chosen layout: accent band with the Hebrew date on top, civil time and
// shaot time as co-heroes in different faces, three slots along the bottom.
//
// Emery is 200x228. Font constraints found by rendering specimens:
//   Leco  - digits, "." and ":" -- the only large face that can show shaot.
//   Roboto-Bold 49 - digits and ":" but NO "." (drops it silently); largest
//                    face available, so civil-only.
//   Bitham-Medium 42/34 - also no ".".
// Worst-case widths at 8 chars: Roboto-Bold 49 = 196, Leco-Regular 42 = 176.
//
// MEMORY: a mod runs inside the host firmware's XS machine, whose budget
// (static 32768, chunk 8192, heap 512 slots -- see the Pebble platform
// manifest in the SDK) is fixed by the watch and cannot be raised from our
// manifest. With all the review variants compiled in we sat right at that
// ceiling: adding even one style property tipped it over, and over the ceiling
// the watch hangs outright -- no error, no log, the emulator simply stops
// answering. Keep this file lean, and suspect the ceiling first if a small
// addition makes the face stop appearing.

const BAND_H = 38;
const FOOTER_TOP = 170;

export function makeStyle(render) {
	return {
		bg: render.makeColor(16, 16, 18),
		fg: render.makeColor(255, 255, 255),
		dim: render.makeColor(140, 140, 145),
		accent: render.makeColor(0, 120, 130),
		onAccent: render.makeColor(255, 255, 255),
		rule: render.makeColor(60, 60, 64),
		fonts: {
			civil: new render.Font("Roboto-Bold", 49),
			shaot: new render.Font("Leco-Regular", 42),
			band: new render.Font("Gothic-Bold", 24),
			slotValue: new render.Font("Gothic-Bold", 18),
			slotLabel: new render.Font("Gothic-Regular", 14),
		},
	};
}

function center(render, str, font, color, y, x0 = 0, w = render.width) {
	const tw = render.getTextWidth(str, font);
	render.drawText(str, font, color, x0 + ((w - tw) >> 1), y);
}

// Four configurable areas: the band (d.band) and the three footer slots
// (d.cells), each a [label, value] pair -- the band shows its value only. Any
// area can hold any content. The accent fill on the outer two footer positions
// is a fixed property of those positions, with only its color a setting.
export function draw(render, s, d) {
	render.fillRectangle(s.bg, 0, 0, render.width, render.height);
	render.fillRectangle(s.accent, 0, 0, render.width, BAND_H);
	center(render, d.band[1], s.fonts.band, s.onAccent, 5);
	center(render, d.civilSec, s.fonts.civil, s.fg, 46);
	center(render, d.shaot, s.fonts.shaot, s.fg, 112);

	const cw = render.width / 3;
	const h = render.height - FOOTER_TOP - 1;
	render.fillRectangle(s.rule, 0, FOOTER_TOP, render.width, 1);
	render.fillRectangle(s.accent, 0, FOOTER_TOP + 1, Math.round(cw), h);
	render.fillRectangle(s.accent, Math.round(2 * cw), FOOTER_TOP + 1,
		render.width - Math.round(2 * cw), h);

	for (let i = 0; i < 3; i++) {
		const onFill = i !== 1;
		center(render, d.cells[i][0], s.fonts.slotLabel, onFill ? s.onAccent : s.dim,
			FOOTER_TOP + 10, i * cw, cw);
		center(render, d.cells[i][1], s.fonts.slotValue, onFill ? s.onAccent : s.fg,
			FOOTER_TOP + 30, i * cw, cw);
	}
}

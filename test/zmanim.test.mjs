import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
// Solar maths lives on the phone now (see src/pkjs/solar.js); CommonJS, so
// import the default export.
import solar from "../src/pkjs/solar.js";
const { bracket, nextEvent, TZEIT_ANGLE } = solar;

const fixtures = JSON.parse(
	readFileSync(new URL("./fixtures/zmanim.json", import.meta.url), "utf8")
);

// NOAA simplified formulas vs ephem's full-precision model + refraction handling:
// agreement within a couple of minutes is expected; the design needs "within a
// couple of seconds" of the *true chelek*, whose length is ~3.3s of wall time,
// so bracket agreement within ~120s keeps the displayed value visually right.
const TOL_MS = 120000;

for (const f of fixtures) {
	test(`bracket ${f.name} @ ${new Date(f.t).toISOString()}`, () => {
		const b = bracket(f.t, f.lat, f.lon);
		assert.ok(b, "bracket should exist");
		assert.ok(Math.abs(b.start - f.prev) < TOL_MS,
			`start off by ${(b.start - f.prev) / 1000}s`);
		assert.ok(Math.abs(b.end - f.next) < TOL_MS,
			`end off by ${(b.end - f.next) / 1000}s`);
		assert.equal(b.isDay, f.prevRising);
	});

	test(`tzeit ${f.name} @ ${new Date(f.t).toISOString()}`, () => {
		const tz = nextEvent(f.t, f.lat, f.lon, TZEIT_ANGLE, false);
		if (f.tzeitNext === null) {
			assert.equal(tz, null, "expected no tzeit (white nights)");
		} else {
			assert.ok(tz !== null, "tzeit should exist");
			assert.ok(Math.abs(tz - f.tzeitNext) < TOL_MS,
				`tzeit off by ${(tz - f.tzeitNext) / 1000}s`);
		}
	});
}

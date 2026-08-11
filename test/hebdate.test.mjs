import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import {
	hebrewFromGregorian,
	hebrewForNow,
	isLeapYear,
	monthLength,
	monthName,
} from "../src/embeddedjs/hebdate.js";

const fixtures = JSON.parse(
	readFileSync(new URL("./fixtures/hebdate.json", import.meta.url), "utf8")
);

test("gregorian -> hebrew matches convertdate for all fixture dates", () => {
	for (const { g, h, leap } of fixtures.dates) {
		const r = hebrewFromGregorian(g[0], g[1], g[2]);
		assert.deepEqual(
			[r.year, r.month, r.day], h,
			`for gregorian ${g.join("-")}`
		);
		assert.equal(isLeapYear(r.year), leap, `leap flag for ${h[0]}`);
	}
});

test("month lengths match convertdate", () => {
	for (const { year, lengths } of fixtures.monthLengths) {
		for (let m = 1; m <= lengths.length; m++) {
			assert.equal(monthLength(year, m), lengths[m - 1], `year ${year} month ${m}`);
		}
	}
});

test("sunset rollover advances the Hebrew day in the evening only", () => {
	const base = hebrewFromGregorian(2026, 8, 11);
	// Daytime: no rollover
	const day = hebrewForNow({ year: 2026, month: 8, day: 11, hour: 15, sunIsUp: true });
	assert.deepEqual(day, base);
	// After sunset, before midnight: next Hebrew day
	const evening = hebrewForNow({ year: 2026, month: 8, day: 11, hour: 21, sunIsUp: false });
	const nextDay = hebrewFromGregorian(2026, 8, 12);
	assert.deepEqual(evening, nextDay);
	// After midnight (sun still down): civil date already advanced, no extra day
	const night = hebrewForNow({ year: 2026, month: 8, day: 12, hour: 2, sunIsUp: false });
	assert.deepEqual(night, nextDay);
});

test("month names incl. leap-year Adar split", () => {
	assert.equal(monthName(5786, 5), "Av");
	assert.equal(monthName(5786, 5, true), "אב");
	// 5787 is a leap year: month 12 = Adar 1, month 13 = Adar 2
	assert.equal(isLeapYear(5787), true);
	assert.equal(monthName(5787, 12), "Adar 1");
	assert.equal(monthName(5787, 13), "Adar 2");
	assert.equal(monthName(5787, 12, true), "אדר א");
	// Non-leap: plain Adar
	assert.equal(isLeapYear(5786), false);
	assert.equal(monthName(5786, 12), "Adar");
});

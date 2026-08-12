import { test } from "node:test";
import assert from "node:assert/strict";
import {
	CHALAKIM_PER_HALF_DAY,
	chalakimNow,
	displayHour,
	formatShaot,
} from "../src/embeddedjs/core.js";

// Synthetic 12-hour bracket: 06:00 -> 18:00 UTC
const START = Date.UTC(2026, 7, 11, 6, 0, 0);
const END = Date.UTC(2026, 7, 11, 18, 0, 0);

test("chalakim across the bracket", () => {
	assert.equal(chalakimNow(START, START, END), 0);
	assert.equal(chalakimNow(END - 1, START, END), CHALAKIM_PER_HALF_DAY - 1);
	// Clamped outside the bracket
	assert.equal(chalakimNow(START - 5000, START, END), 0);
	assert.equal(chalakimNow(END + 5000, START, END), CHALAKIM_PER_HALF_DAY - 1);
	// Exact noon = hour 6 precisely
	assert.equal(chalakimNow((START + END) / 2, START, END), 6 * 1080);
});

test("formatting per design doc examples", () => {
	// Right before sunset: 11.59.17 (minutes mode) or 11.1079 (raw chalakim)
	const last = CHALAKIM_PER_HALF_DAY - 1;
	assert.equal(formatShaot(last), "11.59.17");
	assert.equal(formatShaot(last, { withMinutes: false }), "11.1079");
	assert.equal(formatShaot(last, { offset6: true }), "5.59.17");
	// At the flip
	assert.equal(formatShaot(0), "0.00.00");
	assert.equal(formatShaot(0, { offset6: true }), "6.00.00");
	// True noon
	assert.equal(formatShaot(6 * 1080), "6.00.00");
	assert.equal(formatShaot(6 * 1080, { offset6: true }), "12.00.00");
});

test("6-based display hours run 6..11,12,1..5", () => {
	const expected = [6, 7, 8, 9, 10, 11, 12, 1, 2, 3, 4, 5];
	for (let h = 0; h < 12; h++) {
		assert.equal(displayHour(h, true), expected[h]);
		assert.equal(displayHour(h, false), h);
	}
});


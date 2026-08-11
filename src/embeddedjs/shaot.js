// Shaot zemaniot arithmetic: the half-day (sunrise->sunset or sunset->sunrise)
// divides into 12 proportional hours of 1080 chalakim; 18 chalakim per
// proportional minute.

export const CHALAKIM_PER_HOUR = 1080;
export const CHALAKIM_PER_MINUTE = 18;
export const CHALAKIM_PER_HALF_DAY = 12 * CHALAKIM_PER_HOUR;

// Whole chalakim elapsed since bracket start, clamped to [0, 12959].
export function chalakimNow(nowMs, startMs, endMs) {
	const frac = (nowMs - startMs) / (endMs - startMs);
	const c = Math.floor(frac * CHALAKIM_PER_HALF_DAY);
	return Math.max(0, Math.min(CHALAKIM_PER_HALF_DAY - 1, c));
}

// Display hour for hour index 0-11.
// 0-based convention: 0..11 (sunrise 0.00, noon 6.00, flips to 0.00 at sunset).
// 6-based convention: 6,7..11,12,1..5 (sunrise 6.00, noon 12.00, flips to 6.00).
export function displayHour(hourIndex, offset6) {
	return offset6 ? ((hourIndex + 5) % 12) + 1 : hourIndex;
}

function pad2(n) {
	return n < 10 ? "0" + n : "" + n;
}

// "H.MM.CC" (withMinutes) or "H.CCCC" (raw chalakim within the hour).
export function formatShaot(chalakim, { offset6 = false, withMinutes = true } = {}) {
	const h = displayHour(Math.floor(chalakim / CHALAKIM_PER_HOUR), offset6);
	const rem = chalakim % CHALAKIM_PER_HOUR;
	if (withMinutes) {
		return h + "." + pad2(Math.floor(rem / CHALAKIM_PER_MINUTE)) + "." + pad2(rem % CHALAKIM_PER_MINUTE);
	}
	let c = "" + rem;
	while (c.length < 4) c = "0" + c;
	return h + "." + c;
}

// Duration of one chelek in ms for the given bracket.
export function chelekLengthMs(startMs, endMs) {
	return (endMs - startMs) / CHALAKIM_PER_HALF_DAY;
}

// Ms from nowMs until the next chelek boundary.
export function msUntilNextChelek(nowMs, startMs, endMs) {
	const len = chelekLengthMs(startMs, endMs);
	const next = startMs + (chalakimNow(nowMs, startMs, endMs) + 1) * len;
	return Math.max(0, next - nowMs);
}

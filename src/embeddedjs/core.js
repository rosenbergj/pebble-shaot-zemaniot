// Core calculations: solar events, shaot zemaniot arithmetic, Hebrew calendar.
//
// These three concerns live in one module on purpose: each module carries
// memory overhead in the XS runtime and the mod budget is tight, so the
// official Moddable guidance is to minimise module count. Kept free of any
// watch APIs so the node test suite can import it directly.

const DEG = Math.PI / 180;
export const MS_PER_DAY = 86400000;

// Standard elevation angles (degrees relative to geometric horizon, center of disk).
export const SUNRISE_SET_ANGLE = -0.833; // refraction 34' + semidiameter 16'
export const TZEIT_ANGLE = -8.5;
export const TALIT_ANGLE = -10.2;

function julianCentury(ms) {
	return (ms / MS_PER_DAY + 2440587.5 - 2451545) / 36525;
}

// Solar declination (deg) and equation of time (minutes) at Julian century T.
function solarParams(T) {
	const L0 = (280.46646 + T * (36000.76983 + 0.0003032 * T)) % 360;
	const M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
	const e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
	const C =
		Math.sin(M * DEG) * (1.914602 - T * (0.004817 + 0.000014 * T)) +
		Math.sin(2 * M * DEG) * (0.019993 - 0.000101 * T) +
		Math.sin(3 * M * DEG) * 0.000289;
	const omega = 125.04 - 1934.136 * T;
	const appLong = L0 + C - 0.00569 - 0.00478 * Math.sin(omega * DEG);
	const meanObliq =
		23 + (26 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60) / 60;
	const obliq = meanObliq + 0.00256 * Math.cos(omega * DEG);
	const decl = Math.asin(Math.sin(obliq * DEG) * Math.sin(appLong * DEG)) / DEG;
	const y = Math.tan((obliq / 2) * DEG) ** 2;
	const eqTime =
		(4 / DEG) *
		(y * Math.sin(2 * L0 * DEG) -
			2 * e * Math.sin(M * DEG) +
			4 * e * y * Math.sin(M * DEG) * Math.cos(2 * L0 * DEG) -
			0.5 * y * y * Math.sin(4 * L0 * DEG) -
			1.25 * e * e * Math.sin(2 * M * DEG));
	return { decl, eqTime };
}

// Hour angle (deg) at which the sun's center reaches elevation angleDeg; null if never.
function hourAngle(latDeg, declDeg, angleDeg) {
	const cosHA =
		(Math.cos((90 - angleDeg) * DEG) -
			Math.sin(latDeg * DEG) * Math.sin(declDeg * DEG)) /
		(Math.cos(latDeg * DEG) * Math.cos(declDeg * DEG));
	if (cosHA < -1 || cosHA > 1) return null;
	return Math.acos(cosHA) / DEG;
}

// Rising (rising=true) or setting event on the UTC day containing dayMs.
// Refined by re-evaluating declination/EoT at the event estimate.
function eventForUTCDay(dayMs, lat, lon, angleDeg, rising) {
	const dayStart = Math.floor(dayMs / MS_PER_DAY) * MS_PER_DAY;
	let guess = dayStart + MS_PER_DAY / 2;
	for (let i = 0; i < 3; i++) {
		const { decl, eqTime } = solarParams(julianCentury(guess));
		const ha = hourAngle(lat, decl, angleDeg);
		if (ha === null) return null;
		const noonMin = 720 - 4 * lon - eqTime;
		const evMin = rising ? noonMin - 4 * ha : noonMin + 4 * ha;
		guess = dayStart + evMin * 60000;
	}
	return Math.round(guess);
}

// All rise/set events at angleDeg over the three UTC days around nowMs, sorted.
export function sunEvents(nowMs, lat, lon, angleDeg = SUNRISE_SET_ANGLE) {
	const evs = [];
	for (let d = -1; d <= 1; d++) {
		for (const rising of [true, false]) {
			const t = eventForUTCDay(nowMs + d * MS_PER_DAY, lat, lon, angleDeg, rising);
			if (t !== null) evs.push({ t, rising });
		}
	}
	evs.sort((a, b) => a.t - b.t);
	return evs;
}

// The current half-day: previous sunrise/sunset to next. isDay = sun currently up.
// Returns null at polar latitudes when the sun doesn't cross the horizon.
export function bracket(nowMs, lat, lon) {
	const evs = sunEvents(nowMs, lat, lon);
	let prev = null;
	let next = null;
	for (const ev of evs) {
		if (ev.t <= nowMs) prev = ev;
		else {
			next = ev;
			break;
		}
	}
	if (!prev || !next) return null;
	return { start: prev.t, end: next.t, isDay: prev.rising };
}

// Next rise/set event at angleDeg strictly after afterMs; null if none within a day.
export function nextEvent(afterMs, lat, lon, angleDeg, rising) {
	const evs = sunEvents(afterMs, lat, lon, angleDeg);
	for (const ev of evs) {
		if (ev.rising === rising && ev.t > afterMs) return ev.t;
	}
	return null;
}

// ---------------------------------------------------------------------------
// Shaot zemaniot
// ---------------------------------------------------------------------------

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


// ---------------------------------------------------------------------------
// Hebrew calendar (ported from convertdate 2.4, MIT)
// ---------------------------------------------------------------------------

const HEBREW_EPOCH_JD = 347995.5;
const GREGORIAN_EPOCH_JD = 1721425.5;

const TISHRI = 7;
const ADAR = 12;
const VEADAR = 13;

// index 1..13
const MONTHS_LATIN = [
	"",
	"Nisan", "Iyar", "Sivan", "Tammuz", "Av", "Elul",
	"Tishrei", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar", "Adar 2",
];
const MONTHS_LATIN_LEAP12 = "Adar 1";

const MONTHS_HEBREW = [
	"",
	"ניסן", "אייר", "סיון", "תמוז", "אב", "אלול",
	"תשרי", "חשון", "כסלו", "טבת", "שבט", "אדר", "אדר ב",
];
const MONTHS_HEBREW_LEAP12 = "אדר א";

export function isLeapYear(year) {
	return ((year * 7) + 1) % 19 < 7;
}

function yearMonths(year) {
	return isLeapYear(year) ? VEADAR : ADAR;
}

// Delay of Rosh Hashana to avoid improper weekdays (dechiyot).
function delay1(year) {
	const months = Math.floor((235 * year - 234) / 19);
	const parts = 12084 + 13753 * months;
	let day = months * 29 + Math.floor(parts / 25920);
	if ((3 * (day + 1)) % 7 < 3) day += 1;
	return day;
}

// Additional delay due to length of adjacent years.
function delay2(year) {
	const last = delay1(year - 1);
	const present = delay1(year);
	const next = delay1(year + 1);
	if (next - present === 356) return 2;
	if (present - last === 382) return 1;
	return 0;
}

function yearDays(year) {
	return hebrewToJd(year + 1, TISHRI, 1) - hebrewToJd(year, TISHRI, 1);
}

export function monthLength(year, month) {
	// Fixed 29-day months: Iyar, Tammuz, Elul, Tevet, Adar 2
	if (month === 2 || month === 4 || month === 6 || month === 10 || month === VEADAR) return 29;
	if (month === ADAR && !isLeapYear(year)) return 29;
	if (month === 8 && yearDays(year) % 10 !== 5) return 29; // Heshvan
	if (month === 9 && yearDays(year) % 10 === 3) return 29; // Kislev
	return 30;
}

export function hebrewToJd(year, month, day) {
	const months = yearMonths(year);
	let jd = HEBREW_EPOCH_JD + delay1(year) + delay2(year) + day + 1;
	if (month < TISHRI) {
		for (let m = TISHRI; m <= months; m++) jd += monthLength(year, m);
		for (let m = 1; m < month; m++) jd += monthLength(year, m);
	} else {
		for (let m = TISHRI; m < month; m++) jd += monthLength(year, m);
	}
	return Math.floor(jd) + 0.5;
}

export function hebrewFromJd(jd) {
	jd = Math.floor(jd) + 0.5;
	const count = Math.floor(((jd - HEBREW_EPOCH_JD) * 98496.0) / 35975351.0);
	let year = count - 1;
	for (let i = count; jd >= hebrewToJd(i, TISHRI, 1); i++) year++;
	const first = jd < hebrewToJd(year, 1, 1) ? TISHRI : 1;
	let month = first;
	while (jd > hebrewToJd(year, month, monthLength(year, month))) month++;
	const day = Math.floor(jd - hebrewToJd(year, month, 1)) + 1;
	return { year, month, day };
}

export function gregorianToJd(year, month, day) {
	const isGregLeap = (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
	const leapAdj = month <= 2 ? 0 : isGregLeap ? -1 : -2;
	return (
		GREGORIAN_EPOCH_JD - 1 +
		365 * (year - 1) +
		Math.floor((year - 1) / 4) -
		Math.floor((year - 1) / 100) +
		Math.floor((year - 1) / 400) +
		Math.floor((367 * month - 362) / 12 + leapAdj + day)
	);
}

export function hebrewFromGregorian(year, month, day) {
	return hebrewFromJd(gregorianToJd(year, month, day));
}

// Hebrew date for a local civil moment, rolling to the next Hebrew day after
// sunset: when the sun is down in the evening (hour >= 12), the Jewish day
// has already advanced. (After midnight the civil date has advanced on its own.)
export function hebrewForNow({ year, month, day, hour, sunIsUp }) {
	const rollover = !sunIsUp && hour >= 12 ? 1 : 0;
	return hebrewFromJd(gregorianToJd(year, month, day) + rollover);
}

export function monthName(year, month, hebrewScript = false) {
	if (month === ADAR && isLeapYear(year)) {
		return hebrewScript ? MONTHS_HEBREW_LEAP12 : MONTHS_LATIN_LEAP12;
	}
	return hebrewScript ? MONTHS_HEBREW[month] : MONTHS_LATIN[month];
}

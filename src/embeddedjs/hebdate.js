// Hebrew calendar conversion, ported from convertdate 2.4 (MIT license,
// github.com/fitnr/convertdate). Months are numbered ecclesiastically:
// 1=Nisan .. 7=Tishri .. 12=Adar (Adar 1 in leap years), 13=Adar 2.

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

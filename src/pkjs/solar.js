// Solar event calculations (NOAA algorithm, center-of-disk geometric elevation).
//
// This runs on the PHONE, not the watch: the watch has a hard XS memory
// ceiling and this is the largest block of arithmetic in the project. The
// phone computes a rolling window of upcoming events and sends the
// timestamps, so the watch stays correct offline for as long as the window
// lasts (see src/pkjs/index.js).
//
// CommonJS so both pkjs and the node test suite can load it.

const DEG = Math.PI / 180;
const MS_PER_DAY = 86400000;

// Standard elevation angles (degrees relative to geometric horizon, center of disk).
const SUNRISE_SET_ANGLE = -0.833; // refraction 34' + semidiameter 16'
const TZEIT_ANGLE = -8.5;

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
	const halfTan = Math.tan((obliq / 2) * DEG);
	const y = halfTan * halfTan;   // ** is too new for the pkjs bundler's parser
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
function sunEvents(nowMs, lat, lon, angleDeg = SUNRISE_SET_ANGLE) {
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
function bracket(nowMs, lat, lon) {
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
function nextEvent(afterMs, lat, lon, angleDeg, rising) {
	const evs = sunEvents(afterMs, lat, lon, angleDeg);
	for (const ev of evs) {
		if (ev.rising === rising && ev.t > afterMs) return ev.t;
	}
	return null;
}

module.exports = { sunEvents, bracket, nextEvent, SUNRISE_SET_ANGLE, TZEIT_ANGLE, MS_PER_DAY };

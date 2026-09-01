// Solar event calculations (NOAA algorithm, center-of-disk geometric
// elevation). Ported from src/pkjs/solar.js, which used to run on the phone
// because the Alloy build had no memory for it.
//
// Free of pebble.h on purpose, so the host test harness in test/c can compile
// and check it with plain gcc against the same PyEphem fixtures the JavaScript
// used.
//
// Agreement with PyEphem is ~20s, dominated by refraction modeling near the
// horizon; test/c/run_tests.c documents the tolerance.

#ifndef SOLAR_H
#define SOLAR_H

#include <stdbool.h>

// Degrees relative to the geometric horizon, center of disk.
#define SUNRISE_SET_ANGLE (-0.833)  // refraction 34' + semidiameter 16'
#define TZEIT_ANGLE (-8.5)

#define MS_PER_DAY 86400000.0

// Three UTC days x rise/set.
#define SOLAR_MAX_EVENTS 6

// The most candidates solar_rank_event() will consider: sunrise, sunset and
// nightfall, which is every event a "Next" box can be configured to name.
#define SOLAR_RANK_MAX 3

typedef struct {
  double t_ms;
  bool rising;
} SolarEvent;

typedef struct {
  bool valid;  // false at polar latitudes where the sun does not cross
  double start_ms;
  double end_ms;
  bool is_day;  // the sun is currently up
} SolarBracket;

// Fills out[] with the events at angle_deg over the three UTC days around
// now_ms, ascending. Returns the count (<= SOLAR_MAX_EVENTS).
int solar_events(double now_ms, double lat, double lon, double angle_deg,
                 SolarEvent *out);

// The current half-day: previous sunrise/sunset through the next one.
SolarBracket solar_bracket(double now_ms, double lat, double lon);

// Next event at angle_deg strictly after after_ms. False if none within a day.
bool solar_next_event(double after_ms, double lat, double lon, double angle_deg,
                      bool rising, double *out_ms);

// Index of the rank-th soonest of n candidate times, counting rank from 0, or
// -1 when fewer than rank + 1 of them are available. Entries whose have[] is
// false are not candidates at all, and their t_ms is never read. Ties keep the
// lower index, so the caller's declaration order decides them.
//
// This is the arithmetic behind a "Next sunrise or sunset" box and the tap that
// moves it on to the one after. Rank 0 is what the box says at rest; rank 1 is
// what the tap shows. It lives here, away from pebble.h, because the ordering
// is the whole feature and the host harness can walk it across a day where the
// emulator cannot be held still.
int solar_rank_event(const double *t_ms, const bool *have, int n, int rank);

#endif

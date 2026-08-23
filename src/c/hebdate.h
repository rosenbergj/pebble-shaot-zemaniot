// Hebrew calendar arithmetic (ported from convertdate 2.4, MIT, via the
// JavaScript in src/embeddedjs/core.js).
//
// Free of pebble.h on purpose, so the host test harness in test/c can compile
// and check it with plain gcc.

#ifndef HEBDATE_H
#define HEBDATE_H

#include <stdbool.h>

#define HEB_NISAN 1
#define HEB_SIVAN 3
#define HEB_TISHRI 7
#define HEB_ADAR 12
#define HEB_VEADAR 13

typedef struct {
  int year;
  int month;  // 1..13, Nisan-based; see HEB_* above
  int day;
} HebrewDate;

bool hebdate_is_leap_year(int year);
int hebdate_month_length(int year, int month);

// Julian Day at noon-boundary convention (always ends in .5), matching
// convertdate. Doubles hold these exactly.
double hebdate_hebrew_to_jd(int year, int month, int day);
double hebdate_gregorian_to_jd(int year, int month, int day);

HebrewDate hebdate_from_jd(double jd);
HebrewDate hebdate_from_gregorian(int year, int month, int day);

// Hebrew date for a local civil moment, rolling to the next Hebrew day after
// sunset: when the sun is down in the evening (hour >= 12) the Jewish day has
// already advanced. After midnight the civil date has advanced on its own.
HebrewDate hebdate_for_now(int year, int month, int day, int hour, bool sun_is_up);

// Transliterated, or Hebrew script when hebrew_script. Returns static storage.
const char *hebdate_month_name(int year, int month, bool hebrew_script);

#endif

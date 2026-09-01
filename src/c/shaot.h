// Shaot zemaniot arithmetic: proportional hours and chalakim.
//
// Free of pebble.h on purpose, so the host test harness in test/c can compile
// and check it with plain gcc. Ported from the JavaScript in
// src/embeddedjs/core.js.

#ifndef SHAOT_H
#define SHAOT_H

#include <stdbool.h>
#include <stddef.h>

#define CHALAKIM_PER_HOUR 1080
#define CHALAKIM_PER_MINUTE 18
#define CHALAKIM_PER_HALF_DAY (12 * CHALAKIM_PER_HOUR)

// Whole chalakim elapsed since the bracket start, clamped to
// [0, CHALAKIM_PER_HALF_DAY - 1]. Times are epoch milliseconds; double holds
// them exactly (2^53 ms is far beyond any date we care about).
int shaot_chalakim_now(double now_ms, double start_ms, double end_ms);

// Display hour for hour index 0-11.
//   0-based: 0..11    (sunrise 0.00, true noon 6.00, flips to 0.00 at sunset)
//   6-based: 6..12,1..5 (sunrise 6.00, true noon 12.00, flips to 6.00)
int shaot_display_hour(int hour_index, bool offset6);

// "H.MM.CC" when with_minutes, otherwise "H.CCCC" raw chalakim in the hour.
void shaot_format(int chalakim, bool offset6, bool with_minutes,
                  char *out, size_t out_size);

// Ordinary clock seconds remaining, for the countdown from sunset to nightfall:
// "M:SS" under an hour and "H:MM:SS" at or over it, so the common case stays as
// narrow as possible. Counts the second in progress, so it reads "0:01" for a
// second and then stops rather than sitting on "0:00"; a negative remainder
// formats as "0:01" for the same reason. Minutes are not zero-padded under an
// hour: the line is centered, so a leading zero would only cost width.
void shaot_format_countdown(int seconds, char *out, size_t out_size);

#endif

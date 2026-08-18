// Weather state and the decisions that can be made without a screen.
//
// Deliberately free of pebble.h, like shaot.c, hebdate.c and solar.c, so the
// day selection, the unit conversion and the staleness rule can be checked on
// the host. Loading and drawing the icons lives in main.c, because that needs
// the graphics context.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Icon ids. These are wire values -- the phone sends the number, not the name
// -- so the order is fixed. It matches TimeStyle's, whose icons we use.
typedef enum {
  WCOND_CLEAR_DAY = 0,
  WCOND_CLEAR_NIGHT = 1,
  WCOND_CLOUDY = 2,
  WCOND_HEAVY_RAIN = 3,
  WCOND_HEAVY_SNOW = 4,
  WCOND_LIGHT_RAIN = 5,
  WCOND_LIGHT_SNOW = 6,
  WCOND_PARTLY_CLOUDY_NIGHT = 7,
  WCOND_PARTLY_CLOUDY = 8,
  WCOND_RAINING_AND_SNOWING = 9,
  WCOND_THUNDERSTORM = 10,
  WCOND_GENERIC = 11,
  WCOND_COUNT = 12,
} WeatherCond;

// Two days is all the forecast toggle can ever show: today, and tomorrow once
// the cutoff has passed.
#define WEATHER_DAYS 2

// Before this hour the forecast box means today; from it, tomorrow. Today's
// high stops being useful at about the point it stops being achievable.
#define WEATHER_CUTOFF_HOUR 18

// Six missed refreshes at TimeStyle's half-hourly cadence. Past this the
// reading is shown faded rather than as though it were current.
#define WEATHER_STALE_SECS (3 * 60 * 60)

typedef struct {
  int32_t fetched_at;  // unix time of the last successful fetch, 0 = never
  int16_t temp_c;      // current temperature, always Celsius on the wire
  uint8_t cond;        // WeatherCond for right now
  uint8_t have_current;

  // Each day carries the local calendar date it describes, so the watch can
  // pick the right one itself. The phone cannot: the box has to roll over at
  // the cutoff whether or not a fetch happens, and a fetch that spans local
  // midnight would otherwise leave "day 0" quietly meaning yesterday.
  int32_t day_ymd[WEATHER_DAYS];
  int16_t day_high_c[WEATHER_DAYS];
  int16_t day_low_c[WEATHER_DAYS];
  uint8_t day_cond[WEATHER_DAYS];
  uint8_t have_days;  // bit i set means slot i holds a real forecast
} WeatherData;

// Pack a calendar date into the comparable integer the wire uses, e.g.
// 2026-08-18 becomes 20260818. Comparing dates rather than timestamps keeps
// this correct across a DST change, where "tomorrow" is not always 86400s on.
int32_t weather_ymd(int year, int mon1, int mday);

// Celsius to Fahrenheit in integer arithmetic, rounding half away from zero.
int weather_c_to_f(int c);

// The calendar day after ymd. Done here rather than with mktime(): newlib's
// date routines are the sort with hidden machinery that has failed on this
// hardware before, and this is a dozen lines of arithmetic that the host
// harness can check outright.
int32_t weather_next_ymd(int32_t ymd);

// The date the forecast box means right now: today until the cutoff hour,
// tomorrow from it. Takes the parts of a local struct tm rather than a
// timestamp, so the rule can be checked on the host at any hour of any day --
// which the emulator cannot help with, since its clock resyncs to the host's
// within seconds of being set.
int32_t weather_wanted_ymd(int year, int mon1, int mday, int hour);

// Index of the forecast describing wanted_ymd, or -1 if we do not hold it.
int weather_pick_day(const WeatherData *w, int32_t wanted_ymd);

// True once the reading is too old to present as current. Never-fetched data
// is not stale; it is absent, which the caller distinguishes by have_current.
bool weather_is_stale(const WeatherData *w, int32_t now);

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

// The forecast box only ever *shows* two days -- today, and tomorrow once the
// cutoff has passed -- but a third is held, and it is not spare capacity to be
// trimmed. It is how long the box survives with the phone gone.
//
// A payload fetched on day D answers for D and D+1. From 06:00 on D+1 the low
// belongs to D+2, and at 18:00 on D+1 the box itself rolls to D+2, so two days
// runs out around lunchtime the day after the last fetch: first the low is
// silently replaced by one already behind the wearer, then the whole box goes
// to --/--. Carrying D+2 pushes both of those a full day out, which is what it
// takes to cross a Shabbat or a flat phone battery and still be right.
#define WEATHER_DAYS 3

// Before this hour the forecast box means today; from it, tomorrow. Today's
// high stops being useful at about the point it stops being achievable.
#define WEATHER_CUTOFF_HOUR 18

// From this hour the low is taken from the *next* day, whatever day the box is
// naming. A daily minimum lands just before dawn, so by mid-morning today's is
// hours behind you while tomorrow's is the next one you will actually feel.
#define WEATHER_LOW_CUTOFF_HOUR 6

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

// The date whose low the box should show, which is not always the date the box
// names. A day's minimum is a pre-dawn reading, so showing "today's low" after
// breakfast means showing a temperature already behind you; from
// WEATHER_LOW_CUTOFF_HOUR the next one due is tomorrow's. After the main cutoff
// the box names tomorrow anyway and this agrees with it, so the two rules meet
// rather than fight. It is deliberate that between the two hours the box reads
// "today" over a low belonging to tomorrow: the useful low is the next one.
int32_t weather_low_ymd(int year, int mon1, int mday, int hour);

// True when the pair should read low/high rather than high/low, which is the
// order the two are actually due in. Between the cutoffs the high is this
// afternoon's and the low belongs to the following dawn, so high comes first;
// outside them the next thing due is the low before sunrise, with the high a
// further ten hours out. Same two hours as the rules above, for the same
// underlying reason -- they are where the day's turning points sit.
//
// Days with an unusual shape, a high at dawn or a low at dusk, will read out of
// order. That is accepted: the common day is what this serves.
bool weather_low_first(int hour);

// Index of the forecast describing wanted_ymd, or -1 if we do not hold it.
int weather_pick_day(const WeatherData *w, int32_t wanted_ymd);

// How long to wait before asking the phone again when a request went
// unanswered, in milliseconds, or 0 to stop chasing and leave it to the hourly
// schedule. `attempt` is how many requests have already gone out without a
// reply, so the first unanswered request asks with attempt = 1.
//
// This exists because the watch cannot tell a request that was never heard
// from one whose answer is still coming: the send is fire-and-forget and the
// phone speaks only when it has something. Waiting for the payload and asking
// again is the only signal available, so the schedule has to be short enough
// to matter on a wrist and short enough overall not to become a second, faster
// polling loop running behind the first.
uint32_t weather_retry_ms(int attempt);

// True once the reading is too old to present as current. Never-fetched data
// is not stale; it is absent, which the caller distinguishes by have_current.
bool weather_is_stale(const WeatherData *w, int32_t now);

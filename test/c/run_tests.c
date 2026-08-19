// Host test harness for the pure C modules.
//
// Mirrors the node suites (test/hebdate.test.mjs, test/shaot.test.mjs,
// test/zmanim.test.mjs) assertion for assertion, against the same fixtures.
// Built with plain gcc -- none of shaot.c, hebdate.c or solar.c may include
// pebble.h, which is exactly why they are separate from main.c.
//
//     make -C test/c && ./test/c/run_tests

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../src/c/shaot.h"
#include "../../src/c/hebdate.h"
#include "../../src/c/solar.h"
#include "../../src/c/trig.h"
#include "../../src/c/numparse.h"
#include "../../src/c/weather.h"
#include "fixtures.h"

// NOAA simplified formulas vs PyEphem's full model and refraction handling:
// agreement within a couple of minutes is expected. A chelek is only ~3.3s of
// wall time, so this tolerance is about model choice, not implementation
// error; see the note in test/zmanim.test.mjs.
#define TOL_MS 120000.0

static int g_checks = 0;
static int g_failures = 0;
static const char *g_group = "";

static void group(const char *name) {
  g_group = name;
  printf("-- %s\n", name);
}

static void check(int ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void check(int ok, const char *fmt, ...) {
  g_checks++;
  if (ok) return;
  g_failures++;
  va_list ap;
  va_start(ap, fmt);
  printf("   FAIL [%s] ", g_group);
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}

static void test_hebrew_dates(void) {
  group("gregorian -> hebrew matches convertdate for all fixture dates");
  for (int i = 0; i < HEB_DATES_N; i++) {
    const HebDateFixture *f = &HEB_DATES[i];
    HebrewDate r = hebdate_from_gregorian(f->g[0], f->g[1], f->g[2]);
    check(r.year == f->h[0] && r.month == f->h[1] && r.day == f->h[2],
          "gregorian %d-%d-%d gave %d-%d-%d, want %d-%d-%d",
          f->g[0], f->g[1], f->g[2], r.year, r.month, r.day,
          f->h[0], f->h[1], f->h[2]);
    check(hebdate_is_leap_year(r.year) == (f->leap != 0),
          "leap flag for %d", f->h[0]);
  }
}

static void test_month_lengths(void) {
  group("month lengths match convertdate");
  for (int i = 0; i < HEB_MONTH_LENS_N; i++) {
    const HebMonthLenFixture *f = &HEB_MONTH_LENS[i];
    for (int m = 1; m <= f->n; m++) {
      int got = hebdate_month_length(f->year, m);
      check(got == f->lengths[m - 1], "year %d month %d gave %d, want %d",
            f->year, m, got, f->lengths[m - 1]);
    }
  }
}

static int same_date(HebrewDate a, HebrewDate b) {
  return a.year == b.year && a.month == b.month && a.day == b.day;
}

static void test_rollover(void) {
  group("sunset rollover advances the Hebrew day in the evening only");
  HebrewDate base = hebdate_from_gregorian(2026, 8, 11);
  HebrewDate next_day = hebdate_from_gregorian(2026, 8, 12);

  HebrewDate day = hebdate_for_now(2026, 8, 11, 15, true);
  check(same_date(day, base), "daytime should not roll over");

  HebrewDate evening = hebdate_for_now(2026, 8, 11, 21, false);
  check(same_date(evening, next_day), "after sunset should be the next Hebrew day");

  HebrewDate night = hebdate_for_now(2026, 8, 12, 2, false);
  check(same_date(night, next_day), "after midnight should not add another day");
}

static void test_month_names(void) {
  group("month names incl. leap-year Adar split");
  check(strcmp(hebdate_month_name(5786, 5, false), "Av") == 0, "5786/5 latin");
  check(strcmp(hebdate_month_name(5786, 5, true), "\xd7\x90\xd7\x91") == 0, "5786/5 hebrew");
  check(hebdate_is_leap_year(5787) == true, "5787 is a leap year");
  check(strcmp(hebdate_month_name(5787, 12, false), "Adar 1") == 0, "5787/12 latin");
  check(strcmp(hebdate_month_name(5787, 13, false), "Adar 2") == 0, "5787/13 latin");
  check(strcmp(hebdate_month_name(5787, 12, true), "\xd7\x90\xd7\x93\xd7\xa8 \xd7\x90") == 0,
        "5787/12 hebrew");
  check(hebdate_is_leap_year(5786) == false, "5786 is not a leap year");
  check(strcmp(hebdate_month_name(5786, 12, false), "Adar") == 0, "5786/12 latin");
}

// Synthetic 12-hour bracket: 2026-08-11 06:00 -> 18:00 UTC
#define BR_START 1786428000000.0
#define BR_END 1786471200000.0
#define BR_MID 1786449600000.0

static void test_chalakim(void) {
  group("chalakim across the bracket");
  check(shaot_chalakim_now(BR_START, BR_START, BR_END) == 0, "at start");
  check(shaot_chalakim_now(BR_END - 1, BR_START, BR_END) == CHALAKIM_PER_HALF_DAY - 1,
        "just before end");
  check(shaot_chalakim_now(BR_START - 5000, BR_START, BR_END) == 0, "clamped below");
  check(shaot_chalakim_now(BR_END + 5000, BR_START, BR_END) == CHALAKIM_PER_HALF_DAY - 1,
        "clamped above");
  check(shaot_chalakim_now(BR_MID, BR_START, BR_END) == 6 * 1080, "true noon is hour 6");
}

static void expect_format(int chalakim, bool offset6, bool with_minutes,
                          const char *want) {
  char buf[32];
  shaot_format(chalakim, offset6, with_minutes, buf, sizeof(buf));
  check(strcmp(buf, want) == 0, "format(%d, offset6=%d, minutes=%d) gave %s, want %s",
        chalakim, offset6, with_minutes, buf, want);
}

static void test_formatting(void) {
  group("formatting per design doc examples");
  int last = CHALAKIM_PER_HALF_DAY - 1;
  expect_format(last, false, true, "11.59.17");
  expect_format(last, false, false, "11.1079");
  expect_format(last, true, true, "5.59.17");
  expect_format(0, false, true, "0.00.00");
  expect_format(0, true, true, "6.00.00");
  expect_format(6 * 1080, false, true, "6.00.00");
  expect_format(6 * 1080, true, true, "12.00.00");
}

static void expect_countdown(int seconds, const char *want) {
  char buf[32];
  shaot_format_countdown(seconds, buf, sizeof(buf));
  check(strcmp(buf, want) == 0, "countdown(%d) gave %s, want %s", seconds, buf, want);
}

static void test_countdown_formatting(void) {
  group("sunset-to-nightfall countdown counts the second in progress");
  // The second in progress counts, so every reading is one above the remainder.
  expect_countdown(0, "0:01");
  expect_countdown(1, "0:02");
  expect_countdown(58, "0:59");
  expect_countdown(59, "1:00");
  expect_countdown(60, "1:01");
  // A typical ben-hashmashot is around 45 minutes at these latitudes.
  expect_countdown(44 * 60, "44:01");
  expect_countdown(59 * 60 + 58, "59:59");
  // An hour and over gains an hours field rather than running the minutes up.
  expect_countdown(59 * 60 + 59, "1:00:00");
  expect_countdown(3600, "1:00:01");
  expect_countdown(2 * 3600 + 5 * 60 + 9, "2:05:10");
  // Past the event, and a clock that jumped forward: never a negative or zero
  // reading, because the caller stops drawing it the moment the window closes.
  expect_countdown(-1, "0:01");
  expect_countdown(-500, "0:01");
}

static void test_display_hours(void) {
  group("6-based display hours run 6..11,12,1..5");
  const int expected[12] = {6, 7, 8, 9, 10, 11, 12, 1, 2, 3, 4, 5};
  for (int h = 0; h < 12; h++) {
    check(shaot_display_hour(h, true) == expected[h], "offset6 hour %d", h);
    check(shaot_display_hour(h, false) == h, "0-based hour %d", h);
  }
}

static void test_zmanim(void) {
  group("solar brackets and tzeit match PyEphem within tolerance");
  for (int i = 0; i < ZMANIM_N; i++) {
    const ZmanimFixture *f = &ZMANIM[i];

    SolarBracket b = solar_bracket(f->t, f->lat, f->lon);
    check(b.valid, "%s: bracket should exist", f->name);
    if (b.valid) {
      check(fabs(b.start_ms - f->prev) < TOL_MS, "%s: start off by %.0fs",
            f->name, (b.start_ms - f->prev) / 1000.0);
      check(fabs(b.end_ms - f->next) < TOL_MS, "%s: end off by %.0fs",
            f->name, (b.end_ms - f->next) / 1000.0);
      check(b.is_day == (f->prev_rising != 0), "%s: isDay", f->name);
    }

    double tz;
    bool got = solar_next_event(f->t, f->lat, f->lon, TZEIT_ANGLE, false, &tz);
    if (!f->has_tzeit) {
      check(!got, "%s: expected no tzeit (white nights)", f->name);
    } else {
      check(got, "%s: tzeit should exist", f->name);
      if (got) {
        check(fabs(tz - f->tzeit_next) < TOL_MS, "%s: tzeit off by %.0fs",
              f->name, (tz - f->tzeit_next) / 1000.0);
      }
    }
  }
}

// The watch cannot use libm's trigonometry: newlib's sin() overruns the app
// stack during argument reduction. trig.c replaces it, so it has to be checked
// against the real thing -- on the host, where libm works.
//
// Ranges are the ones the solar code actually produces. The mean anomaly is not
// reduced modulo a turn before use, so sz_sin() sees arguments of several
// hundred radians and the quality of the argument reduction is what is really
// under test here.
static void test_trig(void) {
  group("trig matches libm over the ranges the solar code uses");

  // Absolute error this small leaves the sunrise/sunset result accurate to far
  // under a second, which is the only thing that matters downstream.
  const double tol = 1e-12;
  double worst_sin = 0, worst_cos = 0, worst_tan = 0;

  for (int i = -2000; i <= 2000; i++) {
    double x = i * 0.7;  // out to +-1400 radians, past the largest real argument
    double ds = fabs(sz_sin(x) - sin(x));
    double dc = fabs(sz_cos(x) - cos(x));
    if (ds > worst_sin) worst_sin = ds;
    if (dc > worst_cos) worst_cos = dc;
  }
  check(worst_sin < tol, "sz_sin worst error %.3g over +-1400 rad", worst_sin);
  check(worst_cos < tol, "sz_cos worst error %.3g over +-1400 rad", worst_cos);

  // tan is only ever called on obliquity/2, around 11.7 degrees, but check a
  // decent span away from the poles where it is legitimately ill conditioned.
  for (int i = -70; i <= 70; i++) {
    double x = i * (3.14159265358979323846 / 180.0);
    double dt = fabs(sz_tan(x) - tan(x));
    if (dt > worst_tan) worst_tan = dt;
  }
  check(worst_tan < tol, "sz_tan worst error %.3g over +-70 deg", worst_tan);

  // asin and acos take the full domain: the hour-angle cosine reaches +-1 at
  // the solstices in high latitudes, which is exactly where the naive Newton
  // iteration would fall apart.
  double worst_asin = 0, worst_acos = 0;
  for (int i = -10000; i <= 10000; i++) {
    double x = i / 10000.0;
    double da = fabs(sz_asin(x) - asin(x));
    double dc = fabs(sz_acos(x) - acos(x));
    if (da > worst_asin) worst_asin = da;
    if (dc > worst_acos) worst_acos = dc;
  }
  check(worst_asin < 1e-11, "sz_asin worst error %.3g over [-1,1]", worst_asin);
  check(worst_acos < 1e-11, "sz_acos worst error %.3g over [-1,1]", worst_acos);

  // Endpoints must not produce NaN: a cosine marginally outside [-1,1] from
  // rounding has to clamp, not poison every later calculation.
  check(sz_asin(1.0) == sz_asin(1.5), "sz_asin clamps above 1");
  check(sz_acos(-1.0) == sz_acos(-1.5), "sz_acos clamps below -1");

  double worst_sqrt = 0;
  for (int i = 1; i <= 20000; i++) {
    double x = i / 1000.0;
    double d = fabs(sz_sqrt(x) - sqrt(x)) / sqrt(x);
    if (d > worst_sqrt) worst_sqrt = d;
  }
  check(worst_sqrt < 1e-14, "sz_sqrt worst relative error %.3g", worst_sqrt);
  check(sz_sqrt(0.0) == 0.0, "sz_sqrt(0) is 0");
}

// Settings arrive from Clay as text -- its select components read a DOM
// <select>, whose value is always a string. This parser replaced strtol, which
// behaved inconsistently on the watch, so it needs checking properly.
static void test_numparse(void) {
  group("numparse reads the settings values Clay sends");

  int32_t v;
  // The slot and font values, which is what actually broke.
  for (int i = 0; i <= 9; i++) {
    char buf[2] = {(char)('0' + i), '\0'};
    v = -999;
    check(numparse_int(buf, &v) && v == i, "numparse \"%s\" -> %d", buf, i);
  }

  check(numparse_int("42", &v) && v == 42, "two digits");
  check(numparse_int("-7", &v) && v == -7, "negative");
  check(numparse_int("+7", &v) && v == 7, "explicit plus");
  check(numparse_int(" 5 ", &v) && v == 5, "surrounding spaces");
  check(numparse_int("007", &v) && v == 7, "leading zeros are decimal, not octal");

  // The colour picker's conventional form.
  check(numparse_int("0x007882", &v) && v == 0x007882, "hex colour");
  check(numparse_int("0X00FF00", &v) && v == 0x00FF00, "capital hex prefix");
  check(numparse_int("0xffffff", &v) && v == 0xFFFFFF, "lowercase hex digits");

  // Rejections: a bad value must leave the setting alone rather than become 0.
  v = 123;
  check(!numparse_int("", &v) && v == 123, "empty string rejected, out untouched");
  check(!numparse_int("-", &v) && v == 123, "lone sign rejected");
  check(!numparse_int("abc", &v) && v == 123, "non-numeric rejected");
  check(!numparse_int("12x", &v) && v == 123, "trailing junk rejected");
  check(!numparse_int("0x", &v) && v == 123, "bare 0x rejected");
  check(!numparse_int(NULL, &v) && v == 123, "NULL rejected");

  check(numparse_int("2147483647", &v) && v == 2147483647, "int32 max");
  check(numparse_int("99999999999", &v) && v == 2147483647, "overflow saturates");
}

static void test_weather(void) {
  group("celsius to fahrenheit rounds half away from zero");
  check(weather_c_to_f(0) == 32, "0C is 32F, got %d", weather_c_to_f(0));
  check(weather_c_to_f(100) == 212, "100C is 212F, got %d", weather_c_to_f(100));
  check(weather_c_to_f(20) == 68, "20C is 68F, got %d", weather_c_to_f(20));
  check(weather_c_to_f(37) == 99, "37C is 98.6F, rounds to 99, got %d", weather_c_to_f(37));
  // The negative half-case is the one integer division gets wrong by truncating
  // toward zero: -5C is exactly 23F, and -17C is -1.4F, which rounds to -1.
  check(weather_c_to_f(-5) == 23, "-5C is 23F, got %d", weather_c_to_f(-5));
  check(weather_c_to_f(-17) == 1, "-17C is 1.4F, rounds to 1, got %d", weather_c_to_f(-17));
  check(weather_c_to_f(-18) == 0, "-18C is -0.4F, rounds to 0, got %d", weather_c_to_f(-18));
  check(weather_c_to_f(-40) == -40, "-40 is the crossing point, got %d", weather_c_to_f(-40));

  group("ymd packs dates in comparable order");
  check(weather_ymd(2026, 8, 18) == 20260818, "2026-08-18 packs to 20260818");
  check(weather_ymd(2026, 12, 31) < weather_ymd(2027, 1, 1), "new year increases");
  check(weather_ymd(2026, 1, 9) < weather_ymd(2026, 1, 10), "single digit day sorts low");

  group("next calendar day");
  check(weather_next_ymd(20260818) == 20260819, "mid-month");
  check(weather_next_ymd(20260831) == 20260901, "month end");
  check(weather_next_ymd(20261231) == 20270101, "year end");
  check(weather_next_ymd(20260228) == 20260301, "february in a common year");
  check(weather_next_ymd(20240228) == 20240229, "february in a leap year");
  check(weather_next_ymd(20240229) == 20240301, "leap day");
  check(weather_next_ymd(19000228) == 19000301, "1900 is not a leap year");
  check(weather_next_ymd(20000228) == 20000229, "2000 is a leap year");
  check(weather_next_ymd(20260430) == 20260501, "thirty-day month");

  group("forecast means today until the cutoff, tomorrow after");
  check(weather_wanted_ymd(2026, 8, 18, 0) == 20260818, "just after midnight is today");
  check(weather_wanted_ymd(2026, 8, 18, 17) == 20260818, "the hour before the cutoff is today");
  check(weather_wanted_ymd(2026, 8, 18, 18) == 20260819, "the cutoff hour itself is tomorrow");
  check(weather_wanted_ymd(2026, 8, 18, 23) == 20260819, "late evening is tomorrow");
  // The cutoff and a month or year boundary landing together is the case that
  // would quietly show a forecast for a day that does not exist.
  check(weather_wanted_ymd(2026, 8, 31, 19) == 20260901, "evening of a month end");
  check(weather_wanted_ymd(2026, 12, 31, 19) == 20270101, "new year's eve");
  check(weather_wanted_ymd(2024, 2, 28, 19) == 20240229, "evening before a leap day");

  group("which day's low");
  // The low is a pre-dawn reading, so from the small-hours cutoff onward the
  // next one due belongs to tomorrow -- including through the afternoon, when
  // the box itself still reads "today".
  check(weather_low_ymd(2026, 8, 18, 0) == 20260818, "just after midnight, tonight's low is still ahead");
  check(weather_low_ymd(2026, 8, 18, 5) == 20260818, "the hour before the low cutoff is today's");
  check(weather_low_ymd(2026, 8, 18, 6) == 20260819, "the low cutoff hour itself is tomorrow's");
  check(weather_low_ymd(2026, 8, 18, 14) == 20260819, "an afternoon labelled today shows tomorrow's low");
  check(weather_low_ymd(2026, 8, 18, 17) == 20260819, "the hour before the main cutoff is tomorrow's");
  // Past the main cutoff the box names tomorrow, so the two rules agree and
  // the pair on screen belongs to one day again.
  check(weather_low_ymd(2026, 8, 18, 18) == weather_wanted_ymd(2026, 8, 18, 18),
        "at the main cutoff the low and the named day agree");
  check(weather_low_ymd(2026, 8, 18, 23) == weather_wanted_ymd(2026, 8, 18, 23),
        "late evening keeps them agreed");
  check(weather_low_ymd(2026, 8, 31, 9) == 20260901, "morning of a month end rolls the low");
  check(weather_low_ymd(2026, 12, 31, 9) == 20270101, "new year's eve rolls the low");
  check(weather_low_ymd(2024, 2, 28, 9) == 20240229, "a leap day is the next low");

  group("forecast day selection");
  WeatherData w;
  memset(&w, 0, sizeof(w));
  w.have_days = 0x3;
  w.day_ymd[0] = 20260818;
  w.day_ymd[1] = 20260819;
  check(weather_pick_day(&w, 20260818) == 0, "today matches slot 0");
  check(weather_pick_day(&w, 20260819) == 1, "tomorrow matches slot 1");
  // Going offline across local midnight is the case that makes stamping the
  // days worth it: yesterday's payload must not be shown as today's.
  check(weather_pick_day(&w, 20260820) == -1, "a day we do not hold is refused");
  check(weather_pick_day(&w, 20260817) == -1, "a day already past is refused");
  w.have_days = 0x1;
  check(weather_pick_day(&w, 20260819) == -1, "slot present but unset is refused");

  group("staleness");
  memset(&w, 0, sizeof(w));
  check(!weather_is_stale(&w, 1000000), "never fetched is absent, not stale");
  w.have_current = 1;
  w.fetched_at = 1000000;
  check(!weather_is_stale(&w, 1000000), "a fetch just now is fresh");
  check(!weather_is_stale(&w, 1000000 + WEATHER_STALE_SECS), "exactly at the threshold is fresh");
  check(weather_is_stale(&w, 1000000 + WEATHER_STALE_SECS + 1), "one second past is stale");
  // A watch whose clock jumps backwards would otherwise make a fresh reading
  // look arbitrarily old.
  check(!weather_is_stale(&w, 999000), "a clock that went backwards is not stale");
}

int main(void) {
  test_trig();
  test_numparse();
  test_hebrew_dates();
  test_month_lengths();
  test_rollover();
  test_month_names();
  test_chalakim();
  test_formatting();
  test_countdown_formatting();
  test_display_hours();
  test_zmanim();
  test_weather();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

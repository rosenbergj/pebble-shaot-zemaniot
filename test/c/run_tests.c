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

int main(void) {
  test_trig();
  test_hebrew_dates();
  test_month_lengths();
  test_rollover();
  test_month_names();
  test_chalakim();
  test_formatting();
  test_display_hours();
  test_zmanim();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

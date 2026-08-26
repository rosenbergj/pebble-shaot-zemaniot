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
#include "../../src/c/shabbat.h"
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

// Ranking the next solar events, which is what a "Next sunrise or sunset" box
// shows and what a tap moves it on to.
//
// The tap shows rank 1, and the thing worth checking is that rank 1 is right at
// every hour rather than only in daylight. The hour-by-hour walk below is the
// point of this group: the emulator's clock resyncs within seconds of being
// set, so a day cannot be held still there long enough to watch a box tick past
// sunset.
static void test_next_event_ranking(void) {
  group("ranking picks the soonest, then the one after");
  {
    // Declared out of order on purpose: the function sorts, it does not assume.
    const double t[3] = {300, 100, 200};
    const bool all[3] = {true, true, true};
    check(solar_rank_event(t, all, 3, 0) == 1, "rank 0 is the soonest");
    check(solar_rank_event(t, all, 3, 1) == 2, "rank 1 is the one after it");
    check(solar_rank_event(t, all, 3, 2) == 0, "rank 2 is the last of them");
    check(solar_rank_event(t, all, 3, 3) < 0, "there is no fourth of three");

    const bool two[3] = {true, false, true};
    check(solar_rank_event(t, two, 3, 0) == 2, "an absent event is not a candidate");
    check(solar_rank_event(t, two, 3, 1) == 0, "and the rank after it skips over it");
    check(solar_rank_event(t, two, 3, 2) < 0, "two candidates have no third rank");

    const bool one[3] = {false, true, false};
    check(solar_rank_event(t, one, 3, 0) == 1, "one candidate still has a rank 0");
    check(solar_rank_event(t, one, 3, 1) < 0,
          "a box left with one event has nothing for a tap to show");

    const bool none[3] = {false, false, false};
    check(solar_rank_event(t, none, 3, 0) < 0, "no candidates, no answer");

    const double tie[3] = {100, 100, 100};
    check(solar_rank_event(tie, all, 3, 0) == 0, "a tie keeps the lower index");
    check(solar_rank_event(tie, all, 3, 1) == 1, "and hands the next rank to the next one");
  }

  group("a tap moves a Next box on, at every hour of the year");
  {
    // A middle latitude where all three events happen every day.
    const double lat = 39.95, lon = -75.17;
    // 2026-01-01T00:00:00Z, walked hourly for a year.
    const double start_ms = 1767225600000.0;
    const double HOUR_MS = 3600000.0;

    // The three "Next" kinds, as next_kind_wants() in main.c spells them:
    // sunset or nightfall, sunrise or sunset, and all three.
    const bool KINDS[3][3] = {{false, true, true}, {true, true, false}, {true, true, true}};

    int checked = 0, sunset_to_tzeit = 0;
    for (int h = 0; h < 24 * 365; h++) {
      const double now = start_ms + h * HOUR_MS;
      double rise, set, tz;
      const bool have_all[3] = {
          solar_next_event(now, lat, lon, SUNRISE_SET_ANGLE, true, &rise),
          solar_next_event(now, lat, lon, SUNRISE_SET_ANGLE, false, &set),
          solar_next_event(now, lat, lon, TZEIT_ANGLE, false, &tz),
      };
      if (!have_all[0] || !have_all[1] || !have_all[2]) continue;
      const double t[3] = {rise, set, tz};

      for (int k = 0; k < 3; k++) {
        const bool have[3] = {KINDS[k][0] && have_all[0], KINDS[k][1] && have_all[1],
                              KINDS[k][2] && have_all[2]};
        const int a = solar_rank_event(t, have, 3, 0);
        const int b = solar_rank_event(t, have, 3, 1);
        if (a < 0 || b < 0) {
          check(false, "hour %d kind %d: a Next box should always have both ranks", h, k);
          continue;
        }
        if (t[b] <= t[a]) {
          check(false, "hour %d kind %d: the tap should show a later time", h, k);
          continue;
        }
        if (a == b) {
          check(false, "hour %d kind %d: the tap should show a different event", h, k);
          continue;
        }
        checked++;
      }

      // The case that motivated the feature: between sunset and nightfall, a
      // "sunset or nightfall" box names tonight's tzeit, and the tap should
      // reach tomorrow evening's sunset rather than anything today.
      if (tz < set) {
        const bool set_tz[3] = {false, true, true};
        const int a = solar_rank_event(t, set_tz, 3, 0);
        const int b = solar_rank_event(t, set_tz, 3, 1);
        if (a != 2 || b != 1) {
          check(false, "hour %d: between sunset and nightfall, tzeit then tomorrow's sunset", h);
          continue;
        }
        // Tomorrow's sunset, not some sunset days away.
        const double gap = (t[b] - now) / HOUR_MS;
        if (gap < 12.0 || gap > 30.0) {
          check(false, "hour %d: the next sunset should be tomorrow's, not %.1fh away", h, gap);
          continue;
        }
        sunset_to_tzeit++;
      }
    }
    check(checked == 3 * 24 * 365, "every hour of the year ranked, for all three kinds");
    // The window is about forty minutes, so an hourly walk lands inside it on
    // roughly two days in three -- enough to say the case above was actually
    // reached, without pinning the count to the length of a particular dusk.
    check(sunset_to_tzeit > 150 && sunset_to_tzeit < 365,
          "the walk should pass through a few hundred sunset-to-nightfall hours, got %d",
          sunset_to_tzeit);
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

  group("which reading comes first");
  // The pair reads in the order the two are due. Same hours as the rules above.
  check(!weather_low_first(6), "at the low cutoff the high is next, so it leads");
  check(!weather_low_first(12), "midday leads with the high");
  check(!weather_low_first(17), "the hour before the main cutoff still leads with the high");
  check(weather_low_first(18), "at the main cutoff the next thing due is the low");
  check(weather_low_first(23), "late evening leads with the low");
  check(weather_low_first(0), "midnight leads with the low");
  check(weather_low_first(5), "the hour before the low cutoff still leads with the low");
  // The order flips exactly where the low's day does, because both track the
  // same turning points; a mismatch would show two readings from one day in
  // the wrong sequence.
  for (int h = 0; h < 24; h++) {
    const bool same_day = (weather_low_ymd(2026, 8, 18, h) == weather_wanted_ymd(2026, 8, 18, h));
    check(weather_low_first(h) == same_day, "order flips with the low's day");
  }

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
  w.day_ymd[2] = 20260820;
  w.have_days = 0x7;
  check(weather_pick_day(&w, 20260820) == 2, "the endurance day matches slot 2");
  w.have_days = 0x1;
  check(weather_pick_day(&w, 20260819) == -1, "slot present but unset is refused");

  group("how long a payload lasts with the phone gone");
  // The reason WEATHER_DAYS is what it is, written as a test so that trimming
  // it back fails here rather than on someone's wrist a day into a Shabbat.
  // A payload fetched on day D stamps D .. D+WEATHER_DAYS-1; walk the clock
  // forward through it and past it, and check where the box stops answering.
  memset(&w, 0, sizeof(w));
  int32_t stamp = weather_ymd(2026, 8, 21);  // a Friday
  for (int i = 0; i < WEATHER_DAYS; i++) {
    w.day_ymd[i] = stamp;
    w.have_days |= (uint8_t)(1u << i);
    stamp = weather_next_ymd(stamp);
  }
  const int32_t last_held = w.day_ymd[WEATHER_DAYS - 1];

  int32_t cur = w.day_ymd[0];
  for (int off = 0; off <= WEATHER_DAYS; off++) {
    const int y = (int)(cur / 10000), mo = (int)((cur / 100) % 100), md = (int)(cur % 100);
    for (int h = 0; h < 24; h++) {
      const int32_t want = weather_wanted_ymd(y, mo, md, h);
      const int32_t lowd = weather_low_ymd(y, mo, md, h);
      const bool have_high = weather_pick_day(&w, want) >= 0;
      const bool have_low = weather_pick_day(&w, lowd) >= 0;
      check(have_high == (want <= last_held), "the named day is answerable exactly while held");
      check(have_low == (lowd <= last_held), "the low's day is answerable exactly while held");
      // main.c falls back to the named day's low when it cannot get the right
      // one, which shows a temperature already behind the wearer and says
      // nothing about it. Pin down when that can happen: only in the window
      // between the two cutoffs on the very last day the payload covers.
      if (have_high && !have_low) {
        check(want == last_held && h >= WEATHER_LOW_CUTOFF_HOUR && h < WEATHER_CUTOFF_HOUR,
              "a substituted low happens only on the payload's final day");
      }
    }
    cur = weather_next_ymd(cur);
  }

  // The same thing said in dates, because that is how the promise is made:
  // fetch on Friday evening, and the box is still right at Saturday dusk --
  // which two days' worth was not -- correct through Sunday dawn, high-only
  // through Sunday daytime, and honest about knowing nothing from Sunday
  // evening on.
  check(weather_pick_day(&w, weather_wanted_ymd(2026, 8, 22, 18)) >= 0,
        "Saturday dusk still names a day it holds");
  check(weather_pick_day(&w, weather_low_ymd(2026, 8, 23, 5)) >= 0,
        "Sunday before dawn still has the right low");
  check(weather_pick_day(&w, weather_wanted_ymd(2026, 8, 23, 12)) >= 0,
        "Sunday midday still has the right high");
  check(weather_pick_day(&w, weather_low_ymd(2026, 8, 23, 12)) < 0,
        "Sunday midday has run out of lows");
  check(weather_pick_day(&w, weather_wanted_ymd(2026, 8, 23, 18)) < 0,
        "Sunday evening has run out entirely");

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

  group("chasing an unanswered request");
  // The properties that matter, rather than the numbers themselves: the chase
  // must start, must always end, and must not tighten into a poll.
  check(weather_retry_ms(1) > 0, "an unanswered request is chased");
  uint32_t total = 0;
  int chases = 0;
  uint32_t prev = 0;
  for (int attempt = 1; attempt <= 20; attempt++) {
    const uint32_t ms = weather_retry_ms(attempt);
    if (ms == 0) break;
    check(ms >= prev, "each wait is at least as long as the one before it");
    prev = ms;
    total += ms;
    chases++;
  }
  check(weather_retry_ms(chases + 1) == 0, "the chase gives up rather than running for ever");
  check(chases >= 2 && chases <= 4, "a handful of chases, not a polling loop");
  // Short enough to be worth having on a wrist: the whole schedule has to
  // finish well inside the five-minute sweep that backs it up, or it is just a
  // slower copy of it.
  check(total < 5 * 60 * 1000, "the whole chase finishes inside the sweep that backs it up");
  check(weather_retry_ms(0) == 0, "no chase before a request has gone unanswered");
}

// --- Shabbat and yom tov -----------------------------------------------------

// A synthetic sun, so the definition can be walked without dragging solar.c and
// a location in with it. What the definition turns on is sundown and nightfall,
// not where they fall, and putting them at fixed hours is what makes a two-year
// hour-by-hour walk cheap enough to run on every build.
#define T_SUNRISE 6
#define T_SUNSET 19
#define T_TZEIT 20

static int greg_month_len(int y, int m) {
  static const int len[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
  return len[m - 1];
}

static void greg_next(int *y, int *m, int *d) {
  if (++(*d) > greg_month_len(*y, *m)) {
    *d = 1;
    if (++(*m) > 12) { *m = 1; (*y)++; }
  }
}

// 2026-08-22 was a Saturday; everything else is counted from there, so the
// weekday never depends on a formula this file would also have to be right about.
static int wday_for(int y, int m, int d) {
  const int delta = (int)(hebdate_gregorian_to_jd(y, m, d) -
                          hebdate_gregorian_to_jd(2026, 8, 22));
  const int w = (6 + delta % 7) % 7;
  return w < 0 ? w + 7 : w;
}

static ShabbatNow moment(int y, int m, int d, int hour, bool second_days) {
  ShabbatNow n;
  n.sun_is_up = (hour >= T_SUNRISE && hour < T_SUNSET);
  const HebrewDate h = hebdate_for_now(y, m, d, hour, n.sun_is_up);
  n.heb_month = h.month;
  n.heb_day = h.day;
  n.wday = wday_for(y, m, d);
  n.hour = hour;
  n.before_tzeit = (hour < T_TZEIT);
  n.second_days = second_days;
  return n;
}

// The Hebrew date at midday, which is the one the civil day is named by: the
// sun is up, so hebdate_for_now() has not rolled it forward.
static HebrewDate heb_midday(int y, int m, int d) {
  return hebdate_for_now(y, m, d, 12, true);
}

static void test_moved_far(void) {
  // Coarse coordinates throughout, as everywhere else in this suite.
  const double phl_lat = 39.95, phl_lon = -75.17;

  group("a fix landing near the last one is not a move");
  check(!weather_moved_far(phl_lat, phl_lon, phl_lat, phl_lon), "identical");
  // ~1.1km north, and ~850m east at this latitude: GPS wander, or a walk.
  check(!weather_moved_far(phl_lat, phl_lon, phl_lat + 0.01, phl_lon + 0.01),
        "a kilometre or so");
  // ~11km, which is one Open-Meteo grid cell: errands across town.
  check(!weather_moved_far(phl_lat, phl_lon, phl_lat + 0.1, phl_lon),
        "ten kilometres");
  // Just inside 25km, north and south alike.
  check(!weather_moved_far(phl_lat, phl_lon, phl_lat + 0.2, phl_lon), "22km north");
  check(!weather_moved_far(phl_lat, phl_lon, phl_lat - 0.2, phl_lon), "22km south");

  group("a relocation is");
  check(weather_moved_far(phl_lat, phl_lon, phl_lat + 0.3, phl_lon), "33km north");
  check(weather_moved_far(phl_lat, phl_lon, 40.71, -74.01), "Philadelphia to New York");
  check(weather_moved_far(phl_lat, phl_lon, 34.05, -118.24), "a transcontinental flight");
  check(weather_moved_far(phl_lat, phl_lon, 51.51, -0.13), "an ocean");

  group("the threshold is symmetric");
  check(weather_moved_far(40.71, -74.01, phl_lat, phl_lon) ==
            weather_moved_far(phl_lat, phl_lon, 40.71, -74.01),
        "order does not matter for a long move");
  check(weather_moved_far(phl_lat + 0.01, phl_lon, phl_lat, phl_lon) ==
            weather_moved_far(phl_lat, phl_lon, phl_lat + 0.01, phl_lon),
        "order does not matter for a short one");

  group("longitude is scaled by latitude");
  // 0.3 degrees of longitude is ~33km at the equator and ~13km at 68 north.
  // Unscaled, the northern pair would cross the threshold the southern one does
  // and the face would refetch every time a fix wobbled inside a Norwegian
  // town.
  check(weather_moved_far(0.0, 0.0, 0.0, 0.3), "0.3 deg of longitude at the equator");
  check(!weather_moved_far(68.0, 15.0, 68.0, 15.3), "the same 0.3 deg at 68 north");

  group("the antimeridian is crossed the short way");
  // Two points 0.2 degrees apart with 359.8 between them by the naive
  // subtraction. Near the equator, where the scaling cannot hide the error.
  check(!weather_moved_far(0.0, 179.9, 0.0, -179.9), "0.2 deg across the line");
  check(weather_moved_far(0.0, 179.5, 0.0, -179.5), "1 deg across the line");

  group("the poles do not break it");
  // cos() of a mean latitude of 90 is 0, so every longitude collapses to the
  // same point and only the latitude difference is left. That is the right
  // answer there, and it must not be a divide or a NaN.
  check(!weather_moved_far(90.0, 0.0, 90.0, 180.0), "both at the north pole");
  check(weather_moved_far(90.0, 0.0, 88.0, 0.0), "two degrees down from it");
}

static void test_shabbat(void) {
  group("the yom tov table");
  // Every (month, day) a Hebrew year can hold, counted both ways. Months 12 and
  // 13 are in the sweep even though the table has nothing in Adar -- that is
  // the point: a leap year must not add or move a single date.
  int two_day = 0, one_day = 0;
  for (int m = 1; m <= 13; m++) {
    for (int d = 1; d <= 30; d++) {
      if (shabbat_is_yom_tov(m, d, true)) two_day++;
      if (shabbat_is_yom_tov(m, d, false)) one_day++;
    }
  }
  check(two_day == SHABBAT_YOMTOV_COUNT, "thirteen dates with second days kept");
  check(one_day == SHABBAT_YOMTOV_COUNT_ONE_DAY, "eight dates without them");

  // Rosh Hashana is the exception that stops this being "drop every second
  // day", so it is worth its own assertion in both modes.
  check(shabbat_is_yom_tov(HEB_TISHRI, 2, true), "Rosh Hashana II, second days kept");
  check(shabbat_is_yom_tov(HEB_TISHRI, 2, false), "Rosh Hashana II, second days not kept");
  check(shabbat_is_yom_tov(HEB_TISHRI, 16, true), "Sukkot II needs second days");
  check(!shabbat_is_yom_tov(HEB_TISHRI, 16, false), "Sukkot II drops without them");
  check(shabbat_is_yom_tov(HEB_TISHRI, 23, true), "Simchat Torah needs second days");
  check(!shabbat_is_yom_tov(HEB_TISHRI, 23, false), "Simchat Torah drops without them");

  // Days that are emphatically not yom tov, and would be easy to include by
  // accident: the eve of one, the middle of one, and a festival that is not a
  // yom tov at all.
  check(!shabbat_is_yom_tov(HEB_TISHRI, 14, true), "erev Sukkot is not yom tov");
  check(!shabbat_is_yom_tov(HEB_TISHRI, 17, true), "chol hamoed is not yom tov");
  check(!shabbat_is_yom_tov(HEB_NISAN, 18, true), "chol hamoed Pesach is not yom tov");
  check(!shabbat_is_yom_tov(HEB_ADAR, 14, true), "Purim is not yom tov");
  check(!shabbat_is_yom_tov(HEB_VEADAR, 14, true), "nor is Purim in a leap year");
  // Out of range simply matches nothing, which is what lets clause 4 hand it
  // heb_day - 1 without checking first.
  check(!shabbat_is_yom_tov(HEB_TISHRI, 0, true), "day zero matches nothing");

  group("the evening guard");
  // 2026-08-21 was a Friday, 2026-08-22 the Saturday after it. Neither is
  // anywhere near a festival, so these isolate the weekday clauses.
  ShabbatNow n = moment(2026, 8, 21, 4, true);
  check(!shabbat_is_active(&n), "Friday before dawn is not yet Shabbat");
  n = moment(2026, 8, 21, 18, true);
  check(!shabbat_is_active(&n), "Friday with the sun still up is not yet Shabbat");
  n = moment(2026, 8, 21, T_SUNSET, true);
  check(shabbat_kind(&n) == SHABBAT_EREV, "Friday sundown starts it");
  n = moment(2026, 8, 21, 23, true);
  check(shabbat_kind(&n) == SHABBAT_EREV, "and it survives to midnight");
  n = moment(2026, 8, 22, 0, true);
  check(shabbat_kind(&n) == SHABBAT_DAY, "and across it, as Saturday");
  n = moment(2026, 8, 22, 4, true);
  check(shabbat_kind(&n) == SHABBAT_DAY, "Saturday before dawn is Shabbat -- no evening guard here");
  n = moment(2026, 8, 22, T_TZEIT - 1, true);
  check(shabbat_kind(&n) == SHABBAT_DAY, "the hour before nightfall is still Shabbat");
  n = moment(2026, 8, 22, T_TZEIT, true);
  check(!shabbat_is_active(&n), "nightfall ends it");

  group("Shabbat and yom tov, hour by hour for two years");
  // The definition is four clauses that have to meet exactly, and the failures
  // worth catching are joins and off-by-one hours rather than single dates. So
  // walk every hour of two civil years -- which spans two whole Hebrew years of
  // festivals -- and count what goes wrong rather than asserting inside the
  // loop, so a single mistake does not print seventeen thousand times.
  for (int mode = 0; mode < 2; mode++) {
    const bool second_days = (mode == 1);
    int changed_off_boundary = 0;   // state moved at an hour that is not one
    int uncovered_shabbat = 0;      // Friday sundown .. Saturday nightfall gaps
    int uncovered_yomtov = 0;       // a festival hour that came back inactive
    int friday_morning = 0;         // the clause-1 trap
    int small_hours = 0;            // the clause-4 trap
    int festival_days = 0;

    int y = 2026, m = 1, d = 1;
    bool prev = false;
    bool have_prev = false;
    for (int day = 0; day < 730; day++) {
      const int wd = wday_for(y, m, d);
      const HebrewDate today = heb_midday(y, m, d);
      const bool yt_today = shabbat_is_yom_tov(today.month, today.day, second_days);
      if (yt_today) festival_days++;

      for (int h = 0; h < 24; h++) {
        n = moment(y, m, d, h, second_days);
        const bool active = shabbat_is_active(&n);

        // Nothing may start or stop except at sundown or at nightfall. This is
        // what would catch a clause firing at civil midnight, which is where
        // before_tzeit turns over and where a missing evening guard shows up.
        if (have_prev && active != prev && h != T_SUNSET && h != T_TZEIT) {
          changed_off_boundary++;
        }
        prev = active;
        have_prev = true;

        // Clauses 1 and 2, stated as coverage rather than as themselves.
        if ((wd == 5 && h >= T_SUNSET) || (wd == 6 && h < T_TZEIT)) {
          if (!active) uncovered_shabbat++;
        }
        // Clause 3 with clause 4 behind it: a festival runs from the sundown
        // before its Hebrew date through the nightfall after it, so the civil
        // day it is named by must be covered right up to T_TZEIT.
        if (yt_today && h < T_TZEIT && !active) uncovered_yomtov++;

        // The two traps. Both are stated without reference to how the clauses
        // are written, so they still bite if the implementation is rewritten.
        if (active && wd == 5 && h < 12 && !yt_today) friday_morning++;
        if (active && h < T_SUNRISE && wd != 6 && !yt_today) small_hours++;
      }
      greg_next(&y, &m, &d);
    }

    if (second_days) {
      check(changed_off_boundary == 0, "state only ever moves at sundown or nightfall");
      check(uncovered_shabbat == 0, "every Friday sundown to Saturday nightfall is covered");
      check(uncovered_yomtov == 0, "every yom tov is covered to nightfall");
      check(friday_morning == 0, "Friday morning is never Shabbat");
      check(small_hours == 0, "the small hours after a festival are not still it");
      check(festival_days == 26, "two Hebrew years' worth of festivals were met");
    } else {
      check(changed_off_boundary == 0, "one-day: state only moves at sundown or nightfall");
      check(uncovered_shabbat == 0, "one-day: every Shabbat is covered");
      check(uncovered_yomtov == 0, "one-day: every yom tov is covered to nightfall");
      check(friday_morning == 0, "one-day: Friday morning is never Shabbat");
      check(small_hours == 0, "one-day: the small hours after a festival are not still it");
      check(festival_days == 16, "one-day: eight festivals a year were met");
    }
  }

  group("the tail after a festival");
  // Clause 4 only shows itself on a festival that stands alone. Pesach I is
  // followed by Pesach II, so clause 3 covers its evening and clause 4 never
  // fires -- correct, but it proves nothing. Search for one with no festival
  // and no Shabbat on either side, so the four clauses cannot cover for each
  // other and each edge is attributable.
  {
    int y = 2026, m = 1, d = 1;
    int py = 2025, pm = 12, pd = 31;
    bool found = false;
    for (int day = 0; day < 730; day++) {
      const HebrewDate today = heb_midday(y, m, d);
      if (shabbat_is_yom_tov(today.month, today.day, true)) {
        const ShabbatNow ev = moment(y, m, d, T_SUNSET, true);
        const HebrewDate yest = heb_midday(py, pm, pd);
        const int wd = wday_for(y, m, d), pwd = wday_for(py, pm, pd);
        if (!shabbat_is_yom_tov(ev.heb_month, ev.heb_day, true) &&
            !shabbat_is_yom_tov(yest.month, yest.day, true) &&
            wd != 5 && wd != 6 && pwd != 5 && pwd != 6) {
          found = true;
          break;
        }
      }
      py = y; pm = m; pd = d;
      greg_next(&y, &m, &d);
    }
    check(found, "the walk met a festival standing clear of Shabbat and of a second day");
    if (found) {
      ShabbatNow before = moment(py, pm, pd, T_SUNSET - 1, true);
      ShabbatNow start = moment(py, pm, pd, T_SUNSET, true);
      ShabbatNow midday = moment(y, m, d, 12, true);
      ShabbatNow tail = moment(y, m, d, T_SUNSET, true);
      ShabbatNow over = moment(y, m, d, T_TZEIT, true);
      check(!shabbat_is_active(&before), "the afternoon before a festival is ordinary");
      check(shabbat_kind(&start) == SHABBAT_YOM_TOV, "sundown before it starts it");
      check(shabbat_kind(&midday) == SHABBAT_YOM_TOV, "the day itself is clause 3");
      check(shabbat_kind(&tail) == SHABBAT_YT_TZEIT, "the evening after it is clause 4");
      check(!shabbat_is_active(&over), "nightfall after a festival ends it");
    }
  }
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
  test_next_event_ranking();
  test_weather();
  test_moved_far();
  test_shabbat();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

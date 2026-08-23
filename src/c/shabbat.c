#include "shabbat.h"

#include "hebdate.h"

// The thirteen dates. `second` marks the ones that only count while the
// second-days setting is on; the other eight always count.
//
// None of them falls in Adar, so a leap year adds nothing to reason about --
// the table is the same in all six year lengths. None of them falls on the
// last day of its Hebrew month either, which is what lets clause 4 ask about
// "the day before" as heb_day - 1 with no month arithmetic at all.
typedef struct {
  signed char month;
  signed char day;
  bool second;
} YomTov;

static const YomTov YOM_TOV[SHABBAT_YOMTOV_COUNT] = {
    {HEB_TISHRI, 1, false},   // Rosh Hashana I
    {HEB_TISHRI, 2, false},   // Rosh Hashana II -- two days either way
    {HEB_TISHRI, 10, false},  // Yom Kippur
    {HEB_TISHRI, 15, false},  // Sukkot I
    {HEB_TISHRI, 16, true},   // Sukkot II
    {HEB_TISHRI, 22, false},  // Shmini Atzeret
    {HEB_TISHRI, 23, true},   // Simchat Torah
    {HEB_NISAN, 15, false},   // Pesach I
    {HEB_NISAN, 16, true},    // Pesach II
    {HEB_NISAN, 21, false},   // Pesach VII
    {HEB_NISAN, 22, true},    // Pesach VIII
    {HEB_SIVAN, 6, false},    // Shavuot I
    {HEB_SIVAN, 7, true},     // Shavuot II
};

bool shabbat_is_yom_tov(int heb_month, int heb_day, bool second_days) {
  for (int i = 0; i < SHABBAT_YOMTOV_COUNT; i++) {
    if (YOM_TOV[i].month != heb_month || YOM_TOV[i].day != heb_day) continue;
    return second_days || !YOM_TOV[i].second;
  }
  return false;
}

// The sun is down *and* it is the evening rather than the small hours.
//
// Both halves are needed, and leaving the hour off is the trap in this whole
// module. "Friday after sundown" as sun-is-down-and-it-is-Friday also matches
// Friday at four in the morning; the same reading of clause 4 would keep
// Shabbat running all night and most of the next day after a yom tov ended.
//
// hour >= 12 is the same test hebdate_for_now() uses to decide its own sunset
// rollover, so the two always agree about which Jewish day a dark hour belongs
// to. It assumes nightfall lands before midnight, which it does everywhere the
// rest of this face is usable; at high summer latitudes it, and hebdate.c with
// it, would end the day at midnight instead.
static bool is_evening(const ShabbatNow *n) {
  return !n->sun_is_up && n->hour >= 12;
}

ShabbatKind shabbat_kind(const ShabbatNow *n) {
  // 1. Any Friday after sundown.
  if (n->wday == 5 && is_evening(n)) return SHABBAT_EREV;

  // 2. Any Saturday before nightfall. Deliberately *no* evening guard here,
  // where clauses 1 and 4 both need one: Saturday at four in the morning
  // genuinely is Shabbat, and the whole civil day up to tzeit is covered.
  if (n->wday == 6 && n->before_tzeit) return SHABBAT_DAY;

  // 3. Any yom tov, by a Hebrew date that has already rolled at sundown -- so
  // this begins at the right moment on its own, and ends one nightfall early.
  if (shabbat_is_yom_tov(n->heb_month, n->heb_day, n->second_days)) {
    return SHABBAT_YOM_TOV;
  }

  // 4. The tail clause 3 leaves behind: after sundown the date has moved on to
  // the day *after* the festival, so ask whether yesterday was one. heb_day - 1
  // can be 0 at the start of a month, which matches nothing and is the answer
  // anyway, since no yom tov is the last day of its month.
  if (is_evening(n) && n->before_tzeit &&
      shabbat_is_yom_tov(n->heb_month, n->heb_day - 1, n->second_days)) {
    return SHABBAT_YT_TZEIT;
  }

  return SHABBAT_NONE;
}

bool shabbat_is_active(const ShabbatNow *n) {
  return shabbat_kind(n) != SHABBAT_NONE;
}

// Whether the wearer is in Shabbat or yom tov at this moment.
//
// Free of pebble.h on purpose, like shaot.c, hebdate.c, solar.c and weather.c,
// so the whole definition can be walked hour by hour on the host. That is the
// only way to check it: the emulator's clock resyncs within seconds of being
// set, so it cannot be held still across even one sunset, let alone the year
// of boundaries this has to get right.
//
// The definition, in four clauses:
//
//   1. any Friday after sundown
//   2. any Saturday before tzeit
//   3. any yom tov, by Hebrew date
//   4. the sunset-to-tzeit window immediately following a yom tov
//
// Clauses 3 and 4 are a pair, and they are shaped by where the Hebrew date
// turns over. hebdate_for_now() rolls at sunset, so a yom tov's Hebrew date
// covers sunset to sunset -- which is the right start, because yom tov begins
// at sundown, and the wrong end, because it runs to nightfall. Clause 4 is
// exactly that missing tail. Clauses 1 and 2 do the same job for Shabbat, in
// civil weekdays, because Shabbat is a weekday and not a date.
#ifndef SHABBAT_H
#define SHABBAT_H

#include <stdbool.h>

// Everything the definition needs, and nothing that would drag pebble.h in.
// All of it is already cached in main.c by the time this is asked.
typedef struct {
  int heb_month;      // 1..13, Nisan-based, as hebdate.h numbers them
  int heb_day;        // 1..30 -- from hebdate_for_now(), so already sunset-rolled
  int wday;           // civil weekday, 0 = Sunday .. 6 = Saturday
  int hour;           // local civil hour, 0..23
  bool sun_is_up;
  bool before_tzeit;  // now is earlier than today's nightfall
  bool second_days;   // festivals keep their second day
} ShabbatNow;

// Which clause is answering. Reported in the order above, so a yom tov that
// falls on Shabbat reports SHABBAT_DAY -- the weekday is the more useful label
// when both are true, and callers that only want a yes/no should use
// shabbat_is_active() rather than comparing against a particular kind.
typedef enum {
  SHABBAT_NONE = 0,
  SHABBAT_EREV,      // Friday, after sundown
  SHABBAT_DAY,       // Saturday, before nightfall
  SHABBAT_YOM_TOV,   // a yom tov Hebrew date
  SHABBAT_YT_TZEIT,  // sundown to nightfall at the close of a yom tov
} ShabbatKind;

// The thirteen yom tov dates, or eight with second_days off. Rosh Hashana is
// two days either way, so this is not "drop every second day".
#define SHABBAT_YOMTOV_COUNT 13
#define SHABBAT_YOMTOV_COUNT_ONE_DAY 8

// A yom tov by Hebrew date alone. Out-of-range values simply match nothing,
// which is what lets clause 4 ask about heb_day - 1 without bounds-checking it.
bool shabbat_is_yom_tov(int heb_month, int heb_day, bool second_days);

ShabbatKind shabbat_kind(const ShabbatNow *n);
bool shabbat_is_active(const ShabbatNow *n);

#endif

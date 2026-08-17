#include "hebdate.h"

#include <math.h>

#define HEBREW_EPOCH_JD 347995.5
#define GREGORIAN_EPOCH_JD 1721425.5

// Index 1..13. Slot 0 is unused so month numbers index directly.
static const char *const MONTHS_LATIN[] = {
    "",
    "Nisan", "Iyar", "Sivan", "Tammuz", "Av", "Elul",
    "Tishrei", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar", "Adar 2",
};
static const char *const MONTHS_LATIN_LEAP12 = "Adar 1";

static const char *const MONTHS_HEBREW[] = {
    "",
    "ניסן", "אייר",
    "סיון", "תמוז",
    "אב", "אלול",
    "תשרי", "חשון",
    "כסלו", "טבת",
    "שבט", "אדר",
    "אדר ב",
};
static const char *const MONTHS_HEBREW_LEAP12 = "אדר א";

bool hebdate_is_leap_year(int year) {
  return ((year * 7) + 1) % 19 < 7;
}

static int year_months(int year) {
  return hebdate_is_leap_year(year) ? HEB_VEADAR : HEB_ADAR;
}

// Delay of Rosh Hashana to avoid improper weekdays (dechiyot).
// All intermediates stay positive, so C truncation matches JS Math.floor.
static int delay1(int year) {
  int months = (235 * year - 234) / 19;
  long long parts = 12084LL + 13753LL * months;
  int day = months * 29 + (int)(parts / 25920);
  if ((3 * (day + 1)) % 7 < 3) day += 1;
  return day;
}

// Additional delay due to the length of adjacent years.
static int delay2(int year) {
  int last = delay1(year - 1);
  int present = delay1(year);
  int next = delay1(year + 1);
  if (next - present == 356) return 2;
  if (present - last == 382) return 1;
  return 0;
}

static int year_days(int year) {
  // Both endpoints are exact .5 doubles, so the difference is an exact integer.
  return (int)(hebdate_hebrew_to_jd(year + 1, HEB_TISHRI, 1) -
               hebdate_hebrew_to_jd(year, HEB_TISHRI, 1));
}

int hebdate_month_length(int year, int month) {
  // Fixed 29-day months: Iyar, Tammuz, Elul, Tevet, Adar 2
  if (month == 2 || month == 4 || month == 6 || month == 10 || month == HEB_VEADAR) {
    return 29;
  }
  if (month == HEB_ADAR && !hebdate_is_leap_year(year)) return 29;
  if (month == 8 && year_days(year) % 10 != 5) return 29;  // Heshvan
  if (month == 9 && year_days(year) % 10 == 3) return 29;  // Kislev
  return 30;
}

double hebdate_hebrew_to_jd(int year, int month, int day) {
  int months = year_months(year);
  double jd = HEBREW_EPOCH_JD + delay1(year) + delay2(year) + day + 1;
  if (month < HEB_TISHRI) {
    for (int m = HEB_TISHRI; m <= months; m++) jd += hebdate_month_length(year, m);
    for (int m = 1; m < month; m++) jd += hebdate_month_length(year, m);
  } else {
    for (int m = HEB_TISHRI; m < month; m++) jd += hebdate_month_length(year, m);
  }
  return floor(jd) + 0.5;
}

HebrewDate hebdate_from_jd(double jd) {
  jd = floor(jd) + 0.5;
  int count = (int)floor(((jd - HEBREW_EPOCH_JD) * 98496.0) / 35975351.0);
  int year = count - 1;
  for (int i = count; jd >= hebdate_hebrew_to_jd(i, HEB_TISHRI, 1); i++) year++;

  int first = (jd < hebdate_hebrew_to_jd(year, 1, 1)) ? HEB_TISHRI : 1;
  int month = first;
  while (jd > hebdate_hebrew_to_jd(year, month, hebdate_month_length(year, month))) {
    month++;
  }

  HebrewDate out;
  out.year = year;
  out.month = month;
  out.day = (int)floor(jd - hebdate_hebrew_to_jd(year, month, 1)) + 1;
  return out;
}

double hebdate_gregorian_to_jd(int year, int month, int day) {
  bool is_greg_leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  int leap_adj = (month <= 2) ? 0 : (is_greg_leap ? -1 : -2);
  // Every quotient below has a positive numerator, so C truncation matches the
  // JavaScript Math.floor it was ported from; the integer terms outside the
  // division can be added after flooring without changing the result.
  return GREGORIAN_EPOCH_JD - 1
         + 365.0 * (year - 1)
         + (year - 1) / 4
         - (year - 1) / 100
         + (year - 1) / 400
         + ((367 * month - 362) / 12 + leap_adj + day);
}

HebrewDate hebdate_from_gregorian(int year, int month, int day) {
  return hebdate_from_jd(hebdate_gregorian_to_jd(year, month, day));
}

HebrewDate hebdate_for_now(int year, int month, int day, int hour, bool sun_is_up) {
  int rollover = (!sun_is_up && hour >= 12) ? 1 : 0;
  return hebdate_from_jd(hebdate_gregorian_to_jd(year, month, day) + rollover);
}

const char *hebdate_month_name(int year, int month, bool hebrew_script) {
  if (month == HEB_ADAR && hebdate_is_leap_year(year)) {
    return hebrew_script ? MONTHS_HEBREW_LEAP12 : MONTHS_LATIN_LEAP12;
  }
  if (month < 1 || month > HEB_VEADAR) return "";
  return hebrew_script ? MONTHS_HEBREW[month] : MONTHS_LATIN[month];
}

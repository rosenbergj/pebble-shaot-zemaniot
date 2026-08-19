#include "weather.h"

int32_t weather_ymd(int year, int mon1, int mday) {
  return (int32_t)year * 10000 + (int32_t)mon1 * 100 + (int32_t)mday;
}

int weather_c_to_f(int c) {
  // 9/5 as 18/10 so the halfway case can be rounded away from zero without
  // floating point: -5C is 23F, not 22F.
  const int scaled = c * 18;
  const int rounded = (scaled >= 0) ? (scaled + 5) / 10 : (scaled - 5) / 10;
  return rounded + 32;
}

static int days_in_month(int year, int mon1) {
  static const int len[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mon1 == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  if (mon1 < 1 || mon1 > 12) return 30;
  return len[mon1 - 1];
}

int32_t weather_next_ymd(int32_t ymd) {
  int year = (int)(ymd / 10000);
  int mon = (int)((ymd / 100) % 100);
  int day = (int)(ymd % 100);

  day++;
  if (day > days_in_month(year, mon)) {
    day = 1;
    mon++;
    if (mon > 12) {
      mon = 1;
      year++;
    }
  }
  return weather_ymd(year, mon, day);
}

int32_t weather_wanted_ymd(int year, int mon1, int mday, int hour) {
  const int32_t today = weather_ymd(year, mon1, mday);
  return (hour < WEATHER_CUTOFF_HOUR) ? today : weather_next_ymd(today);
}

int32_t weather_low_ymd(int year, int mon1, int mday, int hour) {
  const int32_t today = weather_ymd(year, mon1, mday);
  return (hour < WEATHER_LOW_CUTOFF_HOUR) ? today : weather_next_ymd(today);
}

int weather_pick_day(const WeatherData *w, int32_t wanted_ymd) {
  for (int i = 0; i < WEATHER_DAYS; i++) {
    if ((w->have_days & (1u << i)) && w->day_ymd[i] == wanted_ymd) return i;
  }
  return -1;
}

bool weather_is_stale(const WeatherData *w, int32_t now) {
  if (!w->have_current || w->fetched_at == 0) return false;
  // A clock that has gone backwards -- a manual time change, say -- would make
  // a fresh reading look arbitrarily old. Treat the future as fresh.
  if (now < w->fetched_at) return false;
  return (now - w->fetched_at) > WEATHER_STALE_SECS;
}

#include "weather.h"

#include "trig.h"

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

bool weather_low_first(int hour) {
  return hour < WEATHER_LOW_CUTOFF_HOUR || hour >= WEATHER_CUTOFF_HOUR;
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

uint32_t weather_retry_ms(int attempt) {
  // Three chases, then stop: about a minute and three quarters of trying,
  // which covers a phone app that is slow to start its JavaScript without
  // turning into a poll. Growing gaps because the failures have different
  // shapes -- a lost race is answered by the first retry, while a phone that
  // is busy or has no network needs to be given room rather than asked harder.
  switch (attempt) {
    case 1: return 10000;
    case 2: return 30000;
    case 3: return 60000;
    default: return 0;
  }
}

// Kilometres in a degree of latitude, near enough anywhere. The threshold this
// feeds is a judgement about relevance rather than a measurement, so the
// spheroid's flattening is far below the slack in it.
#define KM_PER_DEG 111.195

// The Pebble toolchain compiles without the GNU extensions that define M_PI,
// the same reason solar.c carries its own.
#define PI 3.14159265358979323846
#define DEG (PI / 180.0)

// Separation squared, in degrees of latitude. Equirectangular: flat-earth error
// over a few hundred kilometres is nothing against a threshold as coarse as the
// one this feeds, and squared so the predicate below needs no root at all.
static double sep2_deg(double lat1, double lon1, double lat2, double lon2) {
  const double dlat = lat2 - lat1;
  double dlon = lon2 - lon1;
  // The short way round the antimeridian. Without this, crossing it reads as
  // most of the way round the world, which is true of the number and not of
  // the journey.
  if (dlon > 180.0) dlon -= 360.0;
  else if (dlon < -180.0) dlon += 360.0;

  // A degree of longitude shrinks toward the poles. Left unscaled it would be
  // counted at its equatorial length everywhere, so a move in the far north
  // would measure larger than it is and cross the threshold early.
  const double east = dlon * sz_cos(((lat1 + lat2) * 0.5) * DEG);
  return east * east + dlat * dlat;
}

bool weather_moved_far(double lat1, double lon1, double lat2, double lon2) {
  const double limit = WEATHER_MOVE_KM / KM_PER_DEG;
  return sep2_deg(lat1, lon1, lat2, lon2) > (limit * limit);
}

double weather_move_km(double lat1, double lon1, double lat2, double lon2) {
  return sz_sqrt(sep2_deg(lat1, lon1, lat2, lon2)) * KM_PER_DEG;
}

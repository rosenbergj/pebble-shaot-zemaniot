#include "solar.h"

#include <math.h>
#include <stddef.h>

// The Pebble toolchain compiles without the GNU extensions that define M_PI,
// so spell it out rather than relying on math.h.
#define PI 3.14159265358979323846
#define DEG (PI / 180.0)

static double julian_century(double ms) {
  return (ms / MS_PER_DAY + 2440587.5 - 2451545.0) / 36525.0;
}

// Solar declination (deg) and equation of time (minutes) at Julian century T.
static void solar_params(double T, double *decl, double *eq_time) {
  double L0 = fmod(280.46646 + T * (36000.76983 + 0.0003032 * T), 360.0);
  double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  double C = sin(M * DEG) * (1.914602 - T * (0.004817 + 0.000014 * T)) +
             sin(2 * M * DEG) * (0.019993 - 0.000101 * T) +
             sin(3 * M * DEG) * 0.000289;
  double omega = 125.04 - 1934.136 * T;
  double app_long = L0 + C - 0.00569 - 0.00478 * sin(omega * DEG);
  double mean_obliq =
      23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
  double obliq = mean_obliq + 0.00256 * cos(omega * DEG);
  double half_tan = tan((obliq / 2.0) * DEG);
  double y = half_tan * half_tan;

  *decl = asin(sin(obliq * DEG) * sin(app_long * DEG)) / DEG;
  *eq_time = (4.0 / DEG) *
             (y * sin(2 * L0 * DEG) - 2 * e * sin(M * DEG) +
              4 * e * y * sin(M * DEG) * cos(2 * L0 * DEG) -
              0.5 * y * y * sin(4 * L0 * DEG) - 1.25 * e * e * sin(2 * M * DEG));
}

// Hour angle (deg) at which the sun's centre reaches angle_deg.
// False when it never does.
static bool hour_angle(double lat_deg, double decl_deg, double angle_deg, double *out) {
  double cos_ha = (cos((90.0 - angle_deg) * DEG) -
                   sin(lat_deg * DEG) * sin(decl_deg * DEG)) /
                  (cos(lat_deg * DEG) * cos(decl_deg * DEG));
  if (cos_ha < -1.0 || cos_ha > 1.0) return false;
  *out = acos(cos_ha) / DEG;
  return true;
}

// Rising or setting event on the UTC day containing day_ms, refined by
// re-evaluating declination and equation of time at the event estimate.
static bool event_for_utc_day(double day_ms, double lat, double lon,
                              double angle_deg, bool rising, double *out) {
  double day_start = floor(day_ms / MS_PER_DAY) * MS_PER_DAY;
  double guess = day_start + MS_PER_DAY / 2.0;
  for (int i = 0; i < 3; i++) {
    double decl, eq_time, ha;
    solar_params(julian_century(guess), &decl, &eq_time);
    if (!hour_angle(lat, decl, angle_deg, &ha)) return false;
    double noon_min = 720.0 - 4.0 * lon - eq_time;
    double ev_min = rising ? noon_min - 4.0 * ha : noon_min + 4.0 * ha;
    guess = day_start + ev_min * 60000.0;
  }
  *out = round(guess);
  return true;
}

int solar_events(double now_ms, double lat, double lon, double angle_deg,
                 SolarEvent *out) {
  int n = 0;
  for (int d = -1; d <= 1; d++) {
    for (int r = 1; r >= 0; r--) {  // rising first, matching the JS source order
      double t;
      if (event_for_utc_day(now_ms + d * MS_PER_DAY, lat, lon, angle_deg,
                            r == 1, &t)) {
        out[n].t_ms = t;
        out[n].rising = (r == 1);
        n++;
      }
    }
  }
  // Insertion sort by time; n is at most SOLAR_MAX_EVENTS.
  for (int i = 1; i < n; i++) {
    SolarEvent key = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].t_ms > key.t_ms) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = key;
  }
  return n;
}

SolarBracket solar_bracket(double now_ms, double lat, double lon) {
  SolarBracket b = {false, 0, 0, false};
  SolarEvent evs[SOLAR_MAX_EVENTS];
  int n = solar_events(now_ms, lat, lon, SUNRISE_SET_ANGLE, evs);

  const SolarEvent *prev = NULL;
  const SolarEvent *next = NULL;
  for (int i = 0; i < n; i++) {
    if (evs[i].t_ms <= now_ms) {
      prev = &evs[i];
    } else {
      next = &evs[i];
      break;
    }
  }
  if (!prev || !next) return b;

  b.valid = true;
  b.start_ms = prev->t_ms;
  b.end_ms = next->t_ms;
  b.is_day = prev->rising;
  return b;
}

bool solar_next_event(double after_ms, double lat, double lon, double angle_deg,
                      bool rising, double *out_ms) {
  SolarEvent evs[SOLAR_MAX_EVENTS];
  int n = solar_events(after_ms, lat, lon, angle_deg, evs);
  for (int i = 0; i < n; i++) {
    if (evs[i].rising == rising && evs[i].t_ms > after_ms) {
      *out_ms = evs[i].t_ms;
      return true;
    }
  }
  return false;
}

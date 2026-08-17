#include "shaot.h"

#include <math.h>
#include <stdio.h>

int shaot_chalakim_now(double now_ms, double start_ms, double end_ms) {
  double span = end_ms - start_ms;
  if (span <= 0) return 0;
  double frac = (now_ms - start_ms) / span;
  double c = floor(frac * CHALAKIM_PER_HALF_DAY);
  if (c < 0) return 0;
  if (c > CHALAKIM_PER_HALF_DAY - 1) return CHALAKIM_PER_HALF_DAY - 1;
  return (int)c;
}

int shaot_display_hour(int hour_index, bool offset6) {
  return offset6 ? ((hour_index + 5) % 12) + 1 : hour_index;
}

void shaot_format(int chalakim, bool offset6, bool with_minutes,
                  char *out, size_t out_size) {
  int h = shaot_display_hour(chalakim / CHALAKIM_PER_HOUR, offset6);
  int rem = chalakim % CHALAKIM_PER_HOUR;
  if (with_minutes) {
    snprintf(out, out_size, "%d.%02d.%02d", h,
             rem / CHALAKIM_PER_MINUTE, rem % CHALAKIM_PER_MINUTE);
  } else {
    snprintf(out, out_size, "%d.%04d", h, rem);
  }
}

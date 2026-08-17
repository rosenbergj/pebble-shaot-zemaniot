#include "trig.h"

#include <stdint.h>
#include <string.h>

#define SZ_PI 3.14159265358979323846
#define SZ_PI_2 1.57079632679489661923

// pi/2 split so that k * PIO2_HI is exact for the k we ever see: PIO2_HI has
// its low 22 bits clear, so the product keeps every significant bit. The
// remainder is carried by PIO2_LO. This is the same trick fdlibm uses, minus
// the multi-precision path we cannot afford the stack for.
#define PIO2_HI 1.57079632673412561417e+00
#define PIO2_LO 6.07710050650619224932e-11

// sin(r) for |r| <= pi/4, Taylor through r^17. The first omitted term is
// r^19/19!, under 1e-19 at the interval edge, so this is limited by double
// precision rather than by truncation.
static double poly_sin(double r) {
  double z = r * r;
  return r * (1.0
              + z * (-1.0 / 6.0
              + z * (1.0 / 120.0
              + z * (-1.0 / 5040.0
              + z * (1.0 / 362880.0
              + z * (-1.0 / 39916800.0
              + z * (1.0 / 6227020800.0
              + z * (-1.0 / 1307674368000.0
              + z * (1.0 / 355687428096000.0)))))))));
}

// cos(r) for |r| <= pi/4, Taylor through r^16.
static double poly_cos(double r) {
  double z = r * r;
  return 1.0
         + z * (-1.0 / 2.0
         + z * (1.0 / 24.0
         + z * (-1.0 / 720.0
         + z * (1.0 / 40320.0
         + z * (-1.0 / 3628800.0
         + z * (1.0 / 479001600.0
         + z * (-1.0 / 87178291200.0
         + z * (1.0 / 20922789888000.0))))))));
}

// Reduce x to r in [-pi/4, pi/4] and report the quadrant. Constant stack: no
// arrays, no tables, no recursion -- which is the whole point of this file.
static int reduce(double x, double *r) {
  double q = x / SZ_PI_2;
  // Round to nearest without libm: adding the half and truncating toward zero
  // is exact for the magnitudes involved.
  double k = (q >= 0.0) ? (double)(long long)(q + 0.5) : (double)(long long)(q - 0.5);
  *r = (x - k * PIO2_HI) - k * PIO2_LO;
  long long ki = (long long)k;
  int quad = (int)(ki & 3);
  if (quad < 0) quad += 4;  // C truncates toward zero, so ki can be negative
  return quad;
}

double sz_sin(double x) {
  double r;
  switch (reduce(x, &r)) {
    case 0: return poly_sin(r);
    case 1: return poly_cos(r);
    case 2: return -poly_sin(r);
    default: return -poly_cos(r);
  }
}

double sz_cos(double x) {
  double r;
  switch (reduce(x, &r)) {
    case 0: return poly_cos(r);
    case 1: return -poly_sin(r);
    case 2: return -poly_cos(r);
    default: return poly_sin(r);
  }
}

double sz_tan(double x) {
  double r;
  int quad = reduce(x, &r);
  double s = poly_sin(r), c = poly_cos(r);
  // Odd quadrants swap sine and cosine and flip the sign.
  return (quad & 1) ? -c / s : s / c;
}

double sz_sqrt(double x) {
  if (!(x > 0.0)) return 0.0;  // also catches NaN
  // Halving the exponent in place gives a starting point good to a few percent,
  // using only integer arithmetic -- frexp() would be another libm dependency.
  uint64_t bits;
  memcpy(&bits, &x, sizeof(bits));
  bits = (bits >> 1) + 0x1ff8000000000000ULL;
  double y;
  memcpy(&y, &bits, sizeof(y));
  // Newton doubles the correct digits each pass; five passes is ample from
  // that start, and the cost does not matter at twice a day.
  for (int i = 0; i < 5; i++) y = 0.5 * (y + x / y);
  return y;
}

// asin for |t| <= 0.5, by Newton on sin(y) = t. Well conditioned there because
// cos(y) >= 0.866, so it converges quadratically from a cheap first guess.
static double asin_small(double t) {
  double y = t + t * t * t / 6.0;
  for (int i = 0; i < 5; i++) {
    double r;
    int quad = reduce(y, &r);
    double s = (quad == 0) ? poly_sin(r) : ((quad == 1) ? poly_cos(r)
              : ((quad == 2) ? -poly_sin(r) : -poly_cos(r)));
    double c = (quad == 0) ? poly_cos(r) : ((quad == 1) ? -poly_sin(r)
              : ((quad == 2) ? -poly_cos(r) : poly_sin(r)));
    y -= (s - t) / c;
  }
  return y;
}

double sz_asin(double x) {
  if (x >= 1.0) return SZ_PI_2;
  if (x <= -1.0) return -SZ_PI_2;
  double a = (x < 0.0) ? -x : x;
  double y;
  if (a <= 0.5) {
    y = asin_small(a);
  } else {
    // Near +-1 the Newton step above degenerates, so fold the argument into
    // the well-conditioned half: asin(a) = pi/2 - 2*asin(sqrt((1-a)/2)).
    y = SZ_PI_2 - 2.0 * asin_small(sz_sqrt((1.0 - a) * 0.5));
  }
  return (x < 0.0) ? -y : y;
}

double sz_acos(double x) {
  if (x >= 1.0) return 0.0;
  if (x <= -1.0) return SZ_PI;
  return SZ_PI_2 - sz_asin(x);
}

// Trigonometry that does not use libm.
//
// newlib's sin() crashes the watch. Its argument reduction, __kernel_rem_pio2,
// declares three 20-element double arrays plus a large lookup table, which
// overruns a Pebble app's stack; the emulator tolerates it and real hardware
// does not. Everything else we need from libm is fine -- plain double
// arithmetic, floor(), round() and fmod() were all verified on the watch -- so
// only the trigonometric functions are reimplemented here.
//
// Accuracy is checked against libm on the host in test/c/run_tests.c, over the
// argument ranges the solar code actually uses.
//
// This header deliberately does not include pebble.h, so the host harness can
// compile it with plain gcc.

#ifndef TRIG_H
#define TRIG_H

// Argument in radians. Reduction is exact enough for |x| up to a few thousand,
// which covers the mean-anomaly terms in the solar series.
double sz_sin(double x);
double sz_cos(double x);
double sz_tan(double x);

// |x| <= 1. Outside that the result is clamped to the endpoint rather than
// returning NaN, so a marginally out-of-range value cannot poison the caller.
double sz_asin(double x);
double sz_acos(double x);

// x >= 0. Newton's method; negative input returns 0.
double sz_sqrt(double x);

#endif  // TRIG_H

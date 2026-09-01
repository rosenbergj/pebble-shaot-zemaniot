// Parse the small integers that arrive from the phone as text.
//
// Clay's select components send their value as a string, and its color picker
// may send "0xRRGGBB", so the watch has to turn text into a number. This does
// that without newlib: strtol() behaved inconsistently on the watch, and after
// newlib's sin() was found to overrun the app stack there is no appetite for
// depending on its string handling either. The grammar needed here is tiny and
// worth owning outright -- and, unlike strtol(), this is checked by the host
// test suite.
//
// Free of pebble.h so the host harness can compile it with plain gcc.

#ifndef NUMPARSE_H
#define NUMPARSE_H

#include <stdbool.h>
#include <stdint.h>

// Accepts optional surrounding spaces, an optional sign, then either "0x"/"0X"
// followed by hex digits or a run of decimal digits. Trailing spaces are
// allowed; anything else makes it fail rather than guess. A leading zero is
// decimal, not octal.
//
// Returns false and leaves *out untouched on anything it cannot read whole,
// including an empty string, a lone sign, or trailing junk.
bool numparse_int(const char *s, int32_t *out);

#endif  // NUMPARSE_H

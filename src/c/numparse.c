#include "numparse.h"

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Digit value, or -1 if c is not a digit in this base.
static int digit(char c, int base) {
  int v;
  if (c >= '0' && c <= '9') {
    v = c - '0';
  } else if (c >= 'a' && c <= 'f') {
    v = c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    v = c - 'A' + 10;
  } else {
    return -1;
  }
  return (v < base) ? v : -1;
}

bool numparse_int(const char *s, int32_t *out) {
  if (!s || !out) return false;

  while (is_space(*s)) s++;

  bool negative = false;
  if (*s == '+' || *s == '-') {
    negative = (*s == '-');
    s++;
  }

  int base = 10;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && digit(s[2], 16) >= 0) {
    base = 16;
    s += 2;
  }

  if (digit(*s, base) < 0) return false;  // needs at least one digit

  // Accumulate in 64 bits so an overlong value saturates rather than wrapping
  // into a plausible-looking small number.
  int64_t acc = 0;
  bool overflow = false;
  for (int d; (d = digit(*s, base)) >= 0; s++) {
    acc = acc * base + d;
    if (acc > 0x7FFFFFFFLL) {
      overflow = true;
      acc = 0x7FFFFFFFLL;
    }
  }

  while (is_space(*s)) s++;
  if (*s != '\0') return false;  // trailing junk: reject rather than guess

  if (overflow && negative) {
    *out = (int32_t)(-0x7FFFFFFFLL - 1);
    return true;
  }
  *out = (int32_t)(negative ? -acc : acc);
  return true;
}

// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// Shared validation for integer command-line arguments: strtoul with full
// error checking, unlike atoi() / a bare strtoul() which silently yield 0
// on garbage.

#ifndef PARSE_H
#define PARSE_H

#include <stdbool.h>

/* Parse an unsigned integer from `s` (base 0 = auto 0x/decimal) into *out,
 * requiring lo <= value <= hi. Prints a diagnostic and returns false on
 * non-numeric text, trailing junk, overflow, or out-of-range. */
bool parse_uint(const char *s, int base, unsigned long lo, unsigned long hi,
                unsigned long *out);

#endif /* PARSE_H */

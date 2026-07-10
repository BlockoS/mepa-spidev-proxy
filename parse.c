// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause

#include "parse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

bool parse_uint(const char *s, int base, unsigned long lo, unsigned long hi,
                unsigned long *out)
{
    char *end;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, base);
    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) {
        fprintf(stderr, "invalid numeric argument '%s'\n", s);
        return false;
    }
    *out = v;
    return true;
}

// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause

#define _GNU_SOURCE   /* vsyslog */
#include "log.h"

#include <stdarg.h>
#include <time.h>

#define PRI_TRACE (-1)   /* internal: raw op-trace line, no severity tag */

static FILE *g_trace;    /* -L sink; NULL => trace disabled */

static const char *pri_tag(int priority)
{
    switch (priority) {
    case LOG_ERR:     return "ERR ";
    case LOG_WARNING: return "WARN ";
    case LOG_NOTICE:  return "NOTE ";
    default:          return "";      /* PRI_TRACE and anything else */
    }
}

/* One line to the -L sink: monotonic-us timestamp + severity tag + body.
 * No-op when no sink is configured. Not line-atomic across threads (three
 * stdio calls) -- fine for the single-threaded daemon; bracket with
 * flockfile()/funlockfile() if that ever changes. */
static void trace_v(int priority, const char *fmt, va_list ap)
{
    struct timespec ts;

    if (g_trace == NULL) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(g_trace, "%llu %s",
            (unsigned long long)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000,
            pri_tag(priority));
    vfprintf(g_trace, fmt, ap);
    fputc('\n', g_trace);
}

void log_open(const char *ident, FILE *trace_sink)
{
    g_trace = trace_sink;
    openlog(ident, LOG_PID, LOG_DAEMON);  /* add LOG_PERROR for console echo */
    if (g_trace != NULL) {
        setvbuf(g_trace, NULL, _IOLBF, 0);   /* prompt + crash-survivable */
    }
}

void log_close(void)
{
    closelog();
}

void log_msg(int priority, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);

    va_start(ap, fmt);            /* tee into the -L record (no-op if unset) */
    trace_v(priority, fmt, ap);
    va_end(ap);
}

void log_trace(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    trace_v(PRI_TRACE, fmt, ap);
    va_end(ap);
}

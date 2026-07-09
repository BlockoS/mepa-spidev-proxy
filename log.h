// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// Logging for lan80xx-spid.
//
// Operational messages (always/error/warn) go to syslog: run as a systemd
// service they land in the journal with proper priority, and it stays
// libc-only (no libsystemd). They are ALSO teed into the -L trace sink so
// the event log remains a complete, inline-correlated record.
//
// log_debug is the high-rate op trace (the -L event log): a direct,
// line-buffered write to the trace sink, deliberately NOT routed through
// syslog/journald (which would rate-limit and cost a syscall per op).
// No sink configured (no -L) => log_debug is a no-op.

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <syslog.h>   /* LOG_NOTICE / LOG_ERR / LOG_WARNING priorities */

/* `ident` is the syslog identity; `trace_sink` receives log_debug output
 * (the -L file, or stderr for `-L -`), or NULL to disable the trace. */
void log_open(const char *ident, FILE *trace_sink);
void log_close(void);

void log_msg(int priority, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void log_trace(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#define log_always(fmt, ...) log_msg(LOG_NOTICE,  fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)  log_msg(LOG_ERR,     fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)   log_msg(LOG_WARNING, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...)  log_trace(fmt, ##__VA_ARGS__)

#endif /* LOG_H */

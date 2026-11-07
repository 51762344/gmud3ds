#ifndef LOG_H
#define LOG_H

#include "gmud.h"

/* The longest world message is ~150 B (钓鱼), so the line buffer has to be
 * big enough to keep it whole; the old 96 B cut it with "...". */
#define LOG_LINE_MAX 256
#define LOG_MAX_LINES 32

void log_init(void);
void log_add(const char *fmt, ...);

int log_count(void);
/* 0 = newest. Returns "" when out of range. */
const char *log_at(int index);
/* osGetTime() stamp of the entry, 0 when out of range. Used by the
 * transient on-screen notices; the bottom screen no longer shows a log. */
u64 log_time(int index);

#endif

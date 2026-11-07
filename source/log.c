#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char s_lines[LOG_MAX_LINES][LOG_LINE_MAX];
static u64 s_time[LOG_MAX_LINES];
static int s_head;   /* index of the newest line */
static int s_count;

void log_init(void)
{
	memset(s_lines, 0, sizeof(s_lines));
	memset(s_time, 0, sizeof(s_time));
	s_head = 0;
	s_count = 0;
}

/* Keep the tail inside UTF-8 codepoint boundaries when truncating. */
static void trim_utf8(char *s, bool truncated)
{
	if (!truncated)
		return;
	size_t n = LOG_LINE_MAX - 4;   /* leave room for "..." */
	while (n > 0 && ((u8)s[n] & 0xC0) == 0x80)
		n--;
	s[n] = '\0';
	strcat(s, "...");
}

void log_add(const char *fmt, ...)
{
	char buf[LOG_LINE_MAX];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	/* vsnprintf returns the unclipped length: only trim when it actually
	 * had to cut the text, so a message exactly one line long survives. */
	trim_utf8(buf, n < 0 || n >= (int)sizeof(buf));

	s_head = (s_head + 1) % LOG_MAX_LINES;
	memcpy(s_lines[s_head], buf, LOG_LINE_MAX);
	s_time[s_head] = osGetTime();
	if (s_count < LOG_MAX_LINES)
		s_count++;
}

int log_count(void)
{
	return s_count;
}

const char *log_at(int index)
{
	if (index < 0 || index >= s_count)
		return "";
	int i = (s_head - index + LOG_MAX_LINES * 2) % LOG_MAX_LINES;
	return s_lines[i];
}

u64 log_time(int index)
{
	if (index < 0 || index >= s_count)
		return 0;
	int i = (s_head - index + LOG_MAX_LINES * 2) % LOG_MAX_LINES;
	return s_time[i];
}

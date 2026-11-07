/* Shared bottom-screen widgets: panels, buttons, bars, badges.
 * All colours come from the active theme (gfx_ui_color) so the whole UI
 * recolours together with the map atlas on Select. */
#include "ui.h"
#include "gfx.h"
#include "log.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

/* How long a world notice stays on screen.  A one-line reply such as
 * 「面前没有可以互动的东西。」 should not linger for four seconds, while a
 * three-line story message still needs time to be read: the lifetime grows
 * with the codepoint count and is capped at the old fixed value. */
#define NOTICE_MS_BASE     1500
#define NOTICE_MS_PER_CHAR 80
#define NOTICE_MS_MAX      4000
/* The notice wraps over several lines; the box grows upward from its
 * bottom edge, so a long message is never cut off. */
#define NOTICE_MAX_LINES 4
#define NOTICE_LINE_H    16
#define NOTICE_PAD       3
#define NOTICE_TEXT_X    12
#define NOTICE_BUF       512

u32 ui_col(int slot)
{
	return gfx_ui_color(slot);
}

/* Draws whole glyphs only, appending an ellipsis when the line overflows. */
void ui_text_fitted(const char *s, int x, int y, int w, u32 color)
{
	const char *q = s;
	int used = 0, cps = 0;

	while (*q)
	{
		const char *start = q;
		u32 cp = text_utf8_next(&q);
		int adv = text_advance(cp, 1);
		if (used + adv > w - 16)
		{
			q = start;
			break;
		}
		used += adv;
		cps++;
	}
	text_draw_n(s, cps, x, y, 1, color);
	if (*q)
		text_draw("…", x + used + 2, y, 1, ui_col(UI_COL_DIM));
}

bool ui_notice_fresh(void)
{
	u64 now, stamp, ms;
	const char *text = log_at(0);
	if (log_count() <= 0 || !text[0])
		return false;
	now = osGetTime();
	stamp = log_time(0);
	ms = (u64)NOTICE_MS_BASE + (u64)text_count(text) * NOTICE_MS_PER_CHAR;
	if (ms > NOTICE_MS_MAX)
		ms = NOTICE_MS_MAX;
	return stamp && now >= stamp && now - stamp <= ms;
}

/* World notice box: the message is wrapped over as many lines as it needs
 * and the panel grows upward from its bottom edge, so a long message is
 * shown in full instead of being cut off with an ellipsis. */
void ui_notice(int x, int bottom, int w)
{
	if (!ui_notice_fresh())
		return;

	char buf[NOTICE_BUF];
	int lines = text_wrap_fitted(log_at(0), w - 22, 1, buf, sizeof(buf),
	                             NOTICE_MAX_LINES);
	if (lines < 1)
		lines = 1;

	int h = lines * NOTICE_LINE_H + NOTICE_PAD * 2;
	int top = bottom - h;
	if (top < 0)
	{
		top = 0;
		h = bottom;
	}

	C2D_DrawRectSolid((float)x, (float)top, 0.0f, (float)w, (float)h,
	                  ui_col(UI_COL_PANEL));
	ui_frame(x, top, w, h, ui_col(UI_COL_BORDER));
	C2D_DrawRectSolid((float)x, (float)top, 0.0f, 3, (float)h,
	                  ui_col(UI_COL_ACCENT));

	const char *p = buf;
	for (int i = 0; i < lines && *p; i++)
	{
		const char *start = p;
		int cps = 0;
		while (*p && *p != '\n')
		{
			text_utf8_next(&p);
			cps++;
		}
		text_draw_n(start, cps, x + NOTICE_TEXT_X, top + NOTICE_PAD + i * NOTICE_LINE_H,
		            1, ui_col(UI_COL_TEXT));
		if (*p == '\n')
			p++;
	}
}

/* Every page shares the same title rail: an accent label with a thin rule
 * and a small accent tab underneath. Keeps the screens feeling like one
 * interface instead of a set of hand-laid-out debug panels. */
void ui_header(const char *title)
{
	text_draw(title, 10, 3, 1, ui_col(UI_COL_ACCENT));
	C2D_DrawRectSolid(10.0f, 21.0f, 0.0f, 300.0f, 1.0f, ui_col(UI_COL_BORDER));
	C2D_DrawRectSolid(10.0f, 21.0f, 0.0f, 28.0f, 2.0f, ui_col(UI_COL_ACCENT));
}

/* Small down caret that invites the next page/line without telling the
 * player which button to press. */
void ui_more(int cx, int y, u32 color)
{
	C2D_DrawRectSolid((float)(cx - 4), (float)y, 0.0f, 9.0f, 1.0f, color);
	C2D_DrawRectSolid((float)(cx - 3), (float)(y + 1), 0.0f, 7.0f, 1.0f, color);
	C2D_DrawRectSolid((float)(cx - 2), (float)(y + 2), 0.0f, 5.0f, 1.0f, color);
	C2D_DrawRectSolid((float)(cx - 1), (float)(y + 3), 0.0f, 3.0f, 1.0f, color);
	C2D_DrawRectSolid((float)cx, (float)(y + 4), 0.0f, 1.0f, 1.0f, color);
}

void ui_text_right(const char *s, int right, int y, u32 color)
{
	text_draw(s, right - text_width(s, 1), y, 1, color);
}

void ui_frame(int x, int y, int w, int h, u32 color)
{
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, 1.0f, color);
	C2D_DrawRectSolid((float)x, (float)(y + h - 1), 0.0f, (float)w, 1.0f, color);
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, 1.0f, (float)h, color);
	C2D_DrawRectSolid((float)(x + w - 1), (float)y, 0.0f, 1.0f, (float)h, color);
}

void ui_panel(int x, int y, int w, int h)
{
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
	                  ui_col(UI_COL_PANEL));
	ui_frame(x, y, w, h, ui_col(UI_COL_BORDER));
}

void ui_button(int x, int y, int w, int h, const char *label, int state)
{
	u32 body = state == UI_BTN_SELECTED ? ui_col(UI_COL_SEL) : ui_col(UI_COL_PANEL);
	u32 edge = state == UI_BTN_SELECTED ? ui_col(UI_COL_ACCENT)
	         : state == UI_BTN_DISABLED ? ui_col(UI_COL_OFF)
	                                    : ui_col(UI_COL_BORDER);
	u32 fg   = state == UI_BTN_SELECTED ? ui_col(UI_COL_ACCENT)
	         : state == UI_BTN_DISABLED ? ui_col(UI_COL_OFF)
	                                    : ui_col(UI_COL_TEXT);
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h, body);
	ui_frame(x, y, w, h, edge);
	if (label && *label)
		text_draw(label, x + (w - text_width(label, 1)) / 2, y + (h - 16) / 2, 1, fg);
}

void ui_bar(int x, int y, int w, int h, int value, int max, u32 fill)
{
	if (max <= 0)
		max = 1;
	if (value < 0)
		value = 0;
	if (value > max)
		value = max;

	/* 1px frame + 1px inset track so the fill never touches the border. */
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
	                  ui_col(UI_COL_BORDER));
	int iw = w - 2, ih = h - 2;
	if (iw <= 0 || ih <= 0)
		return;
	C2D_DrawRectSolid((float)(x + 1), (float)(y + 1), 0.0f, (float)iw, (float)ih,
	                  ui_col(UI_COL_BARBG));
	int fw = iw * value / max;
	if (fw > 0)
		C2D_DrawRectSolid((float)(x + 1), (float)(y + 1), 0.0f, (float)fw, (float)ih,
		                  fill);

	/* Quarter ticks make small changes readable. */
	for (int q = 1; q <= 3; q++)
	{
		int tx = x + 1 + iw * q / 4;
		C2D_DrawRectSolid((float)tx, (float)(y + 1), 0.0f, 1.0f, (float)ih,
		                  ui_col(UI_COL_BG));
	}
}

void ui_badge(int x, int y, int w, int h, const char *label, u32 bg, u32 fg)
{
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h, bg);
	if (label && *label)
		text_draw(label, x + (w - text_width(label, 1)) / 2, y + (h - 16) / 2, 1, fg);
}

/* ------------------------------------------------------------------ toasts */

/* Short, self-cancelling messages.  The original has no such thing - every
 * line went into the scroll - but a modern player expects to be told about a
 * reward without hunting for it.  Toasts are queued and shown one at a time
 * over the top screen. */

#define TOAST_MAX      4
#define TOAST_TEXT_MAX 72
#define TOAST_LIFE     2.6f
#define TOAST_SLIDE    0.20f

static char s_toastText[TOAST_MAX][TOAST_TEXT_MAX];
static float s_toastAge[TOAST_MAX];
static int s_toastCount;

void ui_toast(const char *text)
{
	if (!text || !*text || s_toastCount >= TOAST_MAX)
		return;
	snprintf(s_toastText[s_toastCount], TOAST_TEXT_MAX, "%s", text);
	s_toastAge[s_toastCount] = 0.0f;
	s_toastCount++;
}

void ui_toast_tick(float dt)
{
	if (s_toastCount <= 0)
		return;
	s_toastAge[0] += dt;
	if (s_toastAge[0] < TOAST_LIFE)
		return;

	for (int i = 1; i < s_toastCount; i++)
	{
		memcpy(s_toastText[i - 1], s_toastText[i], TOAST_TEXT_MAX);
		s_toastAge[i - 1] = s_toastAge[i];
	}
	s_toastCount--;
}

void ui_toast_draw(void)
{
	if (s_toastCount <= 0)
		return;

	const char *text = s_toastText[0];
	float age = s_toastAge[0];
	float t;
	if (age < TOAST_SLIDE)
		t = age / TOAST_SLIDE;
	else if (age > TOAST_LIFE - TOAST_SLIDE)
		t = (TOAST_LIFE - age) / TOAST_SLIDE;
	else
		t = 1.0f;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	int w = text_width(text, 1) + 26;
	int h = 26;
	/* Slides down from above the viewport on entry and back up on exit. */
	int x = (SCR_W - w) / 2;
	int y = 10 - (int)((1.0f - t) * (float)(h + 10));

	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
	                  ui_col(UI_COL_PANEL));
	ui_frame(x, y, w, h, ui_col(UI_COL_BORDER));
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, 3, (float)h,
	                  ui_col(UI_COL_ACCENT));
	text_draw(text, x + 13, y + (h - 16) / 2, 1, ui_col(UI_COL_TEXT));
}

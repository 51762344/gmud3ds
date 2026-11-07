#ifndef TEXT_H
#define TEXT_H

#include "gmud.h"

/* Loads data/font.bin ("romfs:/font.bin"), builds A8 atlas page textures. */
bool text_init(const char *romfs_path);
void text_fini(void);
bool text_ready(void);

int text_line_height(int scale);
int text_advance(u32 cp, int scale);
int text_width(const char *s, int scale);
int text_count(const char *s);

/* Draws the first n codepoints (n < 0 means all) and returns the pixel width
 * consumed on the current line. '\n' starts a new line. */
int text_draw_n(const char *s, int n, int x, int y, int scale, u32 color);
int text_draw(const char *s, int x, int y, int scale, u32 color);

/* Same as text_draw, but the whole string shifts with the current eye at
 * `depth` (see gfx_3d_offset): 0 = glass plane, negative pops toward the
 * viewer, positive recedes into the screen. */
int text_draw3(const char *s, int x, int y, int scale, u32 color, float depth);

/* Greedy wrap into out as UTF-8 with '\n' separators.
 * Returns the line count, or -1 when out_size / max_lines is exceeded. */
int text_wrap(const char *s, int max_width, int scale, char *out, int out_size, int max_lines);

/* Same as text_wrap, but a text that does not fit keeps its head and is
 * marked with "……" (never dropped, never drawn as one long line). */
int text_wrap_fitted(const char *s, int max_width, int scale, char *out,
                     int out_size, int max_lines);

/* Decodes one codepoint and advances *cursor; returns 0xFFFD on bad input. */
u32 text_utf8_next(const char **cursor);

#endif

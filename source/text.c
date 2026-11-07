#include "text.h"
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_MAGIC 0x544E4647u   /* "GFNT" little endian */
#define FONT_VERSION 1u
#define HEADER_SIZE 28
#define INDEX_SIZE 14
#define MAX_PAGES 4

typedef struct
{
	u32 cp;
	u16 x, y;
	u16 page;
	u8 w, h, adv;
} Glyph;

static Glyph *s_glyphs;
static int s_glyphCount;
static int s_cellW = 16, s_cellH = 16;
static int s_atlasDim = 1024;
static int s_pageCount;
static C3D_Tex s_tex[MAX_PAGES];
static bool s_texReady[MAX_PAGES];
static Tex3DS_SubTexture *s_subs;
static bool s_ready;

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

/* PICA200 8x8 Morton order, same scheme as gfx.c but 1 byte per texel. */
static unsigned morton8(unsigned x, unsigned y)
{
	unsigned m = 0;
	m |= (x & 1u);
	m |= (y & 1u) << 1;
	m |= ((x >> 1) & 1u) << 2;
	m |= ((y >> 1) & 1u) << 3;
	m |= ((x >> 2) & 1u) << 4;
	m |= ((y >> 2) & 1u) << 5;
	return m;
}

static void upload_page(int page, const u8 *bits)
{
	unsigned dim = (unsigned)s_atlasDim;
	u8 *dst = (u8 *)s_tex[page].data;
	memset(dst, 0, dim * dim);

	for (unsigned y = 0; y < dim; y++)
	{
		unsigned rowOff = y * dim / 8u;
		for (unsigned x = 0; x < dim; x++)
		{
			if (!(bits[rowOff + x / 8u] & (0x80u >> (x & 7u))))
				continue;
			unsigned block = (y >> 3) * (dim / 8u) + (x >> 3);
			unsigned m = morton8(x & 7u, y & 7u);
			dst[block * 64u + m] = 0xFF;
		}
	}
	C3D_TexFlush(&s_tex[page]);
}

static void free_all(void)
{
	for (int i = 0; i < MAX_PAGES; i++)
	{
		if (s_texReady[i])
		{
			C3D_TexDelete(&s_tex[i]);
			s_texReady[i] = false;
		}
	}
	free(s_glyphs);
	s_glyphs = NULL;
	free(s_subs);
	s_subs = NULL;
	s_glyphCount = 0;
	s_ready = false;
}

bool text_init(const char *romfs_path)
{
	u8 header[HEADER_SIZE];
	u8 *index = NULL;
	u8 *bits = NULL;

	free_all();

	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;
	if (fread(header, 1, sizeof(header), f) != sizeof(header))
	{
		fclose(f);
		return false;
	}

	if (rd32(header) != FONT_MAGIC || rd16(header + 4) != FONT_VERSION)
	{
		fclose(f);
		return false;
	}

	s_cellW = rd16(header + 6);
	s_cellH = rd16(header + 8);
	s_pageCount = rd16(header + 10);
	u32 count = rd32(header + 12);
	int atlasW = (int)rd32(header + 16);
	int atlasH = (int)rd32(header + 20);
	u32 bitmapBytes = rd32(header + 24);

	if (s_cellW != 16 || s_cellH != 16 || count == 0 || count > 65535 ||
	    s_pageCount < 1 || s_pageCount > MAX_PAGES ||
	    atlasW != atlasH || atlasW < 256 || atlasW > 1024 ||
	    (atlasW & (atlasW - 1)) || bitmapBytes != (u32)(atlasW * atlasH / 8))
	{
		fclose(f);
		return false;
	}
	s_atlasDim = atlasW;

	s_glyphs = (Glyph *)calloc(count, sizeof(Glyph));
	s_subs = (Tex3DS_SubTexture *)calloc(count, sizeof(Tex3DS_SubTexture));
	index = (u8 *)malloc(count * INDEX_SIZE);
	bits = (u8 *)malloc(bitmapBytes);
	if (!s_glyphs || !s_subs || !index || !bits)
	{
		free(index);
		free(bits);
		free_all();
		fclose(f);
		return false;
	}

	if (fread(index, 1, count * INDEX_SIZE, f) != count * INDEX_SIZE)
	{
		free(index);
		free(bits);
		free_all();
		fclose(f);
		return false;
	}

	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = index + i * INDEX_SIZE;
		s_glyphs[i].cp = rd32(e);
		s_glyphs[i].x = rd16(e + 4);
		s_glyphs[i].y = rd16(e + 6);
		s_glyphs[i].page = rd16(e + 8);
		s_glyphs[i].w = e[10];
		s_glyphs[i].h = e[11];
		s_glyphs[i].adv = e[12];
		if (s_glyphs[i].page >= s_pageCount ||
		    s_glyphs[i].x + s_cellW > atlasW || s_glyphs[i].y + s_cellH > atlasH)
		{
			free(index);
			free(bits);
			free_all();
			fclose(f);
			return false;
		}
	}
	free(index);
	s_glyphCount = (int)count;

	for (int p = 0; p < s_pageCount; p++)
	{
		if (!C3D_TexInit(&s_tex[p], (u16)atlasW, (u16)atlasH, GPU_A8))
		{
			free(bits);
			free_all();
			fclose(f);
			return false;
		}
		C3D_TexSetFilter(&s_tex[p], GPU_NEAREST, GPU_NEAREST);
		C3D_TexSetWrap(&s_tex[p], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		s_texReady[p] = true;

		if (fread(bits, 1, bitmapBytes, f) != bitmapBytes)
		{
			free(bits);
			free_all();
			fclose(f);
			return false;
		}
		upload_page(p, bits);
	}
	free(bits);
	fclose(f);

	float inv = 1.0f / (float)atlasW;
	for (int i = 0; i < s_glyphCount; i++)
	{
		Tex3DS_SubTexture *st = &s_subs[i];
		st->width = (u16)s_cellW;
		st->height = (u16)s_cellH;
		st->left = (float)s_glyphs[i].x * inv;
		st->right = (float)(s_glyphs[i].x + s_cellW) * inv;
		st->top = 1.0f - (float)s_glyphs[i].y * inv;
		st->bottom = 1.0f - (float)(s_glyphs[i].y + s_cellH) * inv;
	}

	s_ready = true;
	return true;
}

void text_fini(void)
{
	free_all();
}

bool text_ready(void)
{
	return s_ready;
}

int text_line_height(int scale)
{
	return (scale < 1 ? 1 : scale) * 16;
}

int text_advance(u32 cp, int scale)
{
	if (scale < 1)
		scale = 1;
	return (cp < 0x80 ? 8 : 16) * scale;
}

/* Binary search over the codepoint-sorted index. */
static int glyph_index(u32 cp)
{
	int lo = 0, hi = s_glyphCount - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (s_glyphs[mid].cp == cp)
			return mid;
		if (s_glyphs[mid].cp < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

u32 text_utf8_next(const char **cursor)
{
	const u8 *s = (const u8 *)*cursor;
	if (!s || !*s)
		return 0;

	u32 cp;
	int extra;
	if (s[0] < 0x80)
	{
		*cursor = (const char *)(s + 1);
		return s[0];
	}
	else if ((s[0] & 0xE0) == 0xC0)
	{
		cp = s[0] & 0x1F;
		extra = 1;
	}
	else if ((s[0] & 0xF0) == 0xE0)
	{
		cp = s[0] & 0x0F;
		extra = 2;
	}
	else if ((s[0] & 0xF8) == 0xF0)
	{
		cp = s[0] & 0x07;
		extra = 3;
	}
	else
	{
		*cursor = (const char *)(s + 1);
		return 0xFFFD;
	}

	for (int i = 1; i <= extra; i++)
	{
		if ((s[i] & 0xC0) != 0x80)
		{
			*cursor = (const char *)(s + 1);
			return 0xFFFD;
		}
		cp = (cp << 6) | (s[i] & 0x3F);
	}
	/* Reject overlong encodings (C0 80 -> U+0000 etc.) and anything past
	 * Unicode; both would otherwise resolve to wrong glyphs. */
	if (cp > 0x10FFFFu ||
	    (extra == 1 && cp < 0x80u) ||
	    (extra == 2 && cp < 0x800u) ||
	    (extra == 3 && cp < 0x10000u))
	{
		*cursor = (const char *)(s + 1);
		return 0xFFFD;
	}
	*cursor = (const char *)(s + extra + 1);
	return cp;
}

int text_width(const char *s, int scale)
{
	int w = 0;
	const char *p = s;
	while (*p)
	{
		if (*p == '\n')
			break;
		u32 cp = text_utf8_next(&p);
		w += text_advance(cp, scale);
	}
	return w;
}

int text_count(const char *s)
{
	int n = 0;
	const char *p = s;
	while (*p)
	{
		text_utf8_next(&p);
		n++;
	}
	return n;
}

static void draw_missing(int x, int y, int scale)
{
	int s = scale < 1 ? 1 : scale;
	int w = 14 * s, h = 14 * s;
	u32 clr = C2D_RGBA(0xC0, 0x40, 0x40);
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)s, clr);
	C2D_DrawRectSolid((float)x, (float)(y + h - s), 0.0f, (float)w, (float)s, clr);
	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)s, (float)h, clr);
	C2D_DrawRectSolid((float)(x + w - s), (float)y, 0.0f, (float)s, (float)h, clr);
}

/* Depth-aware core: `depth` shifts the string horizontally with the current
 * stereo eye (0 on the bottom screen / 2D mode). */
static int text_draw_core(const char *s, int n, int x, int y, int scale,
                          u32 color, float depth)
{
	if (!s_ready || !s)
		return 0;
	if (scale < 1)
		scale = 1;

	int startX = x + (int)gfx_3d_offset(depth);
	int drawn = 0;
	const char *p = s;
	while (*p)
	{
		if (n >= 0 && drawn >= n)
			break;
		if (*p == '\n')
		{
			p++;
			drawn++;
			x = startX;
			y += text_line_height(scale);
			continue;
		}

		u32 cp = text_utf8_next(&p);
		int adv = text_advance(cp, scale);
		int gi = glyph_index(cp);
		if (gi < 0)
		{
			draw_missing(x, y, scale);
			x += adv;
			drawn++;
			continue;
		}

		C2D_Image img = { &s_tex[s_glyphs[gi].page], &s_subs[gi] };
		C2D_ImageTint tint;
		C2D_PlainImageTint(&tint, color, 1.0f);
		C2D_DrawImageAt(img, (float)x, (float)y, 0.0f, &tint, (float)scale, (float)scale);
		x += adv;
		drawn++;
	}
	return x - startX;
}

int text_draw_n(const char *s, int n, int x, int y, int scale, u32 color)
{
	return text_draw_core(s, n, x, y, scale, color, 0.0f);
}

int text_draw(const char *s, int x, int y, int scale, u32 color)
{
	return text_draw_core(s, -1, x, y, scale, color, 0.0f);
}

int text_draw3(const char *s, int x, int y, int scale, u32 color, float depth)
{
	return text_draw_core(s, -1, x, y, scale, color, depth);
}

static int emit(char *out, int cap, int pos, const char *src, int len)
{
	if (pos + len + 1 > cap)
		return -1;
	memcpy(out + pos, src, (size_t)len);
	return pos + len;
}

static int text_wrap_n(const char *s, int len, int max_width, int scale,
                        char *out, int out_size, int max_lines)
{
	if (!out || out_size < 2)
		return -1;
	if (scale < 1)
		scale = 1;

	int pos = 0;
	int lines = 1;
	int x = 0;
	const char *p = s;
	const char *end = s + len;

	while (p < end && *p)
	{
		if (*p == '\n')
		{
			p++;
			if (lines >= max_lines)
				return -1;
			if (pos + 1 >= out_size)
				return -1;
			out[pos++] = '\n';
			lines++;
			x = 0;
			continue;
		}

		const char *start = p;
		u32 cp = text_utf8_next(&p);
		int adv = text_advance(cp, scale);

		if (x + adv > max_width && x > 0)
		{
			if (lines >= max_lines)
				return -1;
			pos = emit(out, out_size, pos, "\n", 1);
			if (pos < 0)
				return -1;
			lines++;
			x = 0;
		}
		pos = emit(out, out_size, pos, start, (int)(p - start));
		if (pos < 0)
			return -1;
		x += adv;
	}

	out[pos] = '\0';
	return lines;
}

int text_wrap(const char *s, int max_width, int scale, char *out, int out_size, int max_lines)
{
	if (!s)
		return -1;
	return text_wrap_n(s, (int)strlen(s), max_width, scale, out, out_size, max_lines);
}

/* Byte length of the first `count` codepoints of a UTF-8 string. */
static int utf8_prefix_len(const char *s, int count)
{
	const char *p = s;
	for (int i = 0; i < count && *p; i++)
		text_utf8_next(&p);
	return (int)(p - s);
}

/* Same as text_wrap, but a text that does not fit keeps its head and is
 * marked with "……" instead of being dropped or drawn as one long line. */
int text_wrap_fitted(const char *s, int max_width, int scale, char *out,
                     int out_size, int max_lines)
{
	if (!s || !out || out_size < 2)
		return -1;

	int lines = text_wrap(s, max_width, scale, out, out_size, max_lines);
	if (lines >= 0)
		return lines;

	/* Cut the tail at a codepoint boundary and mark the cut. */
	int cps = text_count(s);
	int lo = 0, hi = cps;
	while (lo < hi)
	{
		int mid = (lo + hi + 1) / 2;
		int len = utf8_prefix_len(s, mid);
		if (text_wrap_n(s, len, max_width, scale, out, out_size - 8,
		                max_lines) >= 0)
			lo = mid;
		else
			hi = mid - 1;
	}

	int len = utf8_prefix_len(s, lo);
	lines = text_wrap_n(s, len, max_width, scale, out, out_size - 8, max_lines);
	if (lines < 0)
	{
		out[0] = '\0';
		return 1;
	}
	strcat(out, "……");
	return lines;
}

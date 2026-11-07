#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_DIM 1024
#define ATLAS_COLS 32
#define ATLAS_ROWS 32
#define SLOTS (ATLAS_COLS * ATLAS_ROWS)
#define SLOT_TILE 32
#define IMAGE_HALF 65536

typedef struct
{
	u8 r, g, b;
} Rgb;

/* UI palettes. shade0 paper .. shade3 ink. */
static const Rgb s_themes[][4] =
{
	{ { 0xAE, 0xC2, 0x7E }, { 0x8F, 0xA9, 0x68 }, { 0x5C, 0x7A, 0x3E }, { 0x22, 0x30, 0x0F } },
	{ { 0xEF, 0xE7, 0xD6 }, { 0xD6, 0xCB, 0xBA }, { 0x8C, 0x7F, 0xA8 }, { 0x3A, 0x2A, 0x5E } },
	{ { 0xFF, 0xFF, 0xFF }, { 0xAA, 0xAA, 0xAA }, { 0x66, 0x66, 0x66 }, { 0x00, 0x00, 0x00 } },
};
#define THEME_COUNT ((int)(sizeof(s_themes) / sizeof(s_themes[0])))

/* 功能菜单「主题」行显示用，顺序与 s_themes 一致。 */
static const char *const s_themeNames[THEME_COUNT] =
{
	"原味绿", "液晶蓝", "灰度"
};

/* Bottom-screen UI palettes, one row per tile theme. The green theme keeps
 * BG = #1A2412 because tools/azahar_touch.ps1 still uses it as a fallback
 * when locating the bottom screen (primary location uses the purple
 * corner anchors). */
static const u32 s_ui_pal[][UI_COL_COUNT] =
{
	/* 原味绿 */
	{
		C2D_RGBA(0x1A, 0x24, 0x12), C2D_RGBA(0x22, 0x30, 0x0F), C2D_RGBA(0x5F, 0x77, 0x44),
		C2D_RGBA(0xCF, 0xE0, 0xA8), C2D_RGBA(0x8F, 0xA9, 0x68), C2D_RGBA(0xF0, 0xE6, 0xC8),
		C2D_RGBA(0x44, 0x5A, 0x2C), C2D_RGBA(0xAE, 0xC2, 0x7E), C2D_RGBA(0x2E, 0x3E, 0x20),
		C2D_RGBA(0x55, 0x66, 0x44), C2D_RGBA(0xC8, 0x68, 0x58), C2D_RGBA(0x6A, 0x94, 0xC0),
		C2D_RGBA(0xF0, 0x70, 0x60),
		C2D_RGBA(0xAE, 0xC2, 0x7E), C2D_RGBA(0x8F, 0xA9, 0x68),
		C2D_RGBA(0x5C, 0x7A, 0x3E), C2D_RGBA(0x22, 0x30, 0x0F),
	},
	/* 液晶蓝 */
	{
		C2D_RGBA(0x14, 0x10, 0x1F), C2D_RGBA(0x1E, 0x18, 0x30), C2D_RGBA(0x6E, 0x62, 0x90),
		C2D_RGBA(0xD6, 0xCB, 0xBA), C2D_RGBA(0x9A, 0x8C, 0xB4), C2D_RGBA(0xEF, 0xE7, 0xD6),
		C2D_RGBA(0x3A, 0x2E, 0x56), C2D_RGBA(0xD6, 0xCB, 0xBA), C2D_RGBA(0x25, 0x1D, 0x3A),
		C2D_RGBA(0x58, 0x50, 0x73), C2D_RGBA(0xB4, 0x69, 0x6F), C2D_RGBA(0x7E, 0x86, 0xCC),
		C2D_RGBA(0xE0, 0x84, 0x78),
		C2D_RGBA(0xEF, 0xE7, 0xD6), C2D_RGBA(0xD6, 0xCB, 0xBA),
		C2D_RGBA(0x8C, 0x7F, 0xA8), C2D_RGBA(0x3A, 0x2A, 0x5E),
	},
	/* 灰度 */
	{
		C2D_RGBA(0x12, 0x12, 0x12), C2D_RGBA(0x1C, 0x1C, 0x1C), C2D_RGBA(0x5A, 0x5A, 0x5A),
		C2D_RGBA(0xDA, 0xDA, 0xDA), C2D_RGBA(0x9A, 0x9A, 0x9A), C2D_RGBA(0xFF, 0xFF, 0xFF),
		C2D_RGBA(0x3A, 0x3A, 0x3A), C2D_RGBA(0xC8, 0xC8, 0xC8), C2D_RGBA(0x26, 0x26, 0x26),
		C2D_RGBA(0x56, 0x56, 0x56), C2D_RGBA(0xB0, 0xB0, 0xB0), C2D_RGBA(0x8A, 0x8A, 0x8A),
		C2D_RGBA(0xFF, 0xFF, 0xFF),
		C2D_RGBA(0xFF, 0xFF, 0xFF), C2D_RGBA(0xAA, 0xAA, 0xAA),
		C2D_RGBA(0x66, 0x66, 0x66), C2D_RGBA(0x00, 0x00, 0x00),
	},
};

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static C3D_RenderTarget *s_topRight;

/* Stereoscopic 3D state: slider position (0 = 2D), the gfxSet3D flag and
 * the horizontal offset of the eye currently being rendered. */
static float s_3dSlider;
static bool s_3dOn;
static bool s_3dCandidate;
static float s_3dCandidateTime;
static float s_eyeShift;

static C2D_TextBuf s_textBuf;
static C2D_Font s_font;

static C3D_Tex s_atlas;
static bool s_atlasReady;
static Tex3DS_SubTexture s_sub[SLOTS];
static C2D_Image s_img[SLOTS];

static u8 s_tiles[IMAGE_HALF * 2];
static bool s_tilesReady;
static int s_theme;

/* Player walk sprites (data/player.bin, PSPR v1). 12 image frames + 12 AND
 * masks, 32x48 1bpp each; masks use the same semantics as tile masks
 * (mask bit 1 = keep background / transparent). */
#define PLAYER_FRAMES 12
#define PLAYER_W 32
#define PLAYER_H 48
#define PLAYER_FRAME_BYTES (PLAYER_W * PLAYER_H / 8)   /* 192 B, 1bpp */
#define PLAYER_ATLAS_W 1024
#define PLAYER_ATLAS_H 128

static u8 s_playerFrames[PLAYER_FRAMES][PLAYER_FRAME_BYTES];
static u8 s_playerMasks[PLAYER_FRAMES][PLAYER_FRAME_BYTES];
static bool s_playerReady;
static C3D_Tex s_playerAtlas;
static bool s_playerAtlasReady;
static Tex3DS_SubTexture s_playerSub[PLAYER_FRAMES];
static C2D_Image s_playerImg[PLAYER_FRAMES];

bool gfx_init(void)
{
	gfxInitDefault();
	gfxSetWide(TOP_WIDE ? true : false);

	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
	{
		gfxExit();
		return false;
	}
	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
	{
		C3D_Fini();
		gfxExit();
		return false;
	}
	C2D_Prepare();

	s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	s_topRight = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	if (!s_top || !s_bottom || !s_topRight)
		return false;

	s_textBuf = C2D_TextBufNew(8192);
	s_font = C2D_FontLoadSystem(CFG_REGION_JPN);   /* NULL is allowed: system font */
	return true;
}

void gfx_fini(void)
{
	if (s_textBuf)
		C2D_TextBufDelete(s_textBuf);
	if (s_font)
		C2D_FontFree(s_font);
	if (s_atlasReady)
	{
		C3D_TexDelete(&s_atlas);
		s_atlasReady = false;
	}
	if (s_playerAtlasReady)
	{
		C3D_TexDelete(&s_playerAtlas);
		s_playerAtlasReady = false;
	}
	C2D_Fini();
	C3D_Fini();
	gfxExit();
}

C3D_RenderTarget *gfx_top_target(void) { return s_top; }
C3D_RenderTarget *gfx_bottom_target(void) { return s_bottom; }
C3D_RenderTarget *gfx_top_right_target(void) { return s_topRight; }

void gfx_3d_update(float slider, float dt)
{
	if (slider < 0.0f) slider = 0.0f;
	if (slider > 1.0f) slider = 1.0f;
#if TOP_WIDE
	/* Wide mode drives the same parallax-barrier hardware as stereo 3D;
	 * the two cannot be active at the same time (gmud.h TOP_WIDE). */
	slider = 0.0f;
#endif
	/* Hysteresis + debounce.  The slider sits against a hard detent at 0 on
	 * real hardware, but its ADC still jitters by a few thousandths; with a
	 * single threshold (the old 0.02) that jitter called gfxSet3D() on and
	 * off every few frames, and engaging the parallax barrier visibly dims
	 * the top screen - "the screen keeps getting brighter and darker". */
	const float kOn = 0.10f;    /* must travel this far to engage */
	const float kOff = 0.04f;   /* ...and this far to release */
	const float kStable = 0.15f;/* ...and stay there this long (seconds) */

	bool raw = s_3dOn ? (slider > kOff) : (slider >= kOn);
	if (raw != s_3dCandidate)
	{
		s_3dCandidate = raw;
		s_3dCandidateTime = 0.0f;
	}
	else if (raw != s_3dOn)
	{
		s_3dCandidateTime += dt;
		if (s_3dCandidateTime >= kStable)
		{
			/* If depth ever feels inverted on hardware (GFX_LEFT mapped to
			 * the other eye), swap the GFX_EYE_* constants in gfx.h. */
			gfxSet3D(raw);
			s_3dOn = raw;
			s_3dCandidateTime = 0.0f;
		}
	}
	s_3dSlider = s_3dOn ? slider : 0.0f;
}

bool gfx_3d_active(void) { return s_3dOn; }

void gfx_3d_begin_eye(int eye)
{
	s_eyeShift = s_3dSlider * GFX_3D_BASE_SHIFT * (float)eye;
}

float gfx_3d_offset(float depth)
{
	/* Whole pixels only: the art is 1:1 pixel tiles, subpixel offsets would
	 * smear the NEAREST-sampled texels. */
	float off = s_eyeShift * depth;
	return (float)(int)(off + (off >= 0.0f ? 0.5f : -0.5f));
}

/* PICA200 8x8 Morton order: dst[morton(x,y)] = src[y*8+x]. */
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

static void put_texel_at(C3D_Tex *tex, unsigned tex_w, int x, int y, u32 px)
{
	/* tex_w is a multiple of 8. Block size for RGBA8 is 8*8*4 = 256. */
	unsigned block = (unsigned)(y >> 3) * (tex_w / 8) + (unsigned)(x >> 3);
	unsigned m = morton8((unsigned)x & 7u, (unsigned)y & 7u);
	memcpy((u8 *)tex->data + block * 256u + m * 4u, &px, 4);
}

static void put_texel(int x, int y, u32 px)
{
	put_texel_at(&s_atlas, ATLAS_DIM, x, y, px);
}

static void build_player_atlas(void);

static bool build_atlas(void)
{
	if (!s_tilesReady)
		return false;

	if (!s_atlasReady)
	{
		if (!C3D_TexInit(&s_atlas, ATLAS_DIM, ATLAS_DIM, GPU_RGBA8))
			return false;
		C3D_TexSetFilter(&s_atlas, GPU_NEAREST, GPU_NEAREST);
		C3D_TexSetWrap(&s_atlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		s_atlasReady = true;
	}

	const Rgb *pal = s_themes[s_theme];
	/* PICA200 RGBA8 texels are stored as A,B,G,R bytes (R in the high byte). */
	u32 paper = ((u32)pal[0].r << 24) | ((u32)pal[0].g << 16) | ((u32)pal[0].b << 8) | 255u;
	u32 ink = ((u32)pal[3].r << 24) | ((u32)pal[3].g << 16) | ((u32)pal[3].b << 8) | 255u;

	for (int t = 0; t < SLOTS / 2; t++)
	{
		int col = t % ATLAS_COLS;
		int row = t / ATLAS_COLS;
		for (int py = 0; py < SLOT_TILE; py++)
		{
			for (int px = 0; px < SLOT_TILE; px++)
			{
				unsigned off = (unsigned)t * 128u + (unsigned)py * 4u + (unsigned)px / 8u;
				unsigned shift = 7u - (unsigned)(px & 7);
				unsigned image = (s_tiles[off] >> shift) & 1u;
				unsigned mask = (s_tiles[IMAGE_HALF + off] >> shift) & 1u;

				int gx = col * SLOT_TILE + px;
				int gy = row * SLOT_TILE + py;
				put_texel(gx, gy, image ? ink : paper);

				int oy = gy + ATLAS_DIM / 2;
				put_texel(gx, oy, image ? ink : (mask ? 0u : paper));
			}
		}
	}

	C3D_TexFlush(&s_atlas);

	if (s_img[0].tex == NULL)
	{
		for (int slot = 0; slot < SLOTS; slot++)
		{
			int col = slot % ATLAS_COLS;
			int row = slot / ATLAS_COLS;
			Tex3DS_SubTexture *st = &s_sub[slot];
			st->width = SLOT_TILE;
			st->height = SLOT_TILE;
			st->left = (float)(col * SLOT_TILE) / ATLAS_DIM;
			st->right = (float)((col + 1) * SLOT_TILE) / ATLAS_DIM;
			st->top = 1.0f - (float)(row * SLOT_TILE) / ATLAS_DIM;
			st->bottom = 1.0f - (float)((row + 1) * SLOT_TILE) / ATLAS_DIM;
			s_img[slot].tex = &s_atlas;
			s_img[slot].subtex = st;
		}
	}

	build_player_atlas();
	return true;
}

/* Retints the 12 player frames into s_playerAtlas; called from build_atlas()
 * so Select-theming recolours the walk sprites with the map. */
static void build_player_atlas(void)
{
	if (!s_playerReady)
		return;

	if (!s_playerAtlasReady)
	{
		if (!C3D_TexInit(&s_playerAtlas, PLAYER_ATLAS_W, PLAYER_ATLAS_H, GPU_RGBA8))
			return;
		C3D_TexSetFilter(&s_playerAtlas, GPU_NEAREST, GPU_NEAREST);
		C3D_TexSetWrap(&s_playerAtlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		s_playerAtlasReady = true;
	}

	const Rgb *pal = s_themes[s_theme];
	u32 paper = ((u32)pal[0].r << 24) | ((u32)pal[0].g << 16) | ((u32)pal[0].b << 8) | 255u;
	u32 ink = ((u32)pal[3].r << 24) | ((u32)pal[3].g << 16) | ((u32)pal[3].b << 8) | 255u;

	for (int f = 0; f < PLAYER_FRAMES; f++)
	{
		for (int py = 0; py < PLAYER_H; py++)
		{
			for (int px = 0; px < PLAYER_W; px++)
			{
				unsigned off = (unsigned)py * 4u + (unsigned)px / 8u;
				unsigned shift = 7u - (unsigned)(px & 7);
				unsigned image = (s_playerFrames[f][off] >> shift) & 1u;
				unsigned mask = (s_playerMasks[f][off] >> shift) & 1u;
				u32 c = image ? ink : (mask ? 0u : paper);
				put_texel_at(&s_playerAtlas, PLAYER_ATLAS_W, f * PLAYER_W + px, py, c);
			}
		}
	}

	if (s_playerImg[0].tex == NULL)
	{
		for (int f = 0; f < PLAYER_FRAMES; f++)
		{
			Tex3DS_SubTexture *st = &s_playerSub[f];
			st->width = PLAYER_W;
			st->height = PLAYER_H;
			st->left = (float)(f * PLAYER_W) / PLAYER_ATLAS_W;
			st->right = (float)((f + 1) * PLAYER_W) / PLAYER_ATLAS_W;
			st->top = 1.0f - 0.0f;
			st->bottom = 1.0f - (float)PLAYER_H / PLAYER_ATLAS_H;
			s_playerImg[f].tex = &s_playerAtlas;
			s_playerImg[f].subtex = st;
		}
	}

	C3D_TexFlush(&s_playerAtlas);
}

bool gfx_load_player(const char *romfs_path)
{
	u8 buf[16 + 2 * PLAYER_FRAMES * PLAYER_FRAME_BYTES];

	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n != sizeof(buf) || memcmp(buf, "PSPR", 4) != 0)
		return false;

	u16 version, frames, w, h, fbytes, flags;
	memcpy(&version, buf + 4, 2);
	memcpy(&frames, buf + 6, 2);
	memcpy(&w, buf + 8, 2);
	memcpy(&h, buf + 10, 2);
	memcpy(&fbytes, buf + 12, 2);
	memcpy(&flags, buf + 14, 2);
	if (version != 1 || frames != PLAYER_FRAMES || w != PLAYER_W ||
	    h != PLAYER_H || fbytes != PLAYER_FRAME_BYTES || flags != 0)
		return false;

	const u8 *src = buf + 16;
	for (int i = 0; i < PLAYER_FRAMES; i++)
	{
		memcpy(s_playerFrames[i], src + i * PLAYER_FRAME_BYTES, PLAYER_FRAME_BYTES);
		memcpy(s_playerMasks[i], src + (PLAYER_FRAMES + i) * PLAYER_FRAME_BYTES,
		       PLAYER_FRAME_BYTES);
	}

	s_playerReady = true;
	build_player_atlas();
	return s_playerAtlasReady;
}

void gfx_player_top(int frame, float x, float y, int srcH)
{
	if (frame < 0 || frame >= PLAYER_FRAMES || !s_playerAtlasReady ||
	    srcH <= 0 || srcH > PLAYER_H)
		return;
	Tex3DS_SubTexture sub = *s_playerImg[frame].subtex;
	/* v runs downward as y grows: top (y=0) is 1.0, bottom decreases. */
	sub.bottom = sub.top - (float)srcH / PLAYER_ATLAS_H;
	sub.height = (u16)srcH;
	x += gfx_3d_offset(GFX_DEPTH_STAGE);
	C2D_Image img = { &s_playerAtlas, &sub };
	C2D_DrawImageAt(img, x, y, 0.0f, NULL, 1.0f, 1.0f);
}

bool gfx_load_tiles(const char *romfs_path)
{
	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;
	size_t n = fread(s_tiles, 1, sizeof(s_tiles), f);
	fclose(f);
	if (n != sizeof(s_tiles))
		return false;

	s_tilesReady = true;
	s_theme = 0;
	return build_atlas();
}

bool gfx_set_theme(int theme)
{
	if (!s_tilesReady)
		return false;
	if (theme < 0 || theme >= THEME_COUNT)
		return false;
	s_theme = theme;
	return build_atlas();
}

int gfx_theme(void) { return s_theme; }

const char *gfx_theme_name(void)
{
	if (s_theme < 0 || s_theme >= THEME_COUNT)
		return "?";
	return s_themeNames[s_theme];
}

u32 gfx_ui_color(int slot)
{
	if (slot < 0 || slot >= UI_COL_COUNT)
		return C2D_RGBA(0xFF, 0x00, 0xFF);   /* loud magenta: bad slot */
	return s_ui_pal[s_theme][slot];
}

void gfx_text_begin(void)
{
	if (s_textBuf)
		C2D_TextBufClear(s_textBuf);
}

void gfx_text(const char *utf8, float x, float y, float scale, u32 color)
{
	if (!s_textBuf || !utf8 || !*utf8)
		return;
	C2D_Text text;
	if (!C2D_TextFontParse(&text, s_font, s_textBuf, utf8))
		return;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, x, y, 0.0f, scale, scale, color);
}

float gfx_text_width(const char *utf8, float scale)
{
	if (!s_textBuf || !utf8 || !*utf8)
		return 0.0f;
	C2D_Text text;
	if (!C2D_TextFontParse(&text, s_font, s_textBuf, utf8))
		return 0.0f;
	return text.width * scale;
}

void gfx_tile(u16 tile, TileLayer layer, float x, float y)
{
	unsigned slot = (unsigned)tile + (layer == LAYER_OBJECT ? SLOTS / 2 : 0);
	if (tile >= TILE_COUNT || !s_atlasReady)
		return;
	x += gfx_3d_offset(layer == LAYER_OBJECT ? GFX_DEPTH_STAGE
	                                         : GFX_DEPTH_GROUND);
	C2D_DrawImageAt(s_img[slot], x, y, 0.0f, NULL, 1.0f, 1.0f);
}

void gfx_tile_scaled(u16 tile, TileLayer layer, float x, float y,
                     float sx, float sy, bool flipX)
{
	if (tile >= TILE_COUNT || !s_atlasReady || sx <= 0.0f || sy <= 0.0f)
		return;

	const Tex3DS_SubTexture *base =
		s_img[(unsigned)tile + (layer == LAYER_OBJECT ? SLOTS / 2 : 0)].subtex;
	Tex3DS_SubTexture sub = *base;
	if (flipX)
	{
		/* Swapping the U edges mirrors the quad without another texture. */
		float tmp = sub.left;
		sub.left = sub.right;
		sub.right = tmp;
	}

	x += gfx_3d_offset(layer == LAYER_OBJECT ? GFX_DEPTH_STAGE
	                                         : GFX_DEPTH_GROUND);
	C2D_Image img = { &s_atlas, &sub };
	C2D_DrawParams params =
	{
		{ x, y, SLOT_TILE * sx, SLOT_TILE * sy },
		{ 0.0f, 0.0f },
		0.0f, 0.0f
	};
	C2D_DrawImage(img, &params, NULL);
}

void gfx_tile_clipped(u16 tile, TileLayer layer, float x, float y,
                      float clip_x, float clip_y, float clip_w, float clip_h)
{
	if (tile >= TILE_COUNT || !s_atlasReady)
		return;

	/* Shift BEFORE clipping: the clip rect is screen space (the viewport
	 * frame), so each eye sees the floor windowed inside the same frame. */
	x += gfx_3d_offset(layer == LAYER_OBJECT ? GFX_DEPTH_STAGE
	                                         : GFX_DEPTH_GROUND);
	float x0 = x, y0 = y, x1 = x + SLOT_TILE, y1 = y + SLOT_TILE;
	float cx0 = x0 > clip_x ? x0 : clip_x;
	float cy0 = y0 > clip_y ? y0 : clip_y;
	float cx1 = x1 < clip_x + clip_w ? x1 : clip_x + clip_w;
	float cy1 = y1 < clip_y + clip_h ? y1 : clip_y + clip_h;
	if (cx1 <= cx0 || cy1 <= cy0)
		return;

	const Tex3DS_SubTexture *base = s_img[(unsigned)tile + (layer == LAYER_OBJECT ? SLOTS / 2 : 0)].subtex;
	Tex3DS_SubTexture sub = *base;
	float u0 = base->left + (cx0 - x0) / SLOT_TILE * (base->right - base->left);
	float u1 = base->left + (cx1 - x0) / SLOT_TILE * (base->right - base->left);
	float v0 = base->top + (cy0 - y0) / SLOT_TILE * (base->bottom - base->top);
	float v1 = base->top + (cy1 - y0) / SLOT_TILE * (base->bottom - base->top);
	sub.left = u0;
	sub.right = u1;
	sub.top = v0;
	sub.bottom = v1;
	sub.width = (u16)(cx1 - cx0);
	sub.height = (u16)(cy1 - cy0);

	C2D_Image img = { &s_atlas, &sub };
	C2D_DrawParams params =
	{
		{ cx0, cy0, cx1 - cx0, cy1 - cy0 },
		{ 0.0f, 0.0f },
		0.0f, 0.0f
	};
	C2D_DrawImage(img, &params, NULL);
}

void gfx_player(int frame, float x, float y, float scale)
{
	if (frame < 0 || frame >= PLAYER_FRAMES || !s_playerAtlasReady)
		return;
	x += gfx_3d_offset(GFX_DEPTH_STAGE);
	C2D_DrawParams params =
	{
		{ x, y, PLAYER_W * scale, PLAYER_H * scale },
		{ 0.0f, 0.0f },
		0.0f, 0.0f
	};
	C2D_DrawImage(s_playerImg[frame], &params, NULL);
}

void gfx_player_clipped(int frame, float x, float y, float scale,
                        float clip_x, float clip_y, float clip_w, float clip_h)
{
	if (frame < 0 || frame >= PLAYER_FRAMES || !s_playerAtlasReady)
		return;

	x += gfx_3d_offset(GFX_DEPTH_STAGE);
	float w = PLAYER_W * scale;
	float h = PLAYER_H * scale;
	float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
	float cx0 = x0 > clip_x ? x0 : clip_x;
	float cy0 = y0 > clip_y ? y0 : clip_y;
	float cx1 = x1 < clip_x + clip_w ? x1 : clip_x + clip_w;
	float cy1 = y1 < clip_y + clip_h ? y1 : clip_y + clip_h;
	if (cx1 <= cx0 || cy1 <= cy0)
		return;

	const Tex3DS_SubTexture *base = s_playerImg[frame].subtex;
	Tex3DS_SubTexture sub = *base;
	sub.left = base->left + (cx0 - x0) / w * (base->right - base->left);
	sub.right = base->left + (cx1 - x0) / w * (base->right - base->left);
	sub.top = base->top + (cy0 - y0) / h * (base->bottom - base->top);
	sub.bottom = base->top + (cy1 - y0) / h * (base->bottom - base->top);
	sub.width = (u16)(cx1 - cx0);
	sub.height = (u16)(cy1 - cy0);

	C2D_Image img = { &s_playerAtlas, &sub };
	C2D_DrawParams params =
	{
		{ cx0, cy0, cx1 - cx0, cy1 - cy0 },
		{ 0.0f, 0.0f },
		0.0f, 0.0f
	};
	C2D_DrawImage(img, &params, NULL);
}

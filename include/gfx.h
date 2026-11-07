#ifndef GFX_H
#define GFX_H

#include "gmud.h"

bool gfx_init(void);
void gfx_fini(void);

C3D_RenderTarget *gfx_top_target(void);
C3D_RenderTarget *gfx_bottom_target(void);
C3D_RenderTarget *gfx_top_right_target(void);

/* ---- stereoscopic 3D (top screen) --------------------------------------
 * The 3DS top screen interleaves two framebuffers through a parallax
 * barrier, one per eye. When the 3D slider is up every frame renders the
 * top scene twice (GFX_EYE_LEFT / GFX_EYE_RIGHT) with a horizontal offset
 * per depth factor; at slider 0 only the left eye is rendered and gfxSet3D
 * stays off. Depth follows the "window" convention: positive factors
 * recede INTO the screen, negative ones pop toward the viewer, and 0
 * stays glued to the glass (text, HUD, overlays). */
enum
{
	GFX_EYE_NONE = 0,
	GFX_EYE_LEFT = -1,
	GFX_EYE_RIGHT = 1
};

/* Parallax in px per eye at slider = 1.0; also the overdraw margin used by
 * full-width backdrops so no gap shows at the screen edges. */
#define GFX_3D_BASE_SHIFT 6.0f
#define GFX_DEPTH_GROUND 1.0f   /* deepest layer: map floor, battle backdrop */
#define GFX_DEPTH_STAGE  0.5f   /* sprites / props standing on the ground */

/* Poll once per frame with osGet3DSliderState() and the frame delta; toggles
 * gfxSet3D with hysteresis + a short debounce.  Requires TOP_WIDE == 0:
 * widescreen drives the same barrier hardware. */
void gfx_3d_update(float slider, float dt);
/* True while the right eye must be rendered this frame. */
bool gfx_3d_active(void);
/* Select the eye the following draws target; GFX_EYE_NONE = no shift. */
void gfx_3d_begin_eye(int eye);
/* Horizontal pixel offset (whole px) for the current eye at `depth`. */
float gfx_3d_offset(float depth);

bool gfx_load_tiles(const char *romfs_path);
bool gfx_set_theme(int theme);
int gfx_theme(void);
/* Name of the active tile/UI palette (原味绿 / 液晶蓝 / 灰度). */
const char *gfx_theme_name(void);

/* Player walk sprites (data/player.bin, PSPR v1: 12 frames + 12 AND masks,
 * 32x48 1bpp each). Frames are indexed dir*3 + status with the engine's
 * DIR_DOWN/LEFT/UP/RIGHT order; the atlas is retinted on theme switches. */
bool gfx_load_player(const char *romfs_path);
/* (x, y) is the sprite top-left; scale 1.0 = 32x48. */
void gfx_player(int frame, float x, float y, float scale);
/* Same, but clipped to the map viewport rect. */
void gfx_player_clipped(int frame, float x, float y, float scale,
                        float clip_x, float clip_y, float clip_w, float clip_h);
/* Only the top srcH rows of a frame (fight.s raises the 48-row sprite to 32
 * for the battlefield portrait). */
void gfx_player_top(int frame, float x, float y, int srcH);

/* Bottom-screen UI palette slots. Every page reads colours through
 * gfx_ui_color() so Select-theming recolours the UI together with the map
 * atlas. Slot meanings:
 *   BG      page background            PANEL   card / button body
 *   BORDER  1px frame lines            TEXT    normal text
 *   DIM     secondary text / lines     ACCENT  highlight text & markers
 *   SEL     selected row body          BAR     neutral bar fill
 *   BARBG   bar track                  OFF     disabled
 *   HP      hp bar fill                MP      mp bar fill
 *   DANGER  crit / low-value warning
 *   PAPER..INK are the four original shade steps (shade0..shade3) used by
 *   the battle arena; keeping them as slots lets the arena recolour with
 *   the map atlas instead of hard-coding green. */
enum
{
	UI_COL_BG = 0,
	UI_COL_PANEL,
	UI_COL_BORDER,
	UI_COL_TEXT,
	UI_COL_DIM,
	UI_COL_ACCENT,
	UI_COL_SEL,
	UI_COL_BAR,
	UI_COL_BARBG,
	UI_COL_OFF,
	UI_COL_HP,
	UI_COL_MP,
	UI_COL_DANGER,
	UI_COL_PAPER,
	UI_COL_SHADE,
	UI_COL_SHADE2,
	UI_COL_INK,
	UI_COL_COUNT
};

u32 gfx_ui_color(int slot);

/* Text (system font for M0/M1 debug HUD; replaced by font.bin in M2). */
void gfx_text_begin(void);
void gfx_text(const char *utf8, float x, float y, float scale, u32 color);
float gfx_text_width(const char *utf8, float scale);

void gfx_tile(u16 tile, TileLayer layer, float x, float y);
/* Scaled (and optionally mirrored) tile, used by the battle arena to give
 * the two fighters the same visual weight. */
void gfx_tile_scaled(u16 tile, TileLayer layer, float x, float y,
                     float sx, float sy, bool flipX);
void gfx_tile_clipped(u16 tile, TileLayer layer, float x, float y,
                      float clip_x, float clip_y, float clip_w, float clip_h);

#endif

#include "ui.h"
#include "actor.h"
#include "gfx.h"
#include "map.h"
#include "task.h"
#include "text.h"

#include <stdio.h>

static const u32 CLR_VOID = C2D_RGBA(0x10, 0x18, 0x10);
#define CLR_FRAME ui_col(UI_COL_BORDER)
#define CLR_ACCENT ui_col(UI_COL_ACCENT)

static const char *const SHI_NAMES[12] =
{
	"子时", "丑时", "寅时", "卯时", "辰时", "巳时",
	"午时", "未时", "申时", "酉时", "戌时", "亥时"
};

const char *ui_shi_name(u32 playSeconds)
{
	/* The original has no 时辰 name (rank.s prints game_hour:game_min).
	 * Keep the cosmetic badge but derive it from real play hours: one
	 * traditional 时辰 = 2 hours, starting at 子时. */
	return SHI_NAMES[(playSeconds / 7200u) % 12];
}

/* game.s main_menu: 查看 / 物品 / 技能 / 功能, plus the 3DS port's 设置. */
const char *ui_menu_label(int index)
{
	static const char *const LABELS[UI_MENU_ITEMS] =
	{
		"查看", "物品", "技能", "功能", "设置"
	};
	if (index < 0 || index >= UI_MENU_ITEMS)
		return "";
	return LABELS[index];
}

static void draw_map(const GameMap *m, int cam_x, int cam_y,
                     const Player *p)
{
	const int viewX = MAP_VIEW_X;
	const int viewY = MAP_VIEW_Y;
	const int viewW = MAP_VIEW_W;
	const int viewH = MAP_VIEW_H;

	int mapW = m->w * TILE;
	int mapH = m->h * TILE;
	int offX = mapW < viewW ? (viewW - mapW) / 2 : 0;
	int offY = mapH < viewH ? (viewH - mapH) / 2 : 0;
	int originX = viewX + offX - cam_x;
	int originY = viewY + offY - cam_y;

	int bx0 = 0, by0 = 0, bx1 = m->w - 1, by1 = m->h - 1;
	if (mapW > viewW)
	{
		bx0 = cam_x / TILE;
		bx1 = (cam_x + viewW - 1) / TILE;
	}
	if (mapH > viewH)
	{
		by0 = cam_y / TILE;
		by1 = (cam_y + viewH - 1) / TILE;
	}
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 >= m->w) bx1 = m->w - 1;
	if (by1 >= m->h) by1 = m->h - 1;

	for (int by = by0; by <= by1; by++)
	{
		for (int bx = bx0; bx <= bx1; bx++)
		{
			u16 cell = map_cell(m, bx, by);
			if (cell == 0xFFFF)
				continue;
			float x = (float)(originX + bx * TILE);
			float y = (float)(originY + by * TILE);

			u16 gtile = m->ground[CELL_GROUND(cell)];
			gfx_tile_clipped(gtile, LAYER_GROUND, x, y, viewX, viewY, viewW, viewH);
			{
				u16 obj = CELL_OBJ(cell);
				if (obj)
				{
					u16 tile = map_resolve_tile(m, obj);
					/* tools.s make_ground: tile 0 is the "no object image"
					 * sentinel, not a blank 32x32 paper sprite. Painting it
					 * hid 74 door openings and the blank walls: both became
					 * the same solid square. Keep the ground instead, so an
					 * exit shows the floor tile cut into the wall. */
					if (tile != INVALID_TILE && tile != 0)
						gfx_tile_clipped(tile, LAYER_OBJECT, x, y, viewX, viewY, viewW, viewH);
				}
			}
		}
	}

	/* tools.s draw_scroll: the killer is an overlay at G_Killer_Map/X/Y. */
	if (p && p->ghostActive && map_get(p->ghostMap) == m)
	{
		float gx = (float)(originX + p->ghostX * TILE);
		float gy = (float)(originY + p->ghostY * TILE);
		gfx_tile_clipped(task_ghost_tile(p), LAYER_OBJECT, gx, gy,
		                 viewX, viewY, viewW, viewH);
	}
}

/* The original map screen carries no HUD at all; place/time moved to the
 * bottom dashboard so the world stays uncluttered. */
void ui_top_draw(const GameMap *m, int cam_x, int cam_y,
                 const Player *p, u32 playSeconds)
{
	(void)playSeconds;

	C2D_DrawRectSolid(0, 0, 0.0f, SCR_W, SCR_H, CLR_VOID);
	C2D_DrawRectSolid(MAP_VIEW_X, MAP_VIEW_Y, 0.0f, MAP_VIEW_W, MAP_VIEW_H, CLR_VOID);
	draw_map(m, cam_x, cam_y, p);
	actor_draw(p, m, cam_x, cam_y);

	/* 1px frame + corner brackets around the map viewport */
	ui_frame(MAP_VIEW_X - 1, MAP_VIEW_Y - 1, MAP_VIEW_W + 2, MAP_VIEW_H + 2, CLR_FRAME);
	const int BR = 7;
	const int corners[4][2] =
	{
		{ MAP_VIEW_X - 2, MAP_VIEW_Y - 2 },
		{ MAP_VIEW_X + MAP_VIEW_W - BR + 2, MAP_VIEW_Y - 2 },
		{ MAP_VIEW_X - 2, MAP_VIEW_Y + MAP_VIEW_H - BR + 2 },
		{ MAP_VIEW_X + MAP_VIEW_W - BR + 2, MAP_VIEW_Y + MAP_VIEW_H - BR + 2 },
	};
	for (int i = 0; i < 4; i++)
	{
		int cx = corners[i][0], cy = corners[i][1];
		u32 clr = CLR_ACCENT;
		C2D_DrawRectSolid((float)cx, (float)cy, 0.0f, (float)BR, 1, clr);
		C2D_DrawRectSolid((float)cx, (float)cy, 0.0f, 1, (float)BR, clr);
	}

	/* World feedback lives over the map, not on the bottom dashboard. The
	 * notice box is anchored to its bottom edge and grows upward when the
	 * message needs more than one line. */
	ui_notice(MAP_VIEW_X + 4, MAP_VIEW_Y + MAP_VIEW_H - 4,
	          MAP_VIEW_W - 8);

	/* Transient toasts (rewards, level up) drop in from the top edge. */
	ui_toast_draw();
}

/* Boot title card.  The original went straight into the character sheet on
 * every boot, so this screen is the 3DS port's own addition: it lets the
 * player walk back into an existing run instead of replaying the creation
 * page.  Drawn in the same ink-on-paper language as the rest of the game. */
void ui_top_title_draw(void)
{
	C2D_DrawRectSolid(0, 0, 0.0f, SCR_W, SCR_H, ui_col(UI_COL_BG));

	/* Double frame, like a scroll cover. */
	ui_frame(6, 6, SCR_W - 12, SCR_H - 12, ui_col(UI_COL_BORDER));
	ui_frame(10, 10, SCR_W - 20, SCR_H - 20, ui_col(UI_COL_DIM));

	const char *title = "白金英雄坛说";
	int w = text_width(title, 3);
	/* Negative depth pops the title toward the viewer; the rest of the
	 * card (frame, motto) stays glued to the glass. */
	text_draw3(title, (SCR_W - w) / 2, 62, 3, ui_col(UI_COL_ACCENT), -0.6f);

	/* One quiet line under the title; the title card is the port's own
	 * addition and should stay as bare as the rest of the game. */
	const char *motto = "江湖路远，来日方长。";
	w = text_width(motto, 1);
	text_draw(motto, (SCR_W - w) / 2, 162, 1, ui_col(UI_COL_TEXT));
}

/* Black overlay used for the map-change wipe.  amount 0 = clear, 1 = opaque. */
void ui_top_fade(float amount)
{
	if (amount <= 0.0f)
		return;
	if (amount > 1.0f)
		amount = 1.0f;
	u32 alpha = (u32)(255.0f * amount);
	C2D_DrawRectSolid(0, 0, 0.0f, (float)SCR_W, (float)SCR_H, alpha << 24);
}

/* input.s adjust_attr is a single WQX screen; on the 3DS the four stat rows
 * live on the bottom panel, so the top screen is a standalone title card.
 * Deliberately draws no map/player: the run has not started yet. */
void ui_top_charcreate_draw(const CharCreate *cc)
{
	char line[96];

	C2D_DrawRectSolid(0, 0, 0.0f, SCR_W, SCR_H, ui_col(UI_COL_BG));

	int w = text_width("创建角色", 2);
	text_draw("创建角色", (SCR_W - w) / 2, 26, 2, CLR_ACCENT);
	w = text_width("分配先天资质", 1);
	text_draw("分配先天资质", (SCR_W - w) / 2, 64, 1, ui_col(UI_COL_DIM));

	ui_panel(56, 88, 288, 80);
	for (int i = 0; i < CC_ATTR_COUNT; i++)
	{
		int cx = 56 + 36 + i * 72;
		int nw = text_width(cc_attr_name(i), 1);
		text_draw(cc_attr_name(i), cx - nw / 2, 98, 1, ui_col(UI_COL_DIM));
		snprintf(line, sizeof(line), "%d", cc->attr[i]);
		w = text_width(line, 2);
		text_draw(line, cx - w / 2, 120, 2,
		          cc->pointer == i + 1 ? CLR_ACCENT : ui_col(UI_COL_TEXT));
	}

	snprintf(line, sizeof(line), "姓名 %s", cc->name);
	text_draw(line, 24, 182, 1, ui_col(UI_COL_TEXT));

	/* Gender follows input.s:92 (str>=16 male); 相貌/福缘 are rolled on
	 * confirm, so they are previewed as random rather than shown here. */
	int gender = cc_gender_from_str(cc->attr[CC_ATTR_STR]);
	snprintf(line, sizeof(line), "性别 %s", cc_gender_name(gender));
	text_draw(line, 168, 182, 1, ui_col(UI_COL_TEXT));

	snprintf(line, sizeof(line), "剩余点数 %d", cc->spare);
	w = text_width(line, 1);
	text_draw(line, SCR_W - 24 - w, 182, 1,
	          cc->spare ? ui_col(UI_COL_DANGER) : CLR_ACCENT);
}

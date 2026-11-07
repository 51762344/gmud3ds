#include "ui.h"
#include "actor.h"
#include "dialog.h"
#include "gfx.h"
#include "item.h"
#include "log.h"
#include "map.h"
#include "neili.h"
#include "npcdata.h"
#include "save.h"
#include "skill.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

#define WIPE_SECONDS 0.18f

/* All UI colours follow the active theme (Select); see gfx.c s_ui_pal. */
#define CLR_BG      ui_col(UI_COL_BG)
#define CLR_TX      ui_col(UI_COL_TEXT)
#define CLR_DIM     ui_col(UI_COL_DIM)
#define CLR_ACCENT  ui_col(UI_COL_ACCENT)
#define CLR_PANEL   ui_col(UI_COL_PANEL)
#define CLR_SELBG   ui_col(UI_COL_SEL)
#define CLR_BORDER  ui_col(UI_COL_BORDER)
#define CLR_BAR     ui_col(UI_COL_BAR)
#define CLR_BARBG   ui_col(UI_COL_BARBG)
#define CLR_OFF     ui_col(UI_COL_OFF)
#define CLR_HP      ui_col(UI_COL_HP)
#define CLR_MP      ui_col(UI_COL_MP)
#define CLR_DANGER  ui_col(UI_COL_DANGER)
/* Two tiny dark-purple corner markers so tools/azahar_touch.ps1 can locate
 * the 320x240 bottom screen even when a panel covers the background colour.
 * Must stay theme-independent: automation locates them by colour. */
static const u32 CLR_ANCHOR = C2D_RGBA(0x2A, 0x00, 0x30);

void ui_field_button_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8 + index * 156;
	*y = 196;
	*w = 148;
	*h = 32;
}

/* Small touch exit for pages that otherwise only answer to B. */
void ui_back_rect(int *x, int *y, int *w, int *h)
{
	*x = 250;
	*y = 214;
	*w = 54;
	*h = 22;
}

/* game.s main_menu is a vertical list, not a page grid.  Five rows now
 * (查看 / 物品 / 技能 / 功能 / 设置); 28 + 4*36 + 32 = 204 keeps the 返回
 * button at the bottom clear. */
void ui_menu_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 16;
	*y = 28 + index * 36;
	*w = 288;
	*h = 32;
}

void ui_look_tab_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8 + index * 100;
	*y = 24;
	*w = 96;
	*h = 22;
}

void ui_system_row_rect(int index, int *x, int *y, int *w, int *h)
{
	/* Three rows: 内力 / 练功 / 返回, tall enough to be comfortable. */
	*x = 16;
	*y = 62 + index * 54;
	*w = 288;
	*h = 46;
}

void ui_settings_row_rect(int index, int *x, int *y, int *w, int *h)
{
	/* 主题 / 存档 / 结束 / 返回: 40 + 3*44 + 38 = 210. */
	*x = 16;
	*y = 40 + index * 44;
	*w = 288;
	*h = 38;
}

void ui_quit_button_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = index == UI_QUIT_NO ? 76 : 172;
	*y = 142;
	*w = 72;
	*h = 34;
}

/* Four rows now: the three manual slots plus the auto slot the game writes
 * itself. 26 + 3*48 + 44 = 214, buttons sit in the strip below. */
void ui_save_slot_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 12;
	*y = 26 + index * 48;
	*w = 296;
	*h = 44;
}

void ui_save_button_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 12 + index * 102;
	*y = 216;
	*w = 92;
	*h = 22;
}

/* 行囊 is a framed list, like the original goods menu (goodsx.s list_menu):
 * one row per filled slot, name / type / count columns. */
void ui_bag_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 10;
	*y = 30 + index * 22;
	*w = 300;
	*h = 20;
}

void ui_bag_action_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8 + index * 77;
	*y = 204;
	*w = 72;
	*h = 32;
}

/* 上页 / 下页, only drawn when the list needs more than one page. */
void ui_bag_page_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8 + index * 76;
	*y = 206;
	*w = 72;
	*h = 28;
}

void ui_bag_back_rect(int *x, int *y, int *w, int *h)
{
	/* Same column as the last action button: the row keeps its rhythm
	 * whether or not a slot is open. */
	*x = 239;
	*y = 206;
	*w = 72;
	*h = 28;
}

/* Detail sheet for the picked slot.  It starts right below the fifth row
 * (y=138), so every item on the page stays fully visible (and tappable). */
void ui_bag_sheet_rect(int *x, int *y, int *w, int *h)
{
	*x = 0;
	*y = 140;
	*w = 320;
	*h = 100;
}

void ui_shop_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 6;
	*y = 54 + index * 24;
	*w = 308;
	*h = 24;
}

void ui_shop_button_rect(int index, int *x, int *y, int *w, int *h)
{
	/* 上页 / 下页 on the left, 返回 on the far right. */
	*x = (index == UI_SHOP_BTN_BACK) ? 244 : (8 + index * 72);
	*y = 216;
	*w = 68;
	*h = 22;
}

void ui_skill_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8;
	*y = 28 + index * 20;
	*w = 304;
	*h = 20;
}

void ui_train_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8;
	*y = 28 + index * 22;
	*w = 304;
	*h = 20;
}

void ui_train_button_rect(int index, int *x, int *y, int *w, int *h)
{
	/* 0 = 开始/停止, 1 = 返回. */
	*x = index == 0 ? 8 : 212;
	*y = 214;
	*w = 100;
	*h = 24;
}

void ui_neili_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8;
	*y = 28 + index * 34;
	*w = 304;
	*h = 32;
}

/* M5.5 加力 stepper: 0 = −, 1 = ＋, 2 = 确认, 3 = 取消. */
void ui_neili_edit_rect(int index, int *x, int *y, int *w, int *h)
{
	switch (index)
	{
	case 0: *x = 8;   *y = 188; *w = 44; *h = 28; break;   /* − */
	case 1: *x = 132; *y = 188; *w = 44; *h = 28; break;   /* ＋ */
	case 2: *x = 196; *y = 188; *w = 56; *h = 28; break;   /* 确认 */
	default: *x = 256; *y = 188; *w = 56; *h = 28; break;  /* 取消 */
	}
}

/* T5 创建页: input.s adjust_attr shows exactly four rows.  The 3DS port
 * puts the name field (input_new's first step) above them. */
void ui_cc_name_rect(int *x, int *y, int *w, int *h)
{
	*x = 8;
	*y = 26;
	*w = 304;
	*h = 28;
}

void ui_cc_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8;
	*y = 78 + index * 30;
	*w = 304;
	*h = 28;
}

void ui_cc_step_rect(int index, int which, int *x, int *y, int *w, int *h)
{
	int ry;
	ui_cc_row_rect(index, x, &ry, w, h);
	*x = 140 + which * 56;
	*y = ry + 2;
	*w = 48;
	*h = 26;
}

void ui_cc_button_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8 + index * 104;
	*y = 212;
	*w = 96;
	*h = 26;
}

/* 标题页: 继续游戏 / 新的旅程 / 读取存档 / 退出游戏. */
void ui_title_row_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 24;
	*y = 46 + index * 40;
	*w = 272;
	*h = 36;
}

void ui_init(UiState *ui)
{
	memset(ui, 0, sizeof(*ui));
	ui->page = UI_PAGE_FIELD;
	ui->wipe = 1.0f;
	ui->lookTab = UI_LOOK_STATUS;
	ui->systemSel = UI_SYSTEM_NEILI;
	ui->settingsSel = UI_SETTINGS_THEME;
	ui->quitSel = UI_QUIT_NO;
	ui->bagSel = -1;
	ui->bagAct = -1;
	ui->shopNpc = 0;
	ui->shopTab = 0;
	ui->shopPage = 0;
	ui->shopSel = -1;
}

void ui_set_page(UiState *ui, int page)
{
	if (!ui || page < 0 || page >= UI_PAGE_COUNT)
		return;
	if (page == ui->page)
		return;
	ui->page = page;
	ui->wipe = 0.0f;
	if (page == UI_PAGE_BAG)
	{
		ui->bagSel = -1;
		ui->bagAct = -1;
	}
}

void ui_update(UiState *ui, float dt)
{
	if (ui->wipe < 1.0f)
	{
		ui->wipe += dt / WIPE_SECONDS;
		if (ui->wipe > 1.0f)
			ui->wipe = 1.0f;
	}
}

static void text_line(const char *s, int x, int y, u32 color)
{
	text_draw(s, x, y, 1, color);
}

/* "label value" pair with a dim label and a bright value. */
static void text_pair(const char *label, const char *value, int x, int y, u32 vclr)
{
	text_line(label, x, y, CLR_DIM);
	text_line(value, x + text_width(label, 1) + 4, y, vclr);
}

/* ------------------------------------------------------------------ field */

/* The 3DS bottom screen is a dashboard, not a scrolling log: a map overview
 * plus the vitals needed while walking. World messages (pickups, quests)
 * surface on the top screen as a short-lived notice (ui_notice). */

/* Small centred marker; large maps keep 1-cell dots, small maps get a
 * readable square without turning the whole tile into a bright block. */
static void mini_dot(int x, int y, int cell, u32 clr)
{
	int s = cell >= 6 ? cell / 2 : cell;
	if (s < 1)
		s = 1;
	C2D_DrawRectSolid((float)(x + (cell - s) / 2), (float)(y + (cell - s) / 2),
	                  0.0f, (float)s, (float)s, clr);
}

static void draw_minimap(const GameMap *m, const Player *p, int x, int y, int w, int h)
{
	int cell, mw, mh, ox, oy;

	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h, CLR_BG);
	ui_frame(x, y, w, h, CLR_BORDER);
	if (!m || m->w == 0 || m->h == 0)
		return;

	cell = (w - 16) / m->w;
	if (cell > (h - 16) / m->h)
		cell = (h - 16) / m->h;
	if (cell < 1)
		cell = 1;   /* huge maps shrink to 1px cells; the loops clip below */
	if (cell > 14)
		cell = 14;
	mw = cell * m->w;
	mh = cell * m->h;
	ox = x + (w - mw) / 2;
	oy = y + (h - mh) / 2;
	if (ox < x + 8)
		ox = x + 8;
	if (oy < y + 8)
		oy = y + 8;
	/* Clip oversized maps to the panel: without this, maps wider than the
	 * panel used to spill the grid over the status column on the right. */
	int clipW = (x + w - 8 - ox) / cell;
	int clipH = (y + h - 8 - oy) / cell;
	if (clipW > m->w)
		clipW = m->w;
	if (clipH > m->h)
		clipH = m->h;

	/* Floors are the baseline; walls sit a shade darker, points of interest
	 * stay bright. Reads as a map without a wall of outlines. */
	C2D_DrawRectSolid((float)ox, (float)oy, 0.0f, (float)mw, (float)mh, CLR_BARBG);
	for (int by = 0; by < clipH; by++)
	{
		for (int bx = 0; bx < clipW; bx++)
		{
			u16 c = map_cell(m, bx, by);
			u16 obj;
			int cx = ox + bx * cell;
			int cy = oy + by * cell;

			if (c == 0xFFFF)
			{
				C2D_DrawRectSolid((float)cx, (float)cy, 0.0f,
				                  (float)cell, (float)cell, CLR_BG);
				continue;
			}
			obj = CELL_OBJ(c);
			if (obj >= NPC_OFF)
			{
				if (map_resolve_tile(m, obj) != INVALID_TILE)
					mini_dot(cx, cy, cell, CLR_TX);
			}
			else if (obj >= DOOR_OFF)
			{
				mini_dot(cx, cy, cell, CLR_DIM);
			}
			else if (obj >= BLAK_OFF)
			{
				C2D_DrawRectSolid((float)cx, (float)cy, 0.0f,
				                  (float)cell, (float)cell, CLR_PANEL);
			}
		}
	}

	if (p && p->ghostActive && p->ghostMap == p->mapIndex &&
	    p->ghostX < clipW && p->ghostY < clipH)
		mini_dot(ox + p->ghostX * cell, oy + p->ghostY * cell, cell, CLR_DANGER);

	if (p && p->tx < clipW && p->ty < clipH)
	{
		int s = cell + 2;
		int px, py;
		if (s < 5)
			s = 5;
		px = ox + p->tx * cell + (cell - s) / 2;
		py = oy + p->ty * cell + (cell - s) / 2;
		/* Dark halo so the marker stays visible on doors and NPC tiles. */
		C2D_DrawRectSolid((float)(px - 1), (float)(py - 1), 0.0f,
		                  (float)(s + 2), (float)(s + 2), CLR_BG);
		C2D_DrawRectSolid((float)px, (float)py, 0.0f,
		                  (float)s, (float)s, CLR_ACCENT);
	}
}

static void draw_field(const Player *p, const GameMap *m, u32 playSeconds)
{
	char line[96];
	const int colX = 192;
	const int colR = 312;
	bool hungry = p->food < 15;
	bool thirsty = p->water < 15;
	bool hurt = p->effhp < p->hpMax;

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);

	text_line(m ? m->name : "", 10, 3, CLR_ACCENT);
	ui_text_right(ui_shi_name(playSeconds), 310, 3, CLR_DIM);
	C2D_DrawRectSolid(10, 21, 0.0f, 300, 1, CLR_BORDER);
	C2D_DrawRectSolid(10, 21, 0.0f, 28, 2, CLR_ACCENT);

	draw_minimap(m, p, 8, 28, 176, 144);

	/* Identity */
	text_line(p->name, colX, 28, CLR_TX);
	text_line(npc_pai_name(p->pai), colX, 46, CLR_DIM);

	/* Vitals: numbers are hp/effhp, the bar is hp/hpMax with the injury cap
	 * marked underneath (same reading as the battle screen). */
	snprintf(line, sizeof(line), "%d/%d", (int)p->hp, (int)p->effhp);
	text_line("体力", colX, 68, CLR_DIM);
	ui_text_right(line, colR, 68, (int)p->hp * 4 < (int)p->effhp ? CLR_DANGER : CLR_TX);
	ui_bar(colX, 86, colR - colX, 8, p->hp, p->hpMax, CLR_HP);
	if (hurt && p->hpMax > 0)
	{
		int cap = (colR - colX - 2) * p->effhp / p->hpMax;
		if (cap > 0)
			C2D_DrawRectSolid((float)(colX + 1), 97.0f, 0.0f,
			                  (float)cap, 1.0f, CLR_HP);
	}

	snprintf(line, sizeof(line), "%d/%d", (int)p->mp, (int)p->mpMax);
	text_line("内力", colX, 104, CLR_DIM);
	ui_text_right(line, colR, 104, CLR_TX);
	ui_bar(colX, 122, colR - colX, 8, p->mp, p->mpMax, CLR_MP);

	C2D_DrawRectSolid((float)colX, 138.0f, 0.0f, (float)(colR - colX), 1.0f, CLR_BORDER);

	snprintf(line, sizeof(line), "%lu", (unsigned long)p->money);
	text_line("文钱", colX, 144, CLR_DIM);
	ui_text_right(line, colR, 144, CLR_ACCENT);

	snprintf(line, sizeof(line), "%d/%d", inv_used_slots(p->bag), BAG_SLOTS);
	text_line("行囊", colX, 162, CLR_DIM);
	ui_text_right(line, colR, 162,
	              inv_used_slots(p->bag) >= BAG_SLOTS ? CLR_DANGER : CLR_TX);

	text_line("食", colX, 180, CLR_DIM);
	snprintf(line, sizeof(line), "%d", (int)p->food);
	text_line(line, colX + 22, 180, hungry ? CLR_DANGER : CLR_TX);
	text_line("水", colX + 62, 180, CLR_DIM);
	snprintf(line, sizeof(line), "%d", (int)p->water);
	text_line(line, colX + 84, 180, thirsty ? CLR_DANGER : CLR_TX);

	/* No objective tracker: the original never told the player what to do
	 * next, so the dashboard stays a plain map + vitals sheet. */

	static const char *const BTN[UI_FIELD_BTN_COUNT] = { "互动", "菜单" };
	for (int i = 0; i < UI_FIELD_BTN_COUNT; i++)
	{
		int bx, by, bw, bh;
		ui_field_button_rect(i, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, BTN[i], UI_BTN_NORMAL);
	}
}

/* ------------------------------------------------------------------- look */

static void draw_look_status(const Player *p)
{
	char line[96];
	bool hurt = p->effhp < p->hpMax;
	int pct = p->effhp > 0 ? (int)p->hp * 100 / p->effhp : 0;

	snprintf(line, sizeof(line), "食物 %d/%d", (int)p->food, (int)p->maxFood);
	text_line(line, 16, 58, p->food < 15 ? CLR_DANGER : CLR_TX);
	snprintf(line, sizeof(line), "饮水 %d/%d", (int)p->water, (int)p->maxWater);
	text_line(line, 172, 58, p->water < 15 ? CLR_DANGER : CLR_TX);

	snprintf(line, sizeof(line), "%d/%d%s (%d%%)", (int)p->hp, (int)p->effhp,
	         hurt ? "伤" : "", pct);
	text_pair("体力", line, 16, 82, (hurt || pct < 25) ? CLR_DANGER : CLR_TX);
	/* Bar is absolute HP; the notch marks the injury cap (effhp). */
	ui_bar(16, 100, 288, 9, p->hp, p->hpMax, CLR_HP);
	if (hurt && p->hpMax > 0)
	{
		int nx = 17 + 286 * p->effhp / p->hpMax;
		C2D_DrawRectSolid((float)nx, 101.0f, 0.0f, 1.0f, 7.0f, CLR_ACCENT);
	}

	if (p->force > 0)
		snprintf(line, sizeof(line), "%d/%d (+%d)", (int)p->mp, (int)p->mpMax,
		         (int)p->force);
	else
		snprintf(line, sizeof(line), "%d/%d", (int)p->mp, (int)p->mpMax);
	text_pair("内力", line, 16, 118, CLR_TX);
	ui_bar(16, 136, 288, 9, p->mp, p->mpMax, CLR_MP);

	snprintf(line, sizeof(line), "%lu", (unsigned long)p->exp);
	text_pair("经验", line, 16, 156, CLR_TX);
	snprintf(line, sizeof(line), "%u", p->potential);
	text_pair("潜能", line, 176, 156, CLR_TX);

	snprintf(line, sizeof(line), "%lu", (unsigned long)p->money);
	text_pair("文钱", line, 16, 180, CLR_ACCENT);
	{
		int used = inv_used_slots(p->bag);
		snprintf(line, sizeof(line), "%d/%d", used, BAG_SLOTS);
		text_pair("行囊", line, 176, 180, used >= BAG_SLOTS ? CLR_DANGER : CLR_TX);
	}
}

static void draw_look_desc(const Player *p)
{
	char line[160];
	const NpcInfo *mt = p->master ? npc_info(p->master) : NULL;

	snprintf(line, sizeof(line), "[%s] %s", npc_pai_name(p->pai), p->name);
	text_line(line, 16, 58, CLR_ACCENT);
	/* game.s xingbei: '男女？' - gender 2 (人妖) shows '？'. */
	snprintf(line, sizeof(line), "你是一位%d岁的%s性", (int)p->age,
	         p->gender == 0 ? "男" : p->gender == 1 ? "女" : "？");
	text_line(line, 16, 84, CLR_TX);
	snprintf(line, sizeof(line), "你%s", actor_looks_desc(p));
	text_line(line, 16, 110, CLR_TX);
	snprintf(line, sizeof(line), "武艺看起来%s", actor_martial_desc(p));
	text_line(line, 16, 136, CLR_TX);
	snprintf(line, sizeof(line), "出手似乎%s", actor_strength_desc(p));
	text_line(line, 16, 162, CLR_TX);
	/* game.s desc_msg has five lines and never names the master; this
	 * project adds the sixth line because there is no NPC 说话头像 /
	 * 门派 page to carry that information. */
	text_line("师父：", 16, 188, CLR_DIM);
	text_line(mt ? mt->name : "无", 16 + text_width("师父：", 1) + 2, 188,
	          mt ? CLR_ACCENT : CLR_DIM);
}

static void draw_look_attr(const Player *p)
{
	char left[64], right[64];

	snprintf(left, sizeof(left), "%lu", (unsigned long)p->money);
	text_pair("文钱", left, 16, 58, CLR_ACCENT);

	snprintf(left, sizeof(left), "膂力 [%d/%d]", (int)p->str, (int)p->attrStr);
	snprintf(right, sizeof(right), "命中 [%d]", (int)p->attack);
	text_line(left, 16, 86, CLR_TX);
	text_line(right, 176, 86, CLR_TX);

	snprintf(left, sizeof(left), "敏捷 [%d/%d]", (int)p->dex, (int)p->attrDex);
	snprintf(right, sizeof(right), "回避 [%d]", (int)p->defense);
	text_line(left, 16, 112, CLR_TX);
	text_line(right, 176, 112, CLR_TX);

	snprintf(left, sizeof(left), "悟性 [%d/%d]", (int)p->intel, (int)p->attrIntel);
	snprintf(right, sizeof(right), "攻击 [%d]", (int)p->damage);
	text_line(left, 16, 138, CLR_TX);
	text_line(right, 176, 138, CLR_TX);

	snprintf(left, sizeof(left), "根骨 [%d/%d]", (int)p->con, (int)p->attrCon);
	snprintf(right, sizeof(right), "防御 [%d]", (int)p->armor);
	text_line(left, 16, 164, CLR_TX);
	text_line(right, 176, 164, CLR_TX);
}

static void draw_look(const UiState *ui, const Player *p)
{
	static const char *const TABS[UI_LOOK_TAB_COUNT] = { "状态", "描述", "属性" };

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("查看");
	for (int i = 0; i < UI_LOOK_TAB_COUNT; i++)
	{
		int x, y, w, h;
		bool active = (i == ui->lookTab);
		ui_look_tab_rect(i, &x, &y, &w, &h);
		text_draw(TABS[i], x + (w - text_width(TABS[i], 1)) / 2, y + 2, 1,
		          active ? CLR_ACCENT : CLR_DIM);
		if (active)
			C2D_DrawRectSolid((float)(x + 18), (float)(y + h - 2), 0.0f,
			                  (float)(w - 36), 2.0f, CLR_ACCENT);
	}

	ui_panel(8, 52, 304, 156);
	if (ui->lookTab == UI_LOOK_DESC)
		draw_look_desc(p);
	else if (ui->lookTab == UI_LOOK_ATTR)
		draw_look_attr(p);
	else
		draw_look_status(p);

	{
		int x, y, w, h;
		ui_back_rect(&x, &y, &w, &h);
		ui_button(x, y, w, h, "返回", UI_BTN_NORMAL);
	}
}

/* ----------------------------------------------------------------- system */

static void draw_system(const UiState *ui)
{
	const char *labels[UI_SYSTEM_COUNT] = { "内力", "练功", "返回" };

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("功能");

	for (int i = 0; i < UI_SYSTEM_COUNT; i++)
	{
		int x, y, w, h;
		ui_system_row_rect(i, &x, &y, &w, &h);
		bool sel = (i == ui->systemSel);
		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
		                  sel ? CLR_SELBG : CLR_PANEL);
		ui_frame(x, y, w, h, sel ? CLR_ACCENT : CLR_BORDER);
		if (sel)
			C2D_DrawRectSolid((float)x, (float)y, 0.0f, 3.0f, (float)h, CLR_ACCENT);
		text_line(labels[i], x + 18, y + (h - 16) / 2, sel ? CLR_ACCENT : CLR_TX);
	}
}

/* 设置: shell options, kept apart from the 内力 / 练功 actions.  The theme row
 * names the active palette, so the page reads as a settings list instead of a
 * blind submenu. */
static void draw_settings(const UiState *ui)
{
	char themeRow[40];
	snprintf(themeRow, sizeof(themeRow), "主题：%s", gfx_theme_name());
	const char *labels[UI_SETTINGS_COUNT] =
	{
		themeRow, "存档", "结束本局", "返回"
	};

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("设置");

	for (int i = 0; i < UI_SETTINGS_COUNT; i++)
	{
		int x, y, w, h;
		ui_settings_row_rect(i, &x, &y, &w, &h);
		bool sel = (i == ui->settingsSel);
		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
		                  sel ? CLR_SELBG : CLR_PANEL);
		ui_frame(x, y, w, h, sel ? CLR_ACCENT : CLR_BORDER);
		if (sel)
			C2D_DrawRectSolid((float)x, (float)y, 0.0f, 3.0f, (float)h, CLR_ACCENT);
		text_line(labels[i], x + 18, y + (h - 16) / 2, sel ? CLR_ACCENT : CLR_TX);
	}

	text_line("方向键选择，A 确认，B 返回", 16, 220, CLR_DIM);
}

static void draw_quit(const UiState *ui)
{
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("结束");

	text_line("真的结束这一局吗？", 16, 70, CLR_TX);
	text_line("未保存的进度将会丢失。", 16, 96, CLR_DIM);

	for (int i = 0; i < UI_QUIT_COUNT; i++)
	{
		int x, y, w, h;
		ui_quit_button_rect(i, &x, &y, &w, &h);
		ui_button(x, y, w, h, i == UI_QUIT_NO ? "否" : "是",
		          i == ui->quitSel ? UI_BTN_SELECTED : UI_BTN_NORMAL);
	}
}

/* --------------------------------------------------------------- title */

/* Boot menu.  The original always started with the character sheet, so the
 * title screen is the 3DS port's addition; it only adds a shortcut back into
 * an existing run - the new-game path still ends in the original creation
 * page, unchanged. */
static void draw_title(const UiState *ui)
{
	static const char *const LABELS[UI_TITLE_COUNT] =
	{
		"继续游戏", "新的旅程", "读取存档", "退出游戏"
	};

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("白金英雄坛说");

	for (int i = 0; i < UI_TITLE_COUNT; i++)
	{
		int x, y, w, h;
		ui_title_row_rect(i, &x, &y, &w, &h);
		bool sel = (i == ui->titleSel);
		bool enabled = (i != UI_TITLE_CONTINUE) || ui->hasContinue;
		u32 body = sel && enabled ? CLR_SELBG : CLR_PANEL;
		u32 edge = sel ? (enabled ? CLR_ACCENT : CLR_OFF) : CLR_BORDER;
		u32 fg = !enabled ? CLR_OFF : (sel ? CLR_ACCENT : CLR_TX);

		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h, body);
		ui_frame(x, y, w, h, edge);
		if (sel && enabled)
			C2D_DrawRectSolid((float)x, (float)y, 0.0f, 3, (float)h, CLR_ACCENT);
		text_line(LABELS[i], x + 18, y + (h - 16) / 2, fg);
	}

	text_line("触摸选择，或用方向键与 A", 16, 214, CLR_DIM);
}

/* ------------------------------------------------------------------- menu */

static void draw_menu(const UiState *ui)
{
	/* game.s main_menu: a plain vertical list, no quest or settings entries. */
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("菜单");

	for (int i = 0; i < UI_MENU_ITEMS; i++)
	{
		int x, y, w, h;
		ui_menu_row_rect(i, &x, &y, &w, &h);
		bool sel = (i == ui->menuIndex);

		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
		                  sel ? CLR_SELBG : CLR_PANEL);
		ui_frame(x, y, w, h, sel ? CLR_ACCENT : CLR_BORDER);
		if (sel)
			C2D_DrawRectSolid((float)x, (float)y, 0.0f, 3.0f, (float)h, CLR_ACCENT);

		text_line(ui_menu_label(i), x + 20, y + (h - 16) / 2, sel ? CLR_ACCENT : CLR_TX);
	}

	{
		int x, y, w, h;
		ui_back_rect(&x, &y, &w, &h);
		ui_button(x, y, w, h, "返回", UI_BTN_NORMAL);
	}
}

/* ------------------------------------------------------------------- save */

static void draw_save(const UiState *ui, const SaveSlotInfo slots[SAVE_SLOT_COUNT])
{
	char line[128];

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("存档");

	for (int i = 0; i < SAVE_SLOT_COUNT; i++)
	{
		int x, y, w, h;
		ui_save_slot_rect(i, &x, &y, &w, &h);
		bool sel = (i == ui->saveSlot);
		bool autoSlot = (i == SAVE_SLOT_AUTO);
		const SaveSlotInfo *s = &slots[i];

		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
		                  sel ? CLR_SELBG : CLR_PANEL);
		ui_frame(x, y, w, h, sel ? CLR_ACCENT : CLR_BORDER);

		if (autoSlot)
			snprintf(line, sizeof(line), "自动");
		else
			snprintf(line, sizeof(line), "槽位 %d", i + 1);
		ui_badge(x + 8, y + 12, 52, 20, line,
		         sel ? CLR_ACCENT : CLR_BORDER, sel ? CLR_BG : CLR_BAR);

		if (!s->exists)
		{
			text_line("空（自动档由游戏自己写入）", x + 72, y + 14, CLR_DIM);
		}
		else if (!s->valid)
		{
			text_line("存档损坏", x + 72, y + 14, CLR_DANGER);
		}
		else
		{
			const GameMap *sm = map_get(s->mapIndex);
			const char *mapName = sm ? sm->name : "?";
			snprintf(line, sizeof(line), "%s · %s%s", s->name, mapName,
			         autoSlot ? "（自动）" : "");
			text_line(line, x + 72, y + 4, sel ? CLR_ACCENT : CLR_TX);
			snprintf(line, sizeof(line), "体力 %d/%d    文钱 %lu",
			         (int)s->hp, (int)s->hpMax, (unsigned long)s->money);
			text_line(line, x + 72, y + 22, CLR_DIM);
		}
	}

	static const char *const BTN[UI_SAVE_BTN_COUNT] = { "保存", "读取", "返回" };
	for (int i = 0; i < UI_SAVE_BTN_COUNT; i++)
	{
		int bx, by, bw, bh;
		ui_save_button_rect(i, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, BTN[i], UI_BTN_NORMAL);
	}
}

/* -------------------------------------------------------------------- bag */

/* One row: name, type and quantity.  The original goods menu is a plain
 * list, so nothing has to be squeezed into a small cell. */
static void draw_bag_row(int x, int y, int w, int h, const BagSlot *s, bool sel)
{
	char line[12];
	int id = BAG_ID(*s);
	bool equipped = BAG_EQUIPPED(*s);

	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h,
	                  sel ? CLR_SELBG : CLR_PANEL);
	if (sel)
		ui_frame(x, y, w, h, CLR_ACCENT);
	/* Equipped: accent rail on the left plus an accent name. */
	if (equipped)
		C2D_DrawRectSolid((float)x, (float)y, 0.0f, 2.0f, (float)h, CLR_ACCENT);

	text_line(item_name(id), x + 10, y + 2, (sel || equipped) ? CLR_ACCENT : CLR_TX);
	text_line(item_type_name(id), x + 120, y + 2, CLR_DIM);

	if (equipped)
		ui_text_right("已装备", x + w - 10, y + 2, CLR_ACCENT);
	else if (s->count > 1)
	{
		snprintf(line, sizeof(line), "x%d", s->count);
		ui_text_right(line, x + w - 10, y + 2, CLR_BAR);
	}
}

static void draw_bag(const UiState *ui, const Player *p)
{
	char line[128];
	int slots[BAG_SLOTS];
	int count = inv_list_slots(p->bag, slots, BAG_SLOTS);
	int pages = (count + UI_BAG_ROWS - 1) / UI_BAG_ROWS;
	int page = ui->bagPage;

	if (pages < 1)
		pages = 1;
	if (page < 0)
		page = 0;
	if (page >= pages)
		page = pages - 1;

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("行囊");
	snprintf(line, sizeof(line), "%d/%d", count, BAG_SLOTS);
	ui_badge(50, 2, 46, 18, line, CLR_SELBG,
	         count >= BAG_SLOTS ? CLR_DANGER : CLR_ACCENT);
	snprintf(line, sizeof(line), "文钱 %lu", (unsigned long)p->money);
	ui_text_right(line, 310, 3, CLR_ACCENT);

	/* The framed list is only as tall as its five rows (26..142): the detail
	 * sheet opens in the strip below it, so it never covers an item. */
	ui_frame(8, 26, 304, 116, CLR_BORDER);
	if (count == 0)
	{
		const char *empty = "行囊空空如也。";
		text_line(empty, 8 + (304 - text_width(empty, 1)) / 2, 76, CLR_DIM);
	}
	for (int i = 0; i < UI_BAG_ROWS; i++)
	{
		int index = page * UI_BAG_ROWS + i;
		if (index >= count)
			break;
		int x, y, w, h;
		ui_bag_row_rect(i, &x, &y, &w, &h);
		draw_bag_row(x, y, w, h, &p->bag[slots[index]],
		             slots[index] == ui->bagSel);
	}

	const ItemDef *d = ui->bagSel >= 0 ? item_get(BAG_ID(p->bag[ui->bagSel]))
	                                   : NULL;
	if (!d)
	{
		/* Nothing picked: the footer holds the page buttons (only when the
		 * list overflows) and the way out. */
		if (pages > 1)
		{
			int bx, by, bw, bh;
			ui_bag_page_rect(0, &bx, &by, &bw, &bh);
			ui_button(bx, by, bw, bh, "上页", UI_BTN_NORMAL);
			ui_bag_page_rect(1, &bx, &by, &bw, &bh);
			ui_button(bx, by, bw, bh, "下页", UI_BTN_NORMAL);
			snprintf(line, sizeof(line), "%d/%d", page + 1, pages);
			ui_text_right(line, 232, 212, CLR_DIM);
		}
		int bx, by, bw, bh;
		ui_bag_back_rect(&bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "返回", UI_BTN_NORMAL);
		return;
	}

	int id = BAG_ID(p->bag[ui->bagSel]);
	bool equipped = BAG_EQUIPPED(p->bag[ui->bagSel]);
	bool gear = (d->type == ITEM_WEAPON || d->type == ITEM_EQUIP);
	char effect[64];

	/* Detail sheet for the picked row: name + type, effect, prices / held
	 * count, then the action row.  It sits below the five-row list, so it
	 * never hides an item; the rect is shared with main.c so taps on it
	 * never fall through to the page buttons underneath. */
	int sx, sy, sw, sh;
	ui_bag_sheet_rect(&sx, &sy, &sw, &sh);
	C2D_DrawRectSolid((float)sx, (float)sy, 0.0f, (float)sw, (float)sh, CLR_PANEL);
	C2D_DrawRectSolid((float)sx, (float)sy, 0.0f, (float)sw, 2.0f, CLR_ACCENT);

	text_draw(d->name, 12, 146, 1, CLR_ACCENT);
	snprintf(line, sizeof(line), "%s%s", item_type_name(id),
	         equipped ? " · 已装备" : "");
	ui_text_right(line, 308, 146, equipped ? CLR_ACCENT : CLR_DIM);

	item_describe(id, effect, sizeof(effect));
	text_line(effect, 12, 166, CLR_TX);

	snprintf(line, sizeof(line), "买价 %d   卖价 %d   持有 %d",
	         item_buy_price(id), item_sell_price(id), inv_count(p->bag, id));
	text_line(line, 12, 186, CLR_DIM);

	static const char *const LABELS[UI_BAG_ACT_COUNT] = { "使用", "装备", "丢弃", "返回" };
	int act = ui->bagAct >= 0 ? ui->bagAct
	                          : (gear ? UI_BAG_ACT_EQUIP : UI_BAG_ACT_USE);
	for (int i = 0; i < UI_BAG_ACT_COUNT; i++)
	{
		int bx, by, bw, bh;
		ui_bag_action_rect(i, &bx, &by, &bw, &bh);
		const char *label = LABELS[i];
		if (i == UI_BAG_ACT_USE && gear)
		{
			ui_button(bx, by, bw, bh, label, UI_BTN_DISABLED);
			continue;
		}
		if (i == UI_BAG_ACT_EQUIP)
		{
			if (!gear)
			{
				ui_button(bx, by, bw, bh, label, UI_BTN_DISABLED);
				continue;
			}
			label = equipped ? "卸下" : "装备";
		}
		ui_button(bx, by, bw, bh, label,
		          i == act ? UI_BTN_SELECTED : UI_BTN_NORMAL);
	}
}

/* ------------------------------------------------------------------- shop */

static int shop_vendor_count(const NpcInfo *n)
{
	return n ? (int)n->vendorCount : 0;
}

static int shop_sell_slots(const Player *p, int *slots, int max)
{
	int n = 0;
	for (int i = 0; i < BAG_SLOTS && n < max; i++)
	{
		int id = BAG_ID(p->bag[i]);
		/* goods.s set_all_goods lists equipped goods too (sell_it refuses
		 * them later), but removes the 三角石板 entirely. */
		if (id == 0 || id == ITEM_SANJIAO)
			continue;
		slots[n++] = i;
	}
	return n;
}

static void draw_shop(const UiState *ui, const Player *p)
{
	char line[128];
	const NpcInfo *n = npc_info(ui->shopNpc);
	int rowCount = 0;
	int sellSlots[BAG_SLOTS];

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);

	snprintf(line, sizeof(line), "%s的店铺", n ? n->name : "商人");
	ui_header(line);
	snprintf(line, sizeof(line), "文钱 %lu", (unsigned long)p->money);
	ui_text_right(line, 310, 3, CLR_ACCENT);

	/* 买入/卖出 is chosen in the NPC dialog, so a single mode badge is
	 * clearer than a two-tab strip that cannot be switched here. */
	ui_badge(10, 26, 40, 18, ui->shopTab == 0 ? "买入" : "卖出",
	         CLR_SELBG, CLR_ACCENT);

	if (ui->shopTab == 0)
	{
		rowCount = shop_vendor_count(n);
	}
	else
	{
		rowCount = shop_sell_slots(p, sellSlots, BAG_SLOTS);
	}

	int pages = (rowCount + UI_SHOP_ROWS - 1) / UI_SHOP_ROWS;
	if (pages < 1)
		pages = 1;
	int page = ui->shopPage;
	if (page >= pages)
		page = pages - 1;
	if (page < 0)
		page = 0;

	for (int i = 0; i < UI_SHOP_ROWS; i++)
	{
		int index = page * UI_SHOP_ROWS + i;
		if (index >= rowCount)
			break;

		int rx, ry, rw, rh;
		ui_shop_row_rect(i, &rx, &ry, &rw, &rh);
		/* Alternating row tint so columns read cleanly at a glance; the
		 * keyboard cursor takes the highlight instead. */
		bool sel = (index == ui->shopSel);
		C2D_DrawRectSolid((float)rx, (float)ry, 0.0f, (float)rw, (float)rh - 1,
		                  sel ? CLR_SELBG : ((i & 1) ? CLR_BARBG : CLR_PANEL));
		if (sel)
			ui_frame(rx, ry, rw, rh - 1, CLR_ACCENT);

		if (ui->shopTab == 0)
		{
			int id = n->vendor[index];
			snprintf(line, sizeof(line), "%s", item_name(id));
			text_line(line, rx + 8, ry + 4, sel ? CLR_ACCENT : CLR_TX);
			snprintf(line, sizeof(line), "%d 文", item_buy_price(id));
			ui_text_right(line, rx + rw - 8, ry + 4, CLR_ACCENT);
		}
		else
		{
			int slot = sellSlots[index];
			const BagSlot *s = &p->bag[slot];
			int id = BAG_ID(*s);
			if (s->count > 1)
				snprintf(line, sizeof(line), "%s x%d%s", item_name(id), s->count,
				         BAG_EQUIPPED(*s) ? "（已装备）" : "");
			else
				snprintf(line, sizeof(line), "%s%s", item_name(id),
				         BAG_EQUIPPED(*s) ? "（已装备）" : "");
			text_line(line, rx + 8, ry + 4, sel ? CLR_ACCENT : CLR_TX);
			snprintf(line, sizeof(line), "%d 文", item_sell_price(id));
			ui_text_right(line, rx + rw - 8, ry + 4, CLR_ACCENT);
		}
	}

	if (rowCount == 0)
	{
		text_line(ui->shopTab == 0 ? "本店没有存货。" : "行囊里没有可卖的东西。",
		          16, 100, CLR_DIM);
	}

	if (ui_notice_fresh())
		ui_text_fitted(log_at(0), 8, 198, 302, CLR_DIM);

	snprintf(line, sizeof(line), "%d/%d", page + 1, pages);
	ui_text_right(line, 232, 218, CLR_DIM);

	static const char *const BTN[UI_SHOP_BTN_COUNT] = { "上页", "下页", "返回" };
	for (int i = 0; i < UI_SHOP_BTN_COUNT; i++)
	{
		int bx, by, bw, bh;
		ui_shop_button_rect(i, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, BTN[i], UI_BTN_NORMAL);
	}
}

/* ------------------------------------------------------------------ skills */

static void draw_skills(const UiState *ui, const Player *p)
{
	char line[128];
	int list[SKILL_SLOTS];

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("技能");
	{
		int pfs[24];
		int perfCount = skill_perform_list(p, pfs, 24);
		if (perfCount > 0)
		{
			snprintf(line, sizeof(line), "绝招 %d", perfCount);
			ui_text_right(line, 310, 3, CLR_DIM);
		}
	}

	/* 技能 lists learned kungfu only; 练功/内力 moved to 功能. */
	int count = skill_learned_list(p, list, SKILL_SLOTS);
	int scroll = ui->skillScroll;
	int sel = ui->skillSel;
	if (scroll > count - 8)
		scroll = count > 8 ? count - 8 : 0;
	if (scroll < 0)
		scroll = 0;
	if (sel >= count)
		sel = count > 0 ? count - 1 : 0;

	if (count == 0)
		text_line("还没学会任何功夫。", 12, 40, CLR_DIM);
	for (int i = 0; i < 8 && scroll + i < count; i++)
	{
		int index = scroll + i;
		const SkillSlot *s = &p->skills[list[index]];
		const SkillDef *d = skill_get(s->id);
		int x, y, w, h;
		ui_skill_row_rect(i, &x, &y, &w, &h);
		bool rowSel = (index == sel);
		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)(h - 1),
		                  rowSel ? CLR_SELBG : CLR_PANEL);
		if (rowSel)
			ui_frame(x, y, w, h, CLR_ACCENT);

		const char *type = "？";
		if (d)
		{
			switch (d->type)
			{
			case SKILL_TYPE_FORCE: type = "内功"; break;
			case SKILL_TYPE_HAND: type = "拳脚"; break;
			case SKILL_TYPE_WEAPON: type = "兵器"; break;
			case SKILL_TYPE_DODGE: type = "轻功"; break;
			case SKILL_TYPE_PARRY: type = "招架"; break;
			default: type = "其他"; break;
			}
		}
		snprintf(line, sizeof(line), "%s %s Lv%d 熟练 %u/%d",
		         skill_name(s->id), type, s->level, s->exp,
		         (s->level + 1) * (s->level + 1));
		text_line(line, x + 6, y + 2, rowSel ? CLR_ACCENT : CLR_TX);
		if (d && d->type < SKILL_USEKF_SLOTS)
		{
			/* Enabled = slot carries the 0x80 bit AND points at this row.
			 * skill_usekf_id() alone would false-positive on id 0 (基本内功). */
			u8 slot = p->usekf[d->type];
			if ((slot & 0x80) && (int)(slot & 0x7F) == s->id)
				ui_badge(x + w - 42, y + 2, 36, 16, "启用", CLR_ACCENT, CLR_BG);
		}
	}

	C2D_DrawRectSolid(8, 192, 0.0f, 304, 1, CLR_BORDER);
	snprintf(line, sizeof(line), "经验 %lu   潜能 %u",
	         (unsigned long)p->exp, p->potential);
	text_line(line, 8, 198, CLR_DIM);
	{
		int x, y, w, h;
		ui_back_rect(&x, &y, &w, &h);
		ui_button(x, y, w, h, "返回", UI_BTN_NORMAL);
	}
}

/* ------------------------------------------------------------------ train */

static void draw_train(const UiState *ui, const Player *p)
{
	char line[128];
	const TrainUi *t = &ui->train;

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	const char *title = t->kind == TRAIN_KIND_PRACTICE ? "练功"
	                    : t->kind == TRAIN_KIND_DAZUO  ? "打坐"
	                    : t->kind == TRAIN_KIND_BOOK   ? "读书"
	                                                   : "学艺";
	ui_header(title);

	if (t->kind == TRAIN_KIND_LEARN)
	{
		const NpcInfo *n = npc_info(t->masterNpc);
		snprintf(line, sizeof(line), "师父：%s", n ? n->name : "?");
		ui_text_right(line, 310, 3, CLR_DIM);
	}
	else if (t->kind == TRAIN_KIND_BOOK)
	{
		snprintf(line, sizeof(line), "秘籍：%s", item_name(t->bookId));
		ui_text_right(line, 310, 3, CLR_DIM);
	}
	else
	{
		snprintf(line, sizeof(line), "潜能 %u", p->potential);
		ui_text_right(line, 310, 3, CLR_DIM);
	}

	if (t->phase == TRAIN_PHASE_SELECT)
	{
		for (int i = 0; i < 8; i++)
		{
			int index = t->scroll + i;
			if (index >= t->listCount)
				break;
			int x, y, w, h;
			ui_train_row_rect(i, &x, &y, &w, &h);
			bool sel = (index == t->sel);
			C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)(h - 1),
			                  sel ? CLR_SELBG : CLR_PANEL);
			if (sel)
				ui_frame(x, y, w, h, CLR_ACCENT);
			if (t->kind == TRAIN_KIND_LEARN || t->kind == TRAIN_KIND_BOOK)
			{
				snprintf(line, sizeof(line), "「%s」  我 Lv%d  上限 Lv%d",
				         skill_name(t->list[index]), skill_level(p, t->list[index]),
				         t->listLv[index]);
			}
			else
			{
				snprintf(line, sizeof(line), "「%s」  Lv%d  熟练 %u",
				         skill_name(t->list[index]), skill_level(p, t->list[index]),
				         skill_exp(p, t->list[index]));
			}
			text_line(line, x + 6, y + 2, sel ? CLR_ACCENT : CLR_TX);
		}
		int bx, by, bw, bh;
		ui_train_button_rect(1, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "返回", UI_BTN_NORMAL);
		return;
	}

	/* Run phase. */
	int kf = t->kf;
	int lvl = skill_level(p, kf);
	int pot = skill_exp(p, kf);
	int need = (lvl + 1) * (lvl + 1);

	snprintf(line, sizeof(line), "「%s」  Lv%d", skill_name(kf), lvl);
	text_line(line, 8, 28, CLR_ACCENT);

	ui_bar(8, 52, 240, 10, pot, need, CLR_BAR);
	snprintf(line, sizeof(line), "熟练 %d/%d", pot, need);
	text_line(line, 8, 66, CLR_TX);

	if (t->kind == TRAIN_KIND_DAZUO)
	{
		ui_bar(8, 86, 240, 10, p->mp, p->mpMax * 2, CLR_MP);
		snprintf(line, sizeof(line), "内力 %d/%d",
		         (int)p->mp, (int)p->mpMax);
		text_line(line, 8, 100, CLR_TX);
		snprintf(line, sizeof(line), "体力 %d/%d", (int)p->hp, (int)p->effhp);
		text_line(line, 8, 118, CLR_DIM);
	}

	snprintf(line, sizeof(line), "第 %d 轮   %s", t->rounds,
	         t->running ? "进行中" : "已停止");
	text_line(line, 8, 138, t->running ? CLR_ACCENT : CLR_DIM);

	/* Message strip: panel with a frame so the log line reads as feedback. */
	ui_panel(8, 158, 304, 48);
	if (t->msg[0])
		text_line(t->msg, 16, 174, CLR_TX);
	else
		text_line("……", 16, 174, CLR_DIM);

	int bx, by, bw, bh;
	ui_train_button_rect(0, &bx, &by, &bw, &bh);
	ui_button(bx, by, bw, bh, t->running ? "停止" : "继续",
	          t->running ? UI_BTN_SELECTED : UI_BTN_NORMAL);
	ui_train_button_rect(1, &bx, &by, &bw, &bh);
	ui_button(bx, by, bw, bh, "返回", UI_BTN_NORMAL);
}

/* ----------------------------------------------------------------- neili */

/* M5.5 内功页: 打坐 / 加力 / 吸气 / 疗伤 (game.s neili_menu).
 * ui->neiliSel picks the row; the 加力 stepper state lives in main.c
 * and is passed through ui->train.msg-free statics — drawn from p->force. */
static void draw_neili(const UiState *ui, const Player *p)
{
	char line[128];
	bool jialiEdit = ui->jialiEdit;
	int jialiVal = ui->jialiVal;
	int jialiMax = neili_jiali_max(p);

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	ui_header("内功");
	{
		int kfId = skill_usekf_id(p, SKILL_TYPE_FORCE);
		if (kfId > 0)
			snprintf(line, sizeof(line), "运行 %s", skill_name(kfId));
		else
			snprintf(line, sizeof(line), "未运转内功");
		text_line(line, 60, 3, CLR_DIM);
	}

	static const char *const LABELS[4] = { "打坐", "加力", "吸气", "疗伤" };
	int forceLv = 0;
	{
		int kfId = skill_usekf_id(p, SKILL_TYPE_FORCE);
		if (kfId > 0)
			forceLv = skill_kf_effective_level(p, kfId);
	}
	for (int i = 0; i < 4; i++)
	{
		int x, y, w, h;
		ui_neili_row_rect(i, &x, &y, &w, &h);
		bool sel = (i == ui->neiliSel && !jialiEdit);
		u32 value = CLR_DIM;
		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)(h - 1),
		                  sel ? CLR_SELBG : CLR_PANEL);
		if (sel)
			ui_frame(x, y, w, h - 1, CLR_ACCENT);
		text_line(LABELS[i], x + 10, y + 8, sel ? CLR_ACCENT : CLR_TX);

		switch (i)
		{
		case 0:
			snprintf(line, sizeof(line), "内力 %d/%d", (int)p->mp, (int)p->mpMax);
			break;
		case 1:
			snprintf(line, sizeof(line), "当前 %d", (int)p->force);
			value = p->force > 0 ? CLR_ACCENT : CLR_DIM;
			break;
		case 2:
			snprintf(line, sizeof(line), "体力 %d/%d", (int)p->hp, (int)p->effhp);
			break;
		default:
			snprintf(line, sizeof(line), "内功 %d", forceLv);
			value = forceLv >= 45 ? CLR_TX : CLR_DANGER;
			break;
		}
		ui_text_right(line, x + w - 10, y + 8, value);
	}

	if (jialiEdit)
	{
		ui_panel(8, 176, 304, 52);
		int bx, by, bw, bh;
		ui_neili_edit_rect(0, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "−", UI_BTN_NORMAL);
		ui_neili_edit_rect(1, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "＋", UI_BTN_NORMAL);
		ui_neili_edit_rect(2, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "确认", UI_BTN_SELECTED);
		ui_neili_edit_rect(3, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "取消", UI_BTN_NORMAL);
		snprintf(line, sizeof(line), "%d / %d", jialiVal, jialiMax);
		int vw = text_width(line, 1);
		text_draw(line, 52 + (80 - vw) / 2, 194, 1, CLR_ACCENT);
	}
	else
	{
		int bx, by, bw, bh;
		ui_neili_edit_rect(3, &bx, &by, &bw, &bh);
		ui_button(bx, by, bw, bh, "返回", UI_BTN_NORMAL);
	}
}

/* -------------------------------------------------------------- charcreate */

/* input.s:365-389 init_attr draws a framed panel with four stat rows. Only
 * those four are adjustable: 相貌/福缘 are rolled in input_new afterwards. */
static void draw_charcreate(const UiState *ui)
{
	const CharCreate *cc = &ui->cc;
	char line[96];

	ui_header("创建角色");

	/* input.s input_name: the name is asked for before adjust_attr.  Tap
	 * the row or press X to open the system keyboard. */
	int nx, ny, nw, nh;
	ui_cc_name_rect(&nx, &ny, &nw, &nh);
	C2D_DrawRectSolid((float)nx, (float)ny, 0.0f, (float)nw, (float)nh, CLR_PANEL);
	ui_frame(nx, ny, nw, nh, CLR_BORDER);
	text_line("姓名", nx + 8, ny + 7, CLR_DIM);
	text_line(cc->name, nx + 60, ny + 7, CLR_TX);
	ui_button(nx + nw - 68, ny + 2, 58, nh - 4, "改名", UI_BTN_NORMAL);

	snprintf(line, sizeof(line), "可分配点数 %d", cc->spare);
	text_line(line, 10, 58, cc->spare ? CLR_ACCENT : CLR_TX);
	snprintf(line, sizeof(line), "性别 %s",
	         cc_gender_name(cc_gender_from_str(cc->attr[CC_ATTR_STR])));
	ui_text_right(line, 310, 58, CLR_TX);

	for (int i = 0; i < CC_ATTR_COUNT; i++)
	{
		int rx, ry, rw, rh;
		ui_cc_row_rect(i, &rx, &ry, &rw, &rh);
		bool selected = cc->pointer == i + 1;
		C2D_DrawRectSolid((float)rx, (float)ry, 0.0f, (float)rw, (float)rh,
		                  selected ? CLR_SELBG : CLR_PANEL);
		ui_frame(rx, ry, rw, rh, selected ? CLR_ACCENT : CLR_BORDER);
		text_line(cc_attr_name(i), rx + 8, ry + 7,
		          selected ? CLR_ACCENT : CLR_DIM);
		snprintf(line, sizeof(line), "%d", cc->attr[i]);
		text_line(line, rx + 72, ry + 7, CLR_TX);

		int sx, sy, sw, sh;
		ui_cc_step_rect(i, 0, &sx, &sy, &sw, &sh);
		ui_button(sx, sy, sw, sh, "-",
		          cc->attr[i] <= CC_ATTR_MIN ? UI_BTN_DISABLED : UI_BTN_NORMAL);
		ui_cc_step_rect(i, 1, &sx, &sy, &sw, &sh);
		ui_button(sx, sy, sw, sh, "+",
		          (cc->spare == 0 || cc->attr[i] >= CC_ATTR_MAX)
		              ? UI_BTN_DISABLED : UI_BTN_NORMAL);
	}

	if (cc->notice == CC_NOTICE_RESET)
		text_line("已恢复初值", 10, 198, CLR_DIM);

	const char *labels[UI_CC_BTN_COUNT] = { "确认", "重来", NULL };
	labels[UI_CC_BTN_CANCEL] = cc->fromGameOver ? "返回" : "退出";
	for (int i = 0; i < UI_CC_BTN_COUNT; i++)
	{
		int bx, by, bw, bh;
		ui_cc_button_rect(i, &bx, &by, &bw, &bh);
		int state = UI_BTN_NORMAL;
		if (i == UI_CC_BTN_CONFIRM)
			state = cc->spare == 0 ? UI_BTN_SELECTED : UI_BTN_DISABLED;
		ui_button(bx, by, bw, bh, labels[i], state);
	}
}

/* --------------------------------------------------------------------- api */

void ui_bottom_draw(const UiState *ui, const Player *p, const GameMap *m,
                    u32 playSeconds, const Dialog *dlg,
                    const SaveSlotInfo slots[SAVE_SLOT_COUNT],
                    const Battle *battle)
{
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);

	switch (ui->page)
	{
	case UI_PAGE_FIELD:
		draw_field(p, m, playSeconds);
		break;
	case UI_PAGE_LOOK:
		draw_look(ui, p);
		break;
	case UI_PAGE_MENU:
		draw_menu(ui);
		break;
	case UI_PAGE_SYSTEM:
		draw_system(ui);
		break;
	case UI_PAGE_SETTINGS:
		draw_settings(ui);
		break;
	case UI_PAGE_QUIT:
		draw_quit(ui);
		break;
	case UI_PAGE_SAVE:
		draw_save(ui, slots);
		break;
	case UI_PAGE_DIALOG:
		if (dlg)
			dialog_draw(dlg, DIALOG_PANEL_X, DIALOG_PANEL_Y,
			            DIALOG_PANEL_W, DIALOG_PANEL_H);
		break;
	case UI_PAGE_BAG:
		draw_bag(ui, p);
		break;
	case UI_PAGE_SHOP:
		draw_shop(ui, p);
		break;
	case UI_PAGE_BATTLE:
		if (battle && battle->active)
			battle_draw_bottom(battle, p);
		break;
	case UI_PAGE_SKILLS:
		draw_skills(ui, p);
		break;
	case UI_PAGE_TRAIN:
		draw_train(ui, p);
		break;
	case UI_PAGE_NEILI:
		draw_neili(ui, p);
		break;
	case UI_PAGE_CHARCREATE:
		draw_charcreate(ui);
		break;
	case UI_PAGE_TITLE:
		draw_title(ui);
		break;
	default:
		draw_field(p, m, playSeconds);
		break;
	}

	if (ui->wipe < 1.0f)
	{
		float cover = 320.0f * (1.0f - ui->wipe);
		C2D_DrawRectSolid(0, 0, 0.0f, cover, 240, CLR_BG);
		C2D_DrawRectSolid(cover, 0, 0.0f, 2.0f, 240, CLR_ACCENT);
	}

	/* Automation anchors: opposite corners of the bottom screen. */
	C2D_DrawRectSolid(0, 0, 0.0f, 3, 3, CLR_ANCHOR);
	C2D_DrawRectSolid(317, 237, 0.0f, 3, 3, CLR_ANCHOR);
}

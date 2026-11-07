#ifndef UI_H
#define UI_H

#include "actor.h"
#include "battle.h"
#include "charcreate.h"
#include "dialog.h"
#include "gmud.h"
#include "save.h"

/* Bottom-screen pages. The field page is a map/vitals dashboard; the
 * original WQX build has no persistent stat panel, so 状态 lives under 查看. */
enum
{
	UI_PAGE_FIELD = 0,
	UI_PAGE_LOOK,     /* 查看: 状态 / 描述 / 属性 */
	UI_PAGE_MENU,     /* 查看 / 物品 / 技能 / 功能 / 设置 */
	UI_PAGE_SYSTEM,   /* 功能: 内力 / 练功 / 返回 */
	UI_PAGE_SETTINGS, /* 设置: 主题 / 存档 / 结束 / 返回 */
	UI_PAGE_QUIT,
	UI_PAGE_SAVE,
	UI_PAGE_DIALOG,
	UI_PAGE_BAG,
	UI_PAGE_SHOP,
	UI_PAGE_BATTLE,
	UI_PAGE_SKILLS,
	UI_PAGE_TRAIN,
	UI_PAGE_NEILI,
	UI_PAGE_CHARCREATE,  /* input.s adjust_attr: four innate stats */
	UI_PAGE_TITLE,       /* 3DS port addition: boot menu with 继续游戏 */
	UI_PAGE_COUNT
};

/* 查看 tabs (game.s look_menu). */
enum
{
	UI_LOOK_STATUS = 0,
	UI_LOOK_DESC,
	UI_LOOK_ATTR,
	UI_LOOK_TAB_COUNT
};

/* 功能 rows: the in-game actions only (game.s sys_menu's 内力 / 练功).
 * 存档 / 主题 / 结束 moved to the 设置 menu below so the 功能 page no longer
 * mixes 打坐 with shell options. */
enum
{
	UI_SYSTEM_NEILI = 0,
	UI_SYSTEM_PRACTICE,
	UI_SYSTEM_BACK,
	UI_SYSTEM_COUNT
};

/* 设置 rows (3DS port addition). */
enum
{
	UI_SETTINGS_THEME = 0,
	UI_SETTINGS_SAVE,
	UI_SETTINGS_QUIT,
	UI_SETTINGS_BACK,
	UI_SETTINGS_COUNT
};

enum
{
	UI_QUIT_NO = 0,
	UI_QUIT_YES,
	UI_QUIT_COUNT
};

/* Title screen rows (3DS port addition). */
enum
{
	UI_TITLE_CONTINUE = 0,
	UI_TITLE_NEW,
	UI_TITLE_LOAD,
	UI_TITLE_QUIT,
	UI_TITLE_COUNT
};

enum
{
	UI_CC_BTN_CONFIRM = 0,
	UI_CC_BTN_RESET,
	UI_CC_BTN_CANCEL,
	UI_CC_BTN_COUNT
};

enum
{
	TRAIN_KIND_PRACTICE = 0,   /* 练功: pick a learned kungfu */
	TRAIN_KIND_DAZUO,          /* 打坐: meditate on the force */
	TRAIN_KIND_LEARN,          /* 学艺: learn from the master */
	TRAIN_KIND_BOOK            /* 读书: learn from a goods.dat book table */
};

enum
{
	TRAIN_PHASE_SELECT = 0,
	TRAIN_PHASE_RUN
};

#define TRAIN_LIST_MAX SKILL_SLOTS

typedef struct
{
	int kind;
	int phase;
	int sel;
	int scroll;
	int kf;                    /* kungfu being trained */
	int masterNpc;             /* teacher for TRAIN_KIND_LEARN */
	int bookId;                /* goods id for TRAIN_KIND_BOOK */
	bool running;
	float acc;                 /* tick accumulator: 0.4 s / 0.004 s */
	int rounds;
	/* Long enough for the tick message plus the "（继续学习吗？A 继续）"
	 * suffix that main.c appends after every level-up. */
	char msg[160];
	int list[TRAIN_LIST_MAX];      /* selectable kf ids */
	int listLv[TRAIN_LIST_MAX];    /* master level for LEARN rows */
	int listCount;
} TrainUi;

#define UI_MENU_ITEMS 5
#define UI_SHOP_ROWS 6
/* Rows on one page of the 行囊 list.  Five rows end at y=138, right above the
 * detail sheet (y=140), so the sheet never hides a row or swallows its tap. */
#define UI_BAG_ROWS 5

/* Shared button states for ui_button(). */
enum
{
	UI_BTN_NORMAL = 0,
	UI_BTN_SELECTED,
	UI_BTN_DISABLED
};

/* Shared widget helpers (implemented in ui_widgets.c). All pages read
 * colours through ui_col() so the UI follows the Select theme. */
u32 ui_col(int slot);
void ui_header(const char *title);
void ui_more(int cx, int y, u32 color);
void ui_text_fitted(const char *s, int x, int y, int w, u32 color);
bool ui_notice_fresh(void);
/* Short-lived world message (panel) over the map: the text is wrapped over
 * several lines and the panel grows upward from `bottom`. */
void ui_notice(int x, int bottom, int w);
void ui_text_right(const char *s, int right, int y, u32 color);
void ui_frame(int x, int y, int w, int h, u32 color);
void ui_panel(int x, int y, int w, int h);
void ui_button(int x, int y, int w, int h, const char *label, int state);
void ui_bar(int x, int y, int w, int h, int value, int max, u32 fill);
void ui_badge(int x, int y, int w, int h, const char *label, u32 bg, u32 fg);

enum
{
	UI_SAVE_BTN_SAVE = 0,
	UI_SAVE_BTN_LOAD,
	UI_SAVE_BTN_BACK,
	UI_SAVE_BTN_COUNT
};

enum
{
	UI_FIELD_BTN_INTERACT = 0,
	UI_FIELD_BTN_MENU,
	UI_FIELD_BTN_COUNT
};

enum
{
	UI_BAG_ACT_USE = 0,
	UI_BAG_ACT_EQUIP,
	UI_BAG_ACT_DROP,
	UI_BAG_ACT_BACK,
	UI_BAG_ACT_COUNT
};

enum
{
	UI_SHOP_BTN_PREV = 0,
	UI_SHOP_BTN_NEXT,
	UI_SHOP_BTN_BACK,
	UI_SHOP_BTN_COUNT
};

typedef struct
{
	int page;
	int menuIndex;
	int lookTab;
	int systemSel;
	int settingsSel;  /* 设置 page row */
	int quitSel;
	int saveSlot;     /* 0..2 manual, SAVE_SLOT_AUTO auto */
	int titleSel;    /* title screen row */
	bool hasContinue;/* a save exists: 继续游戏 is offered on the title screen */
	float wipe;      /* 0..1 reveal progress of the page switch */

	int bagSel;      /* bag slot with an open detail sheet, -1 = none */
	int bagAct;      /* highlighted detail-sheet button, -1 = none */
	int bagPage;     /* first 行囊 row shown (UI_BAG_ROWS per page) */
	int shopNpc;     /* npc id of the current merchant */
	int shopTab;     /* 0 = buy, 1 = sell */
	int shopPage;    /* first 买卖 row shown (UI_SHOP_ROWS per page) */
	int shopSel;     /* highlighted 买卖 row, -1 = none */
	int skillScroll; /* first skill row on the 技能 page */
	int skillSel;    /* selected skill row on the 技能 page */
	int neiliSel;    /* selected row on the 内功 page */
	bool jialiEdit;  /* 内功页 加力 stepper open */
	int jialiVal;    /* stepper value while editing */
	TrainUi train;   /* M5 修炼页 state, driven by main.c */
	CharCreate cc;   /* T5 创建页 state, reinitialised before each new run */
} UiState;

void ui_init(UiState *ui);
void ui_set_page(UiState *ui, int page);
void ui_update(UiState *ui, float dt);

/* "子时"..."亥时" cosmetic badge: 1 shi = 2 h of real play time. */
const char *ui_shi_name(u32 playSeconds);
const char *ui_menu_label(int index);

/* Touch hit-test rectangles, shared with the draw code. */
void ui_menu_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_look_tab_rect(int index, int *x, int *y, int *w, int *h);
void ui_system_row_rect(int index, int *x, int *y, int *w, int *h);
/* 设置页: 主题 / 存档 / 结束 / 返回. */
void ui_settings_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_quit_button_rect(int index, int *x, int *y, int *w, int *h);
void ui_save_slot_rect(int index, int *x, int *y, int *w, int *h);
void ui_save_button_rect(int index, int *x, int *y, int *w, int *h);
void ui_field_button_rect(int index, int *x, int *y, int *w, int *h);
void ui_back_rect(int *x, int *y, int *w, int *h);
/* 行囊: one row per filled slot, 5 rows per page. */
void ui_bag_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_bag_action_rect(int index, int *x, int *y, int *w, int *h);
void ui_bag_page_rect(int index, int *x, int *y, int *w, int *h);
void ui_bag_back_rect(int *x, int *y, int *w, int *h);
/* Detail sheet parked below the last bag row: taps on it must not fall
 * through to the page buttons underneath. */
void ui_bag_sheet_rect(int *x, int *y, int *w, int *h);
void ui_shop_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_shop_button_rect(int index, int *x, int *y, int *w, int *h);
void ui_skill_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_train_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_train_button_rect(int index, int *x, int *y, int *w, int *h);
void ui_neili_row_rect(int index, int *x, int *y, int *w, int *h);
/* 内力页加力步进器：− / ＋ / 确认 / 取消；第 4 格在非步进状态复用为「返回」。 */
void ui_neili_edit_rect(int index, int *x, int *y, int *w, int *h);
/* 创建页: 姓名行 + 4 属性行（每行 - / + 步进）+ 3 个底部按钮。 */
void ui_cc_name_rect(int *x, int *y, int *w, int *h);
void ui_cc_row_rect(int index, int *x, int *y, int *w, int *h);
void ui_cc_step_rect(int index, int which, int *x, int *y, int *w, int *h);
void ui_cc_button_rect(int index, int *x, int *y, int *w, int *h);
/* 标题页: 继续游戏 / 新的旅程 / 读取存档 / 退出游戏. */
void ui_title_row_rect(int index, int *x, int *y, int *w, int *h);

void ui_top_draw(const GameMap *m, int cam_x, int cam_y,
                 const Player *p, u32 playSeconds);
/* T5 creation page is a standalone screen: the world must not be visible
 * while the player is still distributing stats (input.s adjust_attr). */
void ui_top_charcreate_draw(const CharCreate *cc);
/* Title card shown while the boot menu is up. */
void ui_top_title_draw(void);
/* Black overlay used for the map-change wipe (alpha 0..1). */
void ui_top_fade(float amount);

/* Slots are ordered 0..2 manual then SAVE_SLOT_AUTO; the 存档 page shows all
 * four, the manual three are writable and the auto row is read-only. */
void ui_bottom_draw(const UiState *ui, const Player *p, const GameMap *m,
                    u32 playSeconds, const Dialog *dlg,
                    const SaveSlotInfo slots[SAVE_SLOT_COUNT],
                    const Battle *battle);

/* --------------------------------------------------------------- toasts ---
 * Short, self-cancelling messages (rewards, level up).  They are queued,
 * shown one at a time and drawn over the top screen. */
void ui_toast(const char *text);
void ui_toast_tick(float dt);
void ui_toast_draw(void);

#endif

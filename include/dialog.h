#ifndef DIALOG_H
#define DIALOG_H

#include "gmud.h"

#define DIALOG_TEXT_W 304
#define DIALOG_WRAP_BYTES 1024
/* The dialog panel fills the bottom screen. The runtime sizes its text area
 * from these numbers, so every draw / hit-test call must use the same rect. */
#define DIALOG_PANEL_X 4
#define DIALOG_PANEL_Y 4
#define DIALOG_PANEL_W 312
#define DIALOG_PANEL_H 232

typedef void (*DialogEffectFn)(int moneyDelta, const char *message);
/* `option` is the index of the chosen option in the current node. */
typedef void (*DialogActionFn)(int action, int option);

enum
{
	DLG_ACT_NONE = 0,
	DLG_ACT_SHOP_BUY,
	DLG_ACT_SHOP_SELL,
	DLG_ACT_FIGHT,
	DLG_ACT_JOIN,     /* M5: apprentice to this master */
	DLG_ACT_LEARN,    /* M5: open the learn-from-master list */
	DLG_ACT_QUEST,       /* M5: hand over the trade quest goods */
	DLG_ACT_TALK,
	DLG_ACT_LOOK,
	DLG_ACT_QUEST_GIVE,  /* M6: hand over the 寻物 task item */
	DLG_ACT_QUEST_DECLINE, /* M6: decline the hand-over */
	DLG_ACT_FINAL_FIGHT, /* M6: start the six-slab final battle */
	DLG_ACT_SUICIDE,     /* talk.s do_suicide: delete the current save */
	DLG_ACT_BOOK_YES1,   /* skill.s study_cmd: 菜花宝典 first Y/N */
	DLG_ACT_BOOK_YES2,   /* skill.s study_cmd: 菜花宝典 second Y/N */
	DLG_ACT_BOOK_NO,
	DLG_ACT_QUIT_SAVE,   /* game.s end_game: "现在保存吗?" */
	DLG_ACT_QUIT_NOSAVE,
	DLG_ACT_ARENA        /* 擂台 (nf.s netfight): sparring match */
};

typedef struct
{
	const char *speaker;      /* NULL = narration */
	const char *text;         /* UTF-8 */
	u8 optionCount;           /* 0..4 */
	const char *options[4];
	s16 next[4];              /* node index per option, -1 = end */
	s16 money[4];             /* money delta applied when an option is chosen */
	s16 after;                /* next node for plain text, -1 = end */
	u8 optAction[4];          /* DLG_ACT_* fired when an option is chosen */
} DialogNode;

typedef struct
{
	const DialogNode *nodes;
	int count;
} DialogScript;

typedef struct
{
	const DialogScript *script;
	int node;
	float reveal;
	int revealed;
	int total;
	int selected;
	bool done;
	int lineCount;      /* wrapped lines of the current node */
	int viewLines;      /* lines that fit into the panel */
	int scroll;         /* first visible line (text longer than the panel) */
	char wrapped[DIALOG_WRAP_BYTES];
} Dialog;

/* Three fixed M2 scripts plus a generic fallback, keyed by the map NPC id. */
const DialogScript *dialog_script_for(int npcId);

/* Runtime-built merchant script: greeting + buy/sell options.
 * name/text must stay valid while the dialog is open (npcs.bin strings). */
const DialogScript *dialog_merchant_script(const char *name, const char *text);

/* Hostile NPC: 动手 starts a battle when the dialog closes. */
const DialogScript *dialog_hostile_script(const char *name);

void dialog_set_effect(DialogEffectFn fn);
void dialog_set_action(DialogActionFn fn);
void dialog_start(Dialog *d, const DialogScript *s);
void dialog_update(Dialog *d, float dt);
void dialog_input(Dialog *d, u32 down);
void dialog_draw(const Dialog *d, int x, int y, int w, int h);

/* Returns the option index under (px,py) for the given dialog draw rect, or -1. */
int dialog_option_at(const Dialog *d, int x, int y, int w, int h, int px, int py);

/* Option count of the current node, or 0 when options are not shown yet. */
int dialog_option_count(const Dialog *d);

#endif

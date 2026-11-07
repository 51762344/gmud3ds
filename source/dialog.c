#include "dialog.h"

#include "gfx.h"
#include "text.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define REVEAL_CPS 45.0f
/* Longest wrapped text the dialog keeps; a longer text is cut with "……". */
#define WRAP_MAX_LINES 16

/* Text area geometry inside the panel (see dialog_draw). */
#define TEXT_PAD_X   8
#define TEXT_PAD_TOP 8
#define SPEAKER_H    22
#define MORE_H       14
#define OPTION_H     24
#define OPTION_GAP   12

/* Themed palette; the dialog keeps a slightly darker body via BARBG. */
#define CLR_PANEL   ui_col(UI_COL_PANEL)
#define CLR_BODY    ui_col(UI_COL_BARBG)
#define CLR_FRAME   ui_col(UI_COL_BORDER)
#define CLR_TEXT    ui_col(UI_COL_TEXT)
#define CLR_SPEAKER ui_col(UI_COL_ACCENT)
#define CLR_HINT    ui_col(UI_COL_DIM)
#define CLR_SELECT  ui_col(UI_COL_ACCENT)

/* --------------------------------------------------------------- scripts */

static const DialogNode AQING_NODES[] =
{
	{
		"潘小莲", "这位客官也是来吃豆腐的么？\n我的豆腐又白又嫩，你不想尝尝？",
		3, { "来一斤白豆腐", "随便看看", "改日再来" },
		{ 1, 2, 3 }, { -10, 0, 0 }, -1
	},
	{ "潘小莲", "好嘞！十文钱，您拿好。\n趁热吃才香。", 0, { 0 }, { 0 }, { 0 }, -1 },
	{ "潘小莲", "不买也无妨，常来坐坐。", 0, { 0 }, { 0 }, { 0 }, -1 },
	{ "潘小莲", "客官慢走，下次再来。", 0, { 0 }, { 0 }, { 0 }, -1 },
};

static const DialogNode BOY_NODES[] =
{
	{ "小顽童", "你也是来找我玩的吗？", 0, { 0 }, { 0 }, { 0 }, 1 },
	{ "小顽童", "听说镇东的武馆在收徒弟，\n可我娘不让我去。", 0, { 0 }, { 0 }, { 0 }, -1 },
};

static const DialogNode BUKUAI_NODES[] =
{
	{
		"捕快", "站住！你可曾在镇上\n见过可疑之人？",
		2, { "没有", "见过一个黑衣人" },
		{ 1, 2 }, { 0, 0 }, -1
	},
	{ "捕快", "哼，若见到生面孔，\n记得来六扇门报信。", 0, { 0 }, { 0 }, { 0 }, -1 },
	{ "捕快", "黑衣人？说清楚些！\n……罢了，你先去吧。", 0, { 0 }, { 0 }, { 0 }, -1 },
};

static const DialogNode FALLBACK_NODES[] =
{
	{ NULL, "这人似乎不想多说什么。", 0, { 0 }, { 0 }, { 0 }, -1 },
};

static const DialogScript AQING = { AQING_NODES, 4 };
static const DialogScript BOY = { BOY_NODES, 2 };
static const DialogScript BUKUAI = { BUKUAI_NODES, 3 };
static const DialogScript FALLBACK = { FALLBACK_NODES, 1 };

static DialogEffectFn s_effect;
static DialogActionFn s_action;

static const char *const MERCHANT_OPTIONS[3] = { "看看货物", "卖出东西", "改日再来" };
static DialogNode s_merchantNode;
static DialogScript s_merchantScript;
static DialogNode s_hostileNode;
static DialogScript s_hostileScript;
static char s_hostileText[96];

static const char *const HOSTILE_OPTIONS[2] = { "动手", "改日再来" };

const DialogScript *dialog_hostile_script(const char *name)
{
	memset(&s_hostileNode, 0, sizeof(s_hostileNode));
	snprintf(s_hostileText, sizeof(s_hostileText), "%s 拦住了去路，似乎想试试你的身手。",
	         name ? name : "这人");
	s_hostileNode.speaker = name;
	s_hostileNode.text = s_hostileText;
	s_hostileNode.optionCount = 2;
	for (int i = 0; i < 2; i++)
	{
		s_hostileNode.options[i] = HOSTILE_OPTIONS[i];
		s_hostileNode.next[i] = -1;
		s_hostileNode.money[i] = 0;
		s_hostileNode.optAction[i] = 0;
	}
	s_hostileNode.optAction[0] = DLG_ACT_FIGHT;
	s_hostileNode.after = -1;
	s_hostileScript.nodes = &s_hostileNode;
	s_hostileScript.count = 1;
	return &s_hostileScript;
}

const DialogScript *dialog_merchant_script(const char *name, const char *text)
{
	memset(&s_merchantNode, 0, sizeof(s_merchantNode));
	s_merchantNode.speaker = name;
	s_merchantNode.text = (text && *text) ? text : "客官要点什么？";
	s_merchantNode.optionCount = 3;
	for (int i = 0; i < 3; i++)
	{
		s_merchantNode.options[i] = MERCHANT_OPTIONS[i];
		s_merchantNode.next[i] = -1;
		s_merchantNode.money[i] = 0;
		s_merchantNode.optAction[i] = 0;
	}
	s_merchantNode.optAction[0] = DLG_ACT_SHOP_BUY;
	s_merchantNode.optAction[1] = DLG_ACT_SHOP_SELL;
	s_merchantNode.after = -1;

	s_merchantScript.nodes = &s_merchantNode;
	s_merchantScript.count = 1;
	return &s_merchantScript;
}

const DialogScript *dialog_script_for(int npcId)
{
	switch (npcId)
	{
	case 1: return &AQING;
	case 2: return &BOY;
	case 3: return &BUKUAI;
	default: return &FALLBACK;
	}
}

void dialog_set_effect(DialogEffectFn fn)
{
	s_effect = fn;
}

void dialog_set_action(DialogActionFn fn)
{
	s_action = fn;
}

/* --------------------------------------------------------------- runtime */

static const DialogNode *current_node(const Dialog *d)
{
	if (!d->script || d->node < 0 || d->node >= d->script->count)
		return NULL;
	return &d->script->nodes[d->node];
}

static int visible_chars(const char *s)
{
	int n = 0;
	const char *p = s;
	while (*p)
	{
		if (*p == '\n')
			p++;
		else
		{
			text_utf8_next(&p);
			n++;
		}
	}
	return n;
}

/* Text lines that fit between the speaker badge and the bottom furniture
 * (the option rows or the "more" caret). Without options the panel has room
 * for the whole NPC description; with options the rows take the lower half. */
static int text_view_lines(const DialogNode *n)
{
	int lineH = text_line_height(1);
	int top = DIALOG_PANEL_Y + TEXT_PAD_TOP + (n->speaker ? SPEAKER_H : 0);
	int limit;

	if (n->optionCount > 0)
		limit = DIALOG_PANEL_Y + DIALOG_PANEL_H - OPTION_GAP - n->optionCount * OPTION_H - 3;
	else
		limit = DIALOG_PANEL_Y + DIALOG_PANEL_H - MORE_H;

	int lines = (limit - top) / lineH;
	return lines > 1 ? lines : 1;
}

/* Loads the text of the current node into the dialog. Text that does not fit
 * the buffer keeps its head and is marked with "……". */
static void set_node_text(Dialog *d, const DialogNode *n)
{
	d->lineCount = text_wrap_fitted(n->text ? n->text : "", DIALOG_TEXT_W, 1,
	                                d->wrapped, sizeof(d->wrapped),
	                                WRAP_MAX_LINES);
	d->viewLines = text_view_lines(n);
	d->scroll = 0;
	d->total = visible_chars(d->wrapped);
}

void dialog_start(Dialog *d, const DialogScript *s)
{
	memset(d, 0, sizeof(*d));
	d->script = s ? s : &FALLBACK;
	d->node = 0;
	d->done = false;

	const DialogNode *n = current_node(d);
	if (!n)
	{
		d->done = true;
		return;
	}

	set_node_text(d, n);
}

void dialog_update(Dialog *d, float dt)
{
	if (d->done)
		return;
	if (d->revealed < d->total)
	{
		d->reveal += dt * REVEAL_CPS;
		int shown = (int)d->reveal;
		if (shown > d->total)
			shown = d->total;
		d->revealed = shown;
	}
}

static void enter_node(Dialog *d, int index)
{
	const DialogNode *n;
	if (!d->script || index < 0 || index >= d->script->count)
	{
		d->done = true;
		return;
	}

	d->node = index;
	d->reveal = 0.0f;
	d->revealed = 0;
	d->selected = 0;
	n = &d->script->nodes[index];
	set_node_text(d, n);
}

void dialog_input(Dialog *d, u32 down)
{
	if (d->done)
		return;
	const DialogNode *n = current_node(d);
	if (!n)
	{
		d->done = true;
		return;
	}

	/* A/B during the typewriter effect reveals the rest immediately. */
	if (d->revealed < d->total)
	{
		if (down & (KEY_A | KEY_B | KEY_TOUCH))
		{
			d->revealed = d->total;
			d->reveal = (float)d->total;
		}
		return;
	}

	if (n->optionCount > 0)
	{
		if (down & KEY_UP)
			d->selected = (d->selected + n->optionCount - 1) % n->optionCount;
		if (down & KEY_DOWN)
			d->selected = (d->selected + 1) % n->optionCount;

		if (down & KEY_A)
		{
			int sel = d->selected;
			if (n->optAction[sel] != DLG_ACT_NONE && s_action)
				s_action(n->optAction[sel], sel);
			if (s_effect)
				s_effect(n->money[sel], n->options[sel]);
			if (n->next[sel] < 0)
				d->done = true;
			else
				enter_node(d, n->next[sel]);
		}
		else if (down & KEY_B)
		{
			d->done = true;
		}
		return;
	}

	/* Text that does not fit the panel scrolls with the d-pad. */
	if (d->lineCount > d->viewLines)
	{
		int last = d->lineCount - d->viewLines;
		if (down & KEY_UP)
			d->scroll = d->scroll > 0 ? d->scroll - 1 : 0;
		if (down & KEY_DOWN)
			d->scroll = d->scroll < last ? d->scroll + 1 : last;
	}

	if (down & (KEY_A | KEY_B))
	{
		if (n->after < 0)
			d->done = true;
		else
			enter_node(d, n->after);
	}
}

/* Draws wrapped text with the first `revealed` visible codepoints shown.
 * Only the window [scroll, scroll + viewLines) is drawn, so a text longer
 * than the panel can be read line by line. */
static void draw_text_revealed(const Dialog *d, int x, int y)
{
	int remaining = d->revealed;
	const char *p = d->wrapped;
	int line = 0;

	while (*p)
	{
		const char *start = p;
		int chars = 0;
		while (*p && *p != '\n')
		{
			text_utf8_next(&p);
			chars++;
		}

		int draw = remaining;
		if (draw > chars)
			draw = chars;
		if (draw > 0 && line >= d->scroll && line - d->scroll < d->viewLines)
			text_draw_n(start, draw, x, y + (line - d->scroll) * text_line_height(1),
			            1, CLR_TEXT);
		remaining -= chars;
		line++;
		if (remaining <= 0)
			break;

		if (*p == '\n')
			p++;
	}
}

int dialog_option_count(const Dialog *d)
{
	if (!d || d->done)
		return 0;
	const DialogNode *n = current_node(d);
	if (!n || d->revealed < d->total)
		return 0;
	return n->optionCount;
}

int dialog_option_at(const Dialog *d, int x, int y, int w, int h, int px, int py)
{
	if (!d || d->done)
		return -1;
	const DialogNode *n = current_node(d);
	if (!n || n->optionCount == 0 || d->revealed < d->total)
		return -1;

	int oy = y + h - OPTION_GAP - n->optionCount * OPTION_H;
	for (int i = 0; i < n->optionCount; i++)
	{
		int rowY = oy + i * OPTION_H;
		if (px >= x + 8 && px < x + w - 8 && py >= rowY - 3 && py < rowY + 17)
			return i;
	}
	return -1;
}

void dialog_draw(const Dialog *d, int x, int y, int w, int h)
{
	if (!d || d->done)
		return;
	const DialogNode *n = current_node(d);
	if (!n)
		return;

	C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h, CLR_BODY);
	ui_frame(x, y, w, h, CLR_FRAME);

	/* The text origin comes from the shared panel constants: the runtime line
	 * budget (text_view_lines) has to agree with the drawing. */
	int tx = DIALOG_PANEL_X + TEXT_PAD_X;
	int ty = DIALOG_PANEL_Y + TEXT_PAD_TOP;
	if (n->speaker)
	{
		int bw = text_width(n->speaker, 1) + 10;
		ui_badge(tx, ty, bw, 16, n->speaker, CLR_PANEL, CLR_SPEAKER);
		ui_frame(tx, ty, bw, 16, CLR_FRAME);
		ty += SPEAKER_H;
	}
	draw_text_revealed(d, tx, ty);

	bool complete = d->revealed >= d->total;
	if (!complete)
	{
		ui_more(x + w - 16, y + h - MORE_H, CLR_HINT);
		return;
	}

	if (n->optionCount == 0)
	{
		/* Only a node that continues somewhere shows the "more" arrow; the
		 * final node would otherwise advertise text it does not have. */
		if (n->after >= 0)
			ui_more(x + w - 16, y + h - MORE_H, CLR_HINT);
		return;
	}

	int oy = y + h - OPTION_GAP - n->optionCount * OPTION_H;
	for (int i = 0; i < n->optionCount; i++)
	{
		int rowY = oy + i * OPTION_H;
		if (i == d->selected)
		{
			C2D_DrawRectSolid((float)(x + 8), (float)(rowY - 3), 0.0f,
			                  (float)(w - 16), 20.0f, ui_col(UI_COL_SEL));
			ui_frame(x + 8, rowY - 3, w - 16, 20, CLR_SELECT);
			text_draw(">", x + 12, rowY, 1, CLR_SELECT);
			text_draw(n->options[i], x + 28, rowY, 1, CLR_SELECT);
		}
		else
		{
			text_draw(n->options[i], x + 28, rowY, 1, CLR_HINT);
		}
	}
}

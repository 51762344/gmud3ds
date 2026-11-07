/* panyan.s: the mini games behind the 山丘 (HILL2_ID) object.
 *
 * lian_dodge  (跳舞毯): a DDR-like panel. A random arrow is shown, the
 *   player has to press the matching direction key before the window closes.
 *   A hit scores DODGE_BONUS (3) and speeds the arrows up (game_level -= 2
 *   while it is >= 80); a wrong key ends the game at once.
 * lian_unarmed(投铅球): a ball slides left and right over a marked target
 *   strip. The player starts the ball and stops it again; stopping it inside
 *   the target scores PARRY_BONUS (10), a miss costs one of the 7 lives.
 *
 * The high scores (top_dance / top_ball) start at 100 (game.s clear_apply)
 * and are never written to the save file in the original, so they only live
 * for the session. rank.s needs >= 300 for 舞林高手 / 灌篮高手.
 */
#include "minigame.h"

#include "gfx.h"
#include "rng.h"
#include "text.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define MG_DANCE_BONUS 3      /* panyan.s DODGE_BONUS */
#define MG_BALL_BONUS  10     /* panyan.s PARRY_BONUS */
#define MG_DANCE_LEVEL 200    /* panyan.s game_level of lian_dodge */
#define MG_BALL_LIVES  7      /* panyan.s game_level of lian_unarmed */
#define MG_TICK_MS     4      /* waittime unit: one tick is 4 ms */
#define MG_TOP_SEED    100    /* game.s clear_apply: top_dance/top_ball = 100 */

/* Original geometry (panyan.s, 160x80 screen): the ball bounces between
 * x = 12 and x = 44, and proc_basket_f1 scores when the ball stops with
 * 26 <= x < 29. The 3DS layout keeps the same proportions. */
#define MG_BALL_MIN_X   40
#define MG_BALL_MAX_X   280
#define MG_BALL_HIT_LO  145
#define MG_BALL_HIT_HI  168
#define MG_BALL_SIZE    10

/* panyan.s hill_msg / select_road_msg (the `if scode` branch). */
static const char *const MG_HILL_MSG =
	"一座高山,山顶终年积雪,相传山顶有神仙居住,想上去看看吗?";
static const char *const MG_HALL_MSG2 =
	"这是方圆百里最有名的游戏厅,\n你想玩甚麽?(选择数字)";
static const char *const MG_HALL_OPTIONS[3] = { "1.跳舞毯", "2.投铅球", "3.放弃" };

static int s_topDance = MG_TOP_SEED;
static int s_topBall = MG_TOP_SEED;

void minigame_reset_top(void)
{
	s_topDance = MG_TOP_SEED;
	s_topBall = MG_TOP_SEED;
}

int minigame_top_dance(void)
{
	return s_topDance;
}

int minigame_top_ball(void)
{
	return s_topBall;
}

/* panyan.s proc_quit: the high score of the finished game is written back to
 * top_dance (跳舞毯) or top_ball (投铅球) when the player leaves. */
void minigame_finish(MiniGame *mg)
{
	if (!mg)
		return;
	if (mg->stage == MG_DANCE)
	{
		if (mg->top > s_topDance)
			s_topDance = mg->top;
	}
	else if (mg->stage == MG_BALL)
	{
		if (mg->top > s_topBall)
			s_topBall = mg->top;
	}
}

/* panyan.s wait_for_key: the arrow is random_it(4) and never the same as
 * the previous one. */
static int mg_random_dir(int prev)
{
	int d;
	do
	{
		d = (int)(rng_random_it(4) & 3);
	} while (d == prev);
	return d;
}

/* panyan.s random_delay: random_it(4) picks the step delay, 20..160 ms. */
static float mg_ball_step_delay(void)
{
	int r = (int)(rng_random_it(4) & 3);
	switch (r)
	{
	case 0: return 5.0f * MG_TICK_MS / 1000.0f;
	case 2: return 20.0f * MG_TICK_MS / 1000.0f;
	default: return 40.0f * MG_TICK_MS / 1000.0f;
	}
}

void minigame_open(MiniGame *mg)
{
	if (!mg)
		return;
	memset(mg, 0, sizeof(*mg));
	mg->stage = MG_HALL;
	mg->sel = 0;
	mg->top = s_topDance;
	snprintf(mg->msg, sizeof(mg->msg), "%s", MG_HILL_MSG);
}

void minigame_start(MiniGame *mg, int stage)
{
	if (!mg)
		return;
	memset(mg, 0, sizeof(*mg));
	mg->stage = stage;
	if (stage == MG_DANCE)
	{
		mg->level = MG_DANCE_LEVEL;
		mg->top = s_topDance;
		mg->dir = mg_random_dir(-1);
		mg->dirPrev = mg->dir;
		mg->timer = (float)mg->level * MG_TICK_MS / 1000.0f;
		snprintf(mg->msg, sizeof(mg->msg), "按方向键踩中提示的箭头");
	}
	else
	{
		mg->level = MG_BALL_LIVES;
		mg->top = s_topBall;
		mg->ballX = (MG_BALL_MIN_X + MG_BALL_MAX_X) / 2;
		mg->ballDir = 1;
		mg->step = mg_ball_step_delay();
		snprintf(mg->msg, sizeof(mg->msg), "A 开始 / 停住小球");
	}
}

/* panyan.s check_score: the pressed arrow must match the shown one. */
static bool mg_dance_hit(MiniGame *mg, int dir)
{
	if (dir == mg->dir)
	{
		mg->score += MG_DANCE_BONUS;
		/* panyan.s add_score: game_level -= 2 while it is >= 80. */
		if (mg->level >= 80)
			mg->level -= 2;
		if (mg->score > mg->top)
			mg->top = mg->score;
		mg->dirPrev = mg->dir;
		mg->dir = mg_random_dir(mg->dirPrev);
		mg->timer = (float)mg->level * MG_TICK_MS / 1000.0f;
		snprintf(mg->msg, sizeof(mg->msg), "踩中！+%d", MG_DANCE_BONUS);
		return true;
	}
	/* panyan.s check_score jumps to proc_esc: a wrong key ends the game. */
	mg->done = true;
	snprintf(mg->msg, sizeof(mg->msg), "踩错了，游戏结束");
	return false;
}

/* panyan.s proc_basket_f1: the ball is stopped inside the target strip. */
static void mg_ball_stop(MiniGame *mg)
{
	mg->ballRunning = false;
	if (mg->ballX >= MG_BALL_HIT_LO && mg->ballX < MG_BALL_HIT_HI)
	{
		mg->score += MG_BALL_BONUS;
		if (mg->score > mg->top)
			mg->top = mg->score;
		snprintf(mg->msg, sizeof(mg->msg), "投进了！+%d", MG_BALL_BONUS);
	}
	else
	{
		mg->level--;
		if (mg->level <= 0)
		{
			mg->level = 0;
			mg->done = true;
			snprintf(mg->msg, sizeof(mg->msg), "没投中，球用完了");
		}
		else
			snprintf(mg->msg, sizeof(mg->msg), "没投中，还剩 %d 次", mg->level);
	}
}

bool minigame_update(MiniGame *mg, float dt, u32 down)
{
	if (!mg)
		return false;

	if (mg->stage == MG_HALL)
	{
		if (down & KEY_UP)
			mg->sel = (mg->sel + 2) % 3;
		if (down & KEY_DOWN)
			mg->sel = (mg->sel + 1) % 3;
		if (down & KEY_A)
		{
			if (mg->sel == 0)
				minigame_start(mg, MG_DANCE);
			else if (mg->sel == 1)
				minigame_start(mg, MG_BALL);
			else
				return false;   /* 放弃 */
		}
		if (down & KEY_B)
			return false;
		return true;
	}

	if (mg->done)
	{
		if (down & (KEY_A | KEY_B))
			return false;
		return true;
	}

	if (mg->stage == MG_DANCE)
	{
		int dir = -1;
		if (down & KEY_UP)    dir = 2;
		else if (down & KEY_DOWN)  dir = 0;
		else if (down & KEY_LEFT)  dir = 1;
		else if (down & KEY_RIGHT) dir = 3;
		if (dir >= 0)
			mg_dance_hit(mg, dir);
		else
		{
			mg->timer -= dt;
			if (mg->timer <= 0.0f)
			{
				/* panyan.s: no key pressed in the window just moves on to
				 * the next arrow, no score and no penalty. */
				mg->dirPrev = mg->dir;
				mg->dir = mg_random_dir(mg->dirPrev);
				mg->timer = (float)mg->level * MG_TICK_MS / 1000.0f;
			}
		}
		if (down & KEY_B)
			return false;
		return true;
	}

	/* MG_BALL */
	if (down & KEY_A)
	{
		if (mg->ballRunning)
			mg_ball_stop(mg);
		else
		{
			mg->ballRunning = true;
			mg->step = mg_ball_step_delay();
			snprintf(mg->msg, sizeof(mg->msg), "再按 A 停住小球");
		}
	}
	if (down & KEY_B)
		return false;
	if (mg->ballRunning)
	{
		mg->step -= dt;
		while (mg->step <= 0.0f)
		{
			mg->step += mg_ball_step_delay();
			mg->ballX += mg->ballDir;
			if (mg->ballX >= MG_BALL_MAX_X)
			{
				mg->ballX = MG_BALL_MAX_X;
				mg->ballDir = -1;
			}
			else if (mg->ballX <= MG_BALL_MIN_X)
			{
				mg->ballX = MG_BALL_MIN_X;
				mg->ballDir = 1;
			}
		}
	}
	return true;
}

bool minigame_touch(MiniGame *mg, int px, int py)
{
	if (!mg)
		return false;
	if (mg->stage == MG_HALL)
	{
		for (int i = 0; i < 3; i++)
		{
			int y = 130 + i * 26;
			if (px < 24 || px >= 296 || py < y || py >= y + 22)
				continue;
			mg->sel = i;
			if (i == 2)
				return false;      /* 放弃 */
			minigame_start(mg, i == 0 ? MG_DANCE : MG_BALL);
			return true;
		}
		return true;
	}
	if (mg->stage == MG_BALL && !mg->done)
	{
		/* Tap the track to start / stop the ball. */
		if (py >= 100 && py < 160)
		{
			if (mg->ballRunning)
				mg_ball_stop(mg);
			else
			{
				mg->ballRunning = true;
				mg->step = mg_ball_step_delay();
				snprintf(mg->msg, sizeof(mg->msg), "再按 A 停住小球");
			}
		}
	}
	return true;
}

/* ------------------------------------------------------------------ drawing */

static void mg_arrow(int cx, int cy, int dir, u32 color, int size)
{
	/* 0 = down, 1 = left, 2 = up, 3 = right (the DIR_* order of actor.h). */
	float x = (float)cx, y = (float)cy, s = (float)size;
	switch (dir)
	{
	case 2:  /* up */
		C2D_DrawTriangle(x, y - s, color, x - s, y + s, color, x + s, y + s,
		                 color, 0.0f);
		break;
	case 0:  /* down */
		C2D_DrawTriangle(x, y + s, color, x - s, y - s, color, x + s, y - s,
		                 color, 0.0f);
		break;
	case 1:  /* left */
		C2D_DrawTriangle(x - s, y, color, x + s, y - s, color, x + s, y + s,
		                 color, 0.0f);
		break;
	default: /* right */
		C2D_DrawTriangle(x + s, y, color, x - s, y - s, color, x - s, y + s,
		                 color, 0.0f);
		break;
	}
}

/* Wrap s to fit max_w, then draw every line centred on the 320 px screen
 * and return the y below the last line. Each wrapped line is drawn on its
 * own: text_draw() would carry on past an embedded '\n' (resetting x and
 * dropping 16 px), which double-printed the second line of MG_HILL_MSG. */
static int mg_draw_wrapped(const char *s, int max_w, int max_lines, int y,
                           int line_h, u32 color)
{
	char buf[256];
	int lines = text_wrap_fitted(s, max_w, 1, buf, sizeof(buf), max_lines);
	if (lines < 1)
		lines = 1;
	char *p = buf;
	for (int i = 0; i < lines && p; i++)
	{
		char *nl = strchr(p, '\n');
		if (nl)
			*nl = '\0';
		int w = text_width(p, 1);
		text_draw(p, (320 - w) / 2, y + i * line_h, 1, color);
		p = nl ? nl + 1 : NULL;
	}
	return y + lines * line_h;
}

static void mg_draw_hall(const MiniGame *mg)
{
	ui_header("山丘");
	int ty = mg_draw_wrapped(MG_HILL_MSG, 300, 3, 34, 18, ui_col(UI_COL_TEXT));
	mg_draw_wrapped(MG_HALL_MSG2, 300, 2, ty + 10, 18, ui_col(UI_COL_DIM));

	for (int i = 0; i < 3; i++)
	{
		int y = 130 + i * 26;
		bool sel = (i == mg->sel);
		C2D_DrawRectSolid(24.0f, (float)y, 0.0f, 272.0f, 22.0f,
		                  sel ? ui_col(UI_COL_SEL) : ui_col(UI_COL_PANEL));
		if (sel)
			ui_frame(24, y, 272, 22, ui_col(UI_COL_ACCENT));
		text_draw(MG_HALL_OPTIONS[i], 34, y + 3, 1,
		          sel ? ui_col(UI_COL_ACCENT) : ui_col(UI_COL_TEXT));
	}
}

static void mg_draw_score(const MiniGame *mg, const char *title)
{
	ui_header(title);
	char line[64];
	snprintf(line, sizeof(line), "SCORE %d", mg->score);
	text_draw(line, 12, 30, 1, ui_col(UI_COL_TEXT));
	snprintf(line, sizeof(line), "TOP %d", mg->top);
	text_draw(line, 12, 48, 1, ui_col(UI_COL_DIM));
}

static void mg_draw_dance(const MiniGame *mg)
{
	mg_draw_score(mg, "跳舞毯");

	/* The four arrows of the original's prompt panel. */
	int cx = 70, cy = 120, size = 16;
	ui_panel(20, 70, 100, 100);
	mg_arrow(cx, cy - 34, 2, ui_col(UI_COL_DIM), size);
	mg_arrow(cx - 34, cy, 1, ui_col(UI_COL_DIM), size);
	mg_arrow(cx + 34, cy, 3, ui_col(UI_COL_DIM), size);
	mg_arrow(cx, cy + 34, 0, ui_col(UI_COL_DIM), size);

	/* The arrow that has to be pressed. */
	ui_panel(150, 70, 150, 100);
	u32 color = mg->done ? ui_col(UI_COL_OFF) : ui_col(UI_COL_ACCENT);
	mg_arrow(225, 120, mg->dir, color, 26);

	if (mg->msg[0])
	{
		int w = text_width(mg->msg, 1);
		text_draw(mg->msg, (320 - w) / 2, 186, 1,
		          mg->done ? ui_col(UI_COL_DANGER) : ui_col(UI_COL_TEXT));
	}
	text_draw("方向键踩箭头, B 退出", 12, 212, 1, ui_col(UI_COL_DIM));
}

static void mg_draw_ball(const MiniGame *mg)
{
	mg_draw_score(mg, "投铅球");

	/* The target strip on the track. */
	C2D_DrawRectSolid((float)MG_BALL_HIT_LO, 108.0f, 0.0f,
	                  (float)(MG_BALL_HIT_HI - MG_BALL_HIT_LO), 34.0f,
	                  ui_col(UI_COL_SEL));
	ui_frame(MG_BALL_HIT_LO, 108, MG_BALL_HIT_HI - MG_BALL_HIT_LO, 34,
	         ui_col(UI_COL_ACCENT));
	ui_frame(MG_BALL_MIN_X - 10, 100, MG_BALL_MAX_X - MG_BALL_MIN_X + 20, 50,
	         ui_col(UI_COL_BORDER));

	/* The ball. */
	C2D_DrawRectSolid((float)mg->ballX, 118.0f, 0.0f, (float)MG_BALL_SIZE,
	                  (float)MG_BALL_SIZE,
	                  mg->done ? ui_col(UI_COL_OFF) : ui_col(UI_COL_ACCENT));

	/* The remaining lives (panyan.s game_level). */
	char line[64];
	snprintf(line, sizeof(line), "剩余 %d 次", mg->level);
	text_draw(line, 12, 62, 1, ui_col(UI_COL_TEXT));
	for (int i = 0; i < mg->level && i < 16; i++)
		C2D_DrawRectSolid((float)(12 + i * 12), 82.0f, 0.0f, 8.0f, 8.0f,
		                  ui_col(UI_COL_ACCENT));

	if (mg->msg[0])
	{
		int w = text_width(mg->msg, 1);
		text_draw(mg->msg, (320 - w) / 2, 186, 1,
		          mg->done ? ui_col(UI_COL_DANGER) : ui_col(UI_COL_TEXT));
	}
	text_draw("A 投球, B 退出", 12, 212, 1, ui_col(UI_COL_DIM));
}

void minigame_draw(const MiniGame *mg)
{
	if (!mg)
		return;
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, ui_col(UI_COL_BG));
	switch (mg->stage)
	{
	case MG_HALL:  mg_draw_hall(mg); break;
	case MG_DANCE: mg_draw_dance(mg); break;
	case MG_BALL:  mg_draw_ball(mg); break;
	default: break;
	}
	if (mg->stage != MG_HALL && mg->done)
	{
		/* The original shows the score and waits for a key. */
		char line[64];
		snprintf(line, sizeof(line), "得分 %d  最高 %d", mg->score, mg->top);
		int w = text_width(line, 1);
		text_draw(line, (320 - w) / 2, 160, 1, ui_col(UI_COL_ACCENT));
	}
}

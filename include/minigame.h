#ifndef MINIGAME_H
#define MINIGAME_H

#include "gmud.h"

/* panyan.s: the two mini games behind the 山丘 (HILL2_ID) object.
 *
 *   山丘 -> hill_msg -> "这是方圆百里最有名的游戏厅,你想玩甚麽?"
 *          1.跳舞毯  -> lian_dodge  -> score to top_dance -> 舞林高手
 *          2.投铅球  -> lian_unarmed-> score to top_ball  -> 灌篮高手
 *          3.放弃    -> back to the map
 *
 * game.s clear_apply seeds both high scores with 100. The original keeps
 * them in RAM only (they are not part of the save file), so they are reset
 * when a new run starts and survive 读档 within the same session.
 */

enum
{
	MG_NONE = 0,
	MG_HALL,     /* 山丘: pick a game */
	MG_DANCE,    /* 跳舞毯 (lian_dodge) */
	MG_BALL      /* 投铅球 (lian_unarmed) */
};

typedef struct
{
	int stage;         /* MG_* */
	int sel;           /* hall row: 0 = 跳舞毯, 1 = 投铅球, 2 = 放弃 */
	int score;         /* tiaowu_score */
	int top;           /* top_score of the running game */
	int level;         /* original game_level (200 for the dance, 7 lives) */
	int dir;           /* dance: the arrow the player has to press */
	int dirPrev;       /* dance: the previous arrow (never repeated) */
	float timer;       /* dance: seconds left in the current arrow window */
	float step;        /* ball: seconds until the ball takes its next step */
	int ballX;         /* ball: x position of the ball */
	int ballDir;       /* ball: +1 / -1 */
	bool ballRunning;  /* ball: the ball is sliding */
	bool done;         /* the game finished, waiting for A/B */
	char msg[128];     /* last result line */
} MiniGame;

/* Resets the session high scores (game.s clear_apply seeds them with 100). */
void minigame_reset_top(void);

/* High scores: rank.s needs >= 300 for 舞林高手 / 灌篮高手. */
int minigame_top_dance(void);
int minigame_top_ball(void);

/* Opens the 山丘 game hall. */
void minigame_open(MiniGame *mg);
/* Starts one of the two games (MG_DANCE / MG_BALL). */
void minigame_start(MiniGame *mg, int stage);
/* Advances the game; returns false when the player left the minigame. */
bool minigame_update(MiniGame *mg, float dt, u32 down);
bool minigame_touch(MiniGame *mg, int px, int py);
/* Writes the high score of the finished game back (panyan.s proc_quit). */
void minigame_finish(MiniGame *mg);
/* Draws the minigame on the bottom screen. */
void minigame_draw(const MiniGame *mg);

#endif

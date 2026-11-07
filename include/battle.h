#ifndef BATTLE_H
#define BATTLE_H

#include "actor.h"
#include "gmud.h"
#include "npcdata.h"

#define BATTLE_LOG_LINES 4
#define BATTLE_LOG_LEN 320     /* original move descriptions run up to ~80 chars */
#define BT_CMD_COUNT 5
#define BT_SUB_ROWS 4

/* One perform can queue a start line, several branch lines, several attack
 * ops, fixed damage/MP ops and the over-text. 24 was reachable when the
 * queue was first introduced; 48 leaves headroom so bt_op_new() no longer
 * has to drop an action in normal play. Battle is a static global, so the
 * extra ~8 KB does not come off the stack. */
#define BT_MAX_OPS 48
#define BT_MAX_EFFECTS 20   /* perform.s ptemp has 20 slots for the player */

enum
{
	BT_CMD_ATTACK = 0,
	BT_CMD_PERFORM,
	BT_CMD_HEAL,
	BT_CMD_ITEM,
	BT_CMD_ESCAPE
};

enum
{
	BT_PHASE_FIGHT = 0,
	BT_PHASE_PERFORM,
	BT_PHASE_ITEM,
	BT_PHASE_MERCY,
	BT_PHASE_WIN,
	BT_PHASE_LOSE,
	BT_PHASE_ESCAPED
};

/* One visible beat of a resolved action. The original perform.s calls
 * attack_xxx() several times inside one technique and lets fight.s show the
 * messages one after another; ops preserve that order instead of spreading
 * the hits over separate enemy turns. */
enum
{
	BT_OP_ATTACK = 0,   /* normal AP/DP/PP attack (multi-hit techniques) */
	BT_OP_DAMAGE,       /* fixed damage that bypasses AP/DP (落英/震/神倒) */
	BT_OP_MP,           /* fixed 内力 drain (挤字诀/震字诀) */
	BT_OP_LOG           /* message only */
};

typedef struct
{
	u8 kind;
	u8 stage;           /* BT_OP_ATTACK: 0 = description, 1 = resolve */
	u8 attacker;        /* 0 = player, 1 = enemy */
	u8 unarmed;         /* player temporarily unwields for 刀影掌/柳浪 */
	u8 target;          /* 0 = player, 1 = enemy (damage/mp ops) */
	int apAdd;
	int dmgAdd;
	int strAdd;
	int value;
	int wound;          /* effhp (伤势) reduction that accompanies fixed damage */
	float hold;         /* seconds this beat stays on screen (0 = silent op) */
	char text[BATTLE_LOG_LEN];
} BattleOp;

/* perform.s ptemp[] equivalent: two u8 fields restored when the delay ends.
 * stat ids match refresh_data(): 1 attack, 2 defense, 3 damage, 4 armor,
 * 5 str, 6 dex, 7 int, 8 con, 9 per, 10 kar, 11 npc_attack. */
typedef struct
{
	u8 id;              /* perform id, 0xFF = empty */
	s16 rounds;
	u8 stat1, stat2;
	u8 old1, old2;
} BattleEffect;

typedef struct
{
	bool active;
	bool fatal;
	int npcId;
	u16 portraitTile;     /* same 32x32 map image used by the field NPC */
	int phase;
	int turn;
	int result;           /* 1 win, 0 lose, -1 escaped */
	bool enemyKilled;     /* true when the mercy prompt chose 杀死 */
	/* 擂台 (nf.s netfight): the original duels another player over the
	 * 文曲星 link cable. There is no such link on the 3DS, so the arena is a
	 * sparring match: the opponent is never killed and gets no reward. */
	bool sparring;
	char enemyName[NPC_NAME_LEN];

	/* Derived combat stats, recomputed at battle start. */
	int pHit, pDodge, pDamage, pArmor;
	int eHit, eDodge, eDamage, eArmor;
	/* npc_state carries effhp as well as hp (npc_data.gb). All 127 records
	 * have effhp == maxhp, so starting at ehpMax is exact. Wounds lower it;
	 * 吸星大法 and 吸气 can never heal past it. */
	s16 ehp, ehpMax, eEffhp, emp, empMax;

	/* npc_state is mutable in the original: weapon can be seized/thrown
	 * away and 烟幕 lowers npc_attack until the effect expires. */
	int eWeapon;
	int eAttack;
	int eStr;
	int eDex;
	int eIntel;
	int eCon;
	int ePer;

	/* man_busy / npc_busy: one point is consumed when the fighter's own
	 * action is skipped; busy also divides the defender's DP/PP by 3. */
	int pBusy;
	int eBusy;

	/* 忍法影分身: kar == 0xFF is the original flag; per is the dodge %. */
	bool pFenshen;

	/* Sequential display of one resolved action. */
	BattleOp ops[BT_MAX_OPS];
	int opCount;
	int opIndex;
	bool opOverflow;       /* report a dropped action once per battle */
	int afterSeq;          /* 0 idle, 1 enemy action, 2 end of round */
	bool tickEnd;          /* perform rounds tick the ptemp delay at the end */

	/* Currently executing op modifiers. */
	int curUnarmed;
	int curAtkAdd;
	int curDmgAdd;
	int curStrAdd;
	/* string.s is_basic_msg writes damage_type from the chosen basic attack
	 * template's third byte. Kungfu move descriptions leave the global byte
	 * alone, so this persists across attacks (and across both fighters). */
	int damageType;

	BattleEffect effects[BT_MAX_EFFECTS];

	int menuIndex;
	int subIndex;
	int subPage;
	int escapeFactor;
	int pendingBook;          /* book item id used in combat; main.c opens 研读 */

	int flashSide;        /* 1 = player hit, 2 = enemy hit */
	int flashDamage;
	int flashCrit;
	int moveAp, moveDamage, moveForce;
	int pMoveDp, pMovePp, eMoveDp, eMovePp;
	float flashTimer;
	float lock;

	u32 moneyReward;

	char lastDesc[BATTLE_LOG_LEN];  /* current turn's line only, no history */
} Battle;

/* Starts a fight against a map NPC. Returns false for an unknown/dead NPC. */
bool battle_start(Battle *b, Player *p, int npcId, u16 portraitTile);
void battle_update(Battle *b, Player *p, float dt);

/* Keyboard / touch input. Both close the result screen when it is up. */
void battle_input_key(Battle *b, Player *p, u32 down);
void battle_touch(Battle *b, Player *p, int px, int py);

void battle_command_rect(int index, int *x, int *y, int *w, int *h);
void battle_sub_rect(int index, int *x, int *y, int *w, int *h);

/* Top screen battlefield + bottom screen command/log UI. */
void battle_draw_top(const Battle *b, const Player *p, u32 playSeconds);
void battle_draw_bottom(const Battle *b, const Player *p);

#endif

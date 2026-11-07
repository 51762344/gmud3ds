#ifndef ACTOR_H
#define ACTOR_H

#include "charcreate.h"
#include "gmud.h"
#include "item.h"
#include "map.h"
#include "maxhp.h"
#include "skill.h"

#define ACTOR_NAME_LEN 16

/* Original time constants (h/gmud.h): 1 age per 12 h of mud_age,
 * hunger/water tick every 15 s. mud_age == our playSeconds. */
#define ACTOR_BASE_AGE GMUD_BASE_AGE
#define ACTOR_AGE_SECONDS GMUD_AGE_SECONDS   /* AGE_TIME = 3600*12 */
#define ACTOR_TICK_SECONDS 15.0f   /* TICK_TIME */

enum
{
	DIR_DOWN = 0,
	DIR_LEFT = 1,
	DIR_UP = 2,
	DIR_RIGHT = 3
};

/* M6 task slots mirror task.s quest_temp[4] (寻人/寻物/杀NPC/动态鬼).
 * state: 0 none, 1 active, 2 complete (report to the giver once),
 * 3 awaiting 顾炎武's reward. */
#define ACTOR_TASK_SLOTS 4
typedef struct
{
	u8 type;
	u8 state;
	u8 id;        /* target npc id or item id */
	u8 index;     /* quest tier index / ghost level */
	u16 reward;
	u32 time;     /* accept / completion playSeconds */
} ActorTask;

typedef struct Player
{
	int mapIndex;
	int tx, ty;        /* current tile */
	int px, py;        /* pixel anchor, top-left of the current tile */
	int dir;
	bool moving;
	bool justArrived;  /* true for one update after a step completes */
	u8 walkFrame;      /* walk pose 0..2, advanced per completed step
	                    * (original G_role_status, tools.s adjust_status) */
	int toTx, toTy;
	int fromPx, fromPy;
	float t;           /* 0..1 interpolation */

	/* Character sheet (M3). Save.c serializes these fields by hand. */
	char name[ACTOR_NAME_LEN];   /* UTF-8 */
	u8 gender;                   /* 0 = male, 1 = female */
	u8 age;
	u8 pai;                      /* school id, 0 = none */
	u8 str, dex, intel, con, per, kar;
	u8 attack;                   /* base attack */
	u8 defense;                  /* base defense */
	u8 damage;                   /* weapon bonus */
	u8 armor;                    /* armor bonus */
	u32 money;
	u32 exp;
	u16 potential;
	s16 hp, hpMax;
	s16 mp, mpMax;
	/* game.s man_food/man_maxfood/man_water/man_maxwater are 16-bit.
	 * maxFood/maxWater = set_attr(attr_str): (str+5)*15 / (str+4)*15. */
	u16 food, water;
	u16 maxFood, maxWater;

	/* M5: innate stats (setup_attr derives the six stats from these),
	 * injury cap effhp (fight.s) and the master NPC id. */
	u8 attrStr, attrDex, attrIntel, attrCon, attrPer, attrKar;
	s16 effhp;                   /* 0..hpMax; practice needs effhp == hpMax */
	u8 master;                   /* master NPC id, 0 = none */

	/* M5.5: enabled kungfu slots (skill.s man_usekf; bit7 = enabled,
	 * low 7 bits = kf id, index = kf type) and the 加力 charge. */
	u8 usekf[SKILL_USEKF_SLOTS];
	s16 force;
	u8 deadNpc[16];
	u8 stones;
	u8 killTarget, killComplete;   /* v7 kill quest, migrated into tasks[] */
	s8 morality;
	u16 killReward;
	u32 killReportTime;

	/* M6: 寻人/寻物/杀NPC/动态鬼 and the placed ghost's location. */
	ActorTask tasks[ACTOR_TASK_SLOTS];
	u8 ghostXing, ghostMing;      /* name table indices (task.s name_*_tbl) */
	u8 ghostGender, ghostPai;
	u8 ghostMap, ghostX, ghostY;
	u8 ghostActive;
	u8 endingSeen;
	/* task.s home_buf (1 扫地, 2 挑水, 3 劈柴) and rank.s counters. */
	u8 homeBuf;
	u16 npcKill, guanKill, siKill;

	u8 weapon;                   /* equipped weapon item id, 0 = none */
	u8 equip[EQUIP_SLOTS];       /* armor slot -> equipped item id */
	BagSlot bag[BAG_SLOTS];

	/* M4: kungfu book and win/loss counters. Save v3 serializes these.
	 * The original has man_exp/man_pot but no player "level" (T2). */
	u16 wins, losses;
	SkillSlot skills[SKILL_SLOTS];
} Player;

/* New game: apply the T5 creation result (name / gender / innate six stats)
 * on top of the M4 combat baseline, then derive stats and vitals.
 * r == NULL uses cc_default_result(), for save_read()'s v1/v2 fallback. */
void actor_stats_init(Player *p, const CharCreateResult *r);

/* Adds an exp reward; the original grants no level-ups or stat bonuses. */
void actor_gain_exp(Player *p, u32 amount);
void actor_full_heal(Player *p);

/* game.s setup_maxhp + check_player: hpMax = f(age, maxfp, 红莲教义) with
 * effhp <= hpMax and hp <= effhp. Call it whenever one of those changes. */
void actor_setup_maxhp(Player *p);
/* Resyncs p->age from playSeconds, then reapplies actor_setup_maxhp. */
void actor_sync_time(Player *p, u32 playSeconds);
/* system.s game_hour/game_min: an independent RTC clock. game.s show_desc
 * reads game_hour's parity for a 人妖's 相貌 table, rank.s prints
 * game_hour:game_min and checks game_hour < 48 for 神行太保.
 * system.s sys_refresh: game_sec wraps at 60 -> game_min++, game_min wraps at
 * 60 -> game_hour++, so the pair is one continuous counter. The clock lives
 * in zero_area (h/gmud.h:334-336), so it is saved with the file, restored by
 * a load and cleared by a new game; only the hour half is persisted. */
void actor_set_game_clock_base(u32 hours);
void actor_advance_game_clock(u64 deltaMs);
u32 actor_get_game_hour(void);
u32 actor_get_game_min(void);

/* M5 time systems (system.s / game.s). */
int actor_age(u32 playSeconds);          /* man_age, one byte: 14 + mud_age/43200 */
/* setup_attr: derived stats = innate + basic kungfu level/10 (skill.s). */
void actor_setup_attr(Player *p);
/* game.s set_attr: maxFood=(attr_str+5)*15, maxWater=(attr_str+4)*15. */
void actor_set_food_caps(Player *p);
/* 15 s tick: hunger/water drain + hp/mp regen + effhp recovery (system.s). */
void actor_regen_tick(Player *p);

/* Places the player, scanning nearby tiles if (tx,ty) is blocked. */
bool actor_init(Player *p, const GameMap *m, int mapIndex, int tx, int ty);
void actor_place(Player *p, int tx, int ty);
void actor_update(Player *p, const GameMap *m, float dt, u32 held, const circlePosition *circle);
void actor_draw(const Player *p, const GameMap *m, int camX, int camY);
const char *actor_dir_name(int dir);

/* Item actions on a bag slot. Return true when the bag/stats changed. */
bool actor_use_item(Player *p, int slot);
bool actor_equip_item(Player *p, int slot);
bool actor_drop_item(Player *p, int slot);

int actor_attack_power(const Player *p);
int actor_defense_power(const Player *p);

/* skill.s get_skill_desc / get_str_desc / get_per_desc. The 查看-描述 page and
 * the NPC 查看 dialog share these tables so both speak the same language. */
const char *actor_martial_desc_levels(int attack, int dodge, int parry);
const char *actor_martial_desc(const Player *p);
const char *actor_strength_desc(const Player *p);
const char *actor_looks_desc(const Player *p);

/* h/gmud.h GOODMAN=160, BADMAN=100 on the 0..255 man_daode byte.
 * The s8 field stores that byte; 0x80 (= -128) is the neutral start. */
static inline u8 actor_morality_raw(const Player *p)
{
	return (u8)p->morality;
}

#endif

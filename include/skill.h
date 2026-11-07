#ifndef SKILL_H
#define SKILL_H

#include "gmud.h"

#define SKILL_SLOTS 20
#define SKILL_NAME_LEN 20
#define SKILL_REC_SIZE 24
#define SKILL_TYPE_FORCE 3
#define SKILL_TYPE_HAND 0
#define SKILL_TYPE_WEAPON 1
#define SKILL_TYPE_DODGE 2
#define SKILL_TYPE_PARRY 4
#define SKILL_TYPE_LEARN 5

/* Weapon types (h/id.h weapon_type); ItemDef.sub carries this for weapons. */
enum
{
	WT_SPEAR = 0, WT_BLADE, WT_CLUB, WT_AXE, WT_FORK,
	WT_HAMMER, WT_SWORD, WT_STAFF, WT_THROW, WT_WHIP
};

/* 0-based kungfu ids in data/skills.bin (h/id.h kf_id enum). */
enum
{
	SK_BASIC_FORCE = 0,
	SK_BASIC_BARE = 1,
	SK_BASIC_SWORD = 2,
	SK_BASIC_BLADE = 3,
	SK_BASIC_CLUB = 4,
	SK_BASIC_STAFF = 5,
	SK_BASIC_WHIP = 6,
	SK_BASIC_DODGE = 7,
	SK_BASIC_PARRY = 8,
	SK_LITERATE = 9,
	SK_LOOKS = 10,
	SK_BAGUAD = 11,
	SK_BAGUAZ = 12,
	SK_BAZHEN = 13,
	SK_HUNYUAN = 14,
	SK_YOULONG = 15,
	SK_HUAFEI = 16,
	SK_HUATUAN = 17,
	SK_LIU = 18,
	SK_MEIHUA = 19,
	SK_SANHUA = 20,
	SK_HEXIAN = 21,
	SK_JIAOYI = 22,
	SK_PIFENG = 23,
	SK_TAIZU = 24,
	SK_TONGJI = 25,
	SK_RENSHU = 26,
	SK_WUFA = 27,
	SK_WUYING = 28,
	SK_YIDAO = 29,
	SK_TAIJI_SWORD = 30,
	SK_TAIJIQUAN = 31,
	SK_TAIJIG = 32,
	SK_WANLIU = 33,
	SK_XUANXU = 34,
	SK_TAXUE = 35,
	SK_XUESHAN_FORCE = 36,
	SK_XUESHAN0 = 37,
	SK_XUESHAN_SWORD = 38,
	SK_XUEYING = 39,
	SK_MENGHU = 40,
	SK_XI = 41,
	SK_COUNT_EXPECTED = 42
};

typedef struct
{
	u8 id;         /* kf id (0 is valid: 基本内功) */
	u8 level;      /* 0 = empty slot */
	u16 exp;       /* practice points */
} SkillSlot;

typedef struct
{
	u8 id;
	u8 type;       /* SKILL_TYPE_* */
	u8 qi;         /* weapon type for WEAPON_KF */
	char name[SKILL_NAME_LEN];
} SkillDef;

typedef struct
{
	u8 id;
	u8 kf;         /* required kungfu id (pf_attr_tbl) */
	char name[SKILL_NAME_LEN];
} PerformDef;

bool skill_pack_load(const char *romfs_path);
void skill_pack_free(void);
int skill_count(void);
int perform_count(void);
const SkillDef *skill_get(int id);
const PerformDef *perform_get(int id);
const char *skill_name(int id);
const char *perform_name(int id);

/* Player skill book (implemented in skill.c, operates on Player.skills). */
struct Player;
int skill_level(const struct Player *p, int id);
/* Current practice points of one kungfu (0 when not learned). */
u16 skill_exp(const struct Player *p, int id);
bool skill_add(struct Player *p, int id, int level);
/* Slot indices of learned kungfu, in book order (original 技能 lists only
 * what the player knows, not the empty slots). */
int skill_learned_list(const struct Player *p, int *out, int max);
/* Highest level among learned kungfu of a given SKILL_TYPE_*. */
int skill_best_level(const struct Player *p, int type);
/* Adds practice points, returns the number of level-ups.
 * Original improve_skill: pot >= (level+1)^2 -> level++, pot = 0. */
int skill_train(struct Player *p, int id, int points);
/* Basic kungfu a skill builds on (check_skill/weapon_basic_tbl); -1 = none. */
int skill_basic_kf(int kfId);

/* M5.5 enabled kungfu (skill.s man_usekf): five slots indexed by kf type
 * (0 拳脚 1 兵刃 2 轻功 3 内功 4 招架); bit7 = enabled, low 7 bits = kf id. */
#define SKILL_USEKF_SLOTS 5
/* Kungfu currently enabled in a slot, 0 = none. */
int skill_usekf_id(const struct Player *p, int type);
/* use_skills: toggles a learned non-basic kungfu in its type slot
 * (enabling replaces the previous one, enabling the same id disables).
 * Returns false for basic kungfu, unlearned ids and 识字/相貌. */
bool skill_usekf_toggle(struct Player *p, int kfId);
/* skill.s query_skill: effective level = basic level/2 + enabled kungfu
 * level, the latter only when check_skill passes (weapon/basic match). */
int skill_query_level(const struct Player *p, int type);

/* Fills out[] with perform ids of the ENABLED kungfu slots:
 * armed -> weapon slot, else hand slot, plus the dodge and force slots. */
int skill_perform_list(const struct Player *p, int *out, int max);
/* Effective (basic/2 + enabled) level of one kungfu, as query_skill. */
int skill_kf_effective_level(const struct Player *p, int kfId);
int skill_perform_cost(int pfId, int kfLevel);
const char *skill_perform_label(int pfId);

#endif

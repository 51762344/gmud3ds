#include "battle.h"

#include "audio.h"
#include "fighttext.h"
#include "gfx.h"
#include "item.h"
#include "log.h"
#include "neili.h"
#include "reward.h"
#include "rng.h"
#include "skill.h"
#include "text.h"
#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* fight.s show_fight_msg: draws the message, waittime 100 (~400 ms with the
 * original 4 ms tick) + delay_1_sec (~960 ms) -> every battle message stays
 * on screen for about 1.36 s. One attack prints two messages (action
 * description, then dodge/result), so one exchange is ~2.7 s. */
#define BT_MSG_HOLD 1.36f
/* Port-only "体力 -N"/"内力 -N" readouts: they are not separate messages in
 * the original, so they get a short beat instead of a full message hold. */
#define BT_NOTE_HOLD 0.90f
#define BT_FLASH 0.85f
#define BT_PERFORM_MAX 24

#define BT_AFTER_NONE 0
#define BT_AFTER_ENEMY 1
#define BT_AFTER_END 2

/* Battle UI colours follow the active theme like every other page. */
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
#define CLR_RED     ui_col(UI_COL_DANGER)

static void battle_command(Battle *b, Player *p, int cmd);
static u32 rnd_below(u32 n);
static void battle_log(Battle *b, const char *fmt, ...);
static void build_attack_desc(Battle *b, const Player *p, bool fromPlayer,
	                          char *out, int size);
static int battle_attack(Battle *b, Player *p, bool fromPlayer);
static int battle_item_slots(const Player *p, int *slots, int max);
static void bt_refresh_enemy_panel(Battle *b);
static void battle_win(Battle *b, Player *p, bool kill);

/* All randomness now comes from the single mud_seed state in rng.c.
 * random_it (2-byte range) is the default; the 4-byte hit/parry checks go
 * through perform_random_it, matching fight.s random4 and perform.s:684. */
static int rnd(int n)
{
	if (n <= 0)
		return 0;
	/* math.s random_it takes a 2-byte range. The original damage/AP values
	 * are 16-bit too, so clamp rather than wrap a wide C int to zero. */
	return (int)rng_random_it((u16)(n > 0xFFFF ? 0xFFFF : n));
}

static u32 rnd_below(u32 n)
{
	return (u32)rng_perform_random_it((uint32_t)n);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static int u8_add(int value, int add)
{
	return clampi(value + add, 0, 255);
}

/* --------------------------------------------------------------- ptemp */
/* stat ids follow perform.s refresh_data(). */
enum
{
	ST_NONE = 0,
	ST_ATTACK,
	ST_DEFENSE,
	ST_DAMAGE,
	ST_ARMOR,
	ST_STR,
	ST_DEX,
	ST_INT,
	ST_CON,
	ST_PER,
	ST_KAR,
	ST_NPC_ATTACK
};

static void bt_stat_set(Battle *b, Player *p, int stat, int value)
{
	value = clampi(value, 0, 255);
	switch (stat)
	{
	case ST_ATTACK: p->attack = (u8)value; break;
	case ST_DEFENSE: p->defense = (u8)value; break;
	case ST_DAMAGE: p->damage = (u8)value; break;
	case ST_ARMOR: p->armor = (u8)value; break;
	case ST_STR: p->str = (u8)value; break;
	case ST_DEX: p->dex = (u8)value; break;
	case ST_INT: p->intel = (u8)value; break;
	case ST_CON: p->con = (u8)value; break;
	case ST_PER: p->per = (u8)value; break;
	case ST_KAR: p->kar = (u8)value; break;
	case ST_NPC_ATTACK: b->eAttack = value; break;
	default: break;
	}
}

/* perform.s show_over_msg: the perform.txt label index each perform prints
 * when its ptemp effect expires. PF_MSG_NONE = the original prints nothing. */
static int bt_over_id(int pf)
{
	static const int s_over[BT_PERFORM_MAX] =
	{
		PF_MSG_NONE, PF_MSG_NONE, PF_MSG_ZHANGDAO_OVER, PF_MSG_ZHANGDAO_OVER,
		PF_MSG_NONE, PF_MSG_NONE, PF_MSG_SANHUA_OVER, PF_MSG_NONE,
		PF_MSG_NONE, PF_MSG_NONE, PF_MSG_FENSHEN_OVER, PF_MSG_YIANMU_OVER,
		PF_MSG_NONE, PF_MSG_NONE, PF_MSG_NONE, PF_MSG_LIAN_OVER,
		PF_MSG_NONE, PF_MSG_NONE, PF_MSG_NONE, PF_MSG_NONE,
		PF_MSG_NONE, PF_MSG_BINGXIN_OVER, PF_MSG_NONE, PF_MSG_NONE
	};
	return pf >= 0 && pf < BT_PERFORM_MAX ? s_over[pf] : PF_MSG_NONE;
}

static int bt_effect_find(const Battle *b, int id)
{
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
		if (b->effects[i].id != 0xFF && b->effects[i].id == id)
			return i;
	return -1;
}

static bool bt_effect_add(Battle *b, int id, int rounds,
	                      int stat1, int old1, int stat2, int old2)
{
	int slot = -1;
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
	{
		if (b->effects[i].id == 0xFF)
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return false;
	BattleEffect *e = &b->effects[slot];
	e->id = (u8)id;
	e->rounds = (s16)(rounds > 0 ? rounds : 1);
	e->stat1 = (u8)stat1;
	e->stat2 = (u8)stat2;
	e->old1 = (u8)old1;
	e->old2 = (u8)old2;
	return true;
}

static void bt_effect_expire(Battle *b, Player *p, int idx, bool restore, bool show)
{
	BattleEffect *e = &b->effects[idx];
	int id = e->id;
	if (id == 0xFF)
		return;
	if (restore)
	{
		if (e->stat1) bt_stat_set(b, p, e->stat1, e->old1);
		if (e->stat2) bt_stat_set(b, p, e->stat2, e->old2);
	}
	if (id == 10)
		b->pFenshen = false;
	if (restore && (e->stat1 == ST_NPC_ATTACK || e->stat2 == ST_NPC_ATTACK))
		bt_refresh_enemy_panel(b);
	if (show)
	{
		const char *tpl = fight_perform_label(bt_over_id(id));
		if (tpl)
		{
			char buf[FT_RENDER_MAX];
			fight_render(buf, sizeof(buf), tpl, p->name, b->enemyName,
			             p->weapon ? item_name(p->weapon) : NULL);
			battle_log(b, "%s", buf);
		}
	}
	e->id = 0xFF;
	e->rounds = 0;
	e->stat1 = e->stat2 = 0;
}

static void bt_effects_tick(Battle *b, Player *p)
{
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
	{
		if (b->effects[i].id == 0xFF)
			continue;
		if (b->effects[i].rounds > 0)
			b->effects[i].rounds--;
		if (b->effects[i].rounds <= 0)
			bt_effect_expire(b, p, i, true, true);
	}
}

static void bt_effects_clear(Battle *b, Player *p)
{
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
	{
		if (b->effects[i].id == 0xFF)
			continue;
		bt_effect_expire(b, p, i, true, false);
	}
	b->pFenshen = false;
}

/* ------------------------------------------------------------------ ops */
static void bt_ops_reset(Battle *b)
{
	b->opCount = 0;
	b->opIndex = 0;
	b->afterSeq = BT_AFTER_NONE;
	b->tickEnd = false;
}

static BattleOp *bt_op_new(Battle *b, int kind)
{
	if (b->opCount >= BT_MAX_OPS)
	{
		/* Never lose a queued action without a trace. */
		if (!b->opOverflow)
		{
			b->opOverflow = true;
			log_add("战斗动作队列溢出（上限 %d），本次表现不完整。", BT_MAX_OPS);
		}
		return NULL;
	}
	BattleOp *op = &b->ops[b->opCount++];
	memset(op, 0, sizeof(*op));
	op->kind = (u8)kind;
	op->hold = BT_MSG_HOLD;
	return op;
}

static void bt_op_log(Battle *b, const char *fmt, ...)
{
	BattleOp *op = bt_op_new(b, BT_OP_LOG);
	if (!op)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(op->text, sizeof(op->text), fmt, ap);
	va_end(ap);
}

static void bt_op_attack(Battle *b, int attacker, int unarmed,
	                     int apAdd, int dmgAdd, int strAdd)
{
	BattleOp *op = bt_op_new(b, BT_OP_ATTACK);
	if (!op)
		return;
	op->attacker = (u8)attacker;
	op->unarmed = (u8)unarmed;
	op->apAdd = apAdd;
	op->dmgAdd = dmgAdd;
	op->strAdd = strAdd;
}

static void bt_op_damage(Battle *b, int target, int value, int wound,
	                        const char *fmt, ...)
{
	BattleOp *op = bt_op_new(b, BT_OP_DAMAGE);
	if (!op)
		return;
	op->target = (u8)target;
	op->value = value;
	op->hold = fmt ? BT_NOTE_HOLD : 0.0f;
	/* perform.s: 落英/流星/震字诀/神倒鬼跌 call sub_you_hp and then
	 * explicitly lower the victim's EFFHP as well. */
	op->wound = wound > 0 ? wound : 0;
	if (fmt)
	{
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(op->text, sizeof(op->text), fmt, ap);
		va_end(ap);
	}
}

static void bt_op_mp(Battle *b, int target, int value, const char *fmt, ...)
{
	BattleOp *op = bt_op_new(b, BT_OP_MP);
	if (!op)
		return;
	op->target = (u8)target;
	op->value = value;
	op->hold = fmt ? BT_NOTE_HOLD : 0.0f;
	if (fmt)
	{
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(op->text, sizeof(op->text), fmt, ap);
		va_end(ap);
	}
}

/* Render one perform.txt label with the original $N/$n/$w placeholders.
 * `label` is the PF_MSG_* index from perform_id.h, not an FTXT entry id. */
static void bt_push_msg(Battle *b, Player *p, int label)
{
	const char *tpl = fight_perform_label(label);
	if (!tpl)
		return;
	char buf[FT_RENDER_MAX];
	fight_render(buf, sizeof(buf), tpl, p->name, b->enemyName,
	             p->weapon ? item_name(p->weapon) : NULL);
	bt_op_log(b, "%s", buf);
}

/* The five format_string variables of the current fight. */
static void bt_names(const Battle *b, const Player *p, FightNames *names)
{
	names->attacker = p ? p->name : "";
	names->defender = b->enemyName;
	names->weapon = (p && p->weapon) ? item_name(p->weapon) : NULL;
	names->kungfu = NULL;
	names->perform = NULL;
}

/* perform.s fail messages: one perform.txt label rendered with $k (kungfu
 * name) and $p (perform name). They are written to the fight log directly,
 * like the original's fail_rts -> show_text. */
static void bt_fail_msg(Battle *b, Player *p, int label, const char *kungfu,
                         const char *perform)
{
	FightNames names;
	bt_names(b, p, &names);
	names.kungfu = kungfu;
	names.perform = perform;
	char buf[FT_RENDER_MAX];
	fight_perform_msg(buf, sizeof(buf), label, &names);
	battle_log(b, "%s", buf);
}

static void bt_perform_start(Battle *b, Player *p, int pf)
{
	bt_push_msg(b, p, fight_perform_primary(pf));
}

/* --------------------------------------------------------------- combat */
static int npc_basic_for_weapon(int sub)
{
	switch (sub)
	{
	case WT_BLADE: return SK_BASIC_BLADE;
	case WT_CLUB: return SK_BASIC_CLUB;
	case WT_SWORD: return SK_BASIC_SWORD;
	case WT_STAFF: return SK_BASIC_STAFF;
	case WT_WHIP: return SK_BASIC_WHIP;
	default: return -1;
	}
}

/* npc.s query_npc_skill: basic level/2 + enabled kungfu level. */
static int npc_kf_level(const NpcInfo *n, int type, int weaponId)
{
	int basic = type == SKILL_TYPE_HAND ? SK_BASIC_BARE
	          : type == SKILL_TYPE_DODGE ? SK_BASIC_DODGE
	          : SK_BASIC_PARRY;
	if (type == SKILL_TYPE_WEAPON)
	{
		const ItemDef *w = item_get(weaponId);
		basic = w ? npc_basic_for_weapon(w->sub) : -1;
	}
	int base = -1, active = 0;
	for (int i = 0; i < (int)n->kfCount; i++)
	{
		int id = n->kf[i * 2];
		int level = n->kf[i * 2 + 1];
		if (id == basic) base = level;
		if ((n->usekf[type] & 0x80) && id == (n->usekf[type] & 0x7f)) active = level;
	}
	return base < 0 ? 0 : base / 2 + active;
}

static int npc_attack_slot(const Battle *b)
{
	return b->eWeapon ? SKILL_TYPE_WEAPON : SKILL_TYPE_HAND;
}

static int npc_attack_level(const Battle *b, const NpcInfo *n)
{
	return npc_kf_level(n, npc_attack_slot(b), b->eWeapon);
}

static int npc_attack_kf_id(const Battle *b, const NpcInfo *n)
{
	int slot = npc_attack_slot(b);
	if (n->usekf[slot] & 0x80)
		return n->usekf[slot] & 0x7F;
	if (slot == SKILL_TYPE_WEAPON)
	{
		const ItemDef *w = item_get(b->eWeapon);
		int basic = w ? npc_basic_for_weapon(w->sub) : -1;
		if (basic >= 0) return basic;
	}
	return SK_BASIC_BARE;
}

static int npc_basic_level(const NpcInfo *n, int id)
{
	for (int i = 0; i < n->kfCount; i++)
		if (n->kf[i * 2] == id)
			return n->kf[i * 2 + 1];
	return 0;
}

/* npc.s init_npc only reaches the 后天属性 block after adding the equipped
 * armor: a record whose goods[1] has bit7 clear returns early, so its
 * str/dex keep the raw values even if it knows 基本拳脚/轻功. */
static int npc_str(const NpcInfo *n)
{
	if (!(n->equippedFlags & 2))
		return n->str;
	return n->str + npc_basic_level(n, SK_BASIC_BARE) / 10;
}

static int npc_dex(const NpcInfo *n)
{
	if (!(n->equippedFlags & 2))
		return n->dex;
	return n->dex + npc_basic_level(n, SK_BASIC_DODGE) / 10;
}

/* fight.s get_kf_action: bare hands use the hand slot, a weapon the weapon
 * slot; a missing enabled kungfu falls back to the matching basic kungfu. */
static int player_attack_kf_id(const Player *p, bool unarmed)
{
	int type = (!unarmed && p->weapon) ? SKILL_TYPE_WEAPON : SKILL_TYPE_HAND;
	int id = skill_usekf_id(p, type);
	if (id <= 0)
		id = type == SKILL_TYPE_WEAPON ? SK_BASIC_SWORD : SK_BASIC_BARE;
	return id;
}

static int player_attack_kf(const Player *p, bool unarmed)
{
	if (!unarmed && p->weapon)
		return skill_query_level(p, SKILL_TYPE_WEAPON);
	if (p->weapon)
	{
		/* 刀影掌/柳浪 really unequip before the hand attacks, so the hand
		 * slot passes check_skill even though the player owns a weapon. */
		Player tmp = *p;
		tmp.weapon = 0;
		return skill_query_level(&tmp, SKILL_TYPE_HAND);
	}
	return skill_query_level(p, SKILL_TYPE_HAND);
}

static int attr_combat_scale(int attr)
{
	/* fight.s point_adjust: >=30 does `sbc #30 / lsr / adc #100`.
	 * The lsr leaves the odd bit in carry, so an odd delta rounds UP. */
	if (attr >= 30)
	{
		int d = attr - 30;
		return 100 + d / 2 + (d & 1);
	}
	return 100 - (30 - attr) / 2;
}

static int combat_power_raw(int skill, int modifier, u32 exp)
{
	int v = skill + modifier;
	if (v < 0) v = 0;
	unsigned long long power = v ? (unsigned long long)v * v * v / 300 + exp / 100 : exp / 200;
	return power > 1000000000 ? 1000000000 : (int)power;
}

static int combat_power(int skill, int modifier, u32 exp, int attr)
{
	unsigned long long power = (unsigned long long)combat_power_raw(skill, modifier, exp);
	power = power * (unsigned)attr_combat_scale(attr) / 100;
	return power > 1000000000 ? 1000000000 : (int)power;
}

static void battle_stats(Battle *b, const Player *p)
{
	const NpcInfo *n = npc_info(b->npcId);
	int pAtkKf = player_attack_kf(p, false);
	int eAtkKf = npc_attack_level(b, n);
	/* Use the same kf_power/point_adjust expressions as battle_attack(), so
	 * the panel never advertises numbers the engine does not use. */
	b->pHit = combat_power(pAtkKf, p->attack, p->exp, p->dex);
	b->pDodge = combat_power(skill_query_level(p, SKILL_TYPE_DODGE),
	                         p->defense, p->exp, p->con);
	b->pDamage = p->damage + p->str;
	b->pArmor = p->armor;

	b->eHit = combat_power(eAtkKf, b->eAttack, n->exp, b->eDex);
	b->eDodge = combat_power(npc_kf_level(n, SKILL_TYPE_DODGE, 0),
	                         n->defense, n->exp, b->eIntel);
	b->eDamage = n->damage + b->eStr;
	b->eArmor = n->armor;
}

static void bt_refresh_enemy_panel(Battle *b)
{
	const NpcInfo *n = npc_info(b->npcId);
	if (!n)
		return;
	int eAtkKf = npc_attack_level(b, n);
	b->eHit = combat_power(eAtkKf, b->eAttack, n->exp, b->eDex);
	b->eDodge = combat_power(npc_kf_level(n, SKILL_TYPE_DODGE, 0),
	                         n->defense, n->exp, b->eIntel);
	b->eDamage = n->damage + b->eStr;
}

/* ----------------------------------------------------------- techniques */
/* perform.s judge_kf: the required kungfu must be the enabled one in that
 * slot (else not_suit_kf -> suit_msg) and its level must reach the required
 * one (else not_level -> level_msg). */
static bool perf_need_msg(Battle *b, Player *p, int kf, int type, int minLevel,
                      const char *pfName, int *outLevel)
{
	int enabled = skill_usekf_id(p, type);
	if (enabled != kf)
	{
		bt_fail_msg(b, p, PF_MSG_SUIT, skill_name(kf), pfName);
		return false;
	}
	int level = skill_query_level(p, type);
	if (level < minLevel)
	{
		bt_fail_msg(b, p, PF_MSG_LEVEL, skill_name(kf), pfName);
		return false;
	}
	if (outLevel) *outLevel = level;
	return true;
}

/* perform.s judge_force: maxfp below the cost is neili_msg, the current fp
 * below the cost is not_neili_msg. */
static bool perf_force(Battle *b, Player *p, int cost, const char *pfName)
{
	if (p->mpMax < cost)
	{
		bt_fail_msg(b, p, PF_MSG_NEILI, NULL, pfName);
		return false;
	}
	if (p->mp < cost)
	{
		bt_fail_msg(b, p, PF_MSG_NOT_NEILI, NULL, pfName);
		return false;
	}
	return true;
}

/* perform.s find_ptemp: a related perform that is still active gives
 * using_msg ("$N已经在使用$p了"), the perform itself still running gives
 * busy_msg ("$N刚使用$p绝技不久,现在还不能施用外功").
 *
 * Eight performs answer with using_msg in both cases (the two 化掌为刀,
 * 三花聚顶, 红莲出世, 雷动九天, 忍法影分身, 连字诀, 冰心诀), and 旋风三连斩
 * answers with busy_msg even for its related perform. */
static const u8 PF_CONFLICT_SELF_USING[24] =
{
	/*  0 八卦刀影掌     */ 0,
	/*  1 八阵刀影掌     */ 0,
	/*  2 八卦化掌为刀   */ 1,
	/*  3 八阵化掌为刀   */ 1,
	/*  4 落英缤纷       */ 0,
	/*  5 柳浪闻莺       */ 0,
	/*  6 三花聚顶       */ 1,
	/*  7 流星飞掷       */ 0,
	/*  8 红莲出世       */ 1,
	/*  9 雷动九天       */ 1,
	/* 10 忍法影分身     */ 1,
	/* 11 忍术烟幕       */ 0,
	/* 12 旋风三连斩     */ 0,
	/* 13 迎风一刀斩     */ 0,
	/* 14 缠字诀         */ 0,
	/* 15 连字诀         */ 1,
	/* 16 三环套月       */ 0,
	/* 17 挤字诀         */ 0,
	/* 18 乱环诀         */ 0,
	/* 19 阴阳诀         */ 0,
	/* 20 震字诀         */ 0,
	/* 21 冰心诀         */ 1,
	/* 22 雪花六出       */ 0,
	/* 23 神倒鬼跌三连环 */ 0,
};

static int pf_conflict_msg(int pf, int entry)
{
	if (pf == 12)     /* 旋风三连斩: the original uses busy_msg for both */
		return PF_MSG_BUSY;
	if (entry != pf)
		return PF_MSG_USING;
	if (pf >= 0 && pf < 24 && PF_CONFLICT_SELF_USING[pf])
		return PF_MSG_USING;
	return PF_MSG_BUSY;
}

static bool perf_conflict(Battle *b, Player *p, int pf, const int *ids, int count)
{
	for (int i = 0; i < count; i++)
	{
		if (bt_effect_find(b, ids[i]) >= 0)
		{
			int msg = pf_conflict_msg(pf, ids[i]);
			bt_fail_msg(b, p, msg, NULL, perform_name(ids[i]));
			return true;
		}
	}
	return false;
}

/* perform.s man_is_busy: the player's 外功 counter is still counting down. */
static bool perf_busy(Battle *b, Player *p)
{
	if (b->pBusy <= 0)
		return false;
	bt_fail_msg(b, p, PF_MSG_MAN_BUSY, NULL, NULL);
	return true;
}


static void perf_pay(Battle *b, Player *p, int cost)
{
	(void)b;
	p->mp = (s16)clampi((int)p->mp - cost, 0, 32767);
}

/* Remove the equipped weapon from the bag: 流星飞掷 throws it away. */
static void perf_consume_weapon(Battle *b, Player *p)
{
	if (!p->weapon)
		return;
	int id = p->weapon;
	int slot = -1;
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		if (BAG_ID(p->bag[i]) == id && BAG_EQUIPPED(p->bag[i]))
		{
			slot = i;
			break;
		}
	}
	if (slot >= 0)
	{
		p->bag[slot].id = (u8)id;   /* clear the equipped bit */
		if (p->bag[slot].count > 1)
			p->bag[slot].count--;
		else
		{
			p->bag[slot].id = 0;
			p->bag[slot].count = 0;
		}
	}
	p->weapon = 0;
	p->damage = 0;
	bt_op_log(b, "「%s」脱手飞出，已不知去向。", item_name(id));
}

static bool battle_perform(Battle *b, Player *p, int pf)
{
	const PerformDef *d = perform_get(pf);
	const NpcInfo *enemy = npc_info(b->npcId);
	if (!d || !enemy)
		return false;
	if (perf_busy(b, p))
		return false;
	/* perform.s set_ptemp returns carry-clear once all 20 ptemp slots are
	 * used, and the original silently drops the effect — but the MP has
	 * already been paid by then. Reject before perf_pay() so nothing is
	 * lost. A few performs (e.g. zhen's stagger branches) register no
	 * effect at all; refusing them in this extreme state is harmless. */
	int freeEffectSlots = 0;
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
		if (b->effects[i].id == 0xFF)
			freeEffectSlots++;
	if (freeEffectSlots == 0)
	{
		battle_log(b, "真气运转已满，无法再施绝招。");
		return false;
	}

	const char *name = d->name;
	int level = 0;
	int cost = 0;

	switch (pf)
	{
	case 0: /* 八卦刀影掌 */
	{
		static const int conflict[] = { 2, 3, 1, 0 };
		if (!perf_need_msg(b, p, SK_HUNYUAN, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_need_msg(b, p, SK_BAGUAD, SKILL_TYPE_WEAPON, 135, name, &level) ||
		    !perf_need_msg(b, p, SK_BAGUAZ, SKILL_TYPE_HAND, 30, name, &level) ||
		    !perf_force(b, p, 150, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 4))
			return false;
		perf_pay(b, p, 150);
		bt_effect_add(b, pf, 7, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		bt_op_attack(b, 0, 1, 0, 0, 0);
		bt_op_attack(b, 0, 0, 0, 0, 0);
		b->pBusy = 3;
		return true;
	}
	case 1: /* 八阵刀影掌 */
	{
		static const int conflict[] = { 2, 3, 0, 1 };
		if (!perf_need_msg(b, p, SK_HUNYUAN, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_need_msg(b, p, SK_BAGUAD, SKILL_TYPE_WEAPON, 90, name, &level) ||
		    !perf_need_msg(b, p, SK_BAZHEN, SKILL_TYPE_HAND, 45, name, &level) ||
		    !perf_force(b, p, 450, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 4))
			return false;
		perf_pay(b, p, 450);
		bt_effect_add(b, pf, 7, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		bt_op_attack(b, 0, 1, 10, 0, level / 9);
		bt_op_attack(b, 0, 1, 10, 0, level / 9);
		bt_op_attack(b, 0, 0, 10, 0, level / 9);
		b->pBusy = 3;
		return true;
	}
	/* hello.s perform_tbl: id 2 = zhangdao_gua (BAGUAZ / 200),
	 * id 3 = zhangdao_zhen (BAZHEN / 400). pf_attr_tbl agrees:
	 * index 2 -> BAGUAZ_KF, index 3 -> BAZHEN_KF. */
	case 2: /* 八卦化掌为刀 (zhangdao_gua) */
	{
		static const int conflict[] = { 3, 2 };
		if (!perf_need_msg(b, p, SK_HUNYUAN, SKILL_TYPE_FORCE, 105, name, &level) ||
		    !perf_need_msg(b, p, SK_BAGUAZ, SKILL_TYPE_HAND, 105, name, &level) ||
		    !perf_force(b, p, 200, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 2))
			return false;
		perf_pay(b, p, 200);
		int inc = level / 15;
		/* perform.s: delay = skill/25, ATTACK only; the set_obj_busy #2
		 * line is commented out in the original. */
		bt_effect_add(b, pf, clampi(level / 25, 1, 255),
		              ST_ATTACK, p->attack, 0, 0);
		p->attack = (u8)u8_add(p->attack, inc);
		bt_perform_start(b, p, pf);
		return true;
	}
	case 3: /* 八阵化掌为刀 (zhangdao_zhen) */
	{
		static const int conflict[] = { 2, 3 };
		if (!perf_need_msg(b, p, SK_HUNYUAN, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_BAZHEN, SKILL_TYPE_HAND, 120, name, &level) ||
		    !perf_force(b, p, 400, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 2))
			return false;
		perf_pay(b, p, 400);
		int inc = level / 15;
		bt_effect_add(b, pf, clampi(level / 20, 1, 255),
		              ST_ATTACK, p->attack, ST_STR, p->str);
		p->attack = (u8)u8_add(p->attack, inc);
		p->str = (u8)u8_add(p->str, inc * 2);
		bt_perform_start(b, p, pf);
		b->pBusy = 2;
		return true;
	}
	case 4: /* 落英缤纷 */
	{
		static const int conflict[] = { 4 };
		if (!perf_need_msg(b, p, SK_SANHUA, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_HUATUAN, SKILL_TYPE_WEAPON, 120, name, &level) ||
		    !perf_force(b, p, 400, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 400);
		bt_effect_add(b, pf, 6, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		if (b->eWeapon)
		{
			int roll = rnd(clampi(p->dex, 1, 65535));
			int guard = npc_dex(enemy) / 3;
			if (roll > guard)
			{
				bt_push_msg(b, p, PF_MSG_LUOYING_SHOOT);
				b->eWeapon = 0;
				bt_refresh_enemy_panel(b);
			}
			else
			{
				bt_push_msg(b, p, PF_MSG_LUOYING_DODGE);
				b->pBusy = 2;
			}
		}
		else
		{
			int roll = rnd(clampi(level, 1, 65535));
			int guard = npc_kf_level(enemy, SKILL_TYPE_DODGE, 0) / 3;
			if (roll < guard)
			{
				bt_push_msg(b, p, PF_MSG_LUOYING_DODGE1);
				b->pBusy = 2;
			}
			else
			{
				bt_push_msg(b, p, PF_MSG_LUOYING_SHOOT1);
				/* perform.s luoying: sub_you_hp(roll), then effhp -= roll/2. */
				bt_op_damage(b, 1, roll, roll / 2, "%s 体力 -%d", b->enemyName, roll);
				if (b->ehp > roll)
					b->eBusy = 2;
			}
		}
		return true;
	}
	case 5: /* 柳浪闻莺 */
	{
		static const int conflict[] = { 6, 5 };
		if (!perf_need_msg(b, p, SK_SANHUA, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_MEIHUA, SKILL_TYPE_HAND, 60, name, &level) ||
		    !perf_need_msg(b, p, SK_LIU, SKILL_TYPE_WEAPON, 90, name, &level))
			return false;
		cost = level >= 120 ? 400 : 200;
		if (!perf_force(b, p, cost, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 2))
			return false;
		perf_pay(b, p, cost);
		bt_effect_add(b, pf, 7, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		bt_op_attack(b, 0, 1, 0, 0, 0);
		bt_op_attack(b, 0, 1, 0, 0, 0);
		bt_op_attack(b, 0, 0, 0, 0, 0);
		b->pBusy = 3;
		return true;
	}
	case 6: /* 三花聚顶 */
	{
		static const int conflict[] = { 6 };
		if (!perf_need_msg(b, p, SK_SANHUA, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_force(b, p, 350, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 350);
		int steps = level / 20;
		int delay = steps < 8 ? steps : 8;
		int defense = clampi(level / 5 - 5, 0, 255);
		bt_effect_add(b, pf, delay, ST_DEX, p->dex, ST_DEFENSE, p->defense);
		p->dex = (u8)u8_add(p->dex, steps * 2);
		p->defense = (u8)defense;
		bt_perform_start(b, p, pf);
		return true;
	}
	case 7: /* 流星飞掷 */
	{
		static const int conflict[] = { 7 };
		if (p->str < 33)
		{
			bt_fail_msg(b, p, PF_MSG_NOT_STR, NULL, name);
			return false;
		}
		if (!perf_need_msg(b, p, SK_TONGJI, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_PIFENG, SKILL_TYPE_WEAPON, 120, name, &level))
			return false;
		cost = level >= 150 ? 850 : 550;
		if (!perf_force(b, p, cost, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, cost);
		bt_effect_add(b, pf, 9, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);

		int ap = combat_power_raw(player_attack_kf(p, false), p->attack + 15, p->exp);
		int dp = combat_power_raw(npc_kf_level(enemy, SKILL_TYPE_DODGE, 0), enemy->defense, enemy->exp);
		/* perform.s:660-663 feizhi calls get_obj_busy AFTER restoring
		 * obj_flag, which reads the CASTER's busy — and hello.s:20-22 has
		 * already refused any perform while the caster is busy. The ÷3
		 * branch is therefore unreachable in the original; do not add a
		 * busy-target vulnerability here. */
		if (rnd_below((u32)((unsigned)ap + (unsigned)dp)) < (u32)dp)
		{
			bt_push_msg(b, p, PF_MSG_FEIZHI_DODGE);
			b->pBusy = 4;
		}
		else
		{
			bt_push_msg(b, p, PF_MSG_FEIZHI_SHOOT);
			/* perform.s feizhi: perform_temp4 = skill_level is saved before
			 * skill_xxx_power overwrites it, so the damage is 2*(STR+level).
			 * The wound uses the last query_*_skill (the defender's dodge). */
			int damage = 2 * (p->str + level);
			int wound = npc_kf_level(enemy, SKILL_TYPE_DODGE, 0) / 2;
			bt_op_damage(b, 1, damage, wound, "%s 体力 -%d", b->enemyName, damage);
			b->pBusy = 3;
		}
		perf_consume_weapon(b, p);
		return true;
	}
	case 8: /* 红莲出世 */
	{
		static const int conflict[] = { 8 };
		if (!perf_need_msg(b, p, SK_TONGJI, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_force(b, p, 350, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 350);
		int inc = level / 9;
		bt_effect_add(b, pf, clampi(level / 20, 1, 255),
		              ST_ATTACK, p->attack, 0, 0);
		p->attack = (u8)u8_add(p->attack, inc);
		bt_perform_start(b, p, pf);
		b->pBusy = 1;
		return true;
	}
	case 9: /* 雷动九天 */
	{
		static const int conflict[] = { 9 };
		if (!perf_need_msg(b, p, SK_TONGJI, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_force(b, p, 150, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 150);
		int inc = level / 6;
		bt_effect_add(b, pf, clampi(level / 20, 1, 255),
		              ST_STR, p->str, 0, 0);
		p->str = (u8)u8_add(p->str, inc);
		bt_perform_start(b, p, pf);
		return true;
	}
	case 10: /* 忍法影分身 */
	{
		static const int conflict[] = { 10 };
		if (!perf_need_msg(b, p, SK_RENSHU, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_force(b, p, 550, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 550);
		int per = level / 5;
		if (per < 40) per = 30;
		bt_effect_add(b, pf, clampi(level / 20, 1, 255),
		              ST_PER, p->per, ST_KAR, p->kar);
		p->per = (u8)clampi(per, 0, 255);
		p->kar = 255;
		b->pFenshen = true;
		bt_perform_start(b, p, pf);
		return true;
	}
	case 11: /* 忍术烟幕 */
	{
		static const int conflict[] = { 11 };
		if (!perf_need_msg(b, p, SK_RENSHU, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_force(b, p, 300, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		int roll = rnd(clampi(p->mp, 1, 32767));
		if (roll < b->emp / 3)
		{
			b->pBusy = 2;
			bt_push_msg(b, p, PF_MSG_YIANMU_FAIL);
			return true;
		}
		perf_pay(b, p, 300);
		int reduction = level / 8;
		if (reduction < 20) reduction = 20;
		bt_effect_add(b, pf, clampi(level / 20, 1, 255),
		              ST_NPC_ATTACK, b->eAttack, 0, 0);
		b->eAttack = clampi(b->eAttack - reduction, 0, 255);
		bt_refresh_enemy_panel(b);
		bt_perform_start(b, p, pf);
		return true;
	}
	case 12: /* 旋风三连斩 */
	{
		static const int conflict[] = { 13, 12 };
		if (!perf_need_msg(b, p, SK_RENSHU, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_YIDAO, SKILL_TYPE_WEAPON, 90, name, &level) ||
		    !perf_force(b, p, 350, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 2))
			return false;
		perf_pay(b, p, 350);
		bt_effect_add(b, pf, 5, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		bt_op_attack(b, 0, 0, 0, 0, 0);
		bt_op_attack(b, 0, 0, 0, 0, 0);
		bt_op_attack(b, 0, 0, 0, 0, 0);
		b->pBusy = 1;
		return true;
	}
	case 13: /* 迎风一刀斩 */
	{
		static const int conflict[] = { 13 };
		if (!perf_need_msg(b, p, SK_RENSHU, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_YIDAO, SKILL_TYPE_WEAPON, 120, name, &level) ||
		    !perf_force(b, p, 550, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 550);
		bt_effect_add(b, pf, 7, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		bt_op_attack(b, 0, 0, 15, level / 3 + 20, 0);
		b->pBusy = 1;
		return true;
	}
	case 14: /* 缠字诀 */
	{
		static const int conflict[] = { 15, 14 };
		if (b->eBusy > 0)
		{
			bt_fail_msg(b, p, PF_MSG_NPC_BUSY, NULL, NULL);
			return false;
		}
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJI_SWORD, SKILL_TYPE_WEAPON, 90, name, &level) ||
		    !perf_force(b, p, 250, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 2))
			return false;
		perf_pay(b, p, 250);
		bt_effect_add(b, pf, 6, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		u32 roll = rnd_below(p->exp > 0 ? p->exp : 1u);
		if (roll < enemy->exp / 3)
		{
			bt_push_msg(b, p, PF_MSG_CHAN_DODGE);
			b->pBusy = 3;
		}
		else
		{
			bt_push_msg(b, p, PF_MSG_CHAN_SHOOT);
			int busy = rnd(clampi(level / 20, 1, 10)) + 1;
			b->eBusy = clampi(busy, 1, 20);
		}
		return true;
	}
	case 15: /* 连字诀 */
	{
		static const int conflict[] = { 15 };
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 120, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJI_SWORD, SKILL_TYPE_WEAPON, 120, name, &level) ||
		    !perf_force(b, p, 350, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 350);
		int delay = level / 30 + 3;
		int defInc = level / 15;
		bt_effect_add(b, pf, delay, ST_DEFENSE, p->defense, ST_ATTACK, p->attack);
		p->defense = (u8)u8_add(p->defense, defInc);
		p->attack = (u8)u8_add(p->attack, 10);
		bt_perform_start(b, p, pf);
		return true;
	}
	case 16: /* 三环套月 */
	{
		static const int conflict[] = { 15, 16 };
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 180, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJI_SWORD, SKILL_TYPE_WEAPON, 180, name, &level) ||
		    !perf_force(b, p, 400, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 2))
			return false;
		perf_pay(b, p, 400);
		bt_effect_add(b, pf, 6, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		int add = level / 5;
		bt_op_attack(b, 0, 0, 0, add, 0);
		bt_op_attack(b, 0, 0, 0, add, 0);
		bt_op_attack(b, 0, 0, 0, add, 0);
		b->pBusy = 3;
		return true;
	}
	case 17: /* 挤字诀 */
	{
		static const int conflict[] = { 17 };
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 105, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJIQUAN, SKILL_TYPE_HAND, 105, name, &level) ||
		    !perf_force(b, p, 350, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 350);
		bt_perform_start(b, p, pf);
		int roll = rnd(clampi(p->mp, 1, 32767));
		if (roll >= b->emp / 3)
		{
			/* perform.s ji: fp/10 + neili_cost(350) + FORCE_OFF. */
			int drain = p->mp / 10 + 350 + p->force;
			bt_push_msg(b, p, PF_MSG_JI_SHOOT);
			bt_op_mp(b, 1, drain, NULL);
			bt_effect_add(b, pf, 2, 0, 0, 0, 0);
		}
		else if (roll >= b->emp / 5)
		{
			bt_push_msg(b, p, PF_MSG_JI_SHOOT1);
			bt_op_mp(b, 1, 350, NULL);
		}
		else
		{
			bt_push_msg(b, p, PF_MSG_JI_DODGE);
			b->pBusy = rnd(3) + 1;
		}
		return true;
	}
	case 18: /* 乱环诀 */
	{
		static const int conflict[] = { 18 };
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 150, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJIQUAN, SKILL_TYPE_HAND, 150, name, &level) ||
		    !perf_force(b, p, 300, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		/* perform.s luanhuan:1241-1242 checks get_you_busy AFTER
		 * judge_kf/judge_force/find_ptemp. */
		if (b->eBusy > 0)
		{
			bt_fail_msg(b, p, PF_MSG_NPC_BUSY, NULL, NULL);
			return false;
		}
		perf_pay(b, p, 300);
		bt_perform_start(b, p, pf);
		int roll = rnd(clampi(level, 1, 65535));
		int guard = npc_kf_level(enemy, SKILL_TYPE_PARRY, 0) / 3;
		if (roll < guard)
		{
			bt_push_msg(b, p, PF_MSG_LUANHUAN_DODGE);
			b->pBusy = 2;
		}
		else
		{
			bt_push_msg(b, p, PF_MSG_LUANHUAN_SHOOT);
			b->eBusy = roll / 30 + 2;
			bt_effect_add(b, pf, b->eBusy + 4, 0, 0, 0, 0);
		}
		return true;
	}
	case 19: /* 阴阳诀 */
	{
		static const int conflict[] = { 19 };
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 180, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJIQUAN, SKILL_TYPE_HAND, 180, name, &level) ||
		    !perf_force(b, p, 500, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 500);
		bt_perform_start(b, p, pf);
		if (b->eBusy == 0 && rnd(5) == 0)
		{
			bt_push_msg(b, p, PF_MSG_YINYANG_ATTACK);
			int roll = rnd(clampi(level, 1, 65535));
			int guard = npc_kf_level(enemy, SKILL_TYPE_PARRY, 0) / 3;
			if (roll < guard)
			{
				bt_push_msg(b, p, PF_MSG_YINYANG_DODGE);
				b->pBusy = 2;
			}
			else
			{
				bt_push_msg(b, p, PF_MSG_YINYANG_SHOOT);
				b->eBusy = roll / 25 + 2;
				bt_effect_add(b, pf, 5, 0, 0, 0, 0);
			}
		}
		else
		{
			bt_op_attack(b, 0, 0, 15, 0, level / 5);
			bt_effect_add(b, pf, 7, 0, 0, 0, 0);
			b->pBusy = 3;
		}
		return true;
	}
	case 20: /* 震字诀 */
	{
		static const int conflict[] = { 20 };
		if (!perf_need_msg(b, p, SK_TAIJIG, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_need_msg(b, p, SK_TAIJIQUAN, SKILL_TYPE_HAND, 90, name, &level) ||
		    !perf_force(b, p, 200, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 200);
		bt_perform_start(b, p, pf);
		/* perform.s zhen: sub_obj_fp runs before the roll, so p->mp is the
		 * already-paid value. roll >= you_fp/3 is the attack branch; the
		 * damage is fp/10 + FORCE_OFF - you_fp/30 and a borrow means the
		 * attacker is staggered instead. */
		int roll = rnd(clampi(p->mp, 1, 32767));
		if (roll >= b->emp / 3)
		{
			int dmg = p->mp / 10 + p->force - b->emp / 30;
			if (dmg < 0)
			{
				bt_push_msg(b, p, PF_MSG_ZHEN_DODGE);
				b->pBusy = 2;
			}
			else
			{
				bt_push_msg(b, p, PF_MSG_ZHEN_SHOOT);
				bt_op_damage(b, 1, dmg, dmg / 2, "%s 体力 -%d", b->enemyName, dmg);
				bt_effect_add(b, pf, 2, 0, 0, 0, 0);
			}
		}
		else if (roll < b->emp / 4)
		{
			bt_push_msg(b, p, PF_MSG_ZHEN_DODGE1);
			b->pBusy = rnd(3) + 2;
		}
		else
		{
			/* zhen_5: the original loads zhen_shoot_msg1 but omits the
			 * show_text call, so this branch prints nothing. */
			if (b->emp < 200)
				b->emp = 0;
			else
				b->emp = (s16)(b->emp - 100);
		}
		return true;
	}
	case 21: /* 冰心诀 */
	{
		static const int conflict[] = { 21 };
		if (!perf_need_msg(b, p, SK_XUESHAN_FORCE, SKILL_TYPE_FORCE, 75, name, &level))
			return false;
		cost = level >= 90 ? 250 : 150;
		if (!perf_force(b, p, cost, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, cost);
		int add = level / 4;
		if (add > 100) add = 100;
		bt_effect_add(b, pf, clampi(level / 20, 1, 10),
		              ST_ARMOR, p->armor, 0, 0);
		p->armor = (u8)u8_add(p->armor, add);
		bt_perform_start(b, p, pf);
		return true;
	}
	case 22: /* 雪花六出 */
	{
		static const int conflict[] = { 22 };
		if (!perf_need_msg(b, p, SK_XUESHAN_FORCE, SKILL_TYPE_FORCE, 90, name, &level) ||
		    !perf_need_msg(b, p, SK_XUESHAN_SWORD, SKILL_TYPE_WEAPON, 90, name, &level))
			return false;
		cost = 250 + ((level - 90) / 30) * 150;
		if (cost > 600) cost = 600;
		if (!perf_force(b, p, cost, name))
			return false;
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, cost);
		int hits = (level - 90) / 30 + 2;
		if (hits > 5) hits = 5;
		bt_effect_add(b, pf, 10, 0, 0, 0, 0);
		bt_perform_start(b, p, pf);
		for (int i = 0; i < hits; i++)
			bt_op_attack(b, 0, 0, 10, 0, 0);
		b->pBusy = 3;
		return true;
	}
	case 23: /* 神倒鬼跌三连环 */
	{
		static const int conflict[] = { 23 };
		if (!perf_need_msg(b, p, SK_XUEYING, SKILL_TYPE_HAND, 120, name, &level) ||
		    !perf_force(b, p, 350, name))
			return false;
		/* perform.s shengui:1922-1923 checks get_you_busy between
		 * judge_force and find_ptemp. */
		if (b->eBusy > 0)
		{
			bt_fail_msg(b, p, PF_MSG_NPC_BUSY, NULL, NULL);
			return false;
		}
		if (perf_conflict(b, p, pf, conflict, 1))
			return false;
		perf_pay(b, p, 350);
		bt_perform_start(b, p, pf);
		int roll = rnd(clampi(p->mp, 1, 32767));
		if (roll < b->emp / 2)
		{
			bt_push_msg(b, p, PF_MSG_SHENGUI_DODGE);
			b->pBusy = 2;
		}
		else
		{
			bt_push_msg(b, p, PF_MSG_SHENGUI_SHOOT);
			b->eBusy = rnd(clampi(level / 35, 1, 10)) + 3;
			/* shengui only calls sub_you_hp, no effhp wound. */
			bt_op_damage(b, 1, level / 3, 0, "%s 体力 -%d", b->enemyName, level / 3);
		}
		bt_effect_add(b, pf, 5, 0, 0, 0, 0);
		return true;
	}
	default:
		battle_log(b, "「%s」的规则尚未接入。", name);
		return false;
	}
}

/* Only the current turn's line is kept — the original fight screen shows one
 * description at a time (write_fight_screen draws a single msg row). */
static void battle_log(Battle *b, const char *fmt, ...)
{
	if (!b) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b->lastDesc, BATTLE_LOG_LEN, fmt, ap);
	va_end(ap);
}

static const char *const CMD_LABELS[BT_CMD_COUNT] =
{
	"攻击", "绝招", "吸气", "物品", "逃跑"
};

bool battle_start(Battle *b, Player *p, int npcId, u16 portraitTile)
{
	const NpcInfo *n = npc_info(npcId);
	/* 3000 guards against corrupt pack records; the scaled ghost may exceed it. */
	if (!b || !p || !n || n->hpMax == 0 || (n->hpMax > 3000 && npcId != 125))
		return false;

	memset(b, 0, sizeof(*b));
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
		b->effects[i].id = 0xFF;
	/* No reseed here: math.s keeps one global mud_seed for the session. */
	b->active = true;
	b->npcId = npcId;
	b->portraitTile = portraitTile;
	b->phase = BT_PHASE_FIGHT;
	b->turn = 1;
	b->escapeFactor = 20;
	b->subIndex = 0;
	b->subPage = 0;
	b->menuIndex = 0;
	snprintf(b->enemyName, sizeof(b->enemyName), "%s", n->name);

	b->ehp = b->ehpMax = (s16)n->hpMax;
	/* npc_data.gb: all 127 records have effhp == maxhp. */
	b->eEffhp = b->ehpMax;
	b->empMax = (s16)n->mpMax;
	b->emp = (s16)(n->mp > n->mpMax ? n->mpMax : n->mp);
	if (b->emp > b->empMax)
		b->emp = b->empMax;

	b->eWeapon = (n->equippedFlags & 1) ? n->weapon : 0;
	b->eAttack = n->attack;
	b->eDamage = n->damage;
	b->eArmor = n->armor;
	b->eStr = npc_str(n);
	b->eDex = npc_dex(n);
	b->eIntel = n->intel;
	b->eCon = n->con;
	b->ePer = n->per;
	battle_stats(b, p);

	battle_log(b, "%s 挡住了去路，战斗开始！", b->enemyName);

	/* fight.s:128-134: 第一回合先手直接取 getms()&1，不消耗 PRNG. */
	if (rng_getms() & 1)
	{
		bt_op_attack(b, 1, 0, 0, 0, 0);
		b->afterSeq = BT_AFTER_END;
		b->tickEnd = false;
		/* Give the intro line one message hold before the first beat. */
		b->lock = BT_MSG_HOLD;
	}
	return true;
}

/* fight.s attack_xxx prints the action description (get_kf_action) first and
 * only then resolves dodge/parry/damage, which produce a second message.
 * Keeping the two beats apart matches that order and stops a long technique
 * line from being merged with the result line (the merged line needed five
 * or more rows and was clipped). */
static bool bt_run_attack(Battle *b, Player *p, BattleOp *op)
{
	if (op->stage == 0)
	{
		/* fight.s attack_npc / attack_man: limb_flag = random_it(16) once at
		 * the start of the attack, shared by the move and dodge messages. */
		fight_set_limb(rnd(16));
		b->curUnarmed = op->unarmed;
		b->curAtkAdd = op->apAdd;
		b->curDmgAdd = op->dmgAdd;
		b->curStrAdd = op->strAdd;
		char desc[FT_RENDER_MAX];
		build_attack_desc(b, p, op->attacker == 0, desc, sizeof(desc));
		battle_log(b, "%s", desc);
		op->stage = 1;
		return false;
	}
	battle_attack(b, p, op->attacker == 0);
	b->curUnarmed = 0;
	b->curAtkAdd = b->curDmgAdd = b->curStrAdd = 0;
	return true;
}

/* Runs one beat. false = the op shows its description now and resolves on
 * the next beat (BT_OP_ATTACK). */
static bool bt_run_op(Battle *b, Player *p, BattleOp *op)
{
	switch (op->kind)
	{
	case BT_OP_ATTACK:
		if (b->phase != BT_PHASE_FIGHT)
			return true;
		return bt_run_attack(b, p, op);
	case BT_OP_DAMAGE:
		if (op->value > 0)
			audio_play(op->target == 1 ? SFX_CRIT : SFX_HURT);
		if (op->target == 1)
		{
			b->ehp = (s16)clampi(b->ehp - op->value, 0, 32767);
			b->eEffhp = (s16)clampi((int)b->eEffhp - op->wound, 0, 32767);
			b->flashSide = 2;
			b->flashDamage = op->value;
			b->flashCrit = 0;
			if (op->value > 0) b->flashTimer = BT_FLASH;
			if (b->ehp <= 0)
			{
				/* fight.s: BOSS_PAI skips the 杀/放 prompt. */
				const NpcInfo *en = npc_info(b->npcId);
				if (en && en->pai == 8)
					battle_win(b, p, true);
				else
					b->phase = BT_PHASE_MERCY;
			}
		}
		else
		{
			p->hp = (s16)clampi(p->hp - op->value, 0, 32767);
			p->effhp = (s16)clampi((int)p->effhp - op->wound, 0, 32767);
			b->flashSide = 1;
			b->flashDamage = op->value;
			b->flashCrit = 0;
			if (op->value > 0) b->flashTimer = BT_FLASH;
			if (p->hp <= 0)
			{
				b->result = 0;
				b->phase = BT_PHASE_LOSE;
				const NpcInfo *n = npc_info(b->npcId);
				b->fatal = !n || n->morality <= 0 || p->morality >= 0;
				/* fight.s who_win/no_kill: a spared defeat leaves man_hp at
				 * 0; natural regen refills it. */
				if (!b->fatal) p->hp = 0;
				p->losses++;
			}
		}
		if (op->text[0])
			battle_log(b, "%s", op->text);
		break;
	case BT_OP_MP:
		if (op->target == 1)
			b->emp = (s16)clampi(b->emp - op->value, 0, 32767);
		else
			p->mp = (s16)clampi(p->mp - op->value, 0, 32767);
		if (op->text[0])
			battle_log(b, "%s", op->text);
		break;
	case BT_OP_LOG:
	default:
		if (op->text[0])
			battle_log(b, "%s", op->text);
		break;
	}
	return true;
}

static void bt_start_enemy_action(Battle *b)
{
	if (b->eBusy > 0)
	{
		b->eBusy--;
		battle_log(b, "%s一时无法行动。", b->enemyName);
		b->afterSeq = BT_AFTER_END;
		b->lock = BT_MSG_HOLD;
		return;
	}
	/* No pre-op lock: the attack description should appear as soon as the
	 * previous message's hold ends. The op itself holds the message. */
	bt_op_attack(b, 1, 0, 0, 0, 0);
	b->afterSeq = BT_AFTER_END;
}

static void bt_end_round(Battle *b, Player *p)
{
	if (b->phase == BT_PHASE_FIGHT)
	{
		if (b->tickEnd)
			bt_effects_tick(b, p);
		b->turn++;
	}
	b->tickEnd = false;
	bt_ops_reset(b);
}

void battle_update(Battle *b, Player *p, float dt)
{
	if (!b->active)
		return;
	if (b->lock > 0.0f)
	{
		b->lock -= dt;
		if (b->lock < 0.0f)
			b->lock = 0.0f;
	}
	if (b->flashTimer > 0.0f)
	{
		b->flashTimer -= dt;
		if (b->flashTimer < 0.0f)
			b->flashTimer = 0.0f;
	}

	if (b->opIndex < b->opCount && b->lock <= 0.0f &&
	    b->phase != BT_PHASE_MERCY && b->phase != BT_PHASE_WIN &&
	    b->phase != BT_PHASE_LOSE && b->phase != BT_PHASE_ESCAPED)
	{
		BattleOp *op = &b->ops[b->opIndex];
		if (bt_run_op(b, p, op))
			b->opIndex++;
		b->lock = op->hold;
		if (b->phase != BT_PHASE_FIGHT)
			b->opIndex = b->opCount;
		return;
	}
	if (b->opIndex >= b->opCount && b->opCount > 0)
	{
		b->opCount = 0;
		b->opIndex = 0;
	}
	if (b->lock > 0.0f)
		return;

	if (b->afterSeq == BT_AFTER_ENEMY)
	{
		if (b->phase != BT_PHASE_FIGHT)
		{
			b->afterSeq = BT_AFTER_END;
			return;
		}
		bt_start_enemy_action(b);
		return;
	}
	if (b->afterSeq == BT_AFTER_END)
	{
		bt_end_round(b, p);
		return;
	}
}


/* ------------------------------------------------------------------ resolution */

static void battle_win(Battle *b, Player *p, bool kill)
{
	const NpcInfo *n = npc_info(b->npcId);
	bt_effects_clear(b, p);
	b->phase = BT_PHASE_WIN;
	b->result = 1;
	b->enemyKilled = kill;
	audio_play(b->sparring ? SFX_COIN : SFX_VICTORY);
	/* npc.s get_npc_goods / fight.s killer: a normal victory pays money and
	 * the first four goods only — NO experience or potential (npc_exp is a
	 * task-sorting table). BOSS_PAI skips 战利品; npc.s runs get_npc_goods
	 * for both 杀死 (3) and 放过 (1), so loot is not gated on the kill. */
	/* The drop money is scaled x15 (intentional deviation, include/reward.h);
	 * the four goods stay as they are - 15 copies would fill the bag. */
	b->moneyReward = (n && n->pai != 8) ? reward_scale(n->money) : 0;
	p->money += b->moneyReward;
	if (n && n->pai != 8)
	{
		int goods[] = { n->weapon, n->equip, n->thirdEquip & 0x7f, n->fourthGoods & 0x7f };
		for (int i = 0; i < 4; i++)
		{
			if (goods[i] == ITEM_SANJIAO)   /* SANJIAO */
			{
				u8 bit = n->pai >= 1 && n->pai <= 6 ? 1u << (n->pai - 1) : 0;
				if (p->stones & bit) continue;
				p->stones |= bit;
			}
			if (goods[i] && inv_add(p->bag, goods[i]) < 0)
				log_add("行囊已满，未能取得「%s」。", item_name(goods[i]));
		}
	}
	if (kill && n && n->pai != 8)
	{
		/* fight.s killer: KILLER_NPC -> guan_kill, others -> npc_kill.
		 * BOSS_PAI skips the kill prompt, so it is not counted here. */
		u16 *counter = b->npcId == 125 ? &p->guanKill : &p->npcKill;
		if (*counter < 65535)
			(*counter)++;
	}
	if (kill && b->npcId != 125 && b->npcId < 128)
		p->deadNpc[b->npcId / 8] |= 1u << (b->npcId % 8);
	if (kill && n)
	{
		/* The original tests the high bit but performs unsigned saturated
		 * arithmetic on the stored byte; 0x80 is the neutral starting point. */
		int moral = (u8)p->morality;
		if (n->morality > 0)
			moral -= n->morality * (moral >= 128 ? 2 : 1);
		else if (n->morality < 0 && moral >= 128)
			moral -= n->morality;
		p->morality = (s8)(u8)clampi(moral, 0, 255);
	}
	p->wins++;
	if (kill)
	{
		/* fight.s kill_npc dead_talk: random_it(5); indexes 0/1 are forced
		 * to the NPC's gender, 2..4 are the shared pool. */
		static const char *const deadTalk[] = {
			"二十年后又是一条好汉！",
			"啊-----！",
			"可恶！我怎么会死呢。",
			"我会在地狱里等着你的。",
			"有的人活着，他已经死了，有...哎，我还没说完...",
		};
		int idx = rnd(5);
		if (idx < 2 && n)
			idx = n->gender & 1;
		battle_log(b, "%s", deadTalk[idx]);
	}
	else
	{
		/* fight.s man_msg7 (放过). */
		battle_log(b, "..............");
	}
	if (b->moneyReward)
		log_add("战胜%s，得到文钱 %lu。", b->enemyName, (unsigned long)b->moneyReward);
	else
		log_add("战胜%s。", b->enemyName);
}

static void battle_lose(Battle *b, Player *p)
{
	bt_effects_clear(b, p);
	b->phase = BT_PHASE_LOSE;
	b->result = 0;
	const NpcInfo *n = npc_info(b->npcId);
	b->fatal = !n || n->morality <= 0 || p->morality >= 0;
	/* fight.s no_kill: the spared player walks out at hp 0. */
	if (!b->fatal) p->hp = 0;
	p->losses++;
	battle_log(b, "你眼前一黑，倒了下去……");
	log_add("败给了%s。", b->enemyName);
	audio_play(SFX_DEFEAT);
}

/* text_addr.s basic templates carry the damage_type byte that string.s
 * is_basic_msg stores globally. Kungfu move descriptions (kf_data.gb) do
 * not, so the previous type survives — see Battle.damageType. */
static const char *bt_pick_template(Battle *b, int base, int count)
{
	int id = base + rnd(count);
	int attr = fight_template_attr(id);
	if (attr != 255)
		b->damageType = attr;
	return fight_template(id);
}

/* fight.s attack strings: an enabled kungfu renders one of its move
 * descriptions (kf_data.gb), otherwise the weapon-type template table from
 * text_addr.s, or the bare-hand table. Placeholders follow the original
 * format_string: $N attacker, $n defender, $l/$1 body part, $w weapon. */
static void build_attack_desc(Battle *b, const Player *p, bool fromPlayer,
                              char *out, int size)
{
	const char *atk = fromPlayer ? "你" : b->enemyName;
	const char *def = fromPlayer ? b->enemyName : "你";
	const NpcInfo *enemy = npc_info(b->npcId);
	int weapon = fromPlayer ? (b->curUnarmed ? 0 : (p->weapon & 0x7f))
	                        : b->eWeapon;
	if (!fromPlayer && b->eBusy > 0)
	{
		snprintf(out,(size_t)size,"%s暂时无法行动",atk);
		return;
	}
	b->moveAp = b->moveDamage = b->moveForce = 0;
	if (fromPlayer) { b->pMoveDp = b->pMovePp = 0; }
	else { b->eMoveDp = b->eMovePp = 0; }

	int kfId = -1;
	if (fromPlayer)
		kfId = player_attack_kf_id(p, b->curUnarmed != 0);
	else if (enemy)
		kfId = npc_attack_kf_id(b, enemy);

	if (kfId >= SK_BAGUAD && fight_kf_moves(kfId) > 0)
	{
		int level = fromPlayer ? skill_level(p, kfId) : 0;
		const NpcInfo *n = npc_info(b->npcId);
		if (!fromPlayer) for (int j = 0; j < n->kfCount; j++)
			if (n->kf[j * 2] == kfId) level = n->kf[j * 2 + 1];
		int count = fight_kf_moves(kfId), eligible = 0;
		for (int j = 0; j < count; j++)
		{
			int min, ap, dp, pp, damage, force;
			if (!fight_move_stats(kfId, j, &min, &ap, &dp, &pp, &damage, &force) || min > level) break;
			eligible++;
		}
		if (eligible > 0)
		{
			int i = rnd(eligible);
			int min, dp, pp;
			fight_move_stats(kfId, i, &min, &b->moveAp, &dp, &pp, &b->moveDamage, &b->moveForce);
			if (fromPlayer) { b->pMoveDp = dp; b->pMovePp = pp; }
			else { b->eMoveDp = dp; b->eMovePp = pp; }
			fight_render(out, size, fight_kf_move(kfId, i), atk, def,
			             weapon ? item_name(weapon) : NULL);
			return;
		}
	}

	if (weapon > 0)
	{
		const ItemDef *d = item_get(weapon);
		int base = d ? fight_weapon_base(d->sub) : 0;
		int cnt = base > 0 ? fight_template_count(base) : 0;
		if (cnt > 0)
		{
			fight_render(out, size, bt_pick_template(b, base, cnt),
			             atk, def, item_name(weapon));
			return;
		}
	}

	int cnt = fight_template_count(FT_BASE_BARE);
	if (cnt > 0)
		fight_render(out, size, bt_pick_template(b, FT_BASE_BARE, cnt),
		             atk, def, NULL);
	else
		snprintf(out, (size_t)size, "%s发起攻击", atk);
}

/* string2.s get_result_msg: "结果"+damage_msg+"($n"+status_msg+")".
 * fight.s:718-743 / 855-878 pass percent(remaining hp/effhp, maxhp) and the
 * template's damage_type; a wound selects both effhp and eff_status_tbl.
 * The numeric "体力 -N" readout is the 3DS UI's own addition.
 * The action description was already shown as the previous beat, exactly like
 * fight.s prints it before the hit calculation. */
static void bt_log_hit(Battle *b, const char *defName,
                       int damage, int wound, int currentHp, int maxHp,
                       bool fromPlayer, const char *tag)
{
	char result[FT_RENDER_MAX];
	char line[FT_RENDER_MAX];
	fight_damage_result(result, sizeof(result), b->damageType, damage, wound,
	                    currentHp, maxHp);
	fight_render(line, sizeof(line), result, NULL, defName, NULL);
	if (fromPlayer)
		battle_log(b, "%s 体力 -%d。%s%s", defName, damage, line, tag);
	else
		battle_log(b, "你 体力 -%d。%s%s", damage, line, tag);
}

static int battle_attack(Battle *b, Player *p, bool fromPlayer)
{
	const NpcInfo *n = npc_info(b->npcId);
	const char *actorName = fromPlayer ? "你" : b->enemyName;
	const char *defName = fromPlayer ? b->enemyName : "你";

	int atk = fromPlayer
	        ? combat_power(player_attack_kf(p, b->curUnarmed != 0),
	                       p->attack + b->moveAp + b->curAtkAdd, p->exp, p->dex)
	        : combat_power(npc_attack_level(b, n), b->eAttack + b->moveAp,
	                       n->exp, b->eDex);
	if (fromPlayer) b->pHit = atk;
	else b->eHit = atk;
	if (fromPlayer) b->moveAp = 0;
	int dodge = fromPlayer
	          ? combat_power(npc_kf_level(n, SKILL_TYPE_DODGE, 0), n->defense + b->eMoveDp, n->exp, b->eIntel)
	          : combat_power(skill_query_level(p, SKILL_TYPE_DODGE), p->defense + b->pMoveDp, p->exp, p->con);
	int parry = fromPlayer
	          ? combat_power(npc_kf_level(n, SKILL_TYPE_PARRY, 0), n->defense + b->eMovePp, n->exp, b->eStr)
	          : combat_power(skill_query_level(p, SKILL_TYPE_PARRY), p->defense + b->pMovePp, p->exp, p->str);
	/* npc_busy only divides DP/PP by 3; it never cancels the player's hit.
	 * The busy counter is consumed by the NPC's own skipped action. */
	if (fromPlayer && b->eBusy > 0) { dodge /= 3; parry /= 3; }
	if (!fromPlayer && b->pBusy > 0) { dodge /= 3; parry /= 3; }

	/* Dodge consumes its modifier even on a miss. Parry is reached only if
	 * dodge fails, so its modifier survives a successful dodge. */
	if (fromPlayer) b->eMoveDp = 0;
	else b->pMoveDp = 0;
	unsigned int atkRoll = (unsigned int)clampi(atk, 0, 1000000000);
	unsigned int dodgeRoll = (unsigned int)clampi(dodge, 0, 1000000000);
	unsigned int total = atkRoll + dodgeRoll;
	if (total == 0) total = 1;
	bool avoided = rnd_below(total) < dodgeRoll;
	if (!avoided)
	{
		if (fromPlayer) b->eMovePp = 0;
		else b->pMovePp = 0;
		unsigned int parryRoll = (unsigned int)clampi(parry, 0, 1000000000);
		total = atkRoll + parryRoll;
		if (total == 0) total = 1;
		avoided = rnd_below(total) < parryRoll;
	}
	if (avoided)
	{
		b->moveDamage = b->moveForce = 0;
		/* dodge_msg_tbl: "但是和$n$l偏了几寸" etc., $n = the defender */
		char dodgeBuf[FT_RENDER_MAX];
		int dc = fight_dodge_count();
		if (dc > 0)
		{
			fight_render(dodgeBuf, sizeof(dodgeBuf), fight_dodge(rnd(dc)),
			             actorName, defName, NULL);
			battle_log(b, "%s", dodgeBuf);
		}
		else
			battle_log(b, "但被%s轻轻闪过。", defName);
		return 0;
	}

	/* fight.s man_fenshen: a shadow clone may negate the hit entirely. */
	if (!fromPlayer && b->pFenshen && rnd(100) < p->per)
	{
		battle_log(b, "%s没料到这一击却打了个空，原来打中的只是个影子！", actorName);
		return 0;
	}

	/* Damage field is the weapon bonus only; unwielding (刀影掌/柳浪)
	 * removes it, and 三环/迎风 add their temporary damage here. */
	int power = fromPlayer
	          ? (b->curUnarmed ? b->curDmgAdd : (int)p->damage + b->curDmgAdd)
	          : n->damage;
	int armor = fromPlayer ? b->eArmor : b->pArmor;
	/* fight.s cal_damage_bonus merges the jiali charge before the
	 * armor comparison; enemies carry no charge. */
	int charge = fromPlayer ? p->force : n->force;
	int fp = fromPlayer ? p->mp : b->emp;
	if (charge > fp) charge = fp;
	if (fromPlayer) p->mp = (s16)(p->mp - charge);
	else b->emp = (s16)(b->emp - charge);
	bool hasWeapon = fromPlayer ? (!b->curUnarmed && p->weapon != 0)
	                            : b->eWeapon != 0;
	int bonus = fromPlayer ? (int)p->str + b->curStrAdd : b->eStr;
	if (charge > 0)
	{
		int jiali = (hasWeapon ? charge / 6 : charge) + (fp > 3000 ? 3000 : fp) / 20 - (fromPlayer ? n->force : p->force) / 25;
		if (jiali >= 0)
		{
			bonus += jiali;
			bonus += bonus * b->moveForce / 100;
		}
	}
	else
		bonus += bonus * b->moveForce / 100;
	int base = (power + rnd(power)) / 2;
	base += base * b->moveDamage / 100;
	int damage = base + (bonus + rnd(bonus)) / 2;
	if (damage < 1)
		damage = 1;

	/* fight.s cal_damage: random_it(damage_point) leaves its low byte in A,
	 * and `cmp a8` compares that byte with the 8-bit ARMOR_OFF. Only a
	 * weapon guarantees the wound once that roll succeeds; bare hands get
	 * one chance in four. The wound lowers effhp, not hp. */
	int wound = 0;
	int armorByte = clampi(armor, 0, 255);
	if ((rnd(damage) & 0xFF) > armorByte && (hasWeapon || rnd(4) == 0))
		wound = damage - armorByte;
	if (wound < 0)
		wound = 0;
	b->moveDamage = b->moveForce = 0;

	if (fromPlayer)
	{
		/* fight.s attack_npc: hp -= damage_point, effhp -= wound. */
		b->ehp = (s16)(b->ehp - damage > 0 ? b->ehp - damage : 0);
		b->eEffhp = (s16)clampi((int)b->eEffhp - wound, 0, 32767);
		b->flashSide = 2;
		char tag[24] = "";
		if (charge > 0)
			snprintf(tag, sizeof(tag), "（加力%d）", charge);
		bt_log_hit(b, defName, damage, wound,
		           wound > 0 ? (int)b->eEffhp : (int)b->ehp,
		           (int)b->ehpMax, true, tag);

		/* 吸星大法 heals the attacker by level*damage/100 (fight.s XI_KF). */
		int xi = skill_level(p, SK_XI);
		if (xi > 0)
		{
			int heal = xi * damage / 100;
			p->hp = (s16)clampi((int)p->hp + heal, 0, p->effhp);
		}
	}
	else
	{
		p->hp = (s16)(p->hp - damage > 0 ? p->hp - damage : 0);
		/* Only a wound reduces the long-term injury cap. */
		p->effhp = (s16)(p->effhp - wound > 0 ? p->effhp - wound : 0);
		b->flashSide = 1;
		bt_log_hit(b, defName, damage, wound,
		           wound > 0 ? (int)p->effhp : (int)p->hp,
		           (int)p->hpMax, false, "");

		int xi = npc_basic_level(n, SK_XI);
		if (xi > 0)
		{
			int heal = xi * damage / 100;
			b->ehp = (s16)clampi((int)b->ehp + heal, 0, b->eEffhp);
		}
	}
	b->flashDamage = damage;
	b->flashCrit = wound > 0 ? 1 : 0;
	b->flashTimer = BT_FLASH;

	/* Sound feedback: a wound (伤势) reads as a critical hit, and a hit on
	 * the player gets its own, duller impact. */
	if (fromPlayer)
		audio_play(wound > 0 ? SFX_CRIT : SFX_HIT);
	else
		audio_play(SFX_HURT);

	if (fromPlayer && b->ehp <= 0)
	{
		const NpcInfo *en = npc_info(b->npcId);
		if (en && en->pai == 8)
			battle_win(b, p, true);
		else
			b->phase = BT_PHASE_MERCY;
	}
	else if (!fromPlayer && p->hp <= 0)
		battle_lose(b, p);
	return damage;
}

/* ------------------------------------------------------------------- commands */

/* lee1.s recover: a free action in the original fight menu — it does not
 * give the opponent a turn. Returns true when it actually healed. */
static bool battle_recover(Battle *b, Player *p)
{
	/* perform.s recover and lee1.s recover are the same routine. Use the
	 * single implementation in neili.c so the field and battle commands
	 * cannot drift apart. */
	char msg[160];
	bool ok = neili_recover(p, msg, sizeof(msg));
	battle_log(b, "%s", msg);
	if (ok)
		audio_play(SFX_HEAL);
	return ok;
}

static void battle_escape(Battle *b, Player *p)
{
	const NpcInfo *n = npc_info(b->npcId);
	/* fight.s escape_action: BOSS_PAI returns immediately — no roll, no
	 * message and no enemy turn. */
	if (n && n->pai == 8)
		return;

	/* fight.s if_escape: escape_factor and man_dex+factor are both 8-bit
	 * values, so repeated failures wrap the range back through zero. */
	if (b->pBusy > 0)
	{
		b->escapeFactor = (b->escapeFactor + 10) & 0xFF;
		battle_log(b, "你现在动弹不得！");
		b->tickEnd = false;
		b->afterSeq = BT_AFTER_ENEMY;
		b->lock = BT_MSG_HOLD;
		return;
	}
	int range = (p->dex + b->escapeFactor) & 0xFF;
	if (rnd(range) > b->eDex)
	{
		bt_effects_clear(b, p);
		b->phase = BT_PHASE_ESCAPED;
		b->result = -1;
		battle_log(b, "你趁隙抽身，脱出了战斗。");
		log_add("从%s面前脱身。", b->enemyName);
		return;
	}
	b->escapeFactor = (b->escapeFactor + 10) & 0xFF;
	battle_log(b, "没能脱身，%s 紧追不舍。", b->enemyName);
	b->tickEnd = false;
	b->afterSeq = BT_AFTER_ENEMY;
	b->lock = BT_MSG_HOLD;
}

static void battle_command(Battle *b, Player *p, int cmd)
{
	if (!b->active || b->phase != BT_PHASE_FIGHT || b->lock > 0.0f)
		return;

	switch (cmd)
	{
	case BT_CMD_ATTACK:
		/* normal_attack calls call_out before attack_npc, even when the
		 * player is busy and the attack is skipped. */
		bt_effects_tick(b, p);
		if (b->pBusy > 0)
		{
			b->pBusy--;
			battle_log(b, "你现在呆若木鸡！");
			b->tickEnd = false;
			b->afterSeq = BT_AFTER_ENEMY;
			b->lock = BT_MSG_HOLD;
			return;
		}
		bt_op_attack(b, 0, 0, 0, 0, 0);
		b->tickEnd = false;
		b->afterSeq = BT_AFTER_ENEMY;
		/* No lock: the op shows its attack description immediately. */
		break;
	case BT_CMD_PERFORM:
	{
		int list[BT_PERFORM_MAX];
		if (skill_perform_list(p, list, BT_PERFORM_MAX) == 0)
		{
			battle_log(b, "先启用有绝招的功夫，才能使出绝招。");
			return;
		}
		b->phase = BT_PHASE_PERFORM;
		b->subIndex = 0;
		b->subPage = 0;
		break;
	}
	case BT_CMD_HEAL:
		/* 吸气 is a free action in fight.s (no attack_man), but lee1.s
		 * still shows its message with the same 400 ms + 960 ms wait. */
		if (battle_recover(b, p))
			b->lock = BT_MSG_HOLD;
		break;
	case BT_CMD_ITEM:
	{
		int slots[BAG_SLOTS];
		if (battle_item_slots(p, slots, BAG_SLOTS) == 0)
		{
			battle_log(b, "行囊里没有可用的物品。");
			return;
		}
		b->phase = BT_PHASE_ITEM;
		b->subIndex = 0;
		b->subPage = 0;
		break;
	}
	case BT_CMD_ESCAPE:
		battle_escape(b, p);
		break;
	default:
		break;
	}
}

static int battle_item_slots(const Player *p, int *slots, int max)
{
	int n = 0;
	for (int i = 0; i < BAG_SLOTS && n < max; i++)
	{
		int id = BAG_ID(p->bag[i]);
		/* goods.s set_all_goods: the fight goods menu lists equipped goods
		 * too (use_weapon/use_equip unwield them); only the 三角石板 is
		 * removed from the list. */
		if (id == 0 || id == ITEM_SANJIAO)
			continue;
		slots[n++] = i;
	}
	return n;
}

static int battle_sub_count(const Battle *b, const Player *p)
{
	if (b->phase == BT_PHASE_PERFORM)
	{
		int list[BT_PERFORM_MAX];
		return skill_perform_list(p, list, BT_PERFORM_MAX);
	}
	int slots[BAG_SLOTS];
	return battle_item_slots(p, slots, BAG_SLOTS);
}

static void battle_sub_activate(Battle *b, Player *p, int index)
{
	int count = battle_sub_count(b, p);
	if (index < 0 || index >= count)
		return;

	if (b->phase == BT_PHASE_PERFORM)
	{
		int list[BT_PERFORM_MAX];
		int n = skill_perform_list(p, list, BT_PERFORM_MAX);
		if (index >= n)
			return;
		int pf = list[index];
		const PerformDef *d = perform_get(pf);
		if (!d)
			return;
		if (!battle_perform(b, p, pf))
		{
			/* fight.s:194 a failed perform pops back to the five main
			 * commands, not the perform list. */
			b->phase = BT_PHASE_FIGHT;
			b->lock = BT_MSG_HOLD;   /* keep the fail message readable */
			return;
		}
		b->phase = BT_PHASE_FIGHT;
		b->tickEnd = true;
		b->afterSeq = BT_AFTER_ENEMY;
		/* No lock: the queued perform messages hold themselves. */
	}
	else
	{
		int slots[BAG_SLOTS];
		int n = battle_item_slots(p, slots, BAG_SLOTS);
		if (index >= n)
			return;
		int slot = slots[index];
		int id = BAG_ID(p->bag[slot]);
		const ItemDef *d = item_get(id);
		const char *name = item_name(id);
		b->phase = BT_PHASE_FIGHT;
		/* goods_action is a free action in fight.s: no enemy turn. The
		 * result line still needs a readable hold. */
		b->lock = BT_MSG_HOLD;

		if (d && d->type == ITEM_BOOK)
		{
			/* use_book -> study_cmd. main.c opens the 研读 page and
			 * returns to this battle afterwards; the book is not consumed. */
			b->pendingBook = id;
			battle_log(b, "你翻开「%s」，静心研读。", name);
			return;
		}
		if (d && (d->type == ITEM_WEAPON || d->type == ITEM_EQUIP))
		{
			/* use_goods dispatches weapon/armour to use_weapon/use_equip;
			 * on an already-equipped entry that unwields it. */
			bool wasEquipped = BAG_EQUIPPED(p->bag[slot]);
			if (actor_equip_item(p, slot))
				battle_log(b, wasEquipped ? "你卸下了「%s」。" : "你装备了「%s」。",
				           name);
			else
				battle_log(b, "「%s」现在装备不上。", name);
			return;
		}
		if (!actor_use_item(p, slot))
			battle_log(b, "「%s」此刻没有效果。", name);
		else
			battle_log(b, "你用掉了「%s」。", name);
	}
}

static void battle_acknowledge(Battle *b, Player *p)
{
	bt_effects_clear(b, p);
	b->active = false;
}

/* ---------------------------------------------------------------------- input */

void battle_input_key(Battle *b, Player *p, u32 down)
{
	if (!b->active)
		return;
	if (b->phase == BT_PHASE_MERCY)
	{
		if (b->opCount > 0 || b->afterSeq != BT_AFTER_NONE)
			return;
		/* 擂台 sparring: the opponent is always let go. */
		if (b->sparring)
		{
			if (down & (KEY_A | KEY_B))
				battle_win(b, p, false);
			return;
		}
		if (down & KEY_A) battle_win(b, p, true);
		else if (down & KEY_B) battle_win(b, p, false);
		return;
	}

	if (b->phase == BT_PHASE_WIN || b->phase == BT_PHASE_LOSE ||
	    b->phase == BT_PHASE_ESCAPED)
	{
		if (down & (KEY_A | KEY_B))
			battle_acknowledge(b, p);
		return;
	}
	if (b->lock > 0.0f || b->opCount > 0 || b->afterSeq != BT_AFTER_NONE)
		return;

	if (b->phase == BT_PHASE_FIGHT)
	{
		if (down & (KEY_LEFT | KEY_UP))
			b->menuIndex = (b->menuIndex + BT_CMD_COUNT - 1) % BT_CMD_COUNT;
		if (down & (KEY_RIGHT | KEY_DOWN))
			b->menuIndex = (b->menuIndex + 1) % BT_CMD_COUNT;
		if (down & KEY_A)
			battle_command(b, p, b->menuIndex);
		return;
	}

	int count = battle_sub_count(b, p);
	if (count <= 0)
	{
		b->phase = BT_PHASE_FIGHT;
		return;
	}
	int pages = (count + BT_SUB_ROWS - 1) / BT_SUB_ROWS;
	if (down & KEY_LEFT)
	{
		b->subPage = (b->subPage + pages - 1) % pages;
		b->subIndex = 0;
	}
	if (down & KEY_RIGHT)
	{
		b->subPage = (b->subPage + 1) % pages;
		b->subIndex = 0;
	}
	if (down & KEY_UP)
	{
		int i = b->subPage * BT_SUB_ROWS + b->subIndex;
		i = (i + count - 1) % count;
		b->subPage = i / BT_SUB_ROWS;
		b->subIndex = i % BT_SUB_ROWS;
	}
	if (down & KEY_DOWN)
	{
		int i = b->subPage * BT_SUB_ROWS + b->subIndex;
		i = (i + 1) % count;
		b->subPage = i / BT_SUB_ROWS;
		b->subIndex = i % BT_SUB_ROWS;
	}
	if (down & KEY_A)
		battle_sub_activate(b, p, b->subPage * BT_SUB_ROWS + b->subIndex);
	if (down & KEY_B)
	{
		b->phase = BT_PHASE_FIGHT;
		b->subIndex = 0;
		b->subPage = 0;
	}
}

void battle_command_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 4 + index * 63;
	*y = 184;
	*w = 60;
	*h = 44;
}

void battle_sub_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = 8;
	*y = 100 + index * 26;
	*w = 304;
	*h = 24;
}

static void battle_sub_back_rect(int *x, int *y, int *w, int *h)
{
	*x = 236;
	*y = 210;
	*w = 76;
	*h = 22;
}

static bool point_in(int px, int py, int x, int y, int w, int h)
{
	return px >= x && py >= y && px < x + w && py < y + h;
}

void battle_touch(Battle *b, Player *p, int px, int py)
{
	if (!b->active)
		return;
	if (b->phase == BT_PHASE_MERCY)
	{
		if (b->opCount > 0 || b->afterSeq != BT_AFTER_NONE)
			return;
		/* 擂台 sparring: the opponent is always let go. */
		if (b->sparring)
		{
			battle_win(b, p, false);
			return;
		}
		if (py >= 130 && py < 190) battle_win(b, p, px < 160);
		return;
	}
	if (b->phase == BT_PHASE_WIN || b->phase == BT_PHASE_LOSE ||
	    b->phase == BT_PHASE_ESCAPED)
	{
		battle_acknowledge(b, p);
		return;
	}
	if (b->lock > 0.0f || b->opCount > 0 || b->afterSeq != BT_AFTER_NONE)
		return;

	if (b->phase == BT_PHASE_FIGHT)
	{
		for (int i = 0; i < BT_CMD_COUNT; i++)
		{
			int x, y, w, h;
			battle_command_rect(i, &x, &y, &w, &h);
			if (point_in(px, py, x, y, w, h))
			{
				b->menuIndex = i;
				battle_command(b, p, i);
				return;
			}
		}
		return;
	}

	int x, y, w, h;
	battle_sub_back_rect(&x, &y, &w, &h);
	if (point_in(px, py, x, y, w, h))
	{
		b->phase = BT_PHASE_FIGHT;
		return;
	}
	int count = battle_sub_count(b, p);
	for (int i = 0; i < BT_SUB_ROWS; i++)
	{
		battle_sub_rect(i, &x, &y, &w, &h);
		if (!point_in(px, py, x, y, w, h))
			continue;
		int index = b->subPage * BT_SUB_ROWS + i;
		if (index < count)
		{
			b->subIndex = i;
			battle_sub_activate(b, p, index);
		}
		return;
	}
}

/* -------------------------------------------------------------------- drawing */

/* 像素风血条：4x5 的方块按 5px 间距铺开，空槽不画底色。粗条 =
 * value/max，下方 1px 的点线 = item/max（有效上限）。`item` 是体力的伤势
 * 上限；内力没有伤势模型，细线恒满宽。max 为 0 时整行不画，对应原版
 * SetLine 的除零保护（新角色 0/0 内力看不到 FP 条）。 */
#define PX_BAR_CELL_W 4
#define PX_BAR_CELL_H 6
#define PX_BAR_STEP   5

static void draw_pixel_bar(int x, int y, int w, int value, int item,
                           int maxValue, u32 clr)
{
	if (maxValue <= 0 || w <= 0)
		return;
	int cells = w / PX_BAR_STEP;
	if (cells <= 0)
		return;
	int fw = cells * clampi(value, 0, maxValue) / maxValue;
	int iw = cells * clampi(item, 0, maxValue) / maxValue;
	if (value > 0 && fw == 0)
		fw = 1;
	if (item > 0 && iw == 0)
		iw = 1;

	for (int i = 0; i < fw; i++)
		C2D_DrawRectSolid((float)(x + i * PX_BAR_STEP), (float)y, 0.0f,
		                  (float)PX_BAR_CELL_W, (float)PX_BAR_CELL_H, clr);
	for (int i = 0; i < iw; i++)
		C2D_DrawRectSolid((float)(x + i * PX_BAR_STEP),
		                  (float)(y + PX_BAR_CELL_H + 2), 0.0f,
		                  (float)PX_BAR_CELL_W, 1.0f, clr);
}

static void draw_text(const char *s, int x, int y, u32 clr)
{
	text_draw(s, x, y, 1, clr);
}

static void draw_text_right(const char *s, int right, int y, u32 clr)
{
	text_draw(s, right - text_width(s, 1), y, 1, clr);
}

/* ------------------------------------------------------- battlefield art */

#define BF_PI 3.14159265f
#define BF_GROUND_Y 192.0f
#define BF_PLAYER_X 56.0f
#define BF_ENEMY_X 248.0f
#define BF_PLAYER_SCALE 2.6f
/* NPC art is a 32x32 square and many robes fill the whole tile. Matching the
 * player's height exactly would make those figures ~1.5x wider than him, so
 * the enemy is scaled to the same visual height and squeezed slightly on X. */
#define BF_ENEMY_SCALE 3.70f
#define BF_ENEMY_SCALE_X (BF_ENEMY_SCALE * 0.865f)

/* Linear RGB interpolation; the high byte (alpha) is carried over. */
static u32 mix_rgb(u32 a, u32 b, float t)
{
	if (t <= 0.0f)
		return a;
	if (t >= 1.0f)
		return b;
	u32 ar = a & 0xFFu, ag = (a >> 8) & 0xFFu, ab = (a >> 16) & 0xFFu;
	u32 br = b & 0xFFu, bg = (b >> 8) & 0xFFu, bb = (b >> 16) & 0xFFu;
	u32 r = (u32)((float)ar + ((float)br - (float)ar) * t);
	u32 g = (u32)((float)ag + ((float)bg - (float)ag) * t);
	u32 bl = (u32)((float)ab + ((float)bb - (float)ab) * t);
	return (a & 0xFF000000u) | (bl << 16) | (g << 8) | r;
}

/* Filled circle built from 2px strips: keeps the hand-drawn pixel feel and
 * avoids needing another texture for a plain backdrop shape. */
static void draw_disc(int cx, int cy, int r, u32 clr)
{
	for (int dy = -r; dy < r; dy += 2)
	{
		int h = dy + 2 > r ? r - dy : 2;
		float mid = (float)dy + (float)h * 0.5f;
		float k = (float)(r * r) - mid * mid;
		if (k <= 0.0f)
			continue;
		int w = (int)sqrtf(k);
		C2D_DrawRectSolid((float)(cx - w), (float)(cy + dy), 0.0f,
		                  (float)(w * 2), (float)h, clr);
	}
}

/* Soft contact shadow: the fighters must sit on the ground line, not float. */
static void draw_ground_shadow(int cx, int cy, int rx, int ry, u32 clr)
{
	for (int dy = -ry; dy <= ry; dy++)
	{
		float k = 1.0f - (float)(dy * dy) / (float)(ry * ry);
		int w = (int)((float)rx * sqrtf(k));
		if (w > 0)
			C2D_DrawRectSolid((float)(cx - w), (float)(cy + dy), 0.0f,
			                  (float)(w * 2), 1.0f, clr);
	}
}

/* Bresenham line of t x t pixel blocks: hit sparks stay stair-stepped. */
static void draw_px_line(int x0, int y0, int x1, int y1, int t, u32 clr)
{
	int dx = abs(x1 - x0);
	int dy = -abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	for (;;)
	{
		C2D_DrawRectSolid((float)x0, (float)y0, 0.0f,
		                  (float)t, (float)t, clr);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = err * 2;
		if (e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}
}

/* 打击贴图: a brush-flash star over the victim, claw slashes for a wound and
 * dust at the feet. `ap` is 0 at the hit and 1 when the flash has decayed. */
static void draw_hit_spark(int cx, int cy, int footY, float ap, bool crit,
                           u32 spark, u32 core, u32 dust)
{
	static const int DIR8[8][2] =
	{
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
	};

	if (ap < 0.0f || ap > 0.55f)
		return;

	float k = ap / 0.55f;             /* 0..1 across the visible window */
	float grow = k < 0.30f ? k / 0.30f : 1.0f;
	float fade = k < 0.62f ? 1.0f : (1.0f - k) / 0.38f;
	if (fade < 0.0f)
		fade = 0.0f;

	int inner = 5 + (int)(3.0f * grow);
	int outer = 12 + (int)(26.0f * grow);
	int lenNow = inner + (int)((float)(outer - inner) * fade);
	u32 ray = mix_rgb(spark, core, 0.15f);

	/* Eight rays; diagonals run a little shorter so the star reads round. */
	for (int i = 0; i < 8; i++)
	{
		int len = lenNow;
		if (i >= 4)
			len = lenNow * 3 / 4;
		int rx = cx + DIR8[i][0] * len;
		int ry = cy + DIR8[i][1] * len;
		draw_px_line(cx + DIR8[i][0] * inner, cy + DIR8[i][1] * inner,
		             rx, ry, 3, ray);
	}

	/* Bright core: a diamond that collapses as the spark fades. */
	int s = (int)(9.0f * fade) + 2;
	for (int dy = -s; dy <= s; dy++)
	{
		int w = s - abs(dy);
		C2D_DrawRectSolid((float)(cx - w), (float)(cy + dy), 0.0f,
		                  (float)(w * 2 + 1), 1.0f, core);
	}

	/* Strike marks across the body. A wound (crit) leaves three claws. */
	int slash = (int)(3.0f * fade);
	if (slash > 0)
	{
		draw_px_line(cx - 28, cy - 20, cx + 26, cy + 14, slash, spark);
		if (crit)
		{
			draw_px_line(cx - 26, cy - 2, cx + 28, cy - 26, slash, spark);
			draw_px_line(cx - 22, cy + 20, cx + 30, cy + 34, slash, spark);
		}
	}

	/* Dust kicked up at ground level, only in the first half of the beat. */
	if (k < 0.55f)
	{
		int spread = 8 + (int)(30.0f * k);
		for (int i = 0; i < 4; i++)
		{
			int dx = (i - 1) * spread / 2 + (i & 1 ? 4 : -4);
			int dy = (i & 1) ? 0 : -3;
			C2D_DrawRectSolid((float)(cx + dx - 3), (float)(footY + dy),
			                  0.0f, 7.0f, 2.0f, dust);
		}
	}
}

/* Placeholder silhouette for custom battles without a 32x32 map portrait. */
static void draw_fallback_enemy(float x, float y, float s, u32 ink)
{
	draw_disc((int)(x + 16 * s), (int)(y + 10 * s), (int)(7 * s), ink);
	C2D_DrawRectSolid(x + 9 * s, y + 17 * s, 0.0f, 14 * s, 14 * s, ink);
	C2D_DrawRectSolid(x + 10 * s, y + 31 * s, 0.0f, 4 * s, 5 * s, ink);
	C2D_DrawRectSolid(x + 18 * s, y + 31 * s, 0.0f, 4 * s, 5 * s, ink);
}

void battle_draw_top(const Battle *b, const Player *p, u32 playSeconds)
{
	(void)playSeconds;
	char line[96];

	const u32 paper  = ui_col(UI_COL_PAPER);
	const u32 shade  = ui_col(UI_COL_SHADE);
	const u32 shade2 = ui_col(UI_COL_SHADE2);
	const u32 ink    = ui_col(UI_COL_INK);
	const u32 accent = ui_col(UI_COL_ACCENT);
	const u32 ground = shade;
	const u32 shadow = mix_rgb(ground, ink, 0.32f);

	/* Paper sky, ink horizon. No floating decorations: everything below the
	 * horizon belongs to the ground plane. The backdrop recedes with the
	 * deepest layer; both eyes still cover the screen edges because the
	 * rect is widened by GFX_3D_BASE_SHIFT on each side. */
	float bgShift = gfx_3d_offset(GFX_DEPTH_GROUND);
	float bgX = bgShift - GFX_3D_BASE_SHIFT;
	float bgW = (float)SCR_W + 2.0f * GFX_3D_BASE_SHIFT;
	C2D_DrawRectSolid(bgX, 0.0f, 0.0f, bgW, SCR_H, paper);
	C2D_DrawRectSolid(bgX, BF_GROUND_Y, 0.0f, bgW, SCR_H - BF_GROUND_Y, ground);
	C2D_DrawRectSolid(bgX, BF_GROUND_Y - 2.0f, 0.0f, bgW, 2.0f, ink);

	static const int tufts[][2] =
	{
		{ 42, 212 }, { 118, 226 }, { 196, 206 }, { 286, 218 }, { 352, 208 }
	};
	for (int i = 0; i < 5; i++)
	{
		int tx = tufts[i][0] + (int)bgShift, ty = tufts[i][1];
		C2D_DrawRectSolid((float)tx, (float)ty, 0.0f, 1.0f, 4.0f, shade2);
		C2D_DrawRectSolid((float)(tx + 3), (float)(ty + 1), 0.0f, 1.0f, 3.0f, shade2);
		C2D_DrawRectSolid((float)(tx - 3), (float)(ty + 1), 0.0f, 1.0f, 3.0f, shade2);
	}
	C2D_DrawRectSolid(74.0f + bgShift, 233.0f, 0.0f, 5.0f, 1.0f, shade2);
	C2D_DrawRectSolid(244.0f + bgShift, 231.0f, 0.0f, 3.0f, 1.0f, shade2);
	C2D_DrawRectSolid(322.0f + bgShift, 227.0f, 0.0f, 6.0f, 1.0f, shade2);

	/* Header: the only text on the world screen; vitals live below. */
	snprintf(line, sizeof(line), "战斗 · %s", b->enemyName);
	text_draw(line, 10, 4, 1, ink);
	snprintf(line, sizeof(line), "第 %d 回合", b->turn);
	text_draw(line, 390 - text_width(line, 1), 4, 1, shade2);
	C2D_DrawRectSolid(10.0f, 25.0f, 0.0f, 380.0f, 1.0f, mix_rgb(paper, ink, 0.22f));

	/* Idle breathing plus the attack beat. The hit flash drives the lunge,
	 * the victim's recoil and a short shake; nothing is frame-rate coupled. */
	float tm = (float)(osGetTime() & 0xFFFFFu) / 1000.0f;
	float bobP = sinf(tm * 2.1f) * 1.4f;
	float bobE = sinf(tm * 1.7f + 1.4f) * 1.6f;

	bool hit = b->flashTimer > 0.0f && b->flashSide != 0;
	float ap = 1.0f;
	float lunge = 0.0f, recoil = 0.0f, shake = 0.0f;
	if (hit)
	{
		ap = 1.0f - b->flashTimer / BT_FLASH;
		if (ap < 0.0f) ap = 0.0f;
		if (ap > 1.0f) ap = 1.0f;
		float lk = ap / 0.34f;
		if (lk > 1.0f) lk = 1.0f;
		lunge = sinf(lk * BF_PI) * 26.0f;
		float rk = ap / 0.50f;
		if (rk > 1.0f) rk = 1.0f;
		recoil = sinf(rk * BF_PI) * 9.0f;
		float sk = 1.0f - ap / 0.50f;
		if (sk < 0.0f) sk = 0.0f;
		shake = sinf(ap * 84.0f) * 3.0f * sk;
	}

	float pX = BF_PLAYER_X;
	float eX = BF_ENEMY_X;
	if (hit)
	{
		if (b->flashSide == 2)
		{
			pX += lunge;
			eX += recoil;
		}
		else
		{
			eX -= lunge;
			pX -= recoil;
		}
	}
	pX += shake;
	eX += shake;

	float pW = 32.0f * BF_PLAYER_SCALE;
	/* Frame 0 keeps two empty rows under the shoes; drop the sprite by those
	 * rows so the feet actually stand on the horizon. */
	float pY = BF_GROUND_Y - 46.0f * BF_PLAYER_SCALE + bobP;
	float eY = BF_GROUND_Y - 32.0f * BF_ENEMY_SCALE + bobE;

	/* Shadows stay on the floor; only the shake moves them. */
	draw_ground_shadow((int)(BF_PLAYER_X + pW * 0.5f + shake) + (int)bgShift,
	                   189, 30, 5, shadow);
	draw_ground_shadow((int)(BF_ENEMY_X + 32.0f * BF_ENEMY_SCALE_X * 0.5f + shake)
	                       + (int)bgShift, 189, 32, 6, shadow);

	/* The player keeps the front-facing idle frame (frame 0 of the DOWN
	 * column: both feet visible). The enemy is the same 32x32 map image
	 * used in the field, in its original orientation: mirroring a costume
	 * with beads/staff on one side reads as a bug, so flipX stays false. */
	gfx_player(DIR_DOWN * 3, pX, pY, BF_PLAYER_SCALE);
	if (b->portraitTile < TILE_COUNT)
		gfx_tile_scaled(b->portraitTile, LAYER_OBJECT, eX, eY,
		                BF_ENEMY_SCALE_X, BF_ENEMY_SCALE, false);
	else
		draw_fallback_enemy(eX + gfx_3d_offset(GFX_DEPTH_STAGE), eY, 1.0f, ink);

	int pChestX = (int)(pX + 16.0f * BF_PLAYER_SCALE);
	int pChestY = (int)(pY + 24.0f * BF_PLAYER_SCALE);
	int eChestX = (int)(eX + 16.0f * BF_ENEMY_SCALE_X);
	int eChestY = (int)(eY + 17.0f * BF_ENEMY_SCALE);

	if (hit)
	{
		int cx = b->flashSide == 1 ? pChestX : eChestX;
		int cy = b->flashSide == 1 ? pChestY : eChestY;
		draw_hit_spark(cx + (int)gfx_3d_offset(GFX_DEPTH_STAGE), cy,
		               (int)BF_GROUND_Y + 2, ap, b->flashCrit != 0,
		               accent, b->flashCrit ? CLR_RED : paper, shade2);

		if (ap < 0.70f)
		{
			int scale = b->flashCrit ? 3 : 2;
			float dx = (b->flashSide == 1 ? 64.0f : 262.0f) + shake;
			float dy = (b->flashSide == 1 ? 40.0f : 66.0f) - ap * 42.0f;
			snprintf(line, sizeof(line), "-%d", b->flashDamage);
			text_draw3(line, (int)dx + 1, (int)dy + 1, scale, shade2,
			           GFX_DEPTH_STAGE);
			text_draw3(line, (int)dx, (int)dy, scale,
			           b->flashCrit ? CLR_RED : ink, GFX_DEPTH_STAGE);
		}
	}
}

static void draw_result(const Battle *b, const Player *p)
{
	char line[128];
	ui_panel(8, 40, 304, 168);

	const char *title = b->phase == BT_PHASE_WIN ? "胜  利"
	                  : b->phase == BT_PHASE_LOSE ? "败  北" : "脱  身";
	text_draw(title, 160 - text_width(title, 2) / 2, 58, 2, CLR_ACCENT);
	C2D_DrawRectSolid(96, 92, 0.0f, 128, 1, CLR_BORDER);

	if (b->phase == BT_PHASE_WIN)
	{
		snprintf(line, sizeof(line), "战胜 %s", b->enemyName);
		draw_text(line, 24, 104, CLR_TX);
		snprintf(line, sizeof(line), "文钱 +%lu", (unsigned long)b->moneyReward);
		draw_text(line, 24, 132, CLR_DIM);
	}
	else if (b->phase == BT_PHASE_LOSE)
	{
		/* fight.s who_win: no_kill 说 npc_msg「承让了.」；kill_you 说
		 * npc_msg2（名字 + 换行 +「,去死吧!」）。 */
		if (b->fatal)
		{
			draw_text(p->name, 24, 104, CLR_TX);
			draw_text(",去死吧!", 24, 132, CLR_DIM);
		}
		else
		{
			draw_text("承让了.", 24, 104, CLR_TX);
		}
	}
	else
	{
		draw_text("你脱离了战斗，没有获得经验。", 24, 104, CLR_TX);
	}
	ui_more(160, 190, CLR_DIM);
}

/* Wraps a battle line into at most maxLines rows. text_wrap_fitted keeps the
 * head and appends "……" when the line still does not fit, so an over-long
 * technique description is visibly truncated instead of silently losing its
 * tail at the last row.
 * Returns the number of rows actually drawn. */
static int draw_wrapped_text(const char *s, int x, int y, int w, int maxLines, u32 clr)
{
	char buf[768];
	char one[192];
	int n, drawn = 0;
	const char *q;

	memset(buf, 0, sizeof(buf));
	n = text_wrap(s, w, 1, buf, (int)sizeof(buf), maxLines);
	if (n < 0)
	{
		/* Reserve the last 32 px so the "……" marker itself fits on the
		 * final row instead of running off the right edge. */
		n = text_wrap_fitted(s, w - 32, 1, buf, (int)sizeof(buf), maxLines);
	}
	if (n < 0)
		n = 0;

	q = buf;
	for (int i = 0; i < n && *q; i++)
	{
		const char *nl = strchr(q, '\n');
		size_t len = nl ? (size_t)(nl - q) : strlen(q);
		if (len >= sizeof(one))
			len = sizeof(one) - 1;
		memcpy(one, q, len);
		one[len] = '\0';
		draw_text(one, x, y + i * 17, clr);
		drawn = i + 1;
		if (!nl)
			break;
		q = nl + 1;
	}
	return drawn;
}

/* Small chips for 受制/影分身/状态; nothing is drawn when all are clear. */
static void draw_status_badges(const Battle *b)
{
	char labels[4][24];
	u32 colors[4];
	int count = 0;
	int x = 8;
	int fx = 0;

	if (b->pBusy > 0)
	{
		snprintf(labels[count], sizeof(labels[0]), "受制 %d", b->pBusy);
		colors[count++] = CLR_RED;
	}
	if (b->pFenshen)
	{
		snprintf(labels[count], sizeof(labels[0]), "影分身");
		colors[count++] = CLR_ACCENT;
	}
	for (int i = 0; i < BT_MAX_EFFECTS; i++)
		if (b->effects[i].id != 0xFF)
			fx++;
	if (fx > 0)
	{
		snprintf(labels[count], sizeof(labels[0]), "状态 %d", fx);
		colors[count++] = CLR_BAR;
	}

	for (int i = 0; i < count; i++)
	{
		int w = text_width(labels[i], 1) + 14;
		ui_badge(x, 64, w, 16, labels[i], CLR_SELBG, colors[i]);
		ui_frame(x, 64, w, 16, CLR_BORDER);
		x += w + 6;
	}

	if (b->eBusy > 0)
	{
		snprintf(labels[0], sizeof(labels[0]), "敌受制 %d", b->eBusy);
		int w = text_width(labels[0], 1) + 14;
		ui_badge(312 - w, 64, w, 16, labels[0], CLR_SELBG, CLR_RED);
		ui_frame(312 - w, 64, w, 16, CLR_BORDER);
	}
}

void battle_draw_bottom(const Battle *b, const Player *p)
{
	char line[96];
	bool low = (int)p->hp * 4 < (int)p->effhp;

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, CLR_BG);
	draw_text(p->name, 8, 4, CLR_ACCENT);
	draw_text_right(b->enemyName, 312, 4, CLR_ACCENT);

	/* HP/FP rows, 我方 left and the opponent right, each stat a pixel bar with
	 * its number beside it. The player's HP reads hp/effhp and its thin line
	 * is the 伤势 ceiling; every other bar has a full-width thin line. */
	draw_text("HP", 4, 22, CLR_DIM);
	snprintf(line, sizeof(line), "%d/%d", (int)p->hp, (int)p->effhp);
	draw_text_right(line, 206, 22, low ? CLR_RED : CLR_TX);
	draw_pixel_bar(28, 26, 106, p->hp, p->effhp, p->hpMax, CLR_HP);

	snprintf(line, sizeof(line), "%d", (int)b->ehp);
	draw_text_right(line, 314, 22, CLR_TX);
	draw_pixel_bar(212, 26, 66, b->ehp, b->eEffhp, b->ehpMax, CLR_HP);

	draw_text("FP", 4, 46, CLR_DIM);
	snprintf(line, sizeof(line), "%d/%d", (int)p->mp, (int)p->mpMax);
	draw_text_right(line, 206, 46, CLR_TX);
	draw_pixel_bar(28, 50, 106, p->mp, p->mpMax, p->mpMax, CLR_MP);

	snprintf(line, sizeof(line), "%d", (int)b->emp);
	draw_text_right(line, 314, 46, CLR_TX);
	draw_pixel_bar(212, 50, 66, b->emp, b->empMax, b->empMax, CLR_MP);

	draw_status_badges(b);

	if (b->phase == BT_PHASE_MERCY)
	{
		ui_panel(8, 88, 304, 108);
		if (b->sparring)
		{
			/* 擂台 sparring: the duel ends, nobody dies. */
			draw_text("对手抱拳认输，这场擂台赛到此为止。", 20, 104, CLR_TX);
			ui_button(16, 146, 130, 40, "结束", UI_BTN_NORMAL);
			return;
		}
		draw_text("对手已败，是否取其性命？", 20, 104, CLR_TX);
		ui_button(16, 146, 130, 40, "杀死", UI_BTN_NORMAL);
		ui_button(174, 146, 130, 40, "放过", UI_BTN_NORMAL);
		return;
	}
	if (b->phase == BT_PHASE_WIN || b->phase == BT_PHASE_LOSE ||
	    b->phase == BT_PHASE_ESCAPED)
	{
		draw_result(b, p);
		return;
	}

	if (b->phase == BT_PHASE_FIGHT)
	{
		bool busy = b->lock > 0.0f || b->opCount > 0 ||
		            b->afterSeq != BT_AFTER_NONE;

		/* The current beat, set off by a thin quote bar instead of a log
		 * box; the original shows one message at a time. */
		int rows = 1;
		if (b->lastDesc[0])
			rows = draw_wrapped_text(b->lastDesc, 12, 86, 298, 4, CLR_TX);
		else
			draw_text("……", 12, 86, CLR_DIM);
		if (rows < 1)
			rows = 1;
		C2D_DrawRectSolid(6, 84, 0.0f, 2.0f,
		                  (float)(rows * 17 - 2), CLR_BORDER);

		for (int i = 0; i < BT_CMD_COUNT; i++)
		{
			int x, y, w, h;
			int state = busy ? UI_BTN_DISABLED
			          : (i == b->menuIndex ? UI_BTN_SELECTED : UI_BTN_NORMAL);
			battle_command_rect(i, &x, &y, &w, &h);
			ui_button(x, y, w, h, CMD_LABELS[i], state);
		}
		return;
	}

	int count = battle_sub_count(b, p);
	int first = b->subPage * BT_SUB_ROWS;
	for (int i = 0; i < BT_SUB_ROWS; i++)
	{
		int index = first + i;
		int x, y, w, h;
		battle_sub_rect(i, &x, &y, &w, &h);
		bool rowSel = (index == first + b->subIndex && index < count);
		C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)(h - 1),
		                  rowSel ? CLR_SELBG : CLR_PANEL);
		if (rowSel)
			ui_frame(x, y, w, h, CLR_ACCENT);
		if (index >= count)
		{
			draw_text("—", x + 10, y + 4, CLR_DIM);
			continue;
		}
		if (b->phase == BT_PHASE_PERFORM)
		{
			int list[BT_PERFORM_MAX];
			int n = skill_perform_list(p, list, BT_PERFORM_MAX);
			if (index >= n)
				continue;
			const PerformDef *d = perform_get(list[index]);
			int level = d ? skill_kf_effective_level(p, d->kf) : 0;
			snprintf(line, sizeof(line), "%s  Lv%d  内力 -%d",
			         d ? d->name : "?", level, skill_perform_cost(list[index], level));
			draw_text(line, x + 10, y + 4, rowSel ? CLR_ACCENT : CLR_TX);
		}
		else
		{
			int slots[BAG_SLOTS];
			int n = battle_item_slots(p, slots, BAG_SLOTS);
			if (index >= n)
				continue;
			int id = BAG_ID(p->bag[slots[index]]);
			snprintf(line, sizeof(line), "%s x%d", item_name(id),
			         p->bag[slots[index]].count);
			draw_text(line, x + 10, y + 4, rowSel ? CLR_ACCENT : CLR_TX);
		}
	}
	int bx, by, bw, bh;
	battle_sub_back_rect(&bx, &by, &bw, &bh);
	ui_button(bx, by, bw, bh, "返回", UI_BTN_NORMAL);
	snprintf(line, sizeof(line), "%s %d/%d",
	         b->phase == BT_PHASE_PERFORM ? "绝招" : "物品",
	         first / BT_SUB_ROWS + 1,
	         (count + BT_SUB_ROWS - 1) / BT_SUB_ROWS);
	draw_text(line, 8, 214, CLR_DIM);
}

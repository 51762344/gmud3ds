/* M5.5 internal-force commands, calibrated against lee1.s:
 *
 *   recover 吸气  (lee1.s:204)  cost = missing*20/(10+eff/15)+1,
 *                               heal = cost*(10+eff/15)/20
 *   heal    疗伤  (lee1.s:91)   gain = 10 + eff/5, raises effhp, mp -50
 *   jiali   加力  (lee1.s:300)  man_force <= eff/2 via input_digit
 *
 * All three first require an enabled force kungfu (man_usekf+3 bit7);
 * without it the original shows heal_enable_msg.
 *
 * Every message is the original perform.txt label (PF_MSG_*), rendered with
 * $N = the player's name, exactly like lee1.s show_text -> format_string.
 */
#include "neili.h"

#include "actor.h"
#include "fighttext.h"
#include "skill.h"

#include <stdio.h>

/* Renders one perform.txt label with the player's own name ($N). */
static void neili_text(char *msg, int size, const struct Player *p, int label)
{
	if (!msg || size <= 0)
		return;
	fight_perform_msg_self(msg, size, label, p ? p->name : "");
}

static bool require_force(const struct Player *p, char *msg, int size)
{
	if (skill_usekf_id(p, SKILL_TYPE_FORCE) > 0)
		return true;
	neili_text(msg, size, p, PF_MSG_HEAL_ENABLE);
	return false;
}

bool neili_recover(struct Player *p, char *msg, int size)
{
	if (!p)
		return false;
	if (!require_force(p, msg, size))
		return false;

	/* recover: neili below 20 cannot pay for a breath. */
	if (p->mp < 20)
	{
		neili_text(msg, size, p, PF_MSG_NOT_NEILI);
		return false;
	}

	int missing = p->effhp - p->hp;
	if (missing <= 0)
	{
		neili_text(msg, size, p, PF_MSG_RECOVER_HP);
		return false;
	}

	int eff = skill_query_level(p, SKILL_TYPE_FORCE);
	int factor = 10 + eff / 15;
	int cost = missing * 20 / factor + 1;
	if (cost > p->mp)
		cost = p->mp;
	/* perform.s recover: receive_heal = cost*(10+skill/15)/20. The
	 * original does not force a minimum of 1. */
	int heal = cost * factor / 20;
	if (heal > missing)
		heal = missing;

	p->mp = (s16)(p->mp - cost);
	p->hp = (s16)(p->hp + heal > p->effhp ? p->effhp : p->hp + heal);
	/* lee1.s recover shows recover_over_msg; the numeric readout is the
	 * project's documented deviation (the original has no number here). */
	char line[FT_RENDER_MAX];
	neili_text(line, sizeof(line), p, PF_MSG_RECOVER_OVER);
	snprintf(msg, size, "%s 体力 +%d，内力 -%d。", line, heal, cost);
	return true;
}

bool neili_heal(struct Player *p, char *msg, int size)
{
	if (!p)
		return false;
	if (!require_force(p, msg, size))
		return false;

	int eff = skill_query_level(p, SKILL_TYPE_FORCE);
	/* lee1.s heal checks the inner kungfu level first (>= 45). */
	if (eff < 45)
	{
		neili_text(msg, size, p, PF_MSG_HEAL_LEVEL);
		return false;
	}
	if (p->mpMax < 150)
	{
		neili_text(msg, size, p, PF_MSG_HEAL_MAXNEILI);
		return false;
	}
	if (p->mp < 100)
	{
		neili_text(msg, size, p, PF_MSG_NOT_NEILI);
		return false;
	}
	if (p->effhp >= p->hpMax)
	{
		neili_text(msg, size, p, PF_MSG_HEAL_NOHURT);
		return false;
	}
	if (p->effhp < p->hpMax / 3)
	{
		neili_text(msg, size, p, PF_MSG_HEAL_BADHURT);
		return false;
	}

	int missing = p->hpMax - p->effhp;
	char start[FT_RENDER_MAX];
	char over[FT_RENDER_MAX];
	if (missing <= 0)
	{
		/* lee1.s heal_4: nothing to heal, no fp is spent and heal_over_msg
		 * is not shown. (Unreachable in practice: effhp == maxhp is caught
		 * by heal_no_hurt above.) */
		neili_text(start, sizeof(start), p, PF_MSG_HEAL_NOEFF);
		neili_text(over, sizeof(over), p, PF_MSG_HEAL_NOEFF1);
		snprintf(msg, size, "%s\n%s", start, over);
		return true;
	}

	/* lee1.s heal: gain = 10 + eff/5, capped by the missing amount; the
	 * treatment costs 50 fp. */
	int gain = 10 + eff / 5;
	if (gain > missing)
		gain = missing;
	p->effhp = (s16)(p->effhp + gain);
	p->mp = (s16)(p->mp - 50);

	/* lee1.s heal shows heal_sucess_msg, then heal_over_msg after the
	 * treatment. */
	neili_text(start, sizeof(start), p, PF_MSG_HEAL_SUCESS);
	neili_text(over, sizeof(over), p, PF_MSG_HEAL_OVER);
	snprintf(msg, size, "%s\n%s（伤势上限 +%d）", start, over, gain);
	return true;
}

int neili_jiali_max(const struct Player *p)
{
	return skill_query_level(p, SKILL_TYPE_FORCE) / 2;
}

/* lee1.s jiali: input_digit clamps the value to skill_level/2 and the
 * original only prints a message when the player dials in the cap
 * (cmp2 a1,a2 / bcs jiali_1 — the value equals the maximum). The text is
 * jiali_flow_msg with the runtime number of man_force inserted, i.e.
 * "db '你目前加力上限为',2 / dw man_force,0". */
void neili_jiali_text(char *msg, int size, const struct Player *p, int value)
{
	if (!msg || size <= 0)
		return;
	msg[0] = '\0';
	FightNames names;
	names.attacker = p ? p->name : "";
	names.defender = NULL;
	names.weapon = NULL;
	names.kungfu = NULL;
	names.perform = NULL;
	fight_perform_msg_num(msg, size, PF_MSG_JIALI_FLOW, value, &names);
}

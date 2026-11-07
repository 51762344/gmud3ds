#include "skill.h"

#include "actor.h"
#include "item.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKILLS_MAGIC 0x4C4B5347u   /* "GSKL" little endian */
#define SKILLS_VERSION 1
#define SKILLS_HEADER 12

static SkillDef *s_skills;
static PerformDef *s_performs;
static int s_skillCount;
static int s_performCount;

static u16 rd16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void skill_pack_free(void)
{
	free(s_skills);
	free(s_performs);
	s_skills = NULL;
	s_performs = NULL;
	s_skillCount = 0;
	s_performCount = 0;
}

bool skill_pack_load(const char *romfs_path)
{
	skill_pack_free();

	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;
	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return false;
	}
	long size = ftell(f);
	if (size < SKILLS_HEADER)
	{
		fclose(f);
		return false;
	}
	rewind(f);

	u8 *buf = (u8 *)malloc((size_t)size);
	if (!buf)
	{
		fclose(f);
		return false;
	}
	size_t got = fread(buf, 1, (size_t)size, f);
	fclose(f);
	if (got != (size_t)size)
	{
		free(buf);
		return false;
	}

	if (rd32(buf) != SKILLS_MAGIC || rd16(buf + 4) != SKILLS_VERSION)
	{
		free(buf);
		return false;
	}
	int kfCount = (int)rd16(buf + 6);
	int pfCount = (int)rd16(buf + 8);
	int recSize = (int)rd16(buf + 10);
	if (kfCount != SK_COUNT_EXPECTED || pfCount <= 0 ||
	    recSize != SKILL_REC_SIZE ||
	    size != SKILLS_HEADER + (kfCount + pfCount) * recSize)
	{
		free(buf);
		return false;
	}

	s_skills = (SkillDef *)calloc((size_t)kfCount, sizeof(SkillDef));
	s_performs = (PerformDef *)calloc((size_t)pfCount, sizeof(PerformDef));
	if (!s_skills || !s_performs)
	{
		free(buf);
		skill_pack_free();
		return false;
	}

	for (int i = 0; i < kfCount; i++)
	{
		const u8 *r = buf + SKILLS_HEADER + i * recSize;
		s_skills[i].id = r[0];
		s_skills[i].type = r[1];
		s_skills[i].qi = r[2];
		memcpy(s_skills[i].name, r + 4, SKILL_NAME_LEN);
		s_skills[i].name[SKILL_NAME_LEN - 1] = '\0';
		if (s_skills[i].id != i)
		{
			free(buf);
			skill_pack_free();
			return false;
		}
	}
	for (int i = 0; i < pfCount; i++)
	{
		const u8 *r = buf + SKILLS_HEADER + (kfCount + i) * recSize;
		s_performs[i].id = r[0];
		s_performs[i].kf = r[1];
		memcpy(s_performs[i].name, r + 4, SKILL_NAME_LEN);
		s_performs[i].name[SKILL_NAME_LEN - 1] = '\0';
		if (s_performs[i].id != i || s_performs[i].kf >= kfCount)
		{
			free(buf);
			skill_pack_free();
			return false;
		}
	}

	s_skillCount = kfCount;
	s_performCount = pfCount;
	free(buf);
	return true;
}

int skill_count(void)
{
	return s_skillCount;
}

int perform_count(void)
{
	return s_performCount;
}

const SkillDef *skill_get(int id)
{
	if (id < 0 || id >= s_skillCount)
		return NULL;
	return &s_skills[id];
}

const PerformDef *perform_get(int id)
{
	if (id < 0 || id >= s_performCount)
		return NULL;
	return &s_performs[id];
}

const char *skill_name(int id)
{
	const SkillDef *d = skill_get(id);
	return d ? d->name : "未知武功";
}

const char *perform_name(int id)
{
	const PerformDef *d = perform_get(id);
	return d ? d->name : "未知绝招";
}

int skill_level(const Player *p, int id)
{
	if (!p || id < 0)
		return 0;
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		if (p->skills[i].level > 0 && p->skills[i].id == id)
			return p->skills[i].level;
	}
	return 0;
}

u16 skill_exp(const Player *p, int id)
{
	if (!p || id < 0)
		return 0;
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		if (p->skills[i].level > 0 && p->skills[i].id == id)
			return p->skills[i].exp;
	}
	return 0;
}

bool skill_add(Player *p, int id, int level)
{
	if (!p || id < 0 || id >= s_skillCount || level <= 0)
		return false;
	if (level > 255)
		level = 255;

	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		if (p->skills[i].level > 0 && p->skills[i].id == id)
		{
			if (level > p->skills[i].level)
			{
				p->skills[i].level = (u8)level;
				/* 红莲教义 level feeds game.s setup_maxhp. */
				actor_setup_maxhp(p);
			}
			return true;
		}
	}
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		if (p->skills[i].level == 0)
		{
			p->skills[i].id = (u8)id;
			p->skills[i].level = (u8)level;
			p->skills[i].exp = 0;
			actor_setup_maxhp(p);
			return true;
		}
	}
	return false;
}

int skill_learned_list(const Player *p, int *out, int max)
{
	int n = 0;
	if (!p || !out)
		return 0;
	for (int i = 0; i < SKILL_SLOTS && n < max; i++)
	{
		if (p->skills[i].level > 0)
			out[n++] = i;
	}
	return n;
}

int skill_best_level(const Player *p, int type)
{
	int best = 0;
	if (!p)
		return 0;
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		const SkillSlot *s = &p->skills[i];
		if (s->level == 0)
			continue;
		const SkillDef *d = skill_get(s->id);
		if (d && d->type == type && s->level > best)
			best = s->level;
	}
	return best;
}

int skill_train(Player *p, int id, int points)
{
	if (!p || points <= 0)
		return 0;
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		SkillSlot *s = &p->skills[i];
		if (s->level == 0 || s->id != id || s->level >= 255)
			continue;
		/* skill.s improve_skill is called once per show_process round: it
		 * adds a1 to the 16-bit score, then at most ONE level-up. On a
		 * level-up the score is reset to zero (the overflow is discarded),
		 * so a single huge gain can never grant multiple levels. */
		int ups = 0;
		int exp = (int)s->exp + points;
		int need = (s->level + 1) * (s->level + 1);
		if (exp >= need)
		{
			s->level++;
			s->exp = 0;
			ups = 1;
		}
		else
		{
			s->exp = (u16)(exp > 65535 ? 65535 : exp);
		}
		if (ups > 0)
		{
			actor_setup_attr(p);    /* original setup_attr after a level-up */
			actor_setup_maxhp(p);   /* 红莲教义 >= 80 changes maxhp */
		}
		return ups;
	}
	return 0;
}

int skill_basic_kf(int kfId)
{
	const SkillDef *d = skill_get(kfId);
	if (!d)
		return -1;
	switch (d->type)
	{
	case SKILL_TYPE_HAND: return SK_BASIC_BARE;
	case SKILL_TYPE_DODGE: return SK_BASIC_DODGE;
	case SKILL_TYPE_FORCE: return SK_BASIC_FORCE;
	case SKILL_TYPE_PARRY: return SK_BASIC_PARRY;
	case SKILL_TYPE_WEAPON:
		/* weapon_basic_tbl: 刀->基本刀法 棍->基本棍法 剑->基本剑术
		 * 杖->基本杖法 鞭->基本鞭法; other types have no basic kungfu. */
		switch (d->qi)
		{
		case WT_BLADE: return SK_BASIC_BLADE;
		case WT_CLUB: return SK_BASIC_CLUB;
		case WT_SWORD: return SK_BASIC_SWORD;
		case WT_STAFF: return SK_BASIC_STAFF;
		case WT_WHIP: return SK_BASIC_WHIP;
		default: return -1;
		}
	default:
		return -1;   /* 读书识字/相貌 have no basic kungfu */
	}
}

/* ------------------------------------------------------------ usekf (M5.5) */

int skill_usekf_id(const Player *p, int type)
{
	if (!p || type < 0 || type >= SKILL_USEKF_SLOTS)
		return 0;
	u8 v = p->usekf[type];
	return (v & 0x80) ? (int)(v & 0x7F) : 0;
}

bool skill_usekf_toggle(Player *p, int kfId)
{
	if (!p || kfId < SK_LITERATE)   /* skill.s: BASIC_KF_NUM cannot be enabled */
		return false;
	const SkillDef *d = skill_get(kfId);
	if (!d || d->type < 0 || d->type >= SKILL_USEKF_SLOTS)
		return false;               /* 读书识字/相貌 have no usekf slot */
	if (skill_level(p, kfId) <= 0)
		return false;               /* find_kf fail -> SSTOP in the original */
	int slot = (int)d->type;
	if ((p->usekf[slot] & 0x80) && (p->usekf[slot] & 0x7F) == kfId)
		p->usekf[slot] = 0;         /* disable_it */
	else
		p->usekf[slot] = (u8)((kfId & 0x7F) | 0x80);   /* enable_it */
	return true;
}

/* skilly.s check_skill: a slot only counts while the basic kungfu is
 * learned and the weapon type matches (hand kungfu needs empty hands). */
static bool usekf_check_skill(const Player *p, int kfId)
{
	const SkillDef *d = skill_get(kfId);
	if (!d || kfId < 0)
		return false;
	int basic = skill_basic_kf(kfId);
	if (basic < 0 || skill_level(p, basic) <= 0)
		return false;
	if (d->type == SKILL_TYPE_HAND && p->weapon != 0)
		return false;
	if (d->type == SKILL_TYPE_WEAPON)
	{
		const ItemDef *w = item_get(p->weapon);
		if (!w || w->sub != d->qi)
			return false;
	}
	return true;
}

/* skill.s get_basic_kf: weapon type follows the equipped weapon. */
static int basic_kf_for_type(const Player *p, int type)
{
	if (type == SKILL_TYPE_WEAPON)
	{
		const ItemDef *w = item_get(p->weapon);
		if (!w)
			return -1;
		switch (w->sub)
		{
		case WT_BLADE: return SK_BASIC_BLADE;
		case WT_CLUB: return SK_BASIC_CLUB;
		case WT_SWORD: return SK_BASIC_SWORD;
		case WT_STAFF: return SK_BASIC_STAFF;
		case WT_WHIP: return SK_BASIC_WHIP;
		default: return -1;
		}
	}
	switch (type)
	{
	case SKILL_TYPE_HAND: return SK_BASIC_BARE;
	case SKILL_TYPE_DODGE: return SK_BASIC_DODGE;
	case SKILL_TYPE_FORCE: return SK_BASIC_FORCE;
	case SKILL_TYPE_PARRY: return SK_BASIC_PARRY;
	default: return -1;
	}
}

int skill_query_level(const Player *p, int type)
{
	if (!p || type < 0 || type >= SKILL_USEKF_SLOTS)
		return 0;
	int basic = basic_kf_for_type(p, type);
	int eff = basic >= 0 ? skill_level(p, basic) / 2 : 0;
	int kfId = skill_usekf_id(p, type);
	if (kfId > 0 && usekf_check_skill(p, kfId))
		eff += skill_level(p, kfId);
	return eff;
}

static int push_performs_of(int kfId, const Player *p, int *out, int n, int max)
{
	(void)p;
	if (kfId <= 0)
		return n;
	for (int i = 0; i < s_performCount && n < max; i++)
	{
		/* skilly.s get_all_perform lists every move of the enabled
		 * kungfu; the level check happens when the move is used. */
		if (s_performs[i].kf == kfId)
			out[n++] = i;
	}
	return n;
}

int skill_perform_list(const Player *p, int *out, int max)
{
	if (!p || !out)
		return 0;
	/* skilly.s get_all_perform: armed fighters pull performs from the
	 * weapon slot (check_skill), bare hands from the hand slot; the dodge
	 * and force slots always add theirs. */
	int act = p->weapon ? skill_usekf_id(p, SKILL_TYPE_WEAPON)
	                    : skill_usekf_id(p, SKILL_TYPE_HAND);
	if (p->weapon && act > 0 && !usekf_check_skill(p, act))
		act = 0;
	int n = push_performs_of(act, p, out, 0, max);
	n = push_performs_of(skill_usekf_id(p, SKILL_TYPE_DODGE), p, out, n, max);
	n = push_performs_of(skill_usekf_id(p, SKILL_TYPE_FORCE), p, out, n, max);
	return n;
}

int skill_kf_effective_level(const Player *p, int kfId)
{
	const SkillDef *d = skill_get(kfId);
	if (!p || !d || d->type < 0 || d->type >= SKILL_USEKF_SLOTS)
		return 0;
	return skill_query_level(p, d->type);
}

/* perform.s judge_force costs. kfLevel is the effective level of the move's
 * own kungfu, which is what the conditional costs test. */
int skill_perform_cost(int pfId, int kfLevel)
{
	if (!perform_get(pfId))
		return 0;
	switch (pfId)
	{
	case 0: return 150;
	case 1: return 450;
	case 2: return 200;   /* ZHANGDAO1 / zhangdao_gua */
	case 3: return 400;   /* ZHANGDAO2 / zhangdao_zhen */
	case 4: return 400;
	case 5: return kfLevel >= 120 ? 400 : 200;
	case 6: return 350;
	case 7: return kfLevel >= 150 ? 850 : 550;
	case 8: return 350;
	case 9: return 150;
	case 10: return 550;
	case 11: return 300;
	case 12: return 350;
	case 13: return 550;
	case 14: return 250;
	case 15: return 350;
	case 16: return 400;
	case 17: return 350;
	case 18: return 300;
	case 19: return 500;
	case 20: return 200;
	case 21: return kfLevel >= 90 ? 250 : 150;
	case 22:
	{
		int cost = 250;
		if (kfLevel > 90)
			cost += ((kfLevel - 90) / 30) * 150;
		return cost > 600 ? 600 : cost;
	}
	case 23: return 350;
	default: return 0;
	}
}

const char *skill_perform_label(int pfId)
{
	return perform_name(pfId);
}

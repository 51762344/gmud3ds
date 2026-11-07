#include "fighttext.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* data/fighttext.bin, FTXT v5:
 *   8 B header: "FTXT", u16 version(5), u16 entryCount
 *   43 B: u8 moveCount[kfId 0..42]
 *   move metadata: 10 B per move
 *   thresholds: u8 damage_msg_data[8] + u8 status_msg_data[11]
 *   24 B: u8 pfPrimary[24] = performId -> perform.txt label index
 *   entries: u16 id, u8 attr, u8 len, UTF-8 + NUL (unique, ascending ids)
 * ids: 0..15 limbs; FT_BASE_* templates; 1000 + kfId*24 + move;
 *      2000..2072 = all 73 perform.txt labels (perform_id.h index);
 *      2100+type*8+level damage, 2148 damage0, 2149 result_tip,
 *      2200+/2211+ status. v4's 3000+ primary/branch duplicates are gone. */

#define FT_ID_KF_BASE 1000
#define FT_KF_STRIDE 24
#define FT_KF_IDS 43
#define FT_THRESHOLD_BYTES (FT_DAMAGE_LEVELS + FT_STATUS_LEVELS)
#define FT_PRIMARY_BYTES FT_PERFORM_MAX

typedef struct
{
	uint16_t id;
	uint8_t attr;   /* original damage_type for templates, 255 = uniform */
	uint8_t len;
	const char *text;
} FtEntry;

static uint8_t *s_buf;
static size_t s_size;
static const FtEntry *s_entries;
static int s_entryCount;
static const uint8_t *s_moveCount;   /* FT_KF_IDS bytes */
static const uint8_t *s_moveStats;
static const uint8_t *s_damageData;  /* FT_DAMAGE_LEVELS bytes */
static const uint8_t *s_statusData;  /* FT_STATUS_LEVELS bytes */
static const uint8_t *s_pfPrimary;   /* FT_PRIMARY_BYTES bytes */
static int s_limb;                  /* fight.s limb_flag, one roll per attack */

static const FtEntry *find_entry(int id)
{
	for (int i = 0; i < s_entryCount; i++)
	{
		if (s_entries[i].id == (uint16_t)id)
			return &s_entries[i];
		if (s_entries[i].id > (uint16_t)id)
			break;                   /* ids are packed ascending */
	}
	return NULL;
}

bool fight_text_load(const char *romfs_path)
{
	fight_text_free();

	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	const size_t headerMin = 8 + FT_KF_IDS + FT_THRESHOLD_BYTES + FT_PRIMARY_BYTES;
	if (n < (long)headerMin)
	{
		fclose(f);
		return false;
	}
	s_buf = (uint8_t *)malloc((size_t)n);
	if (!s_buf)
	{
		fclose(f);
		return false;
	}
	s_size = fread(s_buf, 1, (size_t)n, f);
	fclose(f);
	if (s_size < headerMin || memcmp(s_buf, "FTXT", 4) != 0)
	{
		fight_text_free();
		return false;
	}

	uint16_t version, count;
	memcpy(&version, s_buf + 4, 2);
	memcpy(&count, s_buf + 6, 2);
	/* v4 and older have a different header layout (no pfPrimary table):
	 * refuse them outright. */
	if (version != 5)
	{
		fight_text_free();
		return false;
	}
	s_moveCount = s_buf + 8;
	s_moveStats = s_moveCount + FT_KF_IDS;
	size_t moveTotal = 0;
	for (int i = 0; i < FT_KF_IDS; i++) moveTotal += s_moveCount[i];
	const uint8_t *threshold = s_moveStats + moveTotal * 10;
	if ((size_t)(threshold - s_buf) + FT_THRESHOLD_BYTES + FT_PRIMARY_BYTES > s_size)
	{
		fight_text_free();
		return false;
	}
	s_damageData = threshold;
	s_statusData = threshold + FT_DAMAGE_LEVELS;
	s_pfPrimary = threshold + FT_THRESHOLD_BYTES;

	/* Point every entry into the buffer, verifying the declared total. */
	FtEntry *entries = (FtEntry *)malloc(sizeof(FtEntry) * (count ? count : 1));
	if (!entries)
	{
		fight_text_free();
		return false;
	}
	const uint8_t *p = threshold + FT_THRESHOLD_BYTES + FT_PRIMARY_BYTES;
	const uint8_t *end = s_buf + s_size;
	int parsed = 0;
	while (parsed < count && p + 4 <= end)
	{
		uint16_t id;
		memcpy(&id, p, 2);
		uint8_t attr = p[2];
		uint8_t len = p[3];
		if (p + 4 + len + 1 > end)   /* +1: trailing NUL written by the packer */
			break;
		entries[parsed].id = id;
		entries[parsed].attr = attr;
		entries[parsed].len = len;
		entries[parsed].text = (const char *)p + 4;
		parsed++;
		p += 4 + len + 1;
	}
	if (parsed != count)
	{
		free(entries);
		fight_text_free();
		return false;
	}
	s_entries = entries;
	s_entryCount = count;
	return true;
}

void fight_text_free(void)
{
	free((void *)s_entries);
	s_entries = NULL;
	s_entryCount = 0;
	free(s_buf);
	s_buf = NULL;
	s_size = 0;
	s_moveCount = NULL;
	s_moveStats = NULL;
	s_damageData = NULL;
	s_statusData = NULL;
	s_pfPrimary = NULL;
}

bool fight_move_stats(int id, int index, int *level, int *ap, int *dp, int *pp, int *damage, int *force)
{
	if (!s_moveStats || id < 0 || id >= FT_KF_IDS || index < 0 || index >= s_moveCount[id]) return false;
	int offset = index;
	for (int i = 0; i < id; i++) offset += s_moveCount[i];
	const uint8_t *v = s_moveStats + offset * 10;
	*level = v[0]; *ap = (int8_t)v[3]; *dp = (int8_t)v[4]; *pp = (int8_t)v[5];
	*damage = v[6] | v[7] << 8; *force = v[8] | v[9] << 8;
	return true;
}

int fight_limb_count(void) { return 16; }

void fight_set_limb(int index)
{
	int n = fight_limb_count();
	s_limb = index < 0 ? 0 : (index >= n ? index % n : index);
}

const char *fight_limb(int i)
{
	const FtEntry *e = find_entry(i);
	return e ? e->text : NULL;
}

const char *fight_template(int id)
{
	const FtEntry *e = find_entry(id);
	return e ? e->text : NULL;
}

int fight_template_attr(int id)
{
	const FtEntry *e = find_entry(id);
	return e ? (int)e->attr : 255;
}

int fight_template_count(int base)
{
	int n = 0;
	for (int i = 0; i < 10; i++)
	{
		if (find_entry(base + i))
			n++;
	}
	return n;
}

int fight_weapon_base(int weaponSub)
{
	switch (weaponSub)
	{
	case 1: return FT_BASE_BLADE;    /* 刀 */
	case 2: return FT_BASE_CLUB;     /* 棍棒 */
	case 6: return FT_BASE_SWORD;    /* 剑（匕首也归剑系） */
	case 7: return FT_BASE_STAFF;    /* 杖杆 */
	case 9: return FT_BASE_WHIP;     /* 鞭 */
	default: return 0;               /* axe/fork/hammer/throw: bare hands */
	}
}

int fight_dodge_count(void) { return fight_template_count(FT_BASE_DODGE); }

const char *fight_dodge(int i) { return fight_template(FT_BASE_DODGE + i); }

int fight_kf_moves(int kfId)
{
	if (kfId < 0 || kfId >= FT_KF_IDS || !s_moveCount)
		return 0;
	return s_moveCount[kfId];
}

const char *fight_kf_move(int kfId, int i)
{
	if (i < 0 || i >= fight_kf_moves(kfId))
		return NULL;
	return fight_template(FT_ID_KF_BASE + kfId * FT_KF_STRIDE + i);
}

const char *fight_perform_label(int index)
{
	if (index < 0 || index >= FT_PERFORM_LABELS)
		return NULL;
	return fight_template(FT_ID_PERFORM_LABEL + index);
}

int fight_perform_primary(int performId)
{
	if (!s_pfPrimary || performId < 0 || performId >= FT_PERFORM_MAX)
		return -1;
	int index = s_pfPrimary[performId];
	return index < FT_PERFORM_LABELS ? index : -1;
}

/* ------------------------------------------------------------ string2.s */

int fight_percent(int value, int maxValue)
{
	/* math.s percent: A = low byte of (a1 * 100 / a2). hp/maxhp are 16-bit
	 * in the original; negative vitals are clamped by check_player. */
	if (maxValue <= 0)
		return 0;
	if (value < 0)
		value = 0;
	long long q = (long long)value * 100 / maxValue;
	return (int)(q & 0xFF);
}

const char *fight_damage_msg(int damageType, int damagePoint)
{
	/* get_damage_msg clamps Y >= MAX_TYPE back to table 0, and uses
	 * damage0_msg when the 16-bit damage_point is zero. */
	if (damageType < 0 || damageType >= FT_DAMAGE_TYPES)
		damageType = 0;
	if (damagePoint <= 0)
		return fight_template(FT_ID_DAMAGE0);
	int level = 0;
	if (damagePoint >= 255 || !s_damageData)
	{
		level = FT_DAMAGE_LEVELS - 1;
	}
	else
	{
		/* cmp damage_msg_data,x / bcs next: the first threshold above
		 * damagePoint is the message slot (>= 240 never reaches 255). */
		while (level < FT_DAMAGE_LEVELS && damagePoint >= s_damageData[level])
			level++;
		if (level >= FT_DAMAGE_LEVELS)
			level = FT_DAMAGE_LEVELS - 1;
	}
	return fight_template(FT_ID_DAMAGE + damageType * FT_DAMAGE_LEVELS + level);
}

const char *fight_status_msg(int ratio, bool wounded)
{
	/* get_status_msg maps ratio 0 to 1, then walks the descending
	 * status_msg_data while ratio <= threshold. a9 (wound_flag) selects
	 * eff_status_tbl and the effhp-based ratio. */
	int value = ratio & 0xFF;
	if (value == 0)
		value = 1;
	int level = 0;
	if (!s_statusData)
		level = FT_STATUS_LEVELS - 1;
	else
	{
		while (level < FT_STATUS_LEVELS && value <= s_statusData[level])
			level++;
		if (level >= FT_STATUS_LEVELS)
			level = FT_STATUS_LEVELS - 1;
	}
	return fight_template((wounded ? FT_ID_EFF_STATUS : FT_ID_STATUS) + level);
}

void fight_damage_result(char *out, int size, int damageType, int damagePoint,
                         int woundFlag, int currentHp, int maxHp)
{
	if (!out || size <= 0)
		return;
	out[0] = '\0';
	const char *damage = fight_damage_msg(damageType, damagePoint);
	if (!damage)
		return;
	const char *tip = fight_template(FT_ID_RESULT_TIP);
	if (!tip)
		tip = "";
	const char *status = fight_status_msg(fight_percent(currentHp, maxHp),
	                                     woundFlag != 0);
	if (!status)
		status = "";
	/* string2.s get_result_msg: result_tip + damage_msg + "($n" +
	 * status_msg + ")". $n is left for fight_render() / format_string. */
	snprintf(out, (size_t)size, "%s%s($n%s)", tip, damage, status);
}

void fight_render(char *out, int size, const char *tpl,
                  const char *attacker, const char *defender,
                  const char *weapon)
{
	FightNames names;
	names.attacker = attacker;
	names.defender = defender;
	names.weapon = weapon;
	names.kungfu = NULL;
	names.perform = NULL;
	fight_render_names(out, size, tpl, &names);
}

void fight_render_names(char *out, int size, const char *tpl,
                         const FightNames *names)
{
	if (!out || size <= 0)
		return;
	out[0] = '\0';
	if (!tpl)
		return;

	int o = 0;
	for (const char *p = tpl; *p && o < size - 1; p++)
	{
		const char *sub = NULL;
		if (*p != '$')
		{
			out[o++] = *p;
			continue;
		}
		p++;
		switch (*p)
		{
		case 'N': sub = names ? names->attacker : NULL; break;
		case 'n': sub = names ? names->defender : NULL; break;
		case 'l':
		case '1':
		{
			/* fight.s rolls limb_flag once per attack; all templates in that
			 * attack (including the dodge line) share the same body part. */
			sub = fight_limb(s_limb);
			break;
		}
		case 'w': sub = names ? names->weapon : NULL; break;
		/* $k / $p are the kungfu and perform names the perform.txt
		 * failure messages use (level_msg, suit_msg, neili_msg, using_msg,
		 * busy_msg, not_str_msg). */
		case 'k': sub = names ? names->kungfu : NULL; break;
		case 'p': sub = names ? names->perform : NULL; break;
		case '$': out[o++] = '$'; continue;
		case '\0': p--; continue;        /* trailing '$' */
		default:                          /* $t/$q/$g/...: keep verbatim */
			if (o < size - 2)
			{
				out[o++] = '$';
				out[o++] = *p;
			}
			continue;
		}
		if (sub)
		{
			while (*sub && o < size - 1)
				out[o++] = *sub++;
		}
	}
	out[o] = '\0';
}

void fight_perform_msg(char *out, int size, int label, const FightNames *names)
{
	if (!out || size <= 0)
		return;
	out[0] = '\0';
	const char *tpl = fight_perform_label(label);
	if (!tpl)
		return;
	fight_render_names(out, size, tpl, names);
}

void fight_perform_msg_self(char *out, int size, int label, const char *selfName)
{
	FightNames names;
	names.attacker = selfName ? selfName : "";
	names.defender = NULL;
	names.weapon = NULL;
	names.kungfu = NULL;
	names.perform = NULL;
	fight_perform_msg(out, size, label, &names);
}

void fight_perform_msg_num(char *out, int size, int label, int value,
                            const FightNames *names)
{
	if (!out || size <= 0)
		return;
	out[0] = '\0';
	const char *tpl = fight_perform_label(label);
	if (!tpl)
		return;
	char buf[FT_RENDER_MAX];
	fight_render_names(buf, sizeof(buf), tpl, names);
	/* The original prints the 16-bit word as a plain decimal number. */
	snprintf(out, (size_t)size, "%s%d", buf, value);
}

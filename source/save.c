#include "save.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_MAGIC 0x44554D47u   /* "GMUD" little endian */
#define SAVE_VERSION 8
#define SAVE_V1_SIZE 64
#define SAVE_V2_SIZE 144
#define SAVE_V3_SIZE 240
#define SAVE_V4_SIZE 256
#define SAVE_V5_SIZE 264
#define SAVE_V6_SIZE 284
#define SAVE_V7_SIZE 290
/* v8 appends four 10-byte task slots, 8 ghost bytes, endingSeen, the rank.s
 * counters and task.s home_buf. */
#define SAVE_TASKS_OFF 290
#define SAVE_TASK_SIZE 10
#define SAVE_GHOST_OFF (SAVE_TASKS_OFF + ACTOR_TASK_SLOTS * SAVE_TASK_SIZE)
#define SAVE_ENDING_OFF (SAVE_GHOST_OFF + 8)
#define SAVE_RANK_OFF (SAVE_ENDING_OFF + 1)
#define SAVE_HOME_OFF (SAVE_RANK_OFF + 6)
/* v8 reserved tail (346..351): full 16-bit food/water. Bytes 56/57 keep the
 * low halves for older readers, so the pack stays v8. */
#define SAVE_FOOD16_OFF (SAVE_HOME_OFF + 1)
#define SAVE_WATER16_OFF (SAVE_FOOD16_OFF + 2)
/* v8 reserved tail, final 2 bytes: the original saves game_sec/game_min/
 * game_hour inside zero_area (h/gmud.h:334-336) and persists the whole
 * block (save.s:71-72, SAVE_SIZE). Only the hour half fits here; game_min
 * restarts at 0 after a load (display only). Old v8 saves read as 0. */
#define SAVE_HOUR_OFF (SAVE_WATER16_OFF + 2)
#define SAVE_V8_SIZE 352
#define SAVE_SIZE SAVE_V8_SIZE
#define CRC_START 12

static bool s_fsReady;
static bool s_fsOwned;

static const char *slot_path(int slot, char *buf, size_t size, const char *ext)
{
	snprintf(buf, size, "sdmc:/3ds/gmud/save%d.%s", slot, ext);
	return buf;
}

static u32 crc32_calc(const u8 *data, size_t len)
{
	u32 crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static void put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xFF);
	p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xFF);
	p[1] = (u8)((v >> 8) & 0xFF);
	p[2] = (u8)((v >> 16) & 0xFF);
	p[3] = (u8)((v >> 24) & 0xFF);
}

static u16 get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

bool save_init(void)
{
	if (s_fsReady)
		return true;

	/* romfsInit() may already have brought the FS session up; a second
	 * fsInit() failing is not fatal as long as sdmc is mounted. */
	s_fsOwned = R_SUCCEEDED(fsInit());

	struct stat st;
	if (stat("sdmc:/", &st) != 0)
	{
		/* sdmc is not mounted: saves are impossible. Fail now so the caller
		 * shows its "cannot init SD" path instead of every later write
		 * failing silently. */
		if (s_fsOwned)
			fsExit();
		s_fsOwned = false;
		return false;
	}

	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/gmud", 0777);
	s_fsReady = true;
	return true;
}

void save_fini(void)
{
	if (s_fsReady)
	{
		if (s_fsOwned)
			fsExit();
		s_fsOwned = false;
		s_fsReady = false;
	}
}

/* The M2 64-byte layout only carried position, money and vitals. */
static void decode_v1(const u8 *buf, SaveSlotInfo *out)
{
	out->version = 1;
	out->playSeconds = get32(buf + 12);
	out->money = get32(buf + 16);
	out->mapIndex = (int)get32(buf + 20);
	out->tx = (s16)get16(buf + 24);
	out->ty = (s16)get16(buf + 26);
	out->dir = buf[28];
	out->food = buf[29];
	out->water = buf[30];
	out->hp = (s16)get16(buf + 32);
	out->hpMax = (s16)get16(buf + 34);
	out->mp = (s16)get16(buf + 36);
	out->mpMax = (s16)get16(buf + 38);
	memcpy(out->name, buf + 40, ACTOR_NAME_LEN);
	out->name[ACTOR_NAME_LEN - 1] = '\0';
}

static void decode_v2(const u8 *buf, SaveSlotInfo *out)
{
	out->version = 2;
	out->playSeconds = get32(buf + 12);
	out->money = get32(buf + 16);
	out->exp = get32(buf + 20);
	out->mapIndex = (int)get16(buf + 24);
	out->tx = buf[26];
	out->ty = buf[27];
	out->dir = buf[28];
	out->gender = buf[29];
	out->age = buf[30];
	out->pai = buf[31];
	memcpy(out->name, buf + 32, ACTOR_NAME_LEN);
	out->name[ACTOR_NAME_LEN - 1] = '\0';
	out->hp = (s16)get16(buf + 48);
	out->hpMax = (s16)get16(buf + 50);
	out->mp = (s16)get16(buf + 52);
	out->mpMax = (s16)get16(buf + 54);
	out->food = buf[56];
	out->water = buf[57];
	out->attack = buf[58];
	out->defense = buf[59];
	out->damage = buf[60];
	out->armor = buf[61];
	out->str = buf[62];
	out->dex = buf[63];
	out->intel = buf[64];
	out->con = buf[65];
	out->per = buf[66];
	out->kar = buf[67];
	out->weapon = buf[68];
	memcpy(out->equip, buf + 70, EQUIP_SLOTS);
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		out->bag[i].id = buf[82 + i * 2];
		out->bag[i].count = buf[82 + i * 2 + 1];
	}
	out->potential = get16(buf + 122);
}

/* v3 = v2 + wins/losses + 20 kungfu slots. Offset 144 used to hold the
 * self-invented player level; T2 removed it and now always writes 0. */
static void decode_v3(const u8 *buf, SaveSlotInfo *out)
{
	decode_v2(buf, out);
	out->version = 3;
	out->wins = get16(buf + 146);
	out->losses = get16(buf + 148);
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		out->skills[i].id = buf[150 + i * 4];
		out->skills[i].level = buf[151 + i * 4];
		out->skills[i].exp = get16(buf + 152 + i * 4);
	}
}

/* v4 = v3 + master npc, effhp and the innate six stats. */
static void decode_v4(const u8 *buf, SaveSlotInfo *out)
{
	decode_v3(buf, out);
	out->version = 4;
	out->master = buf[240];
	out->effhp = (s16)get16(buf + 241);
	out->attrStr = buf[243];
	out->attrDex = buf[244];
	out->attrIntel = buf[245];
	out->attrCon = buf[246];
	out->attrPer = buf[247];
	out->attrKar = buf[248];
}

/* v5 = v4 + enabled kungfu slots and the 加力 charge. */
static void decode_v5(const u8 *buf, SaveSlotInfo *out)
{
	decode_v4(buf, out);
	out->version = 5;
	for (int i = 0; i < SKILL_USEKF_SLOTS; i++)
		out->usekf[i] = buf[SAVE_V4_SIZE + i];
	out->force = (s16)get16(buf + SAVE_V4_SIZE + 5);
}

static void decode(const u8 *buf, size_t size, SaveSlotInfo *out)
{
	memset(out, 0, sizeof(*out));
	out->valid = false;
	out->exists = true;
	out->version = 0;
	if (size != SAVE_V1_SIZE && size != SAVE_V2_SIZE &&
	    size != SAVE_V3_SIZE && size != SAVE_V4_SIZE && size != SAVE_V5_SIZE &&
	    size != SAVE_V6_SIZE && size != SAVE_V7_SIZE && size != SAVE_V8_SIZE)
		return;
	if (get32(buf) != SAVE_MAGIC)
		return;

	u16 version = get16(buf + 4);
	if (version == 1 && size == SAVE_V1_SIZE)
	{
		if (get32(buf + 8) != crc32_calc(buf + CRC_START, SAVE_V1_SIZE - CRC_START))
			return;
		decode_v1(buf, out);
	}
	else if (version == 2 && size == SAVE_V2_SIZE)
	{
		if (get32(buf + 8) != crc32_calc(buf + CRC_START, SAVE_V2_SIZE - CRC_START))
			return;
		decode_v2(buf, out);
	}
	else if (version == 3 && size == SAVE_V3_SIZE)
	{
		if (get32(buf + 8) != crc32_calc(buf + CRC_START, SAVE_V3_SIZE - CRC_START))
			return;
		decode_v3(buf, out);
	}
	else if (version == 4 && size == SAVE_V4_SIZE)
	{
		if (get32(buf + 8) != crc32_calc(buf + CRC_START, SAVE_V4_SIZE - CRC_START))
			return;
		decode_v4(buf, out);
	}
	else if ((version == 5 && size == SAVE_V5_SIZE) || (version == 6 && size == SAVE_V6_SIZE) ||
	         (version == 7 && size == SAVE_V7_SIZE) || (version == 8 && size == SAVE_V8_SIZE))
	{
		if (get32(buf + 8) != crc32_calc(buf + CRC_START, size - CRC_START))
			return;
		decode_v5(buf, out);
		if (version >= 6)
		{
			out->version = 6;
			memcpy(out->deadNpc, buf + 264, 16);
			out->stones = buf[280];
			out->killTarget = buf[281];
			out->killComplete = buf[282];
			out->morality = (s8)buf[283];
			if (version >= 7)
			{
				out->version = 7;
				out->killReward = get16(buf + 284);
				out->killReportTime = get32(buf + 286);
			}
			if (version >= 8)
			{
				out->version = 8;
				for (int i = 0; i < ACTOR_TASK_SLOTS; i++)
				{
					const u8 *r = buf + SAVE_TASKS_OFF + i * SAVE_TASK_SIZE;
					out->tasks[i].type = r[0];
					out->tasks[i].state = r[1];
					out->tasks[i].id = r[2];
					out->tasks[i].index = r[3];
					out->tasks[i].reward = get16(r + 4);
					out->tasks[i].time = get32(r + 6);
				}
				out->ghostXing = buf[SAVE_GHOST_OFF + 0];
				out->ghostMing = buf[SAVE_GHOST_OFF + 1];
				out->ghostGender = buf[SAVE_GHOST_OFF + 2];
				out->ghostPai = buf[SAVE_GHOST_OFF + 3];
				out->ghostMap = buf[SAVE_GHOST_OFF + 4];
				out->ghostX = buf[SAVE_GHOST_OFF + 5];
				out->ghostY = buf[SAVE_GHOST_OFF + 6];
				out->ghostActive = buf[SAVE_GHOST_OFF + 7];
				out->endingSeen = buf[SAVE_ENDING_OFF];
				out->npcKill = get16(buf + SAVE_RANK_OFF);
				out->guanKill = get16(buf + SAVE_RANK_OFF + 2);
				out->siKill = get16(buf + SAVE_RANK_OFF + 4);
				out->homeBuf = buf[SAVE_HOME_OFF];
				/* v8 tail extension: old v8 saves have zero here and keep
				 * the byte values from offsets 56/57. */
				u16 food16 = get16(buf + SAVE_FOOD16_OFF);
				u16 water16 = get16(buf + SAVE_WATER16_OFF);
				if (food16)
					out->food = food16;
				if (water16)
					out->water = water16;
				/* game_hour: old v8 saves read 0, matching a fresh clock. */
				out->gameHour = get16(buf + SAVE_HOUR_OFF);
			}
		}
	}
	else
		return;

	out->valid = true;
}

bool save_slot_info(int slot, SaveSlotInfo *out)
{
	if (!out)
		return false;
	memset(out, 0, sizeof(*out));
	if (slot < 0 || slot >= SAVE_SLOT_COUNT)
		return false;

	char path[64];
	FILE *f = fopen(slot_path(slot, path, sizeof(path), "sav"), "rb");
	if (!f)
		return false;

	u8 buf[SAVE_SIZE];
	memset(buf, 0, sizeof(buf));
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n < SAVE_V1_SIZE)
	{
		out->exists = true;
		out->valid = false;
		return true;
	}
	decode(buf, n, out);
	return true;
}

bool save_write(int slot, const Player *p, u32 playSeconds)
{
	if (!s_fsReady || !p || slot < 0 || slot >= SAVE_SLOT_COUNT)
		return false;

	u8 buf[SAVE_SIZE];
	memset(buf, 0, sizeof(buf));
	put32(buf, SAVE_MAGIC);
	put16(buf + 4, SAVE_VERSION);
	put16(buf + 6, 0);
	put32(buf + 12, playSeconds);
	put32(buf + 16, p->money);
	put32(buf + 20, p->exp);
	put16(buf + 24, (u16)p->mapIndex);
	buf[26] = (u8)p->tx;
	buf[27] = (u8)p->ty;
	buf[28] = (u8)p->dir;
	buf[29] = p->gender;
	buf[30] = p->age;
	buf[31] = p->pai;
	size_t nameLen = strlen(p->name);
	if (nameLen > ACTOR_NAME_LEN - 1)
		nameLen = ACTOR_NAME_LEN - 1;
	memcpy(buf + 32, p->name, nameLen);
	put16(buf + 48, (u16)p->hp);
	put16(buf + 50, (u16)p->hpMax);
	put16(buf + 52, (u16)p->mp);
	put16(buf + 54, (u16)p->mpMax);
	buf[56] = (u8)(p->food & 0xFF);
	buf[57] = (u8)(p->water & 0xFF);
	buf[58] = p->attack;
	buf[59] = p->defense;
	buf[60] = p->damage;
	buf[61] = p->armor;
	buf[62] = p->str;
	buf[63] = p->dex;
	buf[64] = p->intel;
	buf[65] = p->con;
	buf[66] = p->per;
	buf[67] = p->kar;
	buf[68] = p->weapon;
	memcpy(buf + 70, p->equip, EQUIP_SLOTS);
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		buf[82 + i * 2] = p->bag[i].id;
		buf[82 + i * 2 + 1] = p->bag[i].count;
	}
	put16(buf + 122, p->potential);
	put16(buf + 144, 0);   /* v3 legacy level; reserved, no longer used */
	put16(buf + 146, p->wins);
	put16(buf + 148, p->losses);
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		buf[150 + i * 4] = p->skills[i].id;
		buf[151 + i * 4] = p->skills[i].level;
		put16(buf + 152 + i * 4, p->skills[i].exp);
	}
	buf[240] = p->master;
	put16(buf + 241, (u16)p->effhp);
	buf[243] = p->attrStr;
	buf[244] = p->attrDex;
	buf[245] = p->attrIntel;
	buf[246] = p->attrCon;
	buf[247] = p->attrPer;
	buf[248] = p->attrKar;
	for (int i = 0; i < SKILL_USEKF_SLOTS; i++)
		buf[SAVE_V4_SIZE + i] = p->usekf[i];
	put16(buf + SAVE_V4_SIZE + 5, (u16)p->force);
	memcpy(buf + 264, p->deadNpc, 16);
	buf[280] = p->stones;
	buf[281] = p->killTarget;
	buf[282] = p->killComplete;
	buf[283] = (u8)p->morality;
	put16(buf + 284, p->killReward);
	put32(buf + 286, p->killReportTime);
	for (int i = 0; i < ACTOR_TASK_SLOTS; i++)
	{
		u8 *r = buf + SAVE_TASKS_OFF + i * SAVE_TASK_SIZE;
		r[0] = p->tasks[i].type;
		r[1] = p->tasks[i].state;
		r[2] = p->tasks[i].id;
		r[3] = p->tasks[i].index;
		put16(r + 4, p->tasks[i].reward);
		put32(r + 6, p->tasks[i].time);
	}
	buf[SAVE_GHOST_OFF + 0] = p->ghostXing;
	buf[SAVE_GHOST_OFF + 1] = p->ghostMing;
	buf[SAVE_GHOST_OFF + 2] = p->ghostGender;
	buf[SAVE_GHOST_OFF + 3] = p->ghostPai;
	buf[SAVE_GHOST_OFF + 4] = p->ghostMap;
	buf[SAVE_GHOST_OFF + 5] = p->ghostX;
	buf[SAVE_GHOST_OFF + 6] = p->ghostY;
	buf[SAVE_GHOST_OFF + 7] = p->ghostActive;
	buf[SAVE_ENDING_OFF] = p->endingSeen;
	put16(buf + SAVE_RANK_OFF, p->npcKill);
	put16(buf + SAVE_RANK_OFF + 2, p->guanKill);
	put16(buf + SAVE_RANK_OFF + 4, p->siKill);
	buf[SAVE_HOME_OFF] = p->homeBuf;
	put16(buf + SAVE_FOOD16_OFF, p->food);
	put16(buf + SAVE_WATER16_OFF, p->water);
	put16(buf + SAVE_HOUR_OFF, (u16)(actor_get_game_hour() & 0xFFFF));
	put32(buf + 8, crc32_calc(buf + CRC_START, SAVE_SIZE - CRC_START));

	char path[64], tmp[64], bak[64];
	slot_path(slot, path, sizeof(path), "sav");
	slot_path(slot, tmp, sizeof(tmp), "tmp");
	slot_path(slot, bak, sizeof(bak), "bak");

	FILE *f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = fwrite(buf, 1, sizeof(buf), f) == sizeof(buf);
	ok = (fclose(f) == 0) && ok;
	if (!ok)
	{
		remove(tmp);
		return false;
	}

	/* Keep one previous good copy; SD failures can still lose the rename
	 * chain, so a CRC failure falls back to .bak on read. */
	remove(bak);
	rename(path, bak);
	if (rename(tmp, path) != 0)
	{
		remove(tmp);
		return false;
	}
	return true;
}

static bool read_file(const char *path, u8 *buf, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	size_t n = fread(buf, 1, SAVE_SIZE, f);
	fclose(f);
	*size = n;
	return n >= SAVE_V1_SIZE;
}

bool save_delete(int slot)
{
	if (!s_fsReady || slot < 0 || slot >= SAVE_SLOT_COUNT)
		return false;
	char path[64];
	int removed = 0;
	slot_path(slot, path, sizeof(path), "sav");
	removed += remove(path) == 0;
	slot_path(slot, path, sizeof(path), "tmp");
	removed += remove(path) == 0;
	slot_path(slot, path, sizeof(path), "bak");
	removed += remove(path) == 0;
	return removed > 0;
}

bool save_read(int slot, Player *p, u32 *playSeconds)
{
	if (!s_fsReady || !p || slot < 0 || slot >= SAVE_SLOT_COUNT)
		return false;

	char path[64], bak[64];
	u8 buf[SAVE_SIZE];
	SaveSlotInfo info;
	size_t size = 0;

	bool valid = read_file(slot_path(slot, path, sizeof(path), "sav"), buf, &size);
	if (valid)
	{
		decode(buf, size, &info);
		valid = info.valid;
	}
	if (!valid)
	{
		size = 0;
		if (!read_file(slot_path(slot, bak, sizeof(bak), "bak"), buf, &size))
			return false;
		decode(buf, size, &info);
		if (!info.valid)
			return false;
	}

	/* v1/v2 saves have no kungfu book: seed the old migration baseline,
	 * including the four basic kungfu that M4 gave every character. New
	 * games no longer receive them (game.s man_kfnum = 0). */
	if (info.version < 3)
	{
		actor_stats_init(p, NULL);
		skill_add(p, SK_BASIC_FORCE, 20);
		skill_add(p, SK_BASIC_BARE, 20);
		skill_add(p, SK_BASIC_DODGE, 20);
		skill_add(p, SK_BASIC_PARRY, 20);
	}

	memcpy(p->name, info.name, sizeof(p->name) - 1);
	p->name[sizeof(p->name) - 1] = '\0';
	if (info.version >= 2)
	{
		p->gender = info.gender;
		p->age = info.age;
		p->pai = info.pai;
		p->str = info.str;
		p->dex = info.dex;
		p->intel = info.intel;
		p->con = info.con;
		p->per = info.per;
		p->kar = info.kar;
		p->attack = info.attack;
		p->defense = info.defense;
		p->damage = info.damage;
		p->armor = info.armor;
		p->exp = info.exp;
		p->potential = info.potential;
		p->weapon = info.weapon;
		memcpy(p->equip, info.equip, sizeof(p->equip));
		memcpy(p->bag, info.bag, sizeof(p->bag));
	}
	if (info.version >= 3)
	{
		p->wins = info.wins;
		p->losses = info.losses;
		memcpy(p->skills, info.skills, sizeof(p->skills));
	}
	if (info.version >= 4)
	{
		p->master = info.master;
		p->effhp = info.effhp > 0 ? info.effhp : p->hpMax;
		p->attrStr = info.attrStr;
		p->attrDex = info.attrDex;
		p->attrIntel = info.attrIntel;
		p->attrCon = info.attrCon;
		p->attrPer = info.attrPer;
		p->attrKar = info.attrKar;
	}
	else
	{
		/* Migrate: v1-v3 carried no innate stats, effhp or master. */
		p->master = 0;
		p->effhp = p->hpMax;
		p->attrStr = p->str;
		p->attrDex = p->dex;
		p->attrIntel = p->intel;
		p->attrCon = p->con;
		p->attrPer = p->per;
		p->attrKar = p->kar;
	}
	if (info.version >= 5)
	{
		memcpy(p->usekf, info.usekf, sizeof(p->usekf));
		p->force = info.force;
	}
	else
	{
		/* Migrate: pre-v5 saves start with everything disabled. */
		memset(p->usekf, 0, sizeof(p->usekf));
		p->force = 0;
	}
	memcpy(p->deadNpc, info.deadNpc, 16);
	p->stones = info.stones;
	p->killTarget = info.killTarget;
	p->killComplete = info.killComplete;
	/* v1-v5 saves predate the daode byte: start neutral (0x80) instead of
	 * the zeroed field, which would read as a wanted criminal. */
	p->morality = info.version >= 6 ? info.morality : (s8)0x80;
	p->killReward = info.killReward;
	p->killReportTime = info.killReportTime;
	memcpy(p->tasks, info.tasks, sizeof(p->tasks));
	p->ghostXing = info.ghostXing;
	p->ghostMing = info.ghostMing;
	p->ghostGender = info.ghostGender;
	p->ghostPai = info.ghostPai;
	p->ghostMap = info.ghostMap;
	p->ghostX = info.ghostX;
	p->ghostY = info.ghostY;
	p->ghostActive = info.ghostActive;
	p->endingSeen = info.endingSeen;
	p->npcKill = info.npcKill;
	p->guanKill = info.guanKill;
	p->siKill = info.siKill;
	p->homeBuf = info.homeBuf;
	p->money = info.money;
	p->hp = info.hp;
	p->hpMax = info.hpMax;
	p->mp = info.mp;
	p->mpMax = info.mpMax;
	p->food = info.food;
	p->water = info.water;
	p->mapIndex = info.mapIndex;
	p->dir = info.dir;
	if (playSeconds)
		*playSeconds = info.playSeconds;

	/* Derived values follow the original formulas after any migration.
	 * game.s enter_gmud/setup_maxhp + check_player: v1-v8 all carry the
	 * legacy hpMax, so recompute it from age/maxfp/红莲教义 and clamp
	 * hp <= effhp <= hpMax. */
	p->age = (u8)actor_age(info.playSeconds);
	actor_set_food_caps(p);
	actor_setup_attr(p);
	actor_setup_maxhp(p);

	/* system.s: game_sec/game_min/game_hour live in zero_area and are
	 * restored by the load; game_min restarts at 0 (display only). */
	actor_set_game_clock_base(info.gameHour);

	actor_place(p, info.tx, info.ty);
	return true;
}

#include "npcdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NPCS_MAGIC 0x43504E47u   /* "GNPC" little endian */
#define NPCS_VERSION 6
#define NPCS_HEADER 12
/* v6: desc 字段从 112 B 扩到 256 B，记录长度 232 -> 376。 */
#define NPCS_RECSIZE 376

/* Record layout (bytes). The desc string grew, so everything behind it moved. */
#define NPCS_OFF_NAME   87
#define NPCS_OFF_DESC   111
#define NPCS_OFF_FORCE  (NPCS_OFF_DESC + NPC_DESC_LEN)   /* 367 */

static NpcInfo *s_npcs;
static int s_count;
static NpcInfo s_ghost;
static bool s_ghostReady;

static u16 rd16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void npc_pack_free(void)
{
	free(s_npcs);
	s_npcs = NULL;
	s_count = 0;
}

bool npc_pack_load(const char *romfs_path)
{
	npc_pack_free();

	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;
	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return false;
	}
	long size = ftell(f);
	if (size < NPCS_HEADER)
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

	if (rd32(buf) != NPCS_MAGIC || rd16(buf + 4) != NPCS_VERSION)
	{
		free(buf);
		return false;
	}

	int count = (int)rd16(buf + 6);
	int recSize = (int)rd16(buf + 8);
	if (count <= 0 || recSize != NPCS_RECSIZE ||
	    size != NPCS_HEADER + count * recSize)
	{
		free(buf);
		return false;
	}

	s_npcs = (NpcInfo *)calloc((size_t)count, sizeof(NpcInfo));
	if (!s_npcs)
	{
		free(buf);
		return false;
	}

	for (int i = 0; i < count; i++)
	{
		const u8 *r = buf + NPCS_HEADER + i * recSize;
		NpcInfo *n = &s_npcs[i];
		n->id = r[0];
		n->pai = r[1];
		n->gender = r[2];
		n->age = r[3];
		n->str = r[4];
		n->dex = r[5];
		n->intel = r[6];
		n->con = r[7];
		n->per = r[8];
		n->kar = r[9];
		n->weapon = r[10];
		n->equip = r[11];
		n->vendorCount = r[12];
		if (n->vendorCount > VENDOR_MAX)
			n->vendorCount = VENDOR_MAX;
		memcpy(n->vendor, r + 13, VENDOR_MAX);
		n->kfCount = r[23];
		if (n->kfCount > NPC_KF_MAX)
			n->kfCount = NPC_KF_MAX;
		memcpy(n->usekf, r + 24, NPC_USEKF_MAX);
		memcpy(n->kf, r + 29, NPC_KF_MAX * 2);
		n->hp = rd16(r + 69);
		n->hpMax = rd16(r + 71);
		n->mp = rd16(r + 73);
		n->mpMax = rd16(r + 75);
		n->money = rd16(r + 77);
		n->damage = rd16(r + 79);
		n->armor = rd16(r + 81);
		n->exp = rd32(r + 83);
		memcpy(n->name, r + NPCS_OFF_NAME, NPC_NAME_LEN);
		n->name[NPC_NAME_LEN - 1] = '\0';
		memcpy(n->desc, r + NPCS_OFF_DESC, NPC_DESC_LEN);
		n->desc[NPC_DESC_LEN - 1] = '\0';
		n->force = rd16(r + NPCS_OFF_FORCE);
		n->thirdEquip = r[NPCS_OFF_FORCE + 2];
		n->equippedFlags = r[NPCS_OFF_FORCE + 3];
		n->fourthGoods = r[NPCS_OFF_FORCE + 4];
		n->morality = (s8)r[NPCS_OFF_FORCE + 5];
		n->attack = r[NPCS_OFF_FORCE + 6];
		n->defense = r[NPCS_OFF_FORCE + 7];
		n->busy = r[NPCS_OFF_FORCE + 8];
		if (n->id != i + 1 || n->hpMax == 0)
		{
			free(buf);
			npc_pack_free();
			return false;
		}
	}

	s_count = count;
	free(buf);
	return true;
}

int npc_count(void)
{
	return s_count;
}

const NpcInfo *npc_info(int npcId)
{
	if (npcId == 125)
		return s_ghostReady ? &s_ghost : NULL;
	if (npcId < 1 || npcId > s_count)
		return NULL;
	return &s_npcs[npcId - 1];
}

void npc_ghost_set(const NpcInfo *n)
{
	if (!n)
	{
		npc_ghost_clear();
		return;
	}
	s_ghost = *n;
	s_ghost.id = 125;
	s_ghostReady = true;
}

void npc_ghost_clear(void)
{
	s_ghostReady = false;
}

bool npc_ghost_active(void)
{
	return s_ghostReady;
}

bool npc_is_merchant(const NpcInfo *n)
{
	return n && n->pai == 0xFF;
}

bool npc_is_hostile(int npcId)
{
	static const u8 HOSTILE[] = { 4, 8, 18, 21, 22, 105, 106 };
	for (unsigned i = 0; i < sizeof(HOSTILE); i++)
	{
		if (HOSTILE[i] == npcId)
			return true;
	}
	return false;
}

const char *npc_pai_name(int pai)
{
	switch (pai)
	{
	case 0: return "无门无派";
	case 1: return "八卦门";
	case 2: return "百花谷";
	case 3: return "红莲教";
	case 4: return "哪吒派";
	case 5: return "太极门";
	case 6: return "雪山派";
	case 7: return "逍遥派";
	case 0xFF: return "商贾";
	default: return "江湖中人";
	}
}

#ifndef NPCDATA_H
#define NPCDATA_H

#include "gmud.h"
#include "item.h"

#define NPC_NAME_LEN 24
/* npc_data.gb 的描述最长 194 B（69 字），字段必须放得下整段，
 * 否则「查看」只会显示前 111 B，后半段被悄悄截断。 */
#define NPC_DESC_LEN 256
#define VENDOR_MAX 10
#define NPC_KF_MAX 20
#define NPC_USEKF_MAX 5

typedef struct
{
	u8 id;          /* npcId as referenced by the map NPC table (1-based) */
	u8 pai;         /* 0 = none, 0xFF = merchant, others = schools */
	u8 gender;
	u8 age;
	u8 str, dex, intel, con, per, kar;
	u8 weapon;      /* NPC goods slot 0 (bit7 stripped) */
	u8 equip;       /* NPC goods slot 1 (bit7 stripped) */
	u8 vendorCount;
	u8 vendor[VENDOR_MAX];
	u8 kfCount;
	u8 usekf[NPC_USEKF_MAX];       /* bit7 enabled, low 7 bits = kf id */
	u8 kf[NPC_KF_MAX * 2];         /* (kf id, level) pairs */
	u16 hp, hpMax, mp, mpMax;
	u16 money;
	u16 damage;     /* base + weapon effect (npc.s init_npc) */
	u16 armor;      /* equip effect; npc_data has no armor byte */
	u16 force;
	u8 thirdEquip;
	u8 equippedFlags;
	u8 busy;
	u8 fourthGoods;
	s8 morality;
	u8 attack, defense;
	u32 exp;
	char name[NPC_NAME_LEN];
	char desc[NPC_DESC_LEN];
} NpcInfo;

bool npc_pack_load(const char *romfs_path);
void npc_pack_free(void);
int npc_count(void);
/* npcId is the byte stored in the map NPC table; valid ids start at 1. */
const NpcInfo *npc_info(int npcId);

/* Runtime ghost (task.s copy_status). While set, npc_info(125) returns it;
 * the pack itself only carries a placeholder at that id. */
void npc_ghost_set(const NpcInfo *n);
void npc_ghost_clear(void);
bool npc_ghost_active(void);
bool npc_is_merchant(const NpcInfo *n);
/* NPCs that offer a fight in V1 (M4); story NPCs are not attackable yet. */
bool npc_is_hostile(int npcId);
const char *npc_pai_name(int pai);

#endif

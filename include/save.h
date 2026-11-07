#ifndef SAVE_H
#define SAVE_H

#include "actor.h"
#include "gmud.h"

/* Three manual slots plus the hidden auto slot.  The auto slot is written by
 * the game itself (map change / task reward / every 120 s in the field) and
 * never overwrites a manual save, so a crash or a 结束 never costs more than
 * the last couple of minutes.  Manual slot numbers stay 0..2, so existing
 * save0..save2 files are untouched; the auto slot lives in save3.sav. */
#define SAVE_SLOTS 3
#define SAVE_SLOT_AUTO SAVE_SLOTS
#define SAVE_SLOT_COUNT (SAVE_SLOTS + 1)

typedef struct
{
	bool exists;
	bool valid;
	int version;
	u32 playSeconds;
	int mapIndex, tx, ty, dir;
	char name[ACTOR_NAME_LEN];
	u8 gender, age, pai;
	u8 str, dex, intel, con, per, kar;
	u8 attack, defense, damage, armor;
	u32 money;
	u32 exp;
	u16 potential;
	s16 hp, hpMax, mp, mpMax;
	u16 food, water;   /* original man_food/man_water are 16-bit */
	u8 weapon;
	u8 equip[EQUIP_SLOTS];
	BagSlot bag[BAG_SLOTS];
	/* v3's offset 144 was a player "level"; T2 removed the concept. The
	 * bytes stay reserved (always written as 0) to keep the layout intact. */
	u16 wins, losses;
	SkillSlot skills[SKILL_SLOTS];
	/* v4: master NPC, injury cap and innate six stats. */
	u8 master;
	s16 effhp;
	u8 attrStr, attrDex, attrIntel, attrCon, attrPer, attrKar;
	/* v5: enabled kungfu slots and the 加力 charge. */
	u8 usekf[SKILL_USEKF_SLOTS];
	s16 force;
	u8 deadNpc[16], stones, killTarget, killComplete;
	s8 morality;
	u16 killReward;
	u32 killReportTime;
	/* v8: 寻人/寻物/杀NPC/动态鬼 slots and the placed ghost. */
	ActorTask tasks[ACTOR_TASK_SLOTS];
	u8 ghostXing, ghostMing, ghostGender, ghostPai;
	u8 ghostMap, ghostX, ghostY, ghostActive;
	u8 endingSeen;
	u8 homeBuf;
	u16 npcKill, guanKill, siKill;
	/* v8 tail (350-351): game_hour, saved inside the original zero_area
	 * block (h/gmud.h:334-336 + save.s SAVE_SIZE). */
	u16 gameHour;
} SaveSlotInfo;

/* Initializes the SD filesystem and creates sdmc:/3ds/gmud/. */
bool save_init(void);
void save_fini(void);

bool save_write(int slot, const Player *p, u32 playSeconds);
bool save_read(int slot, Player *p, u32 *playSeconds);
bool save_delete(int slot);
bool save_slot_info(int slot, SaveSlotInfo *out);

#endif

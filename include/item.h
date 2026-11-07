#ifndef ITEM_H
#define ITEM_H

#include "gmud.h"

#define ITEM_MAX 128
#define BAG_SLOTS 20
#define EQUIP_SLOTS 11
#define ITEM_NAME_LEN 22
/* h/id.h goods_id CLOTH = 42; game.s clear_apply puts one in a new bag. */
#define ITEM_CLOTH 42
/* h/id.h SANJIAO: the six-slab key item. goods.s set_all_goods removes it
 * from the sell/drop lists, task.s removes it from the 寻物 candidates. */
#define ITEM_SANJIAO 77

enum
{
	ITEM_FOOD = 0,
	ITEM_DRUG = 1,
	ITEM_WEAPON = 2,
	ITEM_EQUIP = 3,
	ITEM_BOOK = 4,
	ITEM_OTHER = 5
};

/* Armor sub types come straight from the original goods table. */
enum
{
	ARMOR_HEAD = 0,
	ARMOR_NECK,
	ARMOR_CLOTH,
	ARMOR_BODY,
	ARMOR_SURCOAT,
	ARMOR_WAIST,
	ARMOR_WRISTS,
	ARMOR_SHIELD,
	ARMOR_FINGER,
	ARMOR_HANDS,
	ARMOR_BOOTS
};

#define BAG_ID(s) ((int)((s).id & 0x7F))
#define BAG_EQUIPPED(s) (((s).id & 0x80) != 0)
#define BAG_EMPTY(s) (BAG_ID(s) == 0)

typedef struct
{
	u8 id;
	u8 count;
} BagSlot;

typedef struct
{
	u8 id;
	u8 type;
	u8 sub;
	u8 effect;    /* food: satiety; weapon: damage; armor: defense; drug flag */
	u8 effect2;   /* food: water; weapon/armor: attack modifier (s8) */
	u8 defBonus;  /* weapon/armor: defense modifier (s8) */
	u8 flags;
	u16 price;
	char name[ITEM_NAME_LEN];
} ItemDef;

/* Loads data/items.bin (romfs:/items.bin). */
bool item_pack_load(const char *romfs_path);
void item_pack_free(void);
int item_count(void);
const ItemDef *item_get(int id);

const char *item_name(int id);
int item_buy_price(int id);
int item_sell_price(int id);
bool item_stacks(int id);
const char *item_type_name(int id);
/* Writes a short effect line ("攻击 +5", "饱食 +160 口渴 +0"). */
void item_describe(int id, char *out, int size);

/* Inventory helpers; bag points at Player.bag (BAG_SLOTS entries). */
int inv_add(BagSlot *bag, int itemId);
int inv_count(const BagSlot *bag, int itemId);
bool inv_remove_slot(BagSlot *bag, int slot, int count);
int inv_used_slots(const BagSlot *bag);
/* Filled slot indices in bag order (the 行囊 page lists these). Returns the
 * count, capped at max. */
int inv_list_slots(const BagSlot *bag, int *slots, int max);

#endif

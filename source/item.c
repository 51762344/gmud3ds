#include "item.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITEMS_MAGIC 0x4D544947u   /* "GITM" little endian */
#define ITEMS_VERSION 2          /* v2 adds the equipment defense modifier */
#define ITEMS_HEADER 12
#define ITEMS_RECSIZE 32

static ItemDef *s_items;
static int s_count;

static u16 rd16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void item_pack_free(void)
{
	free(s_items);
	s_items = NULL;
	s_count = 0;
}

bool item_pack_load(const char *romfs_path)
{
	item_pack_free();

	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;

	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return false;
	}
	long size = ftell(f);
	if (size < ITEMS_HEADER)
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

	if (rd32(buf) != ITEMS_MAGIC || rd16(buf + 4) != ITEMS_VERSION)
	{
		free(buf);
		return false;
	}

	int count = (int)rd16(buf + 6);
	int recSize = (int)rd16(buf + 8);
	if (count <= 0 || recSize != ITEMS_RECSIZE ||
	    size != ITEMS_HEADER + count * recSize)
	{
		free(buf);
		return false;
	}

	s_items = (ItemDef *)calloc((size_t)count, sizeof(ItemDef));
	if (!s_items)
	{
		free(buf);
		return false;
	}

	for (int i = 0; i < count; i++)
	{
		const u8 *r = buf + ITEMS_HEADER + i * recSize;
		ItemDef *d = &s_items[i];
		d->id = r[0];
		d->type = r[1];
		d->sub = r[2];
		d->effect = r[3];
		d->effect2 = r[4];
		d->defBonus = r[5];
		d->flags = r[30];
		d->price = rd16(r + 6);
		memcpy(d->name, r + 8, ITEM_NAME_LEN);
		d->name[ITEM_NAME_LEN - 1] = '\0';
		if (d->id != i)
		{
			free(buf);
			item_pack_free();
			return false;
		}
	}

	s_count = count;
	free(buf);
	return true;
}

int item_count(void)
{
	return s_count;
}

const ItemDef *item_get(int id)
{
	if (id < 0 || id >= s_count)
		return NULL;
	return &s_items[id];
}

const char *item_name(int id)
{
	const ItemDef *d = item_get(id);
	return d ? d->name : "?";
}

int item_buy_price(int id)
{
	const ItemDef *d = item_get(id);
	return d ? (int)d->price : 0;
}

int item_sell_price(int id)
{
	const ItemDef *d = item_get(id);
	if (!d)
		return 0;
	return (int)d->price * 7 / 10;
}

bool item_stacks(int id)
{
	const ItemDef *d = item_get(id);
	if (!d)
		return false;
	return d->type != ITEM_WEAPON && d->type != ITEM_EQUIP;
}

const char *item_type_name(int id)
{
	const ItemDef *d = item_get(id);
	if (!d)
		return "";
	switch (d->type)
	{
	case ITEM_FOOD: return "食物";
	case ITEM_DRUG: return "药品";
	case ITEM_WEAPON: return "武器";
	case ITEM_EQUIP: return "护具";
	case ITEM_BOOK: return "书籍";
	default: return "杂物";
	}
}

void item_describe(int id, char *out, int size)
{
	const ItemDef *d = item_get(id);
	if (!out || size <= 0)
		return;
	out[0] = '\0';
	if (!d)
		return;

	switch (d->type)
	{
	case ITEM_FOOD:
		snprintf(out, (size_t)size, "饱食 +%d  口渴 +%d", d->effect, d->effect2);
		break;
	case ITEM_DRUG:
		if (d->sub == 1)
			snprintf(out, (size_t)size, "内力上限 +%d", d->effect);
		else if (d->effect == 0)
			snprintf(out, (size_t)size, "回复伤势上限（一半）");
		else
			snprintf(out, (size_t)size, "回复伤势上限（四分之一）");
		break;
	case ITEM_WEAPON:
		if (d->effect2 || d->defBonus)
			snprintf(out, (size_t)size, "伤害 +%d  命中 %+d  回避 %+d",
			         d->effect, (int)(s8)d->effect2, (int)(s8)d->defBonus);
		else
			snprintf(out, (size_t)size, "伤害 +%d", d->effect);
		break;
	case ITEM_EQUIP:
		if (d->effect2 || d->defBonus)
			snprintf(out, (size_t)size, "防御 +%d  命中 %+d  回避 %+d",
			         d->effect, (int)(s8)d->effect2, (int)(s8)d->defBonus);
		else
			snprintf(out, (size_t)size, "防御 +%d", d->effect);
		break;
	case ITEM_BOOK:
		snprintf(out, (size_t)size, "武学秘籍（可研读）");
		break;
	default:
		snprintf(out, (size_t)size, "寻常物事");
		break;
	}
}

int inv_count(const BagSlot *bag, int itemId)
{
	int n = 0;
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		if (BAG_ID(bag[i]) == itemId)
			n += bag[i].count;
	}
	return n;
}

int inv_used_slots(const BagSlot *bag)
{
	int n = 0;
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		if (!BAG_EMPTY(bag[i]))
			n++;
	}
	return n;
}

int inv_list_slots(const BagSlot *bag, int *slots, int max)
{
	int n = 0;
	if (!slots || max <= 0)
		return 0;
	for (int i = 0; i < BAG_SLOTS && n < max; i++)
	{
		if (!BAG_EMPTY(bag[i]))
			slots[n++] = i;
	}
	return n;
}

int inv_add(BagSlot *bag, int itemId)
{
	const ItemDef *d = item_get(itemId);
	if (!d || itemId == 0)
		return -1;

	if (item_stacks(itemId))
	{
		for (int i = 0; i < BAG_SLOTS; i++)
		{
			if (BAG_ID(bag[i]) == itemId && bag[i].count < 255)
			{
				bag[i].count++;
				return i;
			}
		}
	}

	for (int i = 0; i < BAG_SLOTS; i++)
	{
		if (BAG_EMPTY(bag[i]))
		{
			bag[i].id = (u8)itemId;
			bag[i].count = 1;
			return i;
		}
	}
	return -1;
}

bool inv_remove_slot(BagSlot *bag, int slot, int count)
{
	if (slot < 0 || slot >= BAG_SLOTS || count <= 0)
		return false;
	BagSlot *s = &bag[slot];
	if (BAG_EMPTY(*s) || BAG_EQUIPPED(*s))
		return false;
	if (s->count > count)
	{
		s->count = (u8)(s->count - count);
		return true;
	}
	if (s->count < count)
		return false;   /* refuse to delete more than the slot holds */
	s->id = 0;
	s->count = 0;
	return true;
}

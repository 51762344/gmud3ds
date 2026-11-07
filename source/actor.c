#include "actor.h"

#include "gfx.h"
#include "log.h"
#include "task.h"

#include <string.h>

#define PLAYER_SPEED 200.0f  /* pixels per second */

/* system.s game_hour: one real hour per unit. game.s show_desc uses its
 * low bit to flip a 人妖 (gender 2) between the male/female 相貌 tables.
 * The clock sits in zero_area, so a load restores it and a new game clears
 * it; s_clockBase is the restored hour and s_clockMs the process delta. */
static u32 s_gameHour;
/* system.s game_min: 0..59, the minute half of the same RTC clock. */
static u32 s_gameMin;
static u32 s_clockBase;
static u64 s_clockMs;

static int dir_from_delta(int dx, int dy)
{
	if (dx < 0) return DIR_LEFT;
	if (dx > 0) return DIR_RIGHT;
	if (dy < 0) return DIR_UP;
	return DIR_DOWN;
}

void actor_stats_init(Player *p, const CharCreateResult *r)
{
	CharCreateResult fallback;
	if (!r)
	{
		cc_default_result(&fallback);
		r = &fallback;
	}

	/* game.s man_init_data + input.s input_new + set_attr + clear_apply:
	 * attack/defense/damage/armor start at 0, hp is 100, fp/maxfp are 0,
	 * no kungfu is learned, food/water are 100 and the bag holds one 布衣.
	 * setup_attr below adds basic kungfu /10 (zero for a new character). */
	memcpy(p->name, r->name, ACTOR_NAME_LEN);
	p->name[ACTOR_NAME_LEN - 1] = '\0';
	p->gender = r->gender;
	p->age = ACTOR_BASE_AGE;
	p->pai = 0;
	p->attack = 0;
	p->defense = 0;
	p->damage = 0;
	p->armor = 0;
	p->money = 100;
	p->exp = 0;
	p->potential = 0;
	/* hp/maxhp=100, fp/maxfp=0 in man_init_data; setup_maxhp keeps the
	 * 100 hp because a 14-year-old with no 内力 has 100 + 0. */
	p->hp = 100;
	p->hpMax = 100;
	p->mp = 0;
	p->mpMax = 0;
	p->food = 100;
	p->water = 100;
	p->weapon = 0;
	memset(p->equip, 0, sizeof(p->equip));
	memset(p->bag, 0, sizeof(p->bag));
	/* game.s clear_apply: man_goods[0] = CLOTH, count 1 (not equipped). */
	inv_add(p->bag, ITEM_CLOTH);
	p->wins = 0;
	p->losses = 0;
	p->master = 0;
	memset(p->usekf, 0, sizeof(p->usekf));
	p->force = 0;
	memset(p->deadNpc, 0, sizeof(p->deadNpc));
	p->stones = p->killTarget = p->killComplete = 0;
	p->morality = -128;   /* man_daode = 128 */
	p->killReward = 0;
	p->killReportTime = 0;
	memset(p->tasks, 0, sizeof(p->tasks));
	p->ghostXing = p->ghostMing = 0;
	p->ghostGender = p->ghostPai = 0;
	p->ghostMap = p->ghostX = p->ghostY = 0;
	p->ghostActive = 0;
	p->endingSeen = 0;
	p->homeBuf = 0;
	p->npcKill = p->guanKill = p->siKill = 0;
	/* Innate six stats: four from adjust_attr, 相貌/福缘 from the two
	 * random_it(21) draws in input.s input_new. */
	p->attrStr = r->attr[CC_ATTR_STR];
	p->attrDex = r->attr[CC_ATTR_DEX];
	p->attrIntel = r->attr[CC_ATTR_INT];
	p->attrCon = r->attr[CC_ATTR_CON];
	p->attrPer = r->per;
	p->attrKar = r->kar;
	memset(p->skills, 0, sizeof(p->skills));
	p->effhp = 100;
	actor_set_food_caps(p);
	actor_setup_attr(p);
	/* game.s enter_gmud: setup_maxhp + check_player; a new character then
	 * starts with full vitals. */
	actor_setup_maxhp(p);
	actor_full_heal(p);
}

int actor_age(u32 playSeconds)
{
	return gmud_man_age(playSeconds);
}

void actor_setup_maxhp(Player *p)
{
	if (!p)
		return;
	/* find_kf(JIAOYI_KF) is the learned 红莲教义 level, 0 when unlearned. */
	int maxhp = gmud_setup_maxhp(p->age, p->mpMax,
	                             skill_level(p, SK_JIAOYI), p->attrCon);
	/* The original man_maxhp is an unsigned 16-bit add; Player stores s16. */
	if (maxhp > 32767)
		maxhp = 32767;
	if (maxhp < 1)
		maxhp = 1;
	p->hpMax = (s16)maxhp;

	int hp = p->hp, effhp = p->effhp;
	gmud_check_vitals(maxhp, &hp, &effhp);
	p->hp = (s16)hp;
	p->effhp = (s16)effhp;
}

void actor_sync_time(Player *p, u32 playSeconds)
{
	if (!p)
		return;
	p->age = (u8)actor_age(playSeconds);
	actor_setup_maxhp(p);
}

/* system.s game_hour is an independent RTC counter stored in zero_area:
 * a save persists it (save.s SAVE_SIZE covers game_sec/min/hour), a load
 * restores it and a new game clears it. rank.s prints game_hour:game_min
 * and checks game_hour < 48 for 神行太保. */
void actor_set_game_clock_base(u32 hours)
{
	s_clockBase = hours;
	s_clockMs = 0;
	s_gameHour = hours;
	s_gameMin = 0;
}

void actor_advance_game_clock(u64 deltaMs)
{
	s_clockMs += deltaMs;
	s_gameHour = s_clockBase + (u32)(s_clockMs / 3600000ull);
	s_gameMin = (u32)((s_clockMs / 60000ull) % 60ull);
}

u32 actor_get_game_hour(void)
{
	return s_gameHour;
}

u32 actor_get_game_min(void)
{
	return s_gameMin;
}

/* skill.s setup_attr: man_str=attr_str+基本拳脚/10, dex=+基本轻功/10,
 * int=+读书识字/10, con=+基本内功/10, per=+相貌/10. */
void actor_setup_attr(Player *p)
{
	if (!p)
		return;
	p->str = (u8)((int)p->attrStr + skill_level(p, SK_BASIC_BARE) / 10);
	p->dex = (u8)((int)p->attrDex + skill_level(p, SK_BASIC_DODGE) / 10);
	p->intel = (u8)((int)p->attrIntel + skill_level(p, SK_LITERATE) / 10);
	p->con = (u8)((int)p->attrCon + skill_level(p, SK_BASIC_FORCE) / 10);
	p->per = (u8)((int)p->attrPer + skill_level(p, SK_LOOKS) / 10);
	p->kar = p->attrKar;
}

/* game.s:365-378 set_attr: maxfood=(attr_str+5)*15, maxwater=(attr_str+4)*15.
 * The original keeps food/water as 16-bit values; 100 is only the starting
 * current value in man_init_data, not the cap. */
void actor_set_food_caps(Player *p)
{
	if (!p)
		return;
	p->maxFood = (u16)(((int)p->attrStr + 5) * 15);
	p->maxWater = (u16)(((int)p->attrStr + 4) * 15);
}

/* system.s to_restore tick (every TICK_TIME seconds while not busy):
 * food==0 -> nothing at all; water==0 -> nothing; then
 * hp += con/2 + maxfp/16 (capped at effhp, effhp +1 when touched) and
 * fp += 基本内功 level (only while fp < maxfp, capped at maxfp). */
void actor_regen_tick(Player *p)
{
	if (!p)
		return;
	if (p->food == 0)
		return;
	p->food--;
	if (p->water == 0)
		return;
	p->water--;

	int kee = p->con / 2 + p->mpMax / 16;
	p->hp = (s16)(p->hp + kee);
	if (p->hp >= p->effhp)
	{
		p->hp = p->effhp;
		if (p->effhp < p->hpMax)
			p->effhp++;
	}

	if (p->mpMax > 0 && p->mp < p->mpMax)
	{
		int force = skill_level(p, SK_BASIC_FORCE);
		if (force > 0)
		{
			p->mp = (s16)(p->mp + force);
			if (p->mp > p->mpMax)
				p->mp = p->mpMax;
		}
	}
}

/* The original only accumulates man_exp; there is no player level and no
 * stat/HP bonus on a quest reward (task.s give_exp). */
void actor_gain_exp(Player *p, u32 amount)
{
	if (!p || amount == 0)
		return;
	u64 total = (u64)p->exp + amount;
	if (total > 0xFFFFFFFFu)
		total = 0xFFFFFFFFu;
	p->exp = (u32)total;
}

void actor_full_heal(Player *p)
{
	if (!p)
		return;
	p->hp = p->hpMax;
	p->effhp = p->hpMax;
	p->mp = p->mpMax;
}

static int clamp_u8(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* goods.s wield_goods/unwield_goods: `adc/sbc (a1),y` then `bpl`; a result
 * with bit7 set (including a signed-negative delta) is replaced by 0. */
static u8 mod_add(u8 base, u8 raw)
{
	u8 v = (u8)(base + raw);
	return (v & 0x80) ? 0 : v;
}

static u8 mod_sub(u8 base, u8 raw)
{
	u8 v = (u8)(base - raw);
	return (v & 0x80) ? 0 : v;
}

/* Clears the equipped bit and the cached bonus for one item id. */
static void unequip_id(Player *p, int id)
{
	const ItemDef *d = item_get(id);
	if (!d)
		return;

	for (int i = 0; i < BAG_SLOTS; i++)
	{
		if (BAG_ID(p->bag[i]) == id && BAG_EQUIPPED(p->bag[i]))
			p->bag[i].id = (u8)id;
	}

	if (d->type == ITEM_WEAPON && p->weapon == id)
	{
		p->damage = (u8)clamp_u8((int)p->damage - (int)d->effect);
		p->attack = mod_sub(p->attack, d->effect2);
		p->defense = mod_sub(p->defense, d->defBonus);
		p->weapon = 0;
	}
	else if (d->type == ITEM_EQUIP && d->sub < EQUIP_SLOTS && p->equip[d->sub] == id)
	{
		p->armor = (u8)clamp_u8((int)p->armor - (int)d->effect);
		p->attack = mod_sub(p->attack, d->effect2);
		p->defense = mod_sub(p->defense, d->defBonus);
		p->equip[d->sub] = 0;
	}
}

bool actor_equip_item(Player *p, int slot)
{
	if (slot < 0 || slot >= BAG_SLOTS)
		return false;
	BagSlot *s = &p->bag[slot];
	int id = BAG_ID(*s);
	if (id == 0)
		return false;

	const ItemDef *d = item_get(id);
	if (!d || (d->type != ITEM_WEAPON && d->type != ITEM_EQUIP))
		return false;

	if (BAG_EQUIPPED(*s))
	{
		unequip_id(p, id);
		log_add("已卸下「%s」。", d->name);
		return true;
	}

	if (d->type == ITEM_WEAPON)
	{
		/* Always clear the previous weapon: two identical weapons live in
		 * separate bag slots, and only one may carry the equipped bit. */
		if (p->weapon)
			unequip_id(p, p->weapon);
		p->weapon = (u8)id;
		p->damage = (u8)clamp_u8((int)p->damage + (int)d->effect);
		p->attack = mod_add(p->attack, d->effect2);
		p->defense = mod_add(p->defense, d->defBonus);
	}
	else
	{
		if (d->sub >= EQUIP_SLOTS)
			return false;
		if (p->equip[d->sub])
			unequip_id(p, p->equip[d->sub]);
		p->equip[d->sub] = (u8)id;
		p->armor = (u8)clamp_u8((int)p->armor + (int)d->effect);
		p->attack = mod_add(p->attack, d->effect2);
		p->defense = mod_add(p->defense, d->defBonus);
	}

	s->id = (u8)(id | 0x80);
	log_add("已装备「%s」。", d->name);
	return true;
}

static void consume_one(Player *p, int slot)
{
	if (p->bag[slot].count > 1)
		p->bag[slot].count--;
	else
	{
		p->bag[slot].id = 0;
		p->bag[slot].count = 0;
	}
}

bool actor_use_item(Player *p, int slot)
{
	if (slot < 0 || slot >= BAG_SLOTS)
		return false;
	BagSlot *s = &p->bag[slot];
	int id = BAG_ID(*s);
	if (id == 0 || BAG_EQUIPPED(*s))
		return false;

	const ItemDef *d = item_get(id);
	if (!d)
		return false;

	if (d->type == ITEM_FOOD)
	{
		/* goods.s use_food: only reject when food >= maxfood; the original
		 * then adds the item values without clamping to the caps. */
		if (p->food >= p->maxFood)
		{
			log_add("你已经吃饱了。");
			return false;
		}
		p->food = (u16)(p->food + d->effect);
		p->water = (u16)(p->water + d->effect2);
		consume_one(p, slot);
		log_add("你吃了「%s」，饱食 %d，口渴 %d。", d->name, (int)p->food,
		        (int)p->water);
		return true;
	}

	if (d->type == ITEM_DRUG)
	{
		if (d->sub == 1)
		{
			/* Original improve_maxhp only raises maxfp, not current fp.
			 * maxhp = ... + maxfp/4, so recompute it here. */
			/* man_maxfp is u16 in the original; Player stores s16, so clamp
			 * before the cast or repeated 内力丹 wraps it negative (and
			 * gmud_setup_maxhp would silently drop the maxfp/4 term). */
			int mpMax = p->mpMax + d->effect;
			if (mpMax > 30000)
				mpMax = 30000;
			p->mpMax = (s16)mpMax;
			actor_setup_maxhp(p);
			consume_one(p, slot);
			log_add("你服下「%s」，内力上限 +%d。", d->name, d->effect);
			return true;
		}

		/* goods.s improve_effhp: the gate is effhp >= maxhp, and the drug
		 * raises the INJURY CAP, not the current hp. hp then recovers up to
		 * effhp on the 15 s tick / 吸气 / 疗伤. */
		if (p->effhp >= p->hpMax)
		{
			log_add("你并没有受伤，不必服药。");
			return false;
		}
		int heal = d->effect == 0 ? p->hpMax / 2 : p->hpMax / 4;
		if (heal < 1)
			heal = 1;
		int before = p->effhp;
		p->effhp = (s16)(p->effhp + heal > p->hpMax ? p->hpMax : p->effhp + heal);
		consume_one(p, slot);
		log_add("你服下「%s」，伤势上限 +%d。", d->name, p->effhp - before);
		return true;
	}

	if (d->type == ITEM_BOOK)
	{
		/* goods.s use_book -> skill.s study_cmd. The item menu opens the
		 * 研读 flow in main.c; this path is only a defensive fallback. */
		log_add("「%s」需要从行囊里翻开来研读。", d->name);
		return false;
	}

	log_add("「%s」似乎没什么用。", d->name);
	return false;
}

bool actor_drop_item(Player *p, int slot)
{
	if (slot < 0 || slot >= BAG_SLOTS)
		return false;
	BagSlot *s = &p->bag[slot];
	int id = BAG_ID(*s);
	if (id == 0)
		return false;
	/* goods.s set_all_goods drops SANJIAO from the sell/drop list. */
	if (id == ITEM_SANJIAO)
	{
		log_add("三角石板无法丢弃。");
		return false;
	}
	if (BAG_EQUIPPED(*s))
	{
		log_add("先把「%s」卸下再丢。", item_name(id));
		return false;
	}

	if (!inv_remove_slot(p->bag, slot, 1))
		return false;
	log_add("你丢掉了「%s」。", item_name(id));
	return true;
}

int actor_attack_power(const Player *p)
{
	return (int)p->attack + (int)p->damage;
}

int actor_defense_power(const Player *p)
{
	return (int)p->defense + (int)p->armor;
}

/* skill.s get_skill_desc: 50 rank strings, each 8 bytes in the original. */
const char *actor_martial_desc_levels(int attack, int dodge, int parry)
{
	static const char *const ranks[] =
	{
		"不堪一击", "毫不足虑", "不足挂齿", "初学乍练", "勉勉强强",
		"初窥门径", "初出茅庐", "略知一二", "普普通通", "平平常常",
		"平淡无奇", "粗通皮毛", "半生不熟", "登堂入室", "略有小成",
		"已有小成", "鹤立鸡群", "驾轻就熟", "青出于蓝", "融会贯通",
		"心领神会", "炉火纯青", "了然于胸", "略有大成", "已有大成",
		"豁然贯通", "非比寻常", "出类拔萃", "罕有敌手", "技冠群雄",
		"神乎其技", "出神入化", "傲视群雄", "登峰造极", "无与伦比",
		"所向披靡", "一代宗师", "精深奥妙", "神功盖世", "举世无双",
		"惊世骇俗", "撼天动地", "震古铄今", "超凡入圣", "威镇寰宇",
		"空前绝后", "天人合一", "深藏不露", "深不可测", "返璞归真"
	};
	int rank = (attack + (dodge + parry) / 2) / 3 / 5;
	if (rank < 0) rank = 0;
	if (rank > 49) rank = 49;
	return ranks[rank];
}

const char *actor_martial_desc(const Player *p)
{
	if (!p)
		return "";
	int type = p->weapon ? SKILL_TYPE_WEAPON : SKILL_TYPE_HAND;
	return actor_martial_desc_levels(skill_query_level(p, type),
	                                 skill_query_level(p, SKILL_TYPE_DODGE),
	                                 skill_query_level(p, SKILL_TYPE_PARRY));
}

/* skill.s get_str_desc: (str + damage + force/2)/20, clamped to 0..5. */
const char *actor_strength_desc(const Player *p)
{
	static const char *const desc[] =
	{
		"极轻", "很轻", "不轻", "不重", "很重", "极重"
	};
	if (!p)
		return "";
	int point = (int)p->str + (int)p->damage + (int)p->force / 2;
	int idx = point / 20;
	if (idx < 0) idx = 0;
	if (idx > 5) idx = 5;
	return desc[idx];
}

/* skill.s get_per_desc: under 16 is always 一脸稚气, else (per-10)/3. */
const char *actor_looks_desc(const Player *p)
{
	/* game.s man_per_desc / girl_per_desc, one 18-byte record per rank. */
	static const char *const male[] =
	{
		"一塌糊涂,不是人样", "牛眼马嘴,面目狰狞", "小鼻小眼,一脸麻子", "相貌平平,还过得去",
		"五官端正,身材匀称", "相貌英俊,双目有神", "气宇轩昂,骨骼清奇", "风流俊雅,仪表堂堂"
	};
	static const char *const female[] =
	{
		"貌赛无盐,惨不忍睹", "眼小嘴大,相貌丑陋", "看上去...马马虎虎", "相貌平平,还过得去",
		"身材娇好,尚有资色", "婷婷玉立,美貌如花", "沉鱼落雁,闭月羞花", "美奂绝伦,人间仙子"
	};
	if (!p)
		return "";
	if (p->age < 16)
		return "一脸稚气";
	int per = p->per > 31 ? 31 : p->per;
	int idx = (per - 10) / 3;
	if (idx < 0) idx = 0;
	if (idx > 7) idx = 7;
	/* game.s:621-625: gender 2 (人妖) alternates with game_hour's parity;
	 * odd hours show the male table, even hours the female one. */
	if (p->gender == 2)
		return (s_gameHour & 1u) ? male[idx] : female[idx];
	return p->gender ? female[idx] : male[idx];
}

void actor_place(Player *p, int tx, int ty)
{
	p->tx = tx;
	p->ty = ty;
	p->px = tx * TILE;
	p->py = ty * TILE;
	p->moving = false;
	p->justArrived = false;
	p->walkFrame = 0;
	p->t = 0.0f;
}

bool actor_init(Player *p, const GameMap *m, int mapIndex, int tx, int ty)
{
	p->mapIndex = mapIndex;
	p->dir = DIR_DOWN;
	actor_place(p, tx, ty);
	if (!map_blocked(m, tx, ty))
		return true;

	for (int r = 1; r <= 8; r++)
	{
		for (int dy = -r; dy <= r; dy++)
		{
			for (int dx = -r; dx <= r; dx++)
			{
				if (dx > -r && dx < r && dy > -r && dy < r)
					continue;
				int nx = tx + dx;
				int ny = ty + dy;
				if (!map_blocked(m, nx, ny))
				{
					actor_place(p, nx, ny);
					return true;
				}
			}
		}
	}
	return false;
}

void actor_update(Player *p, const GameMap *m, float dt, u32 held, const circlePosition *circle)
{
	p->justArrived = false;

	int dx = 0, dy = 0;
	if (held & KEY_LEFT) dx = -1;
	else if (held & KEY_RIGHT) dx = 1;
	else if (held & KEY_UP) dy = -1;
	else if (held & KEY_DOWN) dy = 1;

	if (dx == 0 && dy == 0 && circle)
	{
		if (circle->dx < -30) dx = -1;
		else if (circle->dx > 30) dx = 1;
		else if (circle->dy < -30) dy = -1;
		else if (circle->dy > 30) dy = 1;
	}

	if (p->moving)
	{
		/* Steering mid-step: the tile being walked to is already committed,
		 * but the sprite turns towards the new direction at once so a
		 * direction change never waits for the step to finish. */
		if (dx != 0 || dy != 0)
			p->dir = dir_from_delta(dx, dy);

		p->t += dt * PLAYER_SPEED / (float)TILE;
		if (p->t < 1.0f)
		{
			p->px = p->fromPx + (int)((p->toTx * TILE - p->fromPx) * p->t);
			p->py = p->fromPy + (int)((p->toTy * TILE - p->fromPy) * p->t);
			return;
		}

		p->t = 1.0f;
		p->moving = false;
		p->tx = p->toTx;
		p->ty = p->toTy;
		p->px = p->tx * TILE;
		p->py = p->ty * TILE;
		p->justArrived = true;
		/* adjust_status (tools.s:470): pose advances once per step. */
		p->walkFrame = (u8)((p->walkFrame + 1) % 3);

		/* Chaining: a still-held direction starts the next step in THIS
		 * frame, so walking is a steady 160 ms/tile instead of pausing one
		 * frame after every tile. */
		if (dx == 0 && dy == 0)
			return;
	}
	else if (dx == 0 && dy == 0)
		return;

	p->dir = dir_from_delta(dx, dy);

	int nx = p->tx + dx;
	int ny = p->ty + dy;
	if (map_blocked(m, nx, ny) || task_ghost_at(p, nx, ny))
		return;

	p->moving = true;
	p->t = 0.0f;
	p->fromPx = p->px;
	p->fromPy = p->py;
	p->toTx = nx;
	p->toTy = ny;
}

const char *actor_dir_name(int dir)
{
	switch (dir)
	{
	case DIR_LEFT: return "LEFT";
	case DIR_UP: return "UP";
	case DIR_RIGHT: return "RIGHT";
	default: return "DOWN";
	}
}

void actor_draw(const Player *p, const GameMap *m, int camX, int camY)
{
	int mapW = m->w * TILE;
	int mapH = m->h * TILE;
	int offX = mapW < MAP_VIEW_W ? (MAP_VIEW_W - mapW) / 2 : 0;
	int offY = mapH < MAP_VIEW_H ? (MAP_VIEW_H - mapH) / 2 : 0;

	float sx = (float)(MAP_VIEW_X + offX - camX + p->px);
	float sy = (float)(MAP_VIEW_Y + offY - camY + p->py);
	float cx = (float)MAP_VIEW_X;
	float cy = (float)MAP_VIEW_Y;
	float cw = (float)MAP_VIEW_W;
	float ch = (float)MAP_VIEW_H;

	/* Walk pose: mid-step the interpolation sweeps the 3 frames, standing
	 * keeps the pose from the last completed step (original G_role_status). */
	int f = p->walkFrame;
	if (p->moving)
		f = (f + (int)(p->t * 3.0f)) % 3;

	/* 32x48 sprite, feet on the tile bottom (tools.s collision anchors the
	 * feet at G_role_Y + Role_Height - Unit_Height): draw from tileY - 16. */
	gfx_player_clipped(p->dir * 3 + f, sx, sy - 16.0f, 1.0f, cx, cy, cw, ch);
}

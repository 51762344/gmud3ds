/* M6: task.s 第一类任务 (寻人/寻物/杀NPC/动态鬼) and the ghost entity.
 * Rule sources: ref/gmud/src/task.s, qlist.s, npc.s, npc_quest.txt. */
#include "task.h"

#include "audio.h"
#include "item.h"
#include "log.h"
#include "map.h"
#include "npcdata.h"
#include "reward.h"
#include "rng.h"
#include "skill.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define TASK_MAXREWARD      200
#define TASK_WAIT_LIMIT     1200u   /* 20 min to finish a ghost hunt */
#define TASK_DONE_LIMIT     300u    /* 5 min before the next ghost */
#define TASK_REWARD_LIMIT   1200u   /* 20 min to collect the reward */
#define TASK_GHOST_LEVELS   10

/* task.s voluntary_work: the old lady stops giving chores at 5000 exp. The
 * gate is scaled with the rewards (intentional deviation) so she keeps
 * handing out the same number of chores instead of retiring after one quest. */
#define TASK_OLDLADY_EXP_GATE (5000u * REWARD_SCALE)

/* task.s ghost_index_tbl: 80,85,...,125 (percent of the player's stats). */
static const u8 s_ghostFactor[TASK_GHOST_LEVELS] =
{
	80, 85, 90, 95, 100, 105, 110, 115, 120, 125
};

static const char *const s_nameXing[8] =
{
	"赵", "钱", "孙", "李", "周", "吴", "郑", "王"
};
static const char *const s_nameMing[8] =
{
	"英", "雄", "豪", "杰", "梅", "兰", "竹", "菊"
};

/* task.s menpai_tbl: unarmed, weapon, dodge, force, parry + weapon item. */
static const u8 s_schoolKf[6][5] =
{
	{ SK_BAGUAZ, SK_BAGUAD, SK_YOULONG, SK_HUNYUAN, SK_BAGUAD },
	{ SK_MEIHUA, SK_HUATUAN, SK_HUAFEI, SK_SANHUA, SK_HUATUAN },
	{ SK_TAIZU, SK_PIFENG, SK_HEXIAN, SK_TONGJI, SK_PIFENG },
	{ SK_WUFA, SK_YIDAO, SK_WUYING, SK_RENSHU, SK_YIDAO },
	{ SK_TAIJIQUAN, SK_TAIJI_SWORD, SK_WANLIU, SK_TAIJIG, SK_TAIJI_SWORD },
	{ SK_XUEYING, SK_XUESHAN_SWORD, SK_TAXUE, SK_XUESHAN_FORCE, SK_XUESHAN_SWORD }
};
static const u8 s_schoolWeapon[6] = { 11, 22, 21, 11, 35, 35 }; /* 刀/鞭/杖/刀/剑/剑 */

typedef struct
{
	u32 value;
	u16 id;
} TaskCand;

static int task_rnd(int n)
{
	if (n <= 0)
		return 0;
	return (int)rng_random_it((u16)(n > 0xFFFF ? 0xFFFF : n));
}

static int task_clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Runtime dialogs follow quest.c: the strings stay valid until the next
 * task_talk_script() call, which is enough for the open dialog. */
static DialogNode s_nodes[2];
static DialogScript s_script;
static char s_text[256];

static const DialogScript *task_script(int count)
{
	s_script.nodes = s_nodes;
	s_script.count = count;
	return &s_script;
}

static const char *task_npc_name(int npcId, const char *fallback)
{
	const NpcInfo *n = npc_info(npcId);
	return n ? n->name : fallback;
}

static const DialogScript *task_single(const NpcInfo *n, const char *text)
{
	memset(s_nodes, 0, sizeof(s_nodes));
	s_nodes[0].speaker = n ? n->name : NULL;
	s_nodes[0].text = text;
	s_nodes[0].after = -1;
	return task_script(1);
}

static void task_ghost_name(const Player *p, char *out, size_t size)
{
	unsigned x = p->ghostXing < 8 ? p->ghostXing : 0;
	unsigned m = p->ghostMing < 8 ? p->ghostMing : 0;
	snprintf(out, size, "%s%s", s_nameXing[x], s_nameMing[m]);
}

const char *task_ghost_name_text(const Player *p, char *buf, int size)
{
	task_ghost_name(p, buf, (size_t)size);
	return buf;
}

static ActorTask *task_slot(Player *p, int type)
{
	if (!p || type <= 0 || type > TASK_TYPE_GHOST)
		return NULL;
	return &p->tasks[type - 1];
}

static bool task_has_pending_reward(const Player *p)
{
	for (int i = 0; i < 3; i++)
	{
		if (p->tasks[i].type && p->tasks[i].state == TASK_STATE_REWARD)
			return true;
	}
	return false;
}

/* ------------------------------------------------------- candidate tables */

static void task_sort(TaskCand *c, int count)
{
	for (int i = 1; i < count; i++)
	{
		TaskCand v = c[i];
		int j = i;
		while (j > 0 && c[j - 1].value > v.value)
		{
			c[j] = c[j - 1];
			j--;
		}
		c[j] = v;
	}
}

static int task_npc_candidates(const Player *p, TaskCand *out, int max)
{
	int count = 0;
	int limit = npc_count();
	if (limit > 124)
		limit = 124;
	for (int id = 1; id <= limit && count < max; id++)
	{
		if (id == TASK_NPC_BUKUAI || id == TASK_NPC_CUNZHANG ||
		    id == TASK_NPC_FUREN || id == 25 ||
		    id == TASK_NPC_PINGYIZHI || id == TASK_NPC_TEACHER ||
		    id == TASK_GHOST_NPC)
			continue;
		const NpcInfo *n = npc_info(id);
		if (!n || n->hpMax == 0)
			continue;
		if (p->deadNpc[id / 8] & (1u << (id % 8)))
			continue;
		out[count].value = n->exp;
		out[count].id = (u16)id;
		count++;
	}
	task_sort(out, count);
	return count;
}

static int task_goods_candidates(TaskCand *out, int max)
{
	int count = 0;
	for (int id = 1; id < item_count() && count < max; id++)
	{
		const ItemDef *d = item_get(id);
		if (!d)
			continue;
		/* set_all_goods removes id 0 and the 三角石板 (SANJIAO=77). */
		if (id == ITEM_SANJIAO)
			continue;
		out[count].value = d->price;
		out[count].id = (u16)id;
		count++;
	}
	task_sort(out, count);
	return count;
}

/* task.s quest_accurate_index returns how many of the candidates the player
 * qualifies for, and that number is used as random_it's range (0..range-1).
 *
 * The original loop increments quest_index FIRST and compares it with
 * quest_numbers; when they are equal it jumps to quest_index_rts, which
 * DECs quest_index before returning. Two consequences:
 *   - only entries 0..count-2 are ever compared with man_exp;
 *   - the highest tier (index count-1) can therefore never be selected.
 * For count==1 the loop exits immediately and returns 0 (range 0 -> tier 0).
 */
static int task_eligible(const TaskCand *c, int count, u32 value)
{
	if (count <= 0)
		return 0;
	int eligible = 0;
	for (int i = 0; i < count - 1; i++)
	{
		if (value >= c[i].value)
			eligible++;
		else
			break;
	}
	return eligible;
}

/* task.s cal_reward: MAXREWARD*(index+1)/count, scaled by log10(exp/10000)
 * and split by exp/(exp+dx) * dx/(exp+dx), plus kar/int randoms. */
static u16 task_reward_value(const Player *p, u32 targetExp, int index, int count)
{
	unsigned long long reward = (unsigned long long)TASK_MAXREWARD * (u32)(index + 1) / (u32)count;
	int scale = 1;
	for (u32 v = p->exp / 10000; v >= 10; v /= 10)
		scale++;
	reward *= (unsigned)scale;
	unsigned long long sum = (unsigned long long)p->exp + targetExp;
	if (sum)
		reward = reward * p->exp / sum * targetExp / sum;
	/* task.s:566-573: random_it(man_kar) FIRST, then random_it(man_int).
	 * Split the calls so the PRNG order is fixed across compilers. */
	u32 randKar = (u32)task_rnd(p->kar);
	u32 randInt = (u32)task_rnd(p->intel);
	reward += randKar + randInt;
	/* task.s:574-578: when the reward reaches MAXREWARD it is clamped and the
	 * ALREADY drawn kar random is added again - no extra random_it here. */
	if (reward >= TASK_MAXREWARD)
		reward = TASK_MAXREWARD + randKar;
	if (reward > 65535)
		reward = 65535;
	return (u16)reward;
}

static u16 task_ghost_reward(int level)
{
	int r = 80 + task_rnd(80);          /* GHOST_BONUS + random(GHOST_BONUS) */
	return (u16)task_clamp(r * level, 1, 65535);
}

/* ------------------------------------------------------------- ghost entity */

bool task_ghost_prepare(Player *p)
{
	if (!p)
		return false;
	ActorTask *q = task_slot(p, TASK_TYPE_GHOST);
	if (!p->ghostActive || !q || q->type != TASK_TYPE_GHOST)
		return false;

	/* task.s copy_status rolls random_it(6)+1 for ghost_pai every time
	 * init_npc refreshes the ghost (the npc_flag cache is disabled in the
	 * original assembly), so the school can change between visits. */
	p->ghostPai = (u8)(1 + task_rnd(6));

	NpcInfo g;
	memset(&g, 0, sizeof(g));
	g.id = TASK_GHOST_NPC;
	g.pai = task_clamp(p->ghostPai, 1, 6);
	g.gender = p->ghostGender ? 1 : 0;
	g.age = 34;                           /* GHOST_AGE */
	char name[16];
	task_ghost_name(p, name, sizeof(name));
	snprintf(g.name, sizeof(g.name), "%s", name);

	int level = task_clamp(q->index, 1, TASK_GHOST_LEVELS);
	int factor = s_ghostFactor[level - 1];

	int topKf = 0;
	for (int i = 0; i < SKILL_SLOTS; i++)
	{
		if (p->skills[i].level > topKf)
			topKf = p->skills[i].level;
	}
	int kfLevel = topKf * factor / 100;

	g.exp = (u32)((unsigned long long)p->exp * (u32)factor / 100u);
	g.hpMax = (u16)task_clamp((int)p->hpMax * factor / 100, 1, 30000);
	g.hp = g.hpMax;
	/* mpMax must share hpMax's 30000 cap: Battle stores it as s16, and a
	 * 125% scaled value over 32767 would wrap negative in the fight. */
	g.mpMax = (u16)task_clamp((int)p->mpMax * factor / 100, 0, 30000);
	g.mp = g.mpMax;
	g.attack = p->attack;
	g.defense = p->defense;
	g.damage = p->damage;
	g.armor = p->armor;
	g.str = p->attrStr;
	g.dex = p->attrDex;
	g.intel = p->attrIntel;
	g.con = p->attrCon;
	g.per = p->attrPer;
	g.kar = p->attrKar;
	g.force = (u16)(g.mpMax / 40);
	g.money = 10;
	g.morality = 0;

	int school = g.pai - 1;
	g.kfCount = 13;
	for (int i = 0; i < 9; i++)
	{
		g.kf[i * 2] = (u8)i;
		g.kf[i * 2 + 1] = (u8)task_clamp(kfLevel, 0, 255);
	}
	for (int i = 0; i < 4; i++)
	{
		g.kf[(9 + i) * 2] = s_schoolKf[school][i];
		g.kf[(9 + i) * 2 + 1] = (u8)task_clamp(kfLevel, 0, 255);
	}
	for (int i = 0; i < 5; i++)
		g.usekf[i] = (u8)(s_schoolKf[school][i] | 0x80);
	g.weapon = s_schoolWeapon[school];
	g.equippedFlags = 1;
	const ItemDef *w = item_get(g.weapon);
	if (w)
		g.damage = (u16)task_clamp((int)p->damage + w->effect, 0, 255);

	npc_ghost_set(&g);
	return true;
}

/* tools.s random_map: one of maps 1..G_Total_Map-1, up to 8 positions per map,
 * center and four neighbours must be walkable. The ghost is a map overlay
 * like the original (drawn at G_Killer_X/Y), not an entry in the NPC table. */
static bool task_ghost_place(Player *p)
{
	int maps = map_count();
	if (maps <= 1)
		return false;

	for (int mapTry = 0; mapTry < 64; mapTry++)
	{
		int mi = 1 + task_rnd(maps - 1);
		const GameMap *m = map_get(mi);
		if (!m || m->w < 3 || m->h < 3)
			continue;

		for (int posTry = 0; posTry < 8; posTry++)
		{
			int x = 1 + task_rnd(m->w - 2);
			int y = 1 + task_rnd(m->h - 2);
			if (map_blocked(m, x, y) ||
			    (map_cell(m, x, y) & 0x1FFFu) != 0)
				continue;
			if (map_blocked(m, x - 1, y) || map_blocked(m, x + 1, y) ||
			    map_blocked(m, x, y - 1) || map_blocked(m, x, y + 1))
				continue;
			if (p->mapIndex == mi && p->tx == x && p->ty == y)
				continue;

			p->ghostMap = (u8)mi;
			p->ghostX = (u8)x;
			p->ghostY = (u8)y;
			p->ghostActive = 1;
			return true;
		}
	}
	return false;
}

void task_ghost_remove(Player *p)
{
	npc_ghost_clear();
	if (p)
		p->ghostActive = 0;
}

int task_ghost_map(const Player *p)
{
	return (p && p->ghostActive) ? (int)p->ghostMap : -1;
}

bool task_ghost_here(const Player *p)
{
	return p && p->ghostActive && p->ghostMap == p->mapIndex;
}

bool task_ghost_at(const Player *p, int x, int y)
{
	return p && p->ghostActive && p->ghostMap == p->mapIndex &&
	       p->ghostX == x && p->ghostY == y;
}

u16 task_ghost_tile(const Player *p)
{
	/* tools.s draws 474 for a male killer and 466 for a female one. */
	return (p && p->ghostGender) ? 466 : 474;
}

void task_ghost_sync(Player *p)
{
	if (!p)
		return;
	npc_ghost_clear();
	ActorTask *q = task_slot(p, TASK_TYPE_GHOST);
	if (!q || q->type != TASK_TYPE_GHOST ||
	    (q->state != TASK_STATE_ACTIVE && q->state != TASK_STATE_DONE))
		p->ghostActive = 0;
	if (!p->ghostActive)
		return;
	const GameMap *m = map_get(p->ghostMap);
	if (!m || p->ghostX == 0 || p->ghostY == 0 ||
	    p->ghostX >= m->w || p->ghostY >= m->h)
		p->ghostActive = 0;
}

/* --------------------------------------------------------------- new tasks */

static bool task_start_common(Player *p, int type, int npcId, u32 now)
{
	ActorTask *q = task_slot(p, type);
	if (!q)
		return false;

	TaskCand cands[140];
	int count = 0;
	if (type == TASK_TYPE_GOODS)
		count = task_goods_candidates(cands, 140);
	else
		count = task_npc_candidates(p, cands, 140);
	if (count <= 0)
	{
		q->type = 0;
		q->state = TASK_STATE_NONE;
		const NpcInfo *n = npc_info(npcId);
		task_single(n, "什么？你把人杀光了？I 服了 YOU！");
		return false;
	}

	/* task.s quest_accurate_index returns the number of eligible tiers,
	 * then random_it(range) picks 0..range-1 (range 0 -> tier 0). */
	int eligible = task_eligible(cands, count, p->exp);
	int pick = eligible > 0 ? task_rnd(eligible) : 0;
	int target = cands[pick].id;

	q->type = (u8)type;
	q->state = TASK_STATE_ACTIVE;
	q->id = (u8)target;
	q->index = (u8)pick;
	q->reward = task_reward_value(p, cands[pick].value, pick, count);
	q->time = now;
	return true;
}

static const DialogScript *task_new_task_script(Player *p, int type, int npcId, u32 now)
{
	const NpcInfo *n = npc_info(npcId);
	if (!task_start_common(p, type, npcId, now))
		return &s_script; /* task_start_common filled s_nodes as fallback */

	ActorTask *q = task_slot(p, type);
	/* Name/text must be copied: task_single() reuses s_nodes. */
	if (type == TASK_TYPE_NPC)
		snprintf(s_text, sizeof(s_text), "请速去拜见%s！",
		         task_npc_name(q->id, "此人"));
	else if (type == TASK_TYPE_GOODS)
		snprintf(s_text, sizeof(s_text), "今天妾身正准备请人去找%s，能否帮个忙？",
		         item_name(q->id));
	else
		snprintf(s_text, sizeof(s_text), "老夫夜观天象，%s阳寿已尽，你去解决他！",
		         task_npc_name(q->id, "此人"));
	return task_single(n, s_text);
}

/* ------------------------------------------------------------ ghost quest */

static void task_ghost_roll(Player *p)
{
	p->ghostXing = (u8)task_rnd(8);
	/* create_ghost uses the same random value for gender and the given name:
	 * 0..3 -> gender 0, 4..7 -> gender 1. */
	unsigned ming = (unsigned)task_rnd(8);
	p->ghostMing = (u8)ming;
	p->ghostGender = ming < 4 ? 0 : 1;
	/* ghost_pai is NOT rolled here: copy_status consumes random_it(6) on
	 * the first init_npc (task_ghost_prepare). */
	p->ghostPai = 0;
}

/* task.s create_ghost consumes: 姓(8), 名(8), 奖励(80). The 门派 draw
 * belongs to copy_status and happens later, when the ghost is initialised. */
static void task_ghost_begin(Player *p, ActorTask *q, int level, u32 now)
{
	q->type = (u8)TASK_TYPE_GHOST;
	q->state = TASK_STATE_ACTIVE;
	q->id = TASK_GHOST_NPC;
	q->index = (u8)level;
	q->time = now;
	task_ghost_roll(p);
	q->reward = task_ghost_reward(level);
}

static const DialogScript *task_ghost_script(Player *p, u32 now)
{
	const NpcInfo *n = npc_info(TASK_NPC_BUKUAI);
	ActorTask *q = task_slot(p, TASK_TYPE_GHOST);

	/* query_ghost: bpl bad_man -> a wanted (evil) player is refused. */
	if (actor_morality_raw(p) < 128)
		return task_single(n, "你不是通辑犯吗？来人，给我拿下！");

	if (!q)
		return NULL;

	if (q->type != TASK_TYPE_GHOST)
	{
		task_ghost_begin(p, q, 1, now);
		if (!task_ghost_place(p))
		{
			q->type = 0;
			q->state = TASK_STATE_NONE;
			return task_single(n, "近来天下太平，没有什么恶人可抓。");
		}
	}
	else if (q->state == TASK_STATE_ACTIVE)
	{
		if (now >= q->time + TASK_WAIT_LIMIT)
		{
			/* 20 min passed: destroy the old ghost and lose one level
			 * (task.s: level<2 ? 1 : level-1). */
			task_ghost_remove(p);
			int level = q->index < 2 ? 1 : (int)q->index - 1;
			task_ghost_begin(p, q, level, now);
			if (!task_ghost_place(p))
			{
				/* Same rollback as the first-time branch: a slot left
				 * ACTIVE with no ghost on the map could neither be fought
				 * nor reported until the 20-minute timeout hit again. */
				q->type = 0;
				q->state = TASK_STATE_NONE;
				return task_single(n, "近来天下太平，没有什么恶人可抓。");
			}
		}
		else
		{
			char name[16];
			task_ghost_name(p, name, sizeof(name));
			snprintf(s_text, sizeof(s_text), "在下不是请你去收服%s吗？", name);
			return task_single(n, s_text);
		}
	}
	else if (q->state == TASK_STATE_DONE)
	{
		if (now <= q->time + TASK_DONE_LIMIT)
			return task_single(n, "多谢，奸邪暂时已经除尽了，先歇息一下吧！");

		int level = q->index < TASK_GHOST_LEVELS ? (int)q->index + 1 : 2;
		task_ghost_begin(p, q, level, now);
		if (!task_ghost_place(p))
		{
			q->type = 0;
			q->state = TASK_STATE_NONE;
			return task_single(n, "近来天下太平，没有什么恶人可抓。");
		}
	}
	else
	{
		return task_single(n, "先歇息一下吧。");
	}

	char name[16];
	task_ghost_name(p, name, sizeof(name));
	const GameMap *m = map_get(p->ghostMap);
	snprintf(s_text, sizeof(s_text), "近有恶人『%s』在%s为非作歹，请速去为民除害！",
	         name, m ? m->name : "江湖");
	return task_single(n, s_text);
}

/* task.s voluntary_work / pyh_quest.s: 老婆婆's chores. */
static DialogNode s_choreNodes[6];
static DialogScript s_choreScript;
static char s_choreText[6][96];

static const DialogScript *task_voluntary_script(Player *p)
{
	const NpcInfo *n = npc_info(TASK_NPC_OLDLADY);
	/* man_exp >= 5000: the old lady says she cannot order you around. */
	if (p->exp >= TASK_OLDLADY_EXP_GATE)
		return task_single(n, "唉! 你在江湖上也小有名气了,老身使唤不动你了!");
	if (p->homeBuf)
		return task_single(n, "老身吩咐你的事做完了么?");

	static const char *const CHORES[3] = { "扫地", "挑水", "劈柴" };
	int pick = task_rnd(3);
	p->homeBuf = (u8)(pick + 1);
	snprintf(s_text, sizeof(s_text), "老身年事已高,有好心人帮帮我『%s』吗?",
	         CHORES[pick]);
	return task_single(n, s_text);
}

const DialogScript *task_chore_script(Player *p, int kind)
{
	if (!p || kind < 1 || kind > 3 || p->homeBuf != kind)
		return NULL;
	/* do_saodi/do_tiaoshui/do_pichai clear the flag before the HP check. */
	p->homeBuf = 0;

	const NpcInfo *n = npc_info(TASK_NPC_OLDLADY);
	int cost = kind == 1 ? 30 : kind == 2 ? 40 : 50;
	memset(s_choreNodes, 0, sizeof(s_choreNodes));
	memset(s_choreText, 0, sizeof(s_choreText));

	if (p->hp < cost)
	{
		snprintf(s_choreText[0], sizeof(s_choreText[0]),
		         "婆婆呀,手都累软了,我们老板都没你狠");
		s_choreNodes[0].speaker = n ? n->name : NULL;
		s_choreNodes[0].text = s_choreText[0];
		s_choreNodes[0].after = -1;
		s_choreScript.nodes = s_choreNodes;
		s_choreScript.count = 1;
		return &s_choreScript;
	}
	p->hp -= (s16)cost;

	static const char *const POEMS[3][4] =
	{
		{ "扫地扫地我扫地", "地上还有西瓜皮", "婆婆家中欠打扫", "尘土满天难呼吸" },
		{ "挑水挑水我挑水", "倒水进缸水花飞", "一桶两桶三四桶", "反正不用交水费" },
		{ "劈柴劈柴我劈柴", "抡起斧子劈起来", "保护树木讲环保", "婆婆没事别乱烧" }
	};
	for (int i = 0; i < 4; i++)
	{
		snprintf(s_choreText[i], sizeof(s_choreText[i]), "%s", POEMS[kind - 1][i]);
		s_choreNodes[i].speaker = NULL;
		s_choreNodes[i].text = s_choreText[i];
		s_choreNodes[i].after = i + 1;
	}
	snprintf(s_choreText[4], sizeof(s_choreText[4]), "费了老大力气,总算干完了");
	s_choreNodes[4].speaker = NULL;
	s_choreNodes[4].text = s_choreText[4];
	s_choreNodes[4].after = 5;

	/* pyh_quest.s show_bonus: 20 exp / 10 pot / 50 money, scaled x15
	 * (intentional deviation, see include/reward.h). */
	u32 choreExp = reward_scale(20);
	u32 chorePot = reward_scale(10);
	u32 choreMoney = reward_scale(50);
	actor_gain_exp(p, choreExp);
	u32 pot = (u32)p->potential + chorePot;
	p->potential = (u16)(pot > 65535 ? 65535 : pot);
	p->money += choreMoney;
	snprintf(s_choreText[5], sizeof(s_choreText[5]),
	         "你被奖励了：%u 点实战经验 %u 点潜能 %u 金钱",
	         (unsigned)choreExp, (unsigned)chorePot, (unsigned)choreMoney);
	s_choreNodes[5].speaker = NULL;
	s_choreNodes[5].text = s_choreText[5];
	s_choreNodes[5].after = -1;

	s_choreScript.nodes = s_choreNodes;
	s_choreScript.count = 6;
	return &s_choreScript;
}

/* -------------------------------------------------------------- talk entry */

static const DialogScript *task_goods_offer(const NpcInfo *n, int item)
{
	memset(s_nodes, 0, sizeof(s_nodes));
	s_nodes[0].speaker = n ? n->name : NULL;
	snprintf(s_text, sizeof(s_text), "使用「%s」完成任务吗？", item_name(item));
	s_nodes[0].text = s_text;
	s_nodes[0].optionCount = 2;
	s_nodes[0].options[0] = "送给她";
	s_nodes[0].options[1] = "再想想";
	s_nodes[0].optAction[0] = DLG_ACT_QUEST_GIVE;
	s_nodes[0].optAction[1] = DLG_ACT_QUEST_DECLINE;
	s_nodes[0].next[0] = -1;
	s_nodes[0].next[1] = -1;
	s_nodes[0].after = -1;
	return task_script(1);
}

const DialogScript *task_decline_script(Player *p, int npcId)
{
	if (!p || npcId != TASK_NPC_FUREN)
		return NULL;
	ActorTask *q = task_slot(p, TASK_TYPE_GOODS);
	if (!q || q->type != TASK_TYPE_GOODS || q->state != TASK_STATE_ACTIVE)
		return NULL;
	const NpcInfo *n = npc_info(npcId);
	/* query_if_give "no" falls through to check_player/exist message. */
	snprintf(s_text, sizeof(s_text), "妾身还盼着您的%s呢！", item_name(q->id));
	return task_single(n, s_text);
}

const DialogScript *task_talk_script(Player *p, int npcId, u32 now)
{
	if (!p)
		return NULL;
	const NpcInfo *n = npc_info(npcId);
	if (!n)
		return NULL;

	/* npc.s query_find: greeting the target finishes the 寻人 task. */
	ActorTask *npcq = task_slot(p, TASK_TYPE_NPC);
	if (npcq && npcq->type == TASK_TYPE_NPC &&
	    npcq->state == TASK_STATE_ACTIVE && npcq->id == npcId)
	{
		npcq->state = TASK_STATE_DONE;
		npcq->time = now;
		return task_single(n, "我知道了，多谢来访！");
	}

	if (!task_is_giver(npcId))
		return NULL;

	if (npcId == TASK_NPC_TEACHER)
		return task_reward_script(p, now);

	if (npcId == TASK_NPC_BUKUAI)
		return task_ghost_script(p, now);

	if (npcId == TASK_NPC_OLDLADY)
		return task_voluntary_script(p);

	int type = npcId == TASK_NPC_CUNZHANG ? TASK_TYPE_NPC
	         : npcId == TASK_NPC_FUREN    ? TASK_TYPE_GOODS
	                                      : TASK_TYPE_KILL;

	/* 平一指 refuses good players (man_daode bit7 set) before judge_reward. */
	if (type == TASK_TYPE_KILL && actor_morality_raw(p) >= 128)
		return task_single(n, "最讨厌伪君子了，马上从我面前消失！");

	if (task_has_pending_reward(p))
		return task_single(n, "看你红光满面，还是先去顾炎武处领赏吧！");

	ActorTask *q = task_slot(p, type);
	if (!q)
		return NULL;

	if (q->type == type && q->state == TASK_STATE_DONE)
	{
		q->state = TASK_STATE_REWARD;
		q->time = now;
		return task_single(n, "你完成了任务，去顾炎武处领赏吧！");
	}

	if (q->type == type && q->state == TASK_STATE_REWARD)
		return task_single(n, "还是先去顾炎武处领赏吧！");

	if (q->type == type && q->state == TASK_STATE_ACTIVE)
	{
		if (type == TASK_TYPE_GOODS && inv_count(p->bag, q->id) > 0)
			return task_goods_offer(n, q->id);
		if (type == TASK_TYPE_NPC)
			snprintf(s_text, sizeof(s_text), "老夫不是说过请去拜见%s吗！",
			         task_npc_name(q->id, "此人"));
		else if (type == TASK_TYPE_GOODS)
			snprintf(s_text, sizeof(s_text), "妾身还盼着您的%s呢！", item_name(q->id));
		else
			snprintf(s_text, sizeof(s_text), "老夫不是让你解决%s吗？",
			         task_npc_name(q->id, "此人"));
		return task_single(n, s_text);
	}

	return task_new_task_script(p, type, npcId, now);
}

bool task_give_item(Player *p)
{
	ActorTask *q = task_slot(p, TASK_TYPE_GOODS);
	if (!p || !q || q->type != TASK_TYPE_GOODS || q->state != TASK_STATE_ACTIVE)
		return false;

	int slot = -1;
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		if (BAG_ID(p->bag[i]) == q->id && !BAG_EQUIPPED(p->bag[i]))
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return false;
	if (!inv_remove_slot(p->bag, slot, 1))
		return false;

	const NpcInfo *n = npc_info(TASK_NPC_FUREN);
	log_add("你把「%s」交给了%s。", item_name(q->id), n ? n->name : "妇人");
	q->state = TASK_STATE_DONE;
	return true;
}

const DialogScript *task_reward_script(Player *p, u32 now)
{
	const NpcInfo *n = npc_info(TASK_NPC_TEACHER);
	if (!p)
		return NULL;

	bool any = false;
	u32 total = 0;
	for (int i = 0; i < 3; i++)
	{
		ActorTask *q = &p->tasks[i];
		if (!q->type || q->state != TASK_STATE_REWARD)
			continue;
		any = true;
		if (now <= q->time + TASK_REWARD_LIMIT)
			total += q->reward;
		memset(q, 0, sizeof(*q));
	}
	if (!any)
		return task_single(n, "努力吧，干活去吧！");
	if (total == 0)
		return task_single(n, "你完成了任务，可惜已经过了领赏的时限。");
	if (total > 65535)
		total = 65535;

	/* task.s pyh_task_bonus: quest_reward * 3, then 70% exp / 30% pot.
	 * The payout is scaled x15 (intentional deviation). */
	unsigned long long reward = (unsigned long long)total * 3u * REWARD_SCALE;
	char toast[40];
	if (task_rnd(100) < 70)
	{
		actor_gain_exp(p, (u32)reward);
		snprintf(s_text, sizeof(s_text), "你被奖励了：%u 点实战经验。", (unsigned)reward);
		snprintf(toast, sizeof(toast), "经验 +%u", (unsigned)reward);
	}
	else
	{
		reward /= 2;
		u32 pot = (u32)p->potential + (u32)reward;
		p->potential = (u16)(pot > 65535 ? 65535 : pot);
		snprintf(s_text, sizeof(s_text), "你被奖励了：%u 点潜能。", (unsigned)reward);
		snprintf(toast, sizeof(toast), "潜能 +%u", (unsigned)reward);
	}
	ui_toast(toast);
	return task_single(n, s_text);
}

void task_on_battle(Player *p, int npcId, bool killed, u32 now)
{
	if (!p)
		return;

	if (npcId == TASK_GHOST_NPC)
	{
		ActorTask *q = task_slot(p, TASK_TYPE_GHOST);
		if (!killed || !q || q->type != TASK_TYPE_GHOST ||
		    q->state != TASK_STATE_ACTIVE)
			return;

		/* npc.s man_win: exp = quest_reward, pot = quest_reward / 4,
		 * scaled x15 (intentional deviation). */
		u32 reward = reward_scale(q->reward);
		actor_gain_exp(p, reward);
		u32 pot = reward / 4;
		u32 newPot = (u32)p->potential + pot;
		p->potential = (u16)(newPot > 65535 ? 65535 : newPot);
		char name[16];
		task_ghost_name(p, name, sizeof(name));
		log_add("除掉了%s，得到实战经验 %u、潜能 %u。", name, reward, pot);
		{
			char toast[40];
			snprintf(toast, sizeof(toast), "经验 +%u", (unsigned)reward);
			ui_toast(toast);
		}
		audio_play(SFX_QUEST);
		/* task.s keeps the accept time in quest_delay; the 300 s rest period
		 * is measured from acceptance, not from the kill. */
		q->state = TASK_STATE_DONE;
		task_ghost_remove(p);
		return;
	}

	if (!killed)
		return;
	ActorTask *q = task_slot(p, TASK_TYPE_KILL);
	if (q && q->type == TASK_TYPE_KILL && q->state == TASK_STATE_ACTIVE && q->id == npcId)
	{
		q->state = TASK_STATE_DONE;
		q->time = now;
		if (p->siKill < 65535)   /* npc.s man_win: inc si_kill */
			p->siKill++;
	}
}

void task_migrate(Player *p, u32 now)
{
	if (!p)
		return;
	ActorTask *q = task_slot(p, TASK_TYPE_KILL);
	if (q && q->type == 0 && p->killTarget)
	{
		q->type = TASK_TYPE_KILL;
		q->id = p->killTarget;
		q->reward = p->killReward;
		q->index = 0;
		q->time = p->killReportTime ? p->killReportTime : now;
		if (p->killComplete == 2)
			q->state = TASK_STATE_REWARD;
		else if (p->killComplete == 1)
			q->state = TASK_STATE_DONE;
		else
		{
			q->state = TASK_STATE_ACTIVE;
			q->time = now;
		}
	}
	p->killTarget = p->killComplete = 0;
	p->killReward = 0;
	p->killReportTime = 0;
}

bool task_is_giver(int npcId)
{
	return npcId == TASK_NPC_CUNZHANG || npcId == TASK_NPC_FUREN ||
	       npcId == TASK_NPC_PINGYIZHI || npcId == TASK_NPC_BUKUAI ||
	       npcId == TASK_NPC_TEACHER || npcId == TASK_NPC_OLDLADY;
}

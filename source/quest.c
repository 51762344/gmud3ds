/* M5 npc_quest trades and chat lines, transcribed from
 * ref/gmud/src/npc_quest.txt (simplified-Chinese branch). */
#include "quest.h"

#include "item.h"
#include "log.h"
#include "npcdata.h"
#include "rng.h"

#include <stdio.h>
#include <string.h>

/* goods ids per h/id.h: 白玉豆腐7 花裙54 糖葫芦6 老花镜40 猪肉5 菜刀12
 * 鱼74 清风剑33 精制布衣43 仙丹10 金环锁子甲62 黄纸69 拳法书(HAND_BOOK)68
 * 红拂尘28 白玉箫29 包子2 刀法总纲70 毛笔72 内功心法71 */
static const QuestDef s_quests[] =
{
	{ 13, 7, 18, 54 },   /* 公子哥: 白玉豆腐 x18 -> 花裙 */
	{ 2, 6, 2, 40 },     /* 小顽童: 糖葫芦 x2 -> 老花镜 */
	{ 5, 5, 1, 12 },     /* 厨师:   猪肉 x1 -> 菜刀 */
	{ 20, 74, 30, 33 },  /* 荷西:   鱼 x30 -> 清风剑 */
	{ 30, 40, 1, 43 },   /* 老裁缝: 老花镜 x1 -> 精制布衣 */
	{ 37, 10, 1, 62 },   /* 盐商:   仙丹 x1 -> 金环锁子甲 */
	{ 47, 69, 1, 68 },   /* 平阿四: 黄纸 x1 -> 拳法书 */
	{ 68, 29, 1, 28 },   /* 小红:   白玉箫 x1 -> 红拂尘 */
	{ 90, 2, 10, 70 },   /* 食野太郎: 包子 x10 -> 刀法总纲 */
	{ 64, 72, 1, 71 },   /* 侍书:   毛笔 x1 -> 内功心法 */
};

typedef struct
{
	u8 npcId;
	u8 count;
	const char *msgs[3];
} QuestChat;

/* Chat-only NPCs from npc_quest.txt. The table is positional: the i-th
 * symbol of npc_quest.txt's npc_id_tbl matches the i-th entry of
 * npc_quest_tbl, so every label resolves to an id via h/id.h and then to a
 * Chinese name via npc_data.gb (e.g. LIBAI_NPC = 24 = 李也白).
 * Ids follow id.h / npc_data order. */
static const QuestChat s_chats[] =
{
	{ 1, 2, { "这位客官也是来吃豆腐的么？",
	           "我的豆腐又白又嫩，你不想尝尝？" } },
	{ 9, 1, { "客官买花吗？" } },
	{ 11, 1, { "高价收购，价格公道，童叟无欺" } },
	{ 12, 2, { "善恶常在一念之间，施主要好生把握。",
	           "色即是空，空即是色。" } },
	{ 17, 2, { "听说有一个六芒星阵，可以任意转换时空。",
	           "打开六芒星阵，需要六块三角石板。" } },
	{ 19, 1, { "挑件趁手的兵器吧" } },
	{ 24, 1, { "人生得意须尽欢，莫使金樽空对月。" } },
	{ 26, 1, { "终日在江湖上混，身边总要备些伤药吧" } },
	{ 27, 1, { "又大又甜的糖葫芦呦" } },
	{ 28, 3, { "顾先生说过，寸金难买寸光阴呦。",
	           "顾先生真有学问，光毛笔就一大堆，我昨天还捡了一支。",
	           "人之初性本善...上大人孔已己...下面是..." } },
	{ 29, 1, { "这位客官，想做身衣服吗？" } },
	{ 34, 1, { "嘿嘿，没了我胡屠户，就只能吃混毛猪" } },
	{ 35, 2, { "这位客官进来喝杯茶歇歇腿吧！",
	           "客官想来点什么，别看我们店小，东西可不少！" } },
	{ 38, 1, { "居家过日子，买点杂货？" } },
	{ 53, 1, { "正宗的十八学士茶花，只有我们大理才有" } },
	{ 65, 2, { "我门尊主不但武功精深，文采更是不凡。",
	           "我们四姐妹从小就跟随尊主，一直行影不离。" } },
	{ 86, 1, { "唉！来了这么多天也没能见到花讽院主，\n你说是不是我带的礼物太少了" } },
	{ 112, 1, { "天气寒冷，喝碗稣油茶暖暖身子吧" } },
	{ 124, 1, { "&$$^&^^(^(" } },
	/* KILLER_NPC = 125 is the runtime 杀手; in this project id 125 is the
	 * dynamic ghost (task.s create_ghost), so this line is what the ghost
	 * says when the player talks to it. */
	{ 125, 1, { "小兔崽子儿，我看你是不想活了。" } },
};

typedef struct
{
	u8 npcId;
	u8 chatCount;
	const char *chats[3];
	const char *offer;
	const char *thanks;
} QuestMsg;

static const QuestMsg s_msgs[] =
{
	{ 13, 2, { "潘小莲的豆腐真好吃。",
	           "每次去豆腐店，总要买上一斤二两。" },
	  "太好了，我可以吃你的豆腐吗？",
	  "嘻嘻，穿上很漂亮吧。" },
	{ 2, 2, { "我最爱吃糖葫芦了，你喜欢吃吗？",
	          "嘻嘻，昨天捡到个亮闪闪的东西，\n戴上去晕头转向，真好玩。" },
	  "太棒了，你用糖葫芦跟我换了吧！",
	  "你给我的糖葫芦真好吃。" },
	{ 5, 1, { "真奇怪，我条案上的肉不见了，真是急人！" },
	  "我用这把祖传的宝刀换你身上的猪肉，你换吗？",
	  "真是谢谢你啊，我的祖传宝刀还好用吗？" },
	{ 20, 1, { "你也喜欢钓鱼吗，南郊有个鱼塘，\n里面有好多鱼呀！" },
	  "这位客官身上的鱼可以送给小人吗？",
	  "我前天钓鱼钓上来一把破剑，\n留著也是无用，就送给客官了！" },
	{ 30, 1, { "真是不巧，我的老花镜不见了，\n现在什么都做不了..唉！" },
	  "你身上的老花镜反正也用不到，送给他吗？",
	  "太感谢了，看你也是穷小子，\n这件精制布衣就送给你了！" },
	{ 37, 1, { "这些官没一个不贪的，\n几个小钱就能让他们比孙子还听话！" },
	  "老夫一生致力于炼丹，\n你身上的仙丹可否让我研究研究！",
	  "大恩不言谢，这件[金环锁子甲]乃赵云遗物，\n老夫就送给你了！" },
	{ 47, 2, { "都是那个贼人，把我害成这个样子，\n我...我一直记得他！",
	           "我家小爷的书缺了两页，所以总是说瞧不懂，\n那两页就在那贼人身上" },
	  "你身上这两页纸好眼熟，能给我看看吗？",
	  "你看，加上这两页，就是一本书了！" },
	{ 68, 2, { "以前公子常吟诵\"小红低唱我吹萧\"，\n如今萧在人无，唉！",
	           "这里大家都对我很好，可是我．．．" },
	  "你身上的白玉箫是我家公子以前身上之物，\n可否送给我以作纪念",
	  "门主送给我一把[红拂尘]，\n我无意学武，就送给你了！" },
	{ 90, 3, { "为了吃，为了吃，一天都在忙。",
	           "我一顿能吃10个包子，是不是该申请急你死记录？",
	           "师傅总是不给我吃饱，嘻嘻...我偷了他的..." },
	  "包子！我最爱吃的包子！不管那么多了，\n你给我包子，我就用这本书跟你换了！",
	  "你的包子真好吃，呵呵！" },
	{ 64, 2, { "我家主人真是学识渊博，那天她吟诵的\n清心普善咒真是博大精深",
	           "真可惜，当时没有纸笔，\n没办法将那篇清心普善咒记录下来" },
	  "你有笔，太好了，借我用一下吧！",
	  "我现在还记得清清楚楚，现在就默写下来给你" },
};

const QuestDef *quest_find(int npcId)
{
	for (int i = 0; i < (int)(sizeof(s_quests) / sizeof(s_quests[0])); i++)
	{
		if (s_quests[i].npcId == npcId)
			return &s_quests[i];
	}
	return NULL;
}

const QuestDef *quest_at(int index)
{
	if (index < 0 || index >= (int)(sizeof(s_quests) / sizeof(s_quests[0])))
		return NULL;
	return &s_quests[index];
}

int quest_count(void)
{
	return (int)(sizeof(s_quests) / sizeof(s_quests[0]));
}

static const QuestMsg *quest_msg(int npcId)
{
	for (int i = 0; i < (int)(sizeof(s_msgs) / sizeof(s_msgs[0])); i++)
	{
		if (s_msgs[i].npcId == npcId)
			return &s_msgs[i];
	}
	return NULL;
}

bool quest_can_deliver(const Player *p, const QuestDef *q)
{
	if (!p || !q)
		return false;
	int have = 0;
	for (int i = 0; i < BAG_SLOTS; i++)
	{
		const BagSlot *s = &p->bag[i];
		if (BAG_ID(*s) == q->giveItem && !BAG_EQUIPPED(*s))
			have += s->count;
	}
	return have >= q->giveCount;
}

bool quest_complete(Player *p, int npcId)
{
	const QuestDef *q = quest_find(npcId);
	if (!p || !q)
		return false;
	if (!quest_can_deliver(p, q))
	{
		log_add("你身上没有 %d 个「%s」。", q->giveCount, item_name(q->giveItem));
		return false;
	}
	BagSlot result[BAG_SLOTS];
	memcpy(result, p->bag, sizeof(result));
	int need = q->giveCount;
	for (int i = 0; i < BAG_SLOTS && need > 0; i++)
	{
		BagSlot *s = &result[i];
		if (BAG_ID(*s) != q->giveItem || BAG_EQUIPPED(*s))
			continue;
		int take = s->count < need ? s->count : need;
		inv_remove_slot(result, i, take);
		need -= take;
	}
	if (inv_add(result, q->rewardItem) < 0)
	{
		log_add("行囊已满，装不下「%s」。", item_name(q->rewardItem));
		return false;
	}
	memcpy(p->bag, result, sizeof(result));

	const NpcInfo *n = npc_info(npcId);
	log_add("你把 %d 个「%s」交给%s，得到「%s」。",
	        q->giveCount, item_name(q->giveItem), n ? n->name : "对方",
	        item_name(q->rewardItem));
	return true;
}

/* ---------------------------------------------------------------- dialog */

static DialogNode s_questNodes[2];
static DialogScript s_questScript;
static const char *const OFFER_OPTS[2] = { "好啊，换给你", "再想想" };

static int chat_random(int count)
{
	if (count <= 0)
		return 0;
	/* npc_quest.s always calls random_it(msg_number), even when it is 1:
	 * random_it(1) returns 0 but still advances mud_seed. */
	return (int)rng_random_it((u16)(count > 0xFFFF ? 0xFFFF : count));
}

static const DialogScript *build_offer(const NpcInfo *n, const QuestMsg *m)
{
	memset(s_questNodes, 0, sizeof(s_questNodes));

	s_questNodes[0].speaker = n->name;
	s_questNodes[0].text = m->chats[chat_random(m->chatCount)];
	s_questNodes[0].after = 1;
	s_questNodes[1].speaker = n->name;
	s_questNodes[1].text = m->offer;
	s_questNodes[1].optionCount = 2;
	for (int i = 0; i < 2; i++)
	{
		s_questNodes[1].options[i] = OFFER_OPTS[i];
		s_questNodes[1].next[i] = -1;
		s_questNodes[1].money[i] = 0;
		s_questNodes[1].optAction[i] = i == 0 ? DLG_ACT_QUEST : DLG_ACT_NONE;
	}
	s_questNodes[1].after = -1;

	s_questScript.nodes = s_questNodes;
	s_questScript.count = 2;
	return &s_questScript;
}

static const DialogScript *build_chat(const NpcInfo *n, const QuestMsg *m)
{
	memset(&s_questNodes[0], 0, sizeof(s_questNodes[0]));
	s_questNodes[0].speaker = n->name;
	s_questNodes[0].text = m->chats[chat_random(m->chatCount)];
	s_questNodes[0].after = -1;
	s_questScript.nodes = &s_questNodes[0];
	s_questScript.count = 1;
	return &s_questScript;
}

static const DialogScript *build_plain_chat(const NpcInfo *n, const QuestChat *c)
{
	memset(&s_questNodes[0], 0, sizeof(s_questNodes[0]));
	s_questNodes[0].speaker = n->name;
	s_questNodes[0].text = c->msgs[chat_random(c->count)];
	s_questNodes[0].after = -1;
	s_questScript.nodes = &s_questNodes[0];
	s_questScript.count = 1;
	return &s_questScript;
}

const DialogScript *quest_dialog_script(const Player *p, int npcId)
{
	const NpcInfo *n = npc_info(npcId);
	if (!n)
		return NULL;

	const QuestMsg *m = quest_msg(npcId);
	if (m)
	{
		const QuestDef *q = quest_find(npcId);
		if (q && quest_can_deliver(p, q))
			return build_offer(n, m);
		return build_chat(n, m);
	}

	for (int i = 0; i < (int)(sizeof(s_chats) / sizeof(s_chats[0])); i++)
	{
		if (s_chats[i].npcId == npcId)
			return build_plain_chat(n, &s_chats[i]);
	}
	return NULL;
}

const DialogScript *quest_thanks_script(int npcId)
{
	const NpcInfo *n = npc_info(npcId);
	const QuestMsg *m = quest_msg(npcId);
	if (!n || !m) return NULL;
	memset(&s_questNodes[0], 0, sizeof(s_questNodes[0]));
	s_questNodes[0].speaker = n->name;
	s_questNodes[0].text = m->thanks;
	s_questNodes[0].after = -1;
	s_questScript.nodes = s_questNodes;
	s_questScript.count = 1;
	return &s_questScript;
}

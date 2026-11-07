/* M5 schools, apprenticeship and training.
 *
 * Everything here is calibrated against the original assembly:
 *   - task.s menpai_tbl      (school kungfu tables)
 *   - serve.s baishi/...     (18 masters and their conditions)
 *   - pyh.s pyh_practice     (practice conditions and gains)
 *   - pyh.s pyh_learn        (learning from a master, potential cost)
 *   - lee1.s dazuo           (meditation and the force-cap breakthrough)
 *   - skill.s improve_skill  (pot >= (level+1)^2 -> level up)
 */
#include "school.h"

#include "audio.h"
#include "item.h"
#include "fighttext.h"
#include "log.h"
#include "npcdata.h"
#include "rng.h"
#include "skill.h"
#include "ui.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ tables */

/* menpai_tbl: { unarmed, weapon, dodge, force, parry } per pai 1..6. */
static const u8 SCHOOL_KF[SCHOOL_COUNT][5] =
{
	{ SK_BAGUAZ, SK_BAGUAD, SK_YOULONG, SK_HUNYUAN, SK_BAGUAD },    /* 八卦门 */
	{ SK_MEIHUA, SK_HUATUAN, SK_HUAFEI, SK_SANHUA, SK_HUATUAN },    /* 百花谷 */
	{ SK_TAIZU, SK_PIFENG, SK_HEXIAN, SK_TONGJI, SK_PIFENG },       /* 红莲教 */
	{ SK_WUFA, SK_YIDAO, SK_WUYING, SK_RENSHU, SK_YIDAO },          /* 哪吒派 */
	{ SK_TAIJIQUAN, SK_TAIJI_SWORD, SK_WANLIU, SK_TAIJIG, SK_TAIJI_SWORD }, /* 太极门 */
	{ SK_XUEYING, SK_XUESHAN_SWORD, SK_TAXUE, SK_XUESHAN_FORCE, SK_XUESHAN_SWORD } /* 雪山派 */
};

/* menpai_tbl weapon column: BLADE/WHIP/STAFF/BLADE/TIE_SWORD/TIE_SWORD. */
static const u8 SCHOOL_WEAPON[SCHOOL_COUNT] = { 11, 22, 21, 11, 35, 35 };

int school_kf(int pai, int slot)
{
	if (pai < 1 || pai > SCHOOL_COUNT || slot < 0 || slot > 4)
		return -1;
	return SCHOOL_KF[pai - 1][slot];
}

int school_weapon_item(int pai)
{
	if (pai < 1 || pai > SCHOOL_COUNT)
		return 0;
	return SCHOOL_WEAPON[pai - 1];
}

/* ------------------------------------------------------------- book study */

/* goods.dat book1_skill..book5_skill: `db count; db id,level; db id,level`.
 * use_book hands this table to skill.s study_cmd, which copies it into
 * npc_kfnum/npc_kf and opens the very same master_menu used by 请教.
 * Books are never consumed. */
typedef struct
{
	u8 itemId;
	u8 count;
	u8 kf[2];
	u8 level[2];
} BookDef;

static const BookDef s_books[] =
{
	{ 68, 2, { SK_BASIC_PARRY, SK_MENGHU },      { 250, 200 } }, /* 拳经 */
	{ 69, 2, { SK_BASIC_BARE,  SK_BASIC_DODGE }, { 250, 250 } }, /* 焦黄纸页 */
	{ 70, 2, { SK_BASIC_BLADE, SK_YIDAO },       { 250, 200 } }, /* 惊天刀谱 */
	{ 71, 2, { SK_BASIC_FORCE, SK_SANHUA },      { 250, 200 } }, /* 手抄本 */
	{ 81, 2, { SK_LOOKS,       SK_XI },          { 250, 100 } }  /* 菜花宝典 */
};
#define BOOK_COUNT ((int)(sizeof(s_books) / sizeof(s_books[0])))
#define BOOK_BAODIAN 81

static const BookDef *book_def(int itemId)
{
	for (int i = 0; i < BOOK_COUNT; i++)
		if (s_books[i].itemId == (u8)itemId)
			return &s_books[i];
	return NULL;
}

int book_skill_count(int itemId)
{
	const BookDef *b = book_def(itemId);
	return b ? b->count : 0;
}

int book_skill_id(int itemId, int index)
{
	const BookDef *b = book_def(itemId);
	return (b && index >= 0 && index < b->count) ? b->kf[index] : -1;
}

int book_skill_level(int itemId, int index)
{
	const BookDef *b = book_def(itemId);
	return (b && index >= 0 && index < b->count) ? b->level[index] : 0;
}

int book_kf_level(int itemId, int kfId)
{
	const BookDef *b = book_def(itemId);
	if (!b)
		return 0;
	for (int i = 0; i < b->count; i++)
		if (b->kf[i] == kfId)
			return b->level[i];
	return 0;
}

/* skill.s study_cmd: 读书识字 must be learned, and only 无门无派/逍遥派
 * may open a book. */
const char *book_read_error(const Player *p, int itemId)
{
	if (!book_def(itemId) || !p)
		return "这不是可研读的秘籍。";
	if (skill_level(p, SK_LITERATE) <= 0)
		return "你还是个文盲";
	if (p->pai != 0 && p->pai != 7)
		return "还是先练好本门武功吧";
	return NULL;
}

bool book_needs_gender(const Player *p, int itemId)
{
	return p && itemId == BOOK_BAODIAN && p->gender != 2;
}

/* --------------------------------------------------------------- masters */

bool school_is_master(int npcId)
{
	switch (npcId)
	{
	case 44:  /* 商剑鸣 */
	case 39:  /* 商宝震 */
	case 48:  /* 王维扬 */
	case 59:  /* 李青照 */
	case 57:  /* 平婆婆 */
	case 58:  /* 桑轻虹 */
	case 67:  /* 唐晚词 */
	case 74:  /* 方长老 */
	case 81:  /* 余鸿儒 */
	case 95:  /* 和仲阳 */
	case 88:  /* 花十郎 */
	case 91:  /* 藤王丸 */
	case 97:  /* 苍月道长 */
	case 98:  /* 古松道长 */
	case 102: /* 清虚道长 */
	case 123: /* 雪山教头 */
	case 111: /* 白瑞德 */
	case 119: /* 封万剑 */
		return true;
	default:
		return false;
	}
}

/* npc.s special_npc: TEACHER(顾炎武) and DAXIA(独行大侠) use npc_menu4
 * (交谈/查看/战斗/请教); KILLER(动态杀手) uses npc_menu1 WITHOUT 请教.
 * h/id.h: XUEBAO=124, KILLER=125; 雪豹 is an ordinary quest NPC. */
#define NPC_TEACHER 31
#define NPC_DAXIA 7

bool school_can_learn(const Player *p, int npcId)
{
	if (!p)
		return false;
	if (npcId == NPC_TEACHER || npcId == NPC_DAXIA)
		return true;
	return p->master == (u8)npcId;
}

/* serve.s baishi: the refusal strings are the master's own words, so keep
 * them verbatim (simplified-Chinese branch) instead of inventing short
 * reasons. Conditions follow the same source. */
const char *school_master_check(const Player *p, int npcId)
{
	if (!p)
		return "?";
	switch (npcId)
	{
	case 44: /* 商剑鸣 */
		if (p->gender == 1) return "我八卦门功夫一向传男不传女，你还是另择名师吧。";
		if (p->mpMax < 500) return "你内力不足，无法修习八卦门高深武功。";
		if (skill_level(p, SK_HUNYUAN) < 50) return "你混元一气功火候不够，无法修习八卦门高深武功。";
		if (skill_level(p, SK_BAGUAD) < 50) return "你还是先将刀法练好再说吧。";
		return NULL;
	case 39: /* 商宝震 */
		if (p->gender == 1) return "我八卦门功夫一向传男不传女，你还是另择名师吧。";
		return NULL;
	case 48: /* 王维扬 */
		if (p->gender == 1) return "我八卦门功夫一向传男不传女，你还是另择名师吧。";
		if (p->mpMax < 800) return "你内力不足，无法修习八卦门高深武功。";
		if (skill_level(p, SK_HUNYUAN) < 100) return "你混元一气功火候不够，无法修习八卦门高深武功。";
		if (skill_level(p, SK_BAGUAD) < 100)
			return "八卦刀是我派绝学，想当年老夫就凭一把八卦紫金刀闯荡天下，你还是先将刀法练好再说吧。";
		return NULL;
	case 59: /* 李青照 */
		if (p->gender == 0) return "我花间派向来只收女弟子,你还是另择名师吧。";
		if (p->intel < 31 && p->per < 25)
			return "我的嫡传弟子莫不是聪明伶俐，美貌如花，收你入门岂不有损我花间派的名头？";
		if (skill_level(p, SK_LITERATE) < 100)
			return "我花间派各路武功均蕴有深意，你读书识字基础不够，焉能领会高深武功？";
		if (p->mpMax < 1000) return "你内力修为不足，还是下去勤修好了再来吧！";
		return NULL;
	case 57: /* 平婆婆 */
		if (p->gender == 0) return "我花间派向来只收女弟子,你还是另择名师吧。";
		return NULL;
	case 58: /* 桑轻虹 */
		if (p->gender == 0) return "我花间派向来只收女弟子,你还是另择名师吧。";
		if (skill_level(p, SK_MEIHUA) < 60) return "你一剪梅花手功夫尚未纯熟，兵刃功夫又怎学得？";
		return NULL;
	case 67: /* 唐晚词 */
		if (p->gender == 0) return "我花间派向来只收女弟子,你还是另择名师吧。";
		if (skill_level(p, SK_MEIHUA) < 30) return "你一剪梅花手功夫尚未纯熟，兵刃功夫又怎学得？";
		return NULL;
	case 74: /* 方长老 */
		return NULL;
	case 81: /* 余鸿儒 */
		if (skill_level(p, SK_TONGJI) < 100) return "你普天同济心法火候不够，无法修习更高深武学。";
		if (p->str < 30) return "你膂力不足，无法领会乱披风杖法的精髓。";
		return NULL;
	case 95: /* 和仲阳 */
		if (skill_level(p, SK_RENSHU) < 120) return "扶桑功夫全需忍术支持,你还是下去勤修好了再来吧！";
		if (p->str < 32) return "我派一刀流刀法以威猛著称，力气不够是难有进境的！";
		return NULL;
	case 88: /* 花十郎 */
		if (skill_level(p, SK_WUFA) < 60)
			return "一刀流技法讲究出出手狠辣，你无法拳修为尚且不够，如何能领会高深武功？";
		return NULL;
	case 91: /* 藤王丸 */
		return NULL;
	case 97: /* 苍月道长 */
	case 98: /* 古松道长 */
		return NULL;
	case 102: /* 清虚道长 */
		if (p->mpMax < 1500) return "你内力不足，无法修习道家高深武功。";
		if (skill_level(p, SK_TAIJIG) < 120) return "你太极神功火候不够，无法修习道家高深武功。";
		if (skill_level(p, SK_TAIJIQUAN) < 100) return "太极拳是我派精华，还是专心将其练好再说吧。";
		if (p->intel < 28) return "太极剑讲究意在剑先，悟性不够难于领会其理。";
		return NULL;
	case 123: /* 雪山教头 */
		if (p->dex < 22) return "我雪山派武学主旨为招数精妙，依我看你的资质似乎不宜？";
		return NULL;
	case 111: /* 白瑞德: serve.s checks 雪山内功(XUESHANG_KF), not 雪影擒拿手 */
		if (skill_level(p, SK_XUESHAN_FORCE) < 100 && p->mpMax < 1200)
			return "我雪山独门武功雪影擒拿手需要深厚内力才可运用，我看你还是下去勤修好了再来吧！";
		if (p->con < 32) return "擒拿手需强劲内力，根骨太差进境缓慢，你还是先打好基本功再说吧！";
		return NULL;
	case 119: /* 封万剑 */
		if (p->dex < 23) return "我雪山派武学主旨为招数精妙，依我看你的资质似乎不宜？";
		if (skill_level(p, SK_XUESHAN_FORCE) < 40)
			return "你现在的雪山内功太薄弱，无法领会精妙的雪山剑法，还是学好再来吧.";
		return NULL;
	default:
		return NULL;
	}
}

bool school_join(Player *p, int npcId)
{
	const NpcInfo *n = npc_info(npcId);
	if (!p || !n || !school_is_master(npcId))
		return false;

	/* npc.s npc_apprentice: the self-taught path blocks all apprenticeship. */
	if (p->pai == 7)
	{
		log_add("你已自创门派,无需拜师吧?");
		return false;
	}
	if (p->pai != 0 && p->pai != n->pai)
	{
		log_add("你已另有名师,还想来我这儿偷师学艺吗?");
		return false;
	}

	const char *err = school_master_check(p, npcId);
	if (err)
	{
		log_add("%s", err);
		return false;
	}

	p->pai = (u8)n->pai;
	p->master = (u8)npcId;
	/* serve.s: each master says his own success line (suc_rts) before the
	 * shared kneel text added in npc.s baishi. Shared pairs: 桑轻虹/唐晚词,
	 * 方长老/余鸿儒, 花十郎/藤王丸, 苍月/古松. */
	static const struct { int npc; const char *line; } sucMsg[] = {
		{ 44,  "好吧，从今天起我亲受你功夫，你和宝震就是师兄弟了。" },
		{ 39,  "好吧，我暂且收下你，不过我爹的功夫我只学会三成，有机会你再向他老人家请教吧。" },
		{ 48,  "很好，没想到老夫就破例收你做关门弟子。" },
		{ 59,  "有你这样伶俐俊秀的弟子相伴左右也是乐事，为师一定好好点拨于你。" },
		{ 57,  "我花间派文武双修，多加努力吧。" },
		{ 58,  "掌门人要求只收俊俏女子...不过...好吧，我就收下你了。" },
		{ 67,  "掌门人要求只收俊俏女子...不过...好吧，我就收下你了。" },
		{ 74,  "很好，牢记红莲教义，跟我勤修武功吧。" },
		{ 81,  "很好，牢记红莲教义，跟我勤修武功吧。" },
		{ 95,  "你要多加努力，把我扶桑伊贺一派绝学发扬光大。" },
		{ 88,  "你随我好好练吧，保你以后飞黄腾达。" },
		{ 91,  "你随我好好练吧，保你以后飞黄腾达。" },
		{ 97,  "难得你有志武学，不过常言道太极三年不伤人，需得有常恒之心才能在太极门中出人头地。" },
		{ 98,  "难得你有志武学，不过常言道太极三年不伤人，需得有常恒之心才能在太极门中出人头地。" },
		{ 102, "很好，没想到老夫有生之年又觅到一可塑之才。" },
		{ 123, "很好，你跟著我勤加练习，日后如能和封师兄或白老爷子学上几招，那可终身受用。" },
		{ 111, "很好，你多加努力，他日必有所成。" },
		{ 119, "很好，你多加努力，他日争取将雪山剑派在你手中发扬光大。" },
	};
	for (size_t i = 0; i < sizeof(sucMsg) / sizeof(sucMsg[0]); i++)
	{
		if (sucMsg[i].npc == npcId)
		{
			log_add("%s", sucMsg[i].line);
			break;
		}
	}
	log_add("你跪了下来向%s恭恭敬敬地磕了四个响头，叫道：「师父！」", n->name);
	log_add("恭喜您成为%s弟子", npc_pai_name(n->pai));
	return true;
}

int school_master_kf_level(int npcId, int kfId)
{
	const NpcInfo *n = npc_info(npcId);
	if (!n)
		return 0;
	for (int i = 0; i < n->kfCount && i < NPC_KF_MAX; i++)
	{
		if (n->kf[i * 2] == kfId)
			return n->kf[i * 2 + 1];
	}
	return 0;
}

/* ---------------------------------------------------------------- dialog */

static DialogNode s_masterNode;
static DialogScript s_masterScript;
static char s_masterText[96];

static const char *const OPT_JOIN = "拜入门下";
static const char *const OPT_LEARN = "请教武学";
static const char *const OPT_LEAVE = "离开";
static const char *const TXT_LEARN = "你想学什么就说吧！";
static const char *const TXT_DAXIA_LOW = "去去去，攒够经验再来吧！";

/* npc.s npc_learn: 独行大侠 teaches only past 200000 exp. */
#define DAXIA_EXP_GATE 200000u

const DialogScript *school_dialog_script(const Player *p, int npcId)
{
	if (!p)
		return NULL;
	const NpcInfo *n = npc_info(npcId);
	if (!n)
		return NULL;
	bool master = school_is_master(npcId);
	/* npc.s special_npc: 私塾先生/独行大侠 carry 请教; the dynamic killer
	 * (125) uses npc_menu1 and cannot be asked. */
	bool special = npcId == 31 || npcId == 7;
	if (!master && !special)
		return NULL;

	memset(&s_masterNode, 0, sizeof(s_masterNode));
	s_masterNode.speaker = n->name;
	s_masterNode.after = -1;

	/* npc_learn checks the daxia exp gate before showing the menu. */
	if (npcId == 7 && p->exp < DAXIA_EXP_GATE)
	{
		s_masterNode.text = TXT_DAXIA_LOW;
		s_masterNode.optionCount = 0;
		s_masterScript.nodes = &s_masterNode;
		s_masterScript.count = 1;
		return &s_masterScript;
	}

	if (special)
		s_masterNode.text = TXT_LEARN;
	else
	{
		snprintf(s_masterText, sizeof(s_masterText), "（%s上下打量着你。）",
		         n->name);
		s_masterNode.text = s_masterText;
	}

	int count = 0;
	/* 拜师 only for serve.s masters that are not already your master. */
	if (master && p->master != (u8)npcId &&
	    (p->pai == 0 || p->pai == (u8)n->pai))
	{
		s_masterNode.options[count] = OPT_JOIN;
		s_masterNode.next[count] = -1;
		s_masterNode.money[count] = 0;
		s_masterNode.optAction[count++] = DLG_ACT_JOIN;
	}
	if (master ? (p->master == (u8)npcId) : true)
	{
		s_masterNode.options[count] = OPT_LEARN;
		s_masterNode.next[count] = -1;
		s_masterNode.money[count] = 0;
		s_masterNode.optAction[count++] = DLG_ACT_LEARN;
	}
	s_masterNode.options[count] = OPT_LEAVE;
	s_masterNode.next[count] = -1;
	s_masterNode.money[count] = 0;
	s_masterNode.optAction[count++] = DLG_ACT_NONE;
	s_masterNode.optionCount = (u8)count;

	s_masterScript.nodes = &s_masterNode;
	s_masterScript.count = 1;
	return &s_masterScript;
}

/* --------------------------------------------------------------- training */

static int rnd(int n)
{
	if (n <= 0)
		return 0;
	return (int)rng_random_it((u16)(n > 0xFFFF ? 0xFFFF : n));
}

/* pyh.s cal_exp: exp needed to make progress = my_level^3 / 10. */
static u32 cal_exp(int myLevel)
{
	u32 v = (u32)myLevel;
	return v * v * v / 10u;
}

bool train_practice_tick(Player *p, int kfId, char *msg, int size)
{
	const SkillDef *d = skill_get(kfId);
	if (!p || !d || (d->type != SKILL_TYPE_HAND && d->type != SKILL_TYPE_WEAPON &&
	                 d->type != SKILL_TYPE_DODGE))
	{
		snprintf(msg, size, "这门功夫练不了。");
		return false;
	}
	if (p->effhp < p->hpMax)
	{
		/* pyh.s hurt_msg. */
		snprintf(msg, size, "你受伤了，还是先治疗要紧。");
		return false;
	}
	int my = skill_level(p, kfId);
	if (my <= 0)
	{
		snprintf(msg, size, "你还没有学会这门功夫。");
		return false;
	}

	/* check_skill: 拳脚须空手；兵刃须装备同类型兵器。 Both refusal paths
	 * share the original no_weapon_msg. */
	if (d->type == SKILL_TYPE_HAND && p->weapon != 0)
	{
		snprintf(msg, size, "趁手的兵器都没有一把，瞎比划什么");
		return false;
	}
	if (d->type == SKILL_TYPE_WEAPON)
	{
		const ItemDef *w = item_get(p->weapon);
		if (!w || w->sub != d->qi)
		{
			snprintf(msg, size, "趁手的兵器都没有一把，瞎比划什么");
			return false;
		}
	}

	int basic = skill_basic_kf(kfId);
	int basicLv = basic >= 0 ? skill_level(p, basic) : 0;
	if (basicLv <= 0)
	{
		/* pyh.s no_basic_msg. */
		snprintf(msg, size, "想一步登天，可是你基本功夫还没学会呢");
		return false;
	}
	if (basicLv < my)
	{
		/* pyh.s basic_low_msg. */
		snprintf(msg, size, "你的功夫很难再有所提高了，还是向师傅请教一下吧");
		return false;
	}
	if (p->exp < cal_exp(my))
	{
		/* pyh.s exp_msg (shared with 请教). */
		snprintf(msg, size, "你的武学经验不足，无法领会更深的功夫");
		return false;
	}
	if ((int)p->mpMax < (my + basicLv / 2) * 10)
	{
		/* pyh.s maxfp_msg. */
		snprintf(msg, size, "你的内力修为不足，要勤修内功");
		return false;
	}

	int gain = basicLv / 5 + 1;
	int ups = skill_train(p, kfId, gain);
	if (ups > 0)
		snprintf(msg, size, "「%s」进步到 %d 级！", skill_name(kfId),
		         skill_level(p, kfId));
	else
		snprintf(msg, size, "练功中……「%s」熟练 +%d", skill_name(kfId), gain);
	return true;
}

/* pyh.s pyh_learn, parameterised by the target level. 请教 passes the
 * master NPC's level, 读书 passes the book table's level. */
bool train_learn_tick_level(Player *p, int kfId, int masterLv, char *msg, int size)
{
	if (!p)
		return false;
	int my = skill_level(p, kfId);
	/* cmp1 master_skill,my_skill; jcc you_high_master: only a STRICTLY
	 * higher player level is refused. Equal still allows one more round. */
	if (my > masterLv)
	{
		/* pyh.s skill_high_msg. */
		snprintf(msg, size, "你的功夫已经不输为师了，真是可喜可贺呀");
		return false;
	}
	if (p->exp < cal_exp(my))
	{
		/* pyh.s exp_msg (shared with 练功). */
		snprintf(msg, size, "你的武学经验不足，无法领会更深的功夫");
		return false;
	}
	if (p->potential == 0)
	{
		snprintf(msg, size, "你的潜能已经发挥到极限了。");
		return false;
	}
	/* pyh.s:211-215 decrements man_pot BEFORE the tuition check; a failed
	 * 读书识字 payment still costs one point of potential. */
	p->potential--;

	if (kfId == SK_LITERATE)
	{
		/* pyh.s sub_money tuition ladder for 读书识字. */
		int tuition = my < 20 ? 5 : my < 30 ? 10 : my < 60 ? 50
		              : my < 80 ? 150 : my < 100 ? 300 : my < 120 ? 500 : 1000;
		if (p->money < (u32)tuition)
		{
			/* pyh.s learn_money_msg. */
			snprintf(msg, size, "没钱读什么书啊，回去准备够学费再来吧");
			return false;
		}
		p->money -= (u32)tuition;
	}

	/* pyh.s:245-262: random_it(悟性) FIRST, then random_it(福缘/5).
	 * Split the calls: C does not define the evaluation order of `+`. */
	int randInt = rnd(p->intel);
	int randKar = rnd(p->kar / 5);
	int gain = (p->intel / 2 + randInt) / 2 + randKar;
	if (gain < 1)
		gain = 1;

	/* learn_it calls add_kf before show_process: the first round teaches
	 * the kungfu at level 1 (original add_kf level 0 + improve_skill). */
	if (my == 0)
	{
		if (!skill_add(p, kfId, 1))
		{
			p->potential++;   /* add_kf failed before the loop started */
			snprintf(msg, size, "武功簿已经满了，学不了新功夫。");
			return false;
		}
		snprintf(msg, size, "「%s」入门了！（Lv1）", skill_name(kfId));
		return true;
	}

	int ups = skill_train(p, kfId, gain);
	if (ups > 0)
	{
		snprintf(msg, size, "「%s」进步到 %d 级！", skill_name(kfId),
		         skill_level(p, kfId));
		{
			char toast[48];
			snprintf(toast, sizeof(toast), "「%s」升到 %d 级",
			         skill_name(kfId), skill_level(p, kfId));
			ui_toast(toast);
		}
		audio_play(SFX_LEVELUP);
	}
	else
		snprintf(msg, size, "指点中……「%s」熟练 +%d（潜能 -1）", skill_name(kfId), gain);
	return true;
}

bool train_learn_tick(Player *p, int kfId, int masterNpc, char *msg, int size)
{
	return train_learn_tick_level(p, kfId,
	                              school_master_kf_level(masterNpc, kfId),
	                              msg, size);
}

bool train_dazuo_tick(Player *p, char *msg, int size)
{
	if (!p)
		return false;
	/* lee1.s dazuo requires an ENABLED force kungfu (man_usekf+3 bit7). */
	if (skill_usekf_id(p, SKILL_TYPE_FORCE) <= 0)
	{
		/* lee1.s dazuo_noenable_force -> heal_enable_msg. */
		fight_perform_msg_self(msg, size, PF_MSG_HEAL_ENABLE, p->name);
		return false;
	}
	int eff = skill_query_level(p, SKILL_TYPE_FORCE);
	if (eff <= 0)
	{
		/* lee1.s dazuo_no_level: fp is clamped to maxfp and the original
		 * shows dazuo_level_msg. */
		p->mp = p->mpMax;
		fight_perform_msg_self(msg, size, PF_MSG_DAZUO_LEVEL, p->name);
		return false;
	}

	/* force_gain = eff_level/10 + con/5. */
	int gain = eff / 10 + p->con / 5;
	if (p->mp < (int)p->mpMax * 2)
	{
		p->mp = (s16)(p->mp + gain);
		snprintf(msg, size, "打坐中……内力 +%d", gain);
		return true;
	}

	/* Breakthrough: req = eff_level*10 + (min(age,60)-14)*先天根骨 + exp/1000。
	 * lee1.s: maxfp >= req -> dazuo_no_level (fp clamped, fail);
	 * otherwise maxfp+1 and fp = 0. */
	int age = p->age > 60 ? 60 : p->age;
	int years = age - 14;
	if (years < 0)
		years = 0;
	u32 req = (u32)eff * 10u + (u32)(years * p->attrCon) + p->exp / 1000u;
	if ((int)p->mpMax >= (int)req)
	{
		/* lee1.s dazuo_no_level again: fp is clamped and the original
		 * shows dazuo_level_msg. */
		p->mp = p->mpMax;
		fight_perform_msg_self(msg, size, PF_MSG_DAZUO_LEVEL, p->name);
		return false;
	}
	p->mpMax++;
	p->mp = 0;
	actor_setup_maxhp(p);   /* maxhp = ... + maxfp/4 */
	snprintf(msg, size, "内力充盈，内力上限提升到 %d！", p->mpMax);
	return true;
}

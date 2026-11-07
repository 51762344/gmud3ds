#include <3ds.h>

#include <stdio.h>
#include <string.h>

#include "actor.h"
#include "audio.h"
#include "battle.h"
#include "charcreate.h"
#include "dialog.h"
#include "fighttext.h"
#include "gmud.h"
#include "gfx.h"
#include "item.h"
#include "log.h"
#include "map.h"
#include "minigame.h"
#include "neili.h"
#include "npcdata.h"
#include "quest.h"
#include "rng.h"
#include "save.h"
#include "school.h"
#include "settings.h"
#include "skill.h"
#include "task.h"
#include "text.h"
#include "ui.h"

enum
{
	MODE_FIELD = 0,
	MODE_DIALOG,
	MODE_MENU,
	MODE_LOOK,
	MODE_SYSTEM,
	MODE_QUIT,
	MODE_SAVE,
	MODE_BAG,
	MODE_SHOP,
	MODE_BATTLE,
	MODE_SKILLS,
	MODE_TRAIN,
	MODE_NEILI,
	MODE_ENDING,
	MODE_RANK,
	MODE_EPILOGUE,
	MODE_GAMEOVER,
	MODE_CHARCREATE,
	MODE_TITLE,         /* 3DS port addition: boot menu with 继续游戏 */
	MODE_SETTINGS,      /* 菜单 → 设置: 主题 / 存档 / 结束 */
	MODE_MINIGAME      /* 山丘 (panyan.s) */
};

/* Held-direction repeat for lists, steppers and menus.  Every page used to
 * move exactly one row per physical press, which is what made the menus feel
 * unresponsive next to the walking speed: 20 rows of 技能 needed 20 taps.
 * The first press is delivered by the caller from hidKeysDown(); this helper
 * only produces the follow-up presses. */
#define NAV_REPEAT_DELAY 0.26f
#define NAV_REPEAT_RATE  0.07f
static float s_navTimer[4];

static u32 nav_repeat(u32 down, u32 held, float dt, bool enable)
{
	static const u32 DIRS[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
	u32 extra = 0;
	for (int i = 0; i < 4; i++)
	{
		u32 key = DIRS[i];
		if (!enable)
		{
			/* Walking / fighting / mini games read `held` themselves; the
			 * timers must not run there, or opening a menu with a direction
			 * still down would scroll it immediately. */
			s_navTimer[i] = 0.0f;
			continue;
		}
		if (down & key)
		{
			s_navTimer[i] = NAV_REPEAT_DELAY;
			continue;
		}
		if (!(held & key))
		{
			s_navTimer[i] = 0.0f;
			continue;
		}
		s_navTimer[i] -= dt;
		if (s_navTimer[i] <= 0.0f)
		{
			extra |= key;
			s_navTimer[i] += NAV_REPEAT_RATE;
		}
	}
	return extra;
}

/* h/gmud.h SAVE_INTERVAL = 300 s: game.s end_game offers a save when the
 * current run has not been written for that long. */
#define GMUD_SAVE_INTERVAL 300u

/* Which music track belongs to which screen: exploration for the world
 * (including talking to people), battle for the fight, menu for every menu.
 * Kept as the wiring a future sound bank plugs into. */
static int bgm_for_mode(int mode)
{
	switch (mode)
	{
	case MODE_FIELD:
	case MODE_DIALOG:
	case MODE_LOOK:
		return BGM_FIELD;
	case MODE_BATTLE:
		return BGM_BATTLE;
	case MODE_GAMEOVER:
		return BGM_NONE;
	default:
		return BGM_MENU;
	}
}

static Player *s_player;
static SaveSlotInfo s_slots[SAVE_SLOT_COUNT];
static Battle s_battle;
static MiniGame s_minigame;
static int s_currentSlot = -1;   /* slot used by 读档/存档, for 自杀删除 */
static bool s_canSave;           /* save_init() succeeded */
/* Where 存档 returns to: 设置, the title screen, a quit prompt, ... */
static int s_saveReturnMode = MODE_SETTINGS;
/* Auto-save clock: only counts while the player is actually walking around. */
static float s_autoSaveTimer;
static int s_talkNpc;
static u16 s_talkPortrait;
static int s_shopRequest;
static int s_shopOption;       /* index of the chosen dialog option */
static bool s_npcDetail;
static int s_trainReturnMode = MODE_FIELD;
static bool s_trainReturnNpc;   /* 学艺 came from an NPC: 返回 re-opens its menu */
static int s_pendingBook;
static bool s_quitPrompt;
static bool s_saveThenQuit;
static u32 s_lastSaveTime;
/* Map-change wipe: 1 = fully black, fades back to 0. */
static float s_transition;
/* The save page was opened from the title screen: Back returns there. */
static bool s_saveFromTitle;
static DialogNode s_npcNode;
static DialogScript s_npcScript = { &s_npcNode, 1 };
static DialogNode s_lookNodes[2];
static DialogScript s_lookScript = { s_lookNodes, 2 };
static char s_npcText[512];

static int npc_level(const NpcInfo *n, int id)
{
	for (int i = 0; i < n->kfCount; i++)
		if (n->kf[i * 2] == id) return n->kf[i * 2 + 1];
	return 0;
}

static int npc_effective_skill(const NpcInfo *n, int slot)
{
	int basic = slot == SKILL_TYPE_HAND ? SK_BASIC_BARE : slot == SKILL_TYPE_DODGE ? SK_BASIC_DODGE : SK_BASIC_PARRY;
	if (slot == SKILL_TYPE_WEAPON)
	{
		const ItemDef *w = item_get(n->weapon);
		static const int weaponBasic[] = { -1, SK_BASIC_BLADE, SK_BASIC_CLUB, -1, -1, -1, SK_BASIC_SWORD, SK_BASIC_STAFF, -1, SK_BASIC_WHIP };
		basic = w && w->sub < 10 ? weaponBasic[w->sub] : -1;
	}
	int base = npc_level(n, basic);
	bool hasBase = false;
	for (int i = 0; i < n->kfCount; i++)
		if (n->kf[i * 2] == basic) hasBase = true;
	if (!hasBase) return 0;
	int enabled = n->usekf[slot];
	return base / 2 + ((enabled & 0x80) ? npc_level(n, enabled & 0x7f) : 0);
}

static const char *npc_martial(const NpcInfo *n)
{
	return actor_martial_desc_levels(
	    npc_effective_skill(n, (n->equippedFlags & 1) ? SKILL_TYPE_WEAPON : SKILL_TYPE_HAND),
	    npc_effective_skill(n, SKILL_TYPE_DODGE),
	    npc_effective_skill(n, SKILL_TYPE_PARRY));
}

static void npc_menu(Player *p, Dialog *d)
{
	if (s_talkNpc == TASK_GHOST_NPC)
		task_ghost_prepare(p);
	const NpcInfo *n = npc_info(s_talkNpc);
	memset(&s_npcNode, 0, sizeof(s_npcNode));
	s_npcNode.speaker = n ? n->name : NULL;
	s_npcNode.text = "请选择";
	s_npcNode.after = -1;
	const char *labels[] = { "交谈", "查看", "战斗", NULL };
	int actions[] = { DLG_ACT_TALK, DLG_ACT_LOOK, DLG_ACT_FIGHT, 0 };
	int count = 3;
	if (s_talkNpc != 125 && school_can_learn(p, s_talkNpc))
		labels[3] = "请教", actions[3] = DLG_ACT_LEARN, count = 4;
	else if (npc_is_merchant(n))
		labels[3] = "交易", actions[3] = DLG_ACT_SHOP_BUY, count = 4;
	else if (s_talkNpc != 125 && n && n->pai >= 1 && n->pai <= 7)
		labels[3] = "拜师", actions[3] = DLG_ACT_JOIN, count = 4;
	for (int i = 0; i < count; i++)
	{
		s_npcNode.options[i] = labels[i];
		s_npcNode.optAction[i] = actions[i];
		s_npcNode.next[i] = -1;
	}
	s_npcNode.optionCount = count;
	s_npcDetail = false;
	dialog_start(d, &s_npcScript);
}

static void npc_detail(Player *p, Dialog *d, bool look, u32 playSeconds)
{
	const NpcInfo *n = npc_info(s_talkNpc);
	const DialogScript *script = NULL;
	if (!look)
		script = task_talk_script(p, s_talkNpc, playSeconds);
	if (!script && !look)
		script = quest_dialog_script(p, s_talkNpc);
	if (!script)
	{
		memset(&s_npcNode, 0, sizeof(s_npcNode));
		s_npcNode.speaker = n ? n->name : NULL;
		s_npcNode.after = -1;
		if (look && n)
		{
			static const char *const strength[] = { "极轻", "很轻", "不轻", "不重", "很重", "极重" };
			/* npc.s init_npc skips the str/dex derivation when no armor is
			 * equipped (goods[1] bit7 clear); match that for 查看. */
			int str = n->str;
			if (n->equippedFlags & 2)
				str += npc_level(n, SK_BASIC_BARE) / 10;
			int force = (str + n->damage + n->force / 2) / 20;
			snprintf(s_npcText, sizeof(s_npcText),
			         "看起来约%d多岁。\n武艺看起来%s\n出手似乎%s\n带着：%s %s %s",
			         (n->age / 10) * 10, npc_martial(n), strength[force < 5 ? force : 5],
			         (n->equippedFlags & 1) ? item_name(n->weapon) : "",
			         (n->equippedFlags & 2) ? item_name(n->equip) : "",
			         (n->thirdEquip & 0x80) ? item_name(n->thirdEquip & 0x7f) : "");
			memset(s_lookNodes, 0, sizeof(s_lookNodes));
			s_lookNodes[0].speaker = n->name;
			s_lookNodes[0].text = s_npcText;
			s_lookNodes[0].after = 1;
			s_lookNodes[1].speaker = n->name;
			s_lookNodes[1].text = n->desc[0] ? n->desc : "这人没有多说什么。";
			s_lookNodes[1].after = -1;
		}
		else
		{
			static const char *const replies[] = {
				"%s睁大眼睛望着你，显然不知道你在说什么。",
				"%s打了个哈哈：今天的天气真是，哈哈。",
				"%s看了你一眼，又转身忙自己的事情去了。",
				"没看到我在忙吗？你还是找别人聊天去吧。",
				"我什么也不知道，就算知道也不说，打死你我也不说。"
			};
			int choice = (int)rng_random_it(5);
			snprintf(s_npcText, sizeof(s_npcText), replies[choice], n ? n->name : "这人");
			s_npcNode.speaker = NULL;
		}
		s_npcNode.text = s_npcText;
		script = look && n ? &s_lookScript : &s_npcScript;
	}
	s_npcDetail = true;
	dialog_start(d, script);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Camera target: the player sits in the middle of the viewport, clamped to
 * the map. */
static void camera_target(const GameMap *m, const Player *p, int *camX, int *camY)
{
	int maxX = m->w * TILE - MAP_VIEW_W;
	int maxY = m->h * TILE - MAP_VIEW_H;
	if (maxX < 0) maxX = 0;
	if (maxY < 0) maxY = 0;
	*camX = clampi(p->px + TILE / 2 - MAP_VIEW_W / 2, 0, maxX);
	*camY = clampi(p->py + TILE / 2 - MAP_VIEW_H / 2, 0, maxY);
}

/* Eases the camera towards its target so a step slides the view instead of
 * snapping it - the 3DS port's feel change, invisible in the rules.  A map
 * change snaps, because the new map has its own size. */
static void update_camera(const GameMap *m, const Player *p, int *camX, int *camY,
                          float dt, bool snap)
{
	int targetX, targetY;
	camera_target(m, p, &targetX, &targetY);
	if (snap)
	{
		*camX = targetX;
		*camY = targetY;
		return;
	}
	/* 22/s instead of the old 9/s: the camera still slides, but it lands
	 * within ~4 frames of a step (160 ms/tile), so the character stays put
	 * in the middle of the viewport instead of drifting behind the input. */
	float k = dt * 22.0f;
	if (k > 1.0f) k = 1.0f;
	*camX += (int)((float)(targetX - *camX) * k);
	*camY += (int)((float)(targetY - *camY) * k);
	if (targetX - *camX <= 2 && *camX - targetX <= 2) *camX = targetX;
	if (targetY - *camY <= 2 && *camY - targetY <= 2) *camY = targetY;
}

/* Steps onto a door tile -> switch map. Returns true if the map changed. */
static bool try_door(Player *p)
{
	const GameMap *m = map_get(p->mapIndex);
	const Door *d = map_door_at(m, p->tx, p->ty);
	if (!d)
		return false;
	if (d->targetMap >= map_count())
		return false;
	const GameMap *tm = map_get(d->targetMap);
	if (!tm || d->tx >= tm->w || d->ty >= tm->h)
		return false;
	p->mapIndex = d->targetMap;
	actor_place(p, d->tx, d->ty);
	audio_play(SFX_DOOR);
	return true;
}

/* NPC placement-table index on the tile the player faces, or -1. */
static int facing_npc_index(const Player *p, const GameMap *m)
{
	int x = p->tx, y = p->ty;
	switch (p->dir)
	{
	case DIR_LEFT: x--; break;
	case DIR_RIGHT: x++; break;
	case DIR_UP: y--; break;
	default: y++; break;
	}

	u16 cell = map_cell(m, x, y);
	if (cell == 0xFFFF)
		return -1;
	u16 obj = CELL_OBJ(cell);
	if (obj < NPC_OFF)
		return -1;

	int index = (int)(obj - NPC_OFF);
	if (index >= map_npc_count() || map_npc_dead_index((unsigned)index))
		return -1;
	return index;
}

/* --------------------------------------------- interactive map objects */
/* item_action_tbl in talk.s: object ids >= 200 are usable scenery. */
static bool s_objectTalk;
static bool s_loadOnly;
static float s_endingTimer;
static int s_finalBoss;
static int s_endingBoss;
static int s_endingPage;

static int stone_count(const Player *p)
{
	int count = 0;
	for (int i = 0; i < 6; i++)
		if (p->stones & (1u << i))
			count++;
	return count;
}

/* talk.s final_fight: which boss appears depends on 道德和尚 being alive
 * and the player's 0..255 man_daode (GOODMAN=160, BADMAN=100). */
static int final_boss_id(const Player *p)
{
	u8 moral = actor_morality_raw(p);
	bool daodeAlive = (p->deadNpc[12 / 8] & (1u << (12 % 8))) == 0;
	if (daodeAlive)
		return moral >= 160 ? 127 : 126;
	if (moral < 100)
		return 128;
	return moral >= 160 ? 127 : 126;
}

static bool object_has_equipped(const Player *p, int itemId)
{
	if (p->weapon == itemId)
		return true;
	for (int i = 0; i < EQUIP_SLOTS; i++)
		if (p->equip[i] == itemId)
			return true;
	for (int i = 0; i < BAG_SLOTS; i++)
		if (BAG_ID(p->bag[i]) == itemId && BAG_EQUIPPED(p->bag[i]))
			return true;
	return false;
}

static void object_open_dialog(Dialog *d, UiState *ui, int *mode,
                               const DialogScript *script)
{
	dialog_start(d, script);
	s_objectTalk = true;
	s_npcDetail = false;
	*mode = MODE_DIALOG;
	ui_set_page(ui, UI_PAGE_DIALOG);
}

/* fight.s final_fight: 我是谁/东方求败 fight after the intro; 道德和尚
 * offers 1.回去 / 2.少废话. The original picks the boss by man_daode and
 * whether 道德和尚 (NPC 12) is still alive. */
static DialogNode s_finalNodes[8];
static DialogScript s_finalScript;
static char s_finalTexts[8][192];

static void final_fight_dialog(Player *p, Dialog *d, UiState *ui, int *mode)
{
	s_finalBoss = final_boss_id(p);
	const NpcInfo *boss = npc_info(s_finalBoss);
	memset(s_finalNodes, 0, sizeof(s_finalNodes));
	memset(s_finalTexts, 0, sizeof(s_finalTexts));

	int count = 0;

	for (int i = 0; i < 8; i++)
	{
		s_finalNodes[i].speaker = NULL;
		s_finalNodes[i].after = -1;
	}

	if (s_finalBoss == 126)
	{
		snprintf(s_finalTexts[0], sizeof(s_finalTexts[0]),
		         "在每个故事的结尾,通常都会有一个最终BOSS。\n"
		         "遇上我,是你的不幸,要怪就怪那个幕后的导演吧!");
		s_finalNodes[0].speaker = boss ? boss->name : "我是谁";
		s_finalNodes[0].text = s_finalTexts[0];
		s_finalNodes[0].optionCount = 1;
		s_finalNodes[0].options[0] = "动手";
		s_finalNodes[0].optAction[0] = DLG_ACT_FINAL_FIGHT;
		count = 1;
	}
	else if (s_finalBoss == 127)
	{
		static const char *const LINES[5] =
		{
			"哈哈----------!没想到吧。",
			"难道神秘人是...",
			"不错。\n十年前,自诩为名门正派的六大门派合力偷袭,把我关在这里...",
			"我会把你再次封印。",
			"做梦吧.以你的实力,十万年还早!"
		};
		for (int i = 0; i < 5; i++)
		{
			snprintf(s_finalTexts[i], sizeof(s_finalTexts[i]), "%s", LINES[i]);
			s_finalNodes[i].speaker = (i == 1 || i == 3) ? p->name : (boss ? boss->name : "东方求败");
			s_finalNodes[i].text = s_finalTexts[i];
			s_finalNodes[i].after = i + 1;
		}
		s_finalNodes[4].optionCount = 1;
		s_finalNodes[4].options[0] = "动手";
		s_finalNodes[4].optAction[0] = DLG_ACT_FINAL_FIGHT;
		s_finalNodes[4].after = -1;
		count = 5;
	}
	else
	{
		snprintf(s_finalTexts[0], sizeof(s_finalTexts[0]), "苦海无边,回头是岸。");
		snprintf(s_finalTexts[1], sizeof(s_finalTexts[1]), "不可能!\n难道你没有死?");
		snprintf(s_finalTexts[2], sizeof(s_finalTexts[2]),
		         "施主罪孽深重,老纳是不会让你回到原来的世界再次危害天下的.施主,请回吧。");
		s_finalNodes[0].speaker = boss ? boss->name : "道德和尚";
		s_finalNodes[1].speaker = p->name;
		s_finalNodes[2].speaker = boss ? boss->name : "道德和尚";
		for (int i = 0; i < 3; i++)
		{
			s_finalNodes[i].text = s_finalTexts[i];
			s_finalNodes[i].after = i + 1;
		}
		s_finalNodes[2].optionCount = 2;
		s_finalNodes[2].options[0] = "回去";
		s_finalNodes[2].options[1] = "少废话";
		s_finalNodes[2].optAction[0] = DLG_ACT_NONE;
		s_finalNodes[2].optAction[1] = DLG_ACT_FINAL_FIGHT;
		s_finalNodes[2].after = -1;
		count = 3;
	}

	for (int i = 0; i < count; i++)
		for (int k = 0; k < s_finalNodes[i].optionCount; k++)
			s_finalNodes[i].next[k] = -1;

	s_finalScript.nodes = s_finalNodes;
	s_finalScript.count = count;
	object_open_dialog(d, ui, mode, &s_finalScript);
}

static DialogNode s_suicideNode;
static DialogScript s_suicideScript;

/* lee1.s jiali: input_digit clamps the value to the effective force level/2
 * and stores it in man_force. The original prints a message only when the
 * player dials in the cap: jiali_flow_msg with the runtime number inserted
 * ("db '你目前加力上限为',2 / dw man_force,0"). Without an enabled inner
 * kungfu it shows heal_enable_msg instead. */
static void jiali_commit(Player *p, int value)
{
	char msg[160];
	if (skill_usekf_id(p, SKILL_TYPE_FORCE) <= 0)
	{
		fight_perform_msg_self(msg, sizeof(msg), PF_MSG_HEAL_ENABLE, p->name);
		log_add("%s", msg);
		return;
	}
	int max = neili_jiali_max(p);
	if (value > max)
		value = max;
	if (value < 0)
		value = 0;
	p->force = (s16)value;
	if (value >= max)
	{
		neili_jiali_text(msg, sizeof(msg), p, p->force);
		log_add("%s", msg);
	}
}

/* The 32x32 map image of an NPC, used as the battle portrait. The map NPC
 * list holds one entry per placed NPC. */
static u16 npc_portrait_tile(int npcId)
{
	int count = map_npc_count();
	for (int i = 0; i < count; i++)
	{
		const Npc *n = map_npc(i);
		if (n && n->npcId == npcId)
			return n->tile;
	}
	return 0xFFFF;   /* out of range: the battle draws the fallback sprite */
}

/* nf.s netfight: the original 擂台 is a two-player duel over the 文曲星 link
 * cable (创建对战 / 加入对战). The 3DS has no such link, so the arena became a
 * sparring match against one of the challengers below — the fight itself is
 * the normal battle system, only the outcome never kills the opponent. */
static const int ARENA_OPPONENTS[3] = { 21, 106, 40 };
static DialogNode s_arenaNodes[2];
static DialogScript s_arenaScript;

static void arena_menu(Dialog *d)
{
	memset(s_arenaNodes, 0, sizeof(s_arenaNodes));
	s_arenaNodes[0].text = "这是联机对战的擂台,原作靠文曲星联机线对战,这里改为擂台挑战。";
	s_arenaNodes[0].after = 1;
	s_arenaNodes[1].text = "你想挑战哪一位?";
	s_arenaNodes[1].optionCount = 3;
	for (int i = 0; i < 3; i++)
	{
		const NpcInfo *n = npc_info(ARENA_OPPONENTS[i]);
		s_arenaNodes[1].options[i] = n ? n->name : "无名氏";
		s_arenaNodes[1].next[i] = -1;
		s_arenaNodes[1].optAction[i] = DLG_ACT_ARENA;
	}
	s_arenaScript.nodes = s_arenaNodes;
	s_arenaScript.count = 2;
	dialog_start(d, &s_arenaScript);
}

static void object_interact(Player *p, int objectId, Dialog *d, UiState *ui,
                            int *mode, u32 *playSeconds)
{
	switch (objectId)
	{
	case 200:   /* STAGE_ID: 擂台 (nf.s netfight) */
		arena_menu(d);
		*mode = MODE_DIALOG;
		ui_set_page(ui, UI_PAGE_DIALOG);
		break;
	case 201:   /* HILL2_ID: 山丘 (panyan.s 游戏厅) */
		minigame_open(&s_minigame);
		*mode = MODE_MINIGAME;
		break;
	case 202:   /* BROOM_ID */
	case 203:   /* PAIL_ID */
	case 204:   /* WOOD_ID */
	{
		const DialogScript *script = task_chore_script(p, objectId - 201);
		if (script)
			object_open_dialog(d, ui, mode, script);
		break;
	}
	case 205:   /* WELL_ID */
		/* talk.s do_drink: compare with maxwater, then add 20 without a cap. */
		if (p->water >= p->maxWater)
			log_add("你已经再也喝不下一滴水了");
		else
		{
			p->water = (u16)(p->water + 20);
			log_add("你在井边用杯子舀起井水喝了几口");
		}
		break;
	case 206:   /* FISH_ID */
		if (!object_has_equipped(p, 73))
			log_add("你看着池塘中的鱼,摸了摸肚皮,心中暗自思量:要是现在有个钓杆该多好啊!");
		else if (p->hp < 40)
			log_add("现在日头这么毒,以你的体质,恐怕还没钓到鱼就已经...");
		else
		{
			p->hp -= 40;
			log_add("你在土里刨了刨,抓了只小蚯蚓,穿在钓钩上,一挥手,钓线划出一道优美的弧线扎进了水里.你往地上一蹲,开始钓鱼.");
			if (rng_random_it(10) < 5)
			{
				if (inv_count(p->bag, 76) == 0)
					log_add("你把鱼从钩上摘下来,猛然想起:坏了,没带鱼篓!一分神,鱼一下子滑到了水里,到手的鱼跑了!");
				else
				{
					inv_add(p->bag, 74);
					log_add("你把鱼放进鱼篓,心里乐开了花,今天的零花钱有著落了!");
				}
			}
			else
				log_add("过了很久也没有鱼上钩,你一收钓竿,到树下乘凉去了!");
		}
		break;
	case 207:   /* SUICIDE_ID */
		if (!inv_count(p->bag, 19))
			log_add("活得太没意思了,真想找根绳子上吊自杀");
		else
		{
			memset(&s_suicideNode, 0, sizeof(s_suicideNode));
			s_suicideNode.speaker = NULL;
			s_suicideNode.text = "如果您选择上吊自杀\n您的资料就永远删除了\n请务必考虑清楚!!";
			s_suicideNode.optionCount = 2;
			s_suicideNode.options[0] = "算了";
			s_suicideNode.options[1] = "自杀";
			s_suicideNode.optAction[1] = DLG_ACT_SUICIDE;
			s_suicideNode.next[0] = -1;
			s_suicideNode.next[1] = -1;
			s_suicideNode.after = -1;
			s_suicideScript.nodes = &s_suicideNode;
			s_suicideScript.count = 1;
			object_open_dialog(d, ui, mode, &s_suicideScript);
		}
		break;
	case 208:   /* BOARD_ID: 告示牌 (good players see their own bounty) */
		if (actor_morality_raw(p) < 128)
			log_add("悬赏缉拿人犯『%s』", p->name);
		else
			log_add("本镇治安良好");
		break;
	case 209:   /* BED_ID: 睡觉 needs 女儿红 */
	{
		int slot = -1;
		for (int i = 0; i < BAG_SLOTS; i++)
			if (BAG_ID(p->bag[i]) == 78 && !BAG_EQUIPPED(p->bag[i])) { slot = i; break; }
		if (slot >= 0 && inv_remove_slot(p->bag, slot, 1))
		{
			*playSeconds += 10800u;   /* month3: 3 hours of mud_age */
			actor_sync_time(p, *playSeconds);
			log_add("好困啊...ZZZ...");
		}
		break;
	}
	case 210:   /* MAGIC_ID: 六芒星阵, 最终决战 */
		if (stone_count(p) < 6)
			log_add("六芒星阵毫无反应，还缺 %d 块三角石板。", 6 - stone_count(p));
		else
			final_fight_dialog(p, d, ui, mode);
		break;
	case 211:   /* BOOK_ID: 菜花宝典 (恶人/成年/无老花镜 invisible) */
		/* talk.s bao_dian: only gender==1 (女) is refused; 男(0) and 人妖(2)
		 * both pass. */
		if (p->gender != 1 && actor_morality_raw(p) < 128 && p->age < 18 &&
		    object_has_equipped(p, 40))
		{
			if (inv_add(p->bag, 81) >= 0)
				log_add("发现菜花宝典");
			else
				log_add("行囊已满，未能取得菜花宝典。");
		}
		break;
	case 212:   /* 绳子 (ROPE=19) */
		if (inv_count(p->bag, 19) == 0)
		{
			if (inv_add(p->bag, 19) >= 0)
				log_add("发现麻绳");
			else
				log_add("行囊已满，未能取得麻绳。");
		}
		break;
	default:
		log_add("这东西似乎没什么用。");
		break;
	}
}

static bool start_interact(Player *p, const GameMap *m, Dialog *d, UiState *ui,
                       int *mode, u32 *playSeconds)
{
	int fx = p->tx, fy = p->ty;
	if (p->dir == DIR_UP) fy--;
	else if (p->dir == DIR_DOWN) fy++;
	else if (p->dir == DIR_LEFT) fx--;
	else fx++;

	/* tools.s judge_killer: the killer is not in the NPC table. */
	if (task_ghost_at(p, fx, fy))
	{
		s_talkNpc = TASK_GHOST_NPC;
		s_talkPortrait = task_ghost_tile(p);
		s_shopRequest = DLG_ACT_NONE;
		s_objectTalk = false;
		/* npc_menu() runs task_ghost_prepare() once, matching init_npc's
		 * single random_it(6) draw per interaction. */
		npc_menu(p, d);
		*mode = MODE_DIALOG;
		ui_set_page(ui, UI_PAGE_DIALOG);
		return true;
	}

	int npcIndex = facing_npc_index(p, m);
	if (npcIndex < 0)
	{
		log_add("面前没有可以互动的东西。");
		return false;
	}
	const Npc *placed = map_npc(npcIndex);
	int npcId = placed->npcId;

	s_talkNpc = npcId;
	s_talkPortrait = placed->tile;
	s_shopRequest = DLG_ACT_NONE;
	s_objectTalk = false;
	if (npcId >= 200)
	{
		object_interact(p, npcId, d, ui, mode, playSeconds);
		return true;
	}
	npc_menu(p, d);
	*mode = MODE_DIALOG;
	ui_set_page(ui, UI_PAGE_DIALOG);
	return true;
}

static void dialog_effect(int moneyDelta, const char *label)
{
	if (!s_player)
		return;
	if (!label)
		label = "这件事";

	int money = (int)s_player->money + moneyDelta;
	if (money < 0)
	{
		log_add("钱不够，「%s」没做成。", label);
		return;
	}
	s_player->money = (u32)money;
	if (moneyDelta != 0)
	{
		log_add("「%s」：文钱 %+d", label, moneyDelta);
		audio_play(SFX_COIN);
	}
	else
		log_add("你选择了「%s」。", label);
}

/* scroll.s ending1..3, verbatim (one screen line per string). */
static const char *const s_endLines1[] =
{
	"*尾声*",
	"我是谁：不要以为你真",
	"的打败了我，这只是剧",
	"情的安排．其实还有两",
	"个更精采的结......",
	"画外音：快下场！你已",
	"经说的够多了．",
	"    我走到六芒星阵中",
	"央，按下了时空转换的",
	"按钮．．．",
	""
};

static const char *const s_endLines2[] =
{
	"*尾声*",
	"东方求败：为了所谓的",
	"正义，可以不择手段吗",
	"？我只是做了自己想做",
	"的事，我错了吗？倒是",
	"你，一直生活在正义的",
	"阴影下，失去了自我，",
	"那才是可悲呢．",
	"    住口！你这样的人",
	"也配谈正义吗？",
	"东方求败：想杀人灭口",
	"吗？可惜我还不想死．",
	"再见了．",
	"    不知什么时候东方",
	"求败打开了时空转换装",
	"置．看着他渐渐变小的",
	"背影，我毫不犹豫地冲",
	"了上去．．．",
	""
};

static const char *const s_endLines3[] =
{
	"*尾声*",
	"道德和尚：你赢了．但",
	"是请施主记住，正义终",
	"将战胜邪恶．",
	"    只可惜你却看不到",
	"那一天了．我慢慢走近",
	"，一刀劈了下去．",
	"    就象砍进棉花里，",
	"没有任何声音，道德和",
	"尚在一瞬间消失了．难",
	"道说．．．",
	"    轰－！我惊异地发",
	"现，这一刀斩在了时空",
	"转换装置上．",
	"    周围的景物在迅速",
	"的变更，感觉如同掉进",
	"了无尽的深渊．．．",
	""
};

static const char *const *ending_lines(int boss, int *count)
{
	if (boss == 2)
	{
		*count = (int)(sizeof(s_endLines3) / sizeof(s_endLines3[0]));
		return s_endLines3;
	}
	if (boss == 1)
	{
		*count = (int)(sizeof(s_endLines2) / sizeof(s_endLines2[0]));
		return s_endLines2;
	}
	*count = (int)(sizeof(s_endLines1) / sizeof(s_endLines1[0]));
	return s_endLines1;
}

/* Last page start: the window shows five lines at a time. */
static int ending_max_start(int boss)
{
	int count;
	ending_lines(boss, &count);
	return count > 5 ? count - 5 : 0;
}

static void draw_ending(int boss)
{
	int count;
	const char *const *lines = ending_lines(boss, &count);
	if (s_endingPage > count - 1)
		s_endingPage = count - 1;
	if (s_endingPage < 0)
		s_endingPage = 0;

	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, ui_col(UI_COL_BG));
	C2D_DrawRectSolid(8, 8, 0.0f, 304, 2, ui_col(UI_COL_ACCENT));

	for (int i = 0; i < 5 && s_endingPage + i < count; i++)
	{
		const char *line = lines[s_endingPage + i];
		bool title = (i == 0 && strstr(line, "*尾声*") != NULL);
		int w = text_width(line, 1);
		text_draw(line, (320 - w) / 2, 55 + i * 28, 1,
		          title ? ui_col(UI_COL_ACCENT) : ui_col(UI_COL_TEXT));
	}

	ui_more(304, 218, ui_col(UI_COL_DIM));
}

/* rank.s rank: conditions copied from the original.
 *
 * man_daode is a raw byte where >=128 is 正派 (see GOODMAN=160/BADMAN=100 in
 * h/gmud.h plus task.s query_kill/query_ghost). The original chain is:
 *   rank.s:24-34 daode<BADMAN(100) && per>=32 && si_kill>=100 -> 邪恶天使
 *                       (rank5; the bcs on cmp #BADMAN skips this chain for
 *                        daode>=100, so a 正派 can NEVER reach it)
 *   ranking1 正派>=160 && guan_kill>=60 && per>=22 -> 盖世大侠
 *   ranking2 npc_kill==0 && guan_kill==0            -> 好好先生
 *   ranking3 bmi(daode>=128) -> ranking4: guan_kill>=64 -> 无情名捕
 *             else(daode<128)  si_kill>=108          -> 浪子杀手
 *   ranking5 npc_kill>=120                          -> 冷血屠夫
 *   ranking6 gender!=0 && per>=36 && age<30         -> 绝代佳人
 *   ranking7 game_hour<48                           -> 神行太保
 *   ranking8 top_dance>=300                         -> 舞林高手
 *   ranking9 top_ball>=300                          -> 灌篮高手
 *   ranking10                                        -> 普通菜鸟
 * top_dance / top_ball are the 山丘 mini game high scores (panyan.s). */
static const char *rank_title(const Player *p, u32 gameHour)
{
	u8 moral = actor_morality_raw(p);

	/* rank.s:24-33: the 邪恶天使 test comes first and is only reached when
	 * man_daode < BADMAN(100) (bcs ranking1 skips it for daode >= 100). */
	if (moral < 100 && p->per >= 32 && p->siKill >= 100)
		return "邪恶天使";
	/* ranking1 */
	if (moral >= 160 && p->guanKill >= 60 && p->per >= 22)
		return "盖世大侠";
	/* ranking2 */
	if (p->npcKill == 0 && p->guanKill == 0)
		return "好好先生";
	/* ranking3/ranking4: bmi(man_daode) means daode >= 128 (正派) jumps to
	 * ranking4 (guan_kill >= 64 -> 无情名捕); 邪派 falls through to
	 * si_kill >= 108 -> 浪子杀手. */
	if (moral >= 128)
	{
		if (p->guanKill >= 64)
			return "无情名捕";
	}
	else
	{
		if (p->siKill >= 108)
			return "浪子杀手";
	}
	/* ranking5 */
	if (p->npcKill >= 120)
		return "冷血屠夫";
	/* ranking6: lda man_gender; beq — female AND 人妖(2) both qualify. */
	if (p->gender != 0 && p->per >= 36 && p->age < 30)
		return "绝代佳人";
	/* ranking7: lda game_hour+1; bne (high byte must be 0) then cmp #48. */
	if (gameHour < 48u)
		return "神行太保";
	/* ranking8: top_dance >= 300 -> 舞林高手 (山丘的跳舞毯最高分). */
	if (minigame_top_dance() >= 300)
		return "舞林高手";
	/* ranking9: top_ball >= 300 -> 灌篮高手 (山丘的投铅球最高分). */
	if (minigame_top_ball() >= 300)
		return "灌篮高手";
	return "普通菜鸟";
}

static void draw_rank(const Player *p)
{
	char line[64];
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, ui_col(UI_COL_BG));
	C2D_DrawRectSolid(8, 8, 0.0f, 304, 2, ui_col(UI_COL_ACCENT));
	text_draw("等级评定", 8, 18, 1, ui_col(UI_COL_ACCENT));

	u16 doKill = actor_morality_raw(p) >= 128 ? p->guanKill : p->siKill;
	/* rank.s pingding prints game_hour:game_min (the RTC clock), not a
	 * 时辰 name and not mud_age. */
	u32 gameHour = actor_get_game_hour();
	u32 gameMin = actor_get_game_min();
	snprintf(line, sizeof(line), "时间      %u:%02u",
	         (unsigned)gameHour, (unsigned)gameMin);
	text_draw(line, 20, 48, 1, ui_col(UI_COL_TEXT));
	snprintf(line, sizeof(line), "杀NPC数   %u", (unsigned)p->npcKill);
	text_draw(line, 20, 78, 1, ui_col(UI_COL_TEXT));
	snprintf(line, sizeof(line), "追杀数    %u", (unsigned)doKill);
	text_draw(line, 20, 108, 1, ui_col(UI_COL_TEXT));
	snprintf(line, sizeof(line), "名声      %u", (unsigned)actor_morality_raw(p));
	text_draw(line, 20, 138, 1, ui_col(UI_COL_TEXT));
	snprintf(line, sizeof(line), "等级评定  %s", rank_title(p, gameHour));
	text_draw(line, 20, 168, 1, ui_col(UI_COL_ACCENT));

	ui_more(304, 218, ui_col(UI_COL_DIM));
}

/* rank.s end1/end2: a short epilogue after the rank box. */
static char s_epiWrapped[1400];
static int s_epiLineCount;
static int s_epiPage;

static void build_epilogue(const Player *p)
{
	u8 moral = actor_morality_raw(p);
	const char *text;
	if (moral >= 160 || moral < 100)
	{
		text = "    终于着陆了．\n"
		       "    眼前是一个嘈杂的市场，到处都是蓝眼睛白皮肤的外国人，而且装扮也很奇怪．\n"
		       "    “请问，这里是拍片现场吗？”\n"
		       "    “WHAT YOU SAY？”\n"
		       "    不会吧？竟然来到了中世纪的欧洲．\n"
		       "    MY GOD！";
	}
	else
	{
		text = "    “喂，喂！醒醒！太阳都晒到屁股了．再不起床就要迟到啦．”\n"
		       "    ？？？\n"
		       "    一阵忙乱之后．\n"
		       "    “有没有搞错？今天可是星期日啊！”\n"
		       "    ．．．．．．";
	}
	s_epiLineCount = text_wrap(text, 304, 1, s_epiWrapped,
	                           sizeof(s_epiWrapped), 32);
	if (s_epiLineCount < 0)
	{
		snprintf(s_epiWrapped, sizeof(s_epiWrapped), "%s", text);
		s_epiLineCount = 1;
	}
	s_epiPage = 0;
}

static int epilogue_page_count(void)
{
	return s_epiLineCount > 6 ? (s_epiLineCount + 5) / 6 : 1;
}

static void draw_epilogue(void)
{
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, ui_col(UI_COL_BG));
	C2D_DrawRectSolid(8, 8, 0.0f, 304, 2, ui_col(UI_COL_ACCENT));

	const char *p = s_epiWrapped;
	int line = 0, first = s_epiPage * 6;
	while (*p && line < first)
	{
		const char *nl = strchr(p, '\n');
		if (!nl)
			break;
		p = nl + 1;
		line++;
	}
	char buf[64];
	for (int i = 0; i < 6 && *p; i++)
	{
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len >= sizeof(buf))
			len = sizeof(buf) - 1;
		memcpy(buf, p, len);
		buf[len] = '\0';
		if (buf[0])
			text_draw(buf, 12, 40 + i * 28, 1, ui_col(UI_COL_TEXT));
		if (!nl)
			break;
		p = nl + 1;
	}

	ui_more(304, 218, ui_col(UI_COL_DIM));
}

static int s_gameOverReason;   /* 0 ending, 1 battle death, 2 suicide, 3 结束 */

static void gameover_button_rect(int index, int *x, int *y, int *w, int *h)
{
	*x = index == 0 ? 34 : 176;
	*y = 166;
	*w = 110;
	*h = 38;
}

static void draw_gameover(void)
{
	C2D_DrawRectSolid(0, 0, 0.0f, 320, 240, ui_col(UI_COL_BG));
	C2D_DrawRectSolid(8, 8, 0.0f, 304, 2, ui_col(UI_COL_ACCENT));

	const char *title = s_gameOverReason == 0 ? "全剧终"
	                  : s_gameOverReason == 2 ? "你自杀了"
	                  : s_gameOverReason == 3 ? "本局结束" : "你战败身亡";
	const char *sub = s_gameOverReason == 0 ? "感谢游玩白金英雄坛说"
	                : s_gameOverReason == 2 ? "您的资料已从存档中删除"
	                : s_gameOverReason == 3 ? "江湖路远，后会有期"
	                                        : "江湖梦断，一切从头再来";
	int w = text_width(title, 2);
	text_draw(title, (320 - w) / 2, 76, 2, ui_col(UI_COL_ACCENT));
	w = text_width(sub, 1);
	text_draw(sub, (320 - w) / 2, 124, 1, ui_col(UI_COL_TEXT));

	for (int i = 0; i < 2; i++)
	{
		int x, y, bw, bh;
		gameover_button_rect(i, &x, &y, &bw, &bh);
		ui_button(x, y, bw, bh, i == 0 ? "读取存档" : "新的旅程", UI_BTN_NORMAL);
	}

	int hw = text_width("START 退出游戏", 1);
	text_draw("START 退出游戏", (320 - hw) / 2, 216, 1, ui_col(UI_COL_DIM));
}

static void refresh_slots(void);

enum
{
	MENU_ACT_NONE = 0,
	MENU_ACT_CLOSE,
	MENU_ACT_LOOK,
	MENU_ACT_BAG,
	MENU_ACT_SKILLS,
	MENU_ACT_SYSTEM,
	MENU_ACT_SETTINGS
};

static bool point_in(int px, int py, int x, int y, int w, int h)
{
	return px >= x && py >= y && px < x + w && py < y + h;
}

/* game.s main_menu order: 查看 / 物品 / 技能 / 功能, plus the 3DS port's
 * own 设置 entry (theme / volume / save / quit). */
static int menu_action(int index)
{
	switch (index)
	{
	case 0: return MENU_ACT_LOOK;
	case 1: return MENU_ACT_BAG;
	case 2: return MENU_ACT_SKILLS;
	case 3: return MENU_ACT_SYSTEM;
	case 4: return MENU_ACT_SETTINGS;
	default: return MENU_ACT_NONE;
	}
}

static void apply_menu_action(int action, UiState *ui, int *mode)
{
	switch (action)
	{
	case MENU_ACT_CLOSE:
		*mode = MODE_FIELD;
		ui_set_page(ui, UI_PAGE_FIELD);
		break;
	case MENU_ACT_LOOK:
		ui->lookTab = UI_LOOK_STATUS;
		*mode = MODE_LOOK;
		ui_set_page(ui, UI_PAGE_LOOK);
		break;
	case MENU_ACT_BAG:
		ui->bagSel = -1;
		ui->bagAct = -1;
		ui->bagPage = 0;
		*mode = MODE_BAG;
		ui_set_page(ui, UI_PAGE_BAG);
		break;
	case MENU_ACT_SKILLS:
		ui->skillScroll = 0;
		ui->skillSel = 0;
		*mode = MODE_SKILLS;
		ui_set_page(ui, UI_PAGE_SKILLS);
		break;
	case MENU_ACT_SYSTEM:
		ui->systemSel = UI_SYSTEM_NEILI;
		*mode = MODE_SYSTEM;
		ui_set_page(ui, UI_PAGE_SYSTEM);
		break;
	case MENU_ACT_SETTINGS:
		ui->settingsSel = UI_SETTINGS_THEME;
		*mode = MODE_SETTINGS;
		ui_set_page(ui, UI_PAGE_SETTINGS);
		break;
	default:
		break;
	}
}

static void refresh_slots(void)
{
	for (int i = 0; i < SAVE_SLOT_COUNT; i++)
		save_slot_info(i, &s_slots[i]);
}

/* The slot 继续游戏 resumes from.  playSeconds is the length of a run, not a
 * timestamp, so the auto slot can not be ranked by it - but the auto slot is
 * always the run that was played last (it is written on every map change and
 * every 120 s), so it wins whenever it exists.  Without one, the manual slot
 * with the most progress is the best guess. */
static int newest_slot(void)
{
	if (s_slots[SAVE_SLOT_AUTO].exists && s_slots[SAVE_SLOT_AUTO].valid)
		return SAVE_SLOT_AUTO;

	int best = -1;
	u32 bestTime = 0;
	for (int i = 0; i < SAVE_SLOTS; i++)
	{
		if (!s_slots[i].exists || !s_slots[i].valid)
			continue;
		if (best < 0 || s_slots[i].playSeconds >= bestTime)
		{
			best = i;
			bestTime = s_slots[i].playSeconds;
		}
	}
	return best;
}

static const char *slot_name(int slot)
{
	static char buf[24];
	if (slot == SAVE_SLOT_AUTO)
		return "自动存档";
	snprintf(buf, sizeof(buf), "槽位 %d", slot + 1);
	return buf;
}

static bool do_save(int slot, const Player *p, u32 playSeconds)
{
	if (!save_write(slot, p, playSeconds))
	{
		log_add("保存失败：无法写入 SD 卡。");
		return false;
	}
	refresh_slots();
	s_currentSlot = slot;
	s_lastSaveTime = playSeconds;
	log_add("已保存到%s。", slot_name(slot));
	return true;
}

/* Auto-save into its own slot: it never touches the player's three manual
 * saves, so a crash, a flat battery or 结束 can cost at most the last couple
 * of minutes.  Silent on failure (a missing SD card already warned at boot). */
static void auto_save(const Player *p, u32 playSeconds, bool toast)
{
	/* Restart the 120 s clock even when the write fails, so a full SD card
	 * does not turn into one write attempt per frame. */
	s_autoSaveTimer = 0.0f;
	if (!s_canSave)
		return;
	if (!save_write(SAVE_SLOT_AUTO, p, playSeconds))
		return;
	save_slot_info(SAVE_SLOT_AUTO, &s_slots[SAVE_SLOT_AUTO]);
	/* Walking through doors saves silently (every room would otherwise pop a
	 * banner); the timed and reward checkpoints announce themselves.  The
	 * log is left alone so the story lines stay readable. */
	if (toast)
		ui_toast("自动存档");
}

static bool do_load(int slot, Player *p, u32 *playSeconds)
{
	u32 loaded = *playSeconds;
	/* Load into a scratch Player first: save_read() overwrites every field,
	 * so a rejected save (bad map index) must not leave the live sheet in a
	 * half-replaced state that this same frame would then crash on. */
	/* Zeroed first: save_read() overwrites every serialized field, but a
	 * few (walk pose, food caps for v3+ saves) are only meaningful fresh. */
	Player tmp;
	memset(&tmp, 0, sizeof(tmp));
	if (!save_read(slot, &tmp, &loaded))
	{
		log_add("读取失败：%s没有有效存档。", slot_name(slot));
		return false;
	}

	const GameMap *m = map_get(tmp.mapIndex);
	if (!m)
	{
		log_add("读取失败：存档里的地图无效。");
		return false;
	}

	int dir = tmp.dir;
	int tx = tmp.tx, ty = tmp.ty;
	*p = tmp;
	actor_init(p, m, p->mapIndex, tx, ty);
	p->dir = dir;
	/* maxFood/maxWater are not serialized; rederive them from the loaded
	 * attr_str instead of keeping the previous character's values. */
	actor_set_food_caps(p);
	*playSeconds = loaded;
	task_migrate(p, loaded);
	task_ghost_sync(p);
	s_currentSlot = slot;
	s_loadOnly = false;
	s_lastSaveTime = loaded;
	log_add("已读取%s。", slot_name(slot));
	return true;
}

static void on_dialog_action(int action, int option)
{
	s_shopRequest = action;
	s_shopOption = option;
}

/* ------------------------------------------------------------- shop helpers */

static void shop_buy(Player *p, int npcId, int index)
{
	const NpcInfo *n = npc_info(npcId);
	if (!n || index < 0 || index >= (int)n->vendorCount)
		return;

	int id = n->vendor[index] & 0x7F;
	int price = item_buy_price(id);
	if (p->money < (u32)price)
	{
		log_add("文钱不够，买不起「%s」。", item_name(id));
		return;
	}
	if (inv_add(p->bag, id) < 0)
	{
		log_add("行囊已满，装不下「%s」。", item_name(id));
		return;
	}
	p->money -= (u32)price;
	log_add("买了「%s」，文钱 -%d。", item_name(id), price);
	audio_play(SFX_COIN);
}

static int collect_sell_slots(const Player *p, int *slots, int max)
{
	int n = 0;
	for (int i = 0; i < BAG_SLOTS && n < max; i++)
	{
		int id = BAG_ID(p->bag[i]);
		/* goods.s set_all_goods: equipped goods are listed (sell_it then
		 * refuses them), 三角石板 is skipped entirely. */
		if (id == 0 || id == ITEM_SANJIAO)
			continue;
		slots[n++] = i;
	}
	return n;
}

static void shop_sell(Player *p, int slot)
{
	if (slot < 0 || slot >= BAG_SLOTS)
		return;
	if (BAG_EMPTY(p->bag[slot]))
		return;
	int id = BAG_ID(p->bag[slot]);
	if (BAG_EQUIPPED(p->bag[slot]))
	{
		log_add("先把「%s」卸下再卖。", item_name(id));
		return;
	}
	if (id == ITEM_SANJIAO)
		return;

	int price = item_sell_price(id);
	if (!inv_remove_slot(p->bag, slot, 1))
		return;
	p->money += (u32)price;
	log_add("卖了「%s」，文钱 +%d。", item_name(id), price);
	audio_play(SFX_COIN);
}

/* ---------------------------------------------- book study / quit dialogs */

static DialogNode s_bookNode;
static DialogScript s_bookScript;
static char s_bookText[128];
static const char *const BOOK_OPT_YES = "是";
static const char *const BOOK_OPT_NO = "否";

static const DialogScript *book_confirm_script(int step)
{
	memset(&s_bookNode, 0, sizeof(s_bookNode));
	snprintf(s_bookText, sizeof(s_bookText), "%s",
	         step == 1 ? "真的要修练吗(Y/N)?" : "不后悔吗(Y/N)?");
	s_bookNode.text = s_bookText;
	s_bookNode.after = -1;
	s_bookNode.optionCount = 2;
	s_bookNode.options[0] = BOOK_OPT_YES;
	s_bookNode.options[1] = BOOK_OPT_NO;
	s_bookNode.optAction[0] = step == 1 ? DLG_ACT_BOOK_YES1 : DLG_ACT_BOOK_YES2;
	s_bookNode.optAction[1] = DLG_ACT_BOOK_NO;
	s_bookNode.next[0] = s_bookNode.next[1] = -1;
	s_bookScript.nodes = &s_bookNode;
	s_bookScript.count = 1;
	return &s_bookScript;
}

static DialogNode s_quitNode;
static DialogScript s_quitScript;
static const char *const QUIT_SAVE_OPT_YES = "现在存档";
static const char *const QUIT_SAVE_OPT_NO = "直接结束";

static const DialogScript *quit_save_script(void)
{
	memset(&s_quitNode, 0, sizeof(s_quitNode));
	s_quitNode.text = "你已经好久没存档了,现在保存吗?";
	s_quitNode.after = -1;
	s_quitNode.optionCount = 2;
	s_quitNode.options[0] = QUIT_SAVE_OPT_YES;
	s_quitNode.options[1] = QUIT_SAVE_OPT_NO;
	s_quitNode.optAction[0] = DLG_ACT_QUIT_SAVE;
	s_quitNode.optAction[1] = DLG_ACT_QUIT_NOSAVE;
	s_quitNode.next[0] = s_quitNode.next[1] = -1;
	s_quitScript.nodes = &s_quitNode;
	s_quitScript.count = 1;
	return &s_quitScript;
}

/* Training can now be opened from the bag, from a fight or from an NPC's
 * 请教 menu. Restore the caller's page instead of always dropping back to
 * the field: the original pops one menu level per B press, so a 功能
 * sub-page returns to 功能 and a nested one to its own parent. */
static void train_leave(UiState *ui, int *mode, Dialog *d)
{
	int back = s_trainReturnMode;
	*mode = back;
	switch (back)
	{
	case MODE_BAG:    ui_set_page(ui, UI_PAGE_BAG); break;
	case MODE_BATTLE: ui_set_page(ui, UI_PAGE_BATTLE); break;
	case MODE_SYSTEM: ui_set_page(ui, UI_PAGE_SYSTEM); break;
	case MODE_NEILI:  ui_set_page(ui, UI_PAGE_NEILI); break;
	case MODE_DIALOG:
		/* npc.s master: B pops the menu stack back to the NPC's own
		 * 交谈 / 查看 / 战斗 / 请教 menu. Clear the pending 请教 action
		 * first, otherwise closing that menu would start 学艺 again. */
		if (s_trainReturnNpc && d && s_player)
		{
			s_shopRequest = DLG_ACT_NONE;
			s_objectTalk = false;
			npc_menu(s_player, d);
		}
		ui_set_page(ui, UI_PAGE_DIALOG);
		break;
	default:          ui_set_page(ui, UI_PAGE_FIELD); break;
	}
	s_trainReturnMode = MODE_FIELD;
	s_trainReturnNpc = false;
}

/* skill.s study_cmd: reading a book makes the player 逍遥派 and opens the
 * same master_menu / learn loop used by 请教, with the book's level caps. */
static void book_start(Player *p, UiState *ui, int *mode, int itemId)
{
	p->pai = 7;
	s_trainReturnNpc = false;
	TrainUi *t = &ui->train;
	memset(t, 0, sizeof(*t));
	t->kind = TRAIN_KIND_BOOK;
	t->bookId = itemId;
	t->masterNpc = -1;
	t->phase = TRAIN_PHASE_SELECT;
	int count = book_skill_count(itemId);
	for (int i = 0; i < count && i < TRAIN_LIST_MAX; i++)
	{
		t->list[i] = book_skill_id(itemId, i);
		t->listLv[i] = book_skill_level(itemId, i);
	}
	t->listCount = count > TRAIN_LIST_MAX ? TRAIN_LIST_MAX : count;
	log_add("你翻开「%s」，开始研读。", item_name(itemId));
	*mode = MODE_TRAIN;
	ui_set_page(ui, UI_PAGE_TRAIN);
}

/* skill.s study_cmd runs the 文盲/门派 checks only AFTER the 菜花宝典
 * Y/N pair, so a player can become 人妖 before being refused for reading. */
static void book_open(Player *p, UiState *ui, int *mode, int itemId)
{
	const char *err = book_read_error(p, itemId);
	if (err)
	{
		log_add("%s", err);
		return;
	}
	book_start(p, ui, mode, itemId);
}

static void book_request(UiState *ui, Player *p, Dialog *d, int *mode,
                         int itemId, int returnMode)
{
	if (book_skill_count(itemId) <= 0)
		return;
	s_trainReturnMode = returnMode;
	s_pendingBook = itemId;
	if (!book_needs_gender(p, itemId))
	{
		book_open(p, ui, mode, itemId);
		s_pendingBook = 0;
		return;
	}
	/* 菜花宝典: two Y/N confirmations FIRST; both yes -> man_gender = 2. */
	s_shopRequest = DLG_ACT_NONE;
	s_npcDetail = false;
	s_objectTalk = false;
	dialog_start(d, book_confirm_script(1));
	*mode = MODE_DIALOG;
	ui_set_page(ui, UI_PAGE_DIALOG);
}

/* ----------------------------------------------------------- bag actions */

/* The sheet's default button: gear toggles its equip state, everything else
 * is used.  Keyboard navigation starts here, Left / Right walk from it. */
static int bag_default_action(const Player *p, int slot)
{
	const ItemDef *d = item_get(BAG_ID(p->bag[slot]));
	if (d && (d->type == ITEM_WEAPON || d->type == ITEM_EQUIP))
		return UI_BAG_ACT_EQUIP;
	return UI_BAG_ACT_USE;
}

/* Disabled buttons stay in the row but are skipped by Left / Right. */
static bool bag_action_enabled(const ItemDef *d, int act)
{
	bool gear = d && (d->type == ITEM_WEAPON || d->type == ITEM_EQUIP);
	if (act == UI_BAG_ACT_USE)
		return !gear;
	if (act == UI_BAG_ACT_EQUIP)
		return gear;
	return true;
}

/* Runs one detail-sheet button; shared by taps and keys. */
static void bag_action(UiState *ui, Player *p, Dialog *d, int *mode, int act)
{
	if (!ui || ui->bagSel < 0)
		return;

	int id = BAG_ID(p->bag[ui->bagSel]);
	const ItemDef *def = item_get(id);
	if (act == UI_BAG_ACT_USE)
	{
		if (def && def->type == ITEM_BOOK)
			book_request(ui, p, d, mode, id, MODE_BAG);
		else if (def && (def->type == ITEM_WEAPON || def->type == ITEM_EQUIP))
			actor_equip_item(p, ui->bagSel);
		else
			actor_use_item(p, ui->bagSel);
	}
	else if (act == UI_BAG_ACT_EQUIP)
		actor_equip_item(p, ui->bagSel);
	else if (act == UI_BAG_ACT_DROP)
		actor_drop_item(p, ui->bagSel);
	else
	{
		ui->bagSel = -1;
		ui->bagAct = -1;
	}

	/* Using or dropping the last one closes the sheet instead of leaving it
	 * pointing at an empty slot. */
	if (ui->bagSel >= 0 && BAG_EMPTY(p->bag[ui->bagSel]))
	{
		ui->bagSel = -1;
		ui->bagAct = -1;
	}
}

/* ----------------------------------------------------------- touch handlers */

static void bag_click(UiState *ui, Player *p, Dialog *d, int *mode, int px, int py)
{
	int slots[BAG_SLOTS];
	int count = inv_list_slots(p->bag, slots, BAG_SLOTS);
	int pages = (count + UI_BAG_ROWS - 1) / UI_BAG_ROWS;
	int page = ui->bagPage;

	if (pages < 1)
		pages = 1;
	if (page < 0)
		page = 0;
	if (page >= pages)
		page = pages - 1;

	if (ui->bagSel >= 0)
	{
		for (int i = 0; i < UI_BAG_ACT_COUNT; i++)
		{
			int x, y, w, h;
			ui_bag_action_rect(i, &x, &y, &w, &h);
			if (point_in(px, py, x, y, w, h))
			{
				ui->bagAct = i;
				bag_action(ui, p, d, mode, i);
				return;
			}
		}

		/* The open sheet sits below the list and covers the page buttons:
		 * taps on its body belong to the sheet, not to the pager below it. */
		int sx, sy, sw, sh;
		ui_bag_sheet_rect(&sx, &sy, &sw, &sh);
		if (point_in(px, py, sx, sy, sw, sh))
			return;
	}
	else if (pages > 1)
	{
		int x, y, w, h;
		ui_bag_page_rect(0, &x, &y, &w, &h);
		if (point_in(px, py, x, y, w, h))
		{
			ui->bagPage = page > 0 ? page - 1 : 0;
			return;
		}
		ui_bag_page_rect(1, &x, &y, &w, &h);
		if (point_in(px, py, x, y, w, h))
		{
			ui->bagPage = page + 1 < pages ? page + 1 : page;
			return;
		}
	}

	for (int i = 0; i < UI_BAG_ROWS; i++)
	{
		int index = page * UI_BAG_ROWS + i;
		if (index >= count)
			break;
		int x, y, w, h;
		ui_bag_row_rect(i, &x, &y, &w, &h);
		if (point_in(px, py, x, y, w, h))
		{
			ui->bagSel = slots[index];
			ui->bagAct = bag_default_action(p, ui->bagSel);
			return;
		}
	}
	ui->bagSel = -1;
	ui->bagAct = -1;
}

static void shop_click(UiState *ui, Player *p, int px, int py)
{
	for (int i = 0; i < UI_SHOP_BTN_COUNT; i++)
	{
		int x, y, w, h;
		ui_shop_button_rect(i, &x, &y, &w, &h);
		if (point_in(px, py, x, y, w, h))
		{
			if (i == UI_SHOP_BTN_BACK)
				return; /* handled by the caller */
			if (i == UI_SHOP_BTN_PREV && ui->shopPage > 0)
				ui->shopPage--;
			if (i == UI_SHOP_BTN_NEXT)
				ui->shopPage++;
			return;
		}
	}

	int slots[BAG_SLOTS];
	int rowCount;
	if (ui->shopTab == 0)
	{
		const NpcInfo *n = npc_info(ui->shopNpc);
		rowCount = n ? (int)n->vendorCount : 0;
	}
	else
	{
		rowCount = collect_sell_slots(p, slots, BAG_SLOTS);
	}

	for (int i = 0; i < UI_SHOP_ROWS; i++)
	{
		int x, y, w, h;
		ui_shop_row_rect(i, &x, &y, &w, &h);
		if (!point_in(px, py, x, y, w, h))
			continue;

		int index = ui->shopPage * UI_SHOP_ROWS + i;
		if (index >= rowCount)
			return;
		/* Taps move the cursor too, so a key press continues from the row
		 * the player just touched. */
		ui->shopSel = index;
		if (ui->shopTab == 0)
			shop_buy(p, ui->shopNpc, index);
		else
			shop_sell(p, slots[index]);
		return;
	}
}

/* ------------------------------------------------------- training (M5) */

static void train_begin_run(TrainUi *t)
{
	t->phase = TRAIN_PHASE_RUN;
	t->running = true;
	t->acc = 0.0f;
	t->rounds = 0;
	t->msg[0] = '\0';
}

static bool train_open_practice(UiState *ui, Player *p, int *mode)
{
	/* 功能 → 练功: 返回 goes back to 功能, like the original popping one
	 * menu level. */
	s_trainReturnMode = MODE_SYSTEM;
	s_trainReturnNpc = false;
	TrainUi *t = &ui->train;
	memset(t, 0, sizeof(*t));
	t->kind = TRAIN_KIND_PRACTICE;
	t->phase = TRAIN_PHASE_SELECT;
	/* skill.s get_pra_kf: only the ENABLED hand/weapon/dodge slots. */
	for (int type = SKILL_TYPE_HAND; type <= SKILL_TYPE_DODGE; type++)
	{
		int id = skill_usekf_id(p, type);
		if (id > 0 && t->listCount < TRAIN_LIST_MAX)
			t->list[t->listCount++] = id;
	}
	if (t->listCount == 0)
	{
		log_add("先在技能页启用要练的功夫。");
		return false;
	}
	*mode = MODE_TRAIN;
	ui_set_page(ui, UI_PAGE_TRAIN);
	return true;
}

static bool train_open_dazuo(UiState *ui, Player *p, int *mode)
{
	/* 功能 → 内力 → 打坐: 返回 goes back to the 内力 page. */
	s_trainReturnMode = MODE_NEILI;
	s_trainReturnNpc = false;
	/* lee1.s dazuo: requires an enabled force kungfu. */
	int forceId = skill_usekf_id(p, SKILL_TYPE_FORCE);
	if (forceId <= 0)
	{
		log_add("必须先在技能页选择要用的内功心法。");
		return false;
	}
	TrainUi *t = &ui->train;
	memset(t, 0, sizeof(*t));
	t->kind = TRAIN_KIND_DAZUO;
	t->kf = forceId;
	train_begin_run(t);
	*mode = MODE_TRAIN;
	ui_set_page(ui, UI_PAGE_TRAIN);
	return true;
}

static bool train_open_learn(UiState *ui, Player *p, int *mode, int masterNpc)
{
	/* NPC 请教 → 学艺: 返回 re-opens that NPC's menu (npc.s master). */
	s_trainReturnMode = MODE_DIALOG;
	s_trainReturnNpc = true;
	if (masterNpc == 7 && p->exp < 200000)
	{
		log_add("去去去,攒够经验再来吧!");
		return false;
	}
	const NpcInfo *n = npc_info(masterNpc);
	TrainUi *t = &ui->train;
	memset(t, 0, sizeof(*t));
	t->kind = TRAIN_KIND_LEARN;
	t->masterNpc = masterNpc;
	t->phase = TRAIN_PHASE_SELECT;
	for (int i = 0; n && i < n->kfCount && i < NPC_KF_MAX; i++)
	{
		int id = n->kf[i * 2];
		int lv = n->kf[i * 2 + 1];
		if (lv <= 0)
			continue;
		if (t->listCount >= TRAIN_LIST_MAX)
			break;
		t->list[t->listCount] = id;
		t->listLv[t->listCount] = lv;
		t->listCount++;
	}
	if (t->listCount == 0)
	{
		log_add("%s没有什么可以教你的。", n ? n->name : "师父");
		return false;
	}
	(void)p;
	*mode = MODE_TRAIN;
	ui_set_page(ui, UI_PAGE_TRAIN);
	return true;
}

/* One show_process round. 练功/打坐 use 400 ms, 学艺/读书 use 4 ms
 * (npc.s learn_tbl). Returns true while it can continue. */
static bool train_step(Player *p, UiState *ui)
{
	TrainUi *t = &ui->train;
	char msg[96];
	bool cont;
	bool learning = (t->kind == TRAIN_KIND_LEARN || t->kind == TRAIN_KIND_BOOK);
	int before = learning ? skill_level(p, t->kf) : 0;
	if (t->kind == TRAIN_KIND_PRACTICE)
		cont = train_practice_tick(p, t->kf, msg, sizeof(msg));
	else if (t->kind == TRAIN_KIND_DAZUO)
		cont = train_dazuo_tick(p, msg, sizeof(msg));
	else if (t->kind == TRAIN_KIND_LEARN)
		cont = train_learn_tick(p, t->kf, t->masterNpc, msg, sizeof(msg));
	else
		cont = train_learn_tick_level(p, t->kf,
		                              book_kf_level(t->bookId, t->kf),
		                              msg, sizeof(msg));
	/* Practice/learn/dazuo can raise 红莲教义 or maxfp: keep maxhp in sync. */
	actor_setup_maxhp(p);
	snprintf(t->msg, sizeof(t->msg), "%s", msg);
	/* pyh.s:270-283 asks "继续学习吗?" after every level-up. Pause the
	 * loop so the player must press A to resume. */
	if (cont && learning && skill_level(p, t->kf) > before)
	{
		snprintf(t->msg, sizeof(t->msg), "%s  （继续学习吗？A 继续）", msg);
		t->running = false;
	}
	return cont;
}

static void draw_error(const char *msg, const char *hint)
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TargetClear(gfx_top_target(), C2D_RGBA(0x20, 0, 0));
	C2D_SceneBegin(gfx_top_target());
	gfx_text_begin();
	gfx_text("GMUD3DS: startup failed", 16.0f, 60.0f, 0.7f, C2D_RGBA(0xFF, 0xE0, 0xE0));
	gfx_text(msg, 16.0f, 96.0f, 0.6f, C2D_RGBA(0xFF, 0xFF, 0xFF));
	gfx_text(hint, 16.0f, 140.0f, 0.45f, C2D_RGBA(0xC0, 0xC0, 0xC0));
	gfx_text("press START to quit", 16.0f, 196.0f, 0.5f, C2D_RGBA(0xFF, 0xFF, 0xFF));

	C2D_TargetClear(gfx_bottom_target(), C2D_RGBA(0x20, 0, 0));
	C2D_SceneBegin(gfx_bottom_target());
	C2D_Flush();
	C3D_FrameEnd(0);
}

static int fatal(const char *msg, const char *hint)
{
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;
		draw_error(msg, hint);
	}
	return 1;
}

/* input.s input_name: the original hands the terminal to the WQX system
 * input method. The 3DS equivalent is the software keyboard. It has no
 * Chinese IME of its own, so hanzi can only be entered when the console's
 * keyboard provides them; the 16x16 font covers GB2312 level-1 anyway.
 * 密码（原版用于文曲星文件登录）按口径不做。 */
static void charcreate_name_input(CharCreate *cc)
{
	SwkbdState swkbd;
	char buf[64];

	/* input.s name_input_tbl: MAX_NAME = 8 bytes -> 8 ASCII / 4 hanzi;
	 * cc_set_name applies the same cap in UTF-8 bytes. */
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, CC_NAME_MAX_CHARS);
	swkbdSetHintText(&swkbd, "姓名（留空为无名氏）");
	if (strcmp(cc->name, CC_NAME_DEFAULT) != 0)
		swkbdSetInitialText(&swkbd, cc->name);
	swkbdSetValidation(&swkbd, SWKBD_NOTBLANK, 0, 0);
	swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "取消", false);
	swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "确定", true);

	if (swkbdInputText(&swkbd, buf, sizeof(buf)) == SWKBD_BUTTON_RIGHT)
	{
		cc_set_name(cc, buf);
		audio_play(SFX_CONFIRM);
	}
	else
	{
		audio_play(SFX_CANCEL);
	}
}

/* Everything drawn on the top screen for the current mode; called once per
 * eye while stereoscopic 3D is active (gfx_3d_begin_eye picks the offset). */
static void draw_top_screen(int mode, const GameMap *m, int camX, int camY,
                            const Player *p, u32 playSeconds,
                            const UiState *ui)
{
	if (mode == MODE_TITLE)
		ui_top_title_draw();
	else if (mode == MODE_CHARCREATE)
		ui_top_charcreate_draw(&ui->cc);
	else if (mode == MODE_BATTLE && s_battle.active)
		battle_draw_top(&s_battle, p, playSeconds);
	else
		ui_top_draw(m, camX, camY, p, playSeconds);
	if (mode != MODE_TITLE && mode != MODE_CHARCREATE)
		ui_top_fade(s_transition);
}

int main(void)
{
	/* game.s:74 clear_all wipes mud_seed. The original keeps one global
	 * PRNG state for the whole session and never reseeds per fight. */
	rng_init();
	romfsInit();

	/* DSP backend for the two effects (see audio.c).  Fails harmlessly when
	 * sdmc:/3ds/dspfirm.cdc is missing: the game then runs silently. */
	audio_init();

	if (!gfx_init())
	{
		audio_fini();
		romfsExit();
		return 1;
	}

	if (!map_pack_load("romfs:/maps.bin") || !gfx_load_tiles("romfs:/tiles.bin"))
	{
		int rc = fatal("cannot read romfs:/maps.bin or tiles.bin",
		               "run: python tools/extract.py --src ref/gmud/src --out data");
		map_pack_free();
		audio_fini();
		gfx_fini();
		romfsExit();
		return rc;
	}

	if (!gfx_load_player("romfs:/player.bin"))
	{
		int rc = fatal("cannot read romfs:/player.bin",
		               "run: python tools/extract.py --src ref/gmud/src --out data");
		map_pack_free();
		audio_fini();
		gfx_fini();
		romfsExit();
		return rc;
	}

	if (!fight_text_load("romfs:/fighttext.bin"))
	{
		int rc = fatal("cannot read romfs:/fighttext.bin",
		               "run: python tools/extract.py --src ref/gmud/src --out data");
		map_pack_free();
		audio_fini();
		gfx_fini();
		romfsExit();
		return rc;
	}

	if (!item_pack_load("romfs:/items.bin") || !npc_pack_load("romfs:/npcs.bin") ||
	    !skill_pack_load("romfs:/skills.bin"))
	{
		int rc = fatal("cannot read romfs:/items.bin, npcs.bin or skills.bin",
		               "run: python tools/extract.py --src ref/gmud/src --out data");
		skill_pack_free();
		npc_pack_free();
		item_pack_free();
		map_pack_free();
		audio_fini();
		gfx_fini();
		romfsExit();
		return rc;
	}

	if (!text_init("romfs:/font.bin"))
	{
		int rc = fatal("cannot read romfs:/font.bin",
		               "run: python tools/font_build.py --hex <unifont.hex.gz> --out data/font.bin");
		skill_pack_free();
		npc_pack_free();
		item_pack_free();
		map_pack_free();
		audio_fini();
		gfx_fini();
		romfsExit();
		return rc;
	}

	int startMap = map_start_map();
	const GameMap *start = map_get(startMap);
	if (!start)
	{
		fatal("map pack is empty", "");
		text_fini();
		map_pack_free();
		audio_fini();
		gfx_fini();
		romfsExit();
		return 1;
	}

	Player player;
	memset(&player, 0, sizeof(player));
	/* Baseline only: while the creation page is open the map still needs a
	 * valid Player. The real sheet is applied by actor_stats_init(&r) below. */
	actor_stats_init(&player, NULL);
	actor_init(&player, start, startMap, map_start_x(), map_start_y());
	s_player = &player;
	map_set_player(&player);

	log_init();
	s_canSave = save_init();
	if (!s_canSave)
		log_add("无法初始化 SD 卡，存档不可用。");
	else
	{
		refresh_slots();
		/* Theme + volumes are shell settings, stored outside the save
		 * slots so neither a new run nor a load can reset them. */
		settings_load();
	}

	dialog_set_effect(dialog_effect);
	dialog_set_action(on_dialog_action);
	Dialog dialog;
	memset(&dialog, 0, sizeof(dialog));

	UiState ui;
	ui_init(&ui);
	cc_init(&ui.cc);

	/* game.s create_game runs input_new before enter_gmud: a new run always
	 * starts on the original four-stat adjustment screen.  The 3DS port
	 * adds a title screen in front of it so an existing run can be resumed. */
	int mode = MODE_TITLE;
	ui_set_page(&ui, UI_PAGE_TITLE);
	ui.hasContinue = (newest_slot() >= 0);
	int camX = 0, camY = 0;
	int lastMapIndex = player.mapIndex;
	const GameMap *m = map_get(player.mapIndex);
	camera_target(m, &player, &camX, &camY);

	u32 playSeconds = 0;
	float secondAcc = 0.0f;
	float tickAcc = 0.0f;

	u64 last = osGetTime();

	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown();
		u32 held = hidKeysHeld();

		u64 now = osGetTime();
		float dt = (float)(now - last) / 1000.0f;
		last = now;
		if (dt < 0.0f) dt = 0.0f;
		if (dt > 0.1f) dt = 0.1f;

		/* Menu navigation gets the first press from `down` plus held-key
		 * repeats; walking keeps using `held` directly. */
		bool navRepeat = (mode != MODE_FIELD && mode != MODE_BATTLE &&
		                  mode != MODE_MINIGAME);
		u32 nav = down | nav_repeat(down, held, dt, navRepeat);

		/* system.s game_hour is fed by the RTC, not by mud_age. The clock
		 * lives in zero_area: a load restores it (save_read), a new game
		 * clears it. sys_refresh: game_sec wraps at 60 -> game_min++,
		 * at 60 -> game_hour++. */
		actor_advance_game_clock((u64)(dt * 1000.0f));

		/* Music requests follow the screen and stay harmless while the audio
		 * backend is a no-op; a real backend would cross-fade in
		 * audio_update(), so both can be called every frame. */
		audio_update(dt);
		audio_bgm(bgm_for_mode(mode));

		circlePosition circle;
		hidCircleRead(&circle);

		touchPosition touch;
		bool touched = false;
		if (down & KEY_TOUCH)
		{
			hidTouchRead(&touch);
			touched = true;
		}

		m = map_get(player.mapIndex);

		/* system.s sys_refresh is driven by wait_key in EVERY mode: mud_age
		 * always advances. busy_flag (战斗/学习/练功/打坐) only skips the
		 * to_restore block, which owns timetick and hunger/water/regen.
		 * Menus, dialogs, shops and the look pages are NOT busy. */
		secondAcc += dt;
		/* The title screen is not part of a run: the clock must not run
		 * before the player has even created a character. */
		if (mode != MODE_TITLE)
		{
			while (secondAcc >= 1.0f)
			{
				secondAcc -= 1.0f;
				playSeconds++;
				actor_sync_time(&player, playSeconds);
			}
		}
		else
		{
			secondAcc = 0.0f;
		}
		bool busy = (mode == MODE_BATTLE || mode == MODE_TRAIN);
		if (!busy)
		{
			tickAcc += dt;
			while (tickAcc >= ACTOR_TICK_SECONDS)
			{
				tickAcc -= ACTOR_TICK_SECONDS;
				actor_regen_tick(&player);
			}
		}

		if (mode == MODE_TITLE)
		{
			/* Boot menu.  It only adds a way back into an existing run;
			 * the new-game path still goes through the original creation
			 * page, unchanged. */
			if (nav & KEY_UP)
			{
				ui.titleSel = (ui.titleSel + UI_TITLE_COUNT - 1) % UI_TITLE_COUNT;
				audio_play(SFX_CURSOR);
			}
			if (nav & KEY_DOWN)
			{
				ui.titleSel = (ui.titleSel + 1) % UI_TITLE_COUNT;
				audio_play(SFX_CURSOR);
			}

			int act = -1;
			if (down & KEY_A)
				act = ui.titleSel;
			if (touched)
			{
				for (int i = 0; i < UI_TITLE_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_title_row_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						ui.titleSel = i;
						act = i;
						break;
					}
				}
			}

			if (act >= 0)
			{
				if (act == UI_TITLE_CONTINUE)
				{
					int slot = newest_slot();
					if (slot >= 0 && do_load(slot, &player, &playSeconds))
					{
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
						s_transition = 1.0f;
						lastMapIndex = player.mapIndex;
						audio_play(SFX_CONFIRM);
					}
					else
					{
						audio_play(SFX_ERROR);
						log_add("没有可用的存档。");
					}
				}
				else if (act == UI_TITLE_NEW)
				{
					/* game.s create_game runs input_new before enter_gmud:
					 * a new run always starts on the four-stat page. */
					cc_init(&ui.cc);
					mode = MODE_CHARCREATE;
					ui_set_page(&ui, UI_PAGE_CHARCREATE);
					audio_play(SFX_CONFIRM);
				}
				else if (act == UI_TITLE_LOAD)
				{
					refresh_slots();
					s_loadOnly = true;
					s_saveFromTitle = true;
					ui.saveSlot = 0;
					mode = MODE_SAVE;
					ui_set_page(&ui, UI_PAGE_SAVE);
					audio_play(SFX_CONFIRM);
				}
				else
				{
					audio_play(SFX_CANCEL);
					break;   /* leave the game */
				}
			}
		}
		else if (mode == MODE_FIELD)
		{
			actor_update(&player, m, dt, held, &circle);
			if (player.justArrived)
			{
				audio_play(SFX_STEP);
				/* try_door swaps the map in place; re-fetch so the same
				 * frame's A press / touch reads the new room. */
				if (try_door(&player))
				{
					m = map_get(player.mapIndex);
					/* Entering a room is a natural checkpoint. */
					auto_save(&player, playSeconds, false);
				}
			}

			/* The 120 s checkpoint only runs while the player is walking
			 * around, not while a menu is open. */
			s_autoSaveTimer += dt;
			if (s_autoSaveTimer >= 120.0f)
				auto_save(&player, playSeconds, true);

			if (down & KEY_A)
				start_interact(&player, m, &dialog, &ui, &mode, &playSeconds);

			bool openMenu = (down & (KEY_X | KEY_START)) != 0;
			if (touched)
			{
				for (int i = 0; i < UI_FIELD_BTN_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_field_button_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						if (i == UI_FIELD_BTN_INTERACT)
							start_interact(&player, m, &dialog, &ui, &mode, &playSeconds);
						else if (i == UI_FIELD_BTN_MENU)
							openMenu = true;
						break;
					}
				}
			}
			if (openMenu && mode == MODE_FIELD)
			{
				mode = MODE_MENU;
				ui.menuIndex = 0;
				ui_set_page(&ui, UI_PAGE_MENU);
			}
		}
		else if (mode == MODE_DIALOG)
		{
			dialog_update(&dialog, dt);

			if (touched)
			{
				bool consumed = false;
				int options = dialog_option_count(&dialog);
				if (options > 0)
				{
					int opt = dialog_option_at(&dialog, DIALOG_PANEL_X, DIALOG_PANEL_Y,
					                           DIALOG_PANEL_W, DIALOG_PANEL_H,
					                           (int)touch.px, (int)touch.py);
					if (opt >= 0)
					{
						dialog.selected = opt;
						dialog_input(&dialog, KEY_A);
						consumed = true;
					}
				}
				else
				{
					dialog_input(&dialog, KEY_A);
					consumed = true;
				}
				/* The touch already consumed an A press; strip a same-frame
				 * physical A so the advanced node is not advanced again. */
				if (consumed)
				{
					down &= ~KEY_A;
					nav &= ~KEY_A;
				}
			}
			/* nav adds held-direction repeats, so a long option list can be
			 * scrolled without tapping once per row. */
			dialog_input(&dialog, nav & ~KEY_TOUCH);

			if (dialog.done)
			{
				if (s_shopRequest == DLG_ACT_TALK || s_shopRequest == DLG_ACT_LOOK)
				{
					npc_detail(&player, &dialog, s_shopRequest == DLG_ACT_LOOK, playSeconds);
				}
				else if (s_shopRequest == DLG_ACT_BOOK_YES1)
				{
					dialog_start(&dialog, book_confirm_script(2));
				}
				else if (s_shopRequest == DLG_ACT_BOOK_YES2)
				{
					/* skill.s:91: both Y/N answered yes -> man_gender = 2,
					 * then the 文盲/门派 checks run (study_1). */
					player.gender = 2;
					book_open(&player, &ui, &mode, s_pendingBook);
					s_pendingBook = 0;
				}
				else if (s_shopRequest == DLG_ACT_BOOK_NO)
				{
					s_pendingBook = 0;
					train_leave(&ui, &mode, &dialog);
				}
				else if (s_shopRequest == DLG_ACT_QUIT_SAVE)
				{
					s_quitPrompt = false;
					s_saveThenQuit = true;
					refresh_slots();
					ui.saveSlot = 0;
					mode = MODE_SAVE;
					ui_set_page(&ui, UI_PAGE_SAVE);
				}
				else if (s_shopRequest == DLG_ACT_QUIT_NOSAVE)
				{
					s_quitPrompt = false;
					s_gameOverReason = 3;
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				/* dialog_input's KEY_B ends an option node without firing an
				 * optAction, so treat that as decline/直接结束. */
				else if (s_pendingBook && s_shopRequest == DLG_ACT_NONE)
				{
					s_pendingBook = 0;
					train_leave(&ui, &mode, &dialog);
				}
				else if (s_quitPrompt && s_shopRequest == DLG_ACT_NONE)
				{
					s_quitPrompt = false;
					s_gameOverReason = 3;
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else if (s_npcDetail && s_shopRequest == DLG_ACT_NONE)
				{
					/* The original returns to the map after a chat; the NPC
					 * menu is only opened by pressing A again. */
					s_npcDetail = false;
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else if (s_shopRequest == DLG_ACT_SHOP_BUY || s_shopRequest == DLG_ACT_SHOP_SELL)
				{
					const NpcInfo *info = npc_info(s_talkNpc);
					ui.shopNpc = s_talkNpc;
					ui.shopTab = (s_shopRequest == DLG_ACT_SHOP_SELL ||
					              !info || info->vendorCount == 0) ? 1 : 0;
					/* npc.s dealer: buy_goods / sell_goods greeting. */
					log_add(ui.shopTab == 0 ? "要买什么你自己看看吧!"
					                        : "有什么不用的东西就拿来吧!");
					ui.shopPage = 0;
					ui.shopSel = 0;
					mode = MODE_SHOP;
					ui_set_page(&ui, UI_PAGE_SHOP);
				}
				else if (s_shopRequest == DLG_ACT_FIGHT &&
				         battle_start(&s_battle, &player, s_talkNpc, s_talkPortrait))
				{
					mode = MODE_BATTLE;
					ui_set_page(&ui, UI_PAGE_BATTLE);
					audio_play(SFX_ENCOUNTER);
				}
				else if (s_shopRequest == DLG_ACT_ARENA)
				{
					/* 擂台: nf.s netfight is a link-cable duel; here it is a
					 * sparring match, so the opponent is never killed. */
					int idx = s_shopOption;
					if (idx < 0 || idx >= 3)
						idx = 0;
					int npcId = ARENA_OPPONENTS[idx];
					const NpcInfo *n = npc_info(npcId);
					if (n && battle_start(&s_battle, &player, npcId,
					                      npc_portrait_tile(npcId)))
					{
						s_battle.sparring = true;
						log_add("擂台赛开始，对手是「%s」。", n->name);
						mode = MODE_BATTLE;
						ui_set_page(&ui, UI_PAGE_BATTLE);
						audio_play(SFX_ENCOUNTER);
					}
					else
					{
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
				else if (s_shopRequest == DLG_ACT_QUEST)
				{
					const DialogScript *thanks = quest_complete(&player, s_talkNpc)
					                             ? quest_thanks_script(s_talkNpc) : NULL;
					audio_play(SFX_QUEST);
					if (thanks)
						auto_save(&player, playSeconds, true);
					if (thanks)
					{
						s_npcDetail = true;
						dialog_start(&dialog, thanks);
					}
					else
					{
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
				else if (s_shopRequest == DLG_ACT_QUEST_GIVE)
				{
					if (task_give_item(&player))
					{
						audio_play(SFX_QUEST);
						auto_save(&player, playSeconds, true);
						const DialogScript *next = task_talk_script(&player, s_talkNpc, playSeconds);
						s_npcDetail = true;
						if (next)
							dialog_start(&dialog, next);
						else
						{
							mode = MODE_FIELD;
							ui_set_page(&ui, UI_PAGE_FIELD);
						}
					}
					else
					{
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
				else if (s_shopRequest == DLG_ACT_QUEST_DECLINE)
				{
					const DialogScript *next = task_decline_script(&player, s_talkNpc);
					s_npcDetail = true;
					if (next)
						dialog_start(&dialog, next);
					else
					{
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
				else if (s_shopRequest == DLG_ACT_SUICIDE)
				{
					if (s_currentSlot >= 0)
					{
						save_delete(s_currentSlot);
						refresh_slots();
						s_gameOverReason = 2;
						mode = MODE_GAMEOVER;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
					else
					{
						log_add("你还没有存档，没什么可删除的。");
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
				else if (s_shopRequest == DLG_ACT_FINAL_FIGHT)
				{
					static const u16 PORTRAITS[3] = { 479, 467, 478 };
					int index = s_finalBoss - 126;
					if (index < 0 || index > 2)
						index = 0;
					if (npc_info(s_finalBoss) &&
					    battle_start(&s_battle, &player, s_finalBoss, PORTRAITS[index]))
					{
						mode = MODE_BATTLE;
						ui_set_page(&ui, UI_PAGE_BATTLE);
						audio_play(SFX_ENCOUNTER);
					}
					else
					{
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
					s_objectTalk = false;
				}
				else if (s_shopRequest == DLG_ACT_JOIN)
				{
					/* 拜师 only posts a message in the original; the new
					 * school shows up in 查看-描述, so go back to the map. */
					school_join(&player, s_talkNpc);
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else if (s_shopRequest == DLG_ACT_LEARN)
				{
					/* npc.s master: learn_msg is shown before master1's menu. */
					log_add("你想学什么就说吧!");
					train_open_learn(&ui, &player, &mode, s_talkNpc);
					if (mode != MODE_TRAIN)
					{
						/* Nothing to learn: drop the pending return target. */
						s_trainReturnMode = MODE_FIELD;
						s_trainReturnNpc = false;
						mode = MODE_FIELD;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
				else
				{
					s_objectTalk = false;
					s_npcDetail = false;
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				s_shopRequest = DLG_ACT_NONE;
			}
		}
		else if (mode == MODE_MENU)
		{
			if (down & KEY_B)
			{
				audio_play(SFX_CANCEL);
				mode = MODE_FIELD;
				ui_set_page(&ui, UI_PAGE_FIELD);
			}
			if (nav & KEY_UP)
			{
				ui.menuIndex = (ui.menuIndex + UI_MENU_ITEMS - 1) % UI_MENU_ITEMS;
				audio_play(SFX_CURSOR);
			}
			if (nav & KEY_DOWN)
			{
				ui.menuIndex = (ui.menuIndex + 1) % UI_MENU_ITEMS;
				audio_play(SFX_CURSOR);
			}

			bool activate = (down & KEY_A) != 0;
			if (touched)
			{
				int bx, by, bw, bh;
				ui_back_rect(&bx, &by, &bw, &bh);
				if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
				{
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else for (int i = 0; i < UI_MENU_ITEMS; i++)
				{
					int rx, ry, rw, rh;
					ui_menu_row_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						ui.menuIndex = i;
						activate = true;
						break;
					}
				}
			}
			if (activate)
			{
				audio_play(SFX_CONFIRM);
				apply_menu_action(menu_action(ui.menuIndex), &ui, &mode);
			}
		}
		else if (mode == MODE_LOOK)
		{
			if (down & KEY_B)
			{
				mode = MODE_MENU;
				ui_set_page(&ui, UI_PAGE_MENU);
			}
			if (nav & KEY_LEFT)
				ui.lookTab = (ui.lookTab + UI_LOOK_TAB_COUNT - 1) % UI_LOOK_TAB_COUNT;
			if (nav & KEY_RIGHT)
				ui.lookTab = (ui.lookTab + 1) % UI_LOOK_TAB_COUNT;
			if (down & KEY_A)
				ui.lookTab = (ui.lookTab + 1) % UI_LOOK_TAB_COUNT;
			if (touched)
			{
				int bx, by, bw, bh;
				ui_back_rect(&bx, &by, &bw, &bh);
				if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
				{
					mode = MODE_MENU;
					ui_set_page(&ui, UI_PAGE_MENU);
				}
				else for (int i = 0; i < UI_LOOK_TAB_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_look_tab_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						ui.lookTab = i;
						break;
					}
				}
			}
		}
		else if (mode == MODE_SYSTEM)
		{
			int act = -1;
			if (nav & KEY_UP)
			{
				ui.systemSel = (ui.systemSel + UI_SYSTEM_COUNT - 1) % UI_SYSTEM_COUNT;
				audio_play(SFX_CURSOR);
			}
			if (nav & KEY_DOWN)
			{
				ui.systemSel = (ui.systemSel + 1) % UI_SYSTEM_COUNT;
				audio_play(SFX_CURSOR);
			}
			if (down & KEY_A)
				act = ui.systemSel;
			if (down & KEY_B)
				act = UI_SYSTEM_BACK;
			if (touched)
			{
				for (int i = 0; i < UI_SYSTEM_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_system_row_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						ui.systemSel = i;
						act = i;
						break;
					}
				}
			}

			if (act == UI_SYSTEM_NEILI)
			{
				ui.neiliSel = 0;
				ui.jialiEdit = false;
				mode = MODE_NEILI;
				ui_set_page(&ui, UI_PAGE_NEILI);
			}
			else if (act == UI_SYSTEM_PRACTICE)
			{
				train_open_practice(&ui, &player, &mode);
			}
			else if (act == UI_SYSTEM_BACK)
			{
				mode = MODE_MENU;
				ui_set_page(&ui, UI_PAGE_MENU);
			}
		}
		else if (mode == MODE_SETTINGS)
		{
			int act = -1;
			if (nav & KEY_UP)
			{
				ui.settingsSel = (ui.settingsSel + UI_SETTINGS_COUNT - 1) % UI_SETTINGS_COUNT;
				audio_play(SFX_CURSOR);
			}
			if (nav & KEY_DOWN)
			{
				ui.settingsSel = (ui.settingsSel + 1) % UI_SETTINGS_COUNT;
				audio_play(SFX_CURSOR);
			}
			if (down & KEY_A)
				act = ui.settingsSel;
			if (down & KEY_B)
				act = UI_SETTINGS_BACK;
			if (touched)
			{
				for (int i = 0; i < UI_SETTINGS_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_settings_row_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						ui.settingsSel = i;
						act = i;
						break;
					}
				}
			}

			if (act == UI_SETTINGS_THEME)
			{
				/* Persisted in settings.cfg: the palette must survive a
				 * restart instead of falling back to 原味绿. */
				settings_set_theme((settings_theme() + 1) % 3);
				settings_save();
				log_add("界面主题已切换为「%s」。", gfx_theme_name());
				audio_play(SFX_CONFIRM);
			}
			else if (act == UI_SETTINGS_SAVE)
			{
				refresh_slots();
				ui.saveSlot = 0;
				s_saveReturnMode = MODE_SETTINGS;
				mode = MODE_SAVE;
				ui_set_page(&ui, UI_PAGE_SAVE);
			}
			else if (act == UI_SETTINGS_QUIT)
			{
				ui.quitSel = UI_QUIT_NO;
				mode = MODE_QUIT;
				ui_set_page(&ui, UI_PAGE_QUIT);
			}
			else if (act == UI_SETTINGS_BACK)
			{
				mode = MODE_MENU;
				ui_set_page(&ui, UI_PAGE_MENU);
			}
		}
		else if (mode == MODE_QUIT)
		{
			if (nav & KEY_LEFT)
				ui.quitSel = UI_QUIT_NO;
			if (nav & KEY_RIGHT)
				ui.quitSel = UI_QUIT_YES;
			int act = -1;
			if (down & KEY_A)
				act = ui.quitSel;
			if (down & KEY_B)
				act = UI_QUIT_NO;
			if (touched)
			{
				for (int i = 0; i < UI_QUIT_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_quit_button_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						ui.quitSel = i;
						act = i;
						break;
					}
				}
			}

			if (act == UI_QUIT_YES)
			{
				/* game.s end_game: cmp4 mud_age,save_time; 5 min without a
				 * save -> "你已经好久没存档了,现在保存吗?" */
				if (!s_loadOnly && playSeconds >= s_lastSaveTime + GMUD_SAVE_INTERVAL)
				{
					s_shopRequest = DLG_ACT_NONE;
					s_npcDetail = false;
					s_objectTalk = false;
					s_quitPrompt = true;
					dialog_start(&dialog, quit_save_script());
					mode = MODE_DIALOG;
					ui_set_page(&ui, UI_PAGE_DIALOG);
				}
				else
				{
					s_gameOverReason = 3;
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
			}
			else if (act == UI_QUIT_NO)
			{
				mode = MODE_SETTINGS;
				ui_set_page(&ui, UI_PAGE_SETTINGS);
			}
		}
		else if (mode == MODE_SAVE)
		{
			if (nav & KEY_UP)
				ui.saveSlot = (ui.saveSlot + SAVE_SLOT_COUNT - 1) % SAVE_SLOT_COUNT;
			if (nav & KEY_DOWN)
				ui.saveSlot = (ui.saveSlot + 1) % SAVE_SLOT_COUNT;

			int button = -1;
			if (touched)
			{
				for (int i = 0; i < SAVE_SLOT_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_save_slot_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
						ui.saveSlot = i;
				}
				for (int i = 0; i < UI_SAVE_BTN_COUNT; i++)
				{
					int rx, ry, rw, rh;
					ui_save_button_rect(i, &rx, &ry, &rw, &rh);
					if (point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
					{
						button = i;
						break;
					}
				}
			}
			if (down & KEY_B) button = UI_SAVE_BTN_BACK;
			if (down & KEY_A) button = UI_SAVE_BTN_SAVE;
			if (down & KEY_X) button = UI_SAVE_BTN_LOAD;

			if (button == UI_SAVE_BTN_SAVE)
			{
				if (s_loadOnly)
					log_add("江湖梦醒，只能读取旧档。");
				else if (ui.saveSlot == SAVE_SLOT_AUTO)
					log_add("自动存档由游戏自己写入，不能手动覆盖。");
				else
				{
					do_save(ui.saveSlot, &player, playSeconds);
					if (s_saveThenQuit)
					{
						s_saveThenQuit = false;
						s_gameOverReason = 3;
						mode = MODE_GAMEOVER;
						ui_set_page(&ui, UI_PAGE_FIELD);
					}
				}
			}
			else if (button == UI_SAVE_BTN_LOAD)
			{
				if (do_load(ui.saveSlot, &player, &playSeconds))
				{
					s_loadOnly = false;
					s_saveFromTitle = false;
					s_saveThenQuit = false;
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
					s_transition = 1.0f;
					lastMapIndex = player.mapIndex;
				}
			}
			else if (button == UI_SAVE_BTN_BACK)
			{
				if (s_saveFromTitle)
				{
					s_saveFromTitle = false;
					s_loadOnly = false;
					mode = MODE_TITLE;
					ui_set_page(&ui, UI_PAGE_TITLE);
					audio_play(SFX_CANCEL);
				}
				else if (s_saveThenQuit)
				{
					s_saveThenQuit = false;
					s_gameOverReason = 3;
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else if (s_loadOnly)
				{
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else
				{
					mode = s_saveReturnMode;
					ui_set_page(&ui, mode == MODE_SETTINGS ? UI_PAGE_SETTINGS
					                                       : UI_PAGE_MENU);
				}
			}
		}
		else if (mode == MODE_BAG)
		{
			int slots[BAG_SLOTS];
			int count = inv_list_slots(player.bag, slots, BAG_SLOTS);
			/* Using or dropping the last item of a page must not leave the
			 * list on a page that no longer exists. */
			int bagPages = (count + UI_BAG_ROWS - 1) / UI_BAG_ROWS;
			if (bagPages < 1)
				bagPages = 1;
			if (ui.bagPage >= bagPages)
				ui.bagPage = bagPages - 1;
			if (ui.bagPage < 0)
				ui.bagPage = 0;
			if (ui.bagSel >= 0 && BAG_EMPTY(player.bag[ui.bagSel]))
			{
				ui.bagSel = -1;
				ui.bagAct = -1;
			}

			/* Up / Down walk the filled slots (the page follows), Left /
			 * Right pick a detail-sheet button.  The sheet doubles as the
			 * list cursor, so a key-only player sees the item details
			 * without reaching for the touch screen. */
			if (nav & (KEY_UP | KEY_DOWN))
			{
				if (count > 0)
				{
					int sel = -1;
					for (int i = 0; i < count; i++)
						if (slots[i] == ui.bagSel)
							sel = i;
					if (nav & KEY_UP)
						sel = sel < 0 ? count - 1 : (sel + count - 1) % count;
					else
						sel = sel < 0 ? 0 : (sel + 1) % count;
					ui.bagSel = slots[sel];
					ui.bagPage = sel / UI_BAG_ROWS;
					ui.bagAct = bag_default_action(&player, ui.bagSel);
					audio_play(SFX_CURSOR);
				}
			}
			else if (ui.bagSel >= 0 && (down & (KEY_LEFT | KEY_RIGHT)))
			{
				const ItemDef *d = item_get(BAG_ID(player.bag[ui.bagSel]));
				int act = ui.bagAct >= 0 ? ui.bagAct
				                         : bag_default_action(&player, ui.bagSel);
				int step = (down & KEY_RIGHT) ? 1 : UI_BAG_ACT_COUNT - 1;
				for (int i = 0; i < UI_BAG_ACT_COUNT; i++)
				{
					act = (act + step) % UI_BAG_ACT_COUNT;
					if (bag_action_enabled(d, act))
						break;
				}
				if (act != ui.bagAct)
				{
					ui.bagAct = act;
					audio_play(SFX_CURSOR);
				}
			}

			if (down & KEY_B)
			{
				if (ui.bagSel >= 0)
				{
					ui.bagSel = -1;
					ui.bagAct = -1;
				}
				else
				{
					mode = MODE_MENU;
					ui_set_page(&ui, UI_PAGE_MENU);
				}
			}
			if (down & KEY_A)
			{
				if (ui.bagSel < 0)
				{
					/* No cursor yet: A opens the first row's sheet. */
					if (count > 0)
					{
						ui.bagSel = slots[0];
						ui.bagPage = 0;
						ui.bagAct = bag_default_action(&player, ui.bagSel);
						audio_play(SFX_CURSOR);
					}
				}
				else
				{
					audio_play(SFX_CONFIRM);
					bag_action(&ui, &player, &dialog, &mode, ui.bagAct);
				}
			}
			if (touched && mode == MODE_BAG)
			{
				int bx, by, bw, bh;
				ui_bag_back_rect(&bx, &by, &bw, &bh);
				if (ui.bagSel < 0 &&
				    point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
				{
					mode = MODE_MENU;
					ui_set_page(&ui, UI_PAGE_MENU);
				}
				else
				{
					bag_click(&ui, &player, &dialog, &mode,
					          (int)touch.px, (int)touch.py);
				}
			}
		}
		else if (mode == MODE_SHOP)
		{
			const NpcInfo *shop = npc_info(ui.shopNpc);
			int slots[BAG_SLOTS];
			int rows;
			if (ui.shopTab == 0)
			{
				rows = shop ? (int)shop->vendorCount : 0;
			}
			else
			{
				rows = collect_sell_slots(&player, slots, BAG_SLOTS);
			}
			int pages = (rows + UI_SHOP_ROWS - 1) / UI_SHOP_ROWS;
			if (pages < 1) pages = 1;
			if (ui.shopPage >= pages) ui.shopPage = pages - 1;
			if (ui.shopPage < 0) ui.shopPage = 0;

			/* 买卖页的键盘光标：上 / 下走条目，A 买 / 卖，B 离开。光标
			 * 始终留在当前页里——翻页后落到该页第一行，卖空的行走上一行。 */
			int first = ui.shopPage * UI_SHOP_ROWS;
			if (rows <= 0)
				ui.shopSel = -1;
			else if (ui.shopSel < 0)
				ui.shopSel = first;
			else if (ui.shopSel >= rows)
				ui.shopSel = rows - 1;
			else if (ui.shopSel < first || ui.shopSel >= first + UI_SHOP_ROWS)
				ui.shopSel = first;

			if (down & KEY_B)
			{
				mode = MODE_FIELD;
				ui_set_page(&ui, UI_PAGE_FIELD);
			}
			else
			{
				if (nav & (KEY_UP | KEY_DOWN))
				{
					if (rows > 0)
					{
						int sel = ui.shopSel;
						if (sel < 0)
							sel = (nav & KEY_UP) ? rows - 1 : 0;
						else if (nav & KEY_UP)
							sel = (sel + rows - 1) % rows;
						else
							sel = (sel + 1) % rows;
						if (sel != ui.shopSel)
						{
							ui.shopSel = sel;
							ui.shopPage = sel / UI_SHOP_ROWS;
							audio_play(SFX_CURSOR);
						}
					}
				}
				if (down & KEY_A)
				{
					if (ui.shopSel >= 0 && ui.shopSel < rows)
					{
						if (ui.shopTab == 0)
							shop_buy(&player, ui.shopNpc, ui.shopSel);
						else
						{
							shop_sell(&player, slots[ui.shopSel]);
							/* Selling the last one shrinks the list. */
							int n = collect_sell_slots(&player, slots, BAG_SLOTS);
							if (ui.shopSel >= n)
								ui.shopSel = n - 1;
						}
					}
				}
			}
			if (touched && mode == MODE_SHOP)
			{
				int bx, by, bw, bh;
				ui_shop_button_rect(UI_SHOP_BTN_BACK, &bx, &by, &bw, &bh);
				if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
				{
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else
				{
					shop_click(&ui, &player, (int)touch.px, (int)touch.py);
				}
			}
		}
		else if (mode == MODE_BATTLE)
		{
			if (touched)
				battle_touch(&s_battle, &player, (int)touch.px, (int)touch.py);
			battle_input_key(&s_battle, &player, down & ~KEY_TOUCH);
			battle_update(&s_battle, &player, dt);

			/* A book used in combat opens 研读 (use_book -> study_cmd).
			 * The fight stays active and resumes when training is closed. */
			if (s_battle.active && s_battle.pendingBook)
			{
				int id = s_battle.pendingBook;
				s_battle.pendingBook = 0;
				book_request(&ui, &player, &dialog, &mode, id, MODE_BATTLE);
			}

			if (mode == MODE_BATTLE && !s_battle.active)
			{
				int fought = s_battle.npcId;
				int result = s_battle.result;
				bool killed = s_battle.enemyKilled;
				if (result == 1)
				{
					task_on_battle(&player, fought, killed, playSeconds);
					/* A won fight may finish a task (石板 / 追杀 / 换物),
					 * so checkpoint right away. */
					auto_save(&player, playSeconds, true);
				}

				if (s_battle.fatal)
				{
					/* npc.s npc_win -> exit_game; the save file is untouched. */
					s_gameOverReason = 1;
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else if (result == 1 && killed && fought >= 126 && fought <= 128)
				{
					player.endingSeen = 1;
					s_endingBoss = fought - 126;
					s_endingPage = 0;
					s_endingTimer = 0.0f;
					s_gameOverReason = 0;
					mode = MODE_ENDING;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else
				{
					mode = MODE_FIELD;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
			}
		}
		else if (mode == MODE_SKILLS)
		{
			int learned[SKILL_SLOTS];
			int count = skill_learned_list(&player, learned, SKILL_SLOTS);
			if (count > 0)
			{
				if ((nav & KEY_UP) && ui.skillSel > 0)
					ui.skillSel--;
				if ((nav & KEY_DOWN) && ui.skillSel < count - 1)
					ui.skillSel++;
			}
			if (ui.skillSel >= count)
				ui.skillSel = count > 0 ? count - 1 : 0;
			if (ui.skillSel < 0)
				ui.skillSel = 0;
			if (ui.skillScroll > count - 8)
				ui.skillScroll = count > 8 ? count - 8 : 0;
			if (ui.skillScroll < 0)
				ui.skillScroll = 0;
			if (ui.skillSel < ui.skillScroll)
				ui.skillScroll = ui.skillSel;
			if (ui.skillSel > ui.skillScroll + 7)
				ui.skillScroll = ui.skillSel - 7;

			bool toggle = (down & KEY_A) != 0;
			if (down & KEY_B)
			{
				mode = MODE_MENU;
				ui_set_page(&ui, UI_PAGE_MENU);
			}

			if (touched)
			{
				int bx, by, bw, bh;
				ui_back_rect(&bx, &by, &bw, &bh);
				if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
				{
					mode = MODE_MENU;
					ui_set_page(&ui, UI_PAGE_MENU);
				}
				else for (int i = 0; i < 8; i++)
				{
					int rx, ry, rw, rh;
					ui_skill_row_rect(i, &rx, &ry, &rw, &rh);
					if (!point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
						continue;
					int index = ui.skillScroll + i;
					if (index < count)
					{
						ui.skillSel = index;
						toggle = true;
					}
					break;
				}
			}

			/* use_skills: tapping/confirming a learned kungfu toggles it. */
			if (toggle && count > 0 && ui.skillSel < count)
			{
				int kfId = player.skills[learned[ui.skillSel]].id;
				const SkillDef *d = skill_get(kfId);
				if (!skill_usekf_toggle(&player, kfId))
					log_add(d && d->type >= SKILL_USEKF_SLOTS
					            ? "「%s」无须启用。"
					            : "这门功夫不能启用。",
					        skill_name(kfId));
				else if (skill_usekf_id(&player, d ? d->type : 0) == kfId)
					log_add("已启用「%s」。", skill_name(kfId));
				else
					log_add("已停用「%s」。", skill_name(kfId));
			}
		}
		else if (mode == MODE_NEILI)
		{
			if (ui.jialiEdit)
			{
				/* lee1.s jiali: input_digit clamps to eff level/2. */
				int max = neili_jiali_max(&player);
				if (nav & KEY_UP)
					ui.jialiVal = clampi(ui.jialiVal + 1, 0, max);
				if (nav & KEY_DOWN)
					ui.jialiVal = clampi(ui.jialiVal - 1, 0, max);
				if (nav & KEY_RIGHT)
					ui.jialiVal = clampi(ui.jialiVal + 10, 0, max);
				if (nav & KEY_LEFT)
					ui.jialiVal = clampi(ui.jialiVal - 10, 0, max);
				if (down & KEY_B)
					ui.jialiEdit = false;
				if (down & KEY_A)
				{
					jiali_commit(&player, ui.jialiVal);
					ui.jialiEdit = false;
				}
				if (touched)
				{
					for (int i = 0; i < 4; i++)
					{
						int bx, by, bw, bh;
						ui_neili_edit_rect(i, &bx, &by, &bw, &bh);
						if (!point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
							continue;
						if (i == 0)
							ui.jialiVal = clampi(ui.jialiVal - 1, 0, max);
						else if (i == 1)
							ui.jialiVal = clampi(ui.jialiVal + 1, 0, max);
						else if (i == 2)
						{
							jiali_commit(&player, ui.jialiVal);
							ui.jialiEdit = false;
						}
						else
							ui.jialiEdit = false;
						break;
					}
				}
			}
			else
			{
				if (nav & KEY_UP)
					ui.neiliSel = (ui.neiliSel + 3) % 4;
				if (nav & KEY_DOWN)
					ui.neiliSel = (ui.neiliSel + 1) % 4;
				if (down & KEY_B)
				{
					mode = MODE_SYSTEM;
					ui_set_page(&ui, UI_PAGE_SYSTEM);
				}

				bool activate = (down & KEY_A) != 0;
				if (touched)
				{
					int bx, by, bw, bh;
					ui_neili_edit_rect(3, &bx, &by, &bw, &bh);
					if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
					{
						mode = MODE_SYSTEM;
						ui_set_page(&ui, UI_PAGE_SYSTEM);
					}
					else
					{
						for (int i = 0; i < 4; i++)
						{
							int rx, ry, rw, rh;
							ui_neili_row_rect(i, &rx, &ry, &rw, &rh);
							if (!point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
								continue;
							ui.neiliSel = i;
							activate = true;
							break;
						}
					}
				}

				if (activate)
				{
					char msg[256];
					switch (ui.neiliSel)
					{
					case 0:
						train_open_dazuo(&ui, &player, &mode);
						break;
					case 1:
						ui.jialiVal = player.force;
						ui.jialiEdit = true;
						break;
					case 2:
						neili_recover(&player, msg, sizeof(msg));
						log_add("%s", msg);
						break;
					case 3:
						neili_heal(&player, msg, sizeof(msg));
						log_add("%s", msg);
						break;
					default:
						break;
					}
				}
			}
		}
		else if (mode == MODE_MINIGAME)
		{
			/* panyan.s: the 山丘 game hall and its two mini games. */
			bool leave = !minigame_update(&s_minigame, dt, down);
			if (!leave && touched)
				leave = !minigame_touch(&s_minigame, (int)touch.px, (int)touch.py);
			if (leave)
			{
				minigame_finish(&s_minigame);
				mode = MODE_FIELD;
				ui_set_page(&ui, UI_PAGE_FIELD);
			}
		}
		else if (mode == MODE_ENDING)
		{
			s_endingTimer += dt;
			bool advance = (down & (KEY_A | KEY_RIGHT | KEY_TOUCH)) != 0;
			if (advance || s_endingTimer >= 2.2f)
			{
				s_endingTimer = 0.0f;
				if (s_endingPage < ending_max_start(s_endingBoss))
					s_endingPage++;
				else
					mode = MODE_RANK;
			}
		}
		else if (mode == MODE_RANK)
		{
			if (down & (KEY_A | KEY_B | KEY_TOUCH))
			{
				build_epilogue(&player);
				mode = MODE_EPILOGUE;
			}
		}
		else if (mode == MODE_EPILOGUE)
		{
			if (down & (KEY_A | KEY_B | KEY_TOUCH))
			{
				if (s_epiPage + 1 < epilogue_page_count())
					s_epiPage++;
				else
				{
					s_gameOverReason = 0;
					mode = MODE_GAMEOVER;
				}
			}
		}
		else if (mode == MODE_GAMEOVER)
		{
			/* START returns to the HOME menu; 读取存档/新的旅程 stay on A/B. */
			if (down & KEY_START)
				break;

			bool load = (down & KEY_A) != 0;
			bool restart = (down & KEY_B) != 0;

			if (touched)
			{
				int bx, by, bw, bh;
				gameover_button_rect(0, &bx, &by, &bw, &bh);
				if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
					load = true;
				gameover_button_rect(1, &bx, &by, &bw, &bh);
				if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
					restart = true;
			}

			if (load)
			{
				/* Death and suicide are one-way; 全剧终/结束 still allow a
				 * normal game after loading an old save. */
				s_loadOnly = (s_gameOverReason == 1 || s_gameOverReason == 2);
				refresh_slots();
				ui.saveSlot = 0;
				mode = MODE_SAVE;
				ui_set_page(&ui, UI_PAGE_SAVE);
			}
			else if (restart)
			{
				/* "新的旅程" == game.s clear_all: restart the global PRNG,
				 * then run input_new/adjust_attr again before enter_gmud. */
				rng_reset();
				cc_init(&ui.cc);
				cc_set_origin(&ui.cc, 1);
				mode = MODE_CHARCREATE;
				ui_set_page(&ui, UI_PAGE_CHARCREATE);
			}
		}
		else if (mode == MODE_CHARCREATE)
		{
			CharCreate *cc = &ui.cc;

			if (nav & KEY_UP) cc_move(cc, -1);
			if (nav & KEY_DOWN) cc_move(cc, 1);
			if (nav & KEY_LEFT) cc_inc(cc);
			if (nav & KEY_RIGHT) cc_dec(cc);

			bool confirm = (down & KEY_A) != 0;
			bool editName = (down & KEY_X) != 0;
			bool reset = false;
			bool cancel = (down & KEY_B) != 0;

			if (touched)
			{
				bool handled = false;
				int nx, ny, nw, nh;
				ui_cc_name_rect(&nx, &ny, &nw, &nh);
				if (point_in((int)touch.px, (int)touch.py, nx, ny, nw, nh))
				{
					editName = true;
					handled = true;
				}
				for (int i = 0; i < UI_CC_BTN_COUNT && !handled; i++)
				{
					int bx, by, bw, bh;
					ui_cc_button_rect(i, &bx, &by, &bw, &bh);
					if (!point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
						continue;
					if (i == UI_CC_BTN_CONFIRM) confirm = true;
					else if (i == UI_CC_BTN_RESET) reset = true;
					else cancel = true;
					handled = true;
					break;
				}
				for (int i = 0; i < CC_ATTR_COUNT && !handled; i++)
				{
					for (int w = 0; w < 2; w++)
					{
						int sx, sy, sw, sh;
						ui_cc_step_rect(i, w, &sx, &sy, &sw, &sh);
						if (!point_in((int)touch.px, (int)touch.py,
						              sx, sy, sw, sh))
							continue;
						cc_set_pointer(cc, i + 1);
						if (w == 0) cc_dec(cc);
						else cc_inc(cc);
						handled = true;
						break;
					}
				}
				if (!handled)
				{
					for (int i = 0; i < CC_ATTR_COUNT; i++)
					{
						int rx, ry, rw, rh;
						ui_cc_row_rect(i, &rx, &ry, &rw, &rh);
						if (point_in((int)touch.px, (int)touch.py,
						             rx, ry, rw, rh))
						{
							cc_set_pointer(cc, i + 1);
							break;
						}
					}
				}
			}

			if (editName)
				charcreate_name_input(cc);
			else if (reset)
				cc_reset(cc);
			else if (cancel)
				cc_cancel(cc);
			else if (confirm && cc_confirm(cc))
			{
				/* input.s:98-109 consumes two random_it(21) draws, first
				 * for 相貌 then for 福缘. Keep the order explicit: C does
				 * not define argument evaluation order. */
				uint8_t drawPer = rng_random_it_a(CC_RANDOM_RANGE);
				uint8_t drawKar = rng_random_it_a(CC_RANDOM_RANGE);
				CharCreateResult result;
				cc_finalize(cc, drawPer, drawKar, &result);

				task_ghost_remove(&player);
				actor_stats_init(&player, &result);
				/* game.s new_game clears zero_area, including the game_hour
				 * RTC clock. */
				actor_set_game_clock_base(0);
				const GameMap *sm = map_get(map_start_map());
				if (sm)
					actor_init(&player, sm, (int)map_start_map(),
					           map_start_x(), map_start_y());
				playSeconds = 0;
				/* game.s clear_apply: top_dance / top_ball start at 100. */
				minigame_reset_top();
				s_currentSlot = -1;
				s_loadOnly = false;
				s_saveThenQuit = false;
				s_quitPrompt = false;
				s_pendingBook = 0;
				s_trainReturnMode = MODE_FIELD;
				s_trainReturnNpc = false;
				s_lastSaveTime = 0;
				ui_init(&ui);
				/* game.s create_game writes the new character immediately.
				 * The 3DS port puts it in the auto slot: the three manual
				 * slots stay fully under the player's control, and 继续游戏
				 * picks the newest of the four anyway. */
				refresh_slots();
				auto_save(&player, playSeconds, false);
				s_currentSlot = SAVE_SLOT_AUTO;
				ui.hasContinue = true;
				mode = MODE_FIELD;
				ui_set_page(&ui, UI_PAGE_FIELD);
				log_add("「%s」踏入江湖。", player.name);
			}

			if (cc->stage == CC_STAGE_CANCELLED)
			{
				if (cc->fromGameOver)
				{
					/* The ended run stays intact; no new character created. */
					mode = MODE_GAMEOVER;
					ui_set_page(&ui, UI_PAGE_FIELD);
				}
				else
					break;   /* input.s input_error -> exit_game */
			}
		}
		else if (mode == MODE_TRAIN)
		{
			TrainUi *t = &ui.train;
			/* npc.s learn_tbl: dw 4 (ms); skill.s practice/dazuo_tbl: dw 400. */
			float interval = (t->kind == TRAIN_KIND_LEARN ||
			                  t->kind == TRAIN_KIND_BOOK) ? 0.004f : 0.4f;
			if (t->phase == TRAIN_PHASE_RUN && t->running)
			{
				t->acc += dt;
				while (t->acc >= interval)
				{
					t->acc -= interval;
					t->rounds++;
					if (!train_step(&player, &ui))
					{
						t->running = false;
						log_add("%s", t->msg);
						break;
					}
					/* train_step pauses after each level-up to ask
					 * "继续学习吗?"; A resumes on the next frame. */
					if (!t->running)
						break;
				}
			}

			if (t->phase == TRAIN_PHASE_SELECT)
			{
				if (nav & KEY_UP && t->sel > 0)
					t->sel--;
				if (nav & KEY_DOWN && t->sel < t->listCount - 1)
					t->sel++;
				if (t->sel < t->scroll)
					t->scroll = t->sel;
				if (t->sel > t->scroll + 7)
					t->scroll = t->sel - 7;
				if (down & KEY_A)
				{
					t->kf = t->list[t->sel];
					train_begin_run(t);
				}
				if (down & KEY_B)
					train_leave(&ui, &mode, &dialog);
				if (touched)
				{
					int bx, by, bw, bh;
					ui_train_button_rect(1, &bx, &by, &bw, &bh);
					if (point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
					{
						train_leave(&ui, &mode, &dialog);
					}
					else
					{
						for (int i = 0; i < 8; i++)
						{
							int rx, ry, rw, rh;
							ui_train_row_rect(i, &rx, &ry, &rw, &rh);
							if (!point_in((int)touch.px, (int)touch.py, rx, ry, rw, rh))
								continue;
							int index = t->scroll + i;
							if (index < t->listCount)
							{
								t->sel = index;
								t->kf = t->list[index];
								train_begin_run(t);
							}
							break;
						}
					}
				}
			}
			else
			{
				/* B stops the loop first; a second B leaves the page, so
				 * the touch 返回 button is a supplement, not the only
				 * way out (the original pops the menu with B alone). */
				if (down & KEY_B)
				{
					if (t->running)
						t->running = false;
					else
						train_leave(&ui, &mode, &dialog);
				}
				else if ((down & KEY_A) && !t->running)
					t->running = true;
				if (touched)
				{
					for (int i = 0; i < 2; i++)
					{
						int bx, by, bw, bh;
						ui_train_button_rect(i, &bx, &by, &bw, &bh);
						if (!point_in((int)touch.px, (int)touch.py, bx, by, bw, bh))
							continue;
						if (i == 0)
							t->running = !t->running;
						else
						{
							t->running = false;
							train_leave(&ui, &mode, &dialog);
						}
						break;
					}
				}
			}
		}

		m = map_get(player.mapIndex);
		{
			/* A map change swaps the whole scene, so it gets a short black
			 * wipe and the camera snaps to the new room. */
			bool mapChanged = (player.mapIndex != lastMapIndex);
			if (mapChanged)
			{
				lastMapIndex = player.mapIndex;
				s_transition = 1.0f;
			}
			update_camera(m, &player, &camX, &camY, dt, mapChanged);
		}
		if (s_transition > 0.0f)
		{
			s_transition -= dt / 0.40f;
			if (s_transition < 0.0f)
				s_transition = 0.0f;
		}
		ui_toast_tick(dt);
		ui_update(&ui, dt);

		/* The 3D slider drives the stereoscopic parallax: at 0 the game
		 * renders a single eye and gfxSet3D stays off (gfx_3d_update). */
		gfx_3d_update(osGet3DSliderState(), dt);

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		gfx_3d_begin_eye(GFX_EYE_LEFT);
		C2D_TargetClear(gfx_top_target(), C2D_RGBA(0x0B, 0x10, 0x0B));
		C2D_SceneBegin(gfx_top_target());
		draw_top_screen(mode, m, camX, camY, &player, playSeconds, &ui);
		if (gfx_3d_active())
		{
			gfx_3d_begin_eye(GFX_EYE_RIGHT);
			C2D_TargetClear(gfx_top_right_target(), C2D_RGBA(0x0B, 0x10, 0x0B));
			C2D_SceneBegin(gfx_top_right_target());
			draw_top_screen(mode, m, camX, camY, &player, playSeconds, &ui);
		}
		gfx_3d_begin_eye(GFX_EYE_NONE);

		C2D_TargetClear(gfx_bottom_target(), C2D_RGBA(0x10, 0x18, 0x10));
		C2D_SceneBegin(gfx_bottom_target());
		if (mode == MODE_ENDING)
			draw_ending(s_endingBoss);
		else if (mode == MODE_RANK)
			draw_rank(&player);
		else if (mode == MODE_EPILOGUE)
			draw_epilogue();
		else if (mode == MODE_GAMEOVER)
			draw_gameover();
		else if (mode == MODE_MINIGAME)
			minigame_draw(&s_minigame);
		else
			ui_bottom_draw(&ui, &player, m, playSeconds, &dialog,
			               s_slots, &s_battle);
		C2D_Flush();
		C3D_FrameEnd(0);
	}

	save_fini();
	text_fini();
	fight_text_free();
	skill_pack_free();
	npc_pack_free();
	item_pack_free();
	map_pack_free();
	audio_fini();
	gfx_fini();
	romfsExit();
	return 0;
}

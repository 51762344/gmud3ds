#ifndef TASK_H
#define TASK_H

#include "actor.h"
#include "dialog.h"

/* task.s 第一类任务: 寻人/寻物/杀NPC/动态鬼.
 * Slot indices follow the original quest_temp layout. */
enum
{
	TASK_TYPE_NPC = 1,   /* stored type; 0 means an empty slot */
	TASK_TYPE_GOODS,
	TASK_TYPE_KILL,
	TASK_TYPE_GHOST,
	TASK_TYPE_COUNT
};

enum
{
	TASK_STATE_NONE = 0,
	TASK_STATE_ACTIVE,
	TASK_STATE_DONE,        /* target met; report to the giver once */
	TASK_STATE_REWARD       /* waiting at 顾炎武 */
};

/* Task givers from task.s task_npc_tbl. */
#define TASK_NPC_CUNZHANG  6
#define TASK_NPC_FUREN     10
#define TASK_NPC_PINGYIZHI 26
#define TASK_NPC_BUKUAI    3
#define TASK_NPC_TEACHER   31
#define TASK_NPC_OLDLADY   25

/* Interactive object placed on 时空的尽头 (npc table id 210). */
#define TASK_OBJECT_MAGIC  210

/* Runtime-only ghost npc id (h/id.h KILLER_NPC). */
#define TASK_GHOST_NPC     125

/* True for 村长/妇人/平一指/捕快/顾炎武. */
bool task_is_giver(int npcId);

/* 交谈 entry point. Returns a runtime dialog when this NPC has a task
 * response (new task, reminder, completion, reward), else NULL. */
const DialogScript *task_talk_script(Player *p, int npcId, u32 now);

/* Confirm hand-over for a 寻物 task (DLG_ACT_QUEST_GIVE). */
bool task_give_item(Player *p);
/* Decline hand-over: show the "妾身还盼着..." existing-task message. */
const DialogScript *task_decline_script(Player *p, int npcId);
/* 顾炎武's reward message; always returns a script when called for him. */
const DialogScript *task_reward_script(Player *p, u32 now);

/* Battle result hook: completes the kill quest / ghost hunt. */
void task_on_battle(Player *p, int npcId, bool killed, u32 now);

/* Legacy v7 kill-quest fields -> tasks[TASK_TYPE_KILL]. */
void task_migrate(Player *p, u32 now);

/* Dynamic ghost map overlay (G_Killer_Map/X/Y in tools.s). */
const char *task_ghost_name_text(const Player *p, char *buf, int size);
bool task_ghost_prepare(Player *p);       /* refresh npc_info(125) before a talk/fight */
void task_ghost_remove(Player *p);        /* clear the overlay and npc_info */
void task_ghost_sync(Player *p);          /* validate coords after loading a save */
int  task_ghost_map(const Player *p);     /* -1 when none */
bool task_ghost_here(const Player *p);    /* placed on the player's map */
bool task_ghost_at(const Player *p, int x, int y);
u16  task_ghost_tile(const Player *p);    /* 474 male / 466 female */

/* task.s voluntary_work (老婆婆): 1 扫地 / 2 挑水 / 3 劈柴. Returns a dialog
 * showing the four poem lines and the 20 exp / 10 pot / 50 money reward
 * (scaled x15, see reward.h), or NULL when this object is not the requested
 * chore. */
const DialogScript *task_chore_script(Player *p, int kind);

#endif

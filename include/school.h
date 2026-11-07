#ifndef SCHOOL_H
#define SCHOOL_H

#include "actor.h"
#include "dialog.h"

#define SCHOOL_COUNT 6    /* 八卦门/百花谷/红莲教/哪吒派/太极门/雪山派 */

/* Kungfu slots of every school, straight from task.s menpai_tbl:
 * slot 0..4 = 拳脚/兵刃/轻功/内功/招架. pai is the npc_data pai value. */
int school_kf(int pai, int slot);          /* -1 when out of range */
int school_weapon_item(int pai);           /* preferred weapon goods id */

bool school_is_master(int npcId);          /* in serve.s bashi_npc_tbl? */

/* npc.s init_menu: the 请教 entry exists for your own master and the
 * special NPCs 私塾先生(31)/独行大侠(7)/雪豹(124). */
bool school_can_learn(const Player *p, int npcId);

/* serve.s baishi conditions. Returns NULL when the master accepts,
 * otherwise a short refusal reason. */
const char *school_master_check(const Player *p, int npcId);

/* npc.s npc_apprentice: pai rules + per-master conditions. Logs the result. */
bool school_join(Player *p, int npcId);

/* Master's level of one kungfu from npcs.bin kf pairs (0 = not known). */
int school_master_kf_level(int npcId, int kfId);

/* goods.dat book1_skill..book5_skill (skill.s study_cmd). */
int book_skill_count(int itemId);
int book_skill_id(int itemId, int index);
int book_skill_level(int itemId, int index);
int book_kf_level(int itemId, int kfId);
/* NULL when the book may be opened, otherwise the original refusal text. */
const char *book_read_error(const Player *p, int itemId);
/* 菜花宝典 needs the two Y/N confirmations unless the player is 人妖. */
bool book_needs_gender(const Player *p, int itemId);

/* Runtime dialog for a master NPC; NULL when npcId is not a master. */
const DialogScript *school_dialog_script(const Player *p, int npcId);

/* Training ticks. Original show_process loops: 练习/打坐 400 ms, 学习 4 ms.
 * Return true to keep going; on false msg carries the failure reason. */
bool train_practice_tick(Player *p, int kfId, char *msg, int size);
bool train_learn_tick(Player *p, int kfId, int masterNpc, char *msg, int size);
/* Same round with an explicit target level (book tables / master NPC). */
bool train_learn_tick_level(Player *p, int kfId, int masterLv, char *msg, int size);
bool train_dazuo_tick(Player *p, char *msg, int size);

#endif

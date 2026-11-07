#ifndef QUEST_H
#define QUEST_H

#include "actor.h"
#include "dialog.h"

/* npc_quest.s / npc_quest.txt: item-for-item trades with town NPCs.
 * give/reward are goods ids from h/id.h. */
typedef struct
{
	u8 npcId;        /* npc_data id (1-based) */
	u8 giveItem;
	u8 giveCount;
	u8 rewardItem;
} QuestDef;

const QuestDef *quest_find(int npcId);
const QuestDef *quest_at(int index);
int quest_count(void);

/* Does the bag carry enough of the requested item? */
bool quest_can_deliver(const Player *p, const QuestDef *q);

/* Runs the trade after the player confirms in dialog. Logs the result. */
bool quest_complete(Player *p, int npcId);

/* Dialog for quest/chat NPCs of npc_quest.txt: the trade offer when the
 * goods are carried, otherwise the rotating chat lines. NULL if unknown. */
const DialogScript *quest_dialog_script(const Player *p, int npcId);
const DialogScript *quest_thanks_script(int npcId);

#endif

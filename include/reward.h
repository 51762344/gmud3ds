#ifndef REWARD_H
#define REWARD_H

#include "gmud.h"

/* Intentional deviation from the original.
 *
 * The original "白金英雄坛说" pays almost nothing for repeatable content:
 * the 老婆婆's chores give 20 exp / 10 pot / 50 money, 顾炎武 hands out
 * ~600 exp per quest batch and the dynamic ghost ~80..2000 exp. Players had
 * to repeat the same chores over and over just to build up exp / potential /
 * money, which is the most boring part of the old game.
 *
 * REWARD_SCALE multiplies every reward the player earns from content by 15.
 * Only the payouts are scaled: the stamina gates, the school fees, the shop
 * prices and the quest slot values (u16, still the original numbers, so the
 * save layout is unchanged) stay as they are.
 */
#define REWARD_SCALE 15

/* Original reward * REWARD_SCALE, saturated at 32 bit. */
static inline u32 reward_scale(u32 value)
{
	u64 v = (u64)value * (u64)REWARD_SCALE;
	return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)v;
}

#endif

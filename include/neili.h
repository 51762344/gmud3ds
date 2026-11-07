#ifndef NEILI_H
#define NEILI_H

#include "gmud.h"

struct Player;

/* lee1.s internal-force commands (原版主菜单 内力 → 打坐/加力/吸气/疗伤).
 * 打坐 lives in school.c (train_dazuo_tick); this module covers the rest.
 * Every function writes a short result message, matching the original
 * strings (scode branch), and returns true when stats changed. */

/* recover 吸气: converts neili into hp up to the injury cap.
 * Requires an enabled force kungfu and mp >= 20. */
bool neili_recover(struct Player *p, char *msg, int size);

/* heal 疗伤: treats injuries, raising effhp toward hpMax.
 * Requires an enabled force kungfu, effective force level >= 45,
 * mpMax >= 150, mp >= 100 and effhp >= hpMax/3. */
bool neili_heal(struct Player *p, char *msg, int size);

/* jiali 加力 upper bound: effective force level / 2 (input_digit max). */
int neili_jiali_max(const struct Player *p);

/* jiali_flow_msg with the runtime number inserted ("你目前加力上限为" + N). */
void neili_jiali_text(char *msg, int size, const struct Player *p, int value);

#endif

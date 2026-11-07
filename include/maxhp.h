/* game.s:312-358 setup_maxhp and game.s:227-236 check_player (vitals only).
 *
 * Pure integer arithmetic with no libctru dependency: the 3DS build calls
 * these from actor.c, while tools/maxhp_verify.py compiles source/maxhp.c on
 * the host and compares it with an independent Python model of the assembly.
 */
#ifndef GMUD_MAXHP_H
#define GMUD_MAXHP_H

#include <stdint.h>

#define GMUD_AGE_SECONDS 43200u       /* h/gmud.h AGE_TIME = 3600*12 */
#define GMUD_BASE_AGE 14
#define GMUD_MAXHP_AGE_CAP 29
#define GMUD_JIAOYI_MIN_AGE 20
#define GMUD_JIAOYI_MIN_LEVEL 80

/* game.s:314-320: divid42 by AGE_TIME, then `lda a1 / adc #14 / sta man_age`.
 * man_age is one byte, so the quotient's low byte and the +14 wrap at 256. */
uint8_t gmud_man_age(uint32_t playSeconds);

/* game.s:321-357: maxhp = 100 + (clamp(man_age,14,29)-14)*20 + maxfp/4,
 * plus 红莲教义 level * attr_con / 10 when man_age >= 20 and the kungfu is
 * learned at level >= 80. jiaoyiLevel is 0 when the kungfu is not learned. */
int gmud_setup_maxhp(uint8_t manAge, int maxfp, int jiaoyiLevel, int attrCon);

/* game.s:227-236: effhp <= maxhp, hp <= effhp. */
void gmud_check_vitals(int maxhp, int *hp, int *effhp);

#endif

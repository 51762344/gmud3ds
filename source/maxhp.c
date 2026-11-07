/* game.s setup_maxhp / check_player, host-verifiable integer core.
 * See include/maxhp.h and tools/maxhp_verify.py. */
#include "maxhp.h"

uint8_t gmud_man_age(uint32_t playSeconds)
{
	return (uint8_t)(GMUD_BASE_AGE + (playSeconds / GMUD_AGE_SECONDS));
}

int gmud_setup_maxhp(uint8_t manAge, int maxfp, int jiaoyiLevel, int attrCon)
{
	/* game.s:323-331 clamps only A, not man_age: the 红莲教义 check below
	 * still uses the raw (possibly >29 or wrapped) man_age. */
	int age = manAge < GMUD_BASE_AGE ? GMUD_BASE_AGE
	        : (manAge >= GMUD_MAXHP_AGE_CAP ? GMUD_MAXHP_AGE_CAP : (int)manAge);
	int hp = 100 + (age - GMUD_BASE_AGE) * 20;

	/* lm2 a2,man_maxfp; lsr2 twice = unsigned 16-bit divide by four. */
	if (maxfp > 0)
		hp += maxfp / 4;

	/* game.s:341-356: find_kf(JIAOYI_KF) with level >= 80, mul_ax by
	 * attr_con, divid2 by 10, then a 16-bit add to man_maxhp. */
	if (manAge >= GMUD_JIAOYI_MIN_AGE &&
	    jiaoyiLevel >= GMUD_JIAOYI_MIN_LEVEL && attrCon > 0)
		hp += jiaoyiLevel * attrCon / 10;
	return hp;
}

void gmud_check_vitals(int maxhp, int *hp, int *effhp)
{
	if (!hp || !effhp)
		return;
	/* cmp2 man_maxhp,man_effhp / bcs / lm2 man_effhp,man_maxhp */
	if (*effhp > maxhp)
		*effhp = maxhp;
	/* cmp2 man_effhp,man_hp / bcs / lm2 man_hp,man_effhp */
	if (*hp > *effhp)
		*hp = *effhp;
	/* The original fields are unsigned 16-bit; Player uses s16. */
	if (*effhp < 0)
		*effhp = 0;
	if (*hp < 0)
		*hp = 0;
}

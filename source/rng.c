/* T1: bit-exact port of ref/gmud/src/math.s random_it / perform_random_it.
 *
 * Assembly facts that drive every line below (ref/gmud/src/math.s):
 *  - mud_seed is 2 bytes (h/gmud.h:254), cleared by game.s clear_all, never
 *    saved, and shared by every random source in the game.
 *  - random_it perturbs only the LOW byte of mud_seed with
 *    `getms*2 + random + mud_seed`, multiplies the 16-bit state by 65531
 *    (mul2 -> 32-bit product), does `inc2 a1` (a 16-bit increment that does
 *    NOT carry into the high half), writes that half back to mud_seed, then
 *    divides the whole 32-bit a1/a2 by the 16-bit range (divid42).
 *  - perform_random_it builds two 8x8 products with mul_ax, stacks them as
 *    a 32-bit dividend (first product = high half), and calls divid4.
 *  - `random` and `second_m2` are BIOS variables with no code in this tree;
 *    they are sampled through RngHooks. On the 3DS the defaults derive from
 *    osGetTime(); regression builds inject a fixed sequence instead.
 */
#include "rng.h"

#ifdef __3DS__
#include <3ds.h>
#endif

static uint64_t rng_ticks(void)
{
#ifdef __3DS__
	return (uint64_t)osGetTime();
#else
	return 0u; /* host builds inject hooks (tools/rng_dump.c) */
#endif
}

static uint8_t def_getms(void)       { return (uint8_t)(rng_ticks() & 0xFFu); }
static uint8_t def_random_byte(void) { return (uint8_t)((rng_ticks() >> 8) & 0xFFu); }
static uint8_t def_second(void)      { return (uint8_t)((rng_ticks() / 1000u) & 0xFFu); }

static uint8_t s_fixedMs, s_fixedRandom, s_fixedSecond;
static uint8_t fix_getms(void)       { return s_fixedMs; }
static uint8_t fix_random_byte(void) { return s_fixedRandom; }
static uint8_t fix_second(void)      { return s_fixedSecond; }

static const RngHooks s_defaultHooks = { def_getms, def_random_byte, def_second };
static const RngHooks s_fixedHooks   = { fix_getms, fix_random_byte, fix_second };
static RngHooks s_hooks              = { def_getms, def_random_byte, def_second };

static uint16_t s_seed;   /* mud_seed */
static int s_carry;       /* C on entry to perform_random_it */

void rng_init(void)
{
	s_hooks = s_defaultHooks;
	rng_reset();
}

void rng_reset(void)
{
	s_seed = 0;
	s_carry = 0;
}

void rng_set_seed(uint16_t seed)  { s_seed = seed; }
uint16_t rng_seed_value(void)     { return s_seed; }

void rng_set_hooks(const RngHooks *hooks)
{
	s_hooks = hooks ? *hooks : s_defaultHooks;
}

void rng_set_fixed(uint8_t ms, uint8_t random_byte, uint8_t second)
{
	s_fixedMs = ms;
	s_fixedRandom = random_byte;
	s_fixedSecond = second;
	s_hooks = s_fixedHooks;
}

uint8_t rng_getms(void)          { return s_hooks.getms(); }
void rng_set_carry(int carry)    { s_carry = carry ? 1 : 0; }

/* math.s:45-59 mul_ax: a1 = Areg * Xreg (8x8 -> 16 bits).
 *
 * perform_random_it consumes the carry left in C by this routine at
 * math.s:335. asl2/lm2/lda/tax do not change C; `asl a2h` sets it to the
 * multiplier bit being tested and `adda2 a1` (16-bit add of the 8-bit A
 * register) replaces it with that add's carry. The last loop iteration
 * decides the final C, so emulate the shifts one bit at a time. */
static uint16_t mul_ax(uint8_t a, uint8_t x, int *carry)
{
	uint16_t acc = 0;
	uint8_t m = x;
	int c = 0;
	for (int i = 0; i < 8; i++)
	{
		acc = (uint16_t)(acc << 1);          /* asl2 a1 */
		int bit = m >> 7;                    /* asl a2h -> C */
		m = (uint8_t)(m << 1);
		c = bit;                             /* bcc mul_con */
		if (bit)
		{
			uint32_t sum = (uint32_t)acc + a;  /* lda a2 / adda2 a1 */
			c = sum > 0xFFFFu ? 1 : 0;
			acc = (uint16_t)sum;
		}
	}
	*carry = c;
	return acc;
}

uint16_t rng_random_it(uint16_t range)
{
	/* math.s:273-275 and 316-318: a zero range returns 0 from a separate
	 * exit path, leaving mud_seed untouched. */
	if (range == 0)
		return 0;

	/* math.s:278-282 jsr getms / asl a / adc random / adc mud_seed /
	 * sta mud_seed. asl supplies the first adc's carry; `sta` writes only
	 * the low byte, so the high byte survives from the previous call. */
	uint8_t ms = s_hooks.getms();
	uint8_t rb = s_hooks.random_byte();
	uint32_t t = (uint32_t)(uint8_t)(ms << 1) + rb + (uint32_t)(ms >> 7);
	uint8_t lo = (uint8_t)t;
	int c = t > 0xFFu ? 1 : 0;
	t = (uint32_t)lo + (uint32_t)(s_seed & 0xFFu) + (uint32_t)c;
	lo = (uint8_t)t;
	uint16_t seed = (uint16_t)((s_seed & 0xFF00u) | lo);

	/* math.s:283-287 mul2 is 16x16->32. `inc2 a1` is a 16-bit increment
	 * that wraps without carrying into a2 (see h/gmud.h:574 sub0a2 and
	 * math.s:74-80), and mud_seed is written back from that half only. */
	uint32_t prod = (uint32_t)seed * 65531u;
	uint16_t a1 = (uint16_t)((prod & 0xFFFFu) + 1u);
	uint16_t a2 = (uint16_t)(prod >> 16);
	s_seed = a1;

	/* math.s:310-314 divid42: (a2:a1) / range, 16-bit remainder in a3/a1. */
	uint32_t dividend = ((uint32_t)a2 << 16) | a1;
	return (uint16_t)(dividend % (uint32_t)range);
}

uint8_t rng_random_it_a(uint16_t range)
{
	return (uint8_t)rng_random_it(range);
}

static uint32_t perform_it(uint32_t range, int carry_in)
{
	/* math.s:326-342. getms is sampled twice (326, 337) and second_m2
	 * twice (330, 334); every sample goes through the hooks in that order. */
	uint8_t ms1 = s_hooks.getms();         /* 326 jsr getms */
	uint8_t sec1 = s_hooks.second();       /* 330 lda second_m2 */
	uint8_t seedLo = (uint8_t)s_seed;
	uint8_t seedHi = (uint8_t)(s_seed >> 8);

	/* 328 adc mud_seed: C here is the caller's carry. */
	uint32_t t = (uint32_t)ms1 + seedLo + (uint32_t)(carry_in ? 1 : 0);
	uint8_t x1 = (uint8_t)t;              /* 329 tax */
	int c = t > 0xFFu ? 1 : 0;
	/* 331 adc mud_seed+1 */
	t = (uint32_t)sec1 + seedHi + (uint32_t)c;
	uint8_t a1 = (uint8_t)t;
	int cmul;
	uint16_t prod1 = mul_ax(a1, x1, &cmul);  /* 332 jsr mul_ax -> a4 */

	uint8_t sec2 = s_hooks.second();       /* 334 lda second_m2 */
	/* 335 adc mud_seed: C is what mul_ax left behind. */
	t = (uint32_t)sec2 + seedLo + (uint32_t)cmul;
	uint8_t x2 = (uint8_t)t;              /* 336 tax */
	c = t > 0xFFu ? 1 : 0;
	uint8_t ms2 = s_hooks.getms();         /* 337 jsr getms (C preserved) */
	/* 339 adc mud_seed+1 */
	t = (uint32_t)ms2 + seedHi + (uint32_t)c;
	uint8_t a2 = (uint8_t)t;
	int unused;
	uint16_t prod2 = mul_ax(a2, x2, &unused); /* 340 jsr mul_ax -> a3 */

	s_seed = prod2;                        /* 342 lm2 mud_seed,a1 */

	/* math.s:345-350: dividend = a4:a3, divisor = a8:a7, divid4 gives the
	 * 32-bit remainder. With a zero divisor divid4 subtracts nothing, so
	 * the dividend itself remains in a3/a4. */
	uint32_t dividend = ((uint32_t)prod1 << 16) | prod2;
	return range ? dividend % range : dividend;
}

uint32_t rng_perform_random_it(uint32_t range)
{
	return perform_it(range, s_carry);
}

uint32_t rng_perform_random_it_carry(uint32_t range, int carry)
{
	return perform_it(range, carry);
}

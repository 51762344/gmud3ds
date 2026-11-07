/* GMUD 3DS - bit-exact port of the original PRNG in ref/gmud/src/math.s.
 *
 *   random_it         math.s:272-318  16-bit mud_seed, 32-bit product modulo
 *                                    a 2-byte range (divid42).
 *   perform_random_it math.s:325-351  two 8x8 products built from getms and
 *                                    second_m2, modulo a 4-byte range
 *                                    (divid4).
 *
 * Both routines also read BIOS RAM that has no implementation in the source
 * tree: getms (system.s:205), the `random` byte at 0x00c8 used by
 * math.s:280, and second_m2 at 0x0430. Those samples are the only external
 * entropy; rng_set_hooks()/rng_set_fixed() make them injectable so a run is
 * reproducible bit for bit.
 */
#ifndef GMUD_RNG_H
#define GMUD_RNG_H

#include <stdint.h>

typedef struct
{
	uint8_t (*getms)(void);        /* system.s getms: RTC sub-second byte */
	uint8_t (*random_byte)(void);  /* BIOS `random` (math.s:280 adc random) */
	uint8_t (*second)(void);       /* BIOS `second_m2` (math.s:330/334) */
} RngHooks;

/* game.s:150-162 clear_all wipes 0x2000..0x3fff, which contains mud_seed.
 * There is no per-fight reseed and mud_seed is never saved. */
void rng_init(void);
void rng_reset(void);
void rng_set_seed(uint16_t seed);
uint16_t rng_seed_value(void);

/* NULL restores the hardware-backed hooks. */
void rng_set_hooks(const RngHooks *hooks);
/* Pin all three samples to constants (deterministic replay / regression). */
void rng_set_fixed(uint8_t ms, uint8_t random_byte, uint8_t second);
/* getms() as a caller sees it, e.g. fight.s:132 first-round initiative. */
uint8_t rng_getms(void);

/* perform_random_it enters with the caller's carry flag (math.s:328 adc
 * mud_seed); 0 matches a caller that did `clc`. */
void rng_set_carry(int carry);

/* math.s random_it: returns a1, the 16-bit remainder; A is its low byte. */
uint16_t rng_random_it(uint16_t range);
uint8_t rng_random_it_a(uint16_t range);

/* math.s perform_random_it: returns a3/a4, the 32-bit remainder. */
uint32_t rng_perform_random_it(uint32_t range);
uint32_t rng_perform_random_it_carry(uint32_t range, int carry);

#endif

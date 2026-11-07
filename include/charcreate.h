/* GMUD 3DS - original character attribute assignment.
 *
 * Port of ref/gmud/src/input.s adjust_attr (206-343) and the derived stats
 * in input_new (91-110). No libctru dependencies: this core also compiles
 * on the host for tools/charcreate_verify.py.
 *
 * Scope note (T5): the original input_new asks for a name, a password and
 * then the four adjustable stats. The port keeps 姓名 (input.s input_name)
 * but deliberately drops 密码 (input_pass / check_pwd): there is no file
 * login on the 3DS. An empty name falls back to 无名氏, which is what the
 * pre-name-input build always used (the original refuses an empty name and
 * bails out of input_new with input_error instead).
 */
#ifndef GMUD_CHARCREATE_H
#define GMUD_CHARCREATE_H

#include <stdbool.h>
#include <stdint.h>

/* input.s:53 MAX_ATTR equ 4; input.s:207 move #20,attr_str,#6.
 * The four adjustable innate stats always sum to 4*20 = 80. */
#define CC_ATTR_COUNT 4
#define CC_ATTR_INIT 20
#define CC_ATTR_MIN 10
#define CC_ATTR_MAX 30
#define CC_ATTR_TOTAL (CC_ATTR_COUNT * CC_ATTR_INIT)

/* input.s:92 cmp1 attr_str,#16 / bcs set_gender -> >=16 is male (0). */
#define CC_GENDER_STR_THRESHOLD 16
/* input.s:98/105 lm2 range,#21 - two draws from random_it(21). */
#define CC_RANDOM_RANGE 21

#define CC_RESULT_NAME_LEN 16   /* ACTOR_NAME_LEN */

/* input.s MAX_NAME = 8 bytes in GB2312 (four hanzi or eight ASCII chars).
 * UTF-8 needs 3 bytes per hanzi, so the equivalent cap is 12 bytes; the
 * software keyboard is additionally limited to CC_NAME_MAX_CHARS glyphs. */
#define CC_NAME_MAX_BYTES 12
#define CC_NAME_MAX_CHARS 8
#define CC_NAME_DEFAULT "无名氏"

enum
{
	CC_ATTR_STR = 0,
	CC_ATTR_DEX,
	CC_ATTR_INT,
	CC_ATTR_CON
};

enum
{
	CC_STAGE_ATTR = 0,
	CC_STAGE_DONE,
	CC_STAGE_CANCELLED
};

enum
{
	CC_NOTICE_NONE = 0,
	CC_NOTICE_RESET
};

/* What actor_stats_init() consumes. per/kar are derived, not adjustable. */
typedef struct
{
	char name[CC_RESULT_NAME_LEN];   /* UTF-8; default until name input exists */
	uint8_t gender;                  /* 0 = male, 1 = female */
	uint8_t attr[CC_ATTR_COUNT];     /* str, dex, int, con */
	uint8_t per;                     /* 相貌 */
	uint8_t kar;                     /* 福缘 */
} CharCreateResult;

typedef struct
{
	char name[CC_RESULT_NAME_LEN];   /* UTF-8; 无名氏 until the player types one */
	uint8_t attr[CC_ATTR_COUNT];
	uint8_t pointer;                 /* 1..MAX_ATTR, the highlighted row */
	uint8_t spare;                   /* input.s spare_pot = 80 - sum(attr) */
	uint8_t stage;
	uint8_t notice;
	uint8_t fromGameOver;            /* cancellation target (T5 flow) */
} CharCreate;

/* Public labels shown on the original adjust_attr screen. */
const char *cc_attr_name(int index);
const char *cc_gender_name(int gender);

void cc_init(CharCreate *cc);

/* input.s input_name / input_new:73. Accepts UTF-8, strips control chars
 * and surrounding spaces, caps the name at CC_NAME_MAX_BYTES and falls
 * back to 无名氏 when nothing is left. */
void cc_set_name(CharCreate *cc, const char *utf8);

/* input.s:242-263 up_func/down_func wrap the pointer through 1..4. */
void cc_move(CharCreate *cc, int delta);
void cc_set_pointer(CharCreate *cc, int pointer);

/* input.s:265-289 right_func (put a point back) / left_func (spend one).
 * The original key directions are counter-intuitive: RIGHT lowers the
 * selected stat, LEFT raises it. The C names follow the value change. */
void cc_dec(CharCreate *cc);
void cc_inc(CharCreate *cc);

/* input.s:297-302 enter_func: confirm only when spare_pot == 0. */
bool cc_confirm(CharCreate *cc);
/* input.s:305-307 esc_func cancels. The original exits to the WQX file
 * menu; here it restores the initial 20/20/20/20 sheet and stays on the
 * page so a cancelled run can still be completed. */
void cc_reset(CharCreate *cc);
void cc_clear_notice(CharCreate *cc);

/* Who opened the page: 0 = startup (cancel exits the app, like the WQX
 * file menu), 1 = 新的旅程 from the game-over screen (cancel returns). */
void cc_set_origin(CharCreate *cc, int fromGameOver);
/* input.s:305-307 esc_func: game_over_flag = 2 -> input_error. */
void cc_cancel(CharCreate *cc);

int cc_spare(const CharCreate *cc);

/* input.s:91-110. The two draws are the A register (low byte) of
 * random_it(21), in this order: per first, kar second. */
uint8_t cc_gender_from_str(uint8_t str);
uint8_t cc_per_from(uint8_t str, uint8_t draw);
uint8_t cc_kar_from(uint8_t draw);
void cc_finalize(const CharCreate *cc, uint8_t draw_per, uint8_t draw_kar,
                 CharCreateResult *out);

/* Baseline for save_read()'s v1/v2 fallback and NULL callers; it is no
 * longer used as a "new game" character. */
void cc_default_result(CharCreateResult *out);

#endif

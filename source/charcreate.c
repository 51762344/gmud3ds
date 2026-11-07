/* GMUD 3DS - input.s adjust_attr / input_new, pure integer core.
 * Compiled both into the 3DS build and into tools/charcreate_dump.c.
 */
#include "charcreate.h"

#include <string.h>

static const char *const s_attrNames[CC_ATTR_COUNT] =
{
	"膂力", "敏捷", "悟性", "根骨"
};

static const char *const s_genderNames[2] = { "男", "女" };

const char *cc_attr_name(int index)
{
	if (index < 0 || index >= CC_ATTR_COUNT)
		return "";
	return s_attrNames[index];
}

const char *cc_gender_name(int gender)
{
	return s_genderNames[gender ? 1 : 0];
}

/* Sequence length of a UTF-8 code point, 0 for invalid/unsupported bytes.
 * Four-byte planes are rejected: the 16x16 font cannot draw them. */
static int utf8_seq_len(const unsigned char *s)
{
	if (s[0] < 0x80)
		return 1;
	if (s[0] >= 0xC2 && s[0] <= 0xDF)
		return (s[1] & 0xC0) == 0x80 ? 2 : 0;
	if (s[0] >= 0xE0 && s[0] <= 0xEF)
		return ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) ? 3 : 0;
	return 0;
}

void cc_init(CharCreate *cc)
{
	if (!cc)
		return;
	memset(cc, 0, sizeof(*cc));
	for (int i = 0; i < CC_ATTR_COUNT; i++)
		cc->attr[i] = CC_ATTR_INIT;
	cc->pointer = 1;                 /* input.s:212 lm pointer,#1 */
	cc->spare = 0;                   /* input.s:211 lm spare_pot,#0 */
	cc->stage = CC_STAGE_ATTR;
	cc->notice = CC_NOTICE_NONE;
	strcpy(cc->name, CC_NAME_DEFAULT);
}

void cc_set_name(CharCreate *cc, const char *utf8)
{
	char out[CC_RESULT_NAME_LEN];
	size_t n = 0;
	int chars = 0;
	const unsigned char *p = (const unsigned char *)(utf8 ? utf8 : "");

	if (!cc)
		return;

	while (*p == ' ' || *p == '\t')
		p++;

	while (*p && chars < CC_NAME_MAX_CHARS)
	{
		int len = utf8_seq_len(p);
		if (len == 0)
			break;               /* invalid byte: stop instead of copying garbage */
		if (len == 1 && (*p < 0x20 || *p == 0x7F))
		{
			p++;
			continue;
		}
		if (n + (size_t)len > CC_NAME_MAX_BYTES)
			break;
		memcpy(out + n, p, (size_t)len);
		n += (size_t)len;
		p += len;
		chars++;
	}
	while (n > 0 && out[n - 1] == ' ')
		n--;
	out[n] = '\0';

	/* SWKBD can return a full-width space for "blank". */
	if (n == 3 && memcmp(out, "\xE3\x80\x80", 3) == 0)
		n = 0;

	/* input.s input_new rejects an empty name with input_error and never
	 * starts a run; the port keeps the pre-name-input default instead. */
	if (n == 0)
		strcpy(out, CC_NAME_DEFAULT);

	strcpy(cc->name, out);
}

void cc_move(CharCreate *cc, int delta)
{
	if (!cc || delta == 0)
		return;
	/* up_func: pointer--, 0 wraps to MAX_ATTR.
	 * down_func: pointer++, MAX_ATTR+1 wraps to 1. */
	int p = (int)cc->pointer + delta;
	while (p < 1)
		p += CC_ATTR_COUNT;
	while (p > CC_ATTR_COUNT)
		p -= CC_ATTR_COUNT;
	cc->pointer = (uint8_t)p;
	cc_clear_notice(cc);
}

void cc_set_pointer(CharCreate *cc, int pointer)
{
	if (!cc || pointer < 1 || pointer > CC_ATTR_COUNT)
		return;
	cc->pointer = (uint8_t)pointer;
	cc_clear_notice(cc);
}

void cc_dec(CharCreate *cc)
{
	if (!cc || cc->stage != CC_STAGE_ATTR)
		return;
	/* input.s right_func: cmp #10 / beq pot_rts; then A-1 and inc spare_pot. */
	uint8_t *v = &cc->attr[cc->pointer - 1];
	if (*v <= CC_ATTR_MIN)
	{
		cc_clear_notice(cc);
		return;
	}
	(*v)--;
	cc->spare++;
	cc_clear_notice(cc);
}

void cc_inc(CharCreate *cc)
{
	if (!cc || cc->stage != CC_STAGE_ATTR)
		return;
	/* input.s left_func: spare_pot == 0 or cur == 30 does nothing;
	 * otherwise A+1 and dec spare_pot. */
	if (cc->spare == 0)
	{
		cc_clear_notice(cc);
		return;
	}
	uint8_t *v = &cc->attr[cc->pointer - 1];
	if (*v >= CC_ATTR_MAX)
	{
		cc_clear_notice(cc);
		return;
	}
	(*v)++;
	cc->spare--;
	cc_clear_notice(cc);
}

bool cc_confirm(CharCreate *cc)
{
	if (!cc || cc->stage != CC_STAGE_ATTR || cc->spare != 0)
		return false;
	cc->stage = CC_STAGE_DONE;
	return true;
}

void cc_reset(CharCreate *cc)
{
	char name[CC_RESULT_NAME_LEN];

	if (!cc)
		return;
	strcpy(name, cc->name);
	cc_init(cc);
	/* 重来 only re-rolls the four stats; the typed name stays. */
	strcpy(cc->name, name);
	cc->notice = CC_NOTICE_RESET;
}

void cc_clear_notice(CharCreate *cc)
{
	if (cc)
		cc->notice = CC_NOTICE_NONE;
}

void cc_set_origin(CharCreate *cc, int fromGameOver)
{
	if (cc)
		cc->fromGameOver = fromGameOver ? 1 : 0;
}

void cc_cancel(CharCreate *cc)
{
	if (cc)
		cc->stage = CC_STAGE_CANCELLED;
}

int cc_spare(const CharCreate *cc)
{
	if (!cc)
		return 0;
	int sum = 0;
	for (int i = 0; i < CC_ATTR_COUNT; i++)
		sum += cc->attr[i];
	return CC_ATTR_TOTAL - sum;
}

uint8_t cc_gender_from_str(uint8_t str)
{
	/* input.s:91-96: ldx #0 / cmp1 attr_str,#16 / bcs set_gender / inx. */
	return str >= CC_GENDER_STR_THRESHOLD ? 0 : 1;
}

uint8_t cc_per_from(uint8_t str, uint8_t draw)
{
	/* input.s:98-103
	 *   lm2 range,#21 / jsr random_it   -> A = draw (0..20)
	 *   clc / adc #30                   -> A = 30 + draw, C = 0
	 *   sbc attr_str                    -> A - str - (1 - C), no SEC!
	 *   sta attr_per                    -> one byte, wraps at 256
	 * So the original is 29 + draw - str, not 30 + draw - str. */
	return (uint8_t)(29 + (int)draw - (int)str);
}

uint8_t cc_kar_from(uint8_t draw)
{
	/* input.s:105-109: clc / adc #10 / sta attr_kar (no stray borrow). */
	return (uint8_t)(10 + (int)draw);
}

void cc_finalize(const CharCreate *cc, uint8_t draw_per, uint8_t draw_kar,
                 CharCreateResult *out)
{
	if (!cc || !out)
		return;
	memset(out, 0, sizeof(*out));
	/* input.s:73 move scrbuf,man_name,#MAX_NAME */
	strcpy(out->name, cc->name[0] ? cc->name : CC_NAME_DEFAULT);
	out->gender = cc_gender_from_str(cc->attr[CC_ATTR_STR]);
	for (int i = 0; i < CC_ATTR_COUNT; i++)
		out->attr[i] = cc->attr[i];
	out->per = cc_per_from(cc->attr[CC_ATTR_STR], draw_per);
	out->kar = cc_kar_from(draw_kar);
}

void cc_default_result(CharCreateResult *out)
{
	CharCreate cc;
	cc_init(&cc);
	cc_finalize(&cc, 0, 0, out);
	/* The fallback sheet keeps the old neutral 20/20 values instead of
	 * consuming two PRNG draws; it is only used for v1/v2 save migration. */
	out->per = CC_ATTR_INIT;
	out->kar = CC_ATTR_INIT;
	/* attr_str = 20 >= 16, so the fallback gender is male as before. */
}

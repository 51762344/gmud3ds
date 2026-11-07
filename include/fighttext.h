#ifndef FIGHTTEXT_H
#define FIGHTTEXT_H

#include <stdbool.h>

/* Original battle strings from text_addr.s / kf_data.gb / string2.s /
 * perform.txt (data/fighttext.bin, FTXT v5; layout in source/fighttext.c).
 * Only standard C headers so the loader/selection code can be host-verified.
 * Templates use the original format_string placeholders:
 *   $N attacker name, $n defender name, $l / $1 random body part,
 *   $w weapon name, $$ a literal '$'.
 * Kungfu ids are the engine's 0-based SK_* values, which match the original
 * kf_addr_tbl indices (0=force, 1=bare, 2=sword, 3=blade, 4=club, 5=staff,
 * 6=whip, 7=dodge, 11..40 = baguad..menghu, 41 = xi has no templates). */

/* v5 entry id map. 2000..2072 is the complete perform.txt label table,
 * addressed by the original perform_id.h index (show_text's a9 value);
 * 2073..2099 stays free. v4's duplicated 3000+ primary/branch slots are gone. */
#define FT_ID_PERFORM_LABEL 2000
#define FT_PERFORM_LABELS   73
#define FT_PERFORM_MAX      24   /* hello.s perform_tbl entries */
#define FT_ID_DAMAGE       2100  /* 2100 + damage_type*8 + level */
#define FT_ID_DAMAGE0      2148
#define FT_ID_RESULT_TIP   2149
#define FT_ID_STATUS       2200  /* 2200..2210 */
#define FT_ID_EFF_STATUS   2211  /* 2211..2221 */
#define FT_DAMAGE_TYPES    6     /* string2.s MAX_TYPE */
#define FT_DAMAGE_LEVELS   8
#define FT_STATUS_LEVELS   11

/* perform.txt / h/perform_id.h label indices, in perform_tbl dw order.
 * The names keep the original (misspelled) "sucess" spelling. Only the
 * simplified `if scode` branch is packed; the `else` branch is its
 * traditional-chinese mirror. Index 255 means "no message". */
#define PF_MSG_ZHANGDAO_SUCESS              0  /* zhangdao_sucess_msg */
#define PF_MSG_ZHANGDAO_OVER                1  /* zhangdao_over_msg */
#define PF_MSG_DAOYING_SUCESS               2  /* daoying_sucess_msg */
#define PF_MSG_SANHUA_SUCESS                3  /* sanhua_sucess_msg */
#define PF_MSG_SANHUA_OVER                  4  /* sanhua_over_msg */
#define PF_MSG_LIULANG_SUCESS               5  /* liulang_sucess_msg */
#define PF_MSG_LUOYING_SUCESS               6  /* luoying_sucess_msg */
#define PF_MSG_LUOYING_SHOOT                7  /* luoying_shoot_msg */
#define PF_MSG_LUOYING_DODGE                8  /* luoying_dodge_msg */
#define PF_MSG_LUOYING_SHOOT1               9  /* luoying_shoot_msg1 */
#define PF_MSG_LUOYING_DODGE1              10  /* luoying_dodge_msg1 */
#define PF_MSG_FEIZHI_SUCESS               11  /* feizhi_sucess_msg */
#define PF_MSG_FEIZHI_SHOOT                12  /* feizhi_shoot_msg */
#define PF_MSG_FEIZHI_DODGE                13  /* feizhi_dodge_msg */
#define PF_MSG_HONGLIAN_SUCESS             14  /* honglian_sucess_msg */
#define PF_MSG_LEIDONG_SUCESS              15  /* leidong_sucess_msg */
#define PF_MSG_FENSHEN_SUCESS              16  /* fenshen_sucess_msg */
#define PF_MSG_FENSHEN_OVER                17  /* fenshen_over_msg */
#define PF_MSG_YIANMU_SUCESS               18  /* yianmu_sucess_msg */
#define PF_MSG_YIANMU_OVER                 19  /* yianmu_over_msg */
#define PF_MSG_LIANZHAN_SUCESS             20  /* lianzhan_sucess_msg */
#define PF_MSG_YIDAO_SUCESS                21  /* yidao_sucess_msg */
#define PF_MSG_CHAN_SUCESS                 22  /* chan_sucess_msg */
#define PF_MSG_CHAN_SHOOT                  23  /* chan_shoot_msg */
#define PF_MSG_CHAN_DODGE                  24  /* chan_dodge_msg */
#define PF_MSG_LIAN_SUCESS                 25  /* lian_sucess_msg */
#define PF_MSG_LIAN_OVER                   26  /* lian_over_msg */
#define PF_MSG_TAOYUE_SUCESS               27  /* taoyue_sucess_msg */
#define PF_MSG_JI_SUCESS                   28  /* ji_sucess_msg */
#define PF_MSG_JI_SHOOT                    29  /* ji_shoot_msg */
#define PF_MSG_JI_SHOOT1                   30  /* ji_shoot_msg1 */
#define PF_MSG_JI_DODGE                    31  /* ji_dodge_msg */
#define PF_MSG_LUANHUAN_SUCESS             32  /* luanhuan_sucess_msg */
#define PF_MSG_LUANHUAN_SHOOT              33  /* luanhuan_shoot_msg */
#define PF_MSG_LUANHUAN_DODGE              34  /* luanhuan_dodge_msg */
#define PF_MSG_YINYANG_SUCESS              35  /* yinyang_sucess_msg */
#define PF_MSG_YINYANG_ATTACK              36  /* yinyang_attack_msg */
#define PF_MSG_YINYANG_SHOOT               37  /* yinyang_shoot_msg */
#define PF_MSG_YINYANG_DODGE               38  /* yinyang_dodge_msg */
#define PF_MSG_ZHEN_SUCESS                 39  /* zhen_sucess_msg */
#define PF_MSG_ZHEN_DODGE                  40  /* zhen_dodge_msg */
#define PF_MSG_ZHEN_SHOOT                  41  /* zhen_shoot_msg */
#define PF_MSG_ZHEN_SHOOT1                 42  /* zhen_shoot_msg1 */
#define PF_MSG_ZHEN_DODGE1                 43  /* zhen_dodge_msg1 */
#define PF_MSG_BINGXIN_SUCESS              44  /* bingxin_sucess_msg */
#define PF_MSG_BINGXIN_OVER                45  /* bingxin_over_msg */
#define PF_MSG_LIUCHU_SUCESS               46  /* liuchu_sucess_msg */
#define PF_MSG_SHENGUI_SUCESS              47  /* shengui_sucess_msg */
#define PF_MSG_SHENGUI_SHOOT               48  /* shengui_shoot_msg */
#define PF_MSG_SHENGUI_DODGE               49  /* shengui_dodge_msg */
#define PF_MSG_YIANMU_FAIL                 50  /* yianmu_fail_msg */
#define PF_MSG_HEAL_ENABLE                 51  /* heal_enable_msg */
#define PF_MSG_HEAL_MAXNEILI               52  /* heal_maxneili_msg */
#define PF_MSG_HEAL_NOHURT                 53  /* heal_nohurt_msg */
#define PF_MSG_HEAL_BADHURT                54  /* heal_badhurt_msg */
#define PF_MSG_HEAL_SUCESS                 55  /* heal_sucess_msg */
#define PF_MSG_HEAL_OVER                   56  /* heal_over_msg */
#define PF_MSG_HEAL_LEVEL                  57  /* heal_level_msg */
#define PF_MSG_DAZUO_LEVEL                 58  /* dazuo_level_msg */
#define PF_MSG_HEAL_NOEFF                  59  /* heal_noeff_msg */
#define PF_MSG_HEAL_NOEFF1                 60  /* heal_noeff_msg1 */
#define PF_MSG_RECOVER_OVER                61  /* recover_over_msg */
#define PF_MSG_RECOVER_HP                  62  /* recover_hp_msg */
#define PF_MSG_JIALI_FLOW                  63  /* jiali_flow_msg */
#define PF_MSG_LEVEL                       64  /* level_msg */
#define PF_MSG_SUIT                        65  /* suit_msg */
#define PF_MSG_NEILI                       66  /* neili_msg */
#define PF_MSG_NOT_NEILI                   67  /* not_neili_msg */
#define PF_MSG_USING                       68  /* using_msg */
#define PF_MSG_BUSY                        69  /* busy_msg */
#define PF_MSG_MAN_BUSY                    70  /* man_busy_msg */
#define PF_MSG_NPC_BUSY                    71  /* npc_busy_msg */
#define PF_MSG_NOT_STR                     72  /* not_str_msg */
#define PF_MSG_NONE                       255

#define FT_BASE_BARE  100       /* 5 templates, weighted */
#define FT_BASE_SWORD 200       /* 3 */
#define FT_BASE_BLADE 210       /* 3 */
#define FT_BASE_CLUB  220       /* 3 */
#define FT_BASE_STAFF 230       /* 3 */
#define FT_BASE_WHIP  240       /* 1 */
#define FT_BASE_DODGE 300       /* 5, defender-flavoured ("但是被$n…") */

#define FT_RENDER_MAX 320       /* rendered sentence buffer */

bool fight_text_load(const char *romfs_path);
bool fight_move_stats(int kfId, int index, int *level, int *ap, int *dp, int *pp, int *damage, int *force);
void fight_text_free(void);

/* Body parts for $l / $1 ("头部", "右手", ...). fight.s rolls limb_flag
 * once at the start of each attack and every template in that attack shares
 * it, so the caller sets it instead of rolling inside fight_render(). */
int fight_limb_count(void);
const char *fight_limb(int i);
void fight_set_limb(int index);

/* Raw template for a packed entry id; NULL when absent. */
const char *fight_template(int id);
/* string.s is_basic_msg's damage_type byte; 255 when the entry has none. */
int fight_template_attr(int id);
/* Number of packed entries in [base, base+9). */
int fight_template_count(int base);
/* Weapon sub (0-based original weapon_type: 1=blade 2=club 6=sword 7=staff
 * 9=whip) -> template base; 0 = no table, fall back to bare hands. */
int fight_weapon_base(int weaponSub);

int fight_dodge_count(void);
const char *fight_dodge(int i);

/* Kungfu move descriptions (0-based kfId; dodge/force kungfu carry 0). */
int fight_kf_moves(int kfId);
const char *fight_kf_move(int kfId, int i);
/* Full perform.txt label table. `index` is the PF_MSG_* value (the original
 * perform_id.h / perform_tbl index), not an entry id. */
const char *fight_perform_label(int index);
/* u8[24] table from the pack: performId -> its primary label index, as
 * derived from hello.s perform_tbl and perform.s show_text. */
int fight_perform_primary(int performId);

/* string2.s get_damage_msg / get_status_msg / get_result_msg. The returned
 * strings still carry the original $n placeholders; pass fight_render() over
 * them. damagePoint is the 16-bit damage_point, woundFlag is a9 (nonzero
 * selects eff_status_tbl), currentHp/maxHp feed percent() for the status
 * line. fight_percent() is exposed for host regression. */
int fight_percent(int value, int maxValue);
const char *fight_damage_msg(int damageType, int damagePoint);
const char *fight_status_msg(int ratio, bool wounded);
void fight_damage_result(char *out, int size, int damageType, int damagePoint,
                         int woundFlag, int currentHp, int maxHp);

/* Renders a template with names/parts/weapon substituted. */
void fight_render(char *out, int size, const char *tpl,
                  const char *attacker, const char *defender,
                  const char *weapon);

/* The five substitutable variables of string.s format_string that the
 * perform.txt messages use. A NULL member renders as an empty string, the
 * same way the original prints an unset variable. */
typedef struct
{
	const char *attacker;   /* $N  self  (obj_flag) */
	const char *defender;   /* $n  other (obj_flag) */
	const char *weapon;     /* $w  weapon (obj_flag) */
	const char *kungfu;     /* $k  kungfu name (kf_id) */
	const char *perform;    /* $p  perform name (perform_id) */
} FightNames;

/* fight_render with all five variables resolved. */
void fight_render_names(char *out, int size, const char *tpl,
                         const FightNames *names);

/* Renders one perform.txt label (a PF_MSG_* index, not an entry id). */
void fight_perform_msg(char *out, int size, int label, const FightNames *names);

/* Renders one perform.txt label with only $N (the player's own name) set —
 * the 疗伤 / 吸气 / 加力 / 打坐 messages all use that single variable. */
void fight_perform_msg_self(char *out, int size, int label, const char *selfName);

/* Same, but with a runtime 16-bit number appended right after the text.
 *
 * perform.txt jiali_flow_msg is `db '你目前加力上限为',2 / dw man_force,0`:
 * the ',2' control code (string.s format_string: 2 = "2byte整数") makes
 * format_string print the word stored at the given address immediately after
 * the literal text. The packer keeps only the quoted string, so the number is
 * inserted here — this is the original's runtime value insertion. */
void fight_perform_msg_num(char *out, int size, int label, int value,
                            const FightNames *names);

#endif

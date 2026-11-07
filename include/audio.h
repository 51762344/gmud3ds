#ifndef AUDIO_H
#define AUDIO_H

#include "gmud.h"

/* Audio interface.  This build ships two one-shot effects on the 3DS DSP
 * (SFX_DOOR and SFX_CONFIRM, see audio.c); the remaining ids and the music
 * entry points are still silent so more clips can be added without touching
 * a single caller. */

/* Sound effect ids.  Kept as the vocabulary the game already uses at its
 * trigger points; a future bank decides which of them actually sound. */
enum
{
	SFX_STEP = 0,
	SFX_CURSOR,
	SFX_CONFIRM,
	SFX_CANCEL,
	SFX_HIT,
	SFX_CRIT,
	SFX_HURT,
	SFX_HEAL,
	SFX_LEVELUP,
	SFX_VICTORY,
	SFX_DEFEAT,
	SFX_DOOR,
	SFX_PICKUP,
	SFX_QUEST,
	SFX_ERROR,
	SFX_ENCOUNTER,
	SFX_COIN,
	SFX_PAGE,
	SFX_GONG,
	SFX_COUNT
};

/* Background music ids, one per screen family (see bgm_for_mode in main.c). */
enum
{
	BGM_NONE = 0,
	BGM_FIELD,
	BGM_BATTLE,
	BGM_MENU,
	BGM_COUNT
};

/* Starts the DSP backend.  Returns false when it is unavailable (no
 * sdmc:/3ds/dspfirm.cdc) or the WAV assets are missing, in which case the
 * game keeps running silently.  Callers must stay safe on false. */
bool audio_init(void);
void audio_fini(void);
bool audio_ready(void);
/* Why the sound is unavailable; empty when everything is fine. */
const char *audio_status(void);

/* One-shot effect.  Ids without an asset are dropped silently, so this is
 * always safe to call from gameplay code. */
void audio_play(int sfx);

/* Music requests are accepted but silent in this build (no BGM assets). */
void audio_bgm(int bgm);
void audio_bgm_stop(void);

/* Per-frame housekeeping; unused by the effects-only backend today. */
void audio_update(float dt);

/* Volumes are percentages (0..100), stored per channel group so a future
 * backend can turn the music down without touching the effects. */
void audio_set_bgm_volume(int percent);
void audio_set_sfx_volume(int percent);
int audio_bgm_volume(void);
int audio_sfx_volume(void);

/* True when a music track is loaded and playing. */
bool audio_bgm_playing(void);

#endif

/* Audio runtime: two one-shot effects on the 3DS DSP.
 *
 * The original WQX build had no sound, so these effects are the 3DS port's
 * own addition.  They are plain mono PCM16 RIFF/WAVE files built by
 * tools/sfx_build.py and shipped under romfs:/audio/; this file only loads
 * them into linear memory and hands them to an ndsp channel.
 *
 * Scope is deliberately small: only SFX_DOOR (stepping onto a door tile) and
 * SFX_CONFIRM (menu confirm / soft-keyboard OK) have assets.  Every other
 * SFX_* id falls through to silence, so adding one later is a line in
 * sfx_path() plus a WAV in data/audio/ - no caller changes.
 *
 * Volume is a compile-time constant, not a setting: the port has no audio
 * options UI and no settings.cfg entry for sound, on purpose.
 *
 * ndspInit() needs the DSP firmware at sdmc:/3ds/dspfirm.cdc, or the hb:ndsp
 * port Luma3DS provides while running a 3dsx.  Without it the game runs
 * silently; audio_status() then says why for any caller that wants to show it.
 */
#include "audio.h"

#include <stdio.h>
#include <string.h>

/* One DSP channel per effect, so two clips can overlap (e.g. confirming while
 * the door sound is still ringing).  Raise this when more ids get assets. */
#define AUDIO_CHN_MAX 2

typedef struct
{
	bool loaded;
	void *alloc;      /* linear allocation holding the whole file image */
	s16 *data;        /* PCM payload inside that image */
	u32 frames;
	u32 rate;
	int channel;      /* ndsp channel, -1 when the clip failed to load */
} AudioClip;

#define AUDIO_SFX_VOLUME 80

static bool s_ready;
static char s_status[128] = "音频未初始化";
static int s_bgmVol = 70;
static int s_sfxVol = AUDIO_SFX_VOLUME;

static AudioClip s_sfx[SFX_COUNT];
static ndspWaveBuf s_buf[AUDIO_CHN_MAX];
static int s_chnUsed;

/* ------------------------------------------------------------------ helpers */

static u16 rd16(const u8 *p)
{
	return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Minimal RIFF/WAVE reader.  Only mono PCM16 is accepted; anything else is
 * rejected so a bad asset can never reach the DSP. */
static bool wav_parse(const u8 *img, u32 size, u32 *rate, u32 *channels,
                      u32 *bits, u32 *dataOff, u32 *dataSize)
{
	bool haveFmt = false;

	*rate = 0;
	*channels = 0;
	*bits = 0;
	*dataOff = 0;
	*dataSize = 0;

	if (size < 12 || memcmp(img, "RIFF", 4) != 0 || memcmp(img + 8, "WAVE", 4) != 0)
		return false;

	u32 pos = 12;
	while (pos + 8 <= size)
	{
		const u8 *id = img + pos;
		u32 chunk = rd32(img + pos + 4);
		u32 body = pos + 8;
		if (chunk > size - body)
			chunk = size - body;

		if (memcmp(id, "fmt ", 4) == 0 && chunk >= 16)
		{
			u16 fmt = rd16(img + body);
			*channels = rd16(img + body + 2);
			*rate = rd32(img + body + 4);
			*bits = rd16(img + body + 14);
			/* 1 = PCM, 0xFFFE = extensible (still a PCM payload). */
			if (fmt != 1 && fmt != 0xFFFE)
				return false;
			haveFmt = true;
		}
		else if (memcmp(id, "data", 4) == 0)
		{
			*dataOff = body;
			*dataSize = chunk;
		}

		pos = body + chunk + (chunk & 1u);
	}

	return haveFmt && *dataOff != 0 && *dataSize >= 2;
}

/* Loads one WAV into linear memory.  The PCM payload stays inside the file
 * image, so a clip costs a single allocation. */
static bool clip_load(AudioClip *c, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return false;
	}
	long size = ftell(f);
	if (size <= 44 || size > (4L * 1024L * 1024L))
	{
		fclose(f);
		return false;
	}
	rewind(f);

	void *img = linearAlloc((size_t)size);
	if (!img)
	{
		fclose(f);
		return false;
	}
	if (fread(img, 1, (size_t)size, f) != (size_t)size)
	{
		fclose(f);
		linearFree(img);
		return false;
	}
	fclose(f);

	u32 rate = 0, channels = 0, bits = 0, dataOff = 0, dataSize = 0;
	if (!wav_parse((const u8 *)img, (u32)size, &rate, &channels, &bits,
	               &dataOff, &dataSize) ||
	    channels != 1 || bits != 16 || rate < 4000 || rate > 48000)
	{
		linearFree(img);
		return false;
	}

	/* The DSP reads the samples by DMA, so the payload gets its own
	 * linear allocation: it starts 32-byte aligned (linearAlloc guarantees
	 * that) no matter how large the RIFF header was, and the cache has to
	 * be clean before the DSP sees it. */
	u32 pcmBytes = dataSize & ~1u;
	s16 *pcm = (s16 *)linearAlloc((size_t)pcmBytes);
	if (!pcm)
	{
		linearFree(img);
		return false;
	}
	memcpy(pcm, (const u8 *)img + dataOff, pcmBytes);
	linearFree(img);
	DSP_FlushDataCache(pcm, (size_t)pcmBytes);

	c->alloc = pcm;
	c->data = pcm;
	c->frames = pcmBytes / 2;
	c->rate = rate;
	c->loaded = true;
	return true;
}

static void clip_free(AudioClip *c)
{
	if (!c->loaded)
		return;
	c->loaded = false;
	c->data = NULL;
	c->frames = 0;
	linearFree(c->alloc);
	c->alloc = NULL;
}

/* Which ids have a file under romfs:/audio/ (see data/audio/). */
static const char *sfx_path(int sfx)
{
	switch (sfx)
	{
	case SFX_DOOR:    return "romfs:/audio/sfx_door.wav";
	case SFX_CONFIRM: return "romfs:/audio/sfx_confirm.wav";
	default:          return NULL;
	}
}

static void sfx_apply_mix(int ch)
{
	float v = (float)s_sfxVol / 100.0f;
	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = v;
	mix[1] = v;
	mix[2] = v;
	mix[3] = v;
	ndspChnSetMix(ch, mix);
}

static void sfx_configure(int ch, u32 rate)
{
	ndspChnReset(ch);
	ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
	ndspChnSetRate(ch, (float)rate);
	ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
	sfx_apply_mix(ch);
}

/* ------------------------------------------------------------------- api */

bool audio_init(void)
{
	if (s_ready)
		return true;

	Result rc = ndspInit();
	if (R_FAILED(rc))
	{
		snprintf(s_status, sizeof(s_status),
		         "DSP 初始化失败（0x%08lX），缺 sdmc:/3ds/dspfirm.cdc",
		         (unsigned long)rc);
		return false;
	}

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(2);
	ndspSetMasterVol(1.0f);

	s_chnUsed = 0;
	for (int i = 0; i < SFX_COUNT; i++)
	{
		s_sfx[i].channel = -1;
		const char *path = sfx_path(i);
		if (!path)
			continue;
		if (!clip_load(&s_sfx[i], path) || s_chnUsed >= AUDIO_CHN_MAX)
		{
			clip_free(&s_sfx[i]);
			continue;
		}
		s_sfx[i].channel = s_chnUsed;
		sfx_configure(s_chnUsed, s_sfx[i].rate);
		s_chnUsed++;
	}

	if (s_chnUsed == 0)
	{
		ndspExit();
		snprintf(s_status, sizeof(s_status),
		         "缺少音频资源 romfs:/audio/sfx_*.wav");
		return false;
	}

	s_ready = true;
	s_status[0] = '\0';
	return true;
}

void audio_fini(void)
{
	if (!s_ready)
		return;

	for (int i = 0; i < SFX_COUNT; i++)
	{
		AudioClip *c = &s_sfx[i];
		if (c->channel >= 0)
			ndspChnWaveBufClear(c->channel);
		clip_free(c);
		c->channel = -1;
	}
	s_ready = false;
	s_chnUsed = 0;
	ndspExit();
}

bool audio_ready(void)
{
	return s_ready;
}

const char *audio_status(void)
{
	return s_ready ? "" : s_status;
}

void audio_play(int sfx)
{
	if (!s_ready || sfx < 0 || sfx >= SFX_COUNT)
		return;

	AudioClip *c = &s_sfx[sfx];
	if (!c->loaded || c->channel < 0)
		return;

	int ch = c->channel;
	ndspWaveBuf *buf = &s_buf[ch];
	memset(buf, 0, sizeof(*buf));
	buf->data_pcm16 = c->data;
	buf->nsamples = c->frames;
	buf->looping = false;

	/* Reset first: retriggering an effect must start from the top instead of
	 * being swallowed while the previous copy is still on the channel. */
	sfx_configure(ch, c->rate);
	ndspChnWaveBufAdd(ch, buf);
}

/* Music is not implemented in this build; the calls stay no-ops so the main
 * loop can keep requesting a track per mode. */
void audio_bgm(int bgm)
{
	(void)bgm;
}

void audio_bgm_stop(void)
{
}

void audio_update(float dt)
{
	(void)dt;
}

void audio_set_bgm_volume(int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	s_bgmVol = percent;
}

void audio_set_sfx_volume(int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	s_sfxVol = percent;
	if (!s_ready)
		return;
	for (int i = 0; i < SFX_COUNT; i++)
	{
		if (s_sfx[i].loaded && s_sfx[i].channel >= 0)
			sfx_apply_mix(s_sfx[i].channel);
	}
}

int audio_bgm_volume(void)
{
	return s_bgmVol;
}

int audio_sfx_volume(void)
{
	return s_sfxVol;
}

bool audio_bgm_playing(void)
{
	return false;
}

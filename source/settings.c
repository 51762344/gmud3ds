/* Persistent shell settings: theme.
 *
 * The original game had no theme, so this is the 3DS port's own.  It is kept
 * out of the save slots on purpose: a player who switches the palette must not
 * have to write a save.
 *
 * Audio deliberately has no settings: the original build was silent, and the
 * port's two effects are fixed at AUDIO_SFX_VOLUME (see audio.c).  Older
 * settings.cfg files may still carry `music=` / `sfx=` lines; they are ignored
 * on load and dropped on the next save.
 *
 * Format is plain text so a corrupted file can be inspected and edited on a PC:
 *
 *   theme=0
 */
#include "settings.h"

#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_theme = SETTINGS_THEME_DEFAULT;

static int clamp_int(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void apply(void)
{
	/* gfx_set_theme() rebuilds the tile/UI atlas; before gfx_load_tiles() it
	 * fails cleanly, so calling it early is harmless. */
	gfx_set_theme(s_theme);
}

bool settings_load(void)
{
	FILE *f = fopen(SETTINGS_PATH, "rb");
	if (!f)
		return false;

	int theme = s_theme;
	bool any = false;
	char line[80];
	while (fgets(line, sizeof(line), f))
	{
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		int value = atoi(eq + 1);

		if (strcmp(line, "theme") == 0)
		{
			theme = clamp_int(value, 0, 2);
			any = true;
		}
	}
	fclose(f);

	/* Only apply what was actually present: an empty or garbled file keeps
	 * the default instead of forcing theme 0 through a zeroed parse. */
	if (any)
		s_theme = theme;
	apply();
	return any;
}

bool settings_save(void)
{
	char tmp[] = "sdmc:/3ds/gmud/settings.tmp";

	FILE *f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = fprintf(f, "theme=%d\n", s_theme) > 0;
	ok = (fclose(f) == 0) && ok;
	if (!ok)
	{
		remove(tmp);
		return false;
	}

	remove(SETTINGS_PATH);
	if (rename(tmp, SETTINGS_PATH) != 0)
	{
		remove(tmp);
		return false;
	}
	return true;
}

/* The setter only touches the live state; callers persist at a natural point
 * (the theme switch does it immediately).  Rewriting the file per key press
 * would stutter the game on a slow SD card. */
void settings_set_theme(int theme)
{
	s_theme = clamp_int(theme, 0, 2);
	gfx_set_theme(s_theme);
}

int settings_theme(void) { return s_theme; }

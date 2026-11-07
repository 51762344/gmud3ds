#ifndef SETTINGS_H
#define SETTINGS_H

#include "gmud.h"

/* Player-facing options that outlive a session (sdmc:/3ds/gmud/settings.cfg).
 * The save slots keep the run data; this file only holds the shell settings,
 * so switching the theme never touches a save.
 *
 * The 3DS port's own addition: the original WQX build had none.  Audio has no
 * entry here on purpose - see settings.c. */
#define SETTINGS_PATH "sdmc:/3ds/gmud/settings.cfg"

#define SETTINGS_THEME_DEFAULT 0     /* 原味绿 */

/* Reads settings.cfg and applies the theme.  Returns false when the file does
 * not exist yet (first boot); the default stays in effect. */
bool settings_load(void);
/* Writes the current theme.  Silently fails without an SD card. */
bool settings_save(void);

/* Clamps and applies immediately; does NOT write the file.  Persist with
 * settings_save() at a natural point (the theme switch does). */
void settings_set_theme(int theme);

int settings_theme(void);

#endif

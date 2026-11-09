/**
 * Papaya - 3DS Music Player
 * Where the app keeps its files on the SD card.
 */

#ifndef ctrmus_paths_h
#define ctrmus_paths_h

/* Settings, theme and tag cache */
#define PAPAYA_DIR		"sdmc:/3ds/papaya"

/* Where they lived before the rename to Papaya (ctrmus). The first run
 * copies settings.cfg and themes/theme.cfg over; the tag cache is simply
 * rebuilt. Nothing in the old folder is changed or removed. */
#define OLD_CTRMUS_DIR	"sdmc:/3ds/ctrmus"

#endif

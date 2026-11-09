/**
 * ctrmus - 3DS Music Player
 * Screen brightness (both screens), in the system's own five levels.
 *
 * Uses gsp::Lcd SetBrightness, which takes the HOME Menu's levels 1-5 and
 * maps them through the console's own calibration, so it can never drive
 * the backlight harder than the system itself does (unlike SetBrightnessRaw).
 * The level in effect when the app started is restored on exit.
 */

#ifndef ctrmus_brightness_h
#define ctrmus_brightness_h

#include <stdbool.h>

void brightnessInit(void);

/** Restore the brightness the system had when the app started. */
void brightnessExit(void);

/**
 * Change the brightness by `delta` levels (clamped to 1-5).
 *
 * \return	The new brightness in percent (20-100), or -1 if it can't be
 *			changed on this console.
 */
int brightnessStep(int delta);

/** Set the level (1-5, clamped). \return percent, or -1. */
int brightnessSetLevel(int level);

/** Current level (1-5), or -1 if unknown. */
int brightnessLevel(void);

/** Current brightness in percent (20-100), or -1 if unknown. */
int brightnessPercent(void);

#endif

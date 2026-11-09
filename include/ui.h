/**
 * ctrmus - 3DS Music Player
 * Screen layout, splash screen and the top-screen "Now Playing" panel.
 *
 * UI PROTOTYPE: only the bottom screen is a PrintConsole (browser + menu,
 * 40x30). The top screen is software-rendered by topscreen.c into an
 * off-screen canvas (console font + real graphics) and copied to the
 * double-buffered top framebuffer by uiPresentTop() once per frame.
 */

#ifndef ctrmus_ui_h
#define ctrmus_ui_h

#include <3ds.h>
#include <stdbool.h>

#include "themes.h"

struct browser;

struct screens
{
	PrintConsole	bottom;
};

/**
 * Initialise the bottom console and the top-screen canvas, and apply the
 * theme. Leaves the bottom console selected.
 */
void uiInit(struct screens* s, const struct theme_t* theme,
            bool mcuAvailable);

/**
 * The browser whose folder is shown as the "playlist". Set once it exists.
 */
void uiSetBrowser(struct browser* b);

/**
 * Re-apply a theme: clears the bottom console, redraws the top screen.
 */
void uiApplyTheme(struct screens* s, const struct theme_t* theme);

/**
 * Something the top screen shows changed: redraw it (once, on the next
 * uiPresentTop()). Does nothing before the Now Playing screen is up.
 */
void uiRequestRedraw(void);

/**
 * Change the screen brightness by `delta` levels and redraw.
 */
void uiBrightnessStep(int delta);

/**
 * Draw the splash screen ("Press A to start").
 */
void uiShowSplash(struct screens* s);

/**
 * Redraw the top-screen Now Playing screen from the current player state.
 */
void uiDrawNowPlaying(struct screens* s);

/**
 * Copy the top-screen canvas to the top framebuffer if it changed (and
 * pick up newly loaded cover art). Call once per frame, before
 * gfxFlushBuffers()/gfxSwapBuffers().
 */
void uiPresentTop(void);

/** The "placeholder art only" setting changed: drop or load the cover. */
void uiArtSettingChanged(void);

struct botCanvas;
struct topColors;

/**
 * The canvas the pixel-drawn bottom pages (queue editor, search) draw
 * into, and the theme's colours for it.
 */
void uiBottomCanvas(struct botCanvas* c, struct topColors* col);

/** Show the bottom canvas (copies it over the console's screen). */
void uiBottomPresent(void);

#endif

/**
 * ctrmus - 3DS Music Player
 * Value entry: type an exact number for an effect (Menu > Effects, Y on a
 * slider). Same on-screen keyboard as search (bottomscreen.c draws it);
 * digits and '.' only. Done (Y, or the "done" key) sets the value, clamped
 * to the effect's limits, and goes back to the menu. B on an empty field
 * or SELECT cancels. The sliders only have steps (speed 0.1x, spatial
 * 10%...); a typed value in between is kept, and the next step with the
 * D-pad snaps to the neighbouring step in that direction (0.99x, Right ->
 * 1.0x), see menu.c.
 *
 * The playback keys keep working, like on every page.
 */

#ifndef ctrmus_valueentry_h
#define ctrmus_valueentry_h

#include <3ds.h>
#include <stdbool.h>

#include "page.h"

/* What a typed value goes to (the menu's effect sliders) */
enum valueTarget
{
	VT_SPEED,
	VT_DISTORTION,
	VT_BITDEPTH,
	VT_SAMPLERATE,
	VT_SPATIAL,
	VT_REVERB,
	VT_REVERB_TIME,
	VT_COUNT
};

#define VALUE_TEXT_MAX	8

struct valueEntry
{
	enum valueTarget	what;
	char				text[VALUE_TEXT_MAX + 1];
	int					keyRow, keyCol;
	int					pressedKey;		/* stylus on a key, -1 */
	bool				dirty;
	bool				caret;
	u64					caretMs;
	struct keyRepeat	dirRepeat;
	struct keyRepeat	keyHold;		/* A, B or the stylus held on a key */
};

/** Open the page for `what` (draws it on the next valueEntryUpdate). */
void valueEntryOpen(struct valueEntry* s, enum valueTarget what);

/** One frame of input. Returns PAGE_TO_MENU when done or cancelled. */
enum page_result valueEntryInput(struct valueEntry* s, u32 kDown, u32 kHeld);

/** Every frame while open: caret blink, redraw. */
void valueEntryUpdate(struct valueEntry* s);

#endif

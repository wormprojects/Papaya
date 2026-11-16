/**
 * ctrmus - 3DS Music Player
 * The equalizer page (Menu > Effects > Equalizer...), drawn by
 * bottomscreen.c (eqDraw): a preamp and 10 bands as vertical bars.
 *
 *   Left/Right  pick the preamp, a band or a button (Preset, Auto
 *               preamp, Flat)
 *   Up/Down     change the picked one by 1 dB (a typed 2.5 dB goes to 3 or
 *               2, not 3.5); on Preset, the next/previous preset
 *   A           put the picked bar back to 0 dB; on a button, press it
 *   Y           type an exact dB value (valueentry.c, then back here)
 *   stylus      tap or drag a bar to set it; tap a button
 *   B / SELECT  back to the menu
 *
 * The preamp (0 to -30 dB) comes first in the chain: lower it by as much
 * as the bands boost and the sound can't clip, then raise the 3DS volume.
 * The page shows how far the loudest frequency is lifted after the
 * preamp ("loudest point"); Auto preamp sets the preamp to cancel it.
 */

#ifndef ctrmus_eqedit_h
#define ctrmus_eqedit_h

#include <3ds.h>
#include <stdbool.h>

#include "page.h"

struct eqEdit
{
	int					focus;			/* bottomscreen.h EQ_FOCUS_* */
	int					drag;			/* column the stylus holds, or -1 */
	int					pressed;		/* button the stylus holds, or -1 */
	bool				dirty;
	struct keyRepeat	dirRepeat;
};

/** Open the page (draws on the next eqEditUpdate). */
void eqEditOpen(struct eqEdit* s);

/** Back from the value entry. */
void eqEditResume(struct eqEdit* s);

/** One frame of input. PAGE_VALUE: open the value entry for
 * eqEditValueTarget(). PAGE_TO_MENU: done. */
enum page_result eqEditInput(struct eqEdit* s, u32 kDown, u32 kHeld);

/** The valueentry.h target for the picked bar (an enum valueTarget). */
int eqEditValueTarget(const struct eqEdit* s);

/** Every frame while open: redraw if something changed. */
void eqEditUpdate(struct eqEdit* s);

#endif

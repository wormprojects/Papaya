/**
 * ctrmus - 3DS Music Player
 * The queue editor (Menu > Queue..., or "queue" on the browser's bottom
 * bar): the queued songs as a list on the bottom screen.
 *
 *   A        play the song now (only it leaves the queue: with A B C D
 *            queued, playing C leaves A B D)
 *   Y        remove it
 *   Right    lift it; Up/Down then carry it, A / Left / Right put it
 *            down, B puts it back where it was
 *   Down past the last song: the buttons (Play, Up, Down, Remove,
 *            Clear all, which asks first)
 *   Stylus   tap a song to select it, drag it to move it, drag the
 *            scrollbar, tap the buttons
 *   B        back (to the menu if it was opened from there), SELECT
 *            closes
 * The playback keys (X, L, R, ZL, ZR, START) keep working.
 */

#ifndef ctrmus_queueedit_h
#define ctrmus_queueedit_h

#include <3ds.h>
#include <stdbool.h>

#include "page.h"

struct browser;

struct queueEdit
{
	int					selected;		/* song, -1: none */
	int					top;			/* first song on screen */
	bool				lifted;
	int					liftFrom;		/* where it was picked up */
	bool				buttonsFocus;
	int					button;
	bool				confirm;		/* asking "clear all?" */
	bool				fromMenu;

	struct browser*		browser;		/* for playing a playlist song */

	/* What the screen shows was drawn for these */
	unsigned			version, pops, titles, plVersion;
	bool				selIsPl;		/* the selection is a playlist song, */
	char				selFile[256];	/* this one (to follow it) */
	bool				dirty;

	/* Stylus */
	bool				touching;
	bool				dragging;		/* moving a song */
	bool				scrolling;		/* on the scrollbar */
	int					touchY0;
	u64					autoScrollMs;

	struct keyRepeat	repeat;
};

/** Open the editor (draws it). */
void queueEditOpen(struct queueEdit* e, bool fromMenu, struct browser* b);

/** One frame of input. */
enum page_result queueEditInput(struct queueEdit* e, u32 kDown, u32 kHeld);

/** Every frame while open: follows queue changes, redraws if needed. */
void queueEditUpdate(struct queueEdit* e);

#endif

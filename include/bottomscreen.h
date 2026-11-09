/**
 * ctrmus - 3DS Music Player
 * Pixel-drawn bottom-screen pages: the queue editor and search.
 *
 * The browser and the menu are text consoles; these two pages are drawn
 * as graphics instead (like the top screen: same 8x8 font, theme colours,
 * accent headers with a line under them), which leaves room for real
 * buttons, a keyboard and rows sized for the stylus. queueedit.c and
 * search.c fill in a view and draw it into a 320x240 canvas, which is
 * copied over the console's framebuffer while the page is open.
 *
 * Plain C, no libctru, so the pages can be rendered on a PC
 * (tools/preview).
 */

#ifndef ctrmus_bottomscreen_h
#define ctrmus_bottomscreen_h

#include <stdbool.h>
#include <stdint.h>

#include "topscreen.h"		/* struct topColors */

#define BOT_W	320
#define BOT_H	240

/* Same layout as the top canvas: column-major, bottom row first, i.e.
 * pixel (x, y) is px[x * BOT_H + (BOT_H-1-y)]. */
struct botCanvas
{
	uint16_t*		px;
	const uint8_t*	font;	/* 256 glyphs, 8 bytes each, MSB = left */
};

/* ------------------------------------------------------------------ */
/* Queue editor                                                        */
/* ------------------------------------------------------------------ */

#define QE_ROWS		12		/* song rows on screen */

enum qeButton
{
	QE_BTN_PLAY,
	QE_BTN_UP,
	QE_BTN_DOWN,
	QE_BTN_REMOVE,
	QE_BTN_CLEAR,
	QE_BTN_COUNT
};

/* While asking "clear all?": two buttons instead */
enum qeConfirmButton
{
	QE_CONFIRM_CLEAR,
	QE_CONFIRM_KEEP,
	QE_CONFIRM_COUNT
};

struct qeView
{
	int				count;			/* rows: queued songs, then the playlist's */
	int				qCount;			/* of which queued (the first rows) */
	int				max;			/* queue size limit */
	int				top;			/* first song on screen */
	const char*		labels[QE_ROWS];	/* songs top..top+QE_ROWS-1 */
	int				selected;		/* -1: none */
	bool			lifted;			/* the selected song is being moved */
	bool			buttonsFocus;	/* D-pad is on the button row */
	int				button;			/* focused button */
	bool			confirm;		/* "clear all N songs?" */
	const char*		status;			/* line above the buttons (folder) */
};

void qeDraw(struct botCanvas* c, const struct topColors* col,
            const struct qeView* v);

/** Song row (0..QE_ROWS-1, on screen) under the stylus, or -1. */
int qeRowAt(int x, int y);

/** Row under y even outside the list (for dragging past its ends). */
int qeRowAtClamped(int y);

/** True if (x, y) is on the scrollbar strip (when there is one). */
bool qeOnScrollbar(const struct qeView* v, int x, int y);

/** Fraction 0..1 down the scrollbar track for y. */
float qeScrollFraction(int y);

/** Button under the stylus (qeButton, or qeConfirmButton when confirming),
 * or -1. */
int qeButtonAt(int x, int y, bool confirm);

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

#define SS_ROWS_TYPING	6		/* result rows above the keyboard */
#define SS_ROWS_LIST	11		/* result rows with the keyboard away */

#define SS_KB_ROWS		5
#define SS_KB_COLS		10

/* Key codes besides plain characters */
#define SS_KEY_DEL		'\b'
#define SS_KEY_CLEAR	'\x01'
#define SS_KEY_DONE		'\n'

enum ssButton
{
	SS_BTN_PLAY,
	SS_BTN_QUEUE,
	SS_BTN_OPEN,
	SS_BTN_TYPE,
	SS_BTN_COUNT
};

struct ssView
{
	const char*		root;			/* folder being searched */
	const char*		query;
	const char* const* words;		/* query words, highlighted in names */
	int				wordCount;
	bool			typing;			/* keyboard shown */
	bool			caret;			/* caret visible (blinks) */
	int				keyRow, keyCol;	/* keyboard cursor */
	int				pressedKey;		/* key code held by the stylus, or -1 */

	int				found;			/* matches in total */
	int				listed;			/* matches in the list (it's capped) */
	int				top;			/* first match on screen */
	int				rows;			/* rows filled below */
	const char*		names[SS_ROWS_LIST];
	const char*		folders[SS_ROWS_LIST];	/* relative, "" = root */
	int				selected;		/* match index, -1: none */

	bool			buttonsFocus;
	int				button;

	bool			scanning;
	int				scanned;		/* files indexed so far */
	bool			truncated;		/* too many files: not all indexed */

	int				badgeRow;		/* row showing the badge, -1: none */
	const char*		badge;			/* e.g. "queued" */
	const char*		status;			/* line above the buttons */
};

void ssDraw(struct botCanvas* c, const struct topColors* col,
            const struct ssView* v);

/** The key at keyboard cell (row, col). */
int ssKeyCode(int row, int col);

/**
 * Key under the stylus: its code, and its cell in *row / *col (the
 * leftmost cell of a wide key). -1 if none.
 */
int ssKeyAt(int x, int y, int* row, int* col);

/** True if (x, y) is on the query field (tap it to type again). */
bool ssOnField(int x, int y);

/** Result row under the stylus (0.., on screen), or -1. */
int ssRowAt(const struct ssView* v, int x, int y);

/** Button under the stylus (results page), or -1. */
int ssButtonAt(int x, int y);

/** True if (x, y) is on the results' scrollbar strip. */
bool ssOnScrollbar(const struct ssView* v, int x, int y);

/** Fraction 0..1 down the results' scrollbar track for y. */
float ssScrollFraction(const struct ssView* v, int y);

#endif

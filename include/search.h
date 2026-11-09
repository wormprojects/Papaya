/**
 * ctrmus - 3DS Music Player
 * Search (Menu > Search..., or "find" on the browser's bottom bar): finds
 * audio files by filename in the browsed folder and every folder inside
 * it. Our own keyboard on the bottom screen (the system keyboard would
 * pause the app), results update as you type.
 *
 * Opening it lists the folder tree on a background thread (searchindex.h);
 * results show up while that runs. Each word typed has to be somewhere in
 * the filename, in any order, ignoring case.
 *
 * Typing:   A key, B delete (closes when empty), Y / "done" the results,
 *           stylus on the keys
 * Results:  A play (opens its folder in the browser, which becomes the
 *           playlist), Y queue, Right open its folder without playing,
 *           B back to typing, Down past the last one: the buttons
 * SELECT closes; the playback keys keep working.
 */

#ifndef ctrmus_search_h
#define ctrmus_search_h

#include <3ds.h>
#include <stdbool.h>

#include "page.h"
#include "searchindex.h"

struct browser;

/* Matches kept for the list (more are counted, not listed) */
#define SEARCH_MAX_MATCHES	4000

struct search
{
	struct searchIndex	idx;
	bool				idxOk;
	Thread				worker;

	char				text[SEARCH_QUERY_MAX + 1];
	struct searchQuery	query;
	int*				matches;
	int					listed;			/* matches[0..listed-1] */
	int					found;			/* all matches */
	int					checked;		/* index entries matched so far */
	bool				requery;

	bool				typing;
	int					keyRow, keyCol;
	int					pressedKey;		/* stylus on a key, -1 */
	int					selected;		/* match, -1 */
	int					top;
	bool				buttonsFocus;
	int					button;

	int					badgeMatch;		/* match showing "queued", -1 */
	const char*			badge;
	u64					badgeUntil;

	bool				scrolling;		/* stylus on the scrollbar */
	bool				fromMenu;
	bool				dirty;
	u64					caretMs;
	bool				caret;
	int					drawnCount;		/* index size when last drawn */
	u64					drawnMs;
	bool				drawnScanning;

	struct keyRepeat	dirRepeat;
	struct keyRepeat	keyHold;		/* A, B or the stylus held on a key */
};

/**
 * Open search on the browser's folder and start listing it. The browser
 * should be hidden first.
 */
void searchOpen(struct search* s, const struct browser* b, bool fromMenu);

/**
 * One frame of input. Playing or opening a result moves the browser to
 * its folder (still hidden) and returns PAGE_CLOSE.
 */
enum page_result searchInput(struct search* s, u32 kDown, u32 kHeld,
                             struct browser* b);

/** Every frame while open: new matches, caret blink, redraw. */
void searchUpdate(struct search* s);

/** Stop the scan and free everything (after searchInput closed it). */
void searchClose(struct search* s);

#endif

/**
 * ctrmus - 3DS Music Player
 * Bottom-screen file browser: cursor/scroll state, navigation and drawing
 * on top of a dirList_t (dirlist.h).
 *
 * Cursor indexing: the list is a single index space where 0 is "../",
 * 1..dirNum are directories and dirNum+1..dirNum+fileNum are files. Use
 * browserSelectedFile() rather than doing the arithmetic yourself.
 */

#ifndef ctrmus_browser_h
#define ctrmus_browser_h

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

#include "dirlist.h"

/* Folder the browser starts in: DEFAULT_DIR, then MUSIC_SUBDIR if present */
#define DEFAULT_DIR		"sdmc:/"
#define MUSIC_SUBDIR	"MUSIC"

/*
 * Bottom screen (40x30 console): row 0 is the "c>" bar (current folder),
 * rows 1..MAX_LIST the list, row 29 the "p>" bar (playlist folder). When
 * the folder doesn't fit, the last column is a scrollbar (drag it with
 * the stylus).
 */
#define	MAX_LIST		28

/* Arbitrary cap for number of stored parent positions in folder to avoid
 * unbounded memory consumption. If directories are added exceeding this,
 * dequeues path closest to root to make space.
 */
#define MAX_DIRECTORIES 20

struct browser
{
	struct dirList_t	list;
	PrintConsole*		console;	/* Where the list is drawn. */
	int					cursor;		/* Selected entry (see above). */
	int					from;		/* Scroll offset. */
	int					count;		/* Total entries (dirs + files). */

	/* Cursor/scroll of each parent directory, restored on "go up". */
	int					prevCursor[MAX_DIRECTORIES];
	int					prevFrom[MAX_DIRECTORIES];

	bool				hidden;			/* Menu open: don't draw. */
	char*				playlistDir;	/* "p>" line; NULL = none. */
	bool				dragging;		/* Scrollbar drag in progress. */
	int					queueCount;		/* shown on the "queue" button */
	/* Columns of the bottom bar's buttons, [from, to), as last drawn */
	int					findCol0, findCol1, queueCol0, queueCol1;
};

/* What's under the stylus on the bottom bar (browserFooterButtonAt) */
enum browser_button
{
	BROWSER_BTN_NONE,
	BROWSER_BTN_FIND,
	BROWSER_BTN_QUEUE
};

void browserInit(struct browser* b, PrintConsole* console);

/**
 * Change to the start folder, load it and draw it.
 *
 * \return	0 on success, -1 on failure.
 */
int browserOpen(struct browser* b);

/** Free the listing (and its background metadata job). */
void browserExit(struct browser* b);

/** Redraw the list on the browser's console (not while hidden). */
void browserDraw(struct browser* b);

/**
 * Hide the browser while something else uses the bottom screen (the
 * menu), or show it again (clears the screen and redraws).
 */
void browserSetVisible(struct browser* b, bool visible);

/**
 * Set the folder shown on the "p>" line (NULL = none). Redraws that line
 * if it changed.
 */
void browserSetPlaylistDir(struct browser* b, const char* dir);

/** The number of queued songs, for the "queue" button (redraws it). */
void browserSetQueueCount(struct browser* b, int count);

/** The bottom bar button at touch position (x, y). */
enum browser_button browserFooterButtonAt(const struct browser* b, int x,
                                          int y);

/**
 * Go to folder `dir` (absolute) and put the cursor on file `name` in it,
 * for a search result. `levels` is how many folders deeper than the
 * current one it is (0: this folder); B then goes back up through them.
 * Doesn't draw.
 *
 * \return	The file's index, or -1 if it isn't there.
 */
int browserShowFile(struct browser* b, const char* dir, const char* name,
                    int levels);

/**
 * Scrollbar dragging with the stylus. Call every frame; a drag that starts
 * on the scrollbar scrolls the list, and the cursor moves with it (keeps
 * its row on screen). Taps elsewhere do nothing.
 *
 * 
eturn	true if the list moved (and was redrawn).
 */
bool browserTouch(struct browser* b, u32 kDown, u32 kHeld);

/**
 * Apply background metadata results. Keeps the cursor on the same file if
 * the list is re-sorted.
 *
 * \param	visible	Redraw if something changed.
 * \return			What changed (RESORTED once all titles of a folder
 *					are in and the list was re-sorted).
 */
enum dirlist_poll browserPoll(struct browser* b, bool visible);

/**
 * Reload the current folder in place (keeps the cursor where possible).
 * Does not draw.
 *
 * \param	forceMeta	Re-read every file's tags, ignoring the cache.
 */
void browserRefresh(struct browser* b, bool forceMeta);

/* Cursor movement; each returns false (and does nothing) at the limit,
 * otherwise moves and redraws. */
bool browserMoveUp(struct browser* b);
bool browserMoveDown(struct browser* b);
bool browserPageUp(struct browser* b);
bool browserPageDown(struct browser* b);

/** True when the cursor is on "../". */
bool browserOnParentEntry(const struct browser* b);

/** Go to the parent folder, restoring its cursor position, and redraw. */
void browserGoUp(struct browser* b);

/**
 * If a directory is selected, enter it and redraw.
 *
 * \return	true if a directory was entered.
 */
bool browserEnterSelectedDir(struct browser* b);

/** Index into the file list of the selected file, or -1. */
int browserSelectedFile(const struct browser* b);

int browserFileCount(const struct browser* b);

/** File name of file idx (relative to the current directory). */
const char* browserFileName(const struct browser* b, int idx);

/** Display title of file idx (metadata title, or file name). */
const char* browserFileTitle(const struct browser* b, int idx);

/** Absolute path of file idx. */
void browserFilePath(const struct browser* b, int idx, char* out,
                     size_t outLen);

/**
 * Index of a file in the current listing, or -1. `path` is a bare name
 * (relative to the current directory) or an absolute path, which must be
 * in the current directory.
 */
int browserFindFile(const struct browser* b, const char* path);

/** Put the cursor on file idx, scrolling if needed. Does not draw. */
void browserSelectFile(struct browser* b, int idx);

/**
 * Move the cursor to the next file, for sequential playback. Does not
 * draw.
 *
 * \return	false if the cursor is not on a file or is on the last entry.
 */
bool browserStepToNextFile(struct browser* b);

/**
 * Move the cursor to the first file, for Repeat All wrap-around. Does not
 * draw.
 *
 * \return	false if there are no files.
 */
bool browserWrapToFirstFile(struct browser* b);

#endif

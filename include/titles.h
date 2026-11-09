/**
 * ctrmus - 3DS Music Player
 * Tag titles for the few songs the top screen's queue/playlist shows.
 *
 * Big folders don't load tags at all (dirlist.h), so their lists would
 * show long filenames. This reads the title tag of just the visible rows
 * (about 20 at most), on a low-priority worker that pauses between files
 * while music plays, and keeps the last TITLES_CACHE results in memory.
 * Nothing is written to the SD card.
 */

#ifndef ctrmus_titles_h
#define ctrmus_titles_h

#include <stdbool.h>
#include <stddef.h>

#define TITLES_CACHE	64

void titlesInit(void);
void titlesExit(void);

/**
 * The title tag of the song at `path` (absolute).
 *
 * \return	1: copied into out. 0: the song has no title tag.
 *			-1: not known yet, a read has been queued (titlesPoll() says
 *			when results come in).
 */
int titlesLookup(const char* path, char* out, size_t len);

/** True once after new results came in (time to redraw). */
bool titlesPoll(void);

/**
 * Bumped every time a result comes in. For a second reader (the queue
 * editor) that can't use titlesPoll(), which the top screen consumes.
 */
unsigned titlesGeneration(void);

#endif

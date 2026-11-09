/**
 * ctrmus - 3DS Music Player
 * Playback history: a bounded stack of previously played song paths, used
 * by "go back" (L release).
 */

#ifndef ctrmus_history_h
#define ctrmus_history_h

#include <stddef.h>

/* Oldest entries are dropped once this many are stored. */
#define MAX_HISTORY 32

/**
 * Push a song path. Empty/NULL paths are ignored.
 */
void historyPush(const char* path);

/**
 * Pop the most recent path into out.
 *
 * \return	0 on success, -1 if the history is empty.
 */
int historyPop(char* out, size_t outLen);

#endif

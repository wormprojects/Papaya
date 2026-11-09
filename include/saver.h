/**
 * ctrmus - 3DS Music Player
 * Writes small files (settings.cfg, theme.cfg) on a background thread.
 *
 * Writing to the SD card can stall for a long time: the card commits each
 * write, and while it's busy (its own housekeeping, or the playing song's
 * reads) a tiny file can take from a few milliseconds to seconds. Done on
 * the UI thread, that froze the app when the menu closed after a setting
 * changed. Now the UI thread only hands over the finished text.
 */

#ifndef ctrmus_saver_h
#define ctrmus_saver_h

#include <stddef.h>

/** Start the writer thread. */
void saverInit(void);

/** Write whatever is still waiting, then stop the thread. */
void saverExit(void);

/**
 * Write `len` bytes of `data` to `path` (replacing the file) in the
 * background. The data is copied. A newer write to the same path replaces
 * one that hasn't started yet. If the file can't be created, the folders
 * in `dirs` (NULL-terminated, outermost first) are made and it's tried
 * again. Without the thread (not started), writes immediately.
 */
void saverWrite(const char* path, const char* const* dirs, const char* data,
                size_t len);

#endif

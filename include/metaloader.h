/**
 * ctrmus - 3DS Music Player
 * Background metadata loader.
 *
 * Reading tags for every file in a directory is slow on the SD card, so it
 * runs on a low-priority worker thread instead of blocking the UI. For each
 * submitted directory the worker checks every file's mtime against the
 * on-disk cache (metacache.h), re-reads tags only for files that are new
 * or changed, posts their display titles back to the UI thread and finally
 * rewrites the cache file if anything changed.
 *
 * Only one job is worked on at a time; the UI thread owns at most one job
 * (the current directory) and must release it before submitting another.
 */

#ifndef ctrmus_metaloader_h
#define ctrmus_metaloader_h

#include <stdbool.h>

#include "metacache.h"

struct metaJob;

/**
 * Called from metaLoaderPoll() on the UI thread for each file whose
 * display title was (re)loaded. idx is the file's index in the names array
 * given to metaLoaderSubmit().
 */
typedef void (*metaApplyFn)(int idx, const char* title, void* user);

/** Start the worker thread. */
void metaLoaderInit(void);

/** Stop the worker thread. Release any job you hold first. */
void metaLoaderExit(void);

/**
 * Queue a directory for loading.
 *
 * \param	dir		Absolute directory path.
 * \param	names	File names in the directory (copied).
 * \param	count	Number of names.
 * \param	cache	Cache previously loaded for dir; ownership moves to the
 *					job and *cache is reset to empty. May be NULL.
 * \param	force	Re-read every file's tags, ignoring the cache.
 * \return			Job handle, or NULL on failure (nothing queued).
 */
struct metaJob* metaLoaderSubmit(const char* dir, char* const* names,
                                 int count, struct metaCache* cache,
                                 bool force);

/**
 * Hand any new results to fn.
 *
 * \return	true once the job has finished and every result has been
 *			delivered. The job must still be released.
 */
bool metaLoaderPoll(struct metaJob* job, metaApplyFn fn, void* user);

/**
 * Cancel (if still running) and release a job. The handle must not be
 * used afterwards. NULL is ignored.
 */
void metaLoaderRelease(struct metaJob* job);

#endif

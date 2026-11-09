/**
 * ctrmus - 3DS Music Player
 * On-disk cache of per-file metadata, one cache file per music directory.
 *
 * Cache files live in METACACHE_DIR, named after a hash of the directory
 * path (the full path is stored inside to detect collisions). Each entry
 * records the file's modification time so stale entries can be detected
 * without re-reading the file's tags.
 */

#ifndef ctrmus_metacache_h
#define ctrmus_metacache_h

#include <stdbool.h>
#include <stdint.h>

#include "metadata.h"

#ifndef METACACHE_DIR
#define METACACHE_DIR	"sdmc:/3ds/papaya/cache"
#endif

struct metaCacheEntry
{
	char*					name;	/* File name within the directory. */
	uint64_t				mtime;
	struct music_metadata	meta;
};

struct metaCache
{
	struct metaCacheEntry*	entries;	/* Sorted by name. */
	int						count;
};

/**
 * Load the cache for a directory. Missing or corrupt cache files simply
 * produce an empty cache.
 *
 * \param	dir	Absolute directory path (as returned by getcwd()).
 * \param	mc	Output; always initialised, free with metaCacheFree().
 * \return		Number of entries loaded.
 */
int metaCacheLoad(const char* dir, struct metaCache* mc);

/**
 * Find the entry for a file name, or NULL.
 */
const struct metaCacheEntry* metaCacheFind(const struct metaCache* mc,
                                           const char* name);

/**
 * Replace the cache file for a directory with the given entries.
 *
 * \return	0 on success, -1 on failure.
 */
int metaCacheSave(const char* dir, const struct metaCacheEntry* entries,
                  int count);

/**
 * Free all memory held by a cache (the struct itself is not freed).
 */
void metaCacheFree(struct metaCache* mc);

/**
 * Get a file's modification time, or 0 if unavailable.
 */
uint64_t metaCacheGetMtime(const char* path);

#endif

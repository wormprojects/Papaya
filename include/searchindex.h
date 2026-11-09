/**
 * ctrmus - 3DS Music Player
 * The file list search goes through: every audio file under a folder
 * (subfolders included), as paths relative to it.
 *
 * searchIndexScan() walks the folder on a worker thread (search.c starts
 * it) while the UI reads what's there so far: entries are only appended,
 * into memory allocated up front, and `count` is published after each
 * one is complete, so the reader needs no lock. Folders are read one at a
 * time, sorted (natural order, files before subfolders), so entries come
 * out in the same order as the browser would show them.
 *
 * Plain C (POSIX opendir/readdir), no libctru, so it can be tested on a PC.
 */

#ifndef ctrmus_searchindex_h
#define ctrmus_searchindex_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Limits: enough for a big library, small next to the 3DS's memory. */
#define SEARCH_MAX_FILES	32768
#define SEARCH_POOL_BYTES	(3u * 1024 * 1024)
#define SEARCH_MAX_DEPTH	24
/* Query length (characters) */
#define SEARCH_QUERY_MAX	48

struct searchIndex
{
	char		root[1024];		/* absolute, no trailing slash */

	/* Entry i is the relative path at pool + off[i]; its filename starts
	 * nameAt[i] bytes in. Its tag title (if one was known when the folder
	 * was read, see `titles`) is at pool + titleOff[i], 0 for none. */
	char*		pool;
	uint32_t*	off;
	uint16_t*	nameAt;
	uint32_t*	titleOff;
	size_t		used;			/* pool bytes used (worker only) */

	/* Written by the worker, read by the UI (searchIndexCount()). */
	volatile int	count;
	volatile int	folders;		/* folders read so far */
	volatile bool	done;
	volatile bool	truncated;		/* hit a limit: not everything is in */

	volatile bool	cancel;			/* set by the UI to stop the scan */

	/* Called after each folder (the 3DS build pauses briefly while music
	 * plays, so the song's SD reads go first). May be NULL. */
	void		(*pace)(void);

	/*
	 * Optional: tag titles. Called once per folder (absolute path) before
	 * its files are added; returns a handle (or NULL for none), then
	 * `title` is asked for each file name in it (true and a string in
	 * `out` if it has one) and `titlesDone` closes the handle.
	 */
	void*		(*titlesOpen)(const char* dir);
	bool		(*title)(void* handle, const char* name, char* out,
	                     size_t len);
	void		(*titlesClose)(void* handle);
};

/**
 * Allocate the index for `root` (an absolute folder path).
 *
 * \return	0, or -1 if out of memory.
 */
int searchIndexInit(struct searchIndex* idx, const char* root);

/** Walk the folder tree. Runs until done or idx->cancel is set. */
void searchIndexScan(struct searchIndex* idx);

/** Entries complete so far (safe to read from another thread). */
int searchIndexCount(const struct searchIndex* idx);

static inline const char* searchIndexPath(const struct searchIndex* idx,
                                          int i)
{
	return idx->pool + idx->off[i];
}

static inline const char* searchIndexName(const struct searchIndex* idx,
                                          int i)
{
	return idx->pool + idx->off[i] + idx->nameAt[i];
}

/** The entry's tag title, or NULL. */
static inline const char* searchIndexTitle(const struct searchIndex* idx,
                                           int i)
{
	return idx->titleOff[i] != 0 ? idx->pool + idx->titleOff[i] : NULL;
}

void searchIndexFree(struct searchIndex* idx);

/* ------------------------------------------------------------------ */
/* Matching                                                            */
/* ------------------------------------------------------------------ */

#define SEARCH_MAX_WORDS	8

/* A query split into lower-case words ("_" counts as a space). */
struct searchQuery
{
	char	text[SEARCH_QUERY_MAX + 1];
	char*	words[SEARCH_MAX_WORDS];
	int		wordCount;
};

/** Split `text` into words. An empty query has no words. */
void searchQueryParse(struct searchQuery* q, const char* text);

/**
 * True if every word of the query is somewhere in `name` (ignoring case,
 * "_" matching a space). A query with no words matches nothing.
 */
bool searchMatch(const struct searchQuery* q, const char* name);

/**
 * Like searchMatch(), but each word may be in the file name or in the
 * title (which may be NULL).
 */
bool searchMatchTitle(const struct searchQuery* q, const char* name,
                      const char* title);

/**
 * Where `word` first occurs in `name` (same rules as searchMatch), or -1.
 * For highlighting the matched part.
 */
int searchFind(const char* name, const char* word);

#endif

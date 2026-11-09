/**
 * ctrmus - 3DS Music Player
 * Directory listing: the audio files and subdirectories of the current
 * working directory, with display titles from file metadata.
 *
 * Titles come from the on-disk metadata cache immediately; a background job
 * (metaloader.h) then loads tags for new or changed files, which
 * dirListPollMetadata() applies as they arrive.
 */

#ifndef ctrmus_dirlist_h
#define ctrmus_dirlist_h

#include <stdbool.h>

/* Directories with more audio files than this just show filenames: no
 * cache, no background tag loading. Loading runs in the background, but
 * every file still costs SD lookups (an mtime check on each visit, a tag
 * read when new), and in a big folder that competes with the playing
 * song's reads for long enough to make it stutter. */
#define METADATA_LOAD_THRESHOLD 200

struct metaJob;

struct dirList_t
{
	char**	files;			/* Audio file names, sorted. */
	char**	displayNames;	/* Parallel to files: title or file name. */
	int		fileNum;

	char**	directories;	/* Subdirectory names, sorted. */
	int		dirNum;

	char*	currentDir;		/* Absolute path of the listed directory. */

	/* Background metadata job for this listing (NULL when done). Its
	 * result indices refer to `files` as ordered when it was submitted,
	 * so `files` must not be re-sorted while it is active. */
	struct metaJob*	metaJob;
	bool			metaChanged;	/* A title changed during the job. */
};

enum dirlist_poll
{
	DIRLIST_POLL_IDLE,		/* Nothing changed. */
	DIRLIST_POLL_UPDATED,	/* Some displayNames changed in place. */
	DIRLIST_POLL_RESORTED	/* Loading finished and files were re-sorted. */
};

/**
 * (Re)load the listing for the current working directory. Releases any
 * metadata job for the previous listing first.
 *
 * Files are filtered to audio-only and sorted: by file name if it starts
 * with a digit (track numbers), otherwise by display name.
 *
 * \param	forceMeta	Re-read every file's tags, ignoring the cache.
 * \return				Number of entries (directories + files).
 */
int dirListLoad(struct dirList_t* dirList, bool forceMeta);

/**
 * Free everything held by the listing, including its metadata job.
 */
void dirListFree(struct dirList_t* dirList);

/**
 * Apply titles produced by the background metadata job.
 *
 * \param	track	In: a file index to follow through a re-sort, or -1.
 *					Out: its new index (only changed on RESORTED).
 */
enum dirlist_poll dirListPollMetadata(struct dirList_t* dirList, int* track);

/**
 * Count directories + audio files in the current working directory.
 *
 * \return	Count, or -1 on failure with errno set.
 */
int dirListCountEntries(void);

#endif

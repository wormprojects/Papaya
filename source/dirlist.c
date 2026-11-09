/**
 * ctrmus - 3DS Music Player
 * Directory listing with audio-only filter + metadata titles.
 * See dirlist.h.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dirlist.h"
#include "file.h"
#include "metacache.h"
#include "metaloader.h"
#include "natsort.h"
#include "settings.h"

struct sortItem
{
	const char*	key;
	int			idx;
};

static int cmpSortItem(const void* p1, const void* p2)
{
	const struct sortItem* a = p1;
	const struct sortItem* b = p2;
	int r = naturalCompare(a->key, b->key);

	/* Tie-break on original position to keep the sort stable. */
	return r != 0 ? r : a->idx - b->idx;
}

/**
 * Sort the audio files: if a filename starts with a digit (track numbers),
 * sort by filename (naturalCompare() handles numeric prefixes), otherwise by
 * display name (metadata title).
 *
 * \param	track	A file index to follow through the sort, or -1.
 * \return			New index of track (or -1).
 */
static int sortDirFiles(struct dirList_t* dirList, int track)
{
	int					n = dirList->fileNum;
	int					newTrack = -1;
	struct sortItem*	items;
	char**				newFiles;
	char**				newNames;

	if(n < 2)
		return track;

	items = malloc(n * sizeof(*items));
	newFiles = malloc(n * sizeof(char*));
	newNames = malloc(n * sizeof(char*));

	if(items == NULL || newFiles == NULL || newNames == NULL)
	{
		free(items);
		free(newFiles);
		free(newNames);
		return track;
	}

	for(int i = 0; i < n; i++)
	{
		const char* fn = dirList->files[i];
		items[i].key = (fn[0] >= '0' && fn[0] <= '9') ?
		               fn : dirList->displayNames[i];
		items[i].idx = i;
	}

	qsort(items, n, sizeof(*items), cmpSortItem);

	for(int i = 0; i < n; i++)
	{
		newFiles[i] = dirList->files[items[i].idx];
		newNames[i] = dirList->displayNames[items[i].idx];
		if(items[i].idx == track)
			newTrack = i;
	}

	free(dirList->files);
	free(dirList->displayNames);
	dirList->files = newFiles;
	dirList->displayNames = newNames;
	free(items);
	return newTrack;
}

/* ================================================================== */
/* Loading                                                            */
/* ================================================================== */

/*
 * Hidden entries: anything starting with '.'. macOS litters SD cards with
 * these: "._song.mp3" AppleDouble files (resource forks/extended
 * attributes, not audio) next to every song, plus .Trashes,
 * .Spotlight-V100, .fseventsd and friends.
 */
static bool isHidden(const char* name)
{
	return name[0] == '.';
}

/**
 * Free entries and arrays, leaving an empty listing. Does not touch the
 * metadata job.
 */
static void freeEntries(struct dirList_t* dirList)
{
	for(int i = 0; i < dirList->dirNum; i++)
		free(dirList->directories[i]);

	for(int i = 0; i < dirList->fileNum; i++)
	{
		free(dirList->files[i]);
		free(dirList->displayNames[i]);
	}

	free(dirList->directories);
	free(dirList->files);
	free(dirList->displayNames);
	free(dirList->currentDir);
	dirList->files = NULL;
	dirList->displayNames = NULL;
	dirList->directories = NULL;
	dirList->fileNum = 0;
	dirList->dirNum = 0;
	dirList->currentDir = NULL;
}

static void releaseJob(struct dirList_t* dirList)
{
	/* Results of a job for an old listing would land on the wrong
	 * entries; drop it (it still saves what it loaded to the cache). */
	metaLoaderRelease(dirList->metaJob);
	dirList->metaJob = NULL;
	dirList->metaChanged = false;
}

int dirListLoad(struct dirList_t* dirList, bool forceMeta)
{
	DIR				*dp;
	struct dirent	*ep;
	int				fileNum = 0;
	int				dirNum = 0;
	char*			wd = getcwd(NULL, 0);

	releaseJob(dirList);

	if(wd == NULL)
		goto out;

	freeEntries(dirList);

	if((dirList->currentDir = strdup(wd)) == NULL)
		puts("Failure");

	if((dp = opendir(wd)) == NULL)
		goto out;

	/* Single pass: collect directories and audio files with filenames */
	while((ep = readdir(dp)) != NULL)
	{
		if(isHidden(ep->d_name))
			continue;

		if(ep->d_type == DT_DIR)
		{
			dirList->directories = realloc(dirList->directories,
			                               (dirNum + 1) * sizeof(char*));
			if((dirList->directories[dirNum] = strdup(ep->d_name)) == NULL)
				puts("Failure");
			dirNum++;
			continue;
		}

		/* Only include audio files (fast extension check, no I/O) */
		if(getFileTypeFromExt(ep->d_name) == FILE_TYPE_ERROR)
			continue;

		dirList->files = realloc(dirList->files,
		                         (fileNum + 1) * sizeof(char*));
		dirList->displayNames = realloc(dirList->displayNames,
		                                (fileNum + 1) * sizeof(char*));

		if((dirList->files[fileNum] = strdup(ep->d_name)) == NULL)
			puts("Failure");

		/* Placeholder: just use filename for now */
		dirList->displayNames[fileNum] = strdup(ep->d_name);

		fileNum++;
	}

	closedir(dp);

	dirList->dirNum = dirNum;
	dirList->fileNum = fileNum;

	/* Sort directories by name */
	if(dirNum > 0)
		qsort(&dirList->directories[0], dirNum, sizeof(char*), naturalCompareP);

	/* Performance mode: filenames only, no tag reads at all. */
	if(fileNum > 0 && fileNum <= METADATA_LOAD_THRESHOLD &&
	   !settingsPerformanceMode())
	{
		struct metaCache cache;

		/* Cached titles first (possibly stale; the job corrects them). */
		metaCacheLoad(wd, &cache);
		for(int i = 0; i < fileNum; i++)
		{
			const struct metaCacheEntry* e =
				metaCacheFind(&cache, dirList->files[i]);
			char* title;

			if(e == NULL || !e->meta.has_title || e->meta.title[0] == '\0')
				continue;

			if((title = strdup(e->meta.title)) != NULL)
			{
				free(dirList->displayNames[i]);
				dirList->displayNames[i] = title;
			}
		}

		sortDirFiles(dirList, -1);

		/* Job indices match dirList->files from here on; files are only
		 * re-sorted again once the job is finished. */
		dirList->metaJob = metaLoaderSubmit(wd, dirList->files, fileNum,
		                                    &cache, forceMeta);
		metaCacheFree(&cache);
	}
	else
		sortDirFiles(dirList, -1);

out:
	free(wd);
	return fileNum + dirNum;
}

void dirListFree(struct dirList_t* dirList)
{
	releaseJob(dirList);
	freeEntries(dirList);
}

/* ================================================================== */
/* Background metadata results                                        */
/* ================================================================== */

struct titleApplyCtx
{
	struct dirList_t*	dirList;
	bool				changed;
};

static void applyDirTitle(int idx, const char* title, void* user)
{
	struct titleApplyCtx* ctx = user;
	struct dirList_t* dirList = ctx->dirList;
	char* copy;

	if(idx >= dirList->fileNum ||
	   strcmp(dirList->displayNames[idx], title) == 0)
		return;

	if((copy = strdup(title)) == NULL)
		return;

	free(dirList->displayNames[idx]);
	dirList->displayNames[idx] = copy;
	ctx->changed = true;
}

enum dirlist_poll dirListPollMetadata(struct dirList_t* dirList, int* track)
{
	struct titleApplyCtx ctx = { dirList, false };

	if(dirList->metaJob == NULL)
		return DIRLIST_POLL_IDLE;

	if(!metaLoaderPoll(dirList->metaJob, applyDirTitle, &ctx))
	{
		/* Still loading: show new titles in place, sort at the end. */
		dirList->metaChanged |= ctx.changed;
		return ctx.changed ? DIRLIST_POLL_UPDATED : DIRLIST_POLL_IDLE;
	}

	metaLoaderRelease(dirList->metaJob);
	dirList->metaJob = NULL;

	if(!dirList->metaChanged && !ctx.changed)
		return DIRLIST_POLL_IDLE;

	dirList->metaChanged = false;
	*track = sortDirFiles(dirList, *track);
	return DIRLIST_POLL_RESORTED;
}

int dirListCountEntries(void)
{
	DIR				*dp;
	struct dirent	*ep;
	int				ret = 0;

	if((dp = opendir(".")) == NULL)
		return -1;

	while((ep = readdir(dp)) != NULL)
	{
		if(isHidden(ep->d_name))
			continue;

		/* Count directories always */
		if(ep->d_type == DT_DIR)
		{
			ret++;
			continue;
		}
		/* Count only audio files */
		if(getFileTypeFromExt(ep->d_name) != FILE_TYPE_ERROR)
			ret++;
	}

	closedir(dp);
	return ret;
}

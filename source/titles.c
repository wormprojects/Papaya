/**
 * ctrmus - 3DS Music Player
 * Tag titles for the top screen's visible list rows. See titles.h.
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metadata.h"
#include "playback.h"
#include "titles.h"

/* Requests waiting for the worker; the oldest is dropped when full (the
 * visible rows ask again on the next redraw anyway). */
#define MAX_PENDING		32
/* Pause after each read while music plays (ms), so the song's own SD
 * reads go first. */
#define PACE_MS			15

struct entry
{
	char*		path;
	char		title[128];
	bool		hasTitle;
	unsigned	used;		/* for least-recently-used replacement */
};

static struct entry		cache[TITLES_CACHE];
static int				cacheCount = 0;
static unsigned			useTick = 0;
static char*			pending[MAX_PENDING];
static int				pendingCount = 0;
static volatile bool	fresh = false;
static volatile unsigned	generation = 0;

static LightLock		lock;
static LightEvent		wake;
static Thread			worker = NULL;
static volatile bool	quit = false;

static int findLocked(const char* path)
{
	for(int i = 0; i < cacheCount; i++)
		if(strcmp(cache[i].path, path) == 0)
			return i;
	return -1;
}

static bool pendingLocked(const char* path)
{
	for(int i = 0; i < pendingCount; i++)
		if(strcmp(pending[i], path) == 0)
			return true;
	return false;
}

static void storeLocked(char* path, const struct music_metadata* meta)
{
	int i;

	if(findLocked(path) >= 0)
	{
		free(path);
		return;
	}

	if(cacheCount < TITLES_CACHE)
		i = cacheCount++;
	else
	{
		i = 0;
		for(int k = 1; k < TITLES_CACHE; k++)
			if(cache[k].used < cache[i].used)
				i = k;
		free(cache[i].path);
	}

	cache[i].path = path;
	cache[i].hasTitle = meta->has_title && meta->title[0] != '\0';
	snprintf(cache[i].title, sizeof(cache[i].title), "%s",
	         cache[i].hasTitle ? meta->title : "");
	cache[i].used = ++useTick;
}

static void workerMain(void* arg)
{
	(void)arg;

	while(!quit)
	{
		LightEvent_Wait(&wake);

		while(!quit)
		{
			char*					path;
			struct music_metadata	meta;

			LightLock_Lock(&lock);
			if(pendingCount == 0)
			{
				LightLock_Unlock(&lock);
				break;
			}
			/* Oldest first: rows are asked for top to bottom. */
			path = pending[0];
			memmove(&pending[0], &pending[1],
			        (size_t)(pendingCount - 1) * sizeof(pending[0]));
			pendingCount--;
			LightLock_Unlock(&lock);

			memset(&meta, 0, sizeof(meta));
			getMetadata(path, &meta);

			LightLock_Lock(&lock);
			storeLocked(path, &meta);
			fresh = true;
			generation++;
			LightLock_Unlock(&lock);

			if(isPlaying())
				svcSleepThread(PACE_MS * 1000000LL);
		}
	}
}

void titlesInit(void)
{
	s32 prio = 0x30;

	if(worker != NULL)
		return;

	LightLock_Init(&lock);
	LightEvent_Init(&wake, RESET_ONESHOT);
	quit = false;

	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	if(prio < 0x3F)
		prio++;
	worker = threadCreate(workerMain, NULL, 64 * 1024, prio, -2, false);
}

void titlesExit(void)
{
	if(worker == NULL)
		return;

	quit = true;
	LightEvent_Signal(&wake);
	threadJoin(worker, U64_MAX);
	threadFree(worker);
	worker = NULL;

	for(int i = 0; i < cacheCount; i++)
		free(cache[i].path);
	cacheCount = 0;
	for(int i = 0; i < pendingCount; i++)
		free(pending[i]);
	pendingCount = 0;
}

int titlesLookup(const char* path, char* out, size_t len)
{
	int i;
	int ret = -1;

	if(worker == NULL)
		return 0;

	LightLock_Lock(&lock);
	if((i = findLocked(path)) >= 0)
	{
		cache[i].used = ++useTick;
		if(cache[i].hasTitle)
		{
			snprintf(out, len, "%s", cache[i].title);
			ret = 1;
		}
		else
			ret = 0;
	}
	else if(!pendingLocked(path))
	{
		char* copy = strdup(path);

		if(copy != NULL)
		{
			if(pendingCount == MAX_PENDING)
			{
				free(pending[0]);
				memmove(&pending[0], &pending[1],
				        (MAX_PENDING - 1) * sizeof(pending[0]));
				pendingCount--;
			}
			pending[pendingCount++] = copy;
			LightLock_Unlock(&lock);
			LightEvent_Signal(&wake);
			return -1;
		}
	}
	LightLock_Unlock(&lock);
	return ret;
}

bool titlesPoll(void)
{
	if(!fresh)
		return false;
	fresh = false;
	return true;
}

unsigned titlesGeneration(void)
{
	return generation;
}

/**
 * ctrmus - 3DS Music Player
 * Background metadata loader. See metaloader.h.
 *
 * Job ownership: a job is referenced by the UI thread until it calls
 * metaLoaderRelease(), and by the worker from the moment it takes the job
 * until it sets workerDone. Whichever side lets go last frees it; both
 * flags are only changed with `lock` held.
 */

#include <3ds.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metadata.h"
#include "metaloader.h"
#include "playback.h"

/* Pause after each file while music is playing (ms): after a tag read,
 * and after a file that only needed its mtime checked. */
#define PACE_READ_MS	15
#define PACE_CHECK_MS	3

struct metaJob
{
	char*				dir;
	char**				names;
	int					count;
	struct metaCache	cache;
	bool				force;
	volatile bool		cancel;

	/* Guarded by lock. */
	char**				titles;		/* Per file; NULL = nothing to report. */
	int					done;		/* Files processed by the worker. */
	int					consumed;	/* Results handed to the UI. */
	bool				workerDone;
	bool				released;
};

static LightLock		lock;
static LightEvent		wake;
static Thread			worker = NULL;
static volatile bool	quit = false;
static struct metaJob*	pending = NULL;	/* Guarded by lock. */
static struct metaJob*	active = NULL;	/* Guarded by lock. */

static void jobFree(struct metaJob* job)
{
	if(job == NULL)
		return;

	for(int i = 0; i < job->count; i++)
	{
		free(job->names[i]);
		if(job->titles != NULL)
			free(job->titles[i]);
	}

	free(job->names);
	free(job->titles);
	free(job->dir);
	metaCacheFree(&job->cache);
	free(job);
}

static void joinPath(char* out, size_t outLen, const char* dir,
                     const char* name)
{
	size_t len = strlen(dir);
	bool slash = len > 0 && dir[len - 1] == '/';

	snprintf(out, outLen, "%s%s%s", dir, slash ? "" : "/", name);
}

static void processJob(struct metaJob* job)
{
	struct metaCacheEntry*	fresh;
	int						nFresh = 0;
	int						carried = 0;	/* Old entries still valid. */
	bool					dirty = false;
	int						i;
	char					path[PATH_MAX];

	if((fresh = calloc((size_t)job->count, sizeof(*fresh))) == NULL)
		return;

	for(i = 0; i < job->count && !job->cancel && !quit; i++)
	{
		const struct metaCacheEntry*	old;
		struct metaCacheEntry*			e = &fresh[nFresh];
		char*							title = NULL;

		joinPath(path, sizeof(path), job->dir, job->names[i]);
		e->mtime = metaCacheGetMtime(path);
		old = metaCacheFind(&job->cache, job->names[i]);

		if(old != NULL && old->mtime == e->mtime && !job->force)
		{
			/* Up to date: the UI already shows this title. */
			e->meta = old->meta;
			carried++;
		}
		else
		{
			const char* t;

			getMetadata(path, &e->meta);
			t = (e->meta.has_title && e->meta.title[0] != '\0') ?
			    e->meta.title : job->names[i];
			title = strdup(t);
			dirty = true;
		}

		if((e->name = strdup(job->names[i])) != NULL)
			nFresh++;

		LightLock_Lock(&lock);
		job->titles[i] = title;
		job->done = i + 1;
		LightLock_Unlock(&lock);

		/* While music plays, leave gaps between files so the playback
		 * thread's SD reads aren't stuck behind a burst of ours. */
		if(isPlaying())
			svcSleepThread((title != NULL ? PACE_READ_MS : PACE_CHECK_MS) *
			               1000000LL);
	}

	/* Cancelled part-way: keep the old entries for files not reached, so
	 * the work done so far can still be saved without losing anything. */
	for(; i < job->count; i++)
	{
		const struct metaCacheEntry* old =
			metaCacheFind(&job->cache, job->names[i]);

		if(old == NULL)
			continue;

		fresh[nFresh] = *old;
		if((fresh[nFresh].name = strdup(old->name)) != NULL)
		{
			nFresh++;
			carried++;
		}
	}

	/* Save if tags were re-read or files disappeared since last time. */
	if(dirty || carried != job->cache.count)
		metaCacheSave(job->dir, fresh, nFresh);

	for(int j = 0; j < nFresh; j++)
		free(fresh[j].name);
	free(fresh);
}

static void workerMain(void* arg)
{
	(void)arg;

	while(!quit)
	{
		struct metaJob*	job;
		bool			freeIt;

		LightEvent_Wait(&wake);

		LightLock_Lock(&lock);
		job = pending;
		pending = NULL;
		active = job;
		LightLock_Unlock(&lock);

		if(job == NULL)
			continue;

		processJob(job);

		LightLock_Lock(&lock);
		job->workerDone = true;
		active = NULL;
		freeIt = job->released;
		LightLock_Unlock(&lock);

		if(freeIt)
			jobFree(job);
	}
}

void metaLoaderInit(void)
{
	s32 prio = 0x30;

	if(worker != NULL)
		return;

	LightLock_Init(&lock);
	LightEvent_Init(&wake, RESET_ONESHOT);
	quit = false;

	/* Lower priority than the UI thread so browsing stays responsive;
	 * the work is mostly waiting on the SD card anyway. */
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	if(prio < 0x3F)
		prio++;

	worker = threadCreate(workerMain, NULL, 64 * 1024, prio, -2, false);
}

void metaLoaderExit(void)
{
	struct metaJob* orphan;

	if(worker == NULL)
		return;

	LightLock_Lock(&lock);
	quit = true;
	if(active != NULL)
		active->cancel = true;
	orphan = pending;
	pending = NULL;
	LightLock_Unlock(&lock);

	LightEvent_Signal(&wake);
	threadJoin(worker, U64_MAX);
	threadFree(worker);
	worker = NULL;

	/* Never picked up by the worker, so only the UI could hold it. */
	if(orphan != NULL)
	{
		LightLock_Lock(&lock);
		orphan->workerDone = true;
		if(orphan->released)
			jobFree(orphan);
		LightLock_Unlock(&lock);
	}
}

struct metaJob* metaLoaderSubmit(const char* dir, char* const* names,
                                 int count, struct metaCache* cache,
                                 bool force)
{
	struct metaJob*	job;
	struct metaJob*	skipped;
	bool			freeSkipped = false;

	if(worker == NULL || dir == NULL || count <= 0)
		return NULL;

	if((job = calloc(1, sizeof(*job))) == NULL)
		return NULL;

	job->count = count;
	job->force = force;
	job->dir = strdup(dir);
	job->names = calloc((size_t)count, sizeof(char*));
	job->titles = calloc((size_t)count, sizeof(char*));

	if(job->dir == NULL || job->names == NULL || job->titles == NULL)
	{
		job->count = 0;
		jobFree(job);
		return NULL;
	}

	for(int i = 0; i < count; i++)
	{
		if((job->names[i] = strdup(names[i])) == NULL)
		{
			jobFree(job);
			return NULL;
		}
	}

	if(cache != NULL)
	{
		job->cache = *cache;
		cache->entries = NULL;
		cache->count = 0;
	}

	LightLock_Lock(&lock);
	/* A job still waiting is superseded: it finishes with no results. */
	skipped = pending;
	if(skipped != NULL)
	{
		skipped->workerDone = true;
		freeSkipped = skipped->released;
	}
	pending = job;
	LightLock_Unlock(&lock);

	if(freeSkipped)
		jobFree(skipped);

	LightEvent_Signal(&wake);
	return job;
}

bool metaLoaderPoll(struct metaJob* job, metaApplyFn fn, void* user)
{
	bool finished;

	if(job == NULL)
		return true;

	LightLock_Lock(&lock);
	for(; job->consumed < job->done; job->consumed++)
	{
		char* t = job->titles[job->consumed];

		if(t == NULL)
			continue;

		fn(job->consumed, t, user);
		free(t);
		job->titles[job->consumed] = NULL;
	}
	finished = job->workerDone;
	LightLock_Unlock(&lock);

	return finished;
}

void metaLoaderRelease(struct metaJob* job)
{
	bool freeIt = false;

	if(job == NULL)
		return;

	job->cancel = true;

	LightLock_Lock(&lock);
	job->released = true;
	if(pending == job)
	{
		pending = NULL;
		freeIt = true;
	}
	else if(job->workerDone)
		freeIt = true;
	LightLock_Unlock(&lock);

	if(freeIt)
		jobFree(job);
}

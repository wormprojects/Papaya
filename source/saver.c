/**
 * ctrmus - 3DS Music Player
 * Background file writer. See saver.h.
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "saver.h"

#define MAX_JOBS	4
#define MAX_DIRS	4

struct job
{
	char*		path;
	char*		dirs[MAX_DIRS + 1];
	char*		data;
	size_t		len;
};

static struct job		jobs[MAX_JOBS];
static int				jobCount = 0;
static LightLock		lock;
static LightEvent		wake;
static Thread			worker = NULL;
static volatile bool	quit = false;

static void freeJob(struct job* j)
{
	free(j->path);
	for(int i = 0; j->dirs[i] != NULL; i++)
		free(j->dirs[i]);
	free(j->data);
	memset(j, 0, sizeof(*j));
}

/* One fwrite of the whole text: stdio would otherwise split it into
 * 1 KiB writes, and each one is a separate (committed) SD write. */
static void writeFile(const struct job* j)
{
	FILE* f = fopen(j->path, "w");

	if(f == NULL)
	{
		for(int i = 0; j->dirs[i] != NULL; i++)
			mkdir(j->dirs[i], 0755);
		f = fopen(j->path, "w");
	}
	if(f == NULL)
		return;
	setvbuf(f, NULL, _IONBF, 0);
	fwrite(j->data, 1, j->len, f);
	fclose(f);
}

static bool takeJob(struct job* out)
{
	bool got = false;

	LightLock_Lock(&lock);
	if(jobCount > 0)
	{
		*out = jobs[0];
		memmove(&jobs[0], &jobs[1], (size_t)(jobCount - 1) * sizeof(jobs[0]));
		jobCount--;
		memset(&jobs[jobCount], 0, sizeof(jobs[0]));
		got = true;
	}
	LightLock_Unlock(&lock);
	return got;
}

static void workerMain(void* arg)
{
	(void)arg;

	for(;;)
	{
		struct job j;

		while(takeJob(&j))
		{
			writeFile(&j);
			freeJob(&j);
		}
		if(quit)
			break;
		LightEvent_Wait(&wake);
	}
}

void saverInit(void)
{
	s32 prio = 0x30;

	if(worker != NULL)
		return;

	LightLock_Init(&lock);
	LightEvent_Init(&wake, RESET_ONESHOT);
	quit = false;

	/* Below the UI thread: a slow write must never hold it up. */
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	if(prio < 0x3F)
		prio++;
	worker = threadCreate(workerMain, NULL, 16 * 1024, prio, -2, false);
}

void saverExit(void)
{
	if(worker == NULL)
		return;

	quit = true;
	LightEvent_Signal(&wake);
	threadJoin(worker, U64_MAX);	/* writes what's left first */
	threadFree(worker);
	worker = NULL;
}

void saverWrite(const char* path, const char* const* dirs, const char* data,
                size_t len)
{
	struct job	j;
	int			n = 0;

	memset(&j, 0, sizeof(j));
	j.path = strdup(path);
	j.data = malloc(len > 0 ? len : 1);
	while(dirs != NULL && dirs[n] != NULL && n < MAX_DIRS)
	{
		j.dirs[n] = strdup(dirs[n]);
		n++;
	}
	if(j.path == NULL || j.data == NULL)
	{
		freeJob(&j);
		return;
	}
	memcpy(j.data, data, len);
	j.len = len;

	if(worker == NULL)
	{
		writeFile(&j);
		freeJob(&j);
		return;
	}

	LightLock_Lock(&lock);
	for(int i = 0; i < jobCount; i++)
		if(strcmp(jobs[i].path, path) == 0)
		{
			/* Not started yet: the new text replaces it. */
			freeJob(&jobs[i]);
			jobs[i] = j;
			LightLock_Unlock(&lock);
			LightEvent_Signal(&wake);
			return;
		}
	if(jobCount == MAX_JOBS)
	{
		/* Can't happen with two files; never block the UI for it. */
		LightLock_Unlock(&lock);
		freeJob(&j);
		return;
	}
	jobs[jobCount++] = j;
	LightLock_Unlock(&lock);
	LightEvent_Signal(&wake);
}

/**
 * ctrmus - 3DS Music Player
 * Background cover art loader. See coverart.h.
 */

#include <3ds.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "artdecode.h"
#include "coverart.h"
#include "metadata.h"

static Thread			worker = NULL;
static LightEvent		wake;
static LightLock		lock;
static volatile bool	quit = false;

/* Request (UI thread -> worker), guarded by lock. */
static char				reqPath[PATH_MAX];
static uint16_t			reqBg;
static uint32_t			reqGen = 0;

/* Result (worker -> UI thread), guarded by lock. */
static uint16_t			resPixels[COVER_ART_SIZE * COVER_ART_SIZE];
static int				resW, resH;
static bool				resHasArt;
static uint32_t			resGen = 0;
static bool				resReady = false;

/* Worker-only decode target. */
static uint16_t			workPixels[COVER_ART_SIZE * COVER_ART_SIZE];

static void workerMain(void* arg)
{
	static char path[PATH_MAX];

	(void)arg;

	while(true)
	{
		uint32_t		gen;
		uint16_t		bg;
		unsigned char*	data;
		size_t			len;
		int				w = 0, h = 0;
		bool			ok = false;

		LightEvent_Wait(&wake);
		if(quit)
			break;

		LightLock_Lock(&lock);
		memcpy(path, reqPath, sizeof(path));
		bg = reqBg;
		gen = reqGen;
		LightLock_Unlock(&lock);

		if(getCoverArt(path, &data, &len) == 0)
		{
			ok = artDecode(data, len, workPixels, COVER_ART_SIZE, bg,
			               &w, &h) == 0;
			free(data);
		}

		LightLock_Lock(&lock);
		if(gen == reqGen)
		{
			if(ok)
				memcpy(resPixels, workPixels, sizeof(resPixels));
			resW = w;
			resH = h;
			resHasArt = ok;
			resGen = gen;
			resReady = true;
		}
		LightLock_Unlock(&lock);
	}
}

void coverArtInit(void)
{
	s32 prio;

	LightEvent_Init(&wake, RESET_ONESHOT);
	LightLock_Init(&lock);
	quit = false;

	/* Below the UI thread, like the metadata loader. libjpeg and libpng
	 * keep their big buffers on the heap; 64 KiB of stack is plenty. */
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	worker = threadCreate(workerMain, NULL, 64 * 1024, prio + 1, -2, false);
}

void coverArtExit(void)
{
	if(worker == NULL)
		return;

	quit = true;
	LightEvent_Signal(&wake);
	threadJoin(worker, U64_MAX);
	threadFree(worker);
	worker = NULL;
}

void coverArtRequest(const char* path, uint16_t alphaBg)
{
	if(worker == NULL)
		return;

	LightLock_Lock(&lock);
	snprintf(reqPath, sizeof(reqPath), "%s", path);
	reqBg = alphaBg;
	reqGen++;
	resReady = false;
	LightLock_Unlock(&lock);

	LightEvent_Signal(&wake);
}

bool coverArtPoll(uint16_t* pixels, int* w, int* h, bool* hasArt)
{
	bool got = false;

	LightLock_Lock(&lock);
	if(resReady && resGen == reqGen)
	{
		if(resHasArt)
			memcpy(pixels, resPixels, sizeof(resPixels));
		*w = resW;
		*h = resH;
		*hasArt = resHasArt;
		resReady = false;
		got = true;
	}
	LightLock_Unlock(&lock);

	return got;
}

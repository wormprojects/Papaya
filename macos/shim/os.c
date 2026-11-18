/**
 * Papaya desktop: threads, locks, events, time and the 3DS services that
 * don't exist here. See shim/3ds.h.
 */

#include <3ds.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Locks and events                                                    */
/* ------------------------------------------------------------------ */

struct event
{
	pthread_mutex_t	mutex;
	pthread_cond_t	cond;
	bool			set;
	bool			sticky;
};

_Static_assert(sizeof(pthread_mutex_t) <= sizeof(LightLock),
               "LightLock too small");
_Static_assert(sizeof(struct event) <= sizeof(LightEvent),
               "LightEvent too small");

void LightLock_Init(LightLock* lock)
{
	pthread_mutex_init((pthread_mutex_t*)lock, NULL);
}

void LightLock_Lock(LightLock* lock)
{
	pthread_mutex_lock((pthread_mutex_t*)lock);
}

int LightLock_TryLock(LightLock* lock)
{
	/* libctru: 0 = acquired */
	return pthread_mutex_trylock((pthread_mutex_t*)lock) == 0 ? 0 : 1;
}

void LightLock_Unlock(LightLock* lock)
{
	pthread_mutex_unlock((pthread_mutex_t*)lock);
}

static void eventInit(struct event* e, bool sticky)
{
	pthread_mutex_init(&e->mutex, NULL);
	pthread_cond_init(&e->cond, NULL);
	e->set = false;
	e->sticky = sticky;
}

static void eventSignal(struct event* e)
{
	pthread_mutex_lock(&e->mutex);
	e->set = true;
	if(e->sticky)
		pthread_cond_broadcast(&e->cond);
	else
		pthread_cond_signal(&e->cond);
	pthread_mutex_unlock(&e->mutex);
}

static void eventClear(struct event* e)
{
	pthread_mutex_lock(&e->mutex);
	e->set = false;
	pthread_mutex_unlock(&e->mutex);
}

/* timeout_ns < 0: forever. Returns 0, or -1 on timeout. */
static int eventWait(struct event* e, s64 timeout_ns)
{
	struct timespec	until;
	int				ret = 0;

	if(timeout_ns >= 0)
	{
		clock_gettime(CLOCK_REALTIME, &until);
		until.tv_sec += timeout_ns / 1000000000LL;
		until.tv_nsec += timeout_ns % 1000000000LL;
		if(until.tv_nsec >= 1000000000L)
		{
			until.tv_sec++;
			until.tv_nsec -= 1000000000L;
		}
	}

	pthread_mutex_lock(&e->mutex);
	while(!e->set && ret == 0)
	{
		if(timeout_ns < 0)
			pthread_cond_wait(&e->cond, &e->mutex);
		else if(pthread_cond_timedwait(&e->cond, &e->mutex, &until) ==
		        ETIMEDOUT)
			ret = -1;
	}
	if(ret == 0 && !e->sticky)
		e->set = false;
	pthread_mutex_unlock(&e->mutex);
	return ret;
}

void LightEvent_Init(LightEvent* event, ResetType reset)
{
	eventInit((struct event*)event, reset == RESET_STICKY);
}

void LightEvent_Signal(LightEvent* event)
{
	eventSignal((struct event*)event);
}

void LightEvent_Wait(LightEvent* event)
{
	eventWait((struct event*)event, -1);
}

void LightEvent_Clear(LightEvent* event)
{
	eventClear((struct event*)event);
}

/* ---- kernel event handles (player.c's watchdog) ---- */

#define MAX_HANDLES	16
static struct event		handles[MAX_HANDLES];
static bool				handleUsed[MAX_HANDLES];
static pthread_mutex_t	handleLock = PTHREAD_MUTEX_INITIALIZER;

Result svcCreateEvent(Handle* out, ResetType reset)
{
	Result ret = -1;

	pthread_mutex_lock(&handleLock);
	for(int i = 0; i < MAX_HANDLES; i++)
		if(!handleUsed[i])
		{
			handleUsed[i] = true;
			eventInit(&handles[i], reset == RESET_STICKY);
			*out = (Handle)i + 1;
			ret = 0;
			break;
		}
	pthread_mutex_unlock(&handleLock);
	return ret;
}

static struct event* handleEvent(Handle h)
{
	return (h >= 1 && h <= MAX_HANDLES && handleUsed[h - 1]) ?
	       &handles[h - 1] : NULL;
}

Result svcSignalEvent(Handle h)
{
	struct event* e = handleEvent(h);

	if(e == NULL)
		return -1;
	eventSignal(e);
	return 0;
}

Result svcClearEvent(Handle h)
{
	struct event* e = handleEvent(h);

	if(e == NULL)
		return -1;
	eventClear(e);
	return 0;
}

Result svcWaitSynchronization(Handle h, s64 timeout_ns)
{
	struct event* e = handleEvent(h);

	if(e == NULL)
		return -1;
	return eventWait(e, timeout_ns);
}

/* ------------------------------------------------------------------ */
/* Threads                                                             */
/* ------------------------------------------------------------------ */

struct Thread_tag
{
	pthread_t	thread;
	ThreadFunc	entry;
	void*		arg;
	bool		detached;
};

static void* threadMain(void* p)
{
	struct Thread_tag* t = p;
	bool detached = t->detached;
	ThreadFunc entry = t->entry;
	void* arg = t->arg;

	entry(arg);
	if(detached)
		free(t);
	return NULL;
}

Thread threadCreate(ThreadFunc entry, void* arg, size_t stack_size,
                    int prio, int affinity, bool detached)
{
	struct Thread_tag*	t = calloc(1, sizeof(*t));
	pthread_attr_t		attr;
	int					err;

	(void)prio;
	(void)affinity;
	if(t == NULL)
		return NULL;

	t->entry = entry;
	t->arg = arg;
	t->detached = detached;

	/* The 3DS stacks are tiny (4-128 KiB); the host's decoders and the
	 * JPEG/PNG code are used to more. */
	if(stack_size < 1024 * 1024)
		stack_size = 1024 * 1024;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, stack_size);
	if(detached)
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = pthread_create(&t->thread, &attr, threadMain, t);
	pthread_attr_destroy(&attr);
	if(err != 0)
	{
		free(t);
		return NULL;
	}
	return t;
}

Result threadJoin(Thread t, u64 timeout_ns)
{
	(void)timeout_ns;	/* the app always waits forever */
	if(t == NULL || t->detached)
		return -1;
	return pthread_join(t->thread, NULL) == 0 ? 0 : -1;
}

void threadFree(Thread t)
{
	if(t != NULL && !t->detached)
		free(t);
}

void threadExit(int rc)
{
	(void)rc;
	pthread_exit(NULL);
}

Result svcGetThreadPriority(s32* out, Handle thread)
{
	(void)thread;
	*out = 0x30;	/* the app only ever derives other priorities from it */
	return 0;
}

void svcSleepThread(s64 ns)
{
	struct timespec ts;

	if(ns <= 0)
		ns = 1;
	ts.tv_sec = ns / 1000000000LL;
	ts.tv_nsec = ns % 1000000000LL;
	while(nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

u64 osGetTime(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (u64)ts.tv_sec * 1000 + (u64)ts.tv_nsec / 1000000;
}

void* linearAlloc(size_t size)
{
	return malloc(size);
}

void linearFree(void* mem)
{
	free(mem);
}

Result DSP_FlushDataCache(const void* address, u32 size)
{
	(void)address;
	(void)size;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Services that don't exist here                                      */
/* ------------------------------------------------------------------ */

bool aptMainLoop(void)
{
	return !videoQuitRequested();
}

void aptSetSleepAllowed(bool allowed)
{
	(void)allowed;
}

Result APT_CheckNew3DS(bool* out)
{
	*out = false;		/* no "fast mode" here: it is always as fast as it gets */
	return 0;
}

void osSetSpeedupEnable(bool enable)
{
	(void)enable;
}

bool osIsHeadsetConnected(void)
{
	return false;
}

Result HIDUSER_GetSoundVolume(u8* volume)
{
	(void)volume;
	return -1;
}

static bool hasBattery = false;

Result mcuHwcInit(void)
{
	hasBattery = nativeBatteryPercent() >= 0;
	return hasBattery ? 0 : -1;
}

void mcuHwcExit(void)
{
	hasBattery = false;
}

Result MCUHWC_GetBatteryLevel(u8* level)
{
	int pct = hasBattery ? nativeBatteryPercent() : -1;

	if(pct < 0)
		return -1;
	*level = (u8)(pct > 100 ? 100 : pct);
	return 0;
}

Result gspLcdInit(void)
{
	return -1;
}

void gspLcdExit(void)
{
}

Result GSPLCD_GetBrightness(u32 screen, u32* brightness)
{
	(void)screen;
	(void)brightness;
	return -1;
}

Result GSPLCD_SetBrightness(u32 screen, u32 brightness)
{
	(void)screen;
	(void)brightness;
	return -1;
}

Result cfguInit(void)
{
	return -1;
}

void cfguExit(void)
{
}

Result CFGU_GetConfigInfoBlk2(u32 size, u32 block, void* out)
{
	(void)size;
	(void)block;
	(void)out;
	return -1;
}

Result CFG_GetConfigInfoBlk8(u32 size, u32 block, void* out)
{
	(void)size;
	(void)block;
	(void)out;
	return -1;
}

Result archive_getmtime(const char* path, u64* mtime)
{
	struct stat st;

	if(stat(path, &st) != 0)		/* through pathmap.h */
		return -1;
	*mtime = (u64)st.st_mtime;
	return 0;
}

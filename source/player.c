/**
 * ctrmus - 3DS Music Player
 * App-level playback control. See player.h.
 */

#include <3ds.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "error.h"
#include "history.h"
#include "player.h"
#include "ui.h"

static struct playbackInfo_t	playbackInfo = { 0 };
static struct errInfo_t			errInfo;
static Handle					playbackFailEvent;
static volatile bool			runThreads = true;

/* Written by the playback thread through errInfo: an errno/custom error
 * code on failure, then -1 when the thread finishes (song ended, failed,
 * or was stopped). */
static volatile int				playbackError = 0;

static struct music_metadata	nowPlayingMeta;
static char						nowPlayingFile[PATH_MAX];
static bool						hasNowPlaying = false;
static unsigned					songId = 0;	/* bumped on every song start */

static struct queue_t			songQueue;
static bool						shuffleEnabled = false;
static enum repeat_mode			repeatMode = REPEAT_OFF;

/*
 * The playlist: a snapshot of the folder the current song was started
 * from in the browser. Browsing elsewhere doesn't change what plays next,
 * and queued songs (which can be from anywhere) don't replace it.
 *
 * Names and titles share one buffer; `sorted` indexes the entries by name
 * for lookups. `cur` is the playlist song that played last; it stays set
 * while queued songs play, so the playlist carries on after them. The
 * shuffle order is a permutation of entry indices, `orderPos` is where
 * `cur` is in it. All paths played from the playlist are absolute.
 */
static struct
{
	char*	dir;
	char*	strings;
	char**	names;
	char**	titles;
	int*	sorted;
	int		count;
	int		cur;
	int*	order;
	int		orderPos;
	bool	orderValid;
	bool	custom;		/* not shuffled, but `order` holds the user's own
						 * order (the queue editor moved songs) */
} pl = { .cur = -1, .orderPos = -1 };

/* Bumped when the upcoming playlist songs change other than by a song
 * starting (the queue editor follows it). */
static unsigned plVersion = 0;

/**
 * Allows the playback thread to return any error messages that it may
 * encounter.
 */
static void playbackWatchdog(void* arg)
{
	(void)arg;

	while(runThreads)
	{
		svcWaitSynchronization(playbackFailEvent, U64_MAX);
		svcClearEvent(playbackFailEvent);
	}
}

void playerInit(void)
{
	queueInit(&songQueue);

	svcCreateEvent(&playbackFailEvent, RESET_ONESHOT);
	errInfo.error = &playbackError;
	errInfo.failEvent = &playbackFailEvent;
	playbackInfo.errInfo = &errInfo;

	threadCreate(playbackWatchdog, NULL, 4 * 1024, 0x20, -2, true);
}

/**
 * Stop the currently playing file (if there is one) and play another file.
 *
 * \param	ep_file	File to play, or NULL to just stop.
 */
/* Set while going back (playerPlayPrevious): the song being left is not
 * put on the history, or going back twice would return to it instead of
 * to the song before the one just played. */
static bool goingBack = false;

static int changeFile(const char* ep_file)
{
	s32 prio;
	static Thread thread = NULL;

	if(thread != NULL)
	{
		stopPlayback();
		threadJoin(thread, U64_MAX);
		threadFree(thread);
		thread = NULL;
	}

	if(ep_file == NULL)
		return 0;

	/* Push current song to history before switching */
	if(!goingBack && hasNowPlaying && nowPlayingFile[0] != '\0')
		historyPush(nowPlayingFile);

	if(memccpy(playbackInfo.file, ep_file, '\0',
	           sizeof(playbackInfo.file)) == NULL)
		return -1;

	/* Read metadata for now-playing display */
	memset(&nowPlayingMeta, 0, sizeof(nowPlayingMeta));
	getMetadata(ep_file, &nowPlayingMeta);
	snprintf(nowPlayingFile, sizeof(nowPlayingFile), "%s", ep_file);
	hasNowPlaying = true;
	songId++;

	uiRequestRedraw();
	playbackInfo.samples_total = 0;
	playbackInfo.samples_played = 0;
	playbackInfo.samples_per_second = 0;

	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	thread = threadCreate(playFile, &playbackInfo, 128 * 1024,
	                      prio - 1, -2, false);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Playlist                                                            */
/* ------------------------------------------------------------------ */

static void plFree(void)
{
	free(pl.dir);
	free(pl.strings);
	free(pl.names);
	free(pl.titles);
	free(pl.sorted);
	free(pl.order);
	memset(&pl, 0, sizeof(pl));
	pl.cur = -1;
	pl.orderPos = -1;
}

/* Directory paths equal, ignoring trailing slashes. */
static bool dirEqual(const char* a, size_t alen, const char* b, size_t blen)
{
	while(alen > 0 && a[alen - 1] == '/')
		alen--;
	while(blen > 0 && b[blen - 1] == '/')
		blen--;
	return alen == blen && strncmp(a, b, alen) == 0;
}

static const char* const* sortNames;

static int cmpByName(const void* a, const void* b)
{
	return strcmp(sortNames[*(const int*)a], sortNames[*(const int*)b]);
}

/* Entry with this file name, or -1. */
static int plFindName(const char* name)
{
	int lo = 0, hi = pl.count - 1;

	while(lo <= hi)
	{
		int mid = (lo + hi) / 2;
		int c = strcmp(name, pl.names[pl.sorted[mid]]);

		if(c == 0)
			return pl.sorted[mid];
		if(c < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return -1;
}

/* Entry for an absolute path, or -1 if it isn't in the playlist folder. */
static int plFindPath(const char* path)
{
	const char* slash = strrchr(path, '/');

	if(pl.dir == NULL || slash == NULL ||
	   !dirEqual(pl.dir, strlen(pl.dir), path, (size_t)(slash - path)))
		return -1;
	return plFindName(slash + 1);
}

static bool plPath(int i, char* out, size_t len)
{
	size_t dirLen = strlen(pl.dir);

	while(dirLen > 0 && pl.dir[dirLen - 1] == '/')
		dirLen--;
	return snprintf(out, len, "%.*s/%s", (int)dirLen, pl.dir,
	                pl.names[i]) < (int)len;
}

/*
 * Replace the playlist's entries with the browser's current folder (same
 * order). Leaves `cur` and the shuffle order to the caller; any old
 * shuffle order is dropped.
 */
static bool plSnapshot(const struct browser* b)
{
	const char*	dir = b->list.currentDir;
	int			n = browserFileCount(b);
	size_t		total = 0;
	char*		strings;
	char**		names;
	char**		titles;
	int*		sorted;
	char*		dirCopy;
	char*		p;

	if(dir == NULL || n <= 0)
		return false;

	for(int i = 0; i < n; i++)
		total += strlen(browserFileName(b, i)) +
		         strlen(browserFileTitle(b, i)) + 2;

	strings = malloc(total);
	names = malloc(sizeof(char*) * (size_t)n);
	titles = malloc(sizeof(char*) * (size_t)n);
	sorted = malloc(sizeof(int) * (size_t)n);
	dirCopy = strdup(dir);
	if(strings == NULL || names == NULL || titles == NULL ||
	   sorted == NULL || dirCopy == NULL)
	{
		free(strings);
		free(names);
		free(titles);
		free(sorted);
		free(dirCopy);
		return false;
	}

	p = strings;
	for(int i = 0; i < n; i++)
	{
		size_t len = strlen(browserFileName(b, i)) + 1;

		memcpy(p, browserFileName(b, i), len);
		names[i] = p;
		p += len;

		len = strlen(browserFileTitle(b, i)) + 1;
		memcpy(p, browserFileTitle(b, i), len);
		titles[i] = p;
		p += len;

		sorted[i] = i;
	}

	sortNames = (const char* const*)names;
	qsort(sorted, (size_t)n, sizeof(int), cmpByName);

	free(pl.dir);
	free(pl.strings);
	free(pl.names);
	free(pl.titles);
	free(pl.sorted);
	free(pl.order);
	pl.dir = dirCopy;
	pl.strings = strings;
	pl.names = names;
	pl.titles = titles;
	pl.sorted = sorted;
	pl.count = n;
	pl.order = NULL;
	pl.orderPos = -1;
	plVersion++;
	pl.orderValid = false;
	return true;
}

/* New shuffle order; `first` (or -1) goes first and counts as played. */
static void plShuffle(int first)
{
	free(pl.order);
	pl.orderValid = false;
	pl.orderPos = -1;

	if(pl.count <= 0 ||
	   (pl.order = malloc(sizeof(int) * (size_t)pl.count)) == NULL)
		return;

	for(int i = 0; i < pl.count; i++)
		pl.order[i] = i;
	for(int i = pl.count - 1; i > 0; i--)
	{
		int j = rand() % (i + 1);
		int t = pl.order[i];

		pl.order[i] = pl.order[j];
		pl.order[j] = t;
	}

	if(first >= 0)
	{
		for(int k = 0; k < pl.count; k++)
			if(pl.order[k] == first)
			{
				pl.order[k] = pl.order[0];
				pl.order[0] = first;
				break;
			}
		pl.orderPos = 0;
	}
	pl.orderValid = true;
}

/* The shuffle order exists (when shuffle is on). */
static void plEnsureOrder(void)
{
	if(shuffleEnabled && !pl.orderValid)
		plShuffle(pl.cur);
}

/* Entry i is now the current playlist song. */
static void plSetCur(int i)
{
	pl.cur = i;
	plVersion++;
	if(pl.orderValid)
		for(int k = 0; k < pl.count; k++)
			if(pl.order[k] == i)
			{
				pl.orderPos = k;
				break;
			}
}

/*
 * Reverse playback (a negative speed) walks the playlist backwards too, so
 * an album played in reverse goes 144, 143, 142... (in the folder, shuffle
 * or custom order): plNext()/plAfter() and everything shown as "up next"
 * follow the direction. This is not L's "previous", which goes back
 * through the history (where the user jumped from).
 */
static bool plBackwards(void)
{
	return getPlaybackSpeed() < 0.0f;
}

/* Where `cur` is in the play order (order[] position, or the entry index
 * in folder order); -1 = before the first song. */
static int plBase(void)
{
	return pl.orderValid ? pl.orderPos : pl.cur;
}

/**
 * The playlist entry that plays after `cur`: next in the shuffle order
 * (reshuffling when it runs out: shuffle never stops), or the next file,
 * wrapping with Repeat All. `back`: the one before instead (going through
 * the album in reverse). -1 if none.
 */
static int plNext(bool back)
{
	int p;

	if(pl.count <= 0)
		return -1;

	if(shuffleEnabled)
	{
		plEnsureOrder();
		if(!pl.orderValid)
			return -1;

		if(!back && pl.orderPos + 1 >= pl.count)
		{
			/* Whole folder played: new order, not starting with the
			 * song that just played. */
			int last = pl.cur;

			plShuffle(-1);
			if(!pl.orderValid)
				return -1;
			if(last >= 0 && pl.count > 1 && pl.order[0] == last)
			{
				pl.order[0] = pl.order[1];
				pl.order[1] = last;
			}
		}
		else if(back && pl.orderPos <= 0)
		{
			/* Backwards past the start of the order: a new one, played
			 * from its end, not starting with the song that just
			 * played. */
			int last = pl.cur;

			plShuffle(-1);
			if(!pl.orderValid)
				return -1;
			pl.orderPos = pl.count;		/* "after the end" */
			if(last >= 0 && pl.count > 1 && pl.order[pl.count - 1] == last)
			{
				pl.order[pl.count - 1] = pl.order[pl.count - 2];
				pl.order[pl.count - 2] = last;
			}
		}
		return pl.order[pl.orderPos + (back ? -1 : 1)];
	}

	if(pl.custom && pl.orderValid)
	{
		p = pl.orderPos + (back ? -1 : 1);
		if(p >= 0 && p < pl.count)
			return pl.order[p];
		if(repeatMode != REPEAT_ALL)
			return -1;
		return pl.order[back ? pl.count - 1 : 0];
	}

	/* No current song (cur -1) going backwards: start from the last. */
	p = back ? (pl.cur < 0 ? pl.count : pl.cur) - 1 : pl.cur + 1;
	if(p >= 0 && p < pl.count)
		return p;
	if(repeatMode == REPEAT_ALL)
		return back ? pl.count - 1 : 0;
	return -1;
}

/*
 * Position in the play order (order[] index, or entry index in folder
 * order) of the k-th song to play after `cur` (k = 0 plays next), without
 * wrapping: past the end (or before the start, backwards) when it isn't
 * there. In reverse it counts down from `cur`.
 */
static int plAfterPos(int k)
{
	int base = plBase();

	if(plBackwards())
		return (base < 0 ? pl.count : base) - 1 - k;
	return base + 1 + k;
}

/*
 * The k-th playlist song after `cur` in play order (k = 0 plays next), or
 * -1: the shuffle order, the user's own order, or the folder order
 * (wrapping round with Repeat All, which lists the songs before `cur`).
 * Backwards during reverse playback (see plBackwards()).
 */
static int plAfter(int k)
{
	int p, base;
	bool back = plBackwards();

	if(pl.count <= 0 || k < 0)
		return -1;

	if(shuffleEnabled)
	{
		plEnsureOrder();
		if(!pl.orderValid)
			return -1;
	}
	p = plAfterPos(k);
	base = plBase();
	if(p >= 0 && p < pl.count)
		return pl.orderValid ? pl.order[p] : p;

	/* Wrapping round with Repeat All (not in shuffle: a new order is made
	 * when it runs out), up to the current song. */
	if((pl.orderValid && shuffleEnabled) || repeatMode != REPEAT_ALL)
		return -1;
	if(back && base < 0)
		return -1;		/* already listed the whole playlist */
	p += back ? pl.count : -pl.count;
	if(back ? p <= base : p >= base)
		return -1;
	return pl.orderValid ? pl.order[p] : p;
}

/* Keep the browser cursor on the playing song if it shows that folder. */
static void followInBrowser(struct browser* b)
{
	int idx;

	if(pl.cur < 0 || pl.dir == NULL || b->list.currentDir == NULL ||
	   !dirEqual(pl.dir, strlen(pl.dir), b->list.currentDir,
	             strlen(b->list.currentDir)))
		return;

	if((idx = browserFindFile(b, pl.names[pl.cur])) >= 0)
	{
		browserSelectFile(b, idx);
		browserDraw(b);
	}
}

void playerSyncPlaylist(const struct browser* b)
{
	char*	oldStrings;
	char**	oldNames;
	char*	oldDir;
	int*	oldSorted;
	char**	oldTitles;
	int*	oldOrder;
	int		oldCount, oldCur, oldPos;
	bool	oldValid;

	if(pl.dir == NULL || b->list.currentDir == NULL ||
	   !dirEqual(pl.dir, strlen(pl.dir), b->list.currentDir,
	             strlen(b->list.currentDir)))
		return;

	/* Take the old entries so plSnapshot() doesn't free them yet. */
	oldDir = pl.dir;
	oldStrings = pl.strings;
	oldNames = pl.names;
	oldTitles = pl.titles;
	oldSorted = pl.sorted;
	oldOrder = pl.order;
	oldCount = pl.count;
	oldCur = pl.cur;
	oldPos = pl.orderPos;
	oldValid = pl.orderValid;
	pl.dir = NULL;
	pl.strings = NULL;
	pl.names = NULL;
	pl.titles = NULL;
	pl.sorted = NULL;
	pl.order = NULL;

	if(!plSnapshot(b))
	{
		pl.dir = oldDir;
		pl.strings = oldStrings;
		pl.names = oldNames;
		pl.titles = oldTitles;
		pl.sorted = oldSorted;
		pl.order = oldOrder;
		pl.count = oldCount;
		pl.orderPos = oldPos;
		pl.orderValid = oldValid;
		return;
	}

	/* Same songs, new titles/order: carry over by name. */
	pl.cur = oldCur >= 0 ? plFindName(oldNames[oldCur]) : -1;

	if(oldValid && oldOrder != NULL &&
	   (pl.order = malloc(sizeof(int) * (size_t)pl.count)) != NULL)
	{
		bool*	seen = calloc((size_t)pl.count, sizeof(bool));
		int		k = 0;

		if(seen == NULL)
		{
			free(pl.order);
			pl.order = NULL;
		}
		else
		{
			pl.orderPos = -1;
			for(int j = 0; j < oldCount; j++)
			{
				int i = plFindName(oldNames[oldOrder[j]]);

				if(i >= 0 && !seen[i])
				{
					seen[i] = true;
					pl.order[k++] = i;
				}
				if(j == oldPos)
					pl.orderPos = k - 1;
			}
			/* New files go at the end. */
			for(int i = 0; i < pl.count; i++)
				if(!seen[i])
					pl.order[k++] = i;
			pl.orderValid = true;
			free(seen);
		}
	}

	free(oldDir);
	free(oldStrings);
	free(oldNames);
	free(oldTitles);
	free(oldSorted);
	free(oldOrder);
}

int playerUpcoming(const struct browser* b, const char** titles,
                   const char** files, int max, bool* firstIsCurrent,
                   int* total, const char** dir)
{
	int count = 0;

	*firstIsCurrent = false;
	*total = 0;
	*dir = pl.dir;

	if(max <= 0)
		return 0;

	if(pl.count == 0)
	{
		/* Nothing played yet: R would start from the folder being
		 * browsed, after the cursor. */
		int n = browserFileCount(b);
		int from = browserSelectedFile(b);

		*dir = b->list.currentDir;
		*total = n > 0 ? n : 0;
		if(plBackwards())
		{
			/* Reverse: the files before the cursor, going up. */
			for(int i = (from < 0 ? n : from) - 1; i >= 0 && count < max;
			    i--)
			{
				files[count] = browserFileName(b, i);
				titles[count++] = browserFileTitle(b, i);
			}
			return count;
		}
		for(int i = from + 1; i < n && count < max; i++)
		{
			files[count] = browserFileName(b, i);
			titles[count++] = browserFileTitle(b, i);
		}
		return count;
	}

	*total = pl.count;

	/* The current playlist song is highlighted only while it is what's
	 * playing (not while a queued or older song plays). */
	if(pl.cur >= 0 && hasNowPlaying && plFindPath(nowPlayingFile) == pl.cur)
	{
		files[count] = pl.names[pl.cur];
		titles[count++] = pl.titles[pl.cur];
		*firstIsCurrent = true;
	}

	for(int k = 0; count < max; k++)
	{
		int e = plAfter(k);

		if(e < 0)
			break;
		files[count] = pl.names[e];
		titles[count++] = pl.titles[e];
	}

	return count;
}

int playerPlaylistAfter(const char** titles, const char** files, int max)
{
	int count = 0;

	for(int k = 0; count < max; k++)
	{
		int e = plAfter(k);

		if(e < 0)
			break;
		files[count] = pl.names[e];
		titles[count++] = pl.titles[e];
	}
	return count;
}

unsigned playerPlaylistVersion(void)
{
	/* The list turns round when the speed's sign does. */
	return plVersion + songId + (plBackwards() ? 0x40000000u : 0);
}

/* Playlist song `e` plays now (defined below). */
static void plPlay(int i, bool manual);

void playerPlaylistPlay(struct browser* b, int k)
{
	int e = plAfter(k);

	if(e < 0)
		return;
	plPlay(e, true);
	followInBrowser(b);
}

bool playerPlaylistMove(int k, int dir)
{
	int pa, pb;

	if(pl.count <= 1 || k < 0 || k + dir < 0 || (dir != 1 && dir != -1) ||
	   plAfter(k) < 0 || plAfter(k + dir) < 0)
		return false;

	if(shuffleEnabled)
		plEnsureOrder();
	else if(!pl.orderValid)
	{
		/* The first move: start from the folder order. */
		free(pl.order);
		if((pl.order = malloc(sizeof(int) * (size_t)pl.count)) == NULL)
			return false;
		for(int i = 0; i < pl.count; i++)
			pl.order[i] = i;
		pl.orderPos = pl.cur;
		pl.orderValid = true;
		pl.custom = true;
	}
	if(!pl.orderValid)
		return false;

	/* Only songs that haven't wrapped round (Repeat All) swap. In reverse
	 * the list runs down the order, so "down" in it is down the order. */
	pa = plAfterPos(k);
	pb = plAfterPos(k + dir);
	if(pa < 0 || pb < 0 || pa >= pl.count || pb >= pl.count ||
	   (plBackwards() ? (pa >= pl.orderPos || pb >= pl.orderPos) :
	                    (pa <= pl.orderPos || pb <= pl.orderPos)))
		return false;

	{
		int t = pl.order[pa];

		pl.order[pa] = pl.order[pb];
		pl.order[pb] = t;
	}
	plVersion++;
	uiRequestRedraw();
	return true;
}

/* ------------------------------------------------------------------ */
/* Playing                                                             */
/* ------------------------------------------------------------------ */

void playerExit(void)
{
	runThreads = false;
	svcSignalEvent(playbackFailEvent);
	changeFile(NULL);
	plFree();
}

/**
 * Start a song.
 *
 * \param	manual	User action: clear the "finished" signal the stopped
 *					song's thread leaves behind, so it isn't mistaken for
 *					the new song ending. (After a song really ended this
 *					was already cleared before choosing the next one.)
 */
static void startSong(const char* path, bool manual)
{
	changeFile(path);
	if(manual)
		playbackError = 0;
}

/* Start playlist entry i. */
static void plPlay(int i, bool manual)
{
	char path[PATH_MAX];

	plSetCur(i);
	if(plPath(i, path, sizeof(path)))
		startSong(path, manual);
}

void playerPlayFromBrowser(struct browser* b, int idx)
{
	/* This folder becomes the playlist; shuffle starts a new order from
	 * this song (built lazily, with it first). */
	if(idx < 0 || !plSnapshot(b))
		return;
	pl.cur = idx;
	pl.custom = false;		/* a new playlist: folder order */
	plPlay(idx, true);
}

bool playerPlayPrevious(void)
{
	char	prevPath[PATH_MAX];
	int		i;

	if(historyPop(prevPath, sizeof(prevPath)) != 0)
		return false;

	/* Going back to a playlist song moves the playlist back too. */
	if((i = plFindPath(prevPath)) >= 0)
		plSetCur(i);
	goingBack = true;
	startSong(prevPath, true);
	goingBack = false;
	return true;
}

/**
 * queue -> playlist (shuffle order / next file / Repeat All wrap).
 * See player.h.
 */
static void advance(struct browser* b, bool manual, bool back)
{
	int next;

	if(queueSize(&songQueue) > 0)
	{
		char qPath[PATH_MAX];
		char qTitle[128];

		/* Queued songs don't touch the playlist. */
		if(queuePop(&songQueue, qPath, qTitle) == 0)
			startSong(qPath, manual);
		return;
	}

	if(pl.count == 0)
	{
		/* No playlist yet. R starts one from the folder being browsed,
		 * after the cursor; a song ending never does (only queued songs
		 * have played, so stop when they run out). */
		if(!manual || !plSnapshot(b))
			return;
		pl.cur = browserSelectedFile(b);
	}

	if((next = plNext(back)) < 0)
		return;

	plPlay(next, manual);
	followInBrowser(b);
}

void playerSkip(struct browser* b)
{
	/* R plays what the "up next" list shows: in reverse, the song before
	 * this one in the playlist. */
	advance(b, true, plBackwards());
}

bool playerHandleSongEnd(struct browser* b)
{
	bool back;

	if(playbackError != -1)
		return false;

	playbackError = 0;

	/* Reverse playback ran back to the start of the song: like a song
	 * ending, but the playlist goes backwards (an album in reverse: 144,
	 * 143, ...), and the next song plays backwards from its end. Queued
	 * songs still come first, Repeat One plays this song again. */
	back = playbackTakeReachedStart();

	if(repeatMode == REPEAT_ONE && hasNowPlaying &&
	   nowPlayingFile[0] != '\0')
	{
		/* changeFile() overwrites nowPlayingFile; don't pass it in. */
		char path[sizeof(nowPlayingFile)];
		memcpy(path, nowPlayingFile, sizeof(path));
		startSong(path, false);
		return true;
	}

	advance(b, false, back);
	return true;
}

bool playerTogglePause(void)
{
	if(isPlaying() == false)
		return false;

	togglePlayback();
	uiRequestRedraw();
	return true;
}

bool playerPause(void)
{
	if(isPlaying() == false || isPaused())
		return false;

	togglePlayback();
	uiRequestRedraw();
	return true;
}

bool playerSeek(int seconds)
{
	if(isPlaying() == false || playbackInfo.samples_per_second == 0)
		return false;

	requestSeek((int64_t)playbackInfo.samples_per_second * seconds);
	uiRequestRedraw();
	return true;
}

void playerPlayQueued(int index)
{
	char path[PATH_MAX];

	/* Only this song leaves the queue; the rest stay in their order. */
	if(queueTake(&songQueue, index, path, NULL) != 0)
		return;
	startSong(path, true);
	uiRequestRedraw();
}

void playerEnqueue(const char* absPath, const char* title)
{
	if(queueAdd(&songQueue, absPath, title) == 0)
		uiRequestRedraw();
}

const struct playbackInfo_t* playerInfo(void)
{
	return &playbackInfo;
}

struct queue_t* playerQueue(void)
{
	return &songQueue;
}

const char* playerNowPlayingFile(void)
{
	return hasNowPlaying ? nowPlayingFile : NULL;
}

const struct music_metadata* playerNowPlayingMeta(void)
{
	return &nowPlayingMeta;
}

const char* playerPlaylistDir(void)
{
	return pl.dir;
}

unsigned playerSongId(void)
{
	return songId;
}

bool playerShuffle(void)
{
	return shuffleEnabled;
}

void playerToggleShuffle(void)
{
	playerSetShuffle(!shuffleEnabled);
	uiRequestRedraw();
}

void playerSetShuffle(bool on)
{
	shuffleEnabled = on;
	/* New order, starting from the current playlist song. (Moves the
	 * user made in the folder order are dropped.) */
	pl.orderValid = false;
	pl.custom = false;
	plVersion++;
}

enum repeat_mode playerRepeat(void)
{
	return repeatMode;
}

enum repeat_mode playerCycleRepeat(void)
{
	repeatMode = (repeatMode + 1) % 3;
	return repeatMode;
}

const char* repeatModeStr(enum repeat_mode m)
{
	switch(m)
	{
		case REPEAT_ALL: return "All";
		case REPEAT_ONE: return "One";
		default:         return "Off";
	}
}

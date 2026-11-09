/**
 * ctrmus - 3DS Music Player
 * App-level playback control: starting songs on the playback thread
 * (playback.h), now-playing metadata, the song queue, history (L = back),
 * shuffle/repeat modes, and choosing the next song.
 *
 * The playlist is the folder the current song was started from in the
 * browser (a snapshot: browsing elsewhere doesn't change it, and queued
 * songs don't replace it).
 *
 * Next-song order, for both R (skip) and a song ending:
 *   [song end only: Repeat One replays the current song]
 *   queue -> the playlist: next in its shuffle order (reshuffled when it
 *   runs out) or the next file -> Repeat All wraps to the first file.
 * With no playlist yet, R starts one from the browsed folder (after the
 * cursor).
 */

#ifndef ctrmus_player_h
#define ctrmus_player_h

#include <stdbool.h>

#include "metadata.h"
#include "playback.h"
#include "queue.h"

struct browser;

enum repeat_mode
{
	REPEAT_OFF,
	REPEAT_ALL,
	REPEAT_ONE
};

/** Set up the queue, error event and watchdog thread. */
void playerInit(void);

/** Stop playback and the watchdog thread. */
void playerExit(void);

/**
 * Play file idx of the browser's folder now; that folder becomes the
 * playlist. The current song is pushed to the history. The queue is not
 * changed.
 */
void playerPlayFromBrowser(struct browser* b, int idx);

/**
 * The browser's listing changed (titles loaded, re-sorted, refreshed): if
 * it shows the playlist's folder, update the playlist to match, keeping
 * the current song and the shuffle order.
 */
void playerSyncPlaylist(const struct browser* b);

/** Play the previous song from the history, if any. */
void playerPlayPrevious(void);

/** Skip to the next song (R). */
void playerSkip(struct browser* b);

/**
 * If the current song has finished, start the next one.
 *
 * \return	true if a song had ended.
 */
bool playerHandleSongEnd(struct browser* b);

/**
 * Pause/resume and log it.
 *
 * \return	false if nothing is playing (nothing happens).
 */
bool playerTogglePause(void);

/** Pause if a song is playing and not paused already. True if it paused. */
bool playerPause(void);

/**
 * Seek by a number of seconds and log it.
 *
 * \return	false if nothing is playing (nothing happens).
 */
bool playerSeek(int seconds);

/**
 * The "playlist" view: titles of the playlist songs in the order they will
 * play after the queue (shuffle order when shuffle is on), starting with
 * the current song while it is the one playing. With no playlist yet, the
 * browsed folder after the cursor.
 *
 * \param	titles			Out: valid until the playlist changes.
 * \param	files			Out: the file names (in *dir), same order.
 * \param	dir				Out: the folder they are in.
 * \param	firstIsCurrent	Out: titles[0] is the song playing now.
 * \param	total			Out: number of songs in the playlist.
 * \return					Number of entries written (<= max).
 */
int playerUpcoming(const struct browser* b, const char** titles,
                   const char** files, int max, bool* firstIsCurrent,
                   int* total, const char** dir);

/**
 * The playlist songs that play after the current one, in play order (the
 * shuffle order, the user's own order, or the folder order), not
 * including the one playing. Strings valid until the playlist changes
 * (playerPlaylistVersion()).
 *
 * \return	Number written (<= max); 0 with no playlist yet.
 */
int playerPlaylistAfter(const char** titles, const char** files, int max);

/** Changes whenever playerPlaylistAfter() would give a different list. */
unsigned playerPlaylistVersion(void);

/**
 * Play the k-th song of playerPlaylistAfter() now. The playlist carries
 * on from it; the songs before it are skipped over. The queue isn't
 * touched.
 */
void playerPlaylistPlay(struct browser* b, int k);

/**
 * Swap the k-th song of playerPlaylistAfter() with its neighbour
 * (dir -1 or 1). In folder order the first move switches to a custom
 * order (dropped again when shuffle is toggled or a new folder starts).
 *
 * \return	false if it can't move that way.
 */
bool playerPlaylistMove(int k, int dir);

/** Add a file to the queue (redraws the top screen). */
void playerEnqueue(const char* absPath, const char* title);

/**
 * Play queued song `index` now and take just it out of the queue: with
 * A B C D queued, playing C leaves A B D. The playlist isn't touched.
 */
void playerPlayQueued(int index);

/* State used by the UI and menu. */
const struct playbackInfo_t*	playerInfo(void);
struct queue_t*					playerQueue(void);
/** Path of the current song, or NULL if nothing has been played. */
const char*						playerNowPlayingFile(void);
const struct music_metadata*	playerNowPlayingMeta(void);
/** Folder of the playlist (absolute), or NULL before anything played. */
const char*						playerPlaylistDir(void);

/** Changes every time a song starts (also when the same file restarts). */
unsigned						playerSongId(void);

bool				playerShuffle(void);
void				playerSetShuffle(bool on);
/** Flip shuffle and log "shuffle:on/off" (START, and the menu). */
void				playerToggleShuffle(void);
enum repeat_mode	playerRepeat(void);
/** Cycle Off -> All -> One -> Off; returns the new mode. */
enum repeat_mode	playerCycleRepeat(void);
const char*			repeatModeStr(enum repeat_mode m);

#endif

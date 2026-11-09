/**
 * ctrmus - 3DS Music Player
 * Queue system for managing a playlist of songs.
 */

#ifndef ctrmus_queue_h
#define ctrmus_queue_h

#include <limits.h>

#define MAX_QUEUE_SIZE 64

struct queue_entry
{
	char filepath[PATH_MAX];
	char title[128];
};

struct queue_t
{
	struct queue_entry entries[MAX_QUEUE_SIZE];
	int count;
	unsigned version;	/* bumped on every change (screens redraw on it) */
	unsigned pops;		/* bumped by queuePop() (front entry played) */
};

/**
 * Initialize queue to empty state.
 */
void queueInit(struct queue_t* q);

/**
 * Add a song to the end of the queue.
 *
 * \param	q			Queue to add to.
 * \param	filepath	Absolute path to the audio file.
 * \param	title		Display title for the song.
 * \return				0 on success, -1 if queue is full.
 */
int queueAdd(struct queue_t* q, const char* filepath, const char* title);

/**
 * Remove and return the first item in the queue (shift all others down).
 *
 * \param	q			Queue to pop from.
 * \param	outPath		Output buffer for file path (PATH_MAX).
 * \param	outTitle	Output buffer for title (128 bytes).
 * \return				0 on success, -1 if queue is empty.
 */
int queuePop(struct queue_t* q, char* outPath, char* outTitle);

/**
 * Clear all entries from the queue.
 */
void queueClear(struct queue_t* q);

/**
 * Get the number of items in the queue.
 */
int queueSize(const struct queue_t* q);

/** Remove entry `index` (the ones after it move up). */
void queueRemove(struct queue_t* q, int index);

/**
 * Remove entry `index` and copy it out, like queuePop() for any entry
 * (the queue editor's "play now": the others stay queued).
 *
 * \return	0 on success, -1 if index is out of range.
 */
int queueTake(struct queue_t* q, int index, char* outPath, char* outTitle);

/** Move entry `from` to position `to`; the ones between shift over. */
void queueMove(struct queue_t* q, int from, int to);

/**
 * Get a queue entry by index (0-based). Does not remove it.
 *
 * \return	Pointer to entry, or NULL if index out of range.
 */
const struct queue_entry* queueGet(const struct queue_t* q, int index);

#endif

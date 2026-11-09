/**
 * ctrmus - 3DS Music Player
 * Queue implementation.
 */

#include <string.h>

#include "queue.h"

void queueInit(struct queue_t* q)
{
	if(q == NULL)
		return;

	q->count = 0;
	q->version = 0;
	q->pops = 0;
	memset(q->entries, 0, sizeof(q->entries));
}

int queueAdd(struct queue_t* q, const char* filepath, const char* title)
{
	if(q == NULL || filepath == NULL || q->count >= MAX_QUEUE_SIZE)
		return -1;

	strncpy(q->entries[q->count].filepath, filepath,
	        sizeof(q->entries[q->count].filepath) - 1);
	q->entries[q->count].filepath[sizeof(q->entries[q->count].filepath) - 1] = '\0';

	if(title != NULL)
	{
		strncpy(q->entries[q->count].title, title,
		        sizeof(q->entries[q->count].title) - 1);
		q->entries[q->count].title[sizeof(q->entries[q->count].title) - 1] = '\0';
	}
	else
	{
		/* Use filename as title */
		const char* slash = strrchr(filepath, '/');
		const char* name = slash ? slash + 1 : filepath;
		strncpy(q->entries[q->count].title, name,
		        sizeof(q->entries[q->count].title) - 1);
		q->entries[q->count].title[sizeof(q->entries[q->count].title) - 1] = '\0';
	}

	q->count++;
	q->version++;
	return 0;
}

int queuePop(struct queue_t* q, char* outPath, char* outTitle)
{
	if(queueTake(q, 0, outPath, outTitle) != 0)
		return -1;
	q->pops++;
	return 0;
}

int queueTake(struct queue_t* q, int index, char* outPath, char* outTitle)
{
	if(q == NULL || index < 0 || index >= q->count)
		return -1;

	if(outPath != NULL)
	{
		memcpy(outPath, q->entries[index].filepath, PATH_MAX - 1);
		outPath[PATH_MAX - 1] = '\0';
	}
	if(outTitle != NULL)
	{
		memcpy(outTitle, q->entries[index].title, 127);
		outTitle[127] = '\0';
	}

	queueRemove(q, index);
	return 0;
}

void queueRemove(struct queue_t* q, int index)
{
	if(q == NULL || index < 0 || index >= q->count)
		return;

	/* Shift the entries after it forward */
	memmove(&q->entries[index], &q->entries[index + 1],
	        (size_t)(q->count - index - 1) * sizeof(q->entries[0]));
	q->count--;
	memset(&q->entries[q->count], 0, sizeof(q->entries[q->count]));
	q->version++;
}

void queueMove(struct queue_t* q, int from, int to)
{
	struct queue_entry tmp;

	if(q == NULL || from < 0 || from >= q->count || to < 0 ||
	   to >= q->count || from == to)
		return;

	tmp = q->entries[from];
	if(from < to)
		memmove(&q->entries[from], &q->entries[from + 1],
		        (size_t)(to - from) * sizeof(q->entries[0]));
	else
		memmove(&q->entries[to + 1], &q->entries[to],
		        (size_t)(from - to) * sizeof(q->entries[0]));
	q->entries[to] = tmp;
	q->version++;
}

void queueClear(struct queue_t* q)
{
	if(q == NULL)
		return;

	q->count = 0;
	memset(q->entries, 0, sizeof(q->entries));
	q->version++;
}

int queueSize(const struct queue_t* q)
{
	if(q == NULL)
		return 0;

	return q->count;
}

const struct queue_entry* queueGet(const struct queue_t* q, int index)
{
	if(q == NULL || index < 0 || index >= q->count)
		return NULL;

	return &q->entries[index];
}

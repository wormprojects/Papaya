/**
 * ctrmus - 3DS Music Player
 * Small buffered, seek-friendly, read-only file reader. See bufio.h.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bufio.h"

/* First read after a non-sequential seek. Big enough for an Ogg page header
 * plus a typical page, or a FLAC frame header; small enough that bisection
 * probes stay cheap. */
#define BUFIO_MIN_READ	(4 * 1024)

int bufioOpen(struct bufio* b, const char* path, size_t bufCap)
{
	off_t end;

	memset(b, 0, sizeof(*b));
	b->fd = -1;

	if(bufCap < BUFIO_MIN_READ)
		bufCap = BUFIO_MIN_READ;

	if((b->fd = open(path, O_RDONLY)) < 0)
		return -1;

	if((end = lseek(b->fd, 0, SEEK_END)) < 0 ||
	   lseek(b->fd, 0, SEEK_SET) < 0 ||
	   (b->buf = malloc(bufCap)) == NULL)
	{
		close(b->fd);
		b->fd = -1;
		return -1;
	}

	b->size = (int64_t)end;
	b->bufCap = bufCap;
	b->readAhead = BUFIO_MIN_READ;
	b->nextSeq = -1;
	return 0;
}

void bufioClose(struct bufio* b)
{
	if(b->fd >= 0)
		close(b->fd);

	free(b->buf);
	memset(b, 0, sizeof(*b));
	b->fd = -1;
}

/**
 * Read from the descriptor at an absolute offset, looping over short reads.
 */
static size_t physRead(struct bufio* b, int64_t at, void* dst, size_t n)
{
	size_t got = 0;

	if(b->fdPos != at)
	{
		if(lseek(b->fd, (off_t)at, SEEK_SET) < 0)
			return 0;
		b->fdPos = at;
	}

	while(got < n)
	{
		ssize_t r = read(b->fd, (unsigned char*)dst + got, n - got);
		if(r <= 0)
			break;
		got += (size_t)r;
	}

	b->fdPos = at + (int64_t)got;
	b->nextSeq = b->fdPos;
	return got;
}

size_t bufioRead(struct bufio* b, void* dst, size_t n)
{
	unsigned char*	out = dst;
	size_t			total = 0;

	while(n > 0)
	{
		int64_t bufEnd = b->bufStart + (int64_t)b->bufLen;

		/* Serve from the buffer if possible. */
		if(b->pos >= b->bufStart && b->pos < bufEnd)
		{
			size_t off = (size_t)(b->pos - b->bufStart);
			size_t c = b->bufLen - off;

			if(c > n)
				c = n;

			memcpy(out, b->buf + off, c);
			out += c;
			n -= c;
			total += c;
			b->pos += (int64_t)c;
			continue;
		}

		if(b->pos >= b->size)
			break;

		/* Continuing forward from (or slightly past) the last physical
		 * read counts as sequential: grow the read size. Anything else is a
		 * random seek: start small again. */
		if(b->nextSeq >= 0 && b->pos >= b->nextSeq &&
		   b->pos - b->nextSeq <= (int64_t)b->readAhead)
		{
			b->readAhead *= 2;
			if(b->readAhead > b->bufCap)
				b->readAhead = b->bufCap;
		}
		else
			b->readAhead = BUFIO_MIN_READ;

		/* Large request: read straight into the caller's memory. */
		if(n >= b->readAhead)
		{
			size_t r = physRead(b, b->pos, out, n);

			if(r == 0)
				break;

			out += r;
			n -= r;
			total += r;
			b->pos += (int64_t)r;
			continue;
		}

		{
			size_t r = physRead(b, b->pos, b->buf, b->readAhead);

			if(r == 0)
				break;

			b->bufStart = b->pos;
			b->bufLen = r;
		}
	}

	return total;
}

int bufioSeek(struct bufio* b, int64_t offset, int whence)
{
	int64_t target;

	switch(whence)
	{
		case SEEK_SET:
			target = offset;
			break;

		case SEEK_CUR:
			target = b->pos + offset;
			break;

		case SEEK_END:
			target = b->size + offset;
			break;

		default:
			return -1;
	}

	if(target < 0)
		return -1;

	b->pos = target;
	return 0;
}

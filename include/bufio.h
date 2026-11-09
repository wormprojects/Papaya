/**
 * ctrmus - 3DS Music Player
 * Small buffered, seek-friendly, read-only file reader.
 *
 * Every read() on the 3DS SD card is a round trip to the FS service, so
 * many tiny reads are very slow. newlib's stdio does not help: with the
 * default 1 KiB buffer every small fread() is a syscall, and with a large
 * setvbuf() buffer every fseek() throws the buffer away and the next
 * fread() refills the *whole* buffer, even when the target is already in
 * it (newlib never enables its fseek optimisation for sdmc files).
 *
 * This reader instead:
 *  - serves seeks that land inside the buffer without any I/O,
 *  - starts with small reads after a far seek (cheap bisection probes for
 *    the Ogg/FLAC seekers) and doubles the read size while access stays
 *    sequential (few syscalls for normal decoding and frame scanning),
 *  - reads large requests straight into the caller's buffer.
 *
 * One instance must only be used by one thread at a time.
 */

#ifndef ctrmus_bufio_h
#define ctrmus_bufio_h

#include <stddef.h>
#include <stdint.h>

struct bufio
{
	int				fd;
	int64_t			size;		/* File size in bytes. */
	int64_t			pos;		/* Logical read position. */
	int64_t			fdPos;		/* Position of the underlying descriptor. */
	int64_t			nextSeq;	/* End of the last physical read. */
	int64_t			bufStart;	/* File offset of buf[0]. */
	size_t			bufLen;		/* Valid bytes in buf. */
	size_t			bufCap;
	size_t			readAhead;	/* Size of the next buffer refill. */
	unsigned char*	buf;
};

/**
 * Open a file for buffered reading.
 *
 * \param	b		Reader to initialise.
 * \param	path	File to open.
 * \param	bufCap	Maximum buffer size in bytes.
 * \return			0 on success, -1 on failure (b is left closed).
 */
int bufioOpen(struct bufio* b, const char* path, size_t bufCap);

/**
 * Close the reader. Safe to call on a reader that is already closed or was
 * zero-initialised with fd set to -1.
 */
void bufioClose(struct bufio* b);

/**
 * Read up to n bytes at the current position.
 *
 * \return	Bytes read; less than n only at end of file or on error.
 */
size_t bufioRead(struct bufio* b, void* dst, size_t n);

/**
 * Move the read position. Seeking past the end is allowed (reads there
 * return 0), like lseek().
 *
 * \param	whence	SEEK_SET, SEEK_CUR or SEEK_END.
 * \return			0 on success, -1 if the resulting position is negative
 *					or whence is invalid.
 */
int bufioSeek(struct bufio* b, int64_t offset, int whence);

static inline int64_t bufioTell(const struct bufio* b)
{
	return b->pos;
}

static inline int64_t bufioSize(const struct bufio* b)
{
	return b->size;
}

#endif

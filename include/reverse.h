/**
 * Reverse playback: the next block of audio going backwards.
 *
 * Shared by source/playback.c (3DS) and macos/engine/playback_mac.c, and
 * checked on the host by tools/reverse/run.sh.
 *
 * Decoders only go forwards, so a block going backwards is made by seeking
 * to the block just before the position, decoding it forwards and reversing
 * the order of its frames. Blocks are played from the end of the song to its
 * start, each one reversed, which sounds like the whole song reversed.
 */

#ifndef ctrmus_reverse_h
#define ctrmus_reverse_h

#include <stddef.h>
#include <stdint.h>

#include "playback.h"

struct reverseState
{
	int64_t	pos;	/* interleaved samples: the next block ends here */
	size_t	cap;	/* block length in samples; 0 = start with buffSize */
};

/**
 * Reverse the order of the frames in buf (interleaved, ch channels each,
 * `samples` samples in all); L/R stay in place inside a frame.
 */
static inline void reverseFrames(int16_t* buf, size_t samples, size_t ch)
{
	for(size_t a = 0, b = samples / ch; b > 0 && a < b - 1; a++, b--)
		for(size_t c = 0; c < ch; c++)
		{
			int16_t t = buf[a * ch + c];

			buf[a * ch + c] = buf[(b - 1) * ch + c];
			buf[(b - 1) * ch + c] = t;
		}
}

/**
 * Call before the first backwards block of a file: lets the decoder make
 * its seeking exact. Returns the (possibly more exact) length in samples, or
 * 0 if the decoder has no length.
 */
static inline size_t reversePrepare(struct decoder_fn* d)
{
	if(d->prepareExactSeek != NULL)
		(*d->prepareExactSeek)();
	return d->getFileSamples != NULL ? d->getFileSamples() : 0;
}

/**
 * Decode the block before r->pos, reversed, into buf (at least buffSize
 * samples), and move r->pos back to the block's start.
 *
 * Some decoders return less than buffSize per decode() (mpg123 gives half:
 * it is asked for buffSize bytes). Then the block length shrinks to what the
 * decoder gives (r->cap) and the block is decoded again, so the blocks line
 * up with no audio skipped.
 *
 * \return	Interleaved samples in buf; 0 = the start of the song (or a
 *			failed seek).
 */
static inline size_t reverseFill(struct decoder_fn* d, int16_t* buf,
                                 struct reverseState* r)
{
	size_t	ch = (*d->channels)();
	size_t	n, got = 0;
	int64_t	start = 0;

	if(r->pos <= 0 || d->seek == NULL)
		return 0;
	if(r->cap == 0)
		r->cap = d->buffSize;

	n = r->cap;
	if((int64_t)n > r->pos)
		n = (size_t)r->pos;
	n -= n % ch;
	if(n == 0)
		return 0;

	for(int attempt = 0; attempt < 2; attempt++)
	{
		start = r->pos - (int64_t)n;
		if((*d->seek)((size_t)start) != 0)
			return 0;
		got = (*d->decode)(buf);
		got -= got % ch;
		/* Much less than asked for: the file ends here, not the decoder's
		 * block size; use what there is. */
		if(got >= n || got == 0 || got * 4 < n)
			break;
		if(attempt == 0)
		{
			r->cap = got;		/* the decoder's block size */
			n = got;
		}
	}
	if(got > n)
		got = n;
	if(got == 0)
		return 0;

	reverseFrames(buf, got, ch);
	r->pos = start;
	return got;
}

#endif

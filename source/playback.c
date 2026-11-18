#include <3ds.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "all.h"
#include "effects.h"
#include "error.h"
#include "file.h"
#include "flac.h"
#include "m4a.h"
#include "mp3.h"
#include "opus.h"
#include "playback.h"
#include "reverse.h"
#include "vorbis.h"
#include "wav.h"
#include "sid.h"

static volatile bool stop = true;
/* Pending seek in interleaved samples (0 = none). Requests made while a
 * seek is still being processed accumulate. Accessed with atomics: a plain
 * 64-bit volatile could tear on the 32-bit ARM11, and a read-then-clear
 * could drop a request made in between. */
static int32_t seekRequest = 0;
static volatile float playbackSpeed = 1.0f;
static volatile uint32_t baseRate = 0;  /* Original sample rate (Hz) */
static volatile bool reverseAllowed = false;
static volatile bool zeroPaused = false;	/* paused by speed 0.0 */
static volatile bool reachedStart = false;	/* reverse ran out of song */

/**
 * Push the current speed to the ndsp channel. The channel's rate must stay
 * positive: the sign (direction) is handled by the playback loop, and 0.0
 * pauses the channel instead.
 */
static void applySpeed(void)
{
	float s = fabsf(playbackSpeed);

	if(stop || baseRate == 0)
		return;

	if(s < 0.05f)
	{
		if(!ndspChnIsPaused(CHANNEL))
		{
			ndspChnSetPaused(CHANNEL, true);
			zeroPaused = true;
		}
		return;
	}

	ndspChnSetRate(CHANNEL, (float)baseRate * s);
	if(zeroPaused)
	{
		zeroPaused = false;
		ndspChnSetPaused(CHANNEL, false);
	}
}

/* A block finished playing: the position moves by its length, backwards
 * when reversed (never below 0). */
static void advancePlayed(struct playbackInfo_t* info, size_t n, bool rev)
{
	if(!rev)
		info->samples_played += n;
	else
		info->samples_played = info->samples_played > n ?
		                       info->samples_played - n : 0;
}

/**
 * Pause or play current file.
 *
 * \return	True if paused.
 */
bool togglePlayback(void)
{
	bool paused = ndspChnIsPaused(CHANNEL);
	ndspChnSetPaused(CHANNEL, !paused);
	return !paused;
}

/* The next block of audio: forwards, or backwards (reverse.h). */
static size_t fillBlock(struct decoder_fn* dec, int16_t* buf, bool rev,
                        struct reverseState* rs)
{
	return rev ? reverseFill(dec, buf, rs) : (*dec->decode)(buf);
}

/**
 * Stops current playback. Playback thread should exit as a result.
 */
void stopPlayback(void)
{
	stop = true;
}

/**
 * Request a seek by the given number of signed samples.
 * Thread-safe: sets a flag that the playback loop checks.
 */
void requestSeek(int64_t deltaSamples)
{
	/* +-2^30 samples is over 3 hours even at 96 kHz stereo. */
	const int64_t limit = 1 << 30;

	if(deltaSamples > limit)
		deltaSamples = limit;
	if(deltaSamples < -limit)
		deltaSamples = -limit;

	__atomic_add_fetch(&seekRequest, (int32_t)deltaSamples,
	                   __ATOMIC_SEQ_CST);
}

/**
 * Set playback speed multiplier. Takes effect immediately.
 */
void setPlaybackSpeed(float speed)
{
	if(reverseAllowed)
	{
		if(speed < -3.0f) speed = -3.0f;
		/* Tiny values are 0.0 (paused). */
		if(fabsf(speed) < 0.05f) speed = 0.0f;
	}
	else if(speed < 0.1f)
		speed = 0.1f;
	if(speed > 3.0f) speed = 3.0f;
	playbackSpeed = speed;
	applySpeed();
}

void setReverseAllowed(bool allowed)
{
	reverseAllowed = allowed;
	if(!allowed && playbackSpeed < 0.1f)
		setPlaybackSpeed(1.0f);
}

bool getReverseAllowed(void)
{
	return reverseAllowed;
}

bool playbackTakeReachedStart(void)
{
	bool r = reachedStart;

	reachedStart = false;
	return r;
}

/**
 * Get current playback speed multiplier.
 */
float getPlaybackSpeed(void)
{
	return playbackSpeed;
}

/**
 * Returns whether music is playing or paused.
 */
bool isPlaying(void)
{
	return !stop;
}

bool isPaused(void)
{
	return ndspChnIsPaused(CHANNEL);
}

/**
 * Should only be called from a new thread only, and have only one playback
 * thread at time. This function has not been written for more than one
 * playback thread in mind.
 *
 * \param	infoIn	Playback information.
 */
void playFile(void* infoIn)
{
	struct decoder_fn decoder = { 0 };
	struct playbackInfo_t* info = infoIn;
	int16_t*		buffer1 = NULL;
	int16_t*		buffer2 = NULL;
	ndspWaveBuf		waveBuf[2];
	bool			lastbuf = false;
	int				ret = -1;
	const char*		file = info->file;
	bool			isNdspInit = false;
	bool			rev = false;		/* playing backwards */
	bool			canRev = false;
	struct reverseState	rs = { 0, 0 };	/* where the next backwards block is */

	/* Reset previous stop command and any seek meant for the last song */
	stop = false;
	reachedStart = false;
	zeroPaused = false;
	__atomic_store_n(&seekRequest, 0, __ATOMIC_SEQ_CST);

	switch(getFileType(file))
	{
		case FILE_TYPE_WAV:
			setWav(&decoder);
			break;

		case FILE_TYPE_FLAC:
			setFlac(&decoder);
			break;

		case FILE_TYPE_OPUS:
			setOpus(&decoder);
			break;

		case FILE_TYPE_MP3:
			setMp3(&decoder);
			break;

		case FILE_TYPE_VORBIS:
			setVorbis(&decoder);
			break;
		
		case FILE_TYPE_SID:
			setSid(&decoder);
			break;

		case FILE_TYPE_M4A:
			setM4a(&decoder);
			break;

		default:
			goto err;
	}

	if(ndspInit() < 0)
	{
		errno = NDSP_INIT_FAIL;
		goto err;
	}

	isNdspInit = true;

	if((ret = (*decoder.init)(file)) != 0)
	{
		errno = DECODER_INIT_FAIL;
		goto err;
	}

	if((*decoder.channels)() > 2 || (*decoder.channels)() < 1)
	{
		errno = UNSUPPORTED_CHANNELS;
		goto err;
	}

	if(decoder.getFileSamples != NULL)
		info->samples_total = decoder.getFileSamples();

	/* Backwards needs to seek and to know where the end is. */
	canRev = decoder.seek != NULL && info->samples_total > 0;
	rev = canRev && playbackSpeed < 0.0f;
	if(rev)
	{
		size_t exact = reversePrepare(&decoder);

		if(exact > 0)
			info->samples_total = exact;
		/* Starts at the end (e.g. the previous song, played backwards). */
		rs.pos = (int64_t)info->samples_total;
		info->samples_played = info->samples_total;
	}

	info->samples_per_second = decoder.rate() * decoder.channels();
	buffer1 = linearAlloc(decoder.buffSize * sizeof(int16_t));
	buffer2 = linearAlloc(decoder.buffSize * sizeof(int16_t));

	baseRate = (*decoder.rate)();
	ndspChnReset(CHANNEL);
	ndspChnWaveBufClear(CHANNEL);
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetInterp(CHANNEL, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(CHANNEL, (float)baseRate *
	               (fabsf(playbackSpeed) < 0.05f ? 1.0f :
	                fabsf(playbackSpeed)));
	ndspChnSetFormat(CHANNEL,
			(*decoder.channels)() == 2 ? NDSP_FORMAT_STEREO_PCM16 :
			NDSP_FORMAT_MONO_PCM16);

	effectsStart(baseRate, (*decoder.channels)());

	memset(waveBuf, 0, sizeof(waveBuf));
	waveBuf[0].nsamples = fillBlock(&decoder, &buffer1[0], rev, &rs) /
	                      (*decoder.channels)();
	waveBuf[0].data_vaddr = &buffer1[0];

	waveBuf[1].nsamples = fillBlock(&decoder, &buffer2[0], rev, &rs) /
	                      (*decoder.channels)();
	waveBuf[1].data_vaddr = &buffer2[0];

	/* Distortion/reverb (effects.h). */
	effectsProcess(buffer1, waveBuf[0].nsamples);
	effectsProcess(buffer2, waveBuf[1].nsamples);

	DSP_FlushDataCache(buffer1, decoder.buffSize * sizeof(int16_t));
	DSP_FlushDataCache(buffer2, decoder.buffSize * sizeof(int16_t));

	ndspChnWaveBufAdd(CHANNEL, &waveBuf[0]);
	ndspChnWaveBufAdd(CHANNEL, &waveBuf[1]);

	/**
	 * There may be a chance that the music has not started by the time we get
	 * to the while loop. So we ensure that music has started here.
	 */
	while(ndspChnIsPlaying(CHANNEL) == false);

	applySpeed();		/* a speed of 0.0 starts paused */

	while(stop == false)
	{
		/* 1 ms: each buffer holds 0.18-0.4 s of audio, so this is plenty.
		 * (It was 100 us: 10000 wake-ups a second on the highest-priority
		 * thread, a few percent of an Old 3DS core for nothing.) */
		svcSleepThread(1000 * 1000);

		/* When the last buffer has finished playing, break. */
		if(lastbuf == true && waveBuf[0].status == NDSP_WBUF_DONE &&
				waveBuf[1].status == NDSP_WBUF_DONE)
		{
			/* Backwards, that was the start of the song. */
			reachedStart = rev;
			break;
		}

		/* Speed 0.0 holds the song (the pause button can't undo it). */
		if(playbackSpeed == 0.0f && !ndspChnIsPaused(CHANNEL))
		{
			ndspChnSetPaused(CHANNEL, true);
			zeroPaused = true;
		}

		if(ndspChnIsPaused(CHANNEL) == true || lastbuf == true)
			continue;

		/* The sign of the speed flipped: carry on from here the other
		 * way (handled like a seek by 0). */
		bool wantRev = canRev && (playbackSpeed < 0.0f ||
		                          (playbackSpeed == 0.0f && rev));
		bool dirChange = wantRev != rev;

		/* Handle seek request */
		if((__atomic_load_n(&seekRequest, __ATOMIC_SEQ_CST) != 0 ||
		    dirChange) && decoder.seek != NULL)
		{
			int64_t delta = __atomic_exchange_n(&seekRequest, 0,
			                                    __ATOMIC_SEQ_CST);

			if(wantRev && !rev)
			{
				size_t exact = reversePrepare(&decoder);

				if(exact > 0)
					info->samples_total = exact;
			}
			rev = wantRev;

			/* Calculate target sample position */
			int64_t currentPos = (int64_t)info->samples_played;
			int64_t targetPos = currentPos + delta;

			if(targetPos < 0)
				targetPos = 0;

			/* Seeking past the end finishes the song, so the next one
			 * starts (as if it had played to the end). */
			if(decoder.getFileSamples != NULL)
			{
				int64_t total = (int64_t)decoder.getFileSamples();
				if(delta > 0 && total > 0 && targetPos >= total)
					break;
			}

			if(rev || decoder.seek((size_t)targetPos) == 0)
			{
				rs.pos = targetPos;

				/* Pause channel briefly, clear pending buffers, refill */
				ndspChnSetPaused(CHANNEL, true);
				ndspChnWaveBufClear(CHANNEL);

				info->samples_played = (size_t)targetPos;
				lastbuf = false;

				/* Re-fill both buffers */
				memset(waveBuf, 0, sizeof(waveBuf));
				waveBuf[0].nsamples = fillBlock(&decoder, &buffer1[0], rev, &rs) / (*decoder.channels)();
				waveBuf[0].data_vaddr = &buffer1[0];
				waveBuf[1].nsamples = fillBlock(&decoder, &buffer2[0], rev, &rs) / (*decoder.channels)();
				waveBuf[1].data_vaddr = &buffer2[0];

				effectsProcess(buffer1, waveBuf[0].nsamples);
				effectsProcess(buffer2, waveBuf[1].nsamples);

				DSP_FlushDataCache(buffer1, decoder.buffSize * sizeof(int16_t));
				DSP_FlushDataCache(buffer2, decoder.buffSize * sizeof(int16_t));

				/* Landed at (or near) the end: nothing or only one
				 * buffer's worth left. A buffer that isn't queued never
				 * reports DONE, which used to leave the loop waiting
				 * forever (silent, not paused); mark it done and let the
				 * song finish normally. */
				if(waveBuf[0].nsamples == 0)
				{
					ndspChnSetPaused(CHANNEL, false);
					reachedStart = rev;
					break;
				}
				ndspChnWaveBufAdd(CHANNEL, &waveBuf[0]);
				if(waveBuf[1].nsamples > 0)
					ndspChnWaveBufAdd(CHANNEL, &waveBuf[1]);
				else
				{
					waveBuf[1].status = NDSP_WBUF_DONE;
					lastbuf = true;
				}

				ndspChnSetPaused(CHANNEL, false);
			}

			continue;
		}
		else if(__atomic_load_n(&seekRequest, __ATOMIC_SEQ_CST) != 0)
		{
			/* Decoder doesn't support seeking, discard request */
			__atomic_store_n(&seekRequest, 0, __ATOMIC_SEQ_CST);
		}

		if(waveBuf[0].status == NDSP_WBUF_DONE)
		{
			size_t read = fillBlock(&decoder, &buffer1[0], rev, &rs);
			/* The previous block of samples have finished playing,
			 * so accumulate them here. */
			advancePlayed(info, waveBuf[0].nsamples * decoder.channels(),
			              rev);

			if(read <= 0)
			{
				lastbuf = true;
				continue;
			}
			else if(read / (*decoder.channels)() != waveBuf[0].nsamples)
				waveBuf[0].nsamples = read / (*decoder.channels)();

			effectsProcess(buffer1, read / (*decoder.channels)());
			ndspChnWaveBufAdd(CHANNEL, &waveBuf[0]);
		}

		if(waveBuf[1].status == NDSP_WBUF_DONE)
		{
			size_t read = fillBlock(&decoder, &buffer2[0], rev, &rs);
			advancePlayed(info, waveBuf[1].nsamples * decoder.channels(),
			              rev);

			if(read <= 0)
			{
				lastbuf = true;
				continue;
			}
			else if(read / (*decoder.channels)() != waveBuf[1].nsamples)
				waveBuf[1].nsamples = read / (*decoder.channels)();

			effectsProcess(buffer2, read / (*decoder.channels)());
			ndspChnWaveBufAdd(CHANNEL, &waveBuf[1]);
		}

		DSP_FlushDataCache(buffer1, decoder.buffSize * sizeof(int16_t));
		DSP_FlushDataCache(buffer2, decoder.buffSize * sizeof(int16_t));
	}

	if(!rev)
	{
		info->samples_played += waveBuf[0].nsamples * decoder.channels();
		info->samples_played += waveBuf[0].nsamples * decoder.channels();
	}

	(*decoder.exit)();
out:
	if(isNdspInit == true)
	{
		ndspChnWaveBufClear(CHANNEL);
		ndspExit();
	}

	linearFree(buffer1);
	linearFree(buffer2);

	/* Signal Watchdog thread that we've stopped playing */
	*info->errInfo->error = -1;
	svcSignalEvent(*info->errInfo->failEvent);

	threadExit(0);
	return;

err:
	*info->errInfo->error = errno;
	svcSignalEvent(*info->errInfo->failEvent);
	goto out;
}

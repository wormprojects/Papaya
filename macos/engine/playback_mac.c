/**
 * Papaya for macOS (native) - playback.h on CoreAudio.
 *
 * Replaces source/playback.c (ndsp) on this branch. Same contract: player.c
 * starts playFile() on a thread, togglePlayback()/requestSeek()/
 * setPlaybackSpeed() are called from the UI, and the decoders and the
 * effects chain are the 3DS app's own. playFile() decodes into a ring
 * buffer; a CoreAudio render callback pulls from it, resamples for the
 * playback speed (pitch changes with speed, like ndsp) and applies the
 * volume.
 */

#include <AudioToolbox/AudioToolbox.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "effects.h"
#include "file.h"
#include "flac.h"
#include "m4a.h"
#include "mp3.h"
#include "opus.h"
#include "playback.h"
#include "reverse.h"
#include "sid.h"
#include "vorbis.h"
#include "wav.h"

/* Ring of decoded stereo frames (int16 L,R) between the decoder thread
 * (producer) and the render callback (consumer). Power of two. About 1.5 s
 * at 44.1 kHz: long enough to ride out a slow disk read, short enough that
 * seeks and speed changes are felt quickly. */
#define RING_FRAMES		65536u
#define RING_MASK		(RING_FRAMES - 1)

static int16_t ring[RING_FRAMES * 2];
static _Atomic uint64_t ringHead;	/* frames written (decoder thread) */
static _Atomic uint64_t ringTail;	/* frames consumed (render thread) */

/* Render-thread state. */
static AudioUnit	outUnit;
static bool			unitRunning;
static double		frac;			/* resampler phase, source frames */
static int16_t		prev[2][2];		/* last 2 source frames per channel */
static _Atomic uint64_t playedFrames;	/* source frames consumed + base */

/* Shared settings. */
/* error.h's codes (that header pulls in <3ds.h>, whose `Handle` clashes with
 * CoreServices', so the signalling lives in playback_signal.c). */
#define NDSP_INIT_FAIL			1000
#define DECODER_INIT_FAIL		1001
#define UNSUPPORTED_CHANNELS	1003
void macPlaybackDone(struct playbackInfo_t* info, int error);

static _Atomic float	speed = 1.0f;	/* negative = backwards */
static _Atomic bool		reverseAllowed;
static _Atomic bool		reachedStart;	/* reverse ran back to the start */
/* Direction of the audio in the ring (set by the decoder thread, only while
 * the ring is being flushed); the render thread counts the position down
 * instead of up when it is backwards. */
static _Atomic bool		ringRev;
/* The decoder thread's own view (one playback thread at a time). */
static bool				gRev, gCanRev;
static _Atomic float	volume = 1.0f;
static _Atomic float	levelL, levelR;
static _Atomic bool		paused;
static _Atomic bool		stopReq = true;	/* playback.c: `stop` */
static bool				unitReady;
static _Atomic bool		feeding;	/* render callback may read the ring */

/* Flush handshake: the decoder thread can't move the consumer's tail, so
 * it asks the render thread to do it (and to set the new position). */
static _Atomic bool		flushReq;
static _Atomic uint64_t	flushBase;

/* Pending seek in interleaved samples (accumulates, like playback.c). */
static _Atomic int32_t	seekRequest;

/* ---- Render callback ------------------------------------------------- */

/* Catmull-Rom between p1 and p2 (p0 before, p3 after), t in [0,1). */
static inline float cubic(float p0, float p1, float p2, float p3, float t)
{
	return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 +
	       4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

static OSStatus render(void* ref, AudioUnitRenderActionFlags* flags,
                       const AudioTimeStamp* ts, UInt32 bus, UInt32 frames,
                       AudioBufferList* io)
{
	float* out = io->mBuffers[0].mData;
	float vol = atomic_load(&volume);
	float spd = fabsf(atomic_load(&speed));
	bool rev = atomic_load(&ringRev);
	/* Speed 0.0 holds the song: nothing is consumed. */
	bool run = atomic_load(&feeding) && !atomic_load(&paused) &&
	           spd >= 0.05f;
	float peakL = 0, peakR = 0;

	/* Seek: drop what is buffered and jump the position. */
	if(atomic_exchange(&flushReq, false))
	{
		atomic_store(&ringTail, atomic_load(&ringHead));
		atomic_store(&playedFrames, atomic_load(&flushBase));
		frac = 0;
		memset(prev, 0, sizeof(prev));
	}

	if(!run)
	{
		memset(out, 0, frames * 2 * sizeof(float));
		atomic_store(&levelL, 0);
		atomic_store(&levelR, 0);
		return noErr;
	}

	uint64_t tail = atomic_load(&ringTail);
	uint64_t head = atomic_load(&ringHead);
	const float k = 1.0f / 32768.0f;

	for(UInt32 i = 0; i < frames; i++)
	{
		/* Advance the source by `spd` frames per output frame. */
		frac += spd;
		while(frac >= 1.0)
		{
			if(tail == head)
			{
				head = atomic_load(&ringHead);	/* decoder may have caught up */
				if(tail == head)
					break;
			}
			prev[0][0] = prev[0][1];	/* older */
			prev[1][0] = prev[1][1];
			/* keep 2 frames: [x][0] = previous, [x][1] = current */
			prev[0][1] = ring[(tail & RING_MASK) * 2];
			prev[1][1] = ring[(tail & RING_MASK) * 2 + 1];
			tail++;
			frac -= 1.0;
			if(!rev)
				atomic_fetch_add(&playedFrames, 1);
			else if(atomic_load(&playedFrames) > 0)
				atomic_fetch_sub(&playedFrames, 1);
		}
		if(frac >= 1.0)
		{
			/* Starved: output silence, don't let the phase run away. */
			frac = 1.0 - 1e-9;
			out[i * 2] = out[i * 2 + 1] = 0;
			continue;
		}

		float l, r;
		if(spd == 1.0f)
		{
			l = prev[0][1];
			r = prev[1][1];
		}
		else
		{
			/* Peek one frame ahead for the cubic's p3 without consuming. */
			int16_t nl = prev[0][1], nr = prev[1][1];
			if(tail != head)
			{
				nl = ring[(tail & RING_MASK) * 2];
				nr = ring[(tail & RING_MASK) * 2 + 1];
			}
			float t = (float)frac;
			l = cubic(prev[0][0], prev[0][1], nl, nl, t);
			r = cubic(prev[1][0], prev[1][1], nr, nr, t);
		}
		l *= k * vol;
		r *= k * vol;
		out[i * 2] = l;
		out[i * 2 + 1] = r;
		if(fabsf(l) > peakL) peakL = fabsf(l);
		if(fabsf(r) > peakR) peakR = fabsf(r);
	}

	atomic_store(&ringTail, tail);
	atomic_store(&levelL, peakL);
	atomic_store(&levelR, peakR);
	return noErr;
}

/* (Re)configure the output unit for a song's sample rate. */
static bool configureUnit(double rate)
{
	AudioStreamBasicDescription fmt = { 0 };

	if(unitRunning)
		AudioOutputUnitStop(outUnit);
	AudioUnitUninitialize(outUnit);
	unitRunning = false;

	fmt.mSampleRate = rate;
	fmt.mFormatID = kAudioFormatLinearPCM;
	fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	fmt.mChannelsPerFrame = 2;
	fmt.mBitsPerChannel = 32;
	fmt.mBytesPerFrame = 8;
	fmt.mFramesPerPacket = 1;
	fmt.mBytesPerPacket = 8;

	if(AudioUnitSetProperty(outUnit, kAudioUnitProperty_StreamFormat,
	                        kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)))
		return false;
	if(AudioUnitInitialize(outUnit))
		return false;
	if(AudioOutputUnitStart(outUnit))
		return false;
	unitRunning = true;
	return true;
}

/* Output unit: created on first use, reconfigured per song (sample rate). */
static bool audioOpen(void)
{
	AudioComponentDescription d = { 0 };
	AURenderCallbackStruct cb = { render, NULL };

	if(unitReady)
		return true;
	d.componentType = kAudioUnitType_Output;
	d.componentSubType = kAudioUnitSubType_DefaultOutput;
	d.componentManufacturer = kAudioUnitManufacturer_Apple;

	AudioComponent c = AudioComponentFindNext(NULL, &d);
	if(c == NULL || AudioComponentInstanceNew(c, &outUnit))
		return false;
	if(AudioUnitSetProperty(outUnit, kAudioUnitProperty_SetRenderCallback,
	                        kAudioUnitScope_Input, 0, &cb, sizeof(cb)))
		return false;
	unitReady = true;
	return true;
}

static bool pickDecoder(const char* file, struct decoder_fn* dec)
{
	switch(getFileType(file))
	{
		case FILE_TYPE_WAV:		setWav(dec); return true;
		case FILE_TYPE_FLAC:	setFlac(dec); return true;
		case FILE_TYPE_OPUS:	setOpus(dec); return true;
		case FILE_TYPE_MP3:		setMp3(dec); return true;
		case FILE_TYPE_VORBIS:	setVorbis(dec); return true;
		case FILE_TYPE_SID:		setSid(dec); return true;
		case FILE_TYPE_M4A:		setM4a(dec); return true;
		default:				return false;
	}
}

/* Does the speed's sign ask for the other direction than the one playing? */
static bool wantReverse(void)
{
	float s = atomic_load(&speed);

	if(!gCanRev)
		return false;
	return s < 0.0f || (s == 0.0f && gRev);
}

/* Blocks until `frames` fit in the ring, or the song is stopped. Gives up
 * (still returning true, nothing written) when a seek or a change of
 * direction is waiting, so those aren't held up by a full ring. */
static bool ringWrite(const int16_t* src, unsigned frames, unsigned channels)
{
	while(!atomic_load(&stopReq))
	{
		if(atomic_load(&seekRequest) != 0 || wantReverse() != gRev)
			return true;
		uint64_t head = atomic_load(&ringHead);
		uint64_t tail = atomic_load(&ringTail);

		if(RING_FRAMES - (head - tail) >= frames)
		{
			for(unsigned i = 0; i < frames; i++)
			{
				int16_t l = src[i * channels];
				int16_t r = channels == 2 ? src[i * channels + 1] : l;
				ring[((head + i) & RING_MASK) * 2] = l;
				ring[((head + i) & RING_MASK) * 2 + 1] = r;
			}
			atomic_store(&ringHead, head + frames);
			return true;
		}
		usleep(4000);
	}
	return false;
}

/* Drop everything buffered and move the reported position to `frame`. The
 * render thread does the actual flush (it owns the tail); wait for it. */
static void flushTo(uint64_t frame)
{
	atomic_store(&ringRev, gRev);
	atomic_store(&flushBase, frame);
	atomic_store(&flushReq, true);
	for(int i = 0; i < 200 && atomic_load(&flushReq) &&
	    !atomic_load(&stopReq); i++)
		usleep(1000);
	if(atomic_exchange(&flushReq, false))
	{
		atomic_store(&ringTail, atomic_load(&ringHead));
		atomic_store(&playedFrames, frame);
	}
}

/* ---- playback.h ------------------------------------------------------ */

bool togglePlayback(void)
{
	bool p = !atomic_load(&paused);

	atomic_store(&paused, p);
	return p;
}

void stopPlayback(void)		{ atomic_store(&stopReq, true); }
bool isPlaying(void)		{ return !atomic_load(&stopReq); }
bool isPaused(void)			{ return atomic_load(&paused); }

void requestSeek(int64_t deltaSamples)
{
	const int64_t limit = 1 << 30;

	if(deltaSamples > limit) deltaSamples = limit;
	if(deltaSamples < -limit) deltaSamples = -limit;
	atomic_fetch_add(&seekRequest, (int32_t)deltaSamples);
}

void setPlaybackSpeed(float s)
{
	if(atomic_load(&reverseAllowed))
	{
		if(s < -3.0f) s = -3.0f;
		if(fabsf(s) < 0.05f) s = 0.0f;	/* 0.0 = held */
	}
	else if(s < 0.1f)
		s = 0.1f;
	if(s > 3.0f) s = 3.0f;
	atomic_store(&speed, s);
}

void setReverseAllowed(bool allowed)
{
	atomic_store(&reverseAllowed, allowed);
	if(!allowed && atomic_load(&speed) < 0.1f)
		setPlaybackSpeed(1.0f);
}

bool getReverseAllowed(void)	{ return atomic_load(&reverseAllowed); }

bool playbackTakeReachedStart(void)
{
	return atomic_exchange(&reachedStart, false);
}

/* The next block of audio: forwards, or backwards (reverse.h). */
static size_t fillBlock(struct decoder_fn* d, int16_t* buf, bool rev,
                        struct reverseState* rs)
{
	return rev ? reverseFill(d, buf, rs) : d->decode(buf);
}

float getPlaybackSpeed(void)	{ return atomic_load(&speed); }

void macSetVolume(float v)
{
	atomic_store(&volume, v < 0 ? 0 : v > 1 ? 1 : v);
}

float macVolume(void)			{ return atomic_load(&volume); }

void macLevels(float* l, float* r)
{
	*l = atomic_load(&levelL);
	*r = atomic_load(&levelR);
}

/* Runs on the thread player.c made for this song. */
void playFile(void* infoIn)
{
	struct playbackInfo_t*	info = infoIn;
	struct decoder_fn		dec = { 0 };
	int16_t*				buf = NULL;
	unsigned				ch = 0;
	uint64_t				total = 0;
	bool					opened = false, eof = false;
	const char*				file = info->file;

	/* A new song: no stop, no pending seek, not paused (togglePlayback's
	 * pause belongs to the song before). */
	atomic_store(&stopReq, false);
	atomic_store(&seekRequest, 0);
	atomic_store(&reachedStart, false);

	if(!pickDecoder(file, &dec))
		goto err;
	if(!audioOpen())
	{
		errno = NDSP_INIT_FAIL;
		goto err;
	}
	if(dec.init(file) != 0)
	{
		errno = DECODER_INIT_FAIL;
		goto err;
	}
	opened = true;

	ch = dec.channels();
	if(ch < 1 || ch > 2)
	{
		errno = UNSUPPORTED_CHANNELS;
		goto err;
	}
	uint32_t rate = dec.rate();

	if(dec.getFileSamples != NULL)
		info->samples_total = dec.getFileSamples();
	total = info->samples_total / ch;
	info->samples_per_second = rate * ch;

	buf = malloc(dec.buffSize * sizeof(int16_t));
	if(buf == NULL || !configureUnit(rate))
	{
		errno = buf ? NDSP_INIT_FAIL : ENOMEM;
		goto err;
	}

	effectsStart(rate, ch);
	atomic_store(&ringTail, atomic_load(&ringHead));
	atomic_store(&playedFrames, 0);
	atomic_store(&flushReq, false);

	/* Backwards needs seeking and a known length; it starts at the end. */
	struct reverseState rs = { 0, 0 };

	gCanRev = dec.seek != NULL && total > 0;
	gRev = gCanRev && atomic_load(&speed) < 0.0f;
	atomic_store(&ringRev, gRev);
	if(gRev)
	{
		size_t exact = reversePrepare(&dec);

		if(exact > 0)
		{
			total = exact / ch;
			info->samples_total = exact;
		}
		rs.pos = (int64_t)(total * ch);
		atomic_store(&playedFrames, total);
	}
	frac = 0;
	memset(prev, 0, sizeof(prev));
	atomic_store(&paused, false);
	atomic_store(&feeding, true);

	while(!atomic_load(&stopReq))
	{
		info->samples_played = (size_t)(atomic_load(&playedFrames) * ch);

		/* The speed changed sign: carry on from here the other way (a seek
		 * by 0). */
		bool dirChange = wantReverse() != gRev;

		/* Seek requests (interleaved samples, relative). */
		if(atomic_load(&seekRequest) != 0 || dirChange)
		{
			int64_t delta = atomic_exchange(&seekRequest, 0);

			if(dec.seek != NULL)
			{
				int64_t target = (int64_t)atomic_load(&playedFrames) +
				                 delta / (int64_t)ch;

				if(wantReverse() && !gRev)
				{
					size_t exact = reversePrepare(&dec);

					if(exact > 0)
					{
						total = exact / ch;
						info->samples_total = exact;
					}
				}
				gRev = wantReverse();
				if(target < 0)
					target = 0;
				/* Seeking past the end finishes the song. */
				if(delta > 0 && total > 0 && target >= (int64_t)total)
					break;
				if(gRev)
					rs.pos = target * (int64_t)ch;
				if(gRev || dec.seek((size_t)target * ch) == 0)
				{
					flushTo((uint64_t)target);
					effectsStart(rate, ch);
					eof = false;
				}
			}
			continue;
		}

		if(eof)
		{
			/* All decoded: wait for the ring to drain. */
			if(atomic_load(&ringHead) == atomic_load(&ringTail))
			{
				/* Backwards, the song ran out at its start. */
				atomic_store(&reachedStart, gRev);
				break;
			}
			usleep(4000);
			continue;
		}

		uint64_t got = fillBlock(&dec, buf, gRev, &rs);

		if(got == 0)
		{
			eof = true;
			continue;
		}
		effectsProcess(buf, (unsigned)(got / ch));
		if(!ringWrite(buf, (unsigned)(got / ch), ch))
			break;
	}

	info->samples_played = (size_t)(atomic_load(&playedFrames) * ch);
	atomic_store(&feeding, false);
	dec.exit();
	free(buf);
	atomic_store(&stopReq, true);

	/* Tell player.c's watchdog the song is over (-1 = finished). */
	macPlaybackDone(info, -1);
	return;

err:
	atomic_store(&feeding, false);
	if(opened)
		dec.exit();
	free(buf);
	atomic_store(&stopReq, true);
	macPlaybackDone(info, errno);
}

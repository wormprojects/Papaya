#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#ifndef ctrmus_playback_h
#define ctrmus_playback_h

/* Channel to play music on */
#define CHANNEL	0x08

struct decoder_fn
{
	/**
	 * Set decoder parameters.
	 * \param	decoder Structure to store parameters.
	 * \return	0 on success, else failure.
	 */
	int (* init)(const char* file);

	/**
	 * Get sampling rate of file.
	 * \return	Sampling rate.
	 */
	uint32_t (* rate)(void);

	/**
	 * Get number of channels of file.
	 * \return	Number of channels for opened file.
	 */
	uint8_t (* channels)(void);

	/**
	 * Size of output buffer used in decode().
	 */
	size_t buffSize;

	/**
	 * Fill buffer with decoded samples.
	 * \param buffer	Output buffer to fill.
	 * \return		Samples read for each channel.
	 */
	uint64_t (* decode)(void*);

	/**
	 * Free codec resources.
	 */
	void (* exit)(void);

	/**
	 * Optional. Set to NULL if unavailable.
	 * Get number of samples in audio file.
	 */
	size_t (* getFileSamples)(void);

	/**
	 * Optional. Set to NULL if unavailable.
	 * Seek to a specific sample position in the audio file.
	 * \param sample	Target sample position (total samples, not per-channel).
	 * \return		0 on success, -1 on failure.
	 */
	int (* seek)(size_t sample);

	/**
	 * Optional. Called once before playing backwards: make seeks exact
	 * (mp3: scan the file; may take a moment) and the length exact.
	 * Forward-only players never call it.
	 */
	void (* prepareExactSeek)(void);
};

struct playbackInfo_t
{
	char file[PATH_MAX];
	struct errInfo_t *errInfo;

	/* If 0, then the duration of file is unavailable. */
	size_t samples_total;
	size_t samples_played;
	size_t samples_per_second;
};

/**
 * Pause or play current file.
 *
 * \return	True if paused.
 */
bool togglePlayback(void);

/**
 * Stops current playback. Playback thread should exit as a result.
 */
void stopPlayback(void);

/**
 * Returns whether music is playing or paused.
 */
bool isPlaying(void);

/**
 * Returns whether playback is paused.
 */
bool isPaused(void);

/**
 * Should only be called from a new thread only, and have only one playback
 * thread at time. This function has not been written for more than one
 * playback thread in mind.
 *
 * \param	infoIn	Playback information.
 */
void playFile(void* infoIn);

/**
 * Request a seek by the given number of signed samples.
 * Positive = forward, negative = backward.
 * Thread-safe: sets a flag that the playback loop checks.
 *
 * \param	deltaSamples	Number of samples to seek (signed).
 */
void requestSeek(int64_t deltaSamples);

/**
 * Set playback speed multiplier. 1.0 = normal.
 * Takes effect immediately if audio is playing.
 *
 * \param	speed	Speed multiplier (e.g. 0.5 = half, 2.0 = double).
 */
void setPlaybackSpeed(float speed);

/**
 * Get current playback speed multiplier.
 */
float getPlaybackSpeed(void);

/**
 * Reverse playback (1.5.0). Off: the speed range is 0.1 .. 3.0. On: -3.0 ..
 * 3.0, where negative plays the song backwards and 0.0 pauses (internally;
 * the song stays "playing"). Turning it off while the speed is 0 or
 * negative puts the speed back to 1.0.
 */
void setReverseAllowed(bool allowed);
bool getReverseAllowed(void);

/**
 * True once (then cleared) if the last song ended because reverse playback
 * ran back to its start, so player.c goes to the previous song instead of
 * the next one.
 */
bool playbackTakeReachedStart(void);

#endif

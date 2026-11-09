#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DR_WAV_IMPLEMENTATION
#include <dr_libs/dr_wav.h>

#include "bufio.h"
#include "wav.h"
#include "playback.h"

static drwav wav;
static const size_t buffSize = 16 * 1024;

/* With dr_wav's stdio backend every decode was dozens of 1 KiB SD reads
 * (newlib's default buffer); bufio does one read per decode. */
static struct bufio wavBf = { .fd = -1 };

static size_t readWavCb(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
	return bufioRead(pUserData, pBufferOut, bytesToRead);
}

static drwav_bool32 seekWavCb(void* pUserData, int offset,
                              drwav_seek_origin origin)
{
	struct bufio* b = pUserData;
	int64_t target;

	switch(origin)
	{
		case DRWAV_SEEK_SET: target = offset; break;
		case DRWAV_SEEK_CUR: target = bufioTell(b) + offset; break;
		case DRWAV_SEEK_END: target = bufioSize(b) + offset; break;
		default: return DRWAV_FALSE;
	}

	if(target < 0 || target > bufioSize(b))
		return DRWAV_FALSE;

	return bufioSeek(b, target, SEEK_SET) == 0;
}

static drwav_bool32 tellWavCb(void* pUserData, drwav_int64* pCursor)
{
	*pCursor = bufioTell(pUserData);
	return DRWAV_TRUE;
}

static int initWav(const char* file);
static uint32_t rateWav(void);
static uint8_t channelWav(void);
static uint64_t readWav(void* buffer);
static void exitWav(void);
static size_t getFileSamplesWav(void);
static int seekWav(size_t sample);

/**
 * Set decoder parameters for WAV.
 *
 * \param	decoder Structure to store parameters.
 */
void setWav(struct decoder_fn* decoder)
{
	decoder->init = &initWav;
	decoder->rate = &rateWav;
	decoder->channels = &channelWav;
	decoder->buffSize = buffSize;
	decoder->decode = &readWav;
	decoder->exit = &exitWav;
	decoder->getFileSamples = &getFileSamplesWav;
	decoder->seek = &seekWav;
}

/**
 * Initialise WAV playback.
 *
 * \param	file	Location of WAV file to play.
 * \return			0 on success, else failure.
 */
int initWav(const char* file)
{
	if(bufioOpen(&wavBf, file, 64 * 1024) != 0)
		return -1;

	if(!drwav_init(&wav, readWavCb, seekWavCb, tellWavCb, &wavBf, NULL))
	{
		bufioClose(&wavBf);
		return -1;
	}

	return 0;
}

static size_t getFileSamplesWav(void)
{
	return wav.totalPCMFrameCount * (size_t)wav.channels;
}

/**
 * Get sampling rate of Wav file.
 *
 * \return	Sampling rate.
 */
uint32_t rateWav(void)
{
	return wav.sampleRate;
}

/**
 * Get number of channels of Wav file.
 *
 * \return	Number of channels for opened file.
 */
uint8_t channelWav(void)
{
	return wav.channels;
}

/**
 * Read part of open Wav file.
 *
 * \param buffer	Output.
 * \return			Samples read for each channel.
 */
uint64_t readWav(void* buffer)
{
	size_t buffSizeFrames;
	uint64_t samplesRead;

	buffSizeFrames = buffSize / (size_t)wav.channels;
	samplesRead = drwav_read_pcm_frames_s16(&wav, buffSizeFrames, buffer);
	samplesRead *= (uint64_t)wav.channels;
	return samplesRead;
}

/**
 * Seek to a sample position in WAV file.
 *
 * \param sample	Target sample position (total samples across all channels).
 * \return		0 on success, -1 on failure.
 */
static int seekWav(size_t sample)
{
	drwav_uint64 targetFrame = sample / (drwav_uint64)wav.channels;
	return drwav_seek_to_pcm_frame(&wav, targetFrame) ? 0 : -1;
}

/**
 * Free Wav file.
 */
void exitWav(void)
{
	drwav_uninit(&wav);
	bufioClose(&wavBf);
}

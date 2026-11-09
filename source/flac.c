#define DR_FLAC_IMPLEMENTATION
#include <dr_libs/dr_flac.h>

#include <stdio.h>

#include "bufio.h"
#include "flac.h"
#include "playback.h"

static drflac*		pFlac;
static const size_t	buffSize = 16 * 1024;

/*
 * dr_flac's seek does a binary search over the file, decoding a frame at
 * each probe. With its stdio backend every probe cost several small SD
 * reads; bufio keeps probes to one small read and decoding to few large
 * ones.
 */
static struct bufio	flacBf = { .fd = -1 };

static size_t readFlacCb(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
	return bufioRead(pUserData, pBufferOut, bytesToRead);
}

static drflac_bool32 seekFlacCb(void* pUserData, int offset,
                                drflac_seek_origin origin)
{
	struct bufio* b = pUserData;
	int64_t target;

	switch(origin)
	{
		case DRFLAC_SEEK_SET: target = offset; break;
		case DRFLAC_SEEK_CUR: target = bufioTell(b) + offset; break;
		case DRFLAC_SEEK_END: target = bufioSize(b) + offset; break;
		default: return DRFLAC_FALSE;
	}

	/* dr_flac probes past the end while seeking and relies on failure. */
	if(target < 0 || target > bufioSize(b))
		return DRFLAC_FALSE;

	return bufioSeek(b, target, SEEK_SET) == 0;
}

static drflac_bool32 tellFlacCb(void* pUserData, drflac_int64* pCursor)
{
	*pCursor = bufioTell(pUserData);
	return DRFLAC_TRUE;
}

static int initFlac(const char* file);
static uint32_t rateFlac(void);
static uint8_t channelFlac(void);
static uint64_t decodeFlac(void* buffer);
static void exitFlac(void);
static size_t getFileSamplesFlac(void);
static int seekFlac(size_t sample);

/**
 * Set decoder parameters for flac.
 *
 * \param	decoder Structure to store parameters.
 */
void setFlac(struct decoder_fn* decoder)
{
	decoder->init = &initFlac;
	decoder->rate = &rateFlac;
	decoder->channels = &channelFlac;
	decoder->buffSize = buffSize;
	decoder->decode = &decodeFlac;
	decoder->exit = &exitFlac;
	decoder->getFileSamples = &getFileSamplesFlac;
	decoder->seek = &seekFlac;
}

/**
 * Initialise Flac decoder.
 *
 * \param	file	Location of flac file to play.
 * \return			0 on success, else failure.
 */
static int initFlac(const char* file)
{
	if(bufioOpen(&flacBf, file, 64 * 1024) != 0)
		return -1;

	pFlac = drflac_open(readFlacCb, seekFlacCb, tellFlacCb, &flacBf, NULL);
	if(pFlac == NULL)
	{
		bufioClose(&flacBf);
		return -1;
	}

	return 0;
}

static size_t getFileSamplesFlac(void)
{
	return pFlac->totalPCMFrameCount * (size_t)pFlac->channels;
}

/**
 * Get sampling rate of Flac file.
 *
 * \return	Sampling rate.
 */
static uint32_t rateFlac(void)
{
	return pFlac->sampleRate;
}

/**
 * Get number of channels of Flac file.
 *
 * \return	Number of channels for opened file.
 */
static uint8_t channelFlac(void)
{
	return pFlac->channels;
}

/**
 * Decode part of open Flac file.
 *
 * \param buffer	Decoded output.
 * \return			Samples read for each channel.
 */
static uint64_t decodeFlac(void* buffer)
{
	size_t buffSizeFrames;
	uint64_t samplesRead;

	buffSizeFrames = buffSize / (size_t)pFlac->channels;
	samplesRead = drflac_read_pcm_frames_s16(pFlac, buffSizeFrames, buffer);
	samplesRead *= (uint64_t)pFlac->channels;
	return samplesRead;
}

/**
 * Seek to a sample position in Flac file.
 *
 * \param sample	Target sample position (total samples across all channels).
 * \return		0 on success, -1 on failure.
 */
static int seekFlac(size_t sample)
{
	drflac_uint64 targetFrame = sample / (drflac_uint64)pFlac->channels;
	return drflac_seek_to_pcm_frame(pFlac, targetFrame) ? 0 : -1;
}

/**
 * Free Flac decoder.
 */
static void exitFlac(void)
{
	drflac_close(pFlac);
	pFlac = NULL;
	bufioClose(&flacBf);
}

/**
 * Checks if the input file is Flac
 *
 * \param in	Input file.
 * \return		0 if Flac file, else not or failure.
 */
int isFlac(const char* in)
{
	int err = -1;
	drflac* pFlac = drflac_open_file(in, NULL);

	if(pFlac != NULL)
		err = 0;

	drflac_close(pFlac);
	return err;
}

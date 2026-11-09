#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "bufio.h"
#include "opus.h"
#include "playback.h"

static OggOpusFile*		opusFile;
static const OpusHead*	opusHead;
static const size_t		buffSize = 32 * 1024;

/*
 * op_pcm_seek() bisects the file; through stdio every probe was a slow SD
 * round trip. bufio keeps probes small and serves nearby seeks from memory.
 */
static struct bufio		opusBf = { .fd = -1 };

static int readOpusCb(void* stream, unsigned char* ptr, int nbytes)
{
	if(nbytes <= 0)
		return 0;
	return (int)bufioRead(stream, ptr, (size_t)nbytes);
}

static int seekOpusCb(void* stream, opus_int64 offset, int whence)
{
	return bufioSeek(stream, (int64_t)offset, whence);
}

static opus_int64 tellOpusCb(void* stream)
{
	return (opus_int64)bufioTell(stream);
}

static int initOpus(const char* file);
static uint32_t rateOpus(void);
static uint8_t channelOpus(void);
static uint64_t decodeOpus(void* buffer);
static void exitOpus(void);
static uint64_t fillOpusBuffer(int16_t* bufferOut);
static size_t getFileSamplesOpus(void);
static int seekOpus(size_t sample);

/**
 * Set decoder parameters for Opus.
 *
 * \param	decoder	Structure to store parameters.
 */
void setOpus(struct decoder_fn* decoder)
{
	decoder->init = &initOpus;
	decoder->rate = &rateOpus;
	decoder->channels = &channelOpus;
	decoder->buffSize = buffSize;
	decoder->decode = &decodeOpus;
	decoder->exit = &exitOpus;
	decoder->getFileSamples = &getFileSamplesOpus;
	decoder->seek = &seekOpus;
}

static size_t getFileSamplesOpus(void)
{
	ogg_int64_t len = op_pcm_total(opusFile, -1);

	if(len == OP_EINVAL)
		return 0;

	return len * (size_t)channelOpus();
}

/**
 * Initialise Opus decoder.
 *
 * \param	file	Location of opus file to play.
 * \return			0 on success, else failure.
 */
int initOpus(const char* file)
{
	/* close is NULL: exitOpus() closes the reader itself. */
	const OpusFileCallbacks cb = { readOpusCb, seekOpusCb, tellOpusCb, NULL };
	int err = 0;

	if(bufioOpen(&opusBf, file, 64 * 1024) != 0)
		return -1;

	if((opusFile = op_open_callbacks(&opusBf, &cb, NULL, 0, &err)) == NULL)
	{
		bufioClose(&opusBf);
		return err != 0 ? err : -1;
	}

	if((err = op_current_link(opusFile)) < 0)
	{
		exitOpus();
		return err;
	}

	opusHead = op_head(opusFile, err);
	return 0;
}

/**
 * Get sampling rate of Opus file.
 *
 * \return	Sampling rate. Should be 48000.
 */
uint32_t rateOpus(void)
{
	return 48000;
}

/**
 * Get number of channels of Opus file.
 *
 * \return	Number of channels for opened file, so always be 2.
 */
uint8_t channelOpus(void)
{
	/* Opus decoder always returns stereo stream */
	return 2;
}

/**
 * Decode part of open Opus file.
 *
 * \param buffer	Decoded output.
 * \return			Samples read for each channel. 0 for end of file, negative
 *					for error.
 */
uint64_t decodeOpus(void* buffer)
{
	return fillOpusBuffer(buffer);
}

/**
 * Seek to a sample position in Opus file.
 *
 * \param sample	Target sample position (total samples across all channels).
 * \return		0 on success, -1 on failure.
 */
static int seekOpus(size_t sample)
{
	/* Opus samples are per-channel (PCM frames) */
	ogg_int64_t targetSample = (ogg_int64_t)(sample / channelOpus());
	return op_pcm_seek(opusFile, targetSample) < 0 ? -1 : 0;
}

/**
 * Free Opus decoder.
 */
void exitOpus(void)
{
	op_free(opusFile);
	opusFile = NULL;
	bufioClose(&opusBf);
}

/**
 * Decode Opus file to fill buffer.
 *
 * \param bufferOut		Pointer to buffer.
 * \return				Samples read per channel.
 */
uint64_t fillOpusBuffer(int16_t* bufferOut)
{
	uint64_t samplesRead = 0;
	int samplesToRead = buffSize;
	int holes = 0;

	while(samplesToRead > 0)
	{
		int samplesJustRead = op_read_stereo(opusFile, bufferOut,
				samplesToRead > 120*48*2 ? 120*48*2 : samplesToRead);

		/* A hole (missing/corrupt data) is skipped; any other error ends
		 * the song. Never return the negative code: decode() is unsigned
		 * and the playback loop would take it as a huge sample count. */
		if(samplesJustRead == OP_HOLE && ++holes < 16)
			continue;
		if(samplesJustRead < 0)
			break;
		else if(samplesJustRead == 0)
		{
			/* End of file reached. */
			break;
		}

		samplesRead += samplesJustRead * 2;
		samplesToRead -= samplesJustRead * 2;
		bufferOut += samplesJustRead * 2;
	}

	return samplesRead;
}

/**
 * Checks if the input file is Opus.
 *
 * \param in	Input file.
 * \return		0 if Opus file, else not or failure.
 */
int isOpus(const char* in)
{
	int err = 0;
	OggOpusFile* opusTest = op_test_file(in, &err);

	op_free(opusTest);
	return err;
}

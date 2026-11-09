#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bufio.h"
#include "vorbis.h"
#include "playback.h"

static OggVorbis_File	vorbisFile;
static vorbis_info		*vi;
static const size_t		buffSize = 8 * 4096;

/*
 * ov_pcm_seek() bisects the file, so it does many scattered seeks + small
 * reads. Through stdio each of those was a slow SD round trip (and with a
 * big setvbuf() buffer, every fseek() also forced a full buffer refill).
 * bufio keeps probes small and serves nearby seeks from memory.
 */
static struct bufio		vorbisBf = { .fd = -1 };

static size_t readVorbisCb(void* ptr, size_t size, size_t nmemb, void* ds)
{
	if(size == 0)
		return 0;
	return bufioRead(ds, ptr, size * nmemb) / size;
}

static int seekVorbisCb(void* ds, ogg_int64_t offset, int whence)
{
	return bufioSeek(ds, (int64_t)offset, whence);
}

static long tellVorbisCb(void* ds)
{
	return (long)bufioTell(ds);
}

static int initVorbis(const char* file);
static uint32_t rateVorbis(void);
static uint8_t channelVorbis(void);
static uint64_t decodeVorbis(void* buffer);
static void exitVorbis(void);
static uint64_t fillVorbisBuffer(char* bufferOut);
static int seekVorbis(size_t sample);
static size_t getFileSamplesVorbis(void);

/**
 * Set decoder parameters for Vorbis.
 *
 * \param	decoder	Structure to store parameters.
 */
void setVorbis(struct decoder_fn* decoder)
{
	decoder->init = &initVorbis;
	decoder->rate = &rateVorbis;
	decoder->channels = &channelVorbis;
	decoder->buffSize = buffSize;
	decoder->decode = &decodeVorbis;
	decoder->exit = &exitVorbis;
	decoder->getFileSamples = &getFileSamplesVorbis;
	decoder->seek = &seekVorbis;
}

/**
 * Total interleaved samples, so the UI can show the duration and seeking
 * can clamp to the end of the file.
 */
static size_t getFileSamplesVorbis(void)
{
	ogg_int64_t len = ov_pcm_total(&vorbisFile, -1);

	if(len < 0)
		return 0;

	return (size_t)len * vi->channels;
}

/**
 * Initialise Vorbis decoder.
 *
 * \param	file	Location of vorbis file to play.
 * \return			0 on success, else failure.
 */
int initVorbis(const char* file)
{
	/* close_func is NULL: exitVorbis() closes the reader itself. */
	const ov_callbacks cb = { readVorbisCb, seekVorbisCb, NULL, tellVorbisCb };

	if(bufioOpen(&vorbisBf, file, 64 * 1024) != 0)
		return -1;

	if(ov_open_callbacks(&vorbisBf, &vorbisFile, NULL, 0, cb) < 0)
	{
		bufioClose(&vorbisBf);
		return -1;
	}

	if((vi = ov_info(&vorbisFile, -1)) == NULL)
	{
		exitVorbis();
		return -1;
	}

	return 0;
}

/**
 * Get sampling rate of Vorbis file.
 *
 * \return	Sampling rate.
 */
uint32_t rateVorbis(void)
{
	return vi->rate;
}

/**
 * Get number of channels of Vorbis file.
 *
 * \return	Number of channels for opened file.
 */
uint8_t channelVorbis(void)
{
	return vi->channels;
}

/**
 * Decode part of open Vorbis file.
 *
 * \param buffer	Decoded output.
 * \return			Samples read for each channel. 0 for end of file, negative
 *					for error.
 */
uint64_t decodeVorbis(void* buffer)
{
	return fillVorbisBuffer(buffer);
}

/**
 * Seek to a sample position in Vorbis file.
 *
 * \param sample	Target sample position (total samples across all channels).
 * \return		0 on success, -1 on failure.
 */
static int seekVorbis(size_t sample)
{
	ogg_int64_t targetSample = (ogg_int64_t)(sample / vi->channels);
	return ov_pcm_seek(&vorbisFile, targetSample) < 0 ? -1 : 0;
}

/**
 * Free Vorbis decoder.
 */
void exitVorbis(void)
{
	ov_clear(&vorbisFile);
	bufioClose(&vorbisBf);
}

/**
 * Decode Vorbis file to fill buffer.
 *
 * \param opusFile		File to decode.
 * \param bufferOut		Pointer to buffer.
 * \return				Samples read per channel.
 */
uint64_t fillVorbisBuffer(char* bufferOut)
{
	uint64_t samplesRead = 0;
	int samplesToRead = buffSize;
	int holes = 0;

	while(samplesToRead > 0)
	{
		static int current_section;
		int samplesJustRead =
			ov_read(&vorbisFile, bufferOut,
					samplesToRead > 4096 ? 4096	: samplesToRead,
					&current_section);

		/* As in opus.c: skip holes, stop on other errors, and never
		 * return the negative code as an (unsigned) sample count. */
		if(samplesJustRead == OV_HOLE && ++holes < 16)
			continue;
		if(samplesJustRead < 0)
			break;
		else if(samplesJustRead == 0)
		{
			/* End of file reached. */
			break;
		}

		samplesRead += samplesJustRead;
		samplesToRead -= samplesJustRead;
		bufferOut += samplesJustRead;
	}

	return samplesRead / sizeof(int16_t);
}

/**
 * Checks if the input file is Vorbis.
 *
 * \param in	Input file.
 * \return		0 if Vorbis file, else not or failure.
 */
int isVorbis(const char *in)
{
	FILE *ft = fopen(in, "r");
	OggVorbis_File testvf;
	int err;

	if(ft == NULL)
		return -1;

	err = ov_test(ft, &testvf, NULL, 0);

	/* On success the OggVorbis_File owns ft and ov_clear() closes it;
	 * closing it again could close a FILE another thread just opened. */
	if(err == 0)
		ov_clear(&testvf);
	else
		fclose(ft);

	return err;
}

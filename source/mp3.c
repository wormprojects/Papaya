#include <mpg123.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bufio.h"
#include "mp3.h"
#include "playback.h"

static size_t*			buffSize;
static mpg123_handle	*mh = NULL;
static long				rate;
static int				channels;

/*
 * mpg123's own file reader issues a separate unbuffered read() for every
 * frame header and every frame body. On the 3DS each of those is an SD
 * round trip, and an accurate seek forward has to walk every frame up to
 * the target, so seeking 15s meant well over a thousand tiny reads. Feeding
 * mpg123 through bufio turns that into a handful of large reads.
 */
static struct bufio		mp3File = { .fd = -1 };

static mpg123_ssize_t readMp3Cb(void* handle, void* buf, size_t count)
{
	return (mpg123_ssize_t)bufioRead(handle, buf, count);
}

static off_t seekMp3Cb(void* handle, off_t offset, int whence)
{
	if(bufioSeek(handle, (int64_t)offset, whence) != 0)
		return -1;

	return (off_t)bufioTell(handle);
}

static int initMp3(const char* file);
static uint32_t rateMp3(void);
static uint8_t channelMp3(void);
static uint64_t decodeMp3(void* buffer);
static void exitMp3(void);
static size_t getFileSamplesMp3(void);
static int seekMp3(size_t sample);

/**
 * Set decoder parameters for MP3.
 *
 * \param	decoder Structure to store parameters.
 */
void setMp3(struct decoder_fn* decoder)
{
	decoder->init = &initMp3;
	decoder->rate = &rateMp3;
	decoder->channels = &channelMp3;
	/*
	 * buffSize changes depending on input file. So we set buffSize later when
	 * decoder is initialised.
	 */
	buffSize = &(decoder->buffSize);
	decoder->decode = &decodeMp3;
	decoder->exit = &exitMp3;
	decoder->getFileSamples = &getFileSamplesMp3;
	decoder->seek = &seekMp3;
}

static size_t getFileSamplesMp3(void)
{
	off_t len = mpg123_length(mh);
	if(len == MPG123_ERR)
		return 0;
	
	return (size_t)(len * (off_t)channels);
}

/**
 * Initialise MP3 decoder.
 *
 * \param	file	Location of MP3 file to play.
 * \return			0 on success, else failure.
 */
int initMp3(const char* file)
{
	int err = 0;
	int encoding = 0;

	if((err = mpg123_init()) != MPG123_OK)
		return err;

	if((mh = mpg123_new(NULL, &err)) == NULL)
	{
		printf("Error: %s\n", mpg123_plain_strerror(err));
		mpg123_exit();
		return err;
	}

	if(bufioOpen(&mp3File, file, 64 * 1024) != 0)
	{
		mpg123_delete(mh);
		mh = NULL;
		mpg123_exit();
		return -1;
	}

	if(mpg123_replace_reader_handle(mh, readMp3Cb, seekMp3Cb, NULL)
			!= MPG123_OK ||
			mpg123_open_handle(mh, &mp3File) != MPG123_OK ||
			mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK)
	{
		printf("Trouble with mpg123: %s\n", mpg123_strerror(mh));
		exitMp3();
		return -1;
	}

	/*
	 * Ensure that this output format will not change (it might, when we allow
	 * it).
	 */
	mpg123_format_none(mh);
	mpg123_format(mh, rate, channels, encoding);

	/*
	 * Buffer could be almost any size here, mpg123_outblock() is just some
	 * recommendation. The size should be a multiple of the PCM frame size.
	 */
	*buffSize = mpg123_outblock(mh) * 16;

	return 0;
}

/**
 * Get sampling rate of MP3 file.
 *
 * \return	Sampling rate.
 */
uint32_t rateMp3(void)
{
	return (uint32_t)rate;
}

/**
 * Get number of channels of MP3 file.
 *
 * \return	Number of channels for opened file.
 */
uint8_t channelMp3(void)
{
	return (uint8_t)channels;
}

/**
 * Decode part of open MP3 file.
 *
 * \param buffer	Decoded output.
 * \return			Samples read for each channel.
 */
uint64_t decodeMp3(void* buffer)
{
	size_t done = 0;
	mpg123_read(mh, buffer, *buffSize, &done);
	return done / (sizeof(int16_t));
}

/**
 * Seek to a sample position in MP3 file.
 *
 * \param sample	Target sample position (total samples across all channels).
 * \return		0 on success, -1 on failure.
 */
static int seekMp3(size_t sample)
{
	off_t targetFrame = (off_t)(sample / (size_t)channels);
	if(mpg123_seek(mh, targetFrame, SEEK_SET) < 0)
		return -1;
	return 0;
}

/**
 * Free MP3 decoder.
 */
void exitMp3(void)
{
	mpg123_close(mh);
	mpg123_delete(mh);
	mh = NULL;
	mpg123_exit();
	bufioClose(&mp3File);
}

/**
 * Check if a file is a valid MP3 file.
 * 
 * \param path	Path to the MP3 file.
 * \return		0 if valid MP3, 1 if not valid.
 */
int isMp3(const char *path)
{
#if 0
	int err;
	int result = 1;
	mpg123_handle *mh = NULL;
	long rate;
	int channels, encoding;

	// Initialise the library
	if (mpg123_init() != MPG123_OK)
		goto out;

	// Create a decoder handle
	mh = mpg123_new(NULL, &err);
	if (!mh)
		goto exit_init;

	// skip ID3v2 tags rather than parsing them (so tag-only files don’t count as valid mp3)
	mpg123_param(mh, MPG123_SKIP_ID3V2, 1, 0);

	// limit how many bytes to scan for a frame sync (e.g. 2048 bytes)
	mpg123_param(mh, MPG123_RESYNC_LIMIT, 2048, 0);

	// Try opening the file
	err = mpg123_open(mh, path);
	if (err != MPG123_OK)
		goto exit_handle;

	// Query the decoded format
	if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK)
		goto close_handle;

	// Parse first frame in file
	err = mpg123_framebyframe_next(mh);
	if (err != MPG123_OK)
	{
		// If we can't read the first frame, it's not a valid MP3
		goto close_handle;
	}

	// All checks passed: valid MP3
	result = 0;

close_handle:
	mpg123_close(mh);

exit_handle:
	mpg123_delete(mh);

exit_init:
	mpg123_exit();

out:
	return result;
#else
    unsigned char buf[4];
	FILE *f = fopen(path, "rb");
	int ret = 1;

	if(!f) return 1;

    if(fread(buf, 1, 4, f) < 4)
		goto out;

    // ID3v2 tag?
    if(buf[0]=='I' && buf[1]=='D' && buf[2]=='3') {
		ret = 0;
		goto out;
	}

    // MPEG frame sync: 11 one-bits in a row
    if(buf[0]==0xFF && (buf[1]&0xE0)==0xE0) {
		ret = 0;
		goto out;
	}

out:
	fclose(f);
    return ret;
#endif
}


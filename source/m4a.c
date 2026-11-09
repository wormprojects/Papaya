/**
 * ctrmus - 3DS Music Player
 * M4A/AAC decoder using minimp4 (demuxer) + FAAD2 (decoder).
 *
 * Supports M4A files containing AAC-LC audio.
 */

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
#include <neaacdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bufio.h"
#include "m4a.h"
#include "playback.h"

/* Decoder state */
/*
 * Every AAC frame is fetched with a seek to its offset followed by a small
 * read. Through stdio that was ruinous on the SD card: newlib never keeps
 * its buffer across an fseek(), so each ~500 byte frame re-read the whole
 * stdio buffer. bufio serves those seeks from memory.
 */
static struct bufio         m4aFile = { .fd = -1 };
static NeAACDecHandle       hDecoder = NULL;
static MP4D_demux_t         mp4Demux;
static uint32_t             m4aRate = 0;
static uint8_t              m4aChannels = 0;
static size_t               m4aTotalSamples = 0;
static const size_t         m4aBuffSize = 16 * 1024;

/* Track state */
static unsigned             m4aTrackIndex = 0;
static unsigned             m4aSampleIndex = 0;
static unsigned             m4aTotalFrames = 0;

/* Maximum AAC frame size we'll handle */
#define M4A_MAX_FRAME_BYTES (16 * 1024)

/* Internal decoded PCM buffer for leftovers */
static int16_t*             pcmResidual = NULL;
static size_t               pcmResidualLen = 0;     /* samples remaining */
static size_t               pcmResidualOffset = 0;  /* read position */

/* Pre-allocated buffer for AAC frame data */
static unsigned char*       m4aFrameBuf = NULL;

static int initM4a(const char* file);
static uint32_t rateM4a(void);
static uint8_t channelM4a(void);
static uint64_t decodeM4a(void* buffer);
static void exitM4a(void);
static size_t getFileSamplesM4a(void);
static int seekM4a(size_t sample);

/**
 * minimp4 read callback: reads from our FILE*.
 */
static int mp4ReadCallback(int64_t offset, void* buffer, size_t size, void* token)
{
	struct bufio* f = token;

	if(bufioSeek(f, offset, SEEK_SET) != 0)
		return -1;

	if(bufioRead(f, buffer, size) != size)
		return -1;

	return 0;
}

void setM4a(struct decoder_fn* decoder)
{
	decoder->init = &initM4a;
	decoder->rate = &rateM4a;
	decoder->channels = &channelM4a;
	decoder->buffSize = m4aBuffSize;
	decoder->decode = &decodeM4a;
	decoder->exit = &exitM4a;
	decoder->getFileSamples = &getFileSamplesM4a;
	decoder->seek = &seekM4a;
}

static int initM4a(const char* file)
{
	unsigned long sampleRate = 0;
	unsigned char channels = 0;
	int audioTrack = -1;

	memset(&mp4Demux, 0, sizeof(mp4Demux));

	if(bufioOpen(&m4aFile, file, 64 * 1024) != 0)
		return -1;

	/* Demux the MP4 container */
	if(MP4D_open(&mp4Demux, mp4ReadCallback, &m4aFile,
	             bufioSize(&m4aFile)) == 0)
	{
		exitM4a();
		return -1;
	}

	/* Find the first audio track */
	for(unsigned i = 0; i < mp4Demux.track_count; i++)
	{
		MP4D_track_t* track = &mp4Demux.track[i];
		if(track->handler_type == MP4D_HANDLER_TYPE_SOUN)
		{
			audioTrack = (int)i;
			m4aTrackIndex = i;
			m4aTotalFrames = track->sample_count;
			break;
		}
	}

	if(audioTrack < 0)
	{
		exitM4a();
		return -1;
	}

	/* Initialize FAAD2 decoder */
	hDecoder = NeAACDecOpen();
	if(hDecoder == NULL)
	{
		exitM4a();
		return -1;
	}

	/* Configure FAAD2 */
	{
		NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hDecoder);
		conf->outputFormat = FAAD_FMT_16BIT;
		conf->downMatrix = 1; /* Downmix to stereo if needed */
		conf->dontUpSampleImplicitSBR = 1;
		NeAACDecSetConfiguration(hDecoder, conf);
	}

	/* Get the decoder-specific info (ASC) from the MP4 track */
	{
		MP4D_track_t* track = &mp4Demux.track[m4aTrackIndex];

		if(track->dsi != NULL && track->dsi_bytes > 0)
		{
			int err = NeAACDecInit2(hDecoder,
				(unsigned char*)track->dsi,
				track->dsi_bytes,
				&sampleRate, &channels);

			if(err < 0)
			{
				exitM4a();
				return -1;
			}
		}
		else
		{
			/* No ASC available; try raw init with first sample */
			unsigned frame_bytes = 0;
			unsigned timestamp = 0;
			unsigned duration = 0;
			MP4D_file_offset_t ofs;
			unsigned char* buf;

			ofs = MP4D_frame_offset(&mp4Demux, m4aTrackIndex, 0,
					&frame_bytes, &timestamp, &duration);

			if(frame_bytes == 0)
			{
				exitM4a();
				return -1;
			}

			buf = malloc(frame_bytes);
			if(buf == NULL)
			{
				exitM4a();
				return -1;
			}

			if(bufioSeek(&m4aFile, (int64_t)ofs, SEEK_SET) != 0 ||
			   bufioRead(&m4aFile, buf, frame_bytes) != frame_bytes)
			{
				free(buf);
				exitM4a();
				return -1;
			}

			{
				long bytesConsumed = NeAACDecInit(hDecoder, buf,
						frame_bytes, &sampleRate, &channels);
				free(buf);

				if(bytesConsumed < 0)
				{
					exitM4a();
					return -1;
				}
			}
		}
	}

	m4aRate = (uint32_t)sampleRate;
	m4aChannels = (uint8_t)channels;
	if(m4aChannels == 0) m4aChannels = 2;
	if(m4aRate == 0) m4aRate = 44100;

	m4aSampleIndex = 0;
	pcmResidual = NULL;
	pcmResidualLen = 0;
	pcmResidualOffset = 0;

	/* Pre-allocate AAC frame buffer */
	m4aFrameBuf = malloc(M4A_MAX_FRAME_BYTES);
	if(m4aFrameBuf == NULL)
	{
		NeAACDecClose(hDecoder);
		hDecoder = NULL;
		exitM4a();
		return -1;
	}

	/* Calculate total samples from track duration */
	{
		MP4D_track_t* track = &mp4Demux.track[m4aTrackIndex];
		if(track->duration_hi == 0 && track->timescale > 0)
		{
			m4aTotalSamples = (size_t)((uint64_t)track->duration_lo *
				m4aRate / track->timescale) * m4aChannels;
		}
		else
		{
			m4aTotalSamples = 0;
		}
	}

	return 0;
}

static uint32_t rateM4a(void)
{
	return m4aRate;
}

static uint8_t channelM4a(void)
{
	return m4aChannels;
}

static size_t getFileSamplesM4a(void)
{
	return m4aTotalSamples;
}

static uint64_t decodeM4a(void* buffer)
{
	int16_t* outBuf = (int16_t*)buffer;
	size_t samplesWritten = 0;
	size_t samplesNeeded = m4aBuffSize;

	/* First, drain any leftover PCM from previous decode */
	if(pcmResidualLen > 0)
	{
		size_t toCopy = pcmResidualLen;
		if(toCopy > samplesNeeded)
			toCopy = samplesNeeded;

		memcpy(outBuf, pcmResidual + pcmResidualOffset,
				toCopy * sizeof(int16_t));
		samplesWritten += toCopy;
		pcmResidualOffset += toCopy;
		pcmResidualLen -= toCopy;

		if(pcmResidualLen == 0)
		{
			free(pcmResidual);
			pcmResidual = NULL;
			pcmResidualOffset = 0;
		}
	}

	/* Decode MP4 frames until buffer is full or EOF */
	while(samplesWritten < samplesNeeded && m4aSampleIndex < m4aTotalFrames)
	{
		unsigned frame_bytes = 0;
		unsigned timestamp = 0;
		unsigned duration = 0;
		MP4D_file_offset_t ofs;
		NeAACDecFrameInfo frameInfo;
		void* decoded;

		ofs = MP4D_frame_offset(&mp4Demux, m4aTrackIndex, m4aSampleIndex,
				&frame_bytes, &timestamp, &duration);

		if(frame_bytes == 0 || frame_bytes > M4A_MAX_FRAME_BYTES)
			break;

		if(bufioSeek(&m4aFile, (int64_t)ofs, SEEK_SET) != 0 ||
		   bufioRead(&m4aFile, m4aFrameBuf, frame_bytes) != frame_bytes)
			break;

		decoded = NeAACDecDecode(hDecoder, &frameInfo, m4aFrameBuf, frame_bytes);
		m4aSampleIndex++;

		if(frameInfo.error > 0 || decoded == NULL)
			continue;

		if(frameInfo.samples == 0)
			continue;

		/* Copy decoded 16-bit PCM samples */
		{
			size_t decodedSamples = frameInfo.samples;
			int16_t* pcm = (int16_t*)decoded;
			size_t spaceLeft = samplesNeeded - samplesWritten;

			if(decodedSamples <= spaceLeft)
			{
				memcpy(outBuf + samplesWritten, pcm,
						decodedSamples * sizeof(int16_t));
				samplesWritten += decodedSamples;
			}
			else
			{
				/* Copy what fits, store the rest */
				memcpy(outBuf + samplesWritten, pcm,
						spaceLeft * sizeof(int16_t));
				samplesWritten += spaceLeft;

				/* Store residual */
				{
					size_t leftover = decodedSamples - spaceLeft;
					pcmResidual = malloc(leftover * sizeof(int16_t));
					if(pcmResidual != NULL)
					{
						memcpy(pcmResidual, pcm + spaceLeft,
								leftover * sizeof(int16_t));
						pcmResidualLen = leftover;
						pcmResidualOffset = 0;
					}
				}
			}
		}
	}

	return samplesWritten;
}

/**
 * Seek to a sample position in M4A file.
 * Maps PCM sample position to the correct MP4 frame index.
 *
 * \param sample	Target sample position (total samples across all channels).
 * \return		0 on success, -1 on failure.
 */
static int seekM4a(size_t sample)
{
	unsigned targetFrame;
	size_t samplesPerFrame;

	if(m4aTotalFrames == 0 || m4aTotalSamples == 0)
		return -1;

	/* Estimate samples per MP4 frame */
	samplesPerFrame = m4aTotalSamples / m4aTotalFrames;
	if(samplesPerFrame == 0) samplesPerFrame = 1024 * m4aChannels;

	targetFrame = (unsigned)(sample / samplesPerFrame);
	if(targetFrame >= m4aTotalFrames)
		targetFrame = m4aTotalFrames - 1;

	m4aSampleIndex = targetFrame;

	/* Discard residual PCM */
	if(pcmResidual != NULL)
	{
		free(pcmResidual);
		pcmResidual = NULL;
	}
	pcmResidualLen = 0;
	pcmResidualOffset = 0;

	return 0;
}

static void exitM4a(void)
{
	if(pcmResidual != NULL)
	{
		free(pcmResidual);
		pcmResidual = NULL;
	}
	pcmResidualLen = 0;
	pcmResidualOffset = 0;

	if(m4aFrameBuf != NULL)
	{
		free(m4aFrameBuf);
		m4aFrameBuf = NULL;
	}

	if(hDecoder != NULL)
	{
		NeAACDecClose(hDecoder);
		hDecoder = NULL;
	}

	MP4D_close(&mp4Demux);
	memset(&mp4Demux, 0, sizeof(mp4Demux));

	bufioClose(&m4aFile);

	m4aSampleIndex = 0;
	m4aTotalFrames = 0;
}

/**
 * Check if a file is an M4A/MP4 container with audio.
 * Checks for "ftyp" box at offset 4.
 *
 * \param file	Path to check.
 * \return		0 if M4A, -1 otherwise.
 */
int isM4a(const char* file)
{
	FILE* f;
	unsigned char buf[8];

	f = fopen(file, "rb");
	if(f == NULL)
		return -1;

	if(fread(buf, 1, 8, f) != 8)
	{
		fclose(f);
		return -1;
	}

	fclose(f);

	/* MP4 files have "ftyp" at offset 4 */
	if(buf[4] == 'f' && buf[5] == 't' && buf[6] == 'y' && buf[7] == 'p')
		return 0;

	return -1;
}

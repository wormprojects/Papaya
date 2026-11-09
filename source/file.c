#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "error.h"
#include "file.h"
#include "flac.h"
#include "m4a.h"
#include "mp3.h"
#include "opus.h"
#include "vorbis.h"
#include "wav.h"
#include "sid.h"

/* ------------------------------------------------------------------ */
/* Fast extension-based file type guess (no I/O)                      */
/* ------------------------------------------------------------------ */

static int strcasecmp_local(const char* a, const char* b)
{
	while(*a && *b)
	{
		char ca = *a, cb = *b;
		if(ca >= 'A' && ca <= 'Z') ca += 32;
		if(cb >= 'A' && cb <= 'Z') cb += 32;
		if(ca != cb) return (unsigned char)ca - (unsigned char)cb;
		a++; b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

enum file_types getFileTypeFromExt(const char *file)
{
	if(file == NULL)
		return FILE_TYPE_ERROR;

	const char* dot = strrchr(file, '.');
	if(dot == NULL)
		return FILE_TYPE_ERROR;

	dot++; /* skip the '.' */

	if(strcasecmp_local(dot, "wav") == 0 || strcasecmp_local(dot, "aif") == 0 ||
	   strcasecmp_local(dot, "aiff") == 0)
		return FILE_TYPE_WAV;

	if(strcasecmp_local(dot, "flac") == 0)
		return FILE_TYPE_FLAC;

	if(strcasecmp_local(dot, "ogg") == 0 || strcasecmp_local(dot, "oga") == 0)
		return FILE_TYPE_VORBIS; /* Could be vorbis, opus or flac in ogg */

	if(strcasecmp_local(dot, "opus") == 0)
		return FILE_TYPE_OPUS;

	if(strcasecmp_local(dot, "mp3") == 0)
		return FILE_TYPE_MP3;

	if(strcasecmp_local(dot, "sid") == 0)
		return FILE_TYPE_SID;

	if(strcasecmp_local(dot, "m4a") == 0 || strcasecmp_local(dot, "mp4") == 0 ||
	   strcasecmp_local(dot, "aac") == 0)
		return FILE_TYPE_M4A;

	return FILE_TYPE_ERROR;
}

/**
 * Obtain file type string from file_types enum.
 *
 * \param	ft	File type enum.
 * \return		String representation of enum.
 */
const char* fileToStr(enum file_types ft)
{
	static const char *file_types_str[] = {
		"UNKNOWN",
		"WAV",
		"FLAC",
		"VORBIS",
		"OPUS",
		"MP3",
		"SID",
		"M4A"
	};

	return file_types_str[ft];
}

/**
 * Obtains file type.
 *
 * \param	file	File location.
 * \return			file_types enum or 0 on error and errno set.
 */
enum file_types getFileType(const char *file)
{
	FILE* ftest = fopen(file, "rb");
	uint32_t fileSig;
	enum file_types file_type = FILE_TYPE_ERROR;

	/* Failure opening file */
	if(ftest == NULL)
		return FILE_TYPE_ERROR;

	if(fread(&fileSig, 4, 1, ftest) == 0)
		goto err;

	switch(fileSig)
	{
		// "RIFF"
		case 0x46464952:
		// "riff"
		case 0x66666972:
		// "RIFX"
		case 0x58464952:
		// "RF64"
		case 0x34364652:
		// "FORM"
		case 0x4D524F46:
			file_type = FILE_TYPE_WAV;
			break;

		// "fLaC"
		case 0x43614c66:
			file_type = FILE_TYPE_FLAC;
			break;

		// "OggS"
		case 0x5367674F:
			if(isOpus(file) == 0)
				file_type = FILE_TYPE_OPUS;
			else if(isFlac(file) == 0)
				file_type = FILE_TYPE_FLAC;
			else if(isVorbis(file) == 0)
				file_type = FILE_TYPE_VORBIS;
			else
				errno = FILE_NOT_SUPPORTED;

			break;
		
		// "PSID" or "RSID"
		case 0x44495350:
		case 0x44495352:
			file_type = FILE_TYPE_SID;
			break;

		default:
			/*
			 * MP3 without ID3 tag, ID3v1 tag is at the end of file, or MP3
			 * with ID3 tag at the beginning  of the file.
			 */
			if(isMp3(file) == 0)
			{
				file_type = FILE_TYPE_MP3;
				break;
			}

			/* M4A/MP4 container: "ftyp" appears at byte offset 4 */
			if(isM4a(file) == 0)
			{
				file_type = FILE_TYPE_M4A;
				break;
			}

			errno = FILE_NOT_SUPPORTED;
			break;
	}

err:
	fclose(ftest);
	return file_type;
}


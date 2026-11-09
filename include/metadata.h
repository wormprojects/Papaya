/**
 * ctrmus - 3DS Music Player
 * Metadata extraction from audio files.
 *
 * Supports ID3v2 (MP3), Vorbis comments (OGG/FLAC/Opus), M4A atoms.
 */

#ifndef ctrmus_metadata_h
#define ctrmus_metadata_h

#include <stdbool.h>
#include <stddef.h>

struct music_metadata
{
	char title[128];
	char artist[128];
	char album[128];
	bool has_title;
	bool has_artist;
	bool has_album;
};

/**
 * Extract metadata (title, artist, album) from an audio file.
 * Supports MP3 (ID3v2), Vorbis, Opus, FLAC (Vorbis comments), M4A.
 *
 * \param	file	Path to the audio file.
 * \param	meta	Output metadata structure (zeroed on entry).
 * \return			0 on success (at least one field found), -1 on failure.
 */
int getMetadata(const char* file, struct music_metadata* meta);

/**
 * Get the display title for an audio file.
 * Returns metadata title if available, otherwise the filename
 * (without directory path).
 *
 * \param	filepath	Full or relative path to the audio file.
 * \param	out			Output buffer for display title.
 * \param	outLen		Size of output buffer.
 */
void getDisplayTitle(const char* filepath, char* out, size_t outLen);

/**
 * Read the embedded cover art of a file: ID3v2 APIC/PIC, FLAC PICTURE
 * blocks, Ogg METADATA_BLOCK_PICTURE / COVERART comments, or M4A covr.
 * Prefers the front cover if there are several. Reentrant.
 *
 * \param	data	Out: malloc()ed image file bytes (JPEG, PNG, ...); the
 *					caller frees it.
 * \param	len		Out: their size.
 * \return			0 if a picture was found, -1 otherwise.
 */
int getCoverArt(const char* file, unsigned char** data, size_t* len);

#endif

/**
 * ctrmus - 3DS Music Player
 * Background cover art loader.
 *
 * One low-priority worker thread reads a song's embedded picture
 * (getCoverArt) and decodes it to a small RGB565 image (artDecode), so
 * starting a song never waits on it. Only the newest request matters: an
 * older one still being decoded is thrown away when it finishes. Nothing
 * is cached; the art is loaded each time a song starts.
 */

#ifndef ctrmus_coverart_h
#define ctrmus_coverart_h

#include <stdbool.h>
#include <stdint.h>

/* Width/height of the decoded art (matches the top screen's art box). */
#define COVER_ART_SIZE 144

void coverArtInit(void);
void coverArtExit(void);

/**
 * Start loading the art of a file (absolute path). Replaces any earlier
 * request.
 *
 * \param	alphaBg	RGB565 colour transparent PNG pixels blend to.
 */
void coverArtRequest(const char* path, uint16_t alphaBg);

/**
 * Collect the result of the latest request, once.
 *
 * \param	pixels	Out: COVER_ART_SIZE^2 RGB565, image at the top left.
 * \param	hasArt	Out: false if the file has no (decodable) art.
 * \return			true if a new result was delivered.
 */
bool coverArtPoll(uint16_t* pixels, int* w, int* h, bool* hasArt);

#endif

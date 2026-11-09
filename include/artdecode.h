/**
 * ctrmus - 3DS Music Player
 * Cover art decoding: JPEG (libjpeg-turbo) or PNG (libpng) to a small
 * RGB565 image.
 *
 * Built to keep memory use small whatever the picture size: JPEGs are
 * decoded at 1/2, 1/4 or 1/8 size by libjpeg's DCT scaling (a 3000x3000
 * cover decodes as 375x375), and both formats are fed through a streaming
 * box filter one row at a time, so the full image is never held in memory
 * (except interlaced PNGs, which are capped in size).
 *
 * Plain C, no libctru: also built on a PC by tools/preview and
 * tools/arttest.
 */

#ifndef ctrmus_artdecode_h
#define ctrmus_artdecode_h

#include <stddef.h>
#include <stdint.h>

/**
 * Decode an image and scale it to fit in size x size, keeping its aspect
 * ratio.
 *
 * \param	out		size * size RGB565 pixels, row-major. The image is
 *					written at the top left, *w x *h of it (row stride
 *					is still `size`).
 * \param	alphaBg	RGB565 colour that transparent PNG pixels blend to.
 * \return			0 on success, -1 if the data can't be decoded.
 */
int artDecode(const unsigned char* data, size_t len, uint16_t* out,
              int size, uint16_t alphaBg, int* w, int* h);

#endif

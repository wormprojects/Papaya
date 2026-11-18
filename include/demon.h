/**
 * Demon mode (settingsDemonMode): while a song plays in reverse, the
 * screens get a drifting rainbow tint and the odd glitch (a band of rows
 * torn sideways for a moment). Brightness is kept, so text stays readable.
 *
 * Plain C, shared by ui.c (3DS: applied as the frames are copied to the
 * framebuffers) and the macOS shim (applied as the window converts them).
 */

#ifndef papaya_demon_h
#define papaya_demon_h

#include <stdint.h>

/**
 * Apply the effect to one screen: src -> dst (column-major RGB565, w
 * columns of h pixels, like the 3DS framebuffers). `ms` is a clock in
 * milliseconds: the rainbow drifts and the glitches come and go with it.
 * src and dst must not overlap.
 */
void demonApply(const uint16_t* src, uint16_t* dst, int w, int h,
                unsigned long ms);

#endif

/**
 * The app uses Tremor (the 3DS's integer-only Vorbis decoder). On the host
 * libvorbis has the same API except ov_read(), which takes three more
 * arguments (byte order, sample size, signedness): native-endian signed
 * 16-bit is what Tremor always returns.
 */
#ifndef papaya_ivorbisfile_h
#define papaya_ivorbisfile_h

#include <vorbis/vorbisfile.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PAPAYA_VORBIS_BIGENDIAN 1
#else
#define PAPAYA_VORBIS_BIGENDIAN 0
#endif

#define ov_read(vf, buf, len, bitstream) \
	ov_read((vf), (buf), (len), PAPAYA_VORBIS_BIGENDIAN, 2, 1, (bitstream))

#endif

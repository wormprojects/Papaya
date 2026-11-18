/**
 * ctrmus - 3DS Music Player
 * Audio effects applied to the decoded samples on the playback thread,
 * before they go to the DSP: equalizer first, then distortion, then reverb
 * (like pedals in that order), with bitcrush after the distortion and
 * spatial widening last.
 *
 * - Equalizer: a preamp (0 to -30 dB, set by hand: lower it by as much as
 *   the bands boost and the sound can't clip, then raise the 3DS volume)
 *   and 10 bands (31 Hz to 16 kHz, one octave apart), each -18 to +18 dB,
 *   as peaking biquads (Q 1.41) in floats. Values are in tenths of a dB.
 *   Bands at 0 dB are skipped completely, and with everything flat the
 *   equalizer costs nothing. Bands too close to the song's Nyquist
 *   frequency are skipped. Coefficients are worked out on the playback
 *   thread when a value changes; the preamp ramps across the buffer.
 *   First in the chain, so a bass boost drives the distortion.
 *
 * - Distortion: a tanh soft clipper. The intensity (0-100%) sets the drive
 *   (1x to 30x, exponential so the steps sound even), with make-up gain so
 *   it gets dirtier rather than louder. Done with a 1025-point table and
 *   interpolation: a few integer operations per sample.
 * - Reverb: Freeverb-style, 8 damped comb filters and 4 allpasses per
 *   channel (stereo by slightly different delays), fed with the mono sum.
 *   The time is the RT60 (how long the tail takes to fall by 60 dB); the
 *   comb inputs are scaled so the wet signal is about as loud as the dry
 *   one at any time setting. Mix is an equal-power crossfade. Floats; a
 *   few percent of an Old 3DS core at 44.1 kHz stereo. Buffers (~100 KB
 *   at 44.1 kHz) are allocated the first time it's used.
 *
 * - Bitcrush: bit depth and sample rate. The samples are 16-bit, so 32, 24
 *   and 16 bit are the same thing (off); below that the low bits are
 *   rounded away (nearest step, so silence stays silent; 1 bit is three
 *   levels). The sample rate holds each sample until the next one is due
 *   at that rate (sample and hold, no filter: the aliasing is the sound).
 *   32k, the top setting, is off (the 3DS's own rate); a song already
 *   at a lower rate than the setting is left alone. Integer maths, no
 *   memory.
 * - Spatial: stereo widening. The side signal (L-R) is boosted by up to 4x
 *   (intensity 0-300%, side gain 1 + pct/100) while the mid signal (L+R) stays put, so centred
 *   vocals stay centred and the rest spreads out. A fixed trim assuming a
 *   typical mid/side balance keeps the loudness about the same. Gains
 *   ramp across each buffer (no clicks when the slider moves). Integer
 *   maths, no memory, nothing for mono files. Runs after the reverb so
 *   the tail is widened too.
 *
 * Plain C with no libctru dependency (tools/fxtest tests it on a PC).
 * The setters are called from the UI thread; they only store ints, which
 * the playback thread picks up at its next buffer.
 */

#ifndef ctrmus_effects_h
#define ctrmus_effects_h

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EFFECTS_REVERB_TIME_MIN		5	/* tenths of a second */
#define EFFECTS_REVERB_TIME_MAX		60

/* UI thread */
void effectsSetDistortion(int pct);		/* 0 = off .. 100 */
int effectsDistortion(void);
void effectsSetReverbMix(int pct);		/* 0 = off .. 100 = only reverb */
int effectsReverbMix(void);
void effectsSetReverbTime(int tenths);	/* RT60 in 0.1 s */
int effectsReverbTime(void);
#define EFFECTS_SPATIAL_MAX			300	/* slider: % (100 = side x2, 300 = x4) */
#define EFFECTS_SPATIAL_CUSTOM_MAX	1000	/* typed values go up to this */

#define EFFECTS_BITS_OFF			32	/* 32, 24 and 16 bit do nothing */
#define EFFECTS_BITS_MIN			1
#define EFFECTS_RATE_OFF			32000	/* Hz: the top setting is off */
#define EFFECTS_RATE_MIN			100

void effectsSetBitDepth(int bits);		/* EFFECTS_BITS_MIN .. 32 */
int effectsBitDepth(void);
void effectsSetSampleRate(int hz);		/* EFFECTS_RATE_MIN .. 32000 */
int effectsSampleRate(void);

void effectsSetSpatial(int pct);		/* 0 = off .. EFFECTS_SPATIAL_CUSTOM_MAX */
int effectsSpatial(void);

/* Equalizer (tenths of a dB) */
#define EFFECTS_EQ_BANDS			10
#define EFFECTS_EQ_BAND_MAX			180		/* bands: -180 .. +180 */
#define EFFECTS_EQ_PREAMP_MIN		(-300)	/* preamp: -300 .. 0 */

void effectsSetEqBand(int band, int tenths);	/* 0 .. EFFECTS_EQ_BANDS-1 */
int effectsEqBand(int band);
void effectsSetEqPreamp(int tenths);
int effectsEqPreamp(void);
int effectsEqFreq(int band);			/* Hz */
/** True if the equalizer changes anything (a band or the preamp is off 0). */
bool effectsEqActive(void);
/** How far the bands lift the loudest frequency, in tenths of a dB (0 if
 * they don't lift anything). The preamp has to be at least this far down
 * for a full-scale tone not to clip. */
int effectsEqPeak(void);
/** Preamp = minus the peak, rounded up to a whole dB. */
void effectsEqAutoPreamp(void);
/** All bands and the preamp back to 0. */
void effectsEqReset(void);
int effectsEqPresetCount(void);
const char* effectsEqPresetName(int preset);
/** Sets the bands, and the preamp to match (effectsEqAutoPreamp). */
void effectsEqApplyPreset(int preset);
/** The preset the bands match now (the preamp doesn't count), or -1. */
int effectsEqCurrentPreset(void);

/** "+3 dB", "-18 dB", "+2.5 dB" (tenths of a dB). */
static inline void effectsFormatDb(char* buf, size_t len, int tenths)
{
	int a = tenths < 0 ? -tenths : tenths;

	if(a % 10 == 0)
		snprintf(buf, len, "%s%d dB", tenths < 0 ? "-" : tenths > 0 ? "+" : "",
		         a / 10);
	else
		snprintf(buf, len, "%s%d.%d dB", tenths < 0 ? "-" : "+", a / 10,
		         a % 10);
}

/** false forces the reverb off (performance mode) without changing the
 * mix setting. */
void effectsSetReverbAllowed(bool allowed);
/** The reverb mix in use now: 0 when off or not allowed. */
int effectsReverbActiveMix(void);

/** "1.0x", or "0.99x" when the speed isn't a multiple of 0.1 (typed
 * values; the slider steps by 0.1). */
static inline void effectsFormatSpeed(char* buf, size_t len, float speed)
{
	int h = (int)(fabsf(speed) * 100.0f + 0.5f);
	const char* sign = (speed < 0.0f && h > 0) ? "-" : "";

	if(h % 10 == 0)
		snprintf(buf, len, "%s%d.%dx", sign, h / 100, (h / 10) % 10);
	else
		snprintf(buf, len, "%s%d.%02dx", sign, h / 100, h % 100);
}

/** "32k", "1.5k", or "750 Hz" / "22050 Hz" for the rest. */
static inline void effectsFormatRate(char* buf, size_t len, int hz)
{
	if(hz >= 1000 && hz % 1000 == 0)
		snprintf(buf, len, "%dk", hz / 1000);
	else if(hz >= 1000 && hz % 100 == 0)
		snprintf(buf, len, "%d.%dk", hz / 1000, (hz / 100) % 10);
	else
		snprintf(buf, len, "%d Hz", hz);
}

/* Playback thread */
/** A new song starts: clears the reverb tail. */
void effectsStart(unsigned rate, int channels);
/** Apply the effects in place to `frames` frames of interleaved samples. */
void effectsProcess(int16_t* samples, size_t frames);
/** Free the reverb buffers (at exit). */
void effectsFree(void);

#endif

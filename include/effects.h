/**
 * ctrmus - 3DS Music Player
 * Audio effects applied to the decoded samples on the playback thread,
 * before they go to the DSP: distortion, then reverb (like pedals in that
 * order).
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
 * Plain C with no libctru dependency (tools/fxtest tests it on a PC).
 * The setters are called from the UI thread; they only store ints, which
 * the playback thread picks up at its next buffer.
 */

#ifndef ctrmus_effects_h
#define ctrmus_effects_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EFFECTS_REVERB_TIME_MIN		5	/* tenths of a second */
#define EFFECTS_REVERB_TIME_MAX		60

/* UI thread */
void effectsSetDistortion(int pct);		/* 0 = off .. 100 */
int effectsDistortion(void);
void effectsSetReverbMix(int pct);		/* 0 = off .. 100 = only reverb */
int effectsReverbMix(void);
void effectsSetReverbTime(int tenths);	/* RT60 in 0.1 s */
int effectsReverbTime(void);
/** false forces the reverb off (performance mode) without changing the
 * mix setting. */
void effectsSetReverbAllowed(bool allowed);
/** The reverb mix in use now: 0 when off or not allowed. */
int effectsReverbActiveMix(void);

/* Playback thread */
/** A new song starts: clears the reverb tail. */
void effectsStart(unsigned rate, int channels);
/** Apply the effects in place to `frames` frames of interleaved samples. */
void effectsProcess(int16_t* samples, size_t frames);
/** Free the reverb buffers (at exit). */
void effectsFree(void);

#endif

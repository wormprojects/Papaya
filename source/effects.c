/**
 * ctrmus - 3DS Music Player
 * Distortion and reverb. See effects.h.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "effects.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Settings (written by the UI thread, read per buffer)                */
/* ------------------------------------------------------------------ */

static volatile int		distortionPct = 0;
static volatile int		reverbMixPct = 0;
static volatile int		reverbTenths = 20;
static volatile bool	reverbAllowed = true;

static int clampInt(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

void effectsSetDistortion(int pct)
{
	distortionPct = clampInt(pct, 0, 100);
}

int effectsDistortion(void)
{
	return distortionPct;
}

void effectsSetReverbMix(int pct)
{
	reverbMixPct = clampInt(pct, 0, 100);
}

int effectsReverbMix(void)
{
	return reverbMixPct;
}

void effectsSetReverbTime(int tenths)
{
	reverbTenths = clampInt(tenths, EFFECTS_REVERB_TIME_MIN,
	                        EFFECTS_REVERB_TIME_MAX);
}

int effectsReverbTime(void)
{
	return reverbTenths;
}

void effectsSetReverbAllowed(bool allowed)
{
	reverbAllowed = allowed;
}

int effectsReverbActiveMix(void)
{
	return reverbAllowed ? reverbMixPct : 0;
}

/* ------------------------------------------------------------------ */
/* Distortion                                                          */
/* ------------------------------------------------------------------ */

#define DIST_SHIFT	6						/* 65536 >> 6 = 1024 segments */
#define DIST_SEGS	(65536 >> DIST_SHIFT)
#define DIST_MAX_DRIVE	30.0f
/* Make-up gain is set so a sine at this level keeps its loudness. */
#define DIST_REF_LEVEL	0.3f

static int32_t	distTable[DIST_SEGS + 1];
static int		distTableFor = -1;

static void distBuild(int pct)
{
	float	drive = powf(DIST_MAX_DRIVE, pct / 100.0f);
	float	norm = 1.0f / tanhf(drive);
	double	in = 0.0, out = 0.0;
	float	makeup;

	/* tanh(d x) / tanh(d) is never below x on [0, 1], so the make-up
	 * gain is <= 1 and the output can't clip. */
	for(int k = 0; k < 64; k++)
	{
		float x = DIST_REF_LEVEL * sinf(2.0f * (float)M_PI * k / 64.0f);
		float y = tanhf(drive * x) * norm;

		in += (double)x * x;
		out += (double)y * y;
	}
	makeup = out > 0.0 ? (float)sqrt(in / out) : 1.0f;

	for(int i = 0; i <= DIST_SEGS; i++)
	{
		float x = (float)((i << DIST_SHIFT) - 32768) / 32768.0f;
		float y = tanhf(drive * x) * norm * makeup;

		distTable[i] = (int32_t)lrintf(y * 32767.0f);
	}
	distTableFor = pct;
}

static void distProcess(int16_t* s, size_t n)
{
	for(size_t i = 0; i < n; i++)
	{
		int32_t v = (int32_t)s[i] + 32768;			/* 0 .. 65535 */
		int32_t k = v >> DIST_SHIFT;
		int32_t f = v & ((1 << DIST_SHIFT) - 1);
		int32_t a = distTable[k];

		s[i] = (int16_t)(a + (((distTable[k + 1] - a) * f) >> DIST_SHIFT));
	}
}

/* ------------------------------------------------------------------ */
/* Reverb                                                              */
/* ------------------------------------------------------------------ */

#define NUM_COMBS		8
#define NUM_ALLPASS		4
#define STEREO_SPREAD	23		/* extra delay of the right channel */
#define TUNING_RATE		44100u
#define DAMP1			0.2f	/* lowpass in the comb feedback */
#define DAMP2			(1.0f - DAMP1)
#define ALLPASS_FB		0.5f
/* Scales the comb inputs so wet comes out about as loud as dry (tuned
 * with tools/fxtest on pink noise). */
#define WET_TRIM		0.2f
/* Keeps the decaying tail out of the denormal range. */
#define ANTI_DENORMAL	1e-18f

/* Freeverb's delay lengths at 44.1 kHz (mutually prime-ish). */
static const int combTuning[NUM_COMBS] =
	{ 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int allpassTuning[NUM_ALLPASS] = { 556, 441, 341, 225 };

struct line
{
	float*	buf;
	int		len;
	int		pos;
};

struct comb
{
	struct line	l;
	float		filt;
	float		fb;
	float		gain;
};

static struct comb	combs[2][NUM_COMBS];
static struct line	allpasses[2][NUM_ALLPASS];
static float*		rvMem = NULL;
static unsigned		rvMemRate = 0;		/* rate the buffers are sized for */
static unsigned		rate = 44100;
static int			channels = 2;
static int			rvCoeffsFor = -1;	/* reverbTenths of the coefficients */
static bool			rvDirty = true;		/* buffers need clearing */
static bool			rvWasOn = false;

/* Integer maths on purpose: the 3DS build uses -ffast-math, under which
 * the same float expression was compiled two different ways (once as a
 * multiply by a folded reciprocal) and rounded differently, so the
 * buffer was sized a few floats short of the lines placed in it. */
static int scaledLen(int tuning, int ch)
{
	unsigned long long len =
		(unsigned long long)(tuning + ch * STEREO_SPREAD) * rate /
		TUNING_RATE;

	return len < 1 ? 1 : (int)len;
}

static bool rvAlloc(void)
{
	size_t	total = 0;
	float*	p;

	if(rvMem != NULL && rvMemRate == rate)
		return true;

	free(rvMem);
	rvMem = NULL;

	/* Each length is worked out once, and the buffer is sized from
	 * exactly those lengths. */
	for(int ch = 0; ch < 2; ch++)
	{
		for(int i = 0; i < NUM_COMBS; i++)
		{
			combs[ch][i].l.len = scaledLen(combTuning[i], ch);
			total += (size_t)combs[ch][i].l.len;
		}
		for(int i = 0; i < NUM_ALLPASS; i++)
		{
			allpasses[ch][i].len = scaledLen(allpassTuning[i], ch);
			total += (size_t)allpasses[ch][i].len;
		}
	}
	if((rvMem = malloc(total * sizeof(float))) == NULL)
		return false;
	rvMemRate = rate;

	p = rvMem;
	for(int ch = 0; ch < 2; ch++)
	{
		for(int i = 0; i < NUM_COMBS; i++)
		{
			combs[ch][i].l.buf = p;
			combs[ch][i].l.pos = 0;
			p += combs[ch][i].l.len;
		}
		for(int i = 0; i < NUM_ALLPASS; i++)
		{
			allpasses[ch][i].buf = p;
			allpasses[ch][i].pos = 0;
			p += allpasses[ch][i].len;
		}
	}
	/* Never write outside the block: if the lines don't fill it
	 * exactly, run without reverb rather than corrupt the heap. */
	if(p != rvMem + total)
	{
		free(rvMem);
		rvMem = NULL;
		rvMemRate = 0;
		return false;
	}
	rvDirty = true;
	rvCoeffsFor = -1;
	return true;
}

static void rvClear(void)
{
	for(int ch = 0; ch < 2; ch++)
	{
		for(int i = 0; i < NUM_COMBS; i++)
		{
			struct comb* c = &combs[ch][i];

			memset(c->l.buf, 0, (size_t)c->l.len * sizeof(float));
			c->l.pos = 0;
			c->filt = 0.0f;
		}
		for(int i = 0; i < NUM_ALLPASS; i++)
		{
			memset(allpasses[ch][i].buf, 0,
			       (size_t)allpasses[ch][i].len * sizeof(float));
			allpasses[ch][i].pos = 0;
		}
	}
	rvDirty = false;
}

/* Feedback for an RT60 of `tenths`/10 s: each pass round a comb of
 * length L samples must lose 60 dB * L / (rate * RT60). The input gain
 * then evens out the comb's resonance, so the time doesn't change the
 * loudness. */
static void rvCoeffs(int tenths)
{
	float rt60 = tenths / 10.0f;

	for(int ch = 0; ch < 2; ch++)
		for(int i = 0; i < NUM_COMBS; i++)
		{
			struct comb*	c = &combs[ch][i];
			float			fb = powf(10.0f, -3.0f * (float)c->l.len /
			                          ((float)rate * rt60));

			c->fb = fb;
			c->gain = WET_TRIM * sqrtf(1.0f - fb * fb) /
			          sqrtf((float)NUM_COMBS);
		}
	rvCoeffsFor = tenths;
}

static inline float rvChannel(int ch, float in)
{
	float acc = 0.0f;

	for(int i = 0; i < NUM_COMBS; i++)
	{
		struct comb*	c = &combs[ch][i];
		float			y = c->l.buf[c->l.pos];

		c->filt = y * DAMP2 + c->filt * DAMP1;
		c->l.buf[c->l.pos] = in * c->gain + c->filt * c->fb;
		if(++c->l.pos == c->l.len)
			c->l.pos = 0;
		acc += y;
	}

	for(int i = 0; i < NUM_ALLPASS; i++)
	{
		struct line*	a = &allpasses[ch][i];
		float			b = a->buf[a->pos];

		a->buf[a->pos] = acc + b * ALLPASS_FB;
		if(++a->pos == a->len)
			a->pos = 0;
		acc = b - acc;
	}

	return acc;
}

static inline int16_t sat16(float v)
{
	if(v > 32767.0f)
		return 32767;
	if(v < -32768.0f)
		return -32768;
	return (int16_t)lrintf(v);
}

static void rvProcess(int16_t* s, size_t frames, int mixPct)
{
	float	angle = (float)mixPct / 100.0f * (float)M_PI / 2.0f;
	float	dry = cosf(angle);
	float	wet = sinf(angle);

	if(reverbTenths != rvCoeffsFor)
		rvCoeffs(reverbTenths);
	if(rvDirty)
		rvClear();

	if(channels == 2)
	{
		for(size_t i = 0; i < frames; i++, s += 2)
		{
			float l = s[0], r = s[1];
			float in = (l + r) * 0.5f + ANTI_DENORMAL;

			s[0] = sat16(l * dry + rvChannel(0, in) * wet);
			s[1] = sat16(r * dry + rvChannel(1, in) * wet);
		}
	}
	else
	{
		for(size_t i = 0; i < frames; i++, s++)
		{
			float x = s[0];

			s[0] = sat16(x * dry + rvChannel(0, x + ANTI_DENORMAL) * wet);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Playback thread                                                     */
/* ------------------------------------------------------------------ */

void effectsStart(unsigned r, int ch)
{
	rate = r > 0 ? r : 44100;
	channels = ch == 1 ? 1 : 2;
	rvDirty = true;
	rvWasOn = false;
}

void effectsProcess(int16_t* samples, size_t frames)
{
	int dist = distortionPct;
	int mix = effectsReverbActiveMix();

	if(dist > 0)
	{
		if(dist != distTableFor)
			distBuild(dist);
		distProcess(samples, frames * (size_t)channels);
	}

	if(mix > 0 && rvAlloc())
	{
		/* Turned on again: start without the old tail. */
		if(!rvWasOn)
			rvDirty = true;
		rvProcess(samples, frames, mix);
		rvWasOn = true;
	}
	else
		rvWasOn = false;
}

void effectsFree(void)
{
	free(rvMem);
	rvMem = NULL;
	rvMemRate = 0;
}

/**
 * ctrmus - 3DS Music Player
 * Value entry page. See valueentry.h; drawn by bottomscreen.c (veDraw).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bottomscreen.h"
#include "effects.h"
#include "playback.h"
#include "ui.h"
#include "valueentry.h"

#define CARET_MS	500

struct target
{
	const char*	name;
	const char*	unit;
	double		min, max;
	int			decimals;	/* digits after the point that are kept */
};

static const struct target targets[VT_EQ_PRE + 1] =
{
	{ "speed", "x", 0.1, 3.0, 2 },
	{ "distortion", "%", 0.0, 100.0, 0 },
	{ "bit depth", "bit", EFFECTS_BITS_MIN, EFFECTS_BITS_OFF, 0 },
	{ "sample rate", "Hz", EFFECTS_RATE_MIN, EFFECTS_RATE_OFF, 0 },
	{ "spatial", "%", 0.0, EFFECTS_SPATIAL_CUSTOM_MAX, 0 },
	{ "reverb mix", "%", 0.0, 100.0, 0 },
	{ "reverb time", "s", EFFECTS_REVERB_TIME_MIN / 10.0,
	  EFFECTS_REVERB_TIME_MAX / 10.0, 1 },
	{ "eq preamp", "dB", EFFECTS_EQ_PREAMP_MIN / 10.0, 0.0, 1 },
};

static bool isEqBand(enum valueTarget t)
{
	return t >= VT_EQ_BAND0 && t < VT_COUNT;
}

static bool isDb(enum valueTarget t)
{
	return t == VT_EQ_PRE || isEqBand(t);
}

/* The target of `t`; an equalizer band's is made up (name from its
 * frequency). */
static const struct target* targetOf(enum valueTarget t)
{
	static struct target	band;
	static char				name[24];
	int						hz;

	if(!isEqBand(t))
		return &targets[t];
	hz = effectsEqFreq(t - VT_EQ_BAND0);
	if(hz >= 1000)
		snprintf(name, sizeof(name), "eq %d kHz", hz / 1000);
	else
		snprintf(name, sizeof(name), "eq %d Hz", hz);
	band.name = name;
	band.unit = "dB";
	band.min = -EFFECTS_EQ_BAND_MAX / 10.0;
	band.max = EFFECTS_EQ_BAND_MAX / 10.0;
	band.decimals = 1;
	return &band;
}

static double currentValue(enum valueTarget t)
{
	switch(t)
	{
		case VT_SPEED:			return getPlaybackSpeed();
		case VT_DISTORTION:		return effectsDistortion();
		case VT_BITDEPTH:		return effectsBitDepth();
		case VT_SAMPLERATE:		return effectsSampleRate();
		case VT_SPATIAL:		return effectsSpatial();
		case VT_REVERB:			return effectsReverbMix();
		case VT_REVERB_TIME:	return effectsReverbTime() / 10.0;
		case VT_EQ_PRE:			return effectsEqPreamp() / 10.0;
		default:
			return isEqBand(t) ? effectsEqBand(t - VT_EQ_BAND0) / 10.0 : 0.0;
	}
}

static void applyValue(enum valueTarget t, double v)
{
	switch(t)
	{
		case VT_SPEED:			setPlaybackSpeed((float)v); break;
		case VT_DISTORTION:		effectsSetDistortion((int)lrint(v)); break;
		case VT_BITDEPTH:		effectsSetBitDepth((int)lrint(v)); break;
		case VT_SAMPLERATE:		effectsSetSampleRate((int)lrint(v)); break;
		case VT_SPATIAL:		effectsSetSpatial((int)lrint(v)); break;
		case VT_REVERB:			effectsSetReverbMix((int)lrint(v)); break;
		case VT_REVERB_TIME:	effectsSetReverbTime((int)lrint(v * 10.0));
								break;
		case VT_EQ_PRE:			effectsSetEqPreamp((int)lrint(v * 10.0)); break;
		default:
			if(isEqBand(t))
				effectsSetEqBand(t - VT_EQ_BAND0, (int)lrint(v * 10.0));
			break;
	}
}

/* Clamped to the limits, rounded to what the effect keeps. */
static double quantize(const struct target* t, double v)
{
	double f = t->decimals == 0 ? 1.0 : t->decimals == 1 ? 10.0 : 100.0;

	if(v < t->min)
		v = t->min;
	if(v > t->max)
		v = t->max;
	return floor(v * f + 0.5) / f;
}

static void format(enum valueTarget t, double v, char* buf, size_t len)
{
	if(t == VT_SPEED)
		effectsFormatSpeed(buf, len, (float)v);
	else if(t == VT_REVERB_TIME)
		snprintf(buf, len, "%.1fs", v);
	else if(t == VT_BITDEPTH)
		snprintf(buf, len, "%d bit", (int)lrint(v));
	else if(t == VT_SAMPLERATE)
		effectsFormatRate(buf, len, (int)lrint(v));
	else if(isDb(t))
		effectsFormatDb(buf, len, (int)lrint(v * 10.0));
	else
		snprintf(buf, len, "%d%%", (int)lrint(v));
}

void valueEntryOpen(struct valueEntry* s, enum valueTarget what)
{
	memset(s, 0, sizeof(*s));
	s->what = what;
	s->pressedKey = -1;
	s->keyRow = 0;			/* on the digits */
	s->keyCol = 0;
	s->caret = true;
	s->caretMs = osGetTime();
	s->dirty = true;
}

static bool allowDot(const struct valueEntry* s)
{
	return targetOf(s->what)->decimals > 0;
}

/* dB can be negative */
static bool allowMinus(const struct valueEntry* s)
{
	return targetOf(s->what)->min < 0.0;
}

/* "-" or "." alone isn't a number yet. */
static bool hasDigit(const char* text)
{
	for(; *text != '\0'; text++)
		if(*text >= '0' && *text <= '9')
			return true;
	return false;
}

static void draw(struct valueEntry* s)
{
	const struct target*	t = targetOf(s->what);
	struct veView			v;
	struct botCanvas		c;
	struct topColors		col;
	char					now[24], range[40], lo[16], hi[16], res[64];
	char					q[24];

	memset(&v, 0, sizeof(v));
	format(s->what, currentValue(s->what), now, sizeof(now));
	format(s->what, t->min, lo, sizeof(lo));
	format(s->what, t->max, hi, sizeof(hi));
	snprintf(range, sizeof(range), "%s - %s", lo, hi);

	if(hasDigit(s->text))
	{
		double raw = strtod(s->text, NULL);
		double val = quantize(t, raw);

		format(s->what, val, q, sizeof(q));
		snprintf(res, sizeof(res), "sets %s%s", q,
		         raw < t->min ? " (the minimum)" :
		         raw > t->max ? " (the maximum)" : "");
		v.result = res;
	}

	v.name = t->name;
	v.text = s->text;
	v.unit = t->unit;
	v.now = now;
	v.range = range;
	v.allowDot = allowDot(s);
	v.allowMinus = allowMinus(s);
	v.caret = s->caret;
	v.keyRow = s->keyRow;
	v.keyCol = s->keyCol;
	v.pressedKey = s->pressedKey;

	uiBottomCanvas(&c, &col);
	veDraw(&c, &col, &v);
	uiBottomPresent();
	s->dirty = false;
}

void valueEntryUpdate(struct valueEntry* s)
{
	u64 now = osGetTime();

	if(now - s->caretMs >= CARET_MS)
	{
		s->caretMs = now;
		s->caret = !s->caret;
		s->dirty = true;
	}
	if(s->dirty)
		draw(s);
}

/* Set what was typed (nothing typed: nothing changes). */
static enum page_result finish(struct valueEntry* s, bool apply)
{
	if(apply && hasDigit(s->text))
	{
		applyValue(s->what, quantize(targetOf(s->what),
		                             strtod(s->text, NULL)));
		uiRequestRedraw();		/* the top screen shows it */
	}
	return PAGE_TO_MENU;
}

static void pressKey(struct valueEntry* s, int code)
{
	size_t len = strlen(s->text);

	if(code == SS_KEY_DEL)
	{
		if(len == 0)
			return;
		s->text[len - 1] = '\0';
	}
	else if(code == SS_KEY_CLEAR)
	{
		if(len == 0)
			return;
		s->text[0] = '\0';
	}
	else if((code >= '0' && code <= '9') || (code == '.' && allowDot(s)) ||
	        (code == '-' && allowMinus(s) && len == 0))
	{
		if(len >= VALUE_TEXT_MAX)
			return;
		if(code == '.' && strchr(s->text, '.') != NULL)
			return;
		s->text[len] = (char)code;
		s->text[len + 1] = '\0';
	}
	else
		return;		/* letters and the rest do nothing here */
	s->caret = true;
	s->caretMs = osGetTime();
	s->dirty = true;
}

static enum page_result touchInput(struct valueEntry* s, u32 kDown,
                                   u32 kHeld)
{
	touchPosition	tp;
	int				r, c, code;

	if(!(kHeld & KEY_TOUCH))
	{
		if(s->pressedKey >= 0)
		{
			s->pressedKey = -1;
			s->dirty = true;
		}
		return PAGE_STAY;
	}
	hidTouchRead(&tp);
	code = ssKeyAt(tp.px, tp.py, &r, &c);

	if(kDown & KEY_TOUCH)
	{
		if(code < 0)
			return PAGE_STAY;
		s->pressedKey = code;
		s->keyRow = r;
		s->keyCol = c;
		s->dirty = true;
		keyRepeat(&s->keyHold, KEY_TOUCH, KEY_TOUCH, KEY_TOUCH);
		if(code == SS_KEY_DONE)
			return finish(s, true);
		pressKey(s, code);
		return PAGE_STAY;
	}
	if(s->pressedKey >= 0 && code != s->pressedKey)
	{
		s->pressedKey = -1;		/* slid off the key */
		s->dirty = true;
	}
	else if(s->pressedKey >= 0 && s->pressedKey != SS_KEY_DONE &&
	        keyRepeat(&s->keyHold, 0, KEY_TOUCH, KEY_TOUCH))
		pressKey(s, s->pressedKey);
	return PAGE_STAY;
}

enum page_result valueEntryInput(struct valueEntry* s, u32 kDown, u32 kHeld)
{
	u32					dirs = keyRepeat(&s->dirRepeat, kDown, kHeld,
	                                     KEY_UP | KEY_DOWN | KEY_LEFT |
	                                     KEY_RIGHT);
	u32					held = keyRepeat(&s->keyHold, kDown, kHeld,
	                                     KEY_A | KEY_B);
	enum page_result	r;

	if(kDown & KEY_SELECT)
		return finish(s, false);
	if((r = touchInput(s, kDown, kHeld)) != PAGE_STAY)
		return r;

	if(dirs & KEY_UP)
		ssKeyMove(&s->keyRow, &s->keyCol, 0, -1);
	else if(dirs & KEY_DOWN)
		ssKeyMove(&s->keyRow, &s->keyCol, 0, 1);
	else if(dirs & KEY_LEFT)
		ssKeyMove(&s->keyRow, &s->keyCol, -1, 0);
	else if(dirs & KEY_RIGHT)
		ssKeyMove(&s->keyRow, &s->keyCol, 1, 0);
	if(dirs)
		s->dirty = true;

	if(kDown & KEY_Y)
		return finish(s, true);
	if(held & KEY_A)
	{
		int code = ssKeyCode(s->keyRow, s->keyCol);

		if(code == SS_KEY_DONE)
		{
			if(kDown & KEY_A)
				return finish(s, true);
		}
		else
			pressKey(s, code);
	}
	else if(held & KEY_B)
	{
		/* B on an empty field cancels (not on a held repeat) */
		if(s->text[0] == '\0')
		{
			if(kDown & KEY_B)
				return finish(s, false);
		}
		else
			pressKey(s, SS_KEY_DEL);
	}
	return PAGE_STAY;
}

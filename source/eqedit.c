/**
 * ctrmus - 3DS Music Player
 * The equalizer page. See eqedit.h; drawn by bottomscreen.c (eqDraw).
 */

#include <string.h>

#include "bottomscreen.h"
#include "effects.h"
#include "eqedit.h"
#include "ui.h"
#include "valueentry.h"

void eqEditOpen(struct eqEdit* s)
{
	memset(s, 0, sizeof(*s));
	s->focus = 1 + 2;			/* the 125 Hz band: bass is what's used most */
	s->drag = -1;
	s->pressed = -1;
	s->dirty = true;
}

void eqEditResume(struct eqEdit* s)
{
	s->drag = -1;
	s->pressed = -1;
	s->dirty = true;
}

int eqEditValueTarget(const struct eqEdit* s)
{
	return s->focus == 0 ? VT_EQ_PRE : VT_EQ_BAND0 + s->focus - 1;
}

static int tenthsOf(int col)
{
	return col == 0 ? effectsEqPreamp() : effectsEqBand(col - 1);
}

static void setTenths(struct eqEdit* s, int col, int tenths)
{
	if(tenths == tenthsOf(col))
		return;
	if(col == 0)
		effectsSetEqPreamp(tenths);
	else
		effectsSetEqBand(col - 1, tenths);
	s->dirty = true;
	uiRequestRedraw();			/* the top screen shows the preset */
}

/* One whole dB up or down: a typed 2.5 goes to 3.0 (up) or 2.0 (down). */
static int stepTenths(int v, int dir)
{
	int whole = v >= 0 ? v / 10 : -((-v + 9) / 10);		/* floor */

	if(dir > 0)
		return (whole + 1) * 10;
	return (v == whole * 10 ? whole - 1 : whole) * 10;
}

static void changed(struct eqEdit* s)
{
	s->dirty = true;
	uiRequestRedraw();
}

static void stepPreset(struct eqEdit* s, int dir)
{
	int n = effectsEqPresetCount();
	int cur = effectsEqCurrentPreset();
	int next = cur < 0 ? (dir > 0 ? 1 : n - 1) :
	           (cur + dir + n) % n;

	/* "custom" goes to the first real preset one way, the last the other
	 * (flat is the Flat button's job) */
	effectsEqApplyPreset(next);
	changed(s);
}

static void press(struct eqEdit* s, int button)
{
	switch(button)
	{
		case EQ_FOCUS_PRESET:	stepPreset(s, 1); break;
		case EQ_FOCUS_AUTO:		effectsEqAutoPreamp(); changed(s); break;
		case EQ_FOCUS_FLAT:		effectsEqReset(); changed(s); break;
		default:				break;
	}
}

static void viewOf(const struct eqEdit* s, struct eqView* v)
{
	memset(v, 0, sizeof(*v));
	v->focus = s->focus;
	v->pre = effectsEqPreamp();
	for(int i = 0; i < EQ_BANDS; i++)
	{
		v->band[i] = effectsEqBand(i);
		v->freq[i] = effectsEqFreq(i);
	}
	v->bandMax = EFFECTS_EQ_BAND_MAX;
	v->preMin = EFFECTS_EQ_PREAMP_MIN;
	v->netPeak = effectsEqPeak() + v->pre;
	v->preset = effectsEqPresetName(effectsEqCurrentPreset());
	v->pressed = s->pressed;
}

static void draw(struct eqEdit* s)
{
	struct eqView		v;
	struct botCanvas	c;
	struct topColors	col;

	viewOf(s, &v);
	uiBottomCanvas(&c, &col);
	eqDraw(&c, &col, &v);
	uiBottomPresent();
	s->dirty = false;
}

void eqEditUpdate(struct eqEdit* s)
{
	if(s->dirty)
		draw(s);
}

static void touchInput(struct eqEdit* s, u32 kDown, u32 kHeld)
{
	touchPosition	tp;
	struct eqView	v;

	if(!(kHeld & KEY_TOUCH))
	{
		if(s->drag >= 0 || s->pressed >= 0)
		{
			s->drag = -1;
			s->pressed = -1;
			s->dirty = true;
		}
		return;
	}
	hidTouchRead(&tp);

	if(kDown & KEY_TOUCH)
	{
		int col = eqColumnAt(tp.px, tp.py);
		int btn = eqButtonAt(tp.px, tp.py);

		if(btn >= 0)
		{
			s->pressed = btn;
			s->focus = btn;
			s->dirty = true;
			press(s, btn);
			return;
		}
		if(col < 0)
			return;
		s->drag = col;
		s->focus = col;
		s->dirty = true;
	}
	if(s->drag >= 0)
	{
		/* The column stays picked while the stylus slides about; its
		 * height sets the value (whole dB). */
		viewOf(s, &v);
		setTenths(s, s->drag, eqValueAt(&v, s->drag, tp.py));
	}
}

enum page_result eqEditInput(struct eqEdit* s, u32 kDown, u32 kHeld)
{
	u32 dirs = keyRepeat(&s->dirRepeat, kDown, kHeld,
	                     KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);

	if(kDown & (KEY_B | KEY_SELECT))
		return PAGE_TO_MENU;

	touchInput(s, kDown, kHeld);

	if(dirs & KEY_LEFT)
		s->focus = (s->focus + EQ_FOCUS_COUNT - 1) % EQ_FOCUS_COUNT;
	else if(dirs & KEY_RIGHT)
		s->focus = (s->focus + 1) % EQ_FOCUS_COUNT;
	else if(dirs & (KEY_UP | KEY_DOWN))
	{
		int dir = (dirs & KEY_UP) ? 1 : -1;

		if(s->focus < EQ_COLUMNS)
			setTenths(s, s->focus,
			          stepTenths(tenthsOf(s->focus), dir));
		else if(s->focus == EQ_FOCUS_PRESET)
			stepPreset(s, dir);
	}
	if(dirs & (KEY_LEFT | KEY_RIGHT))
		s->dirty = true;

	if(kDown & KEY_A)
	{
		if(s->focus < EQ_COLUMNS)
			setTenths(s, s->focus, 0);
		else
			press(s, s->focus);
	}
	if((kDown & KEY_Y) && s->focus < EQ_COLUMNS)
		return PAGE_VALUE;
	return PAGE_STAY;
}

/**
 * ctrmus - 3DS Music Player
 * Screen brightness. See brightness.h.
 */

#include <3ds.h>

#include "brightness.h"

#define LEVEL_MIN		1
#define LEVEL_MAX		5
/* Used as the starting point if the system's level can't be read. */
#define LEVEL_DEFAULT	3

/* Config block with the HOME Menu backlight settings; byte 1 is the
 * brightness level (1-5). Only trusted if it is in range. */
#define CFG_BACKLIGHT_BLOCK	0x00050001

static bool	available = false;
static int	level = -1;		/* current level, -1 = unknown */
static int	original = -1;	/* level at startup, restored on exit */

/*
 * Raw backlight values that gsp::Lcd GetBrightness reports on New 3DS for
 * levels 1-5 (as reported by 3DSident users). Auto-brightness and power
 * saving give values in between, and calibration differs a little between
 * consoles, so a raw value is mapped to the nearest level.
 */
static const u32 rawLevels[LEVEL_MAX] = { 16, 28, 48, 82, 142 };

/* Nearest level to a raw value (by ratio, as the steps are geometric). */
static int levelFromRaw(u32 raw)
{
	int best = LEVEL_MIN;

	for(int i = 1; i < LEVEL_MAX; i++)
		/* Past the geometric midpoint between level i and i+1? */
		if((u64)raw * raw >= (u64)rawLevels[i - 1] * rawLevels[i])
			best = i + 1;
	return best;
}

static bool rawIsExactLevel(u32 raw)
{
	for(int i = 0; i < LEVEL_MAX; i++)
		if(raw == rawLevels[i])
			return true;
	return false;
}

/* The HOME Menu brightness setting from the config savegame, or -1. */
static int levelFromConfig(void)
{
	u8	blk[2] = { 0, 0 };
	int	found = -1;

	if(R_FAILED(cfguInit()))
		return -1;

	/* cfg:u only allows some blocks; cfguInit() gets cfg:i or cfg:s when
	 * it can, and those can read the rest with GetConfigInfoBlk8. */
	if((R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof(blk), CFG_BACKLIGHT_BLOCK,
	                                       blk)) &&
	    blk[1] >= LEVEL_MIN && blk[1] <= LEVEL_MAX) ||
	   (R_SUCCEEDED(CFG_GetConfigInfoBlk8(sizeof(blk), CFG_BACKLIGHT_BLOCK,
	                                      blk)) &&
	    blk[1] >= LEVEL_MIN && blk[1] <= LEVEL_MAX))
		found = blk[1];

	cfguExit();
	return found;
}

void brightnessInit(void)
{
	u32	raw = 0;
	int	cfg;

	available = R_SUCCEEDED(gspLcdInit());
	if(!available)
		return;

	/*
	 * Best: the live backlight value, when it is exactly one of the five
	 * levels. Next: the HOME Menu setting (GetBrightness is stubbed on Old
	 * 3DS). Last: the live value rounded to the nearest level (auto-
	 * brightness / power saving). If nothing works the level stays unknown
	 * until it is first changed.
	 */
	if(R_FAILED(GSPLCD_GetBrightness(GSPLCD_SCREEN_TOP, &raw)))
		raw = 0;

	if(raw != 0 && rawIsExactLevel(raw))
		level = levelFromRaw(raw);
	else if((cfg = levelFromConfig()) > 0)
		level = cfg;
	else if(raw != 0)
		level = levelFromRaw(raw);

	original = level;
}

void brightnessExit(void)
{
	if(!available)
		return;

	if(original >= 0 && level != original)
		GSPLCD_SetBrightness(GSPLCD_SCREEN_BOTH, (u32)original);
	gspLcdExit();
	available = false;
}

int brightnessSetLevel(int next)
{
	if(!available)
		return -1;

	if(next < LEVEL_MIN)
		next = LEVEL_MIN;
	if(next > LEVEL_MAX)
		next = LEVEL_MAX;

	if(next != level &&
	   R_FAILED(GSPLCD_SetBrightness(GSPLCD_SCREEN_BOTH, (u32)next)))
		return -1;

	level = next;
	return brightnessPercent();
}

int brightnessStep(int delta)
{
	return brightnessSetLevel((level < 0 ? LEVEL_DEFAULT : level) + delta);
}

int brightnessLevel(void)
{
	return level;
}

int brightnessPercent(void)
{
	return level < 0 ? -1 : level * 100 / LEVEL_MAX;
}

/**
 * ctrmus - 3DS Music Player
 * App settings. See settings.h.
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "effects.h"
#include "paths.h"
#include "playback.h"
#include "saver.h"
#include "settings.h"

#define SETTINGS_DIR	PAPAYA_DIR
#define SETTINGS_PATH	SETTINGS_DIR "/settings.cfg"

static const int seekSteps[SEEK_STEP_COUNT] = { 5, 10, 15, 30, 60 };

static bool	isNew3ds = false;
static bool	unsaved = false;

static bool						fastMode = false;
static enum filename_artists	filenameArtists = ARTISTS_SHOW;
static bool						timeRemaining = false;
static bool						placeholderArt = false;
static int						seekStepIdx = 2;	/* 15 s */
static bool						listTitles = true;
static bool						performance = false;
static bool						spinningDisc = false;
static int						discSpeedIdx = 2;	/* 33 1/3 rpm */

/* Record speeds: what the file says, ms per turn, and the label. */
static const int discRpm[DISC_SPEED_COUNT] = { 8, 16, 33, 45, 78 };
static const int discTurnMs[DISC_SPEED_COUNT] = { 7200, 3600, 1800, 1333,
                                                  769 };
static const char* const discLabel[DISC_SPEED_COUNT] =
	{ "8 rpm", "16 rpm", "33 rpm", "45 rpm", "78 rpm" };

static void applyFastMode(void)
{
	if(isNew3ds)
		osSetSpeedupEnable(fastMode);
}

/* Copy one file if the destination doesn't exist yet. */
static void copyOnce(const char* from, const char* to)
{
	FILE*	in;
	FILE*	out;
	char	buf[1024];
	size_t	n;

	if((out = fopen(to, "r")) != NULL)
	{
		fclose(out);
		return;
	}
	if((in = fopen(from, "r")) == NULL)
		return;
	if((out = fopen(to, "w")) == NULL)
	{
		fclose(in);
		return;
	}
	while((n = fread(buf, 1, sizeof(buf), in)) > 0)
		fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
}

void settingsMigrateOld(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(PAPAYA_DIR, 0777);
	mkdir(PAPAYA_DIR "/themes", 0777);
	copyOnce(OLD_CTRMUS_DIR "/settings.cfg", PAPAYA_DIR "/settings.cfg");
	copyOnce(OLD_CTRMUS_DIR "/themes/theme.cfg",
	         PAPAYA_DIR "/themes/theme.cfg");
}

void settingsInit(void)
{
	FILE*	f;
	char	line[128];

	APT_CheckNew3DS(&isNew3ds);

	if((f = fopen(SETTINGS_PATH, "r")) != NULL)
	{
		while(fgets(line, sizeof(line), f) != NULL)
		{
			int v;

			if(line[0] == '#')
				continue;
			if(sscanf(line, " fast_mode = %d", &v) == 1)
				fastMode = v != 0;
			else if(sscanf(line, " filename_artists = %d", &v) == 1 &&
			        v >= 0 && v < ARTISTS_COUNT)
				filenameArtists = (enum filename_artists)v;
			else if(sscanf(line, " time_remaining = %d", &v) == 1)
				timeRemaining = v != 0;
			else if(sscanf(line, " placeholder_art = %d", &v) == 1)
				placeholderArt = v != 0;
			else if(sscanf(line, " list_titles = %d", &v) == 1)
				listTitles = v != 0;
			else if(sscanf(line, " performance_mode = %d", &v) == 1)
				performance = v != 0;
			else if(sscanf(line, " spinning_disc = %d", &v) == 1)
				spinningDisc = v != 0;
			else if(sscanf(line, " disc_speed = %d", &v) == 1)
			{
				for(int i = 0; i < DISC_SPEED_COUNT; i++)
					if(discRpm[i] == v)
						discSpeedIdx = i;
			}
			else if(sscanf(line, " seek_step = %d", &v) == 1)
			{
				for(int i = 0; i < SEEK_STEP_COUNT; i++)
					if(seekSteps[i] == v)
						seekStepIdx = i;
			}
		}
		fclose(f);
	}

	applyFastMode();
	effectsSetReverbAllowed(!performance);
}

/* Written by saver.c in the background: the SD card write used to run
 * on the UI thread and could freeze the app for seconds. */
void settingsSaveIfChanged(void)
{
	static const char* const dirs[] = { "sdmc:/3ds", SETTINGS_DIR, NULL };
	char	text[2048];
	int		n;

	if(!unsaved)
		return;

	n = snprintf(text, sizeof(text),
	        "# Papaya settings (changed from the menu, SELECT)\n"
	        "# fast_mode: New 3DS 804 MHz mode (1 = on). Uses more "
	        "battery.\n"
	        "fast_mode = %d\n"
	        "# filename_artists: \"A, B - Song.mp3\" in the lists: "
	        "0 as is, 1 first artist, 2 no artists\n"
	        "filename_artists = %d\n"
	        "# time_remaining: footer counts down (1 = on)\n"
	        "time_remaining = %d\n"
	        "# placeholder_art: never load cover art (1 = on)\n"
	        "placeholder_art = %d\n"
	        "# seek_step: ZL/ZR in seconds (5, 10, 15, 30 or 60)\n"
	        "seek_step = %d\n"
	        "# list_titles: tag titles in the top screen's queue/playlist "
	        "(0 = filenames)\n"
	        "list_titles = %d\n"
	        "# performance_mode: turns off list titles, cover art, "
	        "reverb,\n"
	        "# folder tags and title scrolling, and redraws the top "
	        "screen less\n"
	        "performance_mode = %d\n"
	        "# spinning_disc: round cover art that spins while playing "
	        "(1 = on)\n"
	        "spinning_disc = %d\n"
	        "# disc_speed: rpm (8, 16, 33, 45 or 78)\n"
	        "disc_speed = %d\n",
	        fastMode ? 1 : 0, (int)filenameArtists,
	        timeRemaining ? 1 : 0, placeholderArt ? 1 : 0,
	        seekSteps[seekStepIdx], listTitles ? 1 : 0,
	        performance ? 1 : 0, spinningDisc ? 1 : 0,
	        discRpm[discSpeedIdx]);
	if(n > 0 && n < (int)sizeof(text))
		saverWrite(SETTINGS_PATH, dirs, text, (size_t)n);
	unsaved = false;
}

void settingsExit(void)
{
	settingsSaveIfChanged();
	if(isNew3ds && fastMode)
		osSetSpeedupEnable(false);
}

/* ------------------------------------------------------------------ */

bool settingsFastModeAvailable(void)
{
	return isNew3ds;
}

bool settingsFastMode(void)
{
	return isNew3ds && fastMode;
}

void settingsSetFastMode(bool on)
{
	if(!isNew3ds || on == fastMode)
		return;
	fastMode = on;
	unsaved = true;
	applyFastMode();
}

enum filename_artists settingsFilenameArtists(void)
{
	return filenameArtists;
}

void settingsSetFilenameArtists(enum filename_artists mode)
{
	if((unsigned)mode >= ARTISTS_COUNT || mode == filenameArtists)
		return;
	filenameArtists = mode;
	unsaved = true;
}

const char* filenameArtistsStr(enum filename_artists mode)
{
	switch(mode)
	{
		case ARTISTS_FIRST:	return "first only";
		case ARTISTS_HIDE:	return "hidden";
		default:			return "shown";
	}
}

const char* settingsShortName(const char* name, char* buf, size_t len)
{
	const char* dash;
	const char* comma;

	if(filenameArtists == ARTISTS_SHOW ||
	   (dash = strstr(name, " - ")) == NULL || dash == name ||
	   dash[3] == '\0')
		return name;

	if(filenameArtists == ARTISTS_HIDE)
		return dash + 3;

	/* First artist only */
	comma = strstr(name, ", ");
	if(comma == NULL || comma > dash)
		return name;
	snprintf(buf, len, "%.*s%s", (int)(comma - name), name, dash);
	return buf;
}

bool settingsTimeRemaining(void)
{
	return timeRemaining;
}

void settingsSetTimeRemaining(bool on)
{
	if(on == timeRemaining)
		return;
	timeRemaining = on;
	unsaved = true;
}

bool settingsPlaceholderArt(void)
{
	return placeholderArt || performance;
}

void settingsSetPlaceholderArt(bool on)
{
	if(on == placeholderArt)
		return;
	placeholderArt = on;
	unsaved = true;
}

int settingsSeekStep(void)
{
	return seekSteps[seekStepIdx];
}

int settingsSeekStepIndex(void)
{
	return seekStepIdx;
}

void settingsSetSeekStepIndex(int idx)
{
	if(idx < 0 || idx >= SEEK_STEP_COUNT || idx == seekStepIdx)
		return;
	seekStepIdx = idx;
	unsaved = true;
}

int seekStepSeconds(int idx)
{
	return (idx >= 0 && idx < SEEK_STEP_COUNT) ? seekSteps[idx] : 0;
}

bool settingsListTitles(void)
{
	return listTitles && !performance;
}

void settingsSetListTitles(bool on)
{
	if(on == listTitles)
		return;
	listTitles = on;
	unsaved = true;
}

bool settingsPerformanceMode(void)
{
	return performance;
}

void settingsSetPerformanceMode(bool on)
{
	if(on == performance)
		return;
	performance = on;
	unsaved = true;
	effectsSetReverbAllowed(!performance);
}

bool settingsSpinningDisc(void)
{
	return spinningDisc && !performance;
}

void settingsSetSpinningDisc(bool on)
{
	if(on == spinningDisc)
		return;
	spinningDisc = on;
	unsaved = true;
}

int settingsDiscSpeedIndex(void)
{
	return discSpeedIdx;
}

void settingsSetDiscSpeedIndex(int idx)
{
	if(idx < 0 || idx >= DISC_SPEED_COUNT || idx == discSpeedIdx)
		return;
	discSpeedIdx = idx;
	unsaved = true;
}

int settingsDiscTurnMs(void)
{
	return discTurnMs[discSpeedIdx];
}

const char* discSpeedStr(int idx)
{
	return (idx >= 0 && idx < DISC_SPEED_COUNT) ? discLabel[idx] : "";
}

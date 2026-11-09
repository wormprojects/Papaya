/**
 * ctrmus - 3DS Music Player
 * App settings kept in sdmc:/3ds/papaya/settings.cfg (`key = value`).
 * Themes have their own file (themes.h).
 *
 * Saved (preferences): fast_mode, filename_artists,
 * time_remaining, placeholder_art, seek_step, list_titles,
 * performance_mode, spinning_disc, disc_speed. Not saved (playback state,
 * reset every start): speed, distortion, reverb, shuffle, repeat. Brightness is the system's
 * own and is put back on exit.
 *
 *   fast_mode = 0|1		New 3DS fast mode (804 MHz + L2 cache). Off by
 *						default: it costs battery. Ignored on Old 3DS.
 *   filename_artists = 0|1|2
 *						How "Artist A, Artist B - Song.mp3" filenames show
 *						(display only, files aren't touched): 0 as is,
 *						1 first artist only, 2 no artists.
 *   time_remaining = 0|1	Footer counts down ("-0:12:34").
 *   placeholder_art = 0|1	Never load cover art (vinyl placeholder only).
 *   seek_step = 5|10|15|30|60	ZL/ZR jump in seconds.
 *   list_titles = 0|1	Tag titles in the top screen's queue/playlist
 *						(read for the visible rows only); 0: filenames.
 *   spinning_disc = 0|1	Round cover art that spins while playing.
 *   disc_speed = 8|16|33|45|78	How fast it turns, in rpm (display only).
 *   performance_mode = 0|1	Forces list titles off, placeholder art,
 *						reverb and the spinning disc off, and no tag
 *						loading in folders;
 *						also no title scrolling and fewer top-screen
 *						redraws (ui.c). The forced settings keep their
 *						own values for when it's turned off again.
 *
 * The getters return the effective value (performance mode applied);
 * the setters change the setting's own value.
 */

#ifndef ctrmus_settings_h
#define ctrmus_settings_h

#include <stdbool.h>
#include <stddef.h>

/** Load the settings file and apply it. */
/**
 * First run after the rename from ctrmus: copy the old settings and theme
 * to the Papaya folder (only what isn't there yet). Call before
 * settingsInit() and loadTheme().
 */
void settingsMigrateOld(void);

void settingsInit(void);

/** Save if anything changed, and undo what was applied (fast mode). */
void settingsExit(void);

/** Save the settings file if anything changed since it was last saved. */
void settingsSaveIfChanged(void);

/** Whether fast mode exists on this console (New 3DS only). */
bool settingsFastModeAvailable(void);
bool settingsFastMode(void);
/** Turn fast mode on/off now (saved later, see settingsSaveIfChanged). */
void settingsSetFastMode(bool on);

enum filename_artists
{
	ARTISTS_SHOW,		/* "Artist A, Artist B - Song.mp3" */
	ARTISTS_FIRST,		/* "Artist A - Song.mp3" */
	ARTISTS_HIDE,		/* "Song.mp3" */
	ARTISTS_COUNT
};

enum filename_artists settingsFilenameArtists(void);
void settingsSetFilenameArtists(enum filename_artists mode);
const char* filenameArtistsStr(enum filename_artists mode);

/**
 * A filename as it should be shown, per filename_artists: "Artist A,
 * Artist B - Song.mp3" -> "Artist A - Song.mp3" or "Song.mp3". Only the
 * first " - " counts; artists split at ", " only (so "Simon & Garfunkel"
 * stays whole). Only call it for filenames, not tag titles.
 *
 * \return	name itself, a pointer into it, or buf.
 */
const char* settingsShortName(const char* name, char* buf, size_t len);


bool settingsTimeRemaining(void);
void settingsSetTimeRemaining(bool on);

bool settingsPlaceholderArt(void);
void settingsSetPlaceholderArt(bool on);

/** Tag titles (true) or filenames in the top screen's side lists. */
bool settingsListTitles(void);
void settingsSetListTitles(bool on);

/** Round, spinning cover art (off while performance mode is on). */
bool settingsSpinningDisc(void);
void settingsSetSpinningDisc(bool on);

/* Disc speed: record speeds, 8 1/3 to 78 rpm (index 2 = 33 1/3). */
#define DISC_SPEED_COUNT 5
int settingsDiscSpeedIndex(void);
void settingsSetDiscSpeedIndex(int idx);
/** Milliseconds per turn at the current disc speed. */
int settingsDiscTurnMs(void);
/** "33 rpm" etc. for speed `idx`. */
const char* discSpeedStr(int idx);

/** Performance mode (see above). The caller handles
 * the cover art (uiArtSettingChanged) and redraws. */
bool settingsPerformanceMode(void);
void settingsSetPerformanceMode(bool on);

#define SEEK_STEP_COUNT 5
/** ZL/ZR seek step in seconds. */
int settingsSeekStep(void);
/** The step as an index into the choices (0..SEEK_STEP_COUNT-1). */
int settingsSeekStepIndex(void);
void settingsSetSeekStepIndex(int idx);
int seekStepSeconds(int idx);

#endif

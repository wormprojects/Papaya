/**
 * ctrmus - 3DS Music Player
 * Screen layout, splash and Now Playing screen. See ui.h.
 *
 * UI PROTOTYPE: this file gathers the player state into a struct npView
 * and has topscreen.c render it. Cover art is loaded in the background by
 * coverart.c each time a song starts.
 *
 * Redraws: anything that changes asks for one (uiRequestRedraw), and the
 * whole screen is also redrawn every REFRESH_MS for the time, clock and
 * battery. In performance mode only the footer is redrawn when the time
 * shown changes, the whole screen every PERF_REFRESH_MS, titles don't
 * scroll and the volume slider is read less often.
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "bottomscreen.h"
#include "brightness.h"
#include "browser.h"
#include "coverart.h"
#include "effects.h"
#include "player.h"
#include "playback.h"
#include "queue.h"
#include "settings.h"
#include "titles.h"
#include "topscreen.h"
#include "ui.h"
#include "version.h"

static uint16_t			topPixels[TOP_W * TOP_H];
static struct topCanvas	canvas;
static struct topColors	colors;
static bool				initialised = false;
static bool				mcuOk = false;
/* What the top screen shows; nothing is drawn before the splash. */
static enum { TOP_NONE, TOP_SPLASH, TOP_NOW_PLAYING } topMode = TOP_NONE;
/* The top screen is double-buffered: after a redraw the canvas has to be
 * copied into both framebuffers, i.e. on the next two frames. */
static int				dirtyFrames = 0;
/* Columns to copy while dirtyFrames > 0 (the canvas is column-major, so
 * a column range is one contiguous block). */
static int				dirtyX0 = 0, dirtyX1 = 0;
/* Cover art generation (topscreen's disc frames follow it) */
static unsigned			artId = 0;
/* Spinning disc: its position in 1/1000 steps (so a speed change only
 * changes how fast it turns, not where it is), and its step now */
static u64				discPos = 0;
static int				discStep = 0;
static struct browser*	browser = NULL;
/* Bottom-screen pages (queue editor, search) draw here; uiBottomPresent()
 * copies it over the console's framebuffer. */
static uint16_t			botPixels[BOT_W * BOT_H];
static PrintConsole*	bottomConsole = NULL;

#define REFRESH_MS				500
#define PERF_REFRESH_MS			5000
#define VOLUME_POLL_MS			50
#define PERF_VOLUME_POLL_MS		250

/* Volume slider (0-100%), polled by uiPresentTop(); -1 until read. */
static int				volumePct = -1;
/* When the whole top screen was last drawn. */
static u64				lastRenderMs = 0;

/* Details of the current song, worked out once when it changes. */
static unsigned		songId = 0;			/* playerSongId() of the info */
static u64			songStartMs = 0;	/* for the title scrolling */
static char			songPath[PATH_MAX];	/* absolute */
static long long	songSize = -1;
static char			songFormat[8];

/* Cover art of the current song (from coverart.c). */
_Static_assert(COVER_ART_SIZE == TOP_ART_SIZE, "art size mismatch");
static uint16_t		artPixels[TOP_ART_SIZE * TOP_ART_SIZE];
static int			artW, artH;
static enum { ART_NONE, ART_LOADING, ART_READY } artState = ART_NONE;

/* Labels of the visible queue/playlist rows (see rowLabel()). */
static char			queueLabels[NP_MAX_LIST][128];
static char			listLabels[NP_MAX_LIST][128];
static char			rowPath[PATH_MAX];

/* Columns [x0, x1) changed: copy them into both framebuffers. */
static void markDirty(int x0, int x1)
{
	if(dirtyFrames <= 0)
	{
		dirtyX0 = x0;
		dirtyX1 = x1;
	}
	else
	{
		if(x0 < dirtyX0)
			dirtyX0 = x0;
		if(x1 > dirtyX1)
			dirtyX1 = x1;
	}
	dirtyFrames = 2;
}

static void markAllDirty(void)
{
	markDirty(0, TOP_W);
}

static uint16_t rgb565(const struct themeColor* c)
{
	return (uint16_t)(((c->r >> 3) << 11) | ((c->g >> 2) << 5) |
	                  (c->b >> 3));
}

static const char* baseName(const char* path)
{
	const char* slash = strrchr(path, '/');

	return slash != NULL ? slash + 1 : path;
}

/* Load the current song's cover art (not cached: read again every time a
 * song starts), unless the "placeholder only" setting is on. */
static void requestArt(void)
{
	if(settingsPlaceholderArt())
	{
		artState = ART_NONE;
		return;
	}
	artState = ART_LOADING;
	coverArtRequest(songPath, colors.bg);
}

/*
 * What a queue/playlist row shows: the title the browser already knew
 * (folders that load tags), else the title tag read in the background for
 * just the visible rows (titles.h), else the filename shortened per the
 * "artists in filenames" setting. With "titles in lists" off (or
 * performance mode on), always the filename.
 */
static const char* rowLabel(const char* path, const char* known,
                            const char* file, char* buf, size_t len)
{
	if(!settingsListTitles())
		return settingsShortName(file, buf, len);
	if(strcmp(known, file) != 0)
		return known;
	if(path != NULL && titlesLookup(path, buf, len) == 1)
		return buf;
	return settingsShortName(file, buf, len);
}

/*
 * Absolute path, size, format and cover art of a newly started song.
 * Runs on the first redraw after it started (playerPlay asks for one).
 * Songs are always started by absolute path; the cwd fallback is
 * only for safety.
 */
static void updateSongInfo(const char* nowFile)
{
	struct stat	st;
	const char*	dot;
	size_t		i;

	if(playerSongId() == songId)
		return;
	songId = playerSongId();
	songStartMs = osGetTime();

	/* Songs played from the browser are relative to the current
	 * directory, which is still the song's folder at this point. */
	songPath[0] = '\0';
	if(strchr(nowFile, ':') == NULL && nowFile[0] != '/')
	{
		char cwd[PATH_MAX];

		if(getcwd(cwd, sizeof(cwd)) != NULL &&
		   snprintf(songPath, sizeof(songPath), "%s%s%s", cwd,
		            cwd[strlen(cwd) - 1] == '/' ? "" : "/",
		            nowFile) >= (int)sizeof(songPath))
			songPath[0] = '\0';
	}
	if(songPath[0] == '\0')
		snprintf(songPath, sizeof(songPath), "%s", nowFile);

	songSize = stat(songPath, &st) == 0 ? (long long)st.st_size : -1;

	requestArt();

	songFormat[0] = '\0';
	dot = strrchr(baseName(nowFile), '.');
	if(dot != NULL)
	{
		for(i = 0; dot[i + 1] != '\0' && i < sizeof(songFormat) - 1; i++)
		{
			char ch = dot[i + 1];
			songFormat[i] = (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch;
		}
		songFormat[i] = '\0';
	}
}

static void buildView(struct npView* v)
{
	static char subtitle[300];
	const struct playbackInfo_t*	pi = playerInfo();
	const struct queue_t*			q = playerQueue();
	const struct music_metadata*	meta = playerNowPlayingMeta();
	const char*						nowFile = playerNowPlayingFile();
	time_t							now = time(NULL);
	struct tm*						tm = localtime(&now);

	memset(v, 0, sizeof(*v));

	/* Header */
	v->brightnessPct = brightnessPercent();
	if(tm != NULL)
	{
		char day[4], mon[4];
		int h = tm->tm_hour % 12;

		strftime(day, sizeof(day), "%a", tm);
		strftime(mon, sizeof(mon), "%b", tm);
		snprintf(v->clock, sizeof(v->clock), "%s %s %d %d:%02d%s", day,
		         mon, tm->tm_mday, h == 0 ? 12 : h, tm->tm_min,
		         tm->tm_hour >= 12 ? "pm" : "am");
	}
	v->batteryPct = -1;
	if(mcuOk)
	{
		u8 level = 0;

		if(R_SUCCEEDED(MCUHWC_GetBatteryLevel(&level)))
			v->batteryPct = level;
	}

	/* Queue */
	v->queueCount = q->count;
	v->queueShown = q->count < NP_MAX_LIST ? q->count : NP_MAX_LIST;
	for(int i = 0; i < v->queueShown; i++)
		v->queue[i] = queueGet(q, i)->title;

	/* Centre + details */
	v->playing = nowFile != NULL;
	if(v->playing)
	{
		updateSongInfo(nowFile);

		v->paused = isPaused();
		v->artLoading = artState == ART_LOADING;
		v->artId = artId;
		v->disc = settingsSpinningDisc();
		v->discStep = discStep;
		if(artState == ART_READY)
		{
			v->artPixels = artPixels;
			v->artW = artW;
			v->artH = artH;
		}
		v->title = meta->has_title ? meta->title : baseName(nowFile);

		if(meta->has_artist && meta->has_album)
			snprintf(subtitle, sizeof(subtitle), "%s - %s", meta->artist,
			         meta->album);
		else
			snprintf(subtitle, sizeof(subtitle), "%s",
			         meta->has_artist ? meta->artist :
			         meta->has_album ? meta->album : "");
		v->subtitle = subtitle;
		v->scrollMs = (unsigned long)(osGetTime() - songStartMs);
		v->noScroll = settingsPerformanceMode();

		v->tagTitle = meta->has_title ? meta->title : NULL;
		v->tagArtist = meta->has_artist ? meta->artist : NULL;
		v->tagAlbum = meta->has_album ? meta->album : NULL;
		snprintf(v->format, sizeof(v->format), "%s", songFormat);
	}

	/* Playlist: the playing song's folder, in the order it will play. */
	v->playlistCurrent = -1;
	if(browser != NULL)
	{
		bool		firstIsCurrent;
		const char*	files[NP_MAX_LIST];
		const char*	dir = NULL;
		int			queueRows, listRows;

		v->playlistShown = playerUpcoming(browser, v->playlist, files,
		                                  NP_MAX_LIST, &firstIsCurrent,
		                                  &v->playlistCount, &dir);
		if(firstIsCurrent)
			v->playlistCurrent = 0;

		/* Real titles, but only for the rows that will be drawn. */
		topListRows(v, &queueRows, &listRows);
		for(int i = 0; i < queueRows; i++)
		{
			const struct queue_entry* e = queueGet(q, i);

			v->queue[i] = rowLabel(e->filepath, e->title,
			                       baseName(e->filepath), queueLabels[i],
			                       sizeof(queueLabels[i]));
		}
		for(int i = 0; i < listRows; i++)
		{
			const char* path = NULL;

			if(i == 0 && firstIsCurrent && meta->has_title &&
			   settingsListTitles())
			{
				v->playlist[0] = meta->title;	/* playing: already read */
				continue;
			}
			if(dir != NULL && dir[0] != '\0' &&
			   snprintf(rowPath, sizeof(rowPath), "%s%s%s", dir,
			            dir[strlen(dir) - 1] == '/' ? "" : "/", files[i]) <
			   (int)sizeof(rowPath))
				path = rowPath;
			v->playlist[i] = rowLabel(path, v->playlist[i], files[i],
			                          listLabels[i], sizeof(listLabels[i]));
		}
	}

	v->volumePct = volumePct;

	/* Times, size, bitrate */
	if(v->playing && pi->samples_per_second > 0)
	{
		v->elapsed = (long)(pi->samples_played / pi->samples_per_second);
		v->total = (long)(pi->samples_total / pi->samples_per_second);
	}
	if(v->playing && songSize >= 0)
	{
		if(songSize >= 1024 * 1024)
			snprintf(v->size, sizeof(v->size), "%.2fmb",
			         (double)songSize / (1024.0 * 1024.0));
		else
			snprintf(v->size, sizeof(v->size), "%lldkb",
			         (songSize + 1023) / 1024);

		/* Average bitrate = size / duration (works for every format) */
		if(v->total > 0)
			snprintf(v->bitrate, sizeof(v->bitrate), "%ldk",
			         (long)(songSize * 8 / v->total / 1000));
	}

	/* Effects */
	v->speed = getPlaybackSpeed();
	v->distortionPct = effectsDistortion();
	v->reverbMixPct = effectsReverbActiveMix();
	v->reverbDecay = effectsReverbTime() / 10.0f;

	/* Footer */
	switch(playerRepeat())
	{
	case REPEAT_ALL:	v->repeat = NP_REPEAT_ALL; break;
	case REPEAT_ONE:	v->repeat = NP_REPEAT_ONE; break;
	default:			v->repeat = NP_REPEAT_OFF; break;
	}
	v->shuffle = playerShuffle();
	v->remaining = settingsTimeRemaining();
}

/* Last rendered view (uiPresentTop() re-uses it for the scrolling). */
static struct npView view;
/* A redraw was asked for; uiPresentTop() does it once per frame, so
 * several changes in one frame cost one redraw. */
static bool renderPending = false;

static void renderNowPlaying(void)
{
	topMode = TOP_NOW_PLAYING;
	renderPending = true;
}

static void renderNow(void)
{
	buildView(&view);
	topDrawNowPlaying(&canvas, &colors, &view);
	renderPending = false;
	lastRenderMs = osGetTime();
	markAllDirty();
}

void uiSetBrowser(struct browser* b)
{
	browser = b;
}

void uiInit(struct screens* s, const struct theme_t* theme,
            bool mcuAvailable)
{
	consoleInit(GFX_BOTTOM, &s->bottom);

	/* Top screen: our own RGB565 canvas, double-buffered (no tearing). */
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	canvas.px = topPixels;
	canvas.font = consoleGetDefault()->font.gfx;
	bottomConsole = &s->bottom;

	mcuOk = mcuAvailable;
	initialised = true;

	uiApplyTheme(s, theme);
	consoleSelect(&s->bottom);
}

void uiApplyTheme(struct screens* s, const struct theme_t* theme)
{
	applyTheme(&s->bottom, theme);

	colors.bg = rgb565(&theme->bg);
	colors.text = rgb565(&theme->textActive);
	colors.dim = rgb565(&theme->textInactive);
	colors.bar = rgb565(&theme->progressBar);
	colors.accent = rgb565(&theme->highlight);

	if(topMode == TOP_SPLASH)
		uiShowSplash(s);
	else if(topMode == TOP_NOW_PLAYING)
		renderNowPlaying();
}

void uiRequestRedraw(void)
{
	if(initialised && topMode == TOP_NOW_PLAYING)
		renderPending = true;
}

void uiBrightnessStep(int delta)
{
	brightnessStep(delta);
	uiRequestRedraw();
}

/* Read the volume slider; true if it moved. HID:USER's value is 0-63. */
static bool pollVolume(void)
{
	static u64	lastMs = 0;
	u8			raw;
	int			pct;

	if(osGetTime() - lastMs < (settingsPerformanceMode() ?
	                           PERF_VOLUME_POLL_MS : VOLUME_POLL_MS))
		return false;
	lastMs = osGetTime();

	if(R_FAILED(HIDUSER_GetSoundVolume(&raw)))
		return false;
	pct = (raw * 100 + 31) / 63;
	if(pct > 100)
		pct = 100;
	if(pct == volumePct)
		return false;
	volumePct = pct;
	return true;
}

/* When the splash first showed: the logo's bob runs from there. */
static u64 splashStartMs = 0;

void uiShowSplash(struct screens* s)
{
	(void)s;
	if(topMode != TOP_SPLASH)
		splashStartMs = osGetTime();
	topDrawSplash(&canvas, &colors, "v" CTRMUS_VERSION,
	              (unsigned long)(osGetTime() - splashStartMs));
	topMode = TOP_SPLASH;
	renderPending = false;
	markAllDirty();
}

void uiDrawNowPlaying(struct screens* s)
{
	(void)s;
	renderNowPlaying();
}

void uiArtSettingChanged(void)
{
	if(topMode != TOP_NOW_PLAYING || playerNowPlayingFile() == NULL)
		return;
	requestArt();
	renderNowPlaying();
}

/* Performance mode: redraw just the footer when the time (or pause
 * state) it shows changed. */
static void updateFooter(void)
{
	const struct playbackInfo_t*	pi = playerInfo();
	long							elapsed = 0, total = 0;
	bool							paused = isPaused();

	if(!view.playing)
		return;
	if(pi->samples_per_second > 0)
	{
		elapsed = (long)(pi->samples_played / pi->samples_per_second);
		total = (long)(pi->samples_total / pi->samples_per_second);
	}
	if(elapsed == view.elapsed && total == view.total &&
	   paused == view.paused)
		return;

	view.elapsed = elapsed;
	view.total = total;
	view.paused = paused;
	topUpdateFooter(&canvas, &colors, &view);
	markAllDirty();		/* a row across the screen = every column */
}

void uiPresentTop(void)
{
	static u64	titlesMs = 0;
	u16		w, h;
	u8*		fb;
	bool	hasArt;

	/* Cover art finished loading: show it (or the placeholder). A result
	 * that arrives after "placeholder only" was turned on is dropped. */
	if(coverArtPoll(artPixels, &artW, &artH, &hasArt))
	{
		artState = (hasArt && !settingsPlaceholderArt()) ? ART_READY :
		           ART_NONE;
		artId++;
		if(topMode == TOP_NOW_PLAYING)
			renderPending = true;
	}

	/* Titles for the list rows came in: redraw, at most 5x a second. */
	if(topMode == TOP_NOW_PLAYING && osGetTime() - titlesMs >= 200 &&
	   titlesPoll())
	{
		titlesMs = osGetTime();
		renderPending = true;
	}

	/* Volume slider moved: just the volume line (a full redraw if one is
	 * due anyway picks it up from volumePct). */
	if(pollVolume() && topMode == TOP_NOW_PLAYING && !renderPending)
	{
		view.volumePct = volumePct;
		topUpdateVolume(&canvas, &colors, &view);
		markDirty(TOP_ART_COL0, TOP_ART_COL0 + TOP_ART_COLS);
	}

	/* Regular refresh (time, clock, battery) */
	if(topMode == TOP_NOW_PLAYING && !renderPending)
	{
		bool perf = settingsPerformanceMode();

		if(osGetTime() - lastRenderMs >= (perf ? PERF_REFRESH_MS :
		                                  REFRESH_MS))
			renderPending = true;
		else if(perf)
			updateFooter();
	}

	if(renderPending && topMode == TOP_NOW_PLAYING)
		renderNow();

	/* Splash: the logo bobs; redrawn only on the frames it moves. */
	if(topMode == TOP_SPLASH &&
	   topUpdateSplash(&canvas, &colors,
	                   (unsigned long)(osGetTime() - splashStartMs)))
		markAllDirty();

	/* Scrolling title/artist: only redraws those two lines, and only
	 * when they move. */
	if(topMode == TOP_NOW_PLAYING)
	{
		view.scrollMs = (unsigned long)(osGetTime() - songStartMs);
		if(topUpdateScroll(&canvas, &colors, &view))
			markDirty(TOP_ART_COL0, TOP_ART_COL0 + TOP_ART_COLS);
	}

	/* Spinning disc: turns while a song plays (not while paused) at the
	 * disc speed setting; only the art box is redrawn and copied (about
	 * 36 times a second at 33 rpm). The remaining rotations are
	 * pre-rendered one per frame meanwhile. */
	{
		static u64	lastMs = 0;
		u64			now = osGetTime();

		if(topMode == TOP_NOW_PLAYING && view.disc && view.playing &&
		   !isPaused())
			discPos = (discPos + (now - lastMs) * TOP_DISC_STEPS * 1000 /
			           (u64)settingsDiscTurnMs()) %
			          (TOP_DISC_STEPS * 1000);
		lastMs = now;
		discStep = (int)(discPos / 1000);

		if(topMode == TOP_NOW_PLAYING && view.disc)
		{
			topDiscWork(&view);
			view.discStep = discStep;
			if(topUpdateDisc(&canvas, &colors, &view))
				markDirty(TOP_ART_COL0, TOP_ART_COL0 + TOP_ART_COLS);
		}
	}

	if(dirtyFrames <= 0)
		return;

	fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
	if((int)w * h == TOP_W * TOP_H && dirtyX1 > dirtyX0)
		memcpy(fb + (size_t)dirtyX0 * TOP_H * sizeof(uint16_t),
		       topPixels + (size_t)dirtyX0 * TOP_H,
		       (size_t)(dirtyX1 - dirtyX0) * TOP_H * sizeof(uint16_t));
	dirtyFrames--;
}

void uiBottomCanvas(struct botCanvas* c, struct topColors* col)
{
	c->px = botPixels;
	c->font = consoleGetDefault()->font.gfx;
	*col = colors;
}

void uiBottomPresent(void)
{
	/* The console's framebuffer has the same layout as the canvas (240
	 * pixels per column); the bottom screen isn't double-buffered. */
	if(bottomConsole != NULL && bottomConsole->frameBuffer != NULL)
		memcpy(bottomConsole->frameBuffer, botPixels, sizeof(botPixels));
}

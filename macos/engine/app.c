/**
 * Papaya for macOS (native) - the app's main loop, one frame at a time.
 *
 * This is source/main.c's state machine (splash, browser, menu, the queue /
 * search / value / EQ pages) as appInit() / appTick() / appShutdown(): the
 * window's display link calls appTick() once per frame instead of a
 * while(aptMainLoop()) loop. The page and playback code it drives is the
 * 3DS app's own. When main.c changes, port the change here (the
 * pieces below are copied from it; keep them in the same order).
 *
 * Input arrives as 3DS-style button bits (the window maps keys and mouse
 * clicks to them, see gfxhid.c), so every page's own handling is reused.
 */

#include <3ds.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include "app.h"
#include "brightness.h"
#include "browser.h"
#include "effects.h"
#include "eqedit.h"
#include "coverart.h"
#include "menu.h"
#include "metadata.h"
#include "metaloader.h"
#include "platform.h"
#include "player.h"
#include "queue.h"
#include "queueedit.h"
#include "saver.h"
#include "search.h"
#include "settings.h"
#include "themes.h"
#include "titles.h"
#include "gfxmac.h"
#include "topscreen.h"
#include "ui.h"
#include "valueentry.h"

enum app_state
{
	STATE_SPLASH,
	STATE_BROWSER,
	STATE_MENU,
	STATE_QUEUE,	/* queue editor */
	STATE_SEARCH,
	STATE_VALUE,	/* typing an exact effect value */
	STATE_EQ		/* the equalizer */
};

/* The pages (one open at a time) */
static struct queueEdit	queueEditor;
static struct search	search;
static struct valueEntry	valueEntry;
static struct eqEdit		eqEditor;

/* Held-direction auto-repeat delay for browser navigation (ms). */
#define KEY_REPEAT_DELAY_MS		500
/* Now Playing panel refresh interval (ms). */

/* L/R were part of a combo, so their release doesn't skip/go back. */
struct combo
{
	bool	l;
	bool	r;
};

/*
 * Keys that work on every screen (browser and menu): pause, previous/next,
 * seeking, brightness, shuffle, the controls help.
 *
 * \return	true if this frame's input was used.
 */
static bool handlePlaybackKeys(u32 kDown, u32 kHeld, u32 kUp,
                               struct browser* b, struct combo* c)
{
	/* Shuffle: START. (It used to exit, which was too easy to hit by
	 * accident; exit is in the menu now, or via HOME.) */
	if(kDown & KEY_START)
	{
		playerToggleShuffle();
		return true;
	}

	/* Pause/Unpause: X */
	if(kDown & KEY_X)
	{
		playerTogglePause();
		return true;
	}

	if(kHeld & KEY_L)
	{
		/* Pause/Play: L+R or L+Up */
		if(kDown & (KEY_R | KEY_UP))
		{
			if(!playerTogglePause())
				return true;

			c->l = true;
			if(KEY_R & kDown)
				c->r = true;
			return true;
		}
	}

	/* R pressed first, then L */
	if((kHeld & KEY_R) && (kDown & KEY_L))
	{
		if(!playerTogglePause())
			return true;

		c->l = true;
		c->r = true;
		return true;
	}

	/* Seek backward: ZL */
	if((kDown & KEY_ZL) && playerSeek(-settingsSeekStep()))
	{
		if(kHeld & KEY_L) c->l = true;
		return true;
	}

	/* Seek forward: ZR */
	if((kDown & KEY_ZR) && playerSeek(settingsSeekStep()))
	{
		if(kHeld & KEY_R) c->r = true;
		return true;
	}

	/* Brightness: R+Up / R+Down. Holding R with Up/Down never moves the
	 * cursor, and R's release then doesn't skip the song. */
	if((kHeld & KEY_R) && (kHeld & (KEY_UP | KEY_DOWN)))
	{
		if(kDown & (KEY_UP | KEY_DOWN))
			uiBrightnessStep((kDown & KEY_UP) ? 1 : -1);
		c->r = true;
		return true;
	}

	/* L release: go BACK in history (previously played song) */
	if(kUp & KEY_L)
	{
		bool activate = !c->l;

		c->l = false;
		if(activate)
		{
			playerPlayPrevious();
			return true;
		}
	}

	/* R release: next song */
	if(kUp & KEY_R)
	{
		bool activate = !c->r;

		c->r = false;
		if(activate)
		{
			playerSkip(b);
			return true;
		}
	}

	return false;
}

/* Browser-only keys: menu, queue, stylus, navigation, open/play. */
static void browserInput(u32 kDown, u32 kHeld, struct browser* b,
                         struct screens* screens, struct menu* menu,
                         enum app_state* appState)
{
	static u64 mill = 0;

	/* Open Global Menu */
	if(kDown & KEY_SELECT)
	{
		*appState = STATE_MENU;
		browserSetVisible(b, false);
		menuOpen(menu, screens);
		return;
	}

	/* The bottom bar's "find" and "queue" buttons */
	if(kDown & KEY_TOUCH)
	{
		touchPosition tp;

		hidTouchRead(&tp);
		switch(browserFooterButtonAt(b, tp.px, tp.py))
		{
			case BROWSER_BTN_FIND:
				browserSetVisible(b, false);
				searchOpen(&search, b, false);
				*appState = STATE_SEARCH;
				return;
			case BROWSER_BTN_QUEUE:
				browserSetVisible(b, false);
				queueEditOpen(&queueEditor, false, b);
				*appState = STATE_QUEUE;
				return;
			case BROWSER_BTN_MENU:
				*appState = STATE_MENU;
				browserSetVisible(b, false);
				menuOpen(menu, screens);
				return;
			default:
				break;
		}

		/* A click on a row selects it (clicking the selected row again
		 * activates it: see "click to activate" in appTick). */
		{
			int idx = browserRowAt(b, tp.px, tp.py);

			if(idx >= 0 && idx != b->cursor)
			{
				b->cursor = idx;
				browserDraw(b);
			}
		}
	}

	/* Add to queue: Y (files only, not directories or ../) */
	if(kDown & KEY_Y)
	{
		int idx = browserSelectedFile(b);

		if(idx >= 0)
		{
			char absPath[PATH_MAX];

			browserFilePath(b, idx, absPath, sizeof(absPath));
			playerEnqueue(absPath, browserFileTitle(b, idx));
		}
		return;
	}

	if(kDown)
		mill = osGetTime();

	/* Dragging the scrollbar with the stylus */
	browserTouch(b, kDown, kHeld);

	/* Navigation (held directions auto-repeat after a delay) */
	{
		bool repeat = osGetTime() - mill > KEY_REPEAT_DELAY_MS;

		if((kDown & KEY_UP) || ((kHeld & KEY_UP) && repeat))
			browserMoveUp(b);

		if((kDown & KEY_DOWN) || ((kHeld & KEY_DOWN) && repeat))
			browserMoveDown(b);

		if((kDown & KEY_LEFT) || ((kHeld & KEY_LEFT) && repeat))
			browserPageUp(b);

		if((kDown & KEY_RIGHT) || ((kHeld & KEY_RIGHT) && repeat))
			browserPageDown(b);
	}

	/* Go up a folder: B, or A when ".." is selected */
	if((kDown & KEY_B) ||
	   ((kDown & KEY_A) && browserOnParentEntry(b)))
	{
		browserGoUp(b);
		return;
	}

	/* A: Open directory or play file (queue is NOT cleared) */
	if(kDown & KEY_A)
	{
		int idx;

		if(browserEnterSelectedDir(b))
			return;

		if((idx = browserSelectedFile(b)) >= 0)
			playerPlayFromBrowser(b, idx);
	}
}


static struct screens	screens;			/* the browser's console */
static struct screens	overlayScreens;		/* the menu's: shown over either panel */
static struct browser	browser;
static struct menu		menu;
static struct theme_t	theme;
static bool				mcuAvailable = false;
static bool				valueFromEq = false;
static enum app_state	appState = STATE_SPLASH;
static struct combo		combo = { false, false };
static bool				fatal = false;
static bool				running = false;

/* Click to activate: a click on a row selects it; a click (that doesn't
 * drag) on the row that was ALREADY selected acts like A on it. So a double
 * click opens/plays, and one click on the selected row does too. Buttons,
 * scrollbars and sliders aren't rows, so they keep their own touch handling. */
static struct { int row; int sel; bool moved; int x0, y0; enum app_state st; }
	tap = { -1, -1, false, 0, 0, STATE_SPLASH };

static int pageRowAt(int px, int py)
{
	switch(appState)
	{
		case STATE_BROWSER:	return browserRowAt(&browser, px, py);
		case STATE_MENU:	return menuRowAt(&menu, px, py);
		case STATE_QUEUE:	return queueEditRowAt(&queueEditor, px, py);
		case STATE_SEARCH:	return searchRowAt(&search, px, py);
		default:			return -1;
	}
}

static int pageSelected(void)
{
	switch(appState)
	{
		case STATE_BROWSER:	return browser.cursor;
		case STATE_MENU:	return menu.selected;
		case STATE_QUEUE:	return queueEditor.selected;
		case STATE_SEARCH:	return search.selected;
		default:			return -1;
	}
}


/* Which screens are overlays, and what Esc / typing mean right now (the
 * window reads these through macSetState). */
static bool overlayState(void)
{
	return appState == STATE_MENU || appState == STATE_QUEUE ||
	       appState == STATE_SEARCH || appState == STATE_VALUE ||
	       appState == STATE_EQ;
}

static void syncTargets(void)
{
	bool ov = overlayState();
	bool text = appState == STATE_VALUE ||
	            (appState == STATE_SEARCH && search.typing);
	/* Esc: opens the menu from a panel; inside the menu and its pages it
	 * goes back one level (B); in a text field it cancels / closes. */
	u32 esc = (appState == STATE_MENU || appState == STATE_QUEUE ||
	           appState == STATE_EQ) ? KEY_B : KEY_SELECT;

	uiSetBottomConsole(ov ? &overlayScreens.bottom : &screens.bottom);
	macSetState(ov, text, esc);
}

void appInit(void)
{
	saverInit();
	settingsMigrateOld();
	settingsInit();

	gfxInitDefault();
	mcuAvailable = (mcuHwcInit() == 0);
	brightnessInit();
	srand((unsigned)osGetTime());

	loadTheme(&theme);
	uiInit(&screens, &theme, mcuAvailable);

	/* The menu and the pages are an overlay: their own 320x240 surface, so
	 * the window can show them over the top panel or over the browser. */
	consoleInit(GFX_OVERLAY, &overlayScreens.bottom);
	uiApplyTheme(&overlayScreens, &theme);
	consoleSelect(&screens.bottom);

	playerInit();
	metaLoaderInit();
	coverArtInit();
	titlesInit();
	browserInit(&browser, &screens.bottom);
	uiSetBrowser(&browser);

	uiShowSplash(&screens);
	hidScanInput();
	running = true;
}

void appTick(void)
{
	u32 kDown, kHeld, kUp;

	if(!running)
		return;

	uiPresentTop();
	hidScanInput();
	kDown = hidKeysDown();
	kHeld = hidKeysHeld();
	kUp = hidKeysUp();

	/* Click to activate (see above): the press remembers what was selected
	 * before the click; if the click is released without moving and landed
	 * on that same row, it counts as A. */
	{
		touchPosition tp;

		hidTouchRead(&tp);
		if(kDown & KEY_TOUCH)
		{
			tap.row = pageRowAt(tp.px, tp.py);
			tap.sel = pageSelected();
			tap.moved = false;
			tap.x0 = tp.px;
			tap.y0 = tp.py;
			tap.st = appState;
		}
		if((kHeld & KEY_TOUCH) && tap.row >= 0 &&
		   (abs(tp.px - tap.x0) > 4 || abs(tp.py - tap.y0) > 4))
			tap.moved = true;
		if(kUp & KEY_TOUCH)
		{
			if(tap.row >= 0 && !tap.moved && tap.row == tap.sel &&
			   tap.st == appState)
				kDown |= (appState == STATE_MENU &&
				          menuActivateIsType(&menu)) ? KEY_Y : KEY_A;
			tap.row = -1;
		}
	}

	/* Pick up titles from the background metadata loader. In the
	 * menu the list isn't visible; it is redrawn when the menu
	 * closes. */
	if(appState != STATE_SPLASH &&
	   browserPoll(&browser, appState == STATE_BROWSER) !=
	   DIRLIST_POLL_IDLE)
		playerSyncPlaylist(&browser);	/* new titles/order */

	/* ============================================================ */
	/* STATE: SPLASH                                                */
	/* ============================================================ */
	if(appState == STATE_SPLASH)
	{
		if(kDown & KEY_A)
		{
			appState = STATE_BROWSER;

			if(browserOpen(&browser) < 0)
			{
				puts("Unable to obtain directory information");
				fatal = true;
				return;
			}
			uiDrawNowPlaying(&screens);
		}

		return;
	}

	consoleSelect(&screens.bottom);
	syncTargets();

	/* Playback keys work everywhere (browser, menu, pages); the rest
	 * is up to the screen that's open. */
	if(handlePlaybackKeys(kDown, kHeld, kUp, &browser, &combo))
	{
		/* Shuffle, brightness and the queue show in the menu. (The
		 * pages follow the queue themselves.) */
		if(appState == STATE_MENU)
			menuRedraw(&menu, &overlayScreens);
	}
	else if(appState == STATE_QUEUE || appState == STATE_SEARCH)
	{
		enum page_result r = appState == STATE_QUEUE ?
		                     queueEditInput(&queueEditor, kDown, kHeld) :
		                     searchInput(&search, kDown, kHeld,
		                                 &browser);

		if(r != PAGE_STAY && appState == STATE_SEARCH)
			searchClose(&search);
		if(r == PAGE_CLOSE)
		{
			appState = STATE_BROWSER;
			browserSetVisible(&browser, true);
		}
		else if(r == PAGE_TO_MENU)
		{
			appState = STATE_MENU;
			menuResume(&menu, &overlayScreens);
		}
	}
	else if(appState == STATE_VALUE)
	{
		/* Done or cancelled: back to where it was opened from (it
		 * redraws its values, and the top screen was told to
		 * redraw). */
		if(valueEntryInput(&valueEntry, kDown, kHeld) != PAGE_STAY)
		{
			if(valueFromEq)
			{
				appState = STATE_EQ;
				eqEditResume(&eqEditor);
			}
			else
			{
				appState = STATE_MENU;
				menuResume(&menu, &overlayScreens);
			}
		}
	}
	else if(appState == STATE_EQ)
	{
		enum page_result r = eqEditInput(&eqEditor, kDown, kHeld);

		if(r == PAGE_VALUE)
		{
			valueEntryOpen(&valueEntry,
			               (enum valueTarget)eqEditValueTarget(&eqEditor));
			valueFromEq = true;
			appState = STATE_VALUE;
		}
		else if(r != PAGE_STAY)
		{
			appState = STATE_MENU;
			menuResume(&menu, &overlayScreens);
		}
	}
	else if(appState == STATE_MENU)
	{
		/* ======================================================== */
		/* STATE: MENU                                              */
		/* ======================================================== */
		enum menu_result r = menuHandleInput(&menu, kDown, kHeld,
		                                     &overlayScreens, &browser);

		/* Theme/setting changes are saved once, when the menu
		 * closes. */
		if(r != MENU_OPEN)
		{
			themeSaveIfChanged();
			settingsSaveIfChanged();
		}
		if(r == MENU_QUIT)
		{
			videoRequestQuit();
			return;
		}
		if(r == MENU_CLOSED)
		{
			appState = STATE_BROWSER;
			/* a theme picked in the menu: the browser's console too */
			uiApplyTheme(&screens, getCurrentTheme());
			consoleSelect(&screens.bottom);
			browserDraw(&browser);
		}
		if(r == MENU_QUEUE)
		{
			queueEditOpen(&queueEditor, true, &browser);
			appState = STATE_QUEUE;
		}
		if(r == MENU_SEARCH)
		{
			searchOpen(&search, &browser, true);
			appState = STATE_SEARCH;
		}
		if(r == MENU_VALUE)
		{
			valueEntryOpen(&valueEntry,
			               (enum valueTarget)menuValueTarget(&menu));
			valueFromEq = false;
			appState = STATE_VALUE;
		}
		if(r == MENU_EQ)
		{
			eqEditOpen(&eqEditor);
			appState = STATE_EQ;
		}
	}
	else
	{
		/* ======================================================== */
		/* STATE: BROWSER                                           */
		/* ======================================================== */
		browserInput(kDown, kHeld, &browser, &overlayScreens, &menu,
		             &appState);
	}

	/* Auto-advance when a song finishes (also while the menu is
	 * open, which used to hold the next song back until it closed). */
	if(playerHandleSongEnd(&browser) && appState == STATE_MENU)
		menuRedraw(&menu, &overlayScreens);

	/* The pages redraw themselves when what they show changed (on the
	 * overlay, if a page just opened) */
	syncTargets();
	if(appState == STATE_QUEUE)
		queueEditUpdate(&queueEditor);
	else if(appState == STATE_SEARCH)
		searchUpdate(&search);
	else if(appState == STATE_VALUE)
		valueEntryUpdate(&valueEntry);
	else if(appState == STATE_EQ)
		eqEditUpdate(&eqEditor);

	/* The "p>" line and the queue button on the bottom screen. */
	browserSetPlaylistDir(&browser, playerPlaylistDir());
	browserSetQueueCount(&browser, queueSize(playerQueue()));
}

void appShutdown(void)
{
	if(!running)
		return;
	running = false;
	if(appState == STATE_SEARCH)
		searchClose(&search);
	themeSaveIfChanged();
	playerExit();
	effectsFree();		/* after the playback thread has stopped */

	uiSetBrowser(NULL);
	browserExit(&browser);
	metaLoaderExit();
	coverArtExit();
	titlesExit();

	if(mcuAvailable)
		mcuHwcExit();
	brightnessExit();

	gfxExit();
	settingsExit();
	saverExit();		/* after the last saves: writes what's waiting */
}

/* ------------------------------------------------------------------ */
/* Things the window asks for outside the button bits                  */
/* ------------------------------------------------------------------ */

/* Wheel / trackpad: scroll whatever list is open by `rows`. Returns false
 * if the open screen has nothing to scroll (the window then sends D-pad
 * taps, which is how the menu moves). Main thread, like appTick(). */
bool appScroll(int rows)
{
	if(!running || rows == 0)
		return false;
	consoleSelect(&screens.bottom);
	switch(appState)
	{
		case STATE_BROWSER:
			browserScrollBy(&browser, rows);
			return true;
		case STATE_QUEUE:
			queueEditScrollBy(&queueEditor, rows);
			return true;
		case STATE_SEARCH:
			searchScrollBy(&search, rows);
			return true;
		default:
			return false;
	}
}

void appDoCommand(enum appCommand cmd)
{
	if(!running || appState == STATE_SPLASH)
		return;
	switch(cmd)
	{
		case APP_PLAY_PAUSE:	playerTogglePause(); break;
		case APP_PLAY:			if(isPlaying() && isPaused()) playerTogglePause(); break;
		case APP_PAUSE:			playerPause(); break;
		case APP_NEXT:			playerSkip(&browser); break;
		case APP_PREVIOUS:		playerPlayPrevious(); break;
		case APP_SEEK_BACK:		playerSeek(-settingsSeekStep()); break;
		case APP_SEEK_FORWARD:	playerSeek(settingsSeekStep()); break;
	}
	if(appState == STATE_MENU)
	{
		consoleSelect(&screens.bottom);
		menuRedraw(&menu, &overlayScreens);	/* shuffle/queue show in the menu */
	}
}

/* Jump to `seconds` in the current song (relative seek by the difference). */
void appSeekTo(double seconds)
{
	const struct playbackInfo_t* pi = playerInfo();

	if(!running || pi->samples_per_second == 0)
		return;
	playerSeek((int)(seconds - (double)pi->samples_played /
	                           (double)pi->samples_per_second));
}

static const char* baseNameOf(const char* p)
{
	const char* s = strrchr(p, '/');

	return s ? s + 1 : p;
}

void appGetState(struct appState* st)
{
	const struct playbackInfo_t*	pi = playerInfo();
	const struct music_metadata*	meta = playerNowPlayingMeta();
	const char*						file = playerNowPlayingFile();

	memset(st, 0, sizeof(*st));
	if(!running || file == NULL)
		return;
	st->playing = isPlaying();
	st->paused = isPaused();
	st->songId = playerSongId();
	if(pi->samples_per_second > 0)
	{
		st->elapsed = (double)pi->samples_played / (double)pi->samples_per_second;
		st->total = (double)pi->samples_total / (double)pi->samples_per_second;
	}
	st->speed = getPlaybackSpeed();
	snprintf(st->path, sizeof(st->path), "%s", file);
	snprintf(st->title, sizeof(st->title), "%s",
	         meta->has_title ? meta->title : baseNameOf(file));
	if(meta->has_artist)
		snprintf(st->artist, sizeof(st->artist), "%s", meta->artist);
	if(meta->has_album)
		snprintf(st->album, sizeof(st->album), "%s", meta->album);
}

/* Embedded cover of a file (JPEG/PNG bytes, malloc'd; free() it). */
bool appCoverArt(const char* path, unsigned char** data, size_t* len)
{
	return getCoverArt(path, data, len) == 0 && *data != NULL;
}

/* The window was resized: lay the top screen out for w x h logical pixels. */
void appResizeTop(int w, int h)
{
	if(w < 400) w = 400;		/* the layout needs at least the 3DS's size */
	if(h < 240) h = 240;
	macSetTopSize(w, h);
	macTopSize(&w, &h);			/* clamped to what the buffers hold */
	if(running)
		uiSetTopSize(w, h);
	else
		topSetSize(w, h);
}

/* The bottom screen is now cols x rows characters (at least 40 x 30). */
void appResizeBottom(int cols, int rows)
{
	if(cols < 40) cols = 40;
	if(rows < 30) rows = 30;
	macSetBottomSize(cols, rows);
	if(!running)
		return;
	consoleResize(&screens.bottom, cols, rows);
	consoleSelect(&screens.bottom);
	browserSetSize(&browser, cols, rows);	/* redraws the browser if shown */
}

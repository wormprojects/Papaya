/**
 * ctrmus - 3DS Music Player
 * Copyright (C) 2016 Mahyar Koshkouei
 *
 * This program comes with ABSOLUTELY NO WARRANTY and is free software. You are
 * welcome to redistribute it under certain conditions; for details see the
 * LICENSE file.
 */

/*
 * Application entry point: setup, the per-frame main loop / input
 * dispatch, and teardown. The actual work lives in:
 *   ui.c       screens, log, splash, Now Playing panel
 *   browser.c  file browser cursor/navigation/drawing (on dirlist.c)
 *   player.c   playing songs, queue, history, shuffle/repeat, next song
 *   menu.c     global menu
 *   queueedit.c / search.c   the pixel-drawn bottom pages
 */

#include <3ds.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "brightness.h"
#include "browser.h"
#include "effects.h"
#include "coverart.h"
#include "menu.h"
#include "metaloader.h"
#include "player.h"
#include "queue.h"
#include "queueedit.h"
#include "saver.h"
#include "search.h"
#include "settings.h"
#include "themes.h"
#include "titles.h"
#include "ui.h"

enum app_state
{
	STATE_SPLASH,
	STATE_BROWSER,
	STATE_MENU,
	STATE_QUEUE,	/* queue editor */
	STATE_SEARCH
};

/* The pages (one open at a time) */
static struct queueEdit	queueEditor;
static struct search	search;

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
			default:
				break;
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

int main(int argc, char **argv)
{
	struct screens	screens;
	struct browser	browser;
	struct menu		menu;
	struct theme_t	theme;
	bool			mcuAvailable = false;
	enum app_state	appState = STATE_SPLASH;

	/* ignore key release of L/R if L+R or L+down was pressed */
	struct combo	combo = { false, false };

	(void)argc;
	(void)argv;

	/* Settings first: it turns on New 3DS fast mode if the user chose it
	 * (off by default, it costs battery). The saver writes settings and
	 * the theme in the background. */
	saverInit();
	settingsMigrateOld();
	settingsInit();

	gfxInitDefault();
	mcuAvailable = (mcuHwcInit() == 0);
	brightnessInit();
	srand(osGetTime());

	/* Load and apply theme */
	loadTheme(&theme);
	uiInit(&screens, &theme, mcuAvailable);

	playerInit();
	metaLoaderInit();
	coverArtInit();
	titlesInit();
	browserInit(&browser, &screens.bottom);
	uiSetBrowser(&browser);

	/* Show splash screen */
	uiShowSplash(&screens);

	gfxFlushBuffers();
	gspWaitForVBlank();
	hidScanInput();

	aptSetSleepAllowed(false);

	bool headsetWas = osIsHeadsetConnected();

	while(aptMainLoop())
	{
		u32 kDown, kHeld, kUp;

		/* Copy the top screen canvas into the back buffer, then swap and
		 * wait for VBlank so the swap has happened before the next frame
		 * draws into the (then) back buffer again. */
		uiPresentTop();
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();

		hidScanInput();
		kDown = hidKeysDown();
		kHeld = hidKeysHeld();
		kUp = hidKeysUp();

		/* Headphones pulled out: pause, so the music doesn't suddenly
		 * play out of the speakers. Plugging them back in doesn't resume. */
		{
			bool headset = osIsHeadsetConnected();

			if(headsetWas && !headset)
				playerPause();
			headsetWas = headset;
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
					goto err;
				}
				uiDrawNowPlaying(&screens);
			}

			continue;
		}

		consoleSelect(&screens.bottom);

		/* Playback keys work everywhere (browser, menu, pages); the rest
		 * is up to the screen that's open. */
		if(handlePlaybackKeys(kDown, kHeld, kUp, &browser, &combo))
		{
			/* Shuffle, brightness and the queue show in the menu. (The
			 * pages follow the queue themselves.) */
			if(appState == STATE_MENU)
				menuRedraw(&menu, &screens);
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
				menuResume(&menu, &screens);
			}
		}
		else if(appState == STATE_MENU)
		{
			/* ======================================================== */
			/* STATE: MENU                                              */
			/* ======================================================== */
			enum menu_result r = menuHandleInput(&menu, kDown, kHeld,
			                                     &screens, &browser);

			/* Theme/setting changes are saved once, when the menu
			 * closes. */
			if(r != MENU_OPEN)
			{
				themeSaveIfChanged();
				settingsSaveIfChanged();
			}
			if(r == MENU_QUIT)
				break;
			if(r == MENU_CLOSED)
				appState = STATE_BROWSER;
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
		}
		else
		{
			/* ======================================================== */
			/* STATE: BROWSER                                           */
			/* ======================================================== */
			browserInput(kDown, kHeld, &browser, &screens, &menu,
			             &appState);
		}

		/* Auto-advance when a song finishes (also while the menu is
		 * open, which used to hold the next song back until it closed). */
		if(playerHandleSongEnd(&browser) && appState == STATE_MENU)
			menuRedraw(&menu, &screens);

		/* The pages redraw themselves when what they show changed */
		if(appState == STATE_QUEUE)
			queueEditUpdate(&queueEditor);
		else if(appState == STATE_SEARCH)
			searchUpdate(&search);

		/* The "p>" line and the queue button on the bottom screen. */
		browserSetPlaylistDir(&browser, playerPlaylistDir());
		browserSetQueueCount(&browser, queueSize(playerQueue()));
	}

out:
	puts("Exiting...");
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
	return 0;

err:
	puts("A fatal error occurred. Press START to exit.");

	while(true)
	{
		u32 kDown2;
		hidScanInput();
		kDown2 = hidKeysDown();
		if(kDown2 & KEY_START)
			break;
	}

	goto out;
}

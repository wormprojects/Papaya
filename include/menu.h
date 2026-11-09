/**
 * ctrmus - 3DS Music Player
 * The menu (SELECT), drawn on the bottom screen: sections of settings and
 * actions, sliders (D-pad or stylus), and an Effects page. START no longer
 * exits; "Exit Papaya" does.
 */

#ifndef ctrmus_menu_h
#define ctrmus_menu_h

#include <3ds.h>
#include <stdbool.h>

struct browser;
struct screens;

struct menu
{
	int	page;		/* MENU_PAGE_* (menu.c) */
	int	selected;	/* item index on the page */
	int	drag;		/* slider being dragged with the stylus, or -1 */
	int	scroll;		/* screen rows the page is scrolled up by */
	bool	picking;	/* the theme picker is open (Menu > Themes) */
	int		themeOrig;	/* theme in use when the picker opened */
};

/** Open the menu on its main page and draw it. */
void menuOpen(struct menu* m, struct screens* s);

/**
 * Show the menu again as it was (selection kept), coming back from a
 * page it opened (Queue..., Search...).
 */
void menuResume(struct menu* m, struct screens* s);

/** Redraw the menu (after something it shows changed). */
void menuRedraw(struct menu* m, struct screens* s);

enum menu_result
{
	MENU_OPEN,		/* still open */
	MENU_CLOSED,	/* closed; the browser has been redrawn */
	MENU_QUIT,		/* "Exit Papaya" was chosen */
	MENU_QUEUE,		/* open the queue editor (the menu stays hidden) */
	MENU_SEARCH		/* open search */
};

/**
 * Handle one frame of input while the menu is open: Up/Down select,
 * Left/Right change, A activates, B goes back/closes, SELECT closes,
 * the stylus selects rows and drags sliders. main.c handles the playback
 * keys first, so they work in the menu too. Hide the browser
 * (browserSetVisible) before opening; closing shows it again.
 */
enum menu_result menuHandleInput(struct menu* m, u32 kDown, u32 kHeld,
                                 struct screens* s, struct browser* b);

#endif

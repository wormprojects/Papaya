/**
 * Papaya - the key hints shown in the menu and the pages.
 *
 * On the 3DS they name the buttons (A, B, Y, SELECT...); on the desktop
 * (PAPAYA_DESKTOP, the macOS branch) there are no such buttons, so the same
 * hints name the keys that do the same thing: Return = A, Esc/Backspace = B,
 * T (or a click on the selected slider) = Y "type a value", Q = Y "queue",
 * Del = Y "remove". Every string is at most 40 characters (one console
 * row / hint bar).
 */

#ifndef papaya_keyhints_h
#define papaya_keyhints_h

#ifdef PAPAYA_DESKTOP

/* menu.c: item help (the line above the key hints) */
#define H_SHUFFLE		"Random order. S works anywhere."
#define H_THEME			"Return: pick from a list. </>: cycle."
#define H_SEEK			"How far Left/Right seek."
#define H_EXIT			"Quit (Cmd+Q also works)."
#define H_SPEED			"Changes pitch. Return: 1.0x. T: type."
#define H_DISTORTION	"Soft clip. Return: off. T: type value."
#define H_BITS			"Grittier. Return: 32 bit. T: type."
#define H_RATE			"Lo-fi. Return: 32k (off). T: type."
#define H_SPATIAL		"Wider stereo. Return: off. T: type."
#define H_REVERB		"Reverb amount. Return: off. T: type."
#define H_REVERB_TIME	"How long it rings out. T: type value."
/* menu.c: theme picker, and the hint bar of every page */
#define H_PICK_HELP		" T: try on. Return: use. Esc: cancel."
#define H_PICK_HINT		" Return use  T preview  Esc cancel"
#define H_MENU_MAIN		" Return select  Esc close  </> change"
#define H_MENU_SUB		" Return select  Esc back  </> change"

/* bottomscreen.c */
#define H_QUEUE_EMPTY1	"Q in the browser queues a song,"
#define H_QUEUE_EMPTY2	"or Q on a search result."
#define H_QUEUE_BAR		" Esc back"
#define H_QUEUE_KEYS	" Return play  Del remove  \x1a lift  Esc"
#define H_SEARCH_FIELD	"click here to type"
#define H_SEARCH_TYPING	" type  Return: results  Esc: close"
#define H_SEARCH_LIST	" Return play Q queue \x1a open Bksp type"
#define H_VALUE_NOTE	"Return sets it, Esc cancels"
#define H_VALUE_BAR		" type a number  Return set  Esc cancel"
#define H_EQ_BAR		" \x1b\x1a pick  \x1e\x1f change  Return 0  T type"

#else

#define H_SHUFFLE		"Random order. START works anywhere."
#define H_THEME			"A: pick from a list. </>: cycle."
#define H_SEEK			"How far ZL/ZR jump."
#define H_EXIT			"Quit (HOME > Close also works)."
#define H_SPEED			"Changes pitch. A: 1.0x. Y: type value."
#define H_DISTORTION	"Soft clip. A: off. Y: type a value."
#define H_BITS			"Grittier. A: 32 bit. Y: type a value."
#define H_RATE			"Lo-fi. A: 32k (off). Y: type a value."
#define H_SPATIAL		"Wider stereo. A: off. Y: type a value."
#define H_REVERB		"Reverb amount. A: off. Y: type a value."
#define H_REVERB_TIME	"How long it rings out. Y: type a value."
#define H_PICK_HELP		" Y: try it on. A: use it. B: cancel."
#define H_PICK_HINT		" A use  Y preview  B cancel  stylus"
#define H_MENU_MAIN		" A select  B close  </> change  stylus"
#define H_MENU_SUB		" A select  B back  </> change  stylus"

#define H_QUEUE_EMPTY1	"Y in the browser queues a song,"
#define H_QUEUE_EMPTY2	"or Y on a search result."
#define H_QUEUE_BAR		" B back  SELECT close"
#define H_QUEUE_KEYS	" A play  Y remove  \x1a lift  B back"
#define H_SEARCH_FIELD	"tap here to type"
#define H_SEARCH_TYPING	" A key  B delete  Y results  SEL close"
#define H_SEARCH_LIST	" A play  Y queue  \x1a open  B type"
#define H_VALUE_NOTE	"Y sets it, B cancels when empty"
#define H_VALUE_BAR		" A key  B delete  Y set  SEL cancel"
#define H_EQ_BAR		" \x1b\x1a pick  \x1e\x1f change  A zero  Y type"

#endif

#endif

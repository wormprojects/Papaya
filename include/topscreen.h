/**
 * ctrmus - 3DS Music Player
 * UI PROTOTYPE: software-rendered top screen.
 *
 * The top screen is no longer a PrintConsole. It is drawn into a 400x240
 * RGB565 canvas using libctru's own 8x8 console font (so text looks the
 * same as before) mixed with real graphics: lines, icons, the progress bar
 * and cover art. ui.c copies the canvas into the (double-buffered) top
 * framebuffer.
 *
 * This file and topscreen.c are plain C with no libctru dependency, so the
 * layout can also be rendered on a PC (tools/preview).
 */

#ifndef ctrmus_topscreen_h
#define ctrmus_topscreen_h

#include <stdbool.h>
#include <stdint.h>

#define TOP_W 400
#define TOP_H 240

/* Cover art box (inside its 1px frame). */
#define TOP_ART_SIZE 144
/* Columns the art box (frame included) covers, for partial copies. */
#define TOP_ART_COL0	127
#define TOP_ART_COLS	(TOP_ART_SIZE + 2)

/* Spinning disc: rotation steps per turn (a multiple of 4). */
#define TOP_DISC_STEPS	64

/**
 * Canvas in the 3DS framebuffer layout: column-major, TOP_H pixels per
 * column, bottom row first, i.e. pixel (x, y) is px[x * TOP_H + (TOP_H-1-y)].
 * That makes copying it to the framebuffer a plain memcpy.
 */
struct topCanvas
{
	uint16_t*		px;		/* TOP_W * TOP_H, RGB565 */
	const uint8_t*	font;	/* 256 glyphs, 8 bytes each, MSB = left */
};

/* RGB565 colours, taken from the theme. */
struct topColors
{
	uint16_t bg;
	uint16_t text;		/* main text, art tint */
	uint16_t dim;		/* labels, secondary text */
	uint16_t bar;		/* progress bar, lines */
	uint16_t accent;	/* section headers */
};

enum npRepeat
{
	NP_REPEAT_OFF,
	NP_REPEAT_ALL,
	NP_REPEAT_ONE
};

#define NP_MAX_LIST		24

/* Everything the Now Playing screen shows, already turned into strings. */
struct npView
{
	/* Header */
	int				brightnessPct;		/* < 0: unknown, shows "--%" */
	char			clock[32];
	int				batteryPct;			/* < 0: unknown, not shown */

	/* Left column */
	int				queueCount;
	int				queueShown;
	const char*		queue[NP_MAX_LIST];
	int				playlistCount;
	int				playlistShown;
	int				playlistCurrent;	/* highlighted entry, -1 = none */
	const char*		playlist[NP_MAX_LIST];	/* in play order */

	/* Centre */
	bool			playing;			/* a song is loaded */
	bool			paused;
	const uint16_t*	artPixels;			/* RGB565, TOP_ART_SIZE stride,
										 * NULL = placeholder */
	int				artW, artH;
	bool			artLoading;			/* still decoding: empty box */
	unsigned		artId;				/* changes when artPixels does */
	bool			disc;				/* round art (spinning disc) */
	int				discStep;			/* rotation, 0..TOP_DISC_STEPS-1 */
	const char*		title;				/* both scroll if too long */
	const char*		subtitle;
	unsigned long	scrollMs;			/* time since the song started */
	bool			noScroll;			/* cut long lines with "..." */
	int				volumePct;			/* volume slider, < 0: not shown */

	/* Right column */
	const char*		tagTitle;
	const char*		tagArtist;
	const char*		tagAlbum;
	char			size[16];
	char			bitrate[16];
	char			format[8];
	float			speed;
	int				distortionPct;		/* 0: off */
	int				reverbMixPct;		/* 0: off */
	float			reverbDecay;		/* seconds */

	/* Footer */
	enum npRepeat	repeat;
	bool			shuffle;
	long			elapsed;			/* seconds */
	long			total;				/* seconds, <= 0: unknown */
	bool			remaining;			/* show "-0:12:34" (time left) */
};

/** Draw the whole Now Playing screen (clears the canvas first). */
void topDrawNowPlaying(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v);

/**
 * How many entries of the queue and the playlist the left column will
 * draw for this view (only these need real titles). Uses the counts in
 * the view, not the strings.
 */
void topListRows(const struct npView* v, int* queueRows, int* playlistRows);

/**
 * Redraw just the title/subtitle lines under the art if their scroll
 * position (from v->scrollMs) changed since they were last drawn. Cheap:
 * call it every frame.
 *
 * \return	true if anything was drawn.
 */
bool topUpdateScroll(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v);

/**
 * Spinning disc: pre-render one more rotation of the current cover art
 * (64 steps per turn are 16 stored frames; the other three quarters are
 * the same frames read sideways). Call once per frame; it does nothing
 * once all are done or when the view has no disc art.
 *
 * \return	true if it rendered a frame.
 */
bool topDiscWork(const struct npView* v);

/**
 * Redraw just the art box if v->discStep changed since it was drawn.
 *
 * \return	true if anything was drawn (columns TOP_ART_COL0..+COLS).
 */
bool topUpdateDisc(struct topCanvas* c, const struct topColors* col,
                   const struct npView* v);

/** Redraw just the footer (time, progress bar, repeat, shuffle). */
void topUpdateFooter(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v);

/** Redraw just the volume line (after v->volumePct changed). */
void topUpdateVolume(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v);

/**
 * Draw the splash screen: the logo and "Press A to start". The logo bobs
 * on a sine of ms (any running time in milliseconds).
 */
void topDrawSplash(struct topCanvas* c, const struct topColors* col,
                   const char* version, unsigned long ms);

/**
 * Move the splash logo to where it is at ms. Cheap: call it every frame.
 *
 * \return	true if it moved (redrawn in place).
 */
bool topUpdateSplash(struct topCanvas* c, const struct topColors* col,
                     unsigned long ms);

#endif

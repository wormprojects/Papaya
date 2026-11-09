/**
 * ctrmus - 3DS Music Player
 * UI PROTOTYPE: software-rendered top screen. See topscreen.h.
 *
 * Layout (pixels, 400x240), modelled on ctrmus-new-ui-concept.png but
 * re-fitted for the 8x8 console font:
 *
 *   y 1    header: brightness | date + time | battery
 *   y 12   left column (queue, playlist) | art frame | right column
 *          (details, effects). Side columns are 15 characters wide, the
 *          art is 144x144 inside a 1px frame.
 *   y 160  title / "artist - album" under the art
 *   y 220  volume, centred just above the progress bar
 *   y 231  footer: repeat | elapsed [progress bar] total | shuffle
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logo.h"
#include "topscreen.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CH			8	/* glyph size */
#define LINE_H		10	/* text line spacing */

#define HEADER_Y	1

#define LCOL_X		2
#define RCOL_X		278
#define COL_CHARS	15
#define COL_TOP		13
#define COL_YMAX	228	/* text in the side columns must end above this */

#define ART_FX		TOP_ART_COL0	/* art frame (1px around ART_IN) */
#define ART_FY		12
#define ART_IN		TOP_ART_SIZE
#define ART_FW		(ART_IN + 2)

#define TITLE_Y		160
#define SUB_Y		170
#define VOL_Y		220

/*
 * Long title/subtitle lines scroll like a CD player display: hold at the
 * start, then step left one character at a time and wrap around with a
 * gap, then hold again.
 */
#define SCROLL_HOLD_MS	2000
#define SCROLL_STEP_MS	300
#define SCROLL_GAP		4

/* Left column: a section only shows if it gets at least this many rows. */
#define MIN_PLAYLIST_ROWS	3
#define SECTION_GAP			5
#define HEADER_H			(CH + 5)	/* header text + underline + gap */
#define CENTER_CHARS (ART_FW / CH)

#define FOOT_Y		232
#define BAR_X		108
#define BAR_W		184
#define BAR_Y		231
#define BAR_H		9

/* 8x8 icons, MSB = left. */
static const uint8_t iconSun[8] =
	{ 0x10, 0x44, 0x38, 0xBA, 0x38, 0x44, 0x10, 0x00 };
static const uint8_t iconRepeat[8] =
	{ 0x04, 0x7E, 0x84, 0x81, 0x81, 0x21, 0x7E, 0x20 };
static const uint8_t iconShuffle[8] =
	{ 0x02, 0xC3, 0x26, 0x18, 0x18, 0x26, 0xC3, 0x02 };
static const uint8_t iconVolume[8] =
	{ 0x12, 0x31, 0xF5, 0xF5, 0xF5, 0x31, 0x12, 0x00 };
static const uint8_t iconMuted[8] =
	{ 0x10, 0x30, 0xF5, 0xF2, 0xF5, 0x30, 0x10, 0x00 };

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

static inline uint16_t* pxAt(struct topCanvas* c, int x, int y)
{
	return &c->px[x * TOP_H + (TOP_H - 1 - y)];
}

static inline void pset(struct topCanvas* c, int x, int y, uint16_t col)
{
	if((unsigned)x < TOP_W && (unsigned)y < TOP_H)
		*pxAt(c, x, y) = col;
}

static void fillRect(struct topCanvas* c, int x, int y, int w, int h,
                     uint16_t col)
{
	for(int i = x; i < x + w; i++)
		for(int j = y; j < y + h; j++)
			pset(c, i, j, col);
}

static void hline(struct topCanvas* c, int x, int y, int w, uint16_t col)
{
	fillRect(c, x, y, w, 1, col);
}

static void rect(struct topCanvas* c, int x, int y, int w, int h,
                 uint16_t col)
{
	fillRect(c, x, y, w, 1, col);
	fillRect(c, x, y + h - 1, w, 1, col);
	fillRect(c, x, y, 1, h, col);
	fillRect(c, x + w - 1, y, 1, h, col);
}

/* Draw the set bits of an 8x8 bitmap, scaled. */
static void bitmap8(struct topCanvas* c, int x, int y, const uint8_t* rows,
                    int scale, uint16_t col)
{
	for(int r = 0; r < 8; r++)
		for(int b = 0; b < 8; b++)
			if(rows[r] & (0x80 >> b))
				fillRect(c, x + b * scale, y + r * scale, scale, scale, col);
}

static void glyph(struct topCanvas* c, int x, int y, unsigned char ch,
                  uint16_t col)
{
	bitmap8(c, x, y, c->font + ch * 8, 1, col);
}

/* Draw n characters of s. */
static void textN(struct topCanvas* c, int x, int y, const char* s, int n,
                  uint16_t col)
{
	for(int i = 0; i < n && s[i] != '\0'; i++)
		glyph(c, x + i * CH, y, (unsigned char)s[i], col);
}

static void text(struct topCanvas* c, int x, int y, const char* s,
                 uint16_t col)
{
	textN(c, x, y, s, (int)strlen(s), col);
}

/* Draw s in at most maxChars, ending in "..." if it doesn't fit. */
static void textFit(struct topCanvas* c, int x, int y, const char* s,
                    int maxChars, uint16_t col)
{
	int n = (int)strlen(s);

	if(n <= maxChars || maxChars <= 3)
	{
		textN(c, x, y, s, maxChars, col);
		return;
	}

	textN(c, x, y, s, maxChars - 3, col);
	text(c, x + (maxChars - 3) * CH, y, "...", col);
}

static void textRight(struct topCanvas* c, int xEnd, int y, const char* s,
                      uint16_t col)
{
	text(c, xEnd - (int)strlen(s) * CH, y, s, col);
}

/* Centre s (truncated to maxChars) in [x, x + w). */
static void textCenter(struct topCanvas* c, int x, int w, int y,
                       const char* s, int maxChars, uint16_t col)
{
	int n = (int)strlen(s);

	if(n > maxChars)
		n = maxChars;
	textFit(c, x + (w - n * CH) / 2, y, s, maxChars, col);
}

/*
 * Draw text in "inverse" over the progress bar: on bar-coloured pixels
 * the glyph is drawn in the background colour and vice versa, so it stays
 * readable over both the filled and the empty part.
 */
static void textInverse(struct topCanvas* c, int x, int y, const char* s,
                        const struct topColors* col)
{
	for(int i = 0; s[i] != '\0'; i++)
	{
		const uint8_t* g = c->font + (unsigned char)s[i] * 8;

		for(int r = 0; r < 8; r++)
			for(int b = 0; b < 8; b++)
			{
				int px = x + i * CH + b, py = y + r;

				if(!(g[r] & (0x80 >> b)) ||
				   (unsigned)px >= TOP_W || (unsigned)py >= TOP_H)
					continue;
				*pxAt(c, px, py) =
					*pxAt(c, px, py) == col->bar ? col->bg : col->bar;
			}
	}
}

/* RGB565 image, row-major with the given stride. */
static void blit(struct topCanvas* c, int x, int y, const uint16_t* px,
                 int w, int h, int stride)
{
	for(int j = 0; j < h; j++)
		for(int i = 0; i < w; i++)
			pset(c, x + i, y + j, px[j * stride + i]);
}

/*
 * Placeholder art: a vinyl record drawn in the theme's colours (grooves in
 * the dim colour, label in the accent colour, a bright rim and sheen).
 * It takes a sqrt, atan2 and sin per pixel, so it is rendered once per
 * set of colours and then just copied.
 */
static uint16_t				vinylPx[ART_IN * ART_IN];
static struct topColors		vinylCols;
static bool					vinylValid = false;

static void renderVinyl(const struct topColors* col)
{
	const int	size = ART_IN;
	const float	mid = (size - 1) / 2.0f;
	const float	rim = size * 0.47f;
	const float	label = rim * 0.36f;
	const float	hole = 2.5f;

	for(int j = 0; j < size; j++)
		for(int i = 0; i < size; i++)
		{
			float		dx = i - mid, dy = j - mid;
			float		r = sqrtf(dx * dx + dy * dy);
			uint16_t	px;

			if(r > rim || r < hole)
			{
				vinylPx[j * size + i] = col->bg;
				continue;
			}

			if(r < label)
			{
				/* Label, with a thin ring near its edge */
				px = (r > label - 4.0f && r < label - 3.0f) ? col->bg :
				     col->accent;
			}
			else if(r > rim - 1.5f)
				px = col->text;
			else
			{
				bool	groove = ((int)r % 4) == 0 && r > label + 3.0f;
				/* Two opposite light reflections across the grooves */
				float	sheen = sinf(2.0f * (atan2f(dy, dx) + 0.75f));

				px = groove ? col->bg : col->dim;
				if(sheen > 0.93f && !groove)
					px = col->text;
			}

			vinylPx[j * size + i] = px;
		}

	vinylCols = *col;
	vinylValid = true;
}

static void drawVinyl(struct topCanvas* c, const struct topColors* col,
                      int x0, int y0)
{
	if(!vinylValid || memcmp(&vinylCols, col, sizeof(*col)) != 0)
		renderVinyl(col);
	blit(c, x0, y0, vinylPx, ART_IN, ART_IN, ART_IN);
}

/* ------------------------------------------------------------------ */
/* Text layout helpers                                                 */
/* ------------------------------------------------------------------ */

/*
 * Where to break s (longer than `avail`) for a line of `avail` characters:
 * before a space, or after a '/' or '-' (for paths). 0 if there is no such
 * place, i.e. the first word alone doesn't fit.
 */
static int breakLen(const char* s, int avail)
{
	for(int i = avail; i > 0; i--)
	{
		if(s[i] == ' ')
			return i;
		if(s[i - 1] == '/' || s[i - 1] == '-')
			return i;
	}
	return 0;
}

/*
 * "label:value" in a side column (no space after the colon, to save
 * room; the label is in the dim colour), wrapped over at most maxLines
 * lines (the last one ends in "..." if it was cut). Continuation lines
 * start at the column's left edge, like the mockup. Returns the next
 * free y.
 */
static int field(struct topCanvas* c, const struct topColors* col, int x,
                 int y, const char* label, const char* value, int maxLines)
{
	int labelLen = (int)strlen(label);
	int avail = COL_CHARS - labelLen;
	int cx = x + labelLen * CH;
	const char* p = (value != NULL && value[0] != '\0') ? value : "-";

	if(y + CH > COL_YMAX)
		return y;

	text(c, x, y, label, col->dim);

	for(int line = 0; *p != '\0' && y + CH <= COL_YMAX; line++)
	{
		int seg;

		if((int)strlen(p) <= avail)
		{
			text(c, cx, y, p, col->text);
			y += LINE_H;
			break;
		}

		if(line == maxLines - 1 || y + LINE_H + CH > COL_YMAX)
		{
			textFit(c, cx, y, p, avail, col->text);
			y += LINE_H;
			break;
		}

		/* A word that doesn't fit after the label moves to the next
		 * line; one that doesn't fit on a whole line is cut. */
		seg = breakLen(p, avail);
		if(seg == 0 && cx == x)
			seg = avail;
		textN(c, cx, y, p, seg, col->text);
		p += seg;
		while(*p == ' ')
			p++;

		y += LINE_H;
		cx = x;
		avail = COL_CHARS;
	}

	return y;
}

/* Section header with an underline across the column. */
static int sectionHeader(struct topCanvas* c, const struct topColors* col,
                         bool right, int y, const char* s)
{
	if(right)
	{
		textRight(c, TOP_W - 2, y, s, col->accent);
		hline(c, RCOL_X - 3, y + CH + 1, TOP_W - (RCOL_X - 3), col->bar);
	}
	else
	{
		text(c, LCOL_X, y, s, col->accent);
		hline(c, 0, y + CH + 1, LCOL_X + COL_CHARS * CH + 3, col->bar);
	}
	return y + HEADER_H;
}

/* How many text rows fit from y down to the bottom of the columns. */
static int rowsFrom(int y)
{
	return y + CH > COL_YMAX ? 0 : (COL_YMAX - CH - y) / LINE_H + 1;
}

static void fmtTime(char* out, size_t len, long secs)
{
	if(secs < 0)
	{
		snprintf(out, len, "--:--:--");
		return;
	}
	snprintf(out, len, "%02ld:%02ld:%02ld",
	         (secs / 3600) % 100, (secs / 60) % 60, secs % 60);
}

/* ------------------------------------------------------------------ */
/* Screen sections                                                     */
/* ------------------------------------------------------------------ */

static void drawHeader(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v)
{
	char buf[16];

	/* Brightness */
	bitmap8(c, 1, HEADER_Y, iconSun, 1, col->text);
	if(v->brightnessPct < 0)
		snprintf(buf, sizeof(buf), "--%%");
	else
		snprintf(buf, sizeof(buf), "%d%%", v->brightnessPct);
	text(c, 11, HEADER_Y, buf, col->text);

	/* Clock */
	textCenter(c, 0, TOP_W, HEADER_Y, v->clock, 30, col->text);

	/* Battery: outline + nub, filled in proportion */
	if(v->batteryPct >= 0)
	{
		int pct = v->batteryPct > 100 ? 100 : v->batteryPct;
		int bx = TOP_W - 15, by = HEADER_Y;

		/* The fill touches the outline (no inner margin) */
		rect(c, bx, by, 12, 8, col->text);
		fillRect(c, bx + 12, by + 2, 2, 4, col->text);
		fillRect(c, bx + 1, by + 1, (10 * pct + 50) / 100, 6, col->text);

		snprintf(buf, sizeof(buf), "%d%%", pct);
		textRight(c, bx - 3, HEADER_Y, buf, col->text);
	}
}

/*
 * One list section of the left column: header, then up to `rows` entries.
 * If `total` items don't fit, the last row says "+N more". Entry
 * `current` (or -1) is highlighted. Returns the next free y.
 */
/* Entries a section draws in `rows` rows (one goes to "+N more"). */
static int sectionEntries(int shown, int total, int rows, bool* more)
{
	int n = total;

	*more = total > rows;
	if(*more)
		n = rows - 1;
	if(n > shown)
		n = shown;
	return n < 0 ? 0 : n;
}

static int listSection(struct topCanvas* c, const struct topColors* col,
                       int y, const char* header, const char* const* items,
                       int shown, int total, int rows, int current)
{
	char	buf[24];
	bool	more;
	int		n = sectionEntries(shown, total, rows, &more);

	y = sectionHeader(c, col, false, y, header);

	if(total == 0)
	{
		text(c, LCOL_X, y, "(empty)", col->dim);
		return y + LINE_H;
	}

	for(int i = 0; i < n; i++, y += LINE_H)
	{
		if(i == current)
		{
			/* Current song: inverted bar */
			fillRect(c, 0, y - 1, LCOL_X + COL_CHARS * CH + 3, LINE_H,
			         col->accent);
			textFit(c, LCOL_X, y, items[i], COL_CHARS, col->bg);
		}
		else
			textFit(c, LCOL_X, y, items[i], COL_CHARS, col->text);
	}

	if(more)
	{
		snprintf(buf, sizeof(buf), "+%d more", total - n);
		text(c, LCOL_X, y, buf, col->dim);
		y += LINE_H;
	}

	return y;
}

/*
 * Queue and playlist share the column dynamically: no queue section when
 * the queue is empty; both when the queue is short enough to leave the
 * playlist a few rows; otherwise just the queue (the playlist doesn't
 * matter until the queue has played).
 */
struct leftLayout
{
	bool	showQueue;
	bool	showList;
	int		queueRows;	/* rows given to the queue */
	int		listY;		/* where the playlist section starts */
	int		listRows;	/* rows left for the playlist */
};

static void leftLayout(const struct npView* v, struct leftLayout* l)
{
	l->showQueue = v->queueCount > 0;
	l->showList = true;
	l->queueRows = 0;
	l->listY = COL_TOP;

	if(l->showQueue)
	{
		int		queueEnd = COL_TOP + HEADER_H + v->queueCount * LINE_H;
		bool	more;
		int		n;

		if(v->playlistShown == 0 ||
		   rowsFrom(queueEnd + SECTION_GAP + HEADER_H) < MIN_PLAYLIST_ROWS)
		{
			l->showList = false;
			l->queueRows = rowsFrom(COL_TOP + HEADER_H);
		}
		else
			l->queueRows = v->queueCount;

		n = sectionEntries(v->queueShown, v->queueCount, l->queueRows,
		                   &more);
		l->listY = COL_TOP + HEADER_H + (n + (more ? 1 : 0)) * LINE_H +
		           SECTION_GAP;
	}

	l->listRows = rowsFrom(l->listY + HEADER_H);
}

void topListRows(const struct npView* v, int* queueRows, int* playlistRows)
{
	struct leftLayout	l;
	bool				more;

	leftLayout(v, &l);
	*queueRows = l.showQueue ?
	             sectionEntries(v->queueShown, v->queueCount, l.queueRows,
	                            &more) : 0;
	*playlistRows = l.showList ?
	                sectionEntries(v->playlistShown, v->playlistShown,
	                               l.listRows, &more) : 0;
}

static void drawLeftColumn(struct topCanvas* c, const struct topColors* col,
                           const struct npView* v)
{
	char				queueHdr[24], listHdr[24];
	struct leftLayout	l;

	snprintf(queueHdr, sizeof(queueHdr), "queued (%d)", v->queueCount);
	snprintf(listHdr, sizeof(listHdr), "playlist (%d)", v->playlistCount);

	leftLayout(v, &l);

	if(l.showQueue)
		listSection(c, col, COL_TOP, queueHdr, v->queue, v->queueShown,
		            v->queueCount, l.queueRows, -1);

	if(l.showList)
		listSection(c, col, l.listY, listHdr, v->playlist, v->playlistShown,
		            v->playlistShown, l.listRows, v->playlistCurrent);
}

/* Character offset of a scrolling line at time ms (0 if it fits). */
static int scrollOffset(const char* s, unsigned long ms)
{
	int				n = (int)strlen(s);
	int				loop = n + SCROLL_GAP;
	unsigned long	period, t;

	if(n <= CENTER_CHARS)
		return 0;

	period = SCROLL_HOLD_MS + (unsigned long)(loop - 1) * SCROLL_STEP_MS;
	t = ms % period;
	if(t < SCROLL_HOLD_MS)
		return 0;
	return 1 + (int)((t - SCROLL_HOLD_MS) / SCROLL_STEP_MS);
}

/* A centred line that scrolls (from `offset`) when it is too long, or
 * is cut with "..." if offset < 0. */
static void scrollLine(struct topCanvas* c, int y, const char* s,
                       int offset, uint16_t col)
{
	int n = (int)strlen(s);
	int loop = n + SCROLL_GAP;

	if(n <= CENTER_CHARS || offset < 0)
	{
		textCenter(c, ART_FX, ART_FW, y, s, CENTER_CHARS, col);
		return;
	}

	for(int i = 0; i < CENTER_CHARS; i++)
	{
		int k = (offset + i) % loop;

		if(k < n)
			glyph(c, ART_FX + (ART_FW - CENTER_CHARS * CH) / 2 + i * CH, y,
			      (unsigned char)s[k], col);
	}
}

/* Scroll positions the title lines were last drawn at. */
static int titleOffset = -1;
static int subOffset = -1;

static void drawTitles(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v)
{
	titleOffset = v->noScroll ? -1 : scrollOffset(v->title, v->scrollMs);
	subOffset = v->noScroll ? -1 : scrollOffset(v->subtitle, v->scrollMs);
	scrollLine(c, TITLE_Y, v->title, titleOffset, col->text);
	scrollLine(c, SUB_Y, v->subtitle, subOffset, col->dim);
}

bool topUpdateScroll(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v)
{
	if(!v->playing || v->noScroll ||
	   (scrollOffset(v->title, v->scrollMs) == titleOffset &&
	                   scrollOffset(v->subtitle, v->scrollMs) == subOffset))
		return false;

	fillRect(c, ART_FX, TITLE_Y, ART_FW, SUB_Y + CH - TITLE_Y, col->bg);
	drawTitles(c, col, v);
	return true;
}

/* "<speaker> 62%", centred above the progress bar (muted icon at 0). */
static void drawVolume(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v)
{
	char	buf[16];
	int		w, x;

	if(v->volumePct < 0)
		return;

	snprintf(buf, sizeof(buf), "%d%%", v->volumePct);
	w = (2 + (int)strlen(buf)) * CH;	/* icon, space, text */
	x = (TOP_W - w) / 2;
	bitmap8(c, x, VOL_Y, v->volumePct == 0 ? iconMuted : iconVolume, 1,
	        v->volumePct == 0 ? col->dim : col->text);
	text(c, x + 2 * CH, VOL_Y, buf, v->volumePct == 0 ? col->dim : col->text);
}

void topUpdateVolume(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v)
{
	fillRect(c, ART_FX, VOL_Y, ART_FW, CH, col->bg);
	drawVolume(c, col, v);
}

/* ------------------------------------------------------------------ */
/* Spinning disc                                                       */
/* ------------------------------------------------------------------ */

/*
 * The art is cut to a circle (cropped to a square first, if it isn't
 * one) with a hub in the middle and an outline. Rotating it with
 * smoothing costs ~4 reads per pixel, so the 16 rotations from 0 to 84
 * degrees are rendered once per song (one per frame, topDiscWork) and
 * kept: a step in the other quarters is one of these read 90/180/270
 * degrees round, which is just a different pixel order. ~650 KB.
 */
#define DISC_FRAMES		(TOP_DISC_STEPS / 4)
#define DISC_N			ART_IN
#define DISC_MID		((DISC_N - 1) / 2.0f)
#define DISC_R			(DISC_N / 2.0f)
#define DISC_HOLE_R		4.0f
#define DISC_HUB_R		7.0f
#define DISC_MAX_OUTLINE	1200

static uint16_t*	discPx = NULL;		/* DISC_FRAMES frames */
static int			discReady = 0;		/* frames rendered */
static unsigned		discArtId = 0;
static bool			discHaveArt = false;
static bool			discInit = false;
static int16_t		discSpan[DISC_N][2];	/* inside the circle, per row */
static uint8_t		discOutline[DISC_MAX_OUTLINE][2];
static int			discOutlineCount = 0;
static int			lastDiscStep = -1;

static void discInitTables(void)
{
	for(int j = 0; j < DISC_N; j++)
	{
		float dy = j - DISC_MID;

		discSpan[j][0] = DISC_N;
		discSpan[j][1] = -1;
		for(int i = 0; i < DISC_N; i++)
		{
			float dx = i - DISC_MID;
			float r2 = dx * dx + dy * dy;

			if(r2 > DISC_R * DISC_R)
				continue;
			if(i < discSpan[j][0])
				discSpan[j][0] = (int16_t)i;
			discSpan[j][1] = (int16_t)i;
			if(r2 > (DISC_R - 1.2f) * (DISC_R - 1.2f) &&
			   discOutlineCount < DISC_MAX_OUTLINE)
			{
				discOutline[discOutlineCount][0] = (uint8_t)i;
				discOutline[discOutlineCount][1] = (uint8_t)j;
				discOutlineCount++;
			}
		}
	}
	discInit = true;
}

/* Bilinear sample of the art at (x, y), in source pixels. */
static uint16_t sampleArt(const struct npView* v, float x, float y)
{
	int		x0, y0, fx, fy, r = 0, g = 0, b = 0;
	const uint16_t* p;

	if(x < 0.0f) x = 0.0f;
	if(y < 0.0f) y = 0.0f;
	if(x > v->artW - 1.001f) x = v->artW - 1.001f;
	if(y > v->artH - 1.001f) y = v->artH - 1.001f;
	x0 = (int)x;
	y0 = (int)y;
	fx = (int)((x - x0) * 256.0f);
	fy = (int)((y - y0) * 256.0f);
	p = v->artPixels + y0 * TOP_ART_SIZE + x0;

	{
		const uint16_t	q[4] = { p[0], p[1], p[TOP_ART_SIZE],
		                         p[TOP_ART_SIZE + 1] };
		const int		w[4] = { (256 - fx) * (256 - fy), fx * (256 - fy),
		                         (256 - fx) * fy, fx * fy };

		for(int k = 0; k < 4; k++)
		{
			r += ((q[k] >> 11) & 31) * w[k];
			g += ((q[k] >> 5) & 63) * w[k];
			b += (q[k] & 31) * w[k];
		}
	}
	return (uint16_t)(((r >> 16) << 11) | ((g >> 16) << 5) | (b >> 16));
}

/* Frame k: the art turned k steps clockwise, inside the circle only. */
static void discRender(const struct npView* v, int k)
{
	uint16_t*	out = discPx + (size_t)k * DISC_N * DISC_N;
	float		a = (float)k * 2.0f * (float)M_PI / TOP_DISC_STEPS;
	float		cs = cosf(a), sn = sinf(a);
	int			side = v->artW < v->artH ? v->artW : v->artH;
	float		scale = (float)side / DISC_N;
	float		cx = (v->artW - 1) / 2.0f, cy = (v->artH - 1) / 2.0f;

	for(int j = 0; j < DISC_N; j++)
		for(int i = discSpan[j][0]; i <= discSpan[j][1]; i++)
		{
			float u = i - DISC_MID, w = j - DISC_MID;

			/* Clockwise on screen (y down) = sample counter-clockwise */
			out[j * DISC_N + i] = sampleArt(v, cx + (cs * u + sn * w) * scale,
			                                cy + (-sn * u + cs * w) * scale);
		}
}

/* New art (or none): start the frames again. */
static bool discSync(const struct npView* v)
{
	if(!discInit)
		discInitTables();
	if(v->artPixels == NULL || v->artW < 2 || v->artH < 2)
	{
		discHaveArt = false;
		return false;
	}
	if(discPx == NULL &&
	   (discPx = calloc((size_t)DISC_FRAMES * DISC_N * DISC_N,
	                    sizeof(uint16_t))) == NULL)
		return false;
	if(!discHaveArt || v->artId != discArtId)
	{
		discHaveArt = true;
		discArtId = v->artId;
		discReady = 0;
	}
	return true;
}

bool topDiscWork(const struct npView* v)
{
	if(!v->disc || v->artLoading || !discSync(v) ||
	   discReady >= DISC_FRAMES)
		return false;
	discRender(v, discReady++);
	return true;
}

static void drawDiscArt(struct topCanvas* c, const struct topColors* col,
                        const struct npView* v, int x0, int y0)
{
	int				q = v->discStep / DISC_FRAMES % 4;
	int				k = v->discStep % DISC_FRAMES;
	const uint16_t*	f;
	const int		n1 = DISC_N - 1;

	if(discReady == 0)
		discRender(v, discReady++);
	if(k >= discReady)
		k = discReady - 1;		/* not rendered yet: the nearest one */
	f = discPx + (size_t)k * DISC_N * DISC_N;

	for(int j = 0; j < DISC_N; j++)
		for(int i = discSpan[j][0]; i <= discSpan[j][1]; i++)
		{
			int si, sj;

			switch(q)	/* a further q quarter turns clockwise */
			{
				case 1:		si = j;			sj = n1 - i;	break;
				case 2:		si = n1 - i;	sj = n1 - j;	break;
				case 3:		si = n1 - j;	sj = i;			break;
				default:	si = i;			sj = j;			break;
			}
			*pxAt(c, x0 + i, y0 + j) = f[sj * DISC_N + si];
		}

	/* Hub: a hole with a ring round it, then the outline. */
	for(int j = (int)(DISC_MID - DISC_HUB_R); j <= DISC_MID + DISC_HUB_R; j++)
		for(int i = (int)(DISC_MID - DISC_HUB_R); i <= DISC_MID + DISC_HUB_R;
		    i++)
		{
			float dx = i - DISC_MID, dy = j - DISC_MID;
			float r2 = dx * dx + dy * dy;

			if(r2 <= DISC_HOLE_R * DISC_HOLE_R)
				pset(c, x0 + i, y0 + j, col->bg);
			else if(r2 <= DISC_HUB_R * DISC_HUB_R)
				pset(c, x0 + i, y0 + j, col->bar);
		}
	for(int p = 0; p < discOutlineCount; p++)
		pset(c, x0 + discOutline[p][0], y0 + discOutline[p][1], col->bar);
}

/* The vinyl placeholder is already round and its sheen shouldn't turn
 * (the light doesn't), so only a mark on its label does. */
static void drawVinylMark(struct topCanvas* c, const struct topColors* col,
                          int x0, int y0, int step)
{
	float a = (float)step * 2.0f * (float)M_PI / TOP_DISC_STEPS -
	          (float)M_PI / 2.0f;
	float cs = cosf(a), sn = sinf(a);

	for(float r = 8.0f; r <= 19.0f; r += 0.5f)
	{
		int x = x0 + (int)lrintf(DISC_MID + cs * r);
		int y = y0 + (int)lrintf(DISC_MID + sn * r);

		fillRect(c, x, y, 2, 2, col->bg);
	}
}

/* Everything inside the art box, round version. */
static void drawDisc(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v)
{
	int x0 = ART_FX + 1, y0 = ART_FY + 1;

	if(!discInit)
		discInitTables();
	lastDiscStep = v->discStep;

	if(v->artLoading)
		return;		/* empty for the moment it takes to decode */
	if(discSync(v))
		drawDiscArt(c, col, v, x0, y0);
	else
	{
		drawVinyl(c, col, x0, y0);
		drawVinylMark(c, col, x0, y0, v->discStep);
	}
}

bool topUpdateDisc(struct topCanvas* c, const struct topColors* col,
                   const struct npView* v)
{
	if(!v->disc || v->discStep == lastDiscStep)
		return false;
	fillRect(c, ART_FX, ART_FY, ART_FW, ART_FW, col->bg);
	drawDisc(c, col, v);
	return true;
}

static void drawCenter(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v)
{
	if(v->disc)
		drawDisc(c, col, v);
	else
	{
		rect(c, ART_FX, ART_FY, ART_FW, ART_FW, col->bar);

		if(v->artLoading)
			;	/* Leave the box empty for the moment it takes to decode. */
		else if(v->artPixels != NULL)
			blit(c, ART_FX + 1 + (ART_IN - v->artW) / 2,
			     ART_FY + 1 + (ART_IN - v->artH) / 2, v->artPixels,
			     v->artW, v->artH, TOP_ART_SIZE);
		else
			drawVinyl(c, col, ART_FX + 1, ART_FY + 1);
	}

	if(v->playing)
		drawTitles(c, col, v);
	else
		textCenter(c, ART_FX, ART_FW, TITLE_Y, "nothing playing",
		           CENTER_CHARS, col->dim);

	drawVolume(c, col, v);
}

static void drawRightColumn(struct topCanvas* c, const struct topColors* col,
                            const struct npView* v)
{
	char buf[24];
	int y = COL_TOP;
	int x = RCOL_X;

	y = sectionHeader(c, col, true, y, "details");
	y = field(c, col, x, y, "title:", v->tagTitle, 3);
	y = field(c, col, x, y, "artist:", v->tagArtist, 2);
	y = field(c, col, x, y, "album:", v->tagAlbum, 2);
	y += SECTION_GAP;
	y = field(c, col, x, y, "size:", v->size, 1);
	y = field(c, col, x, y, "bitrate:", v->bitrate, 1);
	y = field(c, col, x, y, "format:", v->format, 1);

	y += SECTION_GAP;
	if(y + HEADER_H + CH > COL_YMAX)
		return;
	y = sectionHeader(c, col, true, y, "effects");

	snprintf(buf, sizeof(buf), "%.1fx", (double)v->speed);
	y = field(c, col, x, y, "speed:", buf, 1);
	if(v->distortionPct > 0)
		snprintf(buf, sizeof(buf), "%d%%", v->distortionPct);
	else
		snprintf(buf, sizeof(buf), "off");
	y = field(c, col, x, y, "distortion:", buf, 2);

	if(v->reverbMixPct <= 0)
		field(c, col, x, y, "reverb:", "off", 1);
	else if(y + 3 * LINE_H <= COL_YMAX + 1)
	{
		text(c, x, y, "reverb:", col->dim);
		snprintf(buf, sizeof(buf), "%d%% mix", v->reverbMixPct);
		text(c, x + 2 * CH, y + LINE_H, buf, col->text);
		snprintf(buf, sizeof(buf), "%.1fs decay", (double)v->reverbDecay);
		text(c, x + 2 * CH, y + 2 * LINE_H, buf, col->text);
	}
}

static void drawFooter(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v)
{
	static const char* const repeatStr[] = { "off", "all", "one" };
	char buf[16];

	/* Repeat */
	bitmap8(c, 1, FOOT_Y, iconRepeat, 1,
	        v->repeat == NP_REPEAT_OFF ? col->dim : col->text);
	text(c, 11, FOOT_Y, repeatStr[v->repeat],
	     v->repeat == NP_REPEAT_OFF ? col->dim : col->text);

	/* Times: elapsed, or the time left ("-0:12:34", same width) */
	if(v->playing && v->remaining && v->total > 0)
	{
		long left = v->total - v->elapsed;

		if(left < 0)
			left = 0;
		snprintf(buf, sizeof(buf), "-%ld:%02ld:%02ld", (left / 3600) % 10,
		         (left / 60) % 60, left % 60);
	}
	else
		fmtTime(buf, sizeof(buf), v->playing ? v->elapsed : -1);
	text(c, 40, FOOT_Y, buf, v->paused ? col->dim : col->text);
	fmtTime(buf, sizeof(buf), v->playing && v->total > 0 ? v->total : -1);
	text(c, BAR_X + BAR_W + 4, FOOT_Y, buf, col->text);

	/* Progress bar: outline, filled right up to it */
	rect(c, BAR_X, BAR_Y, BAR_W, BAR_H, col->bar);
	if(v->playing && v->total > 0)
	{
		long e = v->elapsed > v->total ? v->total : v->elapsed;
		int w = (int)((long long)(BAR_W - 2) * e / v->total);

		fillRect(c, BAR_X + 1, BAR_Y + 1, w, BAR_H - 2, col->bar);
	}
	if(v->playing && v->paused)
		textInverse(c, BAR_X + (BAR_W - 6 * CH) / 2, BAR_Y + 1, "paused",
		            col);

	/* Shuffle */
	bitmap8(c, TOP_W - 9, FOOT_Y, iconShuffle, 1,
	        v->shuffle ? col->text : col->dim);
	textRight(c, TOP_W - 12, FOOT_Y, v->shuffle ? "on" : "off",
	          v->shuffle ? col->text : col->dim);
}

void topUpdateFooter(struct topCanvas* c, const struct topColors* col,
                     const struct npView* v)
{
	fillRect(c, 0, BAR_Y, TOP_W, TOP_H - BAR_Y, col->bg);
	drawFooter(c, col, v);
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

static void clear(struct topCanvas* c, uint16_t col)
{
	for(int i = 0; i < TOP_W * TOP_H; i++)
		c->px[i] = col;
}

void topDrawNowPlaying(struct topCanvas* c, const struct topColors* col,
                       const struct npView* v)
{
	clear(c, col->bg);
	drawHeader(c, col, v);
	drawLeftColumn(c, col, v);
	drawCenter(c, col, v);
	drawRightColumn(c, col, v);
	drawFooter(c, col, v);
}

/* Splash: the logo centred above "Press A to start". The word and the
 * note bob up and down on a slow sine, the note (in front) a little
 * further and a little later, so it feels like it floats over the word. */
#define SPLASH_LOGO_X	((TOP_W - LOGO_W) / 2)
#define SPLASH_LOGO_Y	50
#define SPLASH_TEXT_Y	(SPLASH_LOGO_Y + LOGO_H + 24)
#define SPLASH_BOB_MS	2600	/* one up-and-down */
#define SPLASH_WORD_AMP	2		/* pixels each way */
#define SPLASH_NOTE_AMP	3
#define SPLASH_NOTE_LAG	0.7f	/* radians behind the word */

static int	splashWordDy = 0;
static int	splashNoteDy = 0;

static int splashBob(unsigned long ms, int amp, float lag)
{
	float t = (float)(ms % SPLASH_BOB_MS) / SPLASH_BOB_MS;

	return (int)floorf(amp * sinf(2.0f * (float)M_PI * t - lag) + 0.5f);
}

/* Draw one logo layer at (x0, y0) + its offset, skipping LOGO_KEY. */
static void drawLogoLayer(struct topCanvas* c, const struct logoLayer* l,
                          int x0, int y0)
{
	for(int i = 0; i < l->w; i++)
	{
		const uint16_t*	src = l->px + i * l->h;
		int				x = x0 + l->x + i;

		if((unsigned)x >= TOP_W)
			continue;
		for(int j = 0; j < l->h; j++)
		{
			int y = y0 + l->y + j;

			if(src[j] != LOGO_KEY && (unsigned)y < TOP_H)
				*pxAt(c, x, y) = src[j];
		}
	}
}

static void drawSplashLogo(struct topCanvas* c, const struct topColors* col)
{
	int amp = SPLASH_WORD_AMP > SPLASH_NOTE_AMP ? SPLASH_WORD_AMP :
	          SPLASH_NOTE_AMP;

	fillRect(c, SPLASH_LOGO_X, SPLASH_LOGO_Y - amp, LOGO_W,
	         LOGO_H + 2 * amp, col->bg);
	drawLogoLayer(c, &logoWord, SPLASH_LOGO_X,
	              SPLASH_LOGO_Y + splashWordDy);
	drawLogoLayer(c, &logoNote, SPLASH_LOGO_X,
	              SPLASH_LOGO_Y + splashNoteDy);
}

void topDrawSplash(struct topCanvas* c, const struct topColors* col,
                   const char* version, unsigned long ms)
{
	clear(c, col->bg);
	splashWordDy = splashBob(ms, SPLASH_WORD_AMP, 0.0f);
	splashNoteDy = splashBob(ms, SPLASH_NOTE_AMP, SPLASH_NOTE_LAG);
	drawSplashLogo(c, col);
	textCenter(c, 0, TOP_W, SPLASH_TEXT_Y, "Press A to start", 50,
	           col->text);
	textRight(c, TOP_W - 2, HEADER_Y, version, col->dim);
}

bool topUpdateSplash(struct topCanvas* c, const struct topColors* col,
                     unsigned long ms)
{
	int word = splashBob(ms, SPLASH_WORD_AMP, 0.0f);
	int note = splashBob(ms, SPLASH_NOTE_AMP, SPLASH_NOTE_LAG);

	if(word == splashWordDy && note == splashNoteDy)
		return false;
	splashWordDy = word;
	splashNoteDy = note;
	drawSplashLogo(c, col);
	return true;
}

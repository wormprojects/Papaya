/**
 * ctrmus - 3DS Music Player
 * Pixel-drawn bottom-screen pages (queue editor, search). See
 * bottomscreen.h.
 *
 * Layout (pixels, 320x240), shared by both pages:
 *   y 2    header: "queue (4)" / "search" in the accent colour, info on
 *          the right; a line under it at y 12 (like the top screen)
 *   y 16   the page
 *   y 228  key hints (dim bar, like the menu's)
 */

#include <stdio.h>
#include <string.h>

#include "bottomscreen.h"
#include "searchindex.h"

#define CH			8
#define HEAD_Y		2
#define LINE_Y		12
#define HINT_Y		228
#define HINT_H		(BOT_H - HINT_Y)

/* Queue editor */
#define QE_LIST_Y	16
#define QE_ROW_H	14
#define QE_STATUS_Y	190
#define QE_BTN_Y	203
#define QE_BTN_H	20
#define SB_X		313		/* scrollbar strip x..x+3 */
#define SB_W		4
#define SB_TOUCH_X	296		/* the stylus grabs it from here right */

/* Search */
#define SS_FIELD_X	4
#define SS_FIELD_Y	16
#define SS_FIELD_W	312
#define SS_FIELD_H	18
#define SS_LIST_Y	38
#define SS_ROW_H	13
#define SS_STATUS_Y	190
#define SS_BTN_Y	203
#define SS_BTN_H	20
#define SS_KB_X		6
#define SS_KB_Y		128
#define SS_KEY_W	28
#define SS_KEY_H	17
#define SS_KEY_GX	3
#define SS_KEY_GY	3

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                  */
/* ------------------------------------------------------------------ */

static inline void pset(struct botCanvas* c, int x, int y, uint16_t col)
{
	if((unsigned)x < BOT_W && (unsigned)y < BOT_H)
		c->px[x * BOT_H + (BOT_H - 1 - y)] = col;
}

static void fill(struct botCanvas* c, int x, int y, int w, int h,
                 uint16_t col)
{
	for(int i = x; i < x + w; i++)
		for(int j = y; j < y + h; j++)
			pset(c, i, j, col);
}

static void hline(struct botCanvas* c, int x, int y, int w, uint16_t col)
{
	fill(c, x, y, w, 1, col);
}

/* Filled box with its corners rounded off (radius 2). */
static void box(struct botCanvas* c, int x, int y, int w, int h,
                uint16_t col)
{
	fill(c, x + 2, y, w - 4, h, col);
	fill(c, x, y + 2, 2, h - 4, col);
	fill(c, x + w - 2, y + 2, 2, h - 4, col);
	pset(c, x + 1, y + 1, col);
	pset(c, x + w - 2, y + 1, col);
	pset(c, x + 1, y + h - 2, col);
	pset(c, x + w - 2, y + h - 2, col);
}

/* 1px outline with rounded corners. */
static void outline(struct botCanvas* c, int x, int y, int w, int h,
                    uint16_t col)
{
	hline(c, x + 2, y, w - 4, col);
	hline(c, x + 2, y + h - 1, w - 4, col);
	fill(c, x, y + 2, 1, h - 4, col);
	fill(c, x + w - 1, y + 2, 1, h - 4, col);
	pset(c, x + 1, y + 1, col);
	pset(c, x + w - 2, y + 1, col);
	pset(c, x + 1, y + h - 2, col);
	pset(c, x + w - 2, y + h - 2, col);
}

/* Mix two RGB565 colours: t = 0 is a, 256 is b. */
static uint16_t blend(uint16_t a, uint16_t b, int t)
{
	int r = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
	int g = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
	int bl = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;

	return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void glyph(struct botCanvas* c, int x, int y, unsigned char ch,
                  uint16_t col)
{
	const uint8_t* g = c->font + ch * 8;

	for(int j = 0; j < 8; j++)
		for(int i = 0; i < 8; i++)
			if(g[j] & (0x80 >> i))
				pset(c, x + i, y + j, col);
}

/* Up to `max` characters; a longer string ends in "..". Returns the x
 * after the last character. */
static int textN(struct botCanvas* c, int x, int y, const char* s, int max,
                 uint16_t col)
{
	int len = (int)strlen(s);

	if(max <= 0)
		return x;
	if(len > max)
	{
		for(int i = 0; i < max - 2; i++)
			glyph(c, x + i * CH, y, (unsigned char)s[i], col);
		glyph(c, x + (max - 2) * CH, y, '.', col);
		glyph(c, x + (max - 1) * CH, y, '.', col);
		return x + max * CH;
	}
	for(int i = 0; i < len; i++)
		glyph(c, x + i * CH, y, (unsigned char)s[i], col);
	return x + len * CH;
}

static int text(struct botCanvas* c, int x, int y, const char* s,
                uint16_t col)
{
	return textN(c, x, y, s, BOT_W / CH, col);
}

static void textRight(struct botCanvas* c, int xEnd, int y, const char* s,
                      uint16_t col)
{
	text(c, xEnd - (int)strlen(s) * CH, y, s, col);
}

/* A path that doesn't fit is cut from the left ("..ic/Album"): its end
 * matters most. "sdmc:" is dropped. */
static void pathText(struct botCanvas* c, int x, int y, const char* path,
                     int max, uint16_t col)
{
	int len;

	if(strncmp(path, "sdmc:", 5) == 0 && path[5] != '\0')
		path += 5;
	len = (int)strlen(path);
	if(len > max && max > 2)
	{
		x = text(c, x, y, "..", col);
		path += len - (max - 2);
	}
	text(c, x, y, path, col);
}

/* Colours derived from the theme */
struct palette
{
	uint16_t	bg, text, dim, bar, accent;
	uint16_t	surface;	/* buttons, keys: a little lighter/darker */
	uint16_t	surface2;	/* special keys */
};

static void palette(const struct topColors* col, struct palette* p)
{
	p->bg = col->bg;
	p->text = col->text;
	p->dim = col->dim;
	p->bar = col->bar;
	p->accent = col->accent;
	p->surface = blend(col->bg, col->text, 40);
	p->surface2 = blend(col->bg, col->dim, 90);
}

static void header(struct botCanvas* c, const struct palette* p,
                   const char* title, const char* info)
{
	text(c, 4, HEAD_Y, title, p->accent);
	if(info != NULL)
		textRight(c, BOT_W - 4, HEAD_Y, info, p->dim);
	hline(c, 0, LINE_Y, BOT_W, p->bar);
}

static void hints(struct botCanvas* c, const struct palette* p,
                  const char* s)
{
	fill(c, 0, HINT_Y, BOT_W, HINT_H, p->dim);
	text(c, 4, HINT_Y + 2, s, p->bg);
}

enum btnStyle
{
	BTN_NORMAL,
	BTN_FOCUS,
	BTN_OFF,
	BTN_OFF_FOCUS	/* can't be used now, but the D-pad is on it */
};

static void button(struct botCanvas* c, const struct palette* p, int x,
                   int y, int w, int h, const char* label,
                   enum btnStyle style)
{
	uint16_t	fg;
	int			tx = x + (w - (int)strlen(label) * CH) / 2;
	int			ty = y + (h - CH) / 2 + 1;

	switch(style)
	{
		case BTN_FOCUS:
			box(c, x, y, w, h, p->accent);
			fg = p->bg;
			break;
		case BTN_OFF:
			outline(c, x, y, w, h, p->surface);
			fg = p->surface2;
			break;
		case BTN_OFF_FOCUS:
			outline(c, x, y, w, h, p->accent);
			fg = p->surface2;
			break;
		default:
			box(c, x, y, w, h, p->surface);
			fg = p->text;
			break;
	}
	text(c, tx, ty, label, fg);
}

static void scrollbar(struct botCanvas* c, const struct palette* p, int y,
                      int h, int total, int shown, int top)
{
	int len, pos, maxTop = total - shown;

	if(total <= shown)
		return;
	len = h * shown / total;
	if(len < 6)
		len = 6;
	pos = maxTop > 0 ? (h - len) * top / maxTop : 0;
	box(c, SB_X, y, SB_W, h, p->surface);
	box(c, SB_X, y + pos, SB_W, len, p->accent);
}

/* ------------------------------------------------------------------ */
/* Queue editor                                                        */
/* ------------------------------------------------------------------ */

struct btn
{
	int			x, w;
	const char*	label;
};

static const struct btn qeButtons[QE_BTN_COUNT] =
{
	{ 6, 64, "\x10 Play" },
	{ 74, 32, "\x1e" },
	{ 110, 32, "\x1f" },
	{ 146, 72, "Remove" },
	{ 222, 92, "Clear all" },
};

static const struct btn qeConfirmButtons[QE_CONFIRM_COUNT] =
{
	{ 178, 64, "Clear" },
	{ 246, 68, "Keep" },
};

static bool qeHasScrollbar(const struct qeView* v)
{
	return v->count > QE_ROWS;
}

static void qeRow(struct botCanvas* c, const struct palette* p,
                  const struct qeView* v, int i)
{
	int			n = v->top + i;
	int			y = QE_LIST_Y + i * QE_ROW_H;
	int			right = qeHasScrollbar(v) ? SB_X - 4 : BOT_W - 3;
	bool		sel = n == v->selected && !v->buttonsFocus;
	bool		lifted = n == v->selected && v->lifted;
	bool		playlist = n >= v->qCount;	/* not queued: the playlist */
	const char*	label = v->labels[i] != NULL ? v->labels[i] : "";
	char		num[16];
	uint16_t	fg = playlist ? p->dim : p->text, numCol = p->dim;
	int			x = 3, chars;

	snprintf(num, sizeof(num), "%2d", n + 1);

	/* A line where the playlist starts */
	if(n == v->qCount && n > 0)
		hline(c, 3, y, right - 3, p->dim);

	if(lifted)
	{
		/* Picked up: lifted off the list a little, with a shadow */
		x += 4;
		hline(c, x + 2, y + QE_ROW_H - 1, right - x - 2, p->dim);
		box(c, x, y, right - x, QE_ROW_H - 1, p->bar);
		fg = numCol = p->bg;
		glyph(c, x + 4, y + 3, 0xF0, fg);	/* grip */
		glyph(c, right - 22, y + 3, 0x1E, fg);
		glyph(c, right - 13, y + 3, 0x1F, fg);
		chars = (right - 26 - (x + 28)) / CH - 1;
		textN(c, x + 28, y + 3, label, chars, fg);
		return;
	}

	if(sel)
	{
		box(c, x, y, right - x, QE_ROW_H - 1, p->accent);
		fg = numCol = p->bg;
	}
	else if(n == v->selected)
		outline(c, x, y, right - x, QE_ROW_H - 1, p->accent);

	text(c, x + 3, y + 3, num, numCol);
	chars = (right - (x + 28) - 4) / CH;
	if(n == 0 && !sel)
		chars -= 5;		/* room for "next" */
	textN(c, x + 28, y + 3, label, chars, fg);
	if(n == 0 && !sel)
		textRight(c, right - 4, y + 3, "next", p->dim);
}

void qeDraw(struct botCanvas* c, const struct topColors* col,
            const struct qeView* v)
{
	struct palette	p;
	char			title[48], info[24];
	int				shown = v->count - v->top;

	palette(col, &p);
	fill(c, 0, 0, BOT_W, BOT_H, p.bg);

	if(v->count > v->qCount)
		snprintf(title, sizeof(title), "queue (%d) + %d next", v->qCount,
		         v->count - v->qCount);
	else
		snprintf(title, sizeof(title), "queue (%d)", v->qCount);
	snprintf(info, sizeof(info), "%d left", v->max - v->qCount);
	header(c, &p, title, v->qCount > 0 ? info : NULL);

	if(v->count == 0)
	{
		static const char l1[] = "The queue is empty.";
		static const char l2[] = "Y in the browser queues a song,";
		static const char l3[] = "or Y on a search result.";

		text(c, (BOT_W - (int)(sizeof(l1) - 1) * CH) / 2, 80, l1, p.text);
		text(c, (BOT_W - (int)(sizeof(l2) - 1) * CH) / 2, 100, l2, p.dim);
		text(c, (BOT_W - (int)(sizeof(l3) - 1) * CH) / 2, 112, l3, p.dim);
	}

	if(shown > QE_ROWS)
		shown = QE_ROWS;
	for(int i = 0; i < shown; i++)
		qeRow(c, &p, v, i);
	if(qeHasScrollbar(v))
		scrollbar(c, &p, QE_LIST_Y, QE_ROWS * QE_ROW_H - 1, v->count,
		          QE_ROWS, v->top);

	if(v->status != NULL && v->status[0] != '\0' && !v->confirm)
		pathText(c, text(c, 6, QE_STATUS_Y, "in ", p.dim), QE_STATUS_Y,
		         v->status, 35, p.dim);

	if(v->confirm)
	{
		char q[40];

		snprintf(q, sizeof(q), "Clear all %d?", v->qCount);
		text(c, 8, QE_BTN_Y + 7, q, p.text);
		for(int i = 0; i < QE_CONFIRM_COUNT; i++)
			button(c, &p, qeConfirmButtons[i].x, QE_BTN_Y,
			       qeConfirmButtons[i].w, QE_BTN_H,
			       qeConfirmButtons[i].label,
			       i == v->button ? BTN_FOCUS : BTN_NORMAL);
		hints(c, &p, " A choose  B keep them  \x1b\x1a pick");
		return;
	}

	for(int i = 0; i < QE_BTN_COUNT; i++)
	{
		enum btnStyle	s = BTN_NORMAL;
		bool			usable = v->count > 0 &&
		                         (i == QE_BTN_CLEAR || v->selected >= 0);
		bool			inQueue = v->selected >= 0 && v->selected < v->qCount;
		int				lo = inQueue ? 0 : v->qCount;
		int				hi = inQueue ? v->qCount - 1 : v->count - 1;

		/* Songs move within their own list; only queued ones go away */
		if(i == QE_BTN_UP && v->selected <= lo)
			usable = false;
		if(i == QE_BTN_DOWN && v->selected >= hi)
			usable = false;
		if(i == QE_BTN_REMOVE && !inQueue)
			usable = false;
		if(i == QE_BTN_CLEAR)
			usable = v->qCount > 0;
		if(v->buttonsFocus && i == v->button)
			s = usable ? BTN_FOCUS : BTN_OFF_FOCUS;
		else if(!usable)
			s = BTN_OFF;
		button(c, &p, qeButtons[i].x, QE_BTN_Y, qeButtons[i].w, QE_BTN_H,
		       qeButtons[i].label, s);
	}

	if(v->lifted)
		hints(c, &p, " \x18\x19 move it  A/\x1b drop  B put back");
	else if(v->buttonsFocus)
		hints(c, &p, " A press  \x1b\x1a pick  \x18 songs  B back");
	else if(v->count == 0)
		hints(c, &p, " B back  SELECT close");
	else if(v->selected >= v->qCount)
		hints(c, &p, " A play  \x1a lift  B back");
	else
		hints(c, &p, " A play  Y remove  \x1a lift  B back");
}

int qeRowAt(int x, int y)
{
	(void)x;
	if(y < QE_LIST_Y || y >= QE_LIST_Y + QE_ROWS * QE_ROW_H)
		return -1;
	return (y - QE_LIST_Y) / QE_ROW_H;
}

int qeRowAtClamped(int y)
{
	if(y < QE_LIST_Y)
		return -1;
	if(y >= QE_LIST_Y + QE_ROWS * QE_ROW_H)
		return QE_ROWS;
	return (y - QE_LIST_Y) / QE_ROW_H;
}

bool qeOnScrollbar(const struct qeView* v, int x, int y)
{
	return qeHasScrollbar(v) && x >= SB_TOUCH_X && y >= QE_LIST_Y &&
	       y < QE_LIST_Y + QE_ROWS * QE_ROW_H;
}

float qeScrollFraction(int y)
{
	float f = (float)(y - QE_LIST_Y) / (float)(QE_ROWS * QE_ROW_H - 1);

	return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

static int buttonAt(const struct btn* b, int n, int by, int bh, int x, int y)
{
	if(y < by - 2 || y >= by + bh + 2)
		return -1;
	for(int i = 0; i < n; i++)
		if(x >= b[i].x - 2 && x < b[i].x + b[i].w + 2)
			return i;
	return -1;
}

int qeButtonAt(int x, int y, bool confirm)
{
	return confirm ?
	       buttonAt(qeConfirmButtons, QE_CONFIRM_COUNT, QE_BTN_Y, QE_BTN_H,
	                x, y) :
	       buttonAt(qeButtons, QE_BTN_COUNT, QE_BTN_Y, QE_BTN_H, x, y);
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

static const char* const kbChars[SS_KB_ROWS - 1] =
{
	"1234567890",
	"qwertyuiop",
	"asdfghjkl'",
	"zxcvbnm-.&",
};

static const int kbLast[SS_KB_COLS] =
{
	' ', ' ', ' ', ' ', ' ', SS_KEY_DEL, SS_KEY_DEL, SS_KEY_CLEAR,
	SS_KEY_DONE, SS_KEY_DONE
};

static const struct btn ssButtons[SS_BTN_COUNT] =
{
	{ 6, 68, "\x10 Play" },
	{ 78, 76, "+ Queue" },
	{ 158, 100, "Open folder" },
	{ 262, 52, "Type" },
};

int ssKeyCode(int row, int col)
{
	if(row < 0 || row >= SS_KB_ROWS || col < 0 || col >= SS_KB_COLS)
		return -1;
	if(row == SS_KB_ROWS - 1)
		return kbLast[col];
	return (unsigned char)kbChars[row][col];
}

static const char* keyLabel(int code)
{
	static char one[2];

	switch(code)
	{
		case ' ':			return "space";
		case SS_KEY_DEL:	return "\x1b del";
		case SS_KEY_CLEAR:	return "clr";
		case SS_KEY_DONE:	return "done";
		default:
			one[0] = (char)code;
			one[1] = '\0';
			return one;
	}
}

static void keyboard(struct botCanvas* c, const struct palette* p,
                     const struct ssView* v)
{
	for(int r = 0; r < SS_KB_ROWS; r++)
	{
		int y = SS_KB_Y + r * (SS_KEY_H + SS_KEY_GY);

		for(int col = 0; col < SS_KB_COLS; )
		{
			int			code = ssKeyCode(r, col);
			int			end = col;
			int			x = SS_KB_X + col * (SS_KEY_W + SS_KEY_GX);
			int			w;
			bool		cursor;
			uint16_t	face, fg;
			const char*	label = keyLabel(code);

			while(end + 1 < SS_KB_COLS && ssKeyCode(r, end + 1) == code)
				end++;
			w = (end - col + 1) * SS_KEY_W + (end - col) * SS_KEY_GX;
			cursor = v->keyRow == r && v->keyCol >= col &&
			         v->keyCol <= end;

			if(v->pressedKey == code)
			{
				face = p->text;
				fg = p->bg;
			}
			else if(cursor)
			{
				face = p->accent;
				fg = p->bg;
			}
			else if(code == SS_KEY_DONE)
			{
				face = p->bar;
				fg = p->bg;
			}
			else if(r == SS_KB_ROWS - 1)
			{
				face = p->surface2;
				fg = p->text;
			}
			else
			{
				face = p->surface;
				fg = p->text;
			}

			box(c, x, y, w, SS_KEY_H, face);
			text(c, x + (w - (int)strlen(label) * CH) / 2,
			     y + (SS_KEY_H - CH) / 2 + 1, label, fg);
			col = end + 1;
		}
	}
}

static int ssRowsShown(const struct ssView* v)
{
	return v->typing ? SS_ROWS_TYPING : SS_ROWS_LIST;
}

static bool ssHasScrollbar(const struct ssView* v)
{
	return v->listed > ssRowsShown(v);
}

/* A result: the name with the matched words in another colour, then
 * its folder (dim) if there's room. */
static void resultRow(struct botCanvas* c, const struct palette* p,
                      const struct ssView* v, int i)
{
	int			y = SS_LIST_Y + i * SS_ROW_H;
	int			right = ssHasScrollbar(v) ? SB_X - 4 : BOT_W - 3;
	bool		sel = v->top + i == v->selected && !v->typing &&
	                  !v->buttonsFocus;
	const char*	name = v->names[i];
	const char*	folder = v->folders[i];
	int			len = (int)strlen(name);
	int			x = 3 + 5;
	int			room = (right - x - 4) / CH;
	bool		mark[256];
	uint16_t	fg = sel ? p->bg : p->text;
	uint16_t	hi = sel ? p->bg : p->bar;
	bool		badge = i == v->badgeRow && v->badge != NULL;

	if(sel)
		box(c, 3, y, right - 3, SS_ROW_H - 1, p->accent);
	else if(v->top + i == v->selected && !v->typing)
		outline(c, 3, y, right - 3, SS_ROW_H - 1, p->accent);

	if(badge)
		room -= (int)strlen(v->badge) + 2;

	/* Which characters a query word matched (first place each) */
	memset(mark, 0, sizeof(mark));
	for(int w = 0; w < v->wordCount; w++)
	{
		int at = searchFind(name, v->words[w]);
		int wl = (int)strlen(v->words[w]);

		for(int k = at; at >= 0 && k < at + wl && k < 256; k++)
			mark[k] = true;
	}

	{
		int shownChars = len > room ? room - 2 : len;

		for(int k = 0; k < shownChars && k < 256; k++)
		{
			glyph(c, x + k * CH, y + 3, (unsigned char)name[k],
			      mark[k] ? hi : fg);
			/* Underlined too: some themes' bar colour is the text's */
			if(mark[k])
				hline(c, x + k * CH, y + 11, CH, hi);
		}
		if(len > room)
		{
			glyph(c, x + shownChars * CH, y + 3, '.', fg);
			glyph(c, x + (shownChars + 1) * CH, y + 3, '.', fg);
		}
		else if(folder != NULL && folder[0] != '\0' &&
		        room - len >= 12)
		{
			/* "  Album/Disc 1", cut from the left if needed */
			int		fx = x + (len + 2) * CH;
			int		fr = room - len - 2;
			uint16_t	fc = sel ? blend(p->accent, p->bg, 150) : p->dim;

			pathText(c, fx, y + 3, folder, fr, fc);
		}
	}

	if(badge)
		textRight(c, right - 4, y + 3, v->badge, sel ? p->bg : p->bar);
}

void ssDraw(struct botCanvas* c, const struct topColors* col,
            const struct ssView* v)
{
	struct palette	p;
	char			info[40];
	int				rows = ssRowsShown(v);

	palette(col, &p);
	fill(c, 0, 0, BOT_W, BOT_H, p.bg);

	/* Header: "search" + the folder, and how many found */
	if(v->scanning)
		snprintf(info, sizeof(info), "scanning %d", v->scanned);
	else if(v->wordCount == 0)
		snprintf(info, sizeof(info), "%d files%s", v->scanned,
		         v->truncated ? "+" : "");
	else
		snprintf(info, sizeof(info), "%d found", v->found);
	text(c, 4, HEAD_Y, "search", p.accent);
	pathText(c, 4 + 7 * CH, HEAD_Y, v->root,
	         (BOT_W - 8) / CH - 7 - (int)strlen(info) - 1, p.dim);
	textRight(c, BOT_W - 4, HEAD_Y, info, p.dim);
	hline(c, 0, LINE_Y, BOT_W, p.bar);

	/* Query field */
	{
		int			maxChars = (SS_FIELD_W - 16) / CH - 1;
		int			qlen = (int)strlen(v->query);
		const char*	q = v->query;
		int			x = SS_FIELD_X + 8;
		int			ty = SS_FIELD_Y + 6;

		if(v->typing)
			outline(c, SS_FIELD_X, SS_FIELD_Y, SS_FIELD_W, SS_FIELD_H,
			        p.accent);
		else
			box(c, SS_FIELD_X, SS_FIELD_Y, SS_FIELD_W, SS_FIELD_H,
			    p.surface);
		if(qlen > maxChars)
			q += qlen - maxChars;
		if(qlen == 0)
			text(c, x, ty, v->typing ? "type part of a filename" :
			     "tap here to type", p.dim);
		else
			x = text(c, x, ty, q, p.text);
		if(v->typing && v->caret)
			fill(c, qlen == 0 ? x : x + 1, ty - 1, 2, 10, p.bar);
	}

	/* Results */
	for(int i = 0; i < v->rows && i < rows; i++)
		resultRow(c, &p, v, i);
	if(v->rows > 0)
		scrollbar(c, &p, SS_LIST_Y, rows * SS_ROW_H - 1, v->listed, rows,
		          v->top);
	else
	{
		const char* msg = NULL;
		const char* msg2 = NULL;

		if(v->wordCount > 0)
			msg = v->scanning ? "looking..." : "No filenames match.";
		else if(v->scanned == 0 && !v->scanning)
			msg = "No songs in this folder.";
		else if(!v->typing)
			msg = "Type something to search.";
		else
		{
			msg = "Searches the filenames in this folder";
			msg2 = "and every folder inside it.";
		}
		if(msg != NULL)
			text(c, (BOT_W - (int)strlen(msg) * CH) / 2,
			     SS_LIST_Y + (v->typing ? 22 : 50), msg,
			     v->wordCount > 0 ? p.text : p.dim);
		if(msg2 != NULL)
			text(c, (BOT_W - (int)strlen(msg2) * CH) / 2,
			     SS_LIST_Y + 34, msg2, p.dim);
	}

	if(v->typing)
	{
		keyboard(c, &p, v);
		hints(c, &p, " A key  B delete  Y results  SEL close");
		return;
	}

	if(v->status != NULL && v->status[0] != '\0')
	{
		int x = text(c, 6, SS_STATUS_Y, "in ", p.dim);

		pathText(c, x, SS_STATUS_Y, v->status, 35, p.dim);
	}
	for(int i = 0; i < SS_BTN_COUNT; i++)
	{
		enum btnStyle s = BTN_NORMAL;

		bool usable = i == SS_BTN_TYPE || v->selected >= 0;

		if(v->buttonsFocus && i == v->button)
			s = usable ? BTN_FOCUS : BTN_OFF_FOCUS;
		else if(!usable)
			s = BTN_OFF;
		button(c, &p, ssButtons[i].x, SS_BTN_Y, ssButtons[i].w, SS_BTN_H,
		       ssButtons[i].label, s);
	}
	if(v->buttonsFocus)
		hints(c, &p, " A press  \x1b\x1a pick  \x18 results  B type");
	else
		hints(c, &p, " A play  Y queue  \x1a open  B type");
}

int ssKeyAt(int x, int y, int* row, int* col)
{
	int r, cl, code;

	if(y < SS_KB_Y - SS_KEY_GY || x < SS_KB_X - SS_KEY_GX ||
	   x >= SS_KB_X + SS_KB_COLS * (SS_KEY_W + SS_KEY_GX))
		return -1;
	r = (y - SS_KB_Y + SS_KEY_GY / 2) / (SS_KEY_H + SS_KEY_GY);
	cl = (x - SS_KB_X + SS_KEY_GX / 2) / (SS_KEY_W + SS_KEY_GX);
	if(r < 0 || r >= SS_KB_ROWS || cl < 0 || cl >= SS_KB_COLS)
		return -1;
	code = ssKeyCode(r, cl);
	while(cl > 0 && ssKeyCode(r, cl - 1) == code)
		cl--;
	*row = r;
	*col = cl;
	return code;
}

bool ssOnField(int x, int y)
{
	return x >= SS_FIELD_X && x < SS_FIELD_X + SS_FIELD_W &&
	       y >= SS_FIELD_Y - 2 && y < SS_FIELD_Y + SS_FIELD_H + 2;
}

int ssRowAt(const struct ssView* v, int x, int y)
{
	int i;

	(void)x;
	if(y < SS_LIST_Y)
		return -1;
	i = (y - SS_LIST_Y) / SS_ROW_H;
	return i < ssRowsShown(v) && i < v->rows ? i : -1;
}

int ssButtonAt(int x, int y)
{
	return buttonAt(ssButtons, SS_BTN_COUNT, SS_BTN_Y, SS_BTN_H, x, y);
}

bool ssOnScrollbar(const struct ssView* v, int x, int y)
{
	return v->rows > 0 && ssHasScrollbar(v) && x >= SB_TOUCH_X &&
	       y >= SS_LIST_Y && y < SS_LIST_Y + ssRowsShown(v) * SS_ROW_H;
}

float ssScrollFraction(const struct ssView* v, int y)
{
	float f = (float)(y - SS_LIST_Y) /
	          (float)(ssRowsShown(v) * SS_ROW_H - 1);

	return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

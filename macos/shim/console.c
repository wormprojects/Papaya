/**
 * Papaya desktop: libctru's text console, for the bottom screen.
 *
 * The browser and the menu print to it with printf()/puts()/putchar() and
 * ANSI escapes (cursor position, 24-bit colours, clear). consoleInit()
 * points stdout at a stream that lands here, so none of that code changes.
 * It draws with the same 8x8 font into the same column-major RGB565
 * framebuffer layout as on the 3DS (x * 240 + (239 - y)), and, like
 * libctru, only wraps when the NEXT character is printed.
 *
 * Supported: \n \r \t \b, UTF-8 (code points above 255 show as '?'),
 * CSI H/f A B C D G (moves), J and K (erase), m (bold, faint, reverse,
 * 16 colours, 38/48;2;r;g;b and 38/48;5;n). Anything else is ignored.
 */

#include <3ds.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "font8x8.h"

/* the framebuffer's column height: the console's rows * 8 (240 for 30 rows,
 * more when the window is resized) */
#define SCREEN_H(c)	((c)->consoleHeight * 8)

/* VGA-ish 16 colours as RGB565 */
#define C565(r, g, b)	((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static const u16 palette[16] =
{
	C565(0, 0, 0),		C565(128, 0, 0),	C565(0, 128, 0),	C565(128, 128, 0),
	C565(0, 0, 128),	C565(128, 0, 128),	C565(0, 128, 128),	C565(192, 192, 192),
	C565(128, 128, 128), C565(255, 0, 0),	C565(0, 255, 0),	C565(255, 255, 0),
	C565(0, 0, 255),	C565(255, 0, 255),	C565(0, 255, 255),	C565(255, 255, 255)
};

static PrintConsole		defaultConsole;
static PrintConsole*	current = NULL;
static pthread_mutex_t	lock = PTHREAD_MUTEX_INITIALIZER;
static bool				redirected = false;

/* Parser state (one stream, so it lives here, not per console) */
static enum { ST_TEXT, ST_ESC, ST_CSI } state = ST_TEXT;
static int				params[8];
static int				paramCount;
static bool				paramSeen;
static unsigned			utf8Cp;
static int				utf8Left;

static u16 colorOf(const PrintConsole* c, bool fg)
{
	int		v = fg ? c->fg : c->bg;
	bool	custom = (c->flags & (fg ? CONSOLE_FG_CUSTOM : CONSOLE_BG_CUSTOM)) != 0;

	if(custom)
		return (u16)v;
	if(fg && (c->flags & CONSOLE_COLOR_BOLD) && v < 8)
		v += 8;
	return palette[v & 15];
}

static void putPixel(const PrintConsole* c, int x, int y, u16 px)
{
	c->frameBuffer[x * SCREEN_H(c) + (SCREEN_H(c) - 1 - y)] = px;
}

static void fillRows(PrintConsole* c, int row0, int rows, u16 px)
{
	for(int x = c->windowX * 8; x < (c->windowX + c->windowWidth) * 8; x++)
		for(int y = (c->windowY + row0) * 8;
		    y < (c->windowY + row0 + rows) * 8; y++)
			putPixel(c, x, y, px);
}

static void clearWindow(PrintConsole* c)
{
	fillRows(c, 0, c->windowHeight, colorOf(c, false));
	c->cursorX = 0;
	c->cursorY = 0;
}

static void scrollUp(PrintConsole* c)
{
	int x0 = c->windowX * 8, x1 = (c->windowX + c->windowWidth) * 8;
	int y0 = c->windowY * 8, y1 = (c->windowY + c->windowHeight) * 8;

	for(int x = x0; x < x1; x++)
		for(int y = y0; y < y1 - 8; y++)
			c->frameBuffer[x * SCREEN_H(c) + (SCREEN_H(c) - 1 - y)] =
				c->frameBuffer[x * SCREEN_H(c) + (SCREEN_H(c) - 1 - (y + 8))];
	fillRows(c, c->windowHeight - 1, 1, colorOf(c, false));
}

static void drawGlyph(PrintConsole* c, unsigned cp)
{
	const u8*	g = c->font.gfx + (cp < 256 ? cp : '?') * 8;
	u16			fg = colorOf(c, true), bg = colorOf(c, false);
	int			x0 = (c->windowX + c->cursorX) * 8;
	int			y0 = (c->windowY + c->cursorY) * 8;

	if(c->flags & CONSOLE_COLOR_REVERSE)
	{
		u16 t = fg;

		fg = bg;
		bg = t;
	}
	if(c->flags & CONSOLE_CONCEAL)
		fg = bg;

	for(int row = 0; row < 8; row++)
		for(int col = 0; col < 8; col++)
			putPixel(c, x0 + col, y0 + row,
			         (g[row] & (0x80 >> col)) ? fg : bg);
}

static void newline(PrintConsole* c)
{
	c->cursorX = 0;
	if(++c->cursorY >= c->windowHeight)
	{
		scrollUp(c);
		c->cursorY = c->windowHeight - 1;
	}
}

static void printCp(PrintConsole* c, unsigned cp)
{
	switch(cp)
	{
	case '\n':
		newline(c);
		return;
	case '\r':
		c->cursorX = 0;
		return;
	case '\b':
		if(c->cursorX > 0)
			c->cursorX--;
		return;
	case '\t':
		do
			printCp(c, ' ');
		while(c->cursorX % c->tabSize != 0);
		return;
	default:
		break;
	}

	if(c->cursorX >= c->windowWidth)		/* wraps on the NEXT char */
		newline(c);
	drawGlyph(c, cp);
	c->cursorX++;
}

static int param(int i, int def)
{
	return (i < paramCount && params[i] > 0) ? params[i] : def;
}

static void sgr(PrintConsole* c)
{
	if(paramCount == 0)
	{
		params[0] = 0;
		paramCount = 1;
	}

	for(int i = 0; i < paramCount; i++)
	{
		int p = params[i];

		if(p == 0)
		{
			c->fg = 7;
			c->bg = 0;
			c->flags &= ~(CONSOLE_COLOR_BOLD | CONSOLE_COLOR_FAINT |
			              CONSOLE_COLOR_REVERSE | CONSOLE_CONCEAL |
			              CONSOLE_FG_CUSTOM | CONSOLE_BG_CUSTOM);
		}
		else if(p == 1)
			c->flags |= CONSOLE_COLOR_BOLD;
		else if(p == 2)
			c->flags |= CONSOLE_COLOR_FAINT;
		else if(p == 7)
			c->flags |= CONSOLE_COLOR_REVERSE;
		else if(p == 8)
			c->flags |= CONSOLE_CONCEAL;
		else if(p == 22)
			c->flags &= ~(CONSOLE_COLOR_BOLD | CONSOLE_COLOR_FAINT);
		else if(p == 27)
			c->flags &= ~CONSOLE_COLOR_REVERSE;
		else if(p == 28)
			c->flags &= ~CONSOLE_CONCEAL;
		else if(p >= 30 && p <= 37)
		{
			c->fg = p - 30;
			c->flags &= ~CONSOLE_FG_CUSTOM;
		}
		else if(p >= 40 && p <= 47)
		{
			c->bg = p - 40;
			c->flags &= ~CONSOLE_BG_CUSTOM;
		}
		else if(p >= 90 && p <= 97)
		{
			c->fg = p - 90 + 8;
			c->flags &= ~CONSOLE_FG_CUSTOM;
		}
		else if(p >= 100 && p <= 107)
		{
			c->bg = p - 100 + 8;
			c->flags &= ~CONSOLE_BG_CUSTOM;
		}
		else if(p == 39)
		{
			c->fg = 7;
			c->flags &= ~CONSOLE_FG_CUSTOM;
		}
		else if(p == 49)
		{
			c->bg = 0;
			c->flags &= ~CONSOLE_BG_CUSTOM;
		}
		else if(p == 38 || p == 48)
		{
			bool	fg = p == 38;
			u16		px = 0;
			bool	ok = false;

			if(i + 4 < paramCount && params[i + 1] == 2)
			{
				px = C565(params[i + 2] & 255, params[i + 3] & 255,
				          params[i + 4] & 255);
				ok = true;
				i += 4;
			}
			else if(i + 2 < paramCount && params[i + 1] == 5)
			{
				int n = params[i + 2];

				if(n < 16)
					px = palette[n];
				else if(n < 232)
				{
					int lv[6] = { 0, 95, 135, 175, 215, 255 };

					n -= 16;
					px = C565(lv[n / 36], lv[(n / 6) % 6], lv[n % 6]);
				}
				else
				{
					int v = 8 + (n - 232) * 10;

					px = C565(v, v, v);
				}
				ok = true;
				i += 2;
			}
			if(ok)
			{
				if(fg)
				{
					c->fg = px;
					c->flags |= CONSOLE_FG_CUSTOM;
				}
				else
				{
					c->bg = px;
					c->flags |= CONSOLE_BG_CUSTOM;
				}
			}
		}
	}
}

static void csi(PrintConsole* c, char final)
{
	int n;

	if(paramSeen && paramCount < 8)
		paramCount++;

	switch(final)
	{
	case 'H':
	case 'f':
		c->cursorY = param(0, 1) - 1;
		c->cursorX = param(1, 1) - 1;
		if(c->cursorY >= c->windowHeight)
			c->cursorY = c->windowHeight - 1;
		if(c->cursorX > c->windowWidth)
			c->cursorX = c->windowWidth;
		break;
	case 'A':
		c->cursorY -= param(0, 1);
		if(c->cursorY < 0)
			c->cursorY = 0;
		break;
	case 'B':
		c->cursorY += param(0, 1);
		if(c->cursorY >= c->windowHeight)
			c->cursorY = c->windowHeight - 1;
		break;
	case 'C':
		c->cursorX += param(0, 1);
		if(c->cursorX > c->windowWidth)
			c->cursorX = c->windowWidth;
		break;
	case 'D':
		c->cursorX -= param(0, 1);
		if(c->cursorX < 0)
			c->cursorX = 0;
		break;
	case 'G':
		c->cursorX = param(0, 1) - 1;
		if(c->cursorX > c->windowWidth)
			c->cursorX = c->windowWidth;
		break;
	case 'J':
		n = paramCount > 0 ? params[0] : 0;
		if(n == 2 || n == 3)
			clearWindow(c);
		else if(n == 0)
		{
			/* cursor to the end of the screen */
			fillRows(c, c->cursorY + 1, c->windowHeight - c->cursorY - 1,
			         colorOf(c, false));
			for(int x = (c->windowX + c->cursorX) * 8;
			    x < (c->windowX + c->windowWidth) * 8; x++)
				for(int y = (c->windowY + c->cursorY) * 8;
				    y < (c->windowY + c->cursorY + 1) * 8; y++)
					putPixel(c, x, y, colorOf(c, false));
		}
		break;
	case 'K':
		n = paramCount > 0 ? params[0] : 0;
		{
			int from = n == 0 ? c->cursorX : 0;
			int to = n == 1 ? c->cursorX + 1 : c->windowWidth;

			for(int x = (c->windowX + from) * 8; x < (c->windowX + to) * 8;
			    x++)
				for(int y = (c->windowY + c->cursorY) * 8;
				    y < (c->windowY + c->cursorY + 1) * 8; y++)
					putPixel(c, x, y, colorOf(c, false));
		}
		break;
	case 'm':
		sgr(c);
		break;
	case 's':
		c->prevCursorX = c->cursorX;
		c->prevCursorY = c->cursorY;
		break;
	case 'u':
		c->cursorX = c->prevCursorX;
		c->cursorY = c->prevCursorY;
		break;
	default:
		break;
	}
}

static void feed(PrintConsole* c, unsigned char ch)
{
	switch(state)
	{
	case ST_ESC:
		if(ch == '[')
		{
			state = ST_CSI;
			paramCount = 0;
			paramSeen = false;
			memset(params, 0, sizeof(params));
		}
		else
			state = ST_TEXT;
		return;

	case ST_CSI:
		if(ch >= '0' && ch <= '9')
		{
			if(paramCount < 8)
				params[paramCount] = params[paramCount] * 10 + (ch - '0');
			paramSeen = true;
		}
		else if(ch == ';')
		{
			if(paramCount < 8)
				paramCount++;
			paramSeen = false;
		}
		else if(ch >= 0x40 && ch <= 0x7e)
		{
			csi(c, (char)ch);
			state = ST_TEXT;
		}
		else if(ch < 0x20 || ch > 0x3f)
			state = ST_TEXT;	/* garbage: give up on it */
		return;

	case ST_TEXT:
	default:
		break;
	}

	if(utf8Left > 0)
	{
		if((ch & 0xC0) == 0x80)
		{
			utf8Cp = (utf8Cp << 6) | (ch & 0x3F);
			if(--utf8Left == 0)
				printCp(c, utf8Cp);
			return;
		}
		utf8Left = 0;			/* broken sequence: treat ch afresh */
	}

	if(ch == 0x1b)
		state = ST_ESC;
	else if(ch >= 0xF0)
	{
		utf8Cp = ch & 0x07;
		utf8Left = 3;
	}
	else if(ch >= 0xE0)
	{
		utf8Cp = ch & 0x0F;
		utf8Left = 2;
	}
	else if(ch >= 0xC2)
	{
		utf8Cp = ch & 0x1F;
		utf8Left = 1;
	}
	else
		printCp(c, ch);		/* ASCII, and 0x80-0xC1 as raw glyph indices */
}

static int streamWrite(void* cookie, const char* buf, int len)
{
	(void)cookie;

	pthread_mutex_lock(&lock);
	if(current != NULL && current->frameBuffer != NULL)
		for(int i = 0; i < len; i++)
			feed(current, (unsigned char)buf[i]);
	pthread_mutex_unlock(&lock);
	return len;
}

#ifdef __APPLE__
static void redirectStdout(void)
{
	FILE* f = funopen(NULL, NULL, streamWrite, NULL, NULL);

	if(f != NULL)
	{
		stdout = f;
		setvbuf(stdout, NULL, _IONBF, 0);
	}
}
#else
static ssize_t cookieWrite(void* cookie, const char* buf, size_t len)
{
	return streamWrite(cookie, buf, (int)len);
}

static void redirectStdout(void)
{
	cookie_io_functions_t io = { NULL, cookieWrite, NULL, NULL };
	FILE* f = fopencookie(NULL, "w", io);

	if(f != NULL)
	{
		stdout = f;
		setvbuf(stdout, NULL, _IONBF, 0);
	}
}
#endif

PrintConsole* consoleGetDefault(void)
{
	if(!defaultConsole.consoleInitialised)
	{
		/* (consoleGetDefault()->font.gfx is all the app reads from it
		 * before consoleInit.) */
		defaultConsole.font.gfx = font8x8;
		defaultConsole.font.asciiOffset = 0;
		defaultConsole.font.numChars = 256;
	}
	return &defaultConsole;
}

PrintConsole* consoleInit(gfxScreen_t screen, PrintConsole* console)
{
	u16 w, h;

	if(console == NULL)
		console = &defaultConsole;

	memset(console, 0, sizeof(*console));
	console->font.gfx = font8x8;
	console->font.asciiOffset = 0;
	console->font.numChars = 256;
	console->frameBuffer = (u16*)gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
	console->consoleWidth = (int)h / 8;
	console->consoleHeight = (int)w / 8;
	console->windowX = 0;
	console->windowY = 0;
	console->windowWidth = console->consoleWidth;
	console->windowHeight = console->consoleHeight;
	console->tabSize = 3;
	console->fg = 7;
	console->bg = 0;
	console->consoleInitialised = true;

	if(!redirected)
	{
		redirected = true;
		redirectStdout();
	}

	consoleSelect(console);
	consoleClear();
	return console;
}

/* The window was resized: the console is now cols x rows characters, on a
 * framebuffer of the same shape (shim/gfxhid.c). Keeps the colours, clears. */
void consoleResize(PrintConsole* console, int cols, int rows)
{
	u16 w, h;

	pthread_mutex_lock(&lock);
	console->frameBuffer = (u16*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
	console->consoleWidth = cols;
	console->consoleHeight = rows;
	console->windowX = console->windowY = 0;
	console->windowWidth = cols;
	console->windowHeight = rows;
	if(console->frameBuffer != NULL)
		clearWindow(console);
	pthread_mutex_unlock(&lock);
}

PrintConsole* consoleSelect(PrintConsole* console)
{
	PrintConsole* prev;

	pthread_mutex_lock(&lock);
	prev = current;
	current = console;
	pthread_mutex_unlock(&lock);
	return prev;
}

void consoleClear(void)
{
	pthread_mutex_lock(&lock);
	if(current != NULL && current->frameBuffer != NULL)
		clearWindow(current);
	pthread_mutex_unlock(&lock);
}

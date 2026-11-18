/**
 * Papaya for macOS - the 3DS's screens and buttons, in memory.
 *
 * ui.c, browser.c, menu.c... draw into "framebuffers" and read buttons and
 * the stylus. Here the framebuffers are plain arrays that the Swift window
 * copies out (macRenderTop/Bottom), and the buttons/stylus are whatever the
 * window last told us (macSetInput). There is no SDL, no window and no
 * frame pacing in this file: the window's display link drives the app.
 */

#include <3ds.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demon.h"
#include "gfxmac.h"
#include "platform.h"

static volatile bool quitRequested = false;

/* ---- input: set by the window, consumed by hidScanInput() ------------ */

static u32				heldNow, pendingDown, prevHeld, kDown, kHeld, kUp;
static touchPosition	touchPos;

void macSetInput(u32 held, u32 pulses, bool touching, int tx, int ty)
{
	/* `pulses` = buttons pressed and released within one frame: they still
	 * count as held for the next scan (same as the old SDL shim). */
	heldNow = held | (touching ? KEY_TOUCH : 0);
	pendingDown |= pulses;
	if(touching)
	{
		touchPos.px = (u16)(tx < 0 ? 0 : tx > MAC_MAX_W - 1 ? MAC_MAX_W - 1 : tx);
		touchPos.py = (u16)(ty < 0 ? 0 : ty > MAC_MAX_H - 1 ? MAC_MAX_H - 1 : ty);
	}
}

void hidScanInput(void)
{
	u32 held = heldNow | pendingDown;

	kDown = held & ~prevHeld;
	kUp = prevHeld & ~held;
	kHeld = held;
	prevHeld = held;
	pendingDown = 0;
}

u32 hidKeysDown(void)	{ return kDown; }
u32 hidKeysHeld(void)	{ return kHeld; }
u32 hidKeysUp(void)		{ return kUp; }
/* Pages that are drawn at their native size, centred in a bigger bottom
 * screen (the menu, queue, search...), read the stylus relative to their own
 * top left. */
static int touchOx = 0, touchOy = 0;
void macSetTouchOffset(int ox, int oy)	{ touchOx = ox; touchOy = oy; }

void hidTouchRead(touchPosition* pos)
{
	int x = touchPos.px - touchOx, y = touchPos.py - touchOy;

	pos->px = (u16)(x < 0 ? 0 : x);
	pos->py = (u16)(y < 0 ? 0 : y);
}

/* ---- screens ---------------------------------------------------------- */

/* The top screen is resizable (the window's logical size); the bottom one is
 * still 320x240. Both framebuffers are column-major RGB565 like the 3DS:
 * pixel (x, y) = fb[x * h + (h - 1 - y)] for an h-pixel-high screen. */
static u16 topFb[MAC_MAX_W * MAC_MAX_H];
static u16 botFb[MAC_MAX_W * MAC_MAX_H];
static int botW = MAC_BOT_W, botH = MAC_SCR_H;
static u16 overlayFb[MAC_BOT_W * MAC_SCR_H];
static u16 overlayShown[MAC_BOT_W * MAC_SCR_H];
static int topW = MAC_TOP_W, topH = MAC_SCR_H;

void macSetTopSize(int w, int h)
{
	if(w > MAC_MAX_W) w = MAC_MAX_W;
	if(h > MAC_MAX_H) h = MAC_MAX_H;
	topW = w;
	topH = h;
	memset(topFb, 0, (size_t)w * h * sizeof(u16));
	macInvalidate();
}

void macTopSize(int* w, int* h)	{ *w = topW; *h = topH; }

/* The bottom screen is a text console, so its size is whole characters. */
void macSetBottomSize(int cols, int rows)
{
	if(cols * 8 > MAC_MAX_W) cols = MAC_MAX_W / 8;
	if(rows * 8 > MAC_MAX_H) rows = MAC_MAX_H / 8;
	botW = cols * 8;
	botH = rows * 8;
	memset(botFb, 0, (size_t)botW * botH * sizeof(u16));
	macInvalidate();
}

void macBottomSize(int* w, int* h)	{ *w = botW; *h = botH; }

void gfxInitDefault(void) {}
void gfxExit(void) {}
void gfxSetScreenFormat(gfxScreen_t s, GSPGPU_FramebufferFormat f) { (void)s; (void)f; }
void gfxSetDoubleBuffering(gfxScreen_t s, bool e) { (void)s; (void)e; }
void gfxFlushBuffers(void) {}
void gfxSwapBuffers(void) {}
void gspWaitForVBlank(void) {}		/* the window's display link paces us */

u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width,
                      u16* height)
{
	(void)side;
	if(screen == GFX_TOP)
	{
		if(width != NULL)	*width = (u16)topH;
		if(height != NULL)	*height = (u16)topW;
		return (u8*)topFb;
	}
	if(screen == GFX_OVERLAY)
	{
		if(width != NULL)	*width = MAC_SCR_H;
		if(height != NULL)	*height = MAC_BOT_W;
		return (u8*)overlayFb;
	}
	if(width != NULL)	*width = (u16)botH;
	if(height != NULL)	*height = (u16)botW;
	return (u8*)botFb;
}

/* Column-major RGB565 (w x h) -> row-major BGRA, only if it changed since
 * the last call (the app already redraws only what changed; this just
 * avoids handing the compositor an identical image). */
/* Demon mode: what the window shows gets demon.c's rainbow/glitch effect
 * (ui.c asks), redone every frame while it's on. */
static volatile bool demon;
static u16 demonBuf[MAC_MAX_W * MAC_MAX_H];

static bool convert(const u16* fb, int w, int h, u16* shown, uint32_t* bgra)
{
	size_t n = (size_t)w * h * sizeof(u16);

	if(demon)
	{
		/* Animated: every frame, whether the app drew or not. */
		demonApply(fb, demonBuf, w, h, (unsigned long)osGetTime());
		fb = demonBuf;
		memset(shown, 0xFF, n);		/* a full convert once it's off */
	}
	else if(memcmp(fb, shown, n) == 0)
		return false;
	else
		memcpy(shown, fb, n);
	for(int x = 0; x < w; x++)
		for(int y = 0; y < h; y++)
		{
			u16 p = fb[x * h + (h - 1 - y)];

			uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;

			r = (r << 3) | (r >> 2);
			g = (g << 2) | (g >> 4);
			b = (b << 3) | (b >> 2);
			bgra[y * w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
		}
	return true;
}

static u16 topShown[MAC_MAX_W * MAC_MAX_H], botShown[MAC_MAX_W * MAC_MAX_H];
static bool topForce = true, botForce = true;

static bool overlayForce = true;
void macInvalidate(void)	{ topForce = botForce = overlayForce = true; }

void macSetDemon(bool on)
{
	if(on == demon)
		return;
	demon = on;
	macInvalidate();	/* every surface again, with/without it */
}

bool macRenderOverlay(uint32_t* bgra)
{
	if(overlayForce)
	{
		memset(overlayShown, 0xFF, sizeof(overlayShown));
		overlayForce = false;
	}
	return convert(overlayFb, MAC_BOT_W, MAC_SCR_H, overlayShown, bgra);
}

/* ---- what the window needs to know about the app's state ---- */

static volatile bool overlayOn, textEntryOn;
static volatile uint32_t escKey = KEY_SELECT;

void macSetState(bool overlay, bool textEntry, uint32_t esc)
{
	overlayOn = overlay;
	textEntryOn = textEntry;
	escKey = esc;
}
bool macOverlayActive(void)	{ return overlayOn; }
bool macTextEntryActive(void)	{ return textEntryOn; }
uint32_t macEscKey(void)	{ return escKey; }

/* ---- typed characters (text fields use the real keyboard) ---- */

#define TYPED_MAX	64
static int typedQ[TYPED_MAX];
static int typedHead, typedTail;

void macTypeChar(int ch)
{
	if((typedTail + 1) % TYPED_MAX != typedHead)
	{
		typedQ[typedTail] = ch;
		typedTail = (typedTail + 1) % TYPED_MAX;
	}
}

int macTypedChar(void)
{
	int ch;

	if(typedHead == typedTail)
		return 0;
	ch = typedQ[typedHead];
	typedHead = (typedHead + 1) % TYPED_MAX;
	return ch;
}

bool macRenderTop(uint32_t* bgra)
{
	if(topForce)
	{
		memset(topShown, 0xFF, (size_t)topW * topH * sizeof(u16));
		topForce = false;
	}
	return convert(topFb, topW, topH, topShown, bgra);
}

bool macRenderBottom(uint32_t* bgra)
{
	if(botForce)
	{
		memset(botShown, 0xFF, (size_t)botW * botH * sizeof(u16));
		botForce = false;
	}
	return convert(botFb, botW, botH, botShown, bgra);
}

/* ---- quit, battery ---------------------------------------------------- */

bool videoQuitRequested(void)	{ return quitRequested; }
void videoRequestQuit(void)		{ quitRequested = true; }
int nativeBatteryPercent(void)	{ return -1; }	/* TODO: IOKit */

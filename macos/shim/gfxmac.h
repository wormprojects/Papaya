/* The Mac window's side of shim/gfxhid.c (Swift calls these). */
#ifndef papaya_gfxmac_h
#define papaya_gfxmac_h
#include <stdbool.h>
#include <stdint.h>

#define MAC_TOP_W	400
#define MAC_BOT_W	320
#define MAC_SCR_H	240
#define MAC_MAX_W	2048	/* biggest top screen (logical pixels) */
#define MAC_MAX_H	1280

/** Buttons held (KEY_* mask), buttons tapped within the frame, the stylus
 * (bottom screen pixels). Call once per frame before appTick(). */
void macSetInput(uint32_t held, uint32_t pulses, bool touching, int tx, int ty);

/** Copy a screen as BGRA (row-major) if it changed; false if identical. */
bool macRenderTop(uint32_t* bgra);
bool macRenderBottom(uint32_t* bgra);
/** The top screen's size now (logical pixels). */
void macTopSize(int* w, int* h);
void macSetTopSize(int w, int h);
/** The bottom screen (a text console): cols x rows characters. */
void macSetBottomSize(int cols, int rows);
void macBottomSize(int* w, int* h);
/** Where a page drawn at its native size sits in the bottom screen. */
void macSetTouchOffset(int ox, int oy);

/** The overlay surface (320x240): the menu and the pages. */
bool macRenderOverlay(uint32_t* bgra);
/** What the app is showing: an overlay (menu/page) is up; a text field has
 * the keyboard; which button Esc stands for right now. */
void macSetState(bool overlay, bool textEntry, uint32_t esc);
bool macOverlayActive(void);
bool macTextEntryActive(void);
uint32_t macEscKey(void);
/** A character typed while a text field is open. */
void macTypeChar(int ch);

/** A new window/view needs a full first frame. */
void macInvalidate(void);

/** Demon mode: show every surface with demon.c's effect (called by ui.c). */
void macSetDemon(bool on);

/* 3DS button bits, for the Swift side (same values as KEY_* in 3ds.h) */
#define MAC_KEY_A		(1u << 0)
#define MAC_KEY_B		(1u << 1)
#define MAC_KEY_SELECT	(1u << 2)
#define MAC_KEY_START	(1u << 3)
#define MAC_KEY_RIGHT	(1u << 4)
#define MAC_KEY_LEFT	(1u << 5)
#define MAC_KEY_UP		(1u << 6)
#define MAC_KEY_DOWN	(1u << 7)
#define MAC_KEY_R		(1u << 8)
#define MAC_KEY_L		(1u << 9)
#define MAC_KEY_X		(1u << 10)
#define MAC_KEY_Y		(1u << 11)
#define MAC_KEY_ZL		(1u << 14)
#define MAC_KEY_ZR		(1u << 15)

/** True once the app asked to quit (menu "Exit Papaya"). */
bool videoQuitRequested(void);
void macSetVolume(float v);
float macVolume(void);

#endif

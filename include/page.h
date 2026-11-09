/**
 * ctrmus - 3DS Music Player
 * Bits shared by the pixel-drawn bottom pages (queueedit.c, search.c).
 */

#ifndef ctrmus_page_h
#define ctrmus_page_h

#include <3ds.h>

/* What a page's input handler asks main.c to do next. */
enum page_result
{
	PAGE_STAY,		/* still open */
	PAGE_CLOSE,		/* close, back to the browser */
	PAGE_TO_MENU	/* close, back to the menu it was opened from */
};

/* Held keys repeat after this delay, at this rate (ms) */
#define PAGE_REPEAT_DELAY_MS	400
#define PAGE_REPEAT_EVERY_MS	80

/* Zeroed when a page opens */
struct keyRepeat
{
	u64	since, last;
	u32	down;		/* keys pressed while this page was open */
};

/**
 * The keys of `mask` pressed now, or held long enough to repeat. Call
 * once per frame.
 *
 * Only keys pressed since the page opened repeat: a key still held from
 * the press that opened it (A on Menu > Search...) must not, or it
 * "types" the selected key over and over until it's let go.
 */
static inline u32 keyRepeat(struct keyRepeat* r, u32 kDown, u32 kHeld,
                            u32 mask)
{
	u64 now = osGetTime();
	u32 held;

	r->down &= kHeld | ~mask;	/* let go: forget them */
	if(kDown & mask)
	{
		r->since = r->last = now;
		r->down |= kDown & mask;
		return kDown & mask;
	}
	held = kHeld & mask & r->down;
	if(held && now - r->since >= PAGE_REPEAT_DELAY_MS &&
	   now - r->last >= PAGE_REPEAT_EVERY_MS)
	{
		r->last = now;
		return held;
	}
	return 0;
}

#endif

/**
 * Demon mode's screen effect. See demon.h.
 *
 * Rainbow: 16 colour tables (one per hue), each mapping a pixel (reduced to
 * 4 bits per channel, 4096 entries) to the same brightness with a tint of
 * that hue added and its own colour turned by the same amount. The hue
 * changes down and across the screen and drifts with time. Built once, on
 * first use (64 K entries, a few ms). Per pixel it costs one table lookup.
 *
 * Glitch: in about one 90 ms slot out of 11, a band of 4-27 rows is torn
 * sideways by 3-14 pixels (different on each screen).
 */

#include <math.h>
#include <stdbool.h>

#include "demon.h"

#define HUES		16
#define SLOT_MS		90		/* the glitches change this often */
#define HUE_MS		160		/* the rainbow moves one hue step this often */
#define TINT		0.13f	/* how much colour is added (YIQ units) */

static uint16_t	lut[HUES][4096];
static bool		ready = false;

/* Pixel -> its table index: the top 4 bits of R, G and B. */
static inline unsigned lutIndex(uint16_t p)
{
	return ((unsigned)(p >> 12) << 8) | (((p >> 7) & 0xFu) << 4) |
	       ((p >> 1) & 0xFu);
}

static int clamp8(float v)
{
	int i = (int)lrintf(v * 255.0f);

	return i < 0 ? 0 : i > 255 ? 255 : i;
}

/* In YIQ, Y is the brightness and (I, Q) the colour: keep Y, turn the
 * colour by the hue's angle and add a tint in that direction. */
static void build(void)
{
	for(int k = 0; k < HUES; k++)
	{
		float a = (float)k * 6.2831853f / HUES;
		float ca = cosf(a), sa = sinf(a);

		for(unsigned i = 0; i < 4096; i++)
		{
			float r = (float)((i >> 8) & 0xF) / 15.0f;
			float g = (float)((i >> 4) & 0xF) / 15.0f;
			float b = (float)(i & 0xF) / 15.0f;
			float y = 0.299f * r + 0.587f * g + 0.114f * b;
			float ci = 0.596f * r - 0.274f * g - 0.322f * b;
			float cq = 0.211f * r - 0.523f * g + 0.312f * b;
			float ni = ci * ca - cq * sa + TINT * ca;
			float nq = ci * sa + cq * ca + TINT * sa;
			int R = clamp8(y + 0.956f * ni + 0.621f * nq);
			int G = clamp8(y - 0.272f * ni - 0.647f * nq);
			int B = clamp8(y - 1.106f * ni + 1.703f * nq);

			lut[k][i] = (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) |
			                       (B >> 3));
		}
	}
	ready = true;
}

/* A well-mixed 32-bit number from x (for the glitches). */
static uint32_t hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

void demonApply(const uint16_t* src, uint16_t* dst, int w, int h,
                unsigned long ms)
{
	uint32_t	r = hash((uint32_t)(ms / SLOT_MS) * 2654435761U ^ (uint32_t)w);
	int			gy0 = 0, gy1 = 0, gd = 0;
	int			phase = (int)((ms / HUE_MS) % HUES);

	if(w <= 0 || h <= 0)
		return;
	if(!ready)
		build();

	/* This slot's glitch, if it has one. */
	if(r % 11 == 0)
	{
		gy0 = (int)((r >> 8) % (uint32_t)h);
		gy1 = gy0 + 4 + (int)((r >> 16) % 24);
		if(gy1 > h)
			gy1 = h;
		gd = 3 + (int)((r >> 20) % 12);
		if(r & 0x80000000U)
			gd = -gd;
	}

	for(int x = 0; x < w; x++)
	{
		const uint16_t*	col = src + (size_t)x * h;
		const uint16_t*	torn = src + (size_t)(((x + gd) % w + w) % w) * h;
		uint16_t*		out = dst + (size_t)x * h;
		/* Half a rainbow across the screen, a whole one down it. */
		int				hue0 = phase + x * HUES / (2 * w);

		for(int band = 0, y = 0; band < HUES; band++)
		{
			const uint16_t*	t = lut[(hue0 + band) % HUES];
			int				y1 = (band + 1) * h / HUES;

			for(; y < y1; y++)
				out[y] = t[lutIndex((y >= gy0 && y < gy1) ? torn[y] :
				                                            col[y])];
		}
	}
}

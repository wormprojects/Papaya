/**
 * ctrmus - 3DS Music Player
 * Natural-order string compare. See natsort.h.
 */

#include "natsort.h"

int naturalCompare(const char *a, const char *b)
{
	while(*a && *b)
	{
		if(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
		{
			const char *na = a, *nb = b;
			unsigned long va = 0, vb = 0;
			int lena = 0, lenb = 0;

			while(*na >= '0' && *na <= '9')
			{ va = va * 10 + (*na - '0'); na++; lena++; }
			while(*nb >= '0' && *nb <= '9')
			{ vb = vb * 10 + (*nb - '0'); nb++; lenb++; }

			if(va != vb) return (va < vb) ? -1 : 1;
			if(lena != lenb) return lena - lenb;

			a = na; b = nb;
			continue;
		}

		{
			char ca = *a, cb = *b;
			if(ca >= 'A' && ca <= 'Z') ca += 32;
			if(cb >= 'A' && cb <= 'Z') cb += 32;
			if(ca != cb)
				return (unsigned char)ca - (unsigned char)cb;
		}

		a++; b++;
	}

	return (unsigned char)*a - (unsigned char)*b;
}

int naturalCompareP(const void *p1, const void *p2)
{
	return naturalCompare(*(char * const *)p1, *(char * const *)p2);
}

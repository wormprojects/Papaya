/**
 * ctrmus - 3DS Music Player
 * Natural-order string compare ("2 song" before "10 song", case
 * insensitive for ASCII), shared by the folder listing and search.
 * Plain C, no libctru.
 */

#ifndef ctrmus_natsort_h
#define ctrmus_natsort_h

/** <0, 0 or >0 like strcmp, with digit runs compared as numbers. */
int naturalCompare(const char* a, const char* b);

/** qsort() comparator for an array of char* (natural order). */
int naturalCompareP(const void* p1, const void* p2);

#endif

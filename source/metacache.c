/**
 * ctrmus - 3DS Music Player
 * On-disk metadata cache. See metacache.h.
 *
 * File format (all integers little-endian):
 *   "CMC1"
 *   u32  entry count
 *   u16  directory path length, directory path bytes
 *   entries:
 *     u64  mtime
 *     u16  name length, name bytes
 *     u8   flags (bit 0 title, bit 1 artist, bit 2 album)
 *     for each flagged field: u8 length, bytes
 *
 * The whole file is read and written in one go: one SD round trip each.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "metacache.h"

#define METACACHE_MAGIC		"CMC1"
#define METACACHE_MAX_SIZE	(8 * 1024 * 1024)
#define METACACHE_MAX_COUNT	100000

#ifdef __3DS__
/* Serialises cache file access between the UI thread (load) and the
 * metadata loader thread (save). */
static LightLock cacheLock;
static bool cacheLockInit = false;

static void lockCache(void)
{
	/* First call happens on the main thread before the loader thread
	 * exists, so lazy initialisation is race-free. */
	if(!cacheLockInit)
	{
		LightLock_Init(&cacheLock);
		cacheLockInit = true;
	}
	LightLock_Lock(&cacheLock);
}

static void unlockCache(void)
{
	LightLock_Unlock(&cacheLock);
}
#else
static void lockCache(void) {}
static void unlockCache(void) {}
#endif

/* 32-bit FNV-1a */
static uint32_t hashStr(const char* s)
{
	uint32_t h = 2166136261u;

	while(*s)
	{
		h ^= (unsigned char)*s++;
		h *= 16777619u;
	}

	return h;
}

static void cachePath(const char* dir, char* out, size_t outLen)
{
	snprintf(out, outLen, "%s/%08lx.bin", METACACHE_DIR,
	         (unsigned long)hashStr(dir));
}

/* mkdir -p; errors (mostly EEXIST) are ignored, open() reports failure. */
static void makeDirs(const char* dir)
{
	char tmp[256];

	if((size_t)snprintf(tmp, sizeof(tmp), "%s", dir) >= sizeof(tmp))
		return;
	for(char* p = strchr(tmp, '/'); p != NULL; p = strchr(p + 1, '/'))
	{
		/* Skip the device root ("sdmc:/") and empty components. */
		if(p == tmp || p[-1] == ':' || p[-1] == '/')
			continue;
		*p = '\0';
		mkdir(tmp, 0777);
		*p = '/';
	}
	mkdir(tmp, 0777);
}

static int cmpEntry(const void* a, const void* b)
{
	return strcmp(((const struct metaCacheEntry*)a)->name,
	              ((const struct metaCacheEntry*)b)->name);
}

/* ------------------------------------------------------------------ */
/* Little-endian (de)serialisation helpers                            */
/* ------------------------------------------------------------------ */

struct rd
{
	const unsigned char*	p;
	const unsigned char*	end;
	bool					bad;
};

static const unsigned char* rdTake(struct rd* r, size_t n)
{
	const unsigned char* p = r->p;

	if(r->bad || (size_t)(r->end - r->p) < n)
	{
		r->bad = true;
		return NULL;
	}

	r->p += n;
	return p;
}

static uint64_t rdUint(struct rd* r, size_t n)
{
	const unsigned char*	p = rdTake(r, n);
	uint64_t				v = 0;

	if(p == NULL)
		return 0;

	for(size_t i = n; i > 0; i--)
		v = (v << 8) | p[i - 1];

	return v;
}

/* Read a u8-length-prefixed string into a fixed buffer. */
static void rdField(struct rd* r, char* dst, size_t cap)
{
	size_t					len = (size_t)rdUint(r, 1);
	const unsigned char*	p = rdTake(r, len);

	if(p == NULL)
		return;

	if(len >= cap)
		len = cap - 1;

	memcpy(dst, p, len);
	dst[len] = '\0';
}

static unsigned char* wrUint(unsigned char* p, uint64_t v, size_t n)
{
	for(size_t i = 0; i < n; i++, v >>= 8)
		*p++ = (unsigned char)(v & 0xFF);
	return p;
}

static unsigned char* wrBytes(unsigned char* p, const void* s, size_t n)
{
	memcpy(p, s, n);
	return p + n;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int metaCacheLoad(const char* dir, struct metaCache* mc)
{
	char			path[256];
	unsigned char*	data = NULL;
	int				fd;
	off_t			size;
	struct rd		r;
	uint32_t		count;
	size_t			dirLen;

	mc->entries = NULL;
	mc->count = 0;

	cachePath(dir, path, sizeof(path));

	lockCache();
	fd = open(path, O_RDONLY);
	if(fd >= 0)
	{
		size = lseek(fd, 0, SEEK_END);
		if(size > 0 && size <= METACACHE_MAX_SIZE &&
		   lseek(fd, 0, SEEK_SET) == 0 &&
		   (data = malloc((size_t)size)) != NULL)
		{
			if(read(fd, data, (size_t)size) != (ssize_t)size)
			{
				free(data);
				data = NULL;
			}
		}
		close(fd);
	}
	unlockCache();

	if(data == NULL)
		return 0;

	r.p = data;
	r.end = data + size;
	r.bad = false;

	{
		const unsigned char* magic = rdTake(&r, 4);
		if(magic == NULL || memcmp(magic, METACACHE_MAGIC, 4) != 0)
			goto out;
	}

	count = (uint32_t)rdUint(&r, 4);
	dirLen = (size_t)rdUint(&r, 2);

	/* Hash collision or corrupt file: ignore. */
	{
		const unsigned char* d = rdTake(&r, dirLen);
		if(d == NULL || dirLen != strlen(dir) || memcmp(d, dir, dirLen) != 0)
			goto out;
	}

	if(count == 0 || count > METACACHE_MAX_COUNT)
		goto out;

	if((mc->entries = calloc(count, sizeof(*mc->entries))) == NULL)
		goto out;

	for(uint32_t i = 0; i < count && !r.bad; i++)
	{
		struct metaCacheEntry*	e = &mc->entries[mc->count];
		size_t					nameLen;
		const unsigned char*	name;
		unsigned				flags;

		e->mtime = rdUint(&r, 8);
		nameLen = (size_t)rdUint(&r, 2);
		name = rdTake(&r, nameLen);
		flags = (unsigned)rdUint(&r, 1);

		if(flags & 1) rdField(&r, e->meta.title,  sizeof(e->meta.title));
		if(flags & 2) rdField(&r, e->meta.artist, sizeof(e->meta.artist));
		if(flags & 4) rdField(&r, e->meta.album,  sizeof(e->meta.album));

		if(r.bad || name == NULL || nameLen == 0)
			break;

		if((e->name = malloc(nameLen + 1)) == NULL)
			break;

		memcpy(e->name, name, nameLen);
		e->name[nameLen] = '\0';
		e->meta.has_title  = (flags & 1) != 0;
		e->meta.has_artist = (flags & 2) != 0;
		e->meta.has_album  = (flags & 4) != 0;
		mc->count++;
	}

	/* Saved sorted, but don't trust that for the binary search. */
	qsort(mc->entries, (size_t)mc->count, sizeof(*mc->entries), cmpEntry);

out:
	free(data);
	if(mc->count == 0)
	{
		free(mc->entries);
		mc->entries = NULL;
	}
	return mc->count;
}

const struct metaCacheEntry* metaCacheFind(const struct metaCache* mc,
                                           const char* name)
{
	struct metaCacheEntry key;

	if(mc == NULL || mc->count == 0)
		return NULL;

	key.name = (char*)name;
	return bsearch(&key, mc->entries, (size_t)mc->count,
	               sizeof(*mc->entries), cmpEntry);
}

static size_t fieldLen(const char* s)
{
	size_t len = strlen(s);
	return len > 255 ? 255 : len;
}

int metaCacheSave(const char* dir, const struct metaCacheEntry* entries,
                  int count)
{
	char				path[256];
	size_t				dirLen = strlen(dir);
	size_t				total;
	unsigned char*		data;
	unsigned char*		p;
	struct metaCacheEntry*	sorted;
	int					fd;
	int					ret = -1;

	if(dirLen > 0xFFFF || count < 0 || count > METACACHE_MAX_COUNT)
		return -1;

	/* Sort a shallow copy so the file is written in lookup order. */
	if((sorted = malloc(sizeof(*sorted) * (size_t)(count ? count : 1))) == NULL)
		return -1;
	memcpy(sorted, entries, sizeof(*sorted) * (size_t)count);
	qsort(sorted, (size_t)count, sizeof(*sorted), cmpEntry);

	total = 4 + 4 + 2 + dirLen;
	for(int i = 0; i < count; i++)
	{
		const struct music_metadata* m = &sorted[i].meta;
		total += 8 + 2 + strlen(sorted[i].name) + 1;
		if(m->has_title)  total += 1 + fieldLen(m->title);
		if(m->has_artist) total += 1 + fieldLen(m->artist);
		if(m->has_album)  total += 1 + fieldLen(m->album);
	}

	if((data = malloc(total)) == NULL)
	{
		free(sorted);
		return -1;
	}

	p = wrBytes(data, METACACHE_MAGIC, 4);
	p = wrUint(p, (uint64_t)count, 4);
	p = wrUint(p, dirLen, 2);
	p = wrBytes(p, dir, dirLen);

	for(int i = 0; i < count; i++)
	{
		const struct music_metadata* m = &sorted[i].meta;
		size_t nameLen = strlen(sorted[i].name);
		unsigned flags = (m->has_title ? 1u : 0u) |
		                 (m->has_artist ? 2u : 0u) |
		                 (m->has_album ? 4u : 0u);

		p = wrUint(p, sorted[i].mtime, 8);
		p = wrUint(p, nameLen, 2);
		p = wrBytes(p, sorted[i].name, nameLen);
		p = wrUint(p, flags, 1);

		if(m->has_title)
		{
			p = wrUint(p, fieldLen(m->title), 1);
			p = wrBytes(p, m->title, fieldLen(m->title));
		}
		if(m->has_artist)
		{
			p = wrUint(p, fieldLen(m->artist), 1);
			p = wrBytes(p, m->artist, fieldLen(m->artist));
		}
		if(m->has_album)
		{
			p = wrUint(p, fieldLen(m->album), 1);
			p = wrBytes(p, m->album, fieldLen(m->album));
		}
	}

	cachePath(dir, path, sizeof(path));

	lockCache();
	makeDirs(METACACHE_DIR);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if(fd >= 0)
	{
		if(write(fd, data, total) == (ssize_t)total)
			ret = 0;
		close(fd);

		/* Don't leave a truncated file behind (load would reject it
		 * anyway, but there's no point keeping it). */
		if(ret != 0)
			unlink(path);
	}
	unlockCache();

	free(data);
	free(sorted);
	return ret;
}

void metaCacheFree(struct metaCache* mc)
{
	for(int i = 0; i < mc->count; i++)
		free(mc->entries[i].name);

	free(mc->entries);
	mc->entries = NULL;
	mc->count = 0;
}

uint64_t metaCacheGetMtime(const char* path)
{
#ifdef __3DS__
	/* One FS control call; unlike stat() this doesn't open the file. */
	u64 mtime = 0;

	if(R_FAILED(archive_getmtime(path, &mtime)))
		return 0;

	return mtime;
#else
	struct stat st;

	if(stat(path, &st) != 0)
		return 0;

	return (uint64_t)st.st_mtime;
#endif
}

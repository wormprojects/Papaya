/**
 * ctrmus - 3DS Music Player
 * File list for search. See searchindex.h.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "natsort.h"
#include "searchindex.h"

int searchIndexInit(struct searchIndex* idx, const char* root)
{
	size_t len;

	memset(idx, 0, sizeof(*idx));
	snprintf(idx->root, sizeof(idx->root), "%s", root);
	len = strlen(idx->root);
	while(len > 1 && idx->root[len - 1] == '/' && idx->root[len - 2] != ':')
		idx->root[--len] = '\0';

	idx->pool = malloc(SEARCH_POOL_BYTES);
	idx->off = malloc(SEARCH_MAX_FILES * sizeof(idx->off[0]));
	idx->nameAt = malloc(SEARCH_MAX_FILES * sizeof(idx->nameAt[0]));
	idx->titleOff = malloc(SEARCH_MAX_FILES * sizeof(idx->titleOff[0]));
	if(idx->pool == NULL || idx->off == NULL || idx->nameAt == NULL ||
	   idx->titleOff == NULL)
	{
		searchIndexFree(idx);
		return -1;
	}
	return 0;
}

void searchIndexFree(struct searchIndex* idx)
{
	free(idx->pool);
	free(idx->off);
	free(idx->nameAt);
	free(idx->titleOff);
	idx->titleOff = NULL;
	idx->pool = NULL;
	idx->off = NULL;
	idx->nameAt = NULL;
	idx->count = 0;
}

int searchIndexCount(const struct searchIndex* idx)
{
	return __atomic_load_n(&idx->count, __ATOMIC_ACQUIRE);
}

/* Append "rel/name" (and its title, if any) as the next entry. False once
 * full. */
static bool addEntry(struct searchIndex* idx, const char* rel, size_t relLen,
                     const char* name, const char* title)
{
	size_t	nameLen = strlen(name);
	size_t	titleLen = title != NULL ? strlen(title) + 1 : 0;
	size_t	need = relLen + (relLen > 0 ? 1 : 0) + nameLen + 1 + titleLen;
	int		n = idx->count;
	char*	p;

	if(n >= SEARCH_MAX_FILES || idx->used + need > SEARCH_POOL_BYTES ||
	   relLen + 1 > UINT16_MAX)
	{
		idx->truncated = true;
		return false;
	}

	p = idx->pool + idx->used;
	if(relLen > 0)
	{
		memcpy(p, rel, relLen);
		p[relLen] = '/';
		p += relLen + 1;
	}
	memcpy(p, name, nameLen + 1);
	idx->titleOff[n] = 0;
	if(titleLen > 0)
	{
		memcpy(p + nameLen + 1, title, titleLen);
		idx->titleOff[n] = (uint32_t)((size_t)(p - idx->pool) + nameLen + 1);
	}

	idx->off[n] = (uint32_t)idx->used;
	idx->nameAt[n] = (uint16_t)(relLen > 0 ? relLen + 1 : 0);
	idx->used += need;
	__atomic_store_n(&idx->count, n + 1, __ATOMIC_RELEASE);
	return true;
}

/* The names in one folder, read before going any deeper (so only one
 * folder is open at a time). */
struct names
{
	char**	v;
	int		n, cap;
};

static bool push(struct names* l, const char* s)
{
	char* copy;

	if(l->n == l->cap)
	{
		int		cap = l->cap ? l->cap * 2 : 32;
		char**	v = realloc(l->v, (size_t)cap * sizeof(char*));

		if(v == NULL)
			return false;
		l->v = v;
		l->cap = cap;
	}
	if((copy = strdup(s)) == NULL)
		return false;
	l->v[l->n++] = copy;
	return true;
}

static void freeNames(struct names* l)
{
	for(int i = 0; i < l->n; i++)
		free(l->v[i]);
	free(l->v);
}

/*
 * Read one folder (root + "/" + rel), add its audio files, then go into
 * its subfolders. `rel` has room for 1024 bytes.
 */
static void walk(struct searchIndex* idx, char* rel, size_t relLen,
                 int depth)
{
	char			path[1024 + 1024 + 2];
	DIR*			dp;
	struct dirent*	ep;
	struct names	files = { NULL, 0, 0 };
	struct names	dirs = { NULL, 0, 0 };

	if(idx->cancel || depth > SEARCH_MAX_DEPTH)
		return;

	snprintf(path, sizeof(path), "%s%s%.*s", idx->root,
	         relLen > 0 && idx->root[strlen(idx->root) - 1] != '/' ? "/" : "",
	         (int)relLen, rel);
	if((dp = opendir(path)) == NULL)
		return;

	while((ep = readdir(dp)) != NULL && !idx->cancel)
	{
		/* Hidden, like the browser: macOS's "._song.mp3" junk etc. */
		if(ep->d_name[0] == '.')
			continue;
		if(ep->d_type == DT_DIR)
			push(&dirs, ep->d_name);
		else if(getFileTypeFromExt(ep->d_name) != FILE_TYPE_ERROR)
			push(&files, ep->d_name);
	}
	closedir(dp);
	idx->folders++;

	qsort(files.v, (size_t)files.n, sizeof(char*), naturalCompareP);
	qsort(dirs.v, (size_t)dirs.n, sizeof(char*), naturalCompareP);

	{
		void* th = idx->titlesOpen != NULL && files.n > 0 ?
		           idx->titlesOpen(path) : NULL;

		for(int i = 0; i < files.n && !idx->cancel; i++)
		{
			char title[128];
			bool has = th != NULL && idx->title != NULL &&
			           idx->title(th, files.v[i], title, sizeof(title)) &&
			           title[0] != '\0';

			if(!addEntry(idx, rel, relLen, files.v[i], has ? title : NULL))
				break;
		}
		if(th != NULL && idx->titlesClose != NULL)
			idx->titlesClose(th);
	}
	freeNames(&files);

	if(idx->pace != NULL)
		idx->pace();

	for(int i = 0; i < dirs.n && !idx->cancel && !idx->truncated; i++)
	{
		size_t nameLen = strlen(dirs.v[i]);
		size_t len = relLen + (relLen > 0 ? 1 : 0) + nameLen;

		if(len >= 1024)
			continue;
		if(relLen > 0)
			rel[relLen] = '/';
		memcpy(rel + relLen + (relLen > 0 ? 1 : 0), dirs.v[i], nameLen + 1);
		walk(idx, rel, len, depth + 1);
		rel[relLen] = '\0';
	}
	freeNames(&dirs);
}

void searchIndexScan(struct searchIndex* idx)
{
	char rel[1024];

	rel[0] = '\0';
	walk(idx, rel, 0, 0);
	__atomic_store_n(&idx->done, true, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------ */
/* Matching                                                            */
/* ------------------------------------------------------------------ */

static inline char fold(char c)
{
	if(c >= 'A' && c <= 'Z')
		return (char)(c - 'A' + 'a');
	return c == '_' ? ' ' : c;
}

void searchQueryParse(struct searchQuery* q, const char* text)
{
	char* p;

	snprintf(q->text, sizeof(q->text), "%s", text);
	q->wordCount = 0;
	for(p = q->text; *p != '\0'; p++)
		*p = fold(*p);

	p = q->text;
	while(*p != '\0' && q->wordCount < SEARCH_MAX_WORDS)
	{
		while(*p == ' ')
			*p++ = '\0';
		if(*p == '\0')
			break;
		q->words[q->wordCount++] = p;
		while(*p != '\0' && *p != ' ')
			p++;
	}
	/* Cut the last word off from any spaces after it */
	while(*p != '\0')
		*p++ = '\0';
}

int searchFind(const char* name, const char* word)
{
	size_t wlen = strlen(word);

	if(wlen == 0)
		return -1;
	for(size_t i = 0; name[i] != '\0'; i++)
	{
		size_t k = 0;

		while(k < wlen && name[i + k] != '\0' &&
		      fold(name[i + k]) == word[k])
			k++;
		if(k == wlen)
			return (int)i;
	}
	return -1;
}

bool searchMatch(const struct searchQuery* q, const char* name)
{
	if(q->wordCount == 0)
		return false;
	for(int i = 0; i < q->wordCount; i++)
		if(searchFind(name, q->words[i]) < 0)
			return false;
	return true;
}

bool searchMatchTitle(const struct searchQuery* q, const char* name,
                      const char* title)
{
	if(q->wordCount == 0)
		return false;
	for(int i = 0; i < q->wordCount; i++)
		if(searchFind(name, q->words[i]) < 0 &&
		   (title == NULL || searchFind(title, q->words[i]) < 0))
			return false;
	return true;
}

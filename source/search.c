/**
 * ctrmus - 3DS Music Player
 * Search. See search.h; drawn by bottomscreen.c, file list from
 * searchindex.c.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bottomscreen.h"
#include "browser.h"
#include "metacache.h"
#include "playback.h"
#include "player.h"
#include "queue.h"
#include "search.h"
#include "ui.h"

#define CARET_MS		500
/* While the folder tree is being listed, redraw at most this often */
#define SCAN_DRAW_MS	150
#define BADGE_MS		1200
/* Pause after each folder while music plays, so its SD reads go first */
#define PACE_MS			2

static void pace(void)
{
	if(isPlaying())
		svcSleepThread(PACE_MS * 1000000LL);
}

/* Tag titles come from the tag cache of each folder: only folders that
 * were opened in the browser before (with tags on) have them. */
static void* titlesOpen(const char* dir)
{
	struct metaCache* mc = calloc(1, sizeof(*mc));

	if(mc == NULL)
		return NULL;
	if(metaCacheLoad(dir, mc) <= 0)
	{
		metaCacheFree(mc);
		free(mc);
		return NULL;
	}
	return mc;
}

static bool titleOf(void* h, const char* name, char* out, size_t len)
{
	const struct metaCacheEntry* e = metaCacheFind(h, name);

	if(e == NULL || !e->meta.has_title || e->meta.title[0] == '\0')
		return false;
	snprintf(out, len, "%s", e->meta.title);
	return true;
}

static void titlesClose(void* h)
{
	metaCacheFree(h);
	free(h);
}

static void workerMain(void* arg)
{
	searchIndexScan((struct searchIndex*)arg);
}

void searchOpen(struct search* s, const struct browser* b, bool fromMenu)
{
	s32 prio = 0x30;

	memset(s, 0, sizeof(*s));
	s->fromMenu = fromMenu;
	s->typing = true;
	s->keyRow = 1;			/* on the letters */
	s->keyCol = 0;
	s->pressedKey = -1;
	s->selected = -1;
	s->badgeMatch = -1;
	s->caret = true;
	s->caretMs = osGetTime();
	s->dirty = true;
	searchQueryParse(&s->query, "");

	s->matches = malloc(SEARCH_MAX_MATCHES * sizeof(int));
	s->idxOk = s->matches != NULL && b->list.currentDir != NULL &&
	           searchIndexInit(&s->idx, b->list.currentDir) == 0;
	if(!s->idxOk)
		return;
	s->idx.pace = pace;
	s->idx.titlesOpen = titlesOpen;
	s->idx.title = titleOf;
	s->idx.titlesClose = titlesClose;

	/* Below the UI thread, on the same core: it lists folders while the
	 * UI waits for the next frame. */
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	if(prio < 0x3F)
		prio++;
	s->worker = threadCreate(workerMain, &s->idx, 128 * 1024, prio, -2,
	                         false);
	if(s->worker == NULL)
		s->idx.done = true;
}

void searchClose(struct search* s)
{
	if(s->worker != NULL)
	{
		s->idx.cancel = true;
		threadJoin(s->worker, U64_MAX);
		threadFree(s->worker);
		s->worker = NULL;
	}
	if(s->idxOk)
		searchIndexFree(&s->idx);
	s->idxOk = false;
	free(s->matches);
	s->matches = NULL;
}

/* ------------------------------------------------------------------ */
/* Results                                                             */
/* ------------------------------------------------------------------ */

static int rowsShown(const struct search* s)
{
	return s->typing ? SS_ROWS_TYPING : SS_ROWS_LIST;
}

static void keepSelectionVisible(struct search* s)
{
	int rows = rowsShown(s);
	int maxTop = s->listed - rows;

	if(s->selected >= 0)
	{
		if(s->selected < s->top)
			s->top = s->selected;
		if(s->selected >= s->top + rows)
			s->top = s->selected - rows + 1;
	}
	if(s->top > maxTop)
		s->top = maxTop;
	if(s->top < 0)
		s->top = 0;
}

static void setText(struct search* s)
{
	searchQueryParse(&s->query, s->text);
	s->requery = true;
	s->dirty = true;
}

/* Match the index entries that came in (or all, after the query
 * changed). */
static bool matchNew(struct search* s)
{
	int		count;
	bool	more = false;

	if(!s->idxOk)
		return false;
	count = searchIndexCount(&s->idx);

	if(s->requery)
	{
		s->listed = s->found = s->checked = 0;
		s->top = 0;
		s->selected = -1;
		s->buttonsFocus = false;
		s->badgeMatch = -1;
		s->requery = false;
		more = true;
	}
	if(s->query.wordCount == 0)
	{
		s->checked = count;
		return more;
	}
	for(; s->checked < count; s->checked++)
	{
		if(!searchMatchTitle(&s->query, searchIndexName(&s->idx, s->checked),
		                     searchIndexTitle(&s->idx, s->checked)))
			continue;
		if(s->listed < SEARCH_MAX_MATCHES)
			s->matches[s->listed++] = s->checked;
		s->found++;
		more = true;
	}
	if(!s->typing && s->selected < 0 && s->listed > 0)
		s->selected = 0;
	return more;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/* "Album/Disc 1" for match m ("" in the searched folder itself). */
static const char* folderOf(const struct search* s, int m, char* buf,
                            size_t len)
{
	const char*	rel = searchIndexPath(&s->idx, m);
	int			dirLen = s->idx.nameAt[m];

	snprintf(buf, len, "%.*s", dirLen > 0 ? dirLen - 1 : 0, rel);
	return buf;
}

static void draw(struct search* s)
{
	static char			folders[SS_ROWS_LIST][256];
	static char			status[256];
	struct ssView		v;
	struct botCanvas	c;
	struct topColors	col;
	int					rows = rowsShown(s);

	memset(&v, 0, sizeof(v));
	v.root = s->idxOk ? s->idx.root : "";
	v.query = s->text;
	v.words = (const char* const*)s->query.words;
	v.wordCount = s->query.wordCount;
	v.typing = s->typing;
	v.caret = s->caret;
	v.keyRow = s->keyRow;
	v.keyCol = s->keyCol;
	v.pressedKey = s->pressedKey;
	v.found = s->found;
	v.listed = s->listed;
	v.top = s->top;
	v.selected = s->selected;
	v.buttonsFocus = s->buttonsFocus;
	v.button = s->button;
	v.scanning = s->idxOk && !s->idx.done;
	v.scanned = s->idxOk ? searchIndexCount(&s->idx) : 0;
	v.truncated = s->idxOk && s->idx.truncated;
	v.badgeRow = -1;

	for(int i = 0; i < rows && s->top + i < s->listed; i++)
	{
		int m = s->matches[s->top + i];

		/* The file name, unless the match is in the title: then the
		 * title, so the highlight is visible. */
		v.names[i] = searchMatch(&s->query, searchIndexName(&s->idx, m)) ?
		             searchIndexName(&s->idx, m) :
		             searchIndexTitle(&s->idx, m);
		v.folders[i] = folderOf(s, m, folders[i], sizeof(folders[i]));
		if(s->top + i == s->badgeMatch && s->badge != NULL)
		{
			v.badgeRow = i;
			v.badge = s->badge;
		}
		v.rows++;
	}
	if(s->selected >= 0 && s->selected < s->listed)
	{
		folderOf(s, s->matches[s->selected], status, sizeof(status));
		v.status = status[0] != '\0' ? status : "this folder";
	}

	uiBottomCanvas(&c, &col);
	ssDraw(&c, &col, &v);
	uiBottomPresent();

	s->dirty = false;
	s->drawnMs = osGetTime();
	s->drawnCount = v.scanned;
	s->drawnScanning = v.scanning;
}

void searchUpdate(struct search* s)
{
	u64		now = osGetTime();
	bool	scanChanged;

	if(matchNew(s))
		s->dirty = true;

	if(s->typing && now - s->caretMs >= CARET_MS)
	{
		s->caretMs = now;
		s->caret = !s->caret;
		s->dirty = true;
	}
	if(s->badge != NULL && now >= s->badgeUntil)
	{
		s->badge = NULL;
		s->badgeMatch = -1;
		s->dirty = true;
	}

	scanChanged = s->idxOk &&
	              (searchIndexCount(&s->idx) != s->drawnCount ||
	               s->idx.done == s->drawnScanning);
	if(s->dirty || (scanChanged && now - s->drawnMs >= SCAN_DRAW_MS))
		draw(s);
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static void showResults(struct search* s)
{
	s->typing = false;
	s->pressedKey = -1;
	s->buttonsFocus = false;
	if(s->selected < 0 && s->listed > 0)
		s->selected = 0;
	keepSelectionVisible(s);
	s->dirty = true;
}

static void showKeyboard(struct search* s)
{
	s->typing = true;
	s->buttonsFocus = false;
	s->caret = true;
	s->caretMs = osGetTime();
	keepSelectionVisible(s);
	s->dirty = true;
}

static void pressKey(struct search* s, int code)
{
	size_t len = strlen(s->text);

	switch(code)
	{
		case SS_KEY_DEL:
			if(len == 0)
				return;
			s->text[len - 1] = '\0';
			break;
		case SS_KEY_CLEAR:
			if(len == 0)
				return;
			s->text[0] = '\0';
			break;
		case SS_KEY_DONE:
			showResults(s);
			return;
		default:
			if(len >= SEARCH_QUERY_MAX || code <= 0)
				return;
			s->text[len] = (char)code;
			s->text[len + 1] = '\0';
			break;
	}
	s->caret = true;
	s->caretMs = osGetTime();
	setText(s);
}

/* Absolute folder of match m: the searched folder + its relative one. */
static void absFolder(const struct search* s, int m, char* out, size_t len)
{
	const char*	root = s->idx.root;
	const char*	rel = searchIndexPath(&s->idx, m);
	int			dirLen = s->idx.nameAt[m];
	bool		slash = root[0] != '\0' && root[strlen(root) - 1] == '/';

	if(dirLen == 0)
		snprintf(out, len, "%s", root);
	else
		snprintf(out, len, "%s%s%.*s", root, slash ? "" : "/",
		         dirLen - 1, rel);
}

/* Show the selected match's folder in the browser, with the cursor on
 * it, and play it if asked. */
static bool openSelected(struct search* s, struct browser* b, bool play)
{
	char		dir[PATH_MAX];
	const char*	rel;
	int			m, levels = 0, idx;

	if(s->selected < 0 || s->selected >= s->listed)
		return false;
	m = s->matches[s->selected];
	rel = searchIndexPath(&s->idx, m);
	for(int i = 0; i < s->idx.nameAt[m]; i++)
		if(rel[i] == '/')
			levels++;
	absFolder(s, m, dir, sizeof(dir));

	idx = browserShowFile(b, dir, searchIndexName(&s->idx, m), levels);
	if(idx < 0)
		return false;
	if(play)
		playerPlayFromBrowser(b, idx);
	return true;
}

static void queueSelected(struct search* s)
{
	char	dir[PATH_MAX], path[PATH_MAX];
	int		m, before = queueSize(playerQueue());

	if(s->selected < 0 || s->selected >= s->listed)
		return;
	m = s->matches[s->selected];
	absFolder(s, m, dir, sizeof(dir));
	if(snprintf(path, sizeof(path), "%s%s%s", dir,
	            dir[strlen(dir) - 1] == '/' ? "" : "/",
	            searchIndexName(&s->idx, m)) >= (int)sizeof(path))
		return;
	playerEnqueue(path, NULL);

	s->badgeMatch = s->selected;
	s->badge = queueSize(playerQueue()) > before ? "queued" : "queue full";
	s->badgeUntil = osGetTime() + BADGE_MS;
	s->dirty = true;
}

static enum page_result leave(struct search* s)
{
	return s->fromMenu ? PAGE_TO_MENU : PAGE_CLOSE;
}

/* Results page button (or the matching key) */
static enum page_result pressButton(struct search* s, struct browser* b,
                                    int button)
{
	switch(button)
	{
		case SS_BTN_PLAY:
			if(openSelected(s, b, true))
				return PAGE_CLOSE;
			break;
		case SS_BTN_QUEUE:
			queueSelected(s);
			break;
		case SS_BTN_OPEN:
			if(openSelected(s, b, false))
				return PAGE_CLOSE;
			break;
		case SS_BTN_TYPE:
			showKeyboard(s);
			break;
	}
	return PAGE_STAY;
}

/* Keyboard cursor: Left/Right skip over the rest of a wide key. */
static void moveKey(struct search* s, int dx, int dy)
{
	int r = s->keyRow, c = s->keyCol;
	int code = ssKeyCode(r, c);

	if(dy != 0)
		r = (r + dy + SS_KB_ROWS) % SS_KB_ROWS;
	if(dx > 0)
	{
		while(c < SS_KB_COLS && ssKeyCode(r, c) == code)
			c++;
		if(c >= SS_KB_COLS)
			c = 0;
	}
	else if(dx < 0)
	{
		while(c >= 0 && ssKeyCode(r, c) == code)
			c--;
		if(c < 0)
			c = SS_KB_COLS - 1;
	}
	s->keyRow = r;
	s->keyCol = c;
	s->dirty = true;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static enum page_result touchInput(struct search* s, u32 kDown, u32 kHeld,
                                   struct browser* b)
{
	struct ssView	v;
	touchPosition	tp;

	if(!(kHeld & KEY_TOUCH))
	{
		if(s->pressedKey >= 0)
		{
			s->pressedKey = -1;
			s->dirty = true;
		}
		s->scrolling = false;
		return PAGE_STAY;
	}
	hidTouchRead(&tp);

	/* Just enough of the view for the hit tests */
	memset(&v, 0, sizeof(v));
	v.typing = s->typing;
	v.found = s->found;
	v.listed = s->listed;
	v.rows = s->listed - s->top;
	if(v.rows > rowsShown(s))
		v.rows = rowsShown(s);

	if(s->typing)
	{
		int r, c, code;

		/* A key: pressed on touch, repeats while held there */
		if(kDown & KEY_TOUCH)
		{
			int row;

			if((code = ssKeyAt(tp.px, tp.py, &r, &c)) >= 0)
			{
				s->pressedKey = code;
				s->keyRow = r;
				s->keyCol = c;
				keyRepeat(&s->keyHold, KEY_TOUCH, KEY_TOUCH, KEY_TOUCH);
				pressKey(s, code);
				return PAGE_STAY;
			}
			if((row = ssRowAt(&v, tp.px, tp.py)) >= 0)
			{
				s->selected = s->top + row;
				showResults(s);
			}
			return PAGE_STAY;
		}
		code = ssKeyAt(tp.px, tp.py, &r, &c);
		if(s->pressedKey >= 0 && code != s->pressedKey)
		{
			s->pressedKey = -1;		/* slid off the key */
			s->dirty = true;
		}
		else if(s->pressedKey >= 0 && s->pressedKey != SS_KEY_DONE &&
		        keyRepeat(&s->keyHold, 0, KEY_TOUCH, KEY_TOUCH))
			pressKey(s, s->pressedKey);
		return PAGE_STAY;
	}

	if(kDown & KEY_TOUCH)
	{
		int button, row;

		if(ssOnField(tp.px, tp.py))
		{
			showKeyboard(s);
			return PAGE_STAY;
		}
		if((button = ssButtonAt(tp.px, tp.py)) >= 0)
			return pressButton(s, b, button);
		if(ssOnScrollbar(&v, tp.px, tp.py))
			s->scrolling = true;
		else if((row = ssRowAt(&v, tp.px, tp.py)) >= 0)
		{
			s->selected = s->top + row;
			s->buttonsFocus = false;
			s->dirty = true;
		}
	}
	if(s->scrolling)
	{
		int maxTop = s->listed - rowsShown(s);
		int top;

		if(maxTop < 0)
			maxTop = 0;
		top = (int)(ssScrollFraction(&v, tp.py) * maxTop + 0.5f);
		if(top != s->top)
		{
			s->top = top;
			if(s->selected < top)
				s->selected = top;
			if(s->selected >= top + rowsShown(s))
				s->selected = top + rowsShown(s) - 1;
			s->dirty = true;
		}
	}
	return PAGE_STAY;
}

enum page_result searchInput(struct search* s, u32 kDown, u32 kHeld,
                             struct browser* b)
{
	u32					dirs = keyRepeat(&s->dirRepeat, kDown, kHeld,
	                                     KEY_UP | KEY_DOWN | KEY_LEFT |
	                                     KEY_RIGHT);
	enum page_result	r;

	if(kDown & KEY_SELECT)
		return PAGE_CLOSE;

	if((r = touchInput(s, kDown, kHeld, b)) != PAGE_STAY)
		return r;

	if(s->typing)
	{
		u32 held = keyRepeat(&s->keyHold, kDown, kHeld, KEY_A | KEY_B);

		if(dirs & KEY_UP)
			moveKey(s, 0, -1);
		else if(dirs & KEY_DOWN)
			moveKey(s, 0, 1);
		else if(dirs & KEY_LEFT)
			moveKey(s, -1, 0);
		else if(dirs & KEY_RIGHT)
			moveKey(s, 1, 0);

		if(kDown & KEY_Y)
			showResults(s);
		else if(held & KEY_A)
		{
			int code = ssKeyCode(s->keyRow, s->keyCol);

			if(code != SS_KEY_DONE || (kDown & KEY_A))
				pressKey(s, code);
		}
		else if(held & KEY_B)
		{
			/* B on an empty field closes (not on a held repeat) */
			if(s->text[0] == '\0')
			{
				if(kDown & KEY_B)
					return leave(s);
			}
			else
				pressKey(s, SS_KEY_DEL);
		}
		return PAGE_STAY;
	}

	/* Results page */
	if(kDown & KEY_B)
	{
		showKeyboard(s);
		return PAGE_STAY;
	}

	if(s->buttonsFocus)
	{
		if(kDown & KEY_A)
			return pressButton(s, b, s->button);
		if(dirs & KEY_LEFT)
			s->button = (s->button + SS_BTN_COUNT - 1) % SS_BTN_COUNT;
		else if(dirs & KEY_RIGHT)
			s->button = (s->button + 1) % SS_BTN_COUNT;
		else if((dirs & KEY_UP) && s->listed > 0)
		{
			s->buttonsFocus = false;
			keepSelectionVisible(s);
		}
		if(dirs)
			s->dirty = true;
		return PAGE_STAY;
	}

	if(kDown & KEY_A)
		return pressButton(s, b, SS_BTN_PLAY);
	if(kDown & KEY_Y)
		queueSelected(s);
	else if(kDown & KEY_RIGHT)
		return pressButton(s, b, SS_BTN_OPEN);
	else if(dirs & KEY_UP)
	{
		if(s->selected > 0)
			s->selected--;
		keepSelectionVisible(s);
		s->dirty = true;
	}
	else if(dirs & KEY_DOWN)
	{
		if(s->selected < s->listed - 1)
			s->selected++;
		else if(kDown & KEY_DOWN)
		{
			s->buttonsFocus = true;
			s->button = s->listed > 0 ? SS_BTN_PLAY : SS_BTN_TYPE;
		}
		keepSelectionVisible(s);
		s->dirty = true;
	}
	else if(dirs & KEY_LEFT)
	{
		/* A page up */
		s->selected -= rowsShown(s);
		if(s->selected < 0)
			s->selected = s->listed > 0 ? 0 : -1;
		keepSelectionVisible(s);
		s->dirty = true;
	}
	return PAGE_STAY;
}

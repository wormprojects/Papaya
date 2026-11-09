/**
 * ctrmus - 3DS Music Player
 * The queue editor. See queueedit.h; drawn by bottomscreen.c.
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "bottomscreen.h"
#include "player.h"
#include "queue.h"
#include "queueedit.h"
#include "settings.h"
#include "titles.h"
#include "ui.h"

/* Dragging a song past the list's top/bottom scrolls it this often (ms) */
#define AUTOSCROLL_MS	110
/* The stylus has to move this far (px) before a press becomes a drag */
#define DRAG_START_PX	6

/* The playlist songs shown after the queue (the strings belong to the
 * player: refreshed whenever its playlist changes). */
#define PL_MAX	2000

static const char*	plTitles[PL_MAX];
static const char*	plFiles[PL_MAX];
static int			plCount;

static struct queue_t* q(void)
{
	return playerQueue();
}

/* Rows: the queued songs, then the playlist's. */
static int total(void)
{
	return q()->count + plCount;
}

static bool inQueue(int row)
{
	return row >= 0 && row < q()->count;
}

/* The rows a song can move between: its own list. */
static void region(int row, int* lo, int* hi)
{
	if(inQueue(row))
	{
		*lo = 0;
		*hi = q()->count - 1;
	}
	else
	{
		*lo = q()->count;
		*hi = total() - 1;
	}
}

static int maxTop(void)
{
	int m = total() - QE_ROWS;

	return m > 0 ? m : 0;
}

/* Re-read the playlist songs. With `follow`, the selection stays on the
 * same playlist song (by file name) when the list shifted. */
static void refreshPlaylist(struct queueEdit* e, bool follow)
{
	plCount = playerPlaylistAfter(plTitles, plFiles, PL_MAX);
	e->plVersion = playerPlaylistVersion();

	if(follow && e->selIsPl)
	{
		int found = -1;

		for(int i = 0; i < plCount && e->selFile[0] != '\0'; i++)
			if(strcmp(plFiles[i], e->selFile) == 0)
			{
				found = i;
				break;
			}
		e->selected = q()->count + (found >= 0 ? found : 0);
		e->lifted = e->lifted && found >= 0;
	}
}

/* Keep the selection valid and on screen. */
static void clampSelection(struct queueEdit* e)
{
	int n = total();

	if(n == 0)
	{
		e->selected = -1;
		e->lifted = false;
		e->buttonsFocus = false;
		e->confirm = false;
	}
	else
	{
		if(e->selected >= n)
			e->selected = n - 1;
		if(e->selected < 0 && !e->buttonsFocus)
			e->selected = 0;
	}
	if(e->selected >= 0)
	{
		if(e->selected < e->top)
			e->top = e->selected;
		if(e->selected >= e->top + QE_ROWS)
			e->top = e->selected - QE_ROWS + 1;
	}
	if(e->top > maxTop())
		e->top = maxTop();
	if(e->top < 0)
		e->top = 0;
}

/* Our own change to the queue: it's already accounted for. */
static void changed(struct queueEdit* e)
{
	e->version = q()->version;
	e->pops = q()->pops;
	clampSelection(e);
	e->dirty = true;
	uiRequestRedraw();		/* the top screen's queue list */
}

static const char* baseName(const char* path)
{
	const char* slash = strrchr(path, '/');

	return slash != NULL ? slash + 1 : path;
}

/* What a row shows: like the top screen's lists (tag title if known,
 * else the filename per the "artists in names" setting). */
static const char* label(const struct queue_entry* qe, char* buf,
                         size_t len)
{
	const char* file = baseName(qe->filepath);

	if(!settingsListTitles())
		return settingsShortName(file, buf, len);
	if(strcmp(qe->title, file) != 0)
		return qe->title;
	if(titlesLookup(qe->filepath, buf, len) == 1)
		return buf;
	return settingsShortName(file, buf, len);
}

static void draw(struct queueEdit* e)
{
	static char			labels[QE_ROWS][128];
	static char			folder[PATH_MAX];
	struct qeView		v;
	struct botCanvas	c;
	struct topColors	col;

	memset(&v, 0, sizeof(v));
	v.count = total();
	v.qCount = q()->count;
	v.max = MAX_QUEUE_SIZE;
	v.top = e->top;
	v.selected = e->selected;
	v.lifted = e->lifted;
	v.buttonsFocus = e->buttonsFocus;
	v.button = e->button;
	v.confirm = e->confirm;

	for(int i = 0; i < QE_ROWS && e->top + i < v.count; i++)
	{
		int n = e->top + i;

		if(inQueue(n))
			v.labels[i] = label(queueGet(q(), n), labels[i],
			                    sizeof(labels[i]));
		else
			v.labels[i] = plTitles[n - q()->count];
	}

	/* Remember which playlist song is selected, for following it. */
	e->selIsPl = e->selected >= q()->count && e->selected < v.count;
	e->selFile[0] = '\0';
	if(e->selIsPl)
		snprintf(e->selFile, sizeof(e->selFile), "%s",
		         plFiles[e->selected - q()->count]);

	if(inQueue(e->selected))
	{
		const char*	path = queueGet(q(), e->selected)->filepath;
		const char*	slash = strrchr(path, '/');

		if(slash != NULL)
		{
			snprintf(folder, sizeof(folder), "%.*s", (int)(slash - path),
			         path);
			v.status = folder;
		}
	}
	else if(e->selIsPl && playerPlaylistDir() != NULL)
		v.status = playerPlaylistDir();

	uiBottomCanvas(&c, &col);
	qeDraw(&c, &col, &v);
	uiBottomPresent();
	e->titles = titlesGeneration();
	e->dirty = false;
}

void queueEditOpen(struct queueEdit* e, bool fromMenu, struct browser* b)
{
	memset(e, 0, sizeof(*e));
	e->fromMenu = fromMenu;
	e->browser = b;
	refreshPlaylist(e, false);
	e->selected = total() > 0 ? 0 : -1;
	changed(e);
	draw(e);
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static void playSelected(struct queueEdit* e)
{
	if(e->selected < 0 || e->selected >= total())
		return;
	if(inQueue(e->selected))
	{
		playerPlayQueued(e->selected);	/* the next one moves up into place */
		changed(e);
		return;
	}

	/* A playlist song: it plays now and the playlist carries on from it
	 * (the songs before it are skipped). The queue stays as it is. */
	playerPlaylistPlay(e->browser, e->selected - q()->count);
	refreshPlaylist(e, false);
	e->selected = q()->count;
	changed(e);
}

/* Only queued songs can be removed (skipping a playlist song is just
 * playing the one after it). */
static void removeSelected(struct queueEdit* e)
{
	if(!inQueue(e->selected))
		return;
	queueRemove(q(), e->selected);
	changed(e);
}

/* Move the selected song to row `to`, within its own list. */
static void moveSelected(struct queueEdit* e, int to)
{
	int lo, hi;

	if(e->selected < 0)
		return;
	region(e->selected, &lo, &hi);
	if(to < lo)
		to = lo;
	if(to > hi)
		to = hi;
	if(to == e->selected)
		return;

	if(inQueue(e->selected))
		queueMove(q(), e->selected, to);
	else
	{
		/* One neighbour swap at a time */
		int dir = to > e->selected ? 1 : -1;

		for(int row = e->selected; row != to; row += dir)
			if(!playerPlaylistMove(row - q()->count, dir))
			{
				to = row;
				break;
			}
		refreshPlaylist(e, false);
		if(to == e->selected)
			return;
	}
	e->selected = to;
	changed(e);
}

static void lift(struct queueEdit* e)
{
	if(e->selected < 0)
		return;
	e->lifted = true;
	e->liftFrom = e->selected;
	e->dirty = true;
}

static void drop(struct queueEdit* e)
{
	e->lifted = false;
	e->dirty = true;
}

static void putBack(struct queueEdit* e)
{
	moveSelected(e, e->liftFrom);
	e->lifted = false;
	e->dirty = true;
}

static void pressButton(struct queueEdit* e, int b)
{
	switch(b)
	{
		case QE_BTN_PLAY:	playSelected(e); break;
		case QE_BTN_UP:		moveSelected(e, e->selected - 1); break;
		case QE_BTN_DOWN:	moveSelected(e, e->selected + 1); break;
		case QE_BTN_REMOVE:	removeSelected(e); break;
		case QE_BTN_CLEAR:
			if(q()->count > 0)
			{
				e->confirm = true;
				e->button = QE_CONFIRM_CLEAR;
				e->dirty = true;
			}
			break;
	}
}

static void answerConfirm(struct queueEdit* e, int b)
{
	e->confirm = false;
	e->button = QE_BTN_CLEAR;
	if(b == QE_CONFIRM_CLEAR)
	{
		queueClear(q());
		changed(e);
	}
	e->dirty = true;
}

static enum page_result leave(struct queueEdit* e, bool toBrowser)
{
	return e->fromMenu && !toBrowser ? PAGE_TO_MENU : PAGE_CLOSE;
}

/* ------------------------------------------------------------------ */
/* Stylus                                                              */
/* ------------------------------------------------------------------ */

static void touch(struct queueEdit* e, u32 kDown, u32 kHeld)
{
	struct qeView	v;
	touchPosition	tp;

	if(!(kHeld & KEY_TOUCH))
	{
		if(e->dragging)
			drop(e);
		e->touching = e->dragging = e->scrolling = false;
		return;
	}
	hidTouchRead(&tp);
	memset(&v, 0, sizeof(v));
	v.count = total();

	if(kDown & KEY_TOUCH)
	{
		int b = qeButtonAt(tp.px, tp.py, e->confirm);
		int row;

		if(b >= 0)
		{
			if(e->lifted)
				drop(e);	/* a lifted song is put down first */
			if(e->confirm)
				answerConfirm(e, b);
			else
				pressButton(e, b);
			return;
		}
		if(e->confirm || e->lifted)
			return;
		if(qeOnScrollbar(&v, tp.px, tp.py))
		{
			e->scrolling = true;
		}
		else if((row = qeRowAt(tp.px, tp.py)) >= 0 &&
		        e->top + row < v.count)
		{
			e->selected = e->top + row;
			e->buttonsFocus = false;
			e->touching = true;
			e->touchY0 = tp.py;
			e->dirty = true;
		}
	}

	if(e->scrolling)
	{
		int top = (int)(qeScrollFraction(tp.py) * maxTop() + 0.5f);

		if(top != e->top)
		{
			e->top = top;
			/* The selection rides along so it stays on screen */
			if(e->selected < top)
				e->selected = top;
			if(e->selected >= top + QE_ROWS)
				e->selected = top + QE_ROWS - 1;
			e->dirty = true;
		}
		return;
	}

	if(!e->touching)
		return;

	/* Press, then move: pick the song up and carry it */
	if(!e->dragging &&
	   (tp.py - e->touchY0 >= DRAG_START_PX ||
	    e->touchY0 - tp.py >= DRAG_START_PX))
	{
		e->dragging = true;
		lift(e);
	}
	if(e->dragging)
	{
		int row = qeRowAtClamped(tp.py);
		int to;

		/* Past the ends of the list: scroll it, a row at a time */
		if((row < 0 || row >= QE_ROWS) &&
		   osGetTime() - e->autoScrollMs >= AUTOSCROLL_MS)
		{
			e->autoScrollMs = osGetTime();
			if(row < 0 && e->top > 0)
				e->top--;
			else if(row >= QE_ROWS && e->top < maxTop())
				e->top++;
		}
		if(row < 0)
			row = 0;
		if(row >= QE_ROWS)
			row = QE_ROWS - 1;
		to = e->top + row;
		if(to >= total())
			to = total() - 1;
		if(to != e->selected)
		{
			int top = e->top;

			moveSelected(e, to);
			e->top = top;		/* don't let clamping jump the list */
			clampSelection(e);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

enum page_result queueEditInput(struct queueEdit* e, u32 kDown, u32 kHeld)
{
	u32 dirs = keyRepeat(&e->repeat, kDown, kHeld,
	                     KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);

	if(kDown & KEY_SELECT)
	{
		if(e->lifted)
			drop(e);
		return leave(e, true);
	}

	touch(e, kDown, kHeld);

	if(e->confirm)
	{
		if(kDown & KEY_B)
			answerConfirm(e, QE_CONFIRM_KEEP);
		else if(kDown & KEY_A)
			answerConfirm(e, e->button);
		else if(dirs & (KEY_LEFT | KEY_RIGHT))
		{
			e->button = !e->button;
			e->dirty = true;
		}
		return PAGE_STAY;
	}

	if(e->lifted && !e->dragging)
	{
		if(kDown & KEY_B)
			putBack(e);
		else if(kDown & (KEY_A | KEY_LEFT | KEY_RIGHT))
			drop(e);
		else if(dirs & KEY_UP)
			moveSelected(e, e->selected - 1);
		else if(dirs & KEY_DOWN)
			moveSelected(e, e->selected + 1);
		return PAGE_STAY;
	}
	if(e->dragging)
		return PAGE_STAY;

	if(e->buttonsFocus)
	{
		if(kDown & KEY_B)
			return leave(e, false);
		if(kDown & KEY_A)
			pressButton(e, e->button);
		else if(dirs & KEY_LEFT)
		{
			e->button = (e->button + QE_BTN_COUNT - 1) % QE_BTN_COUNT;
			e->dirty = true;
		}
		else if(dirs & KEY_RIGHT)
		{
			e->button = (e->button + 1) % QE_BTN_COUNT;
			e->dirty = true;
		}
		else if((dirs & KEY_UP) && total() > 0)
		{
			e->buttonsFocus = false;
			if(e->selected < 0)
				e->selected = total() - 1;
			clampSelection(e);
			e->dirty = true;
		}
		return PAGE_STAY;
	}

	if(kDown & KEY_B)
		return leave(e, false);
	if(total() == 0)
		return PAGE_STAY;

	if(kDown & KEY_A)
		playSelected(e);
	else if(kDown & KEY_Y)
		removeSelected(e);
	else if(kDown & KEY_RIGHT)
		lift(e);
	else if(dirs & KEY_UP)
	{
		if(e->selected > 0)
		{
			e->selected--;
			clampSelection(e);
			e->dirty = true;
		}
	}
	else if(dirs & KEY_DOWN)
	{
		if(e->selected < total() - 1)
			e->selected++;
		else if(kDown & KEY_DOWN)
		{
			/* Past the last song: the buttons (not on a held repeat,
			 * so holding Down stops at the end of the list) */
			e->buttonsFocus = true;
			e->button = QE_BTN_PLAY;
		}
		clampSelection(e);
		e->dirty = true;
	}
	return PAGE_STAY;
}

void queueEditUpdate(struct queueEdit* e)
{
	const struct queue_t* qq = q();

	/* Songs played from the front of the queue (a song ended, or R):
	 * everything moved up one, so the selection follows its song. */
	if(qq->pops != e->pops)
	{
		int n = (int)(qq->pops - e->pops);

		if(e->selected >= 0)
			e->selected -= n;
		e->liftFrom -= n;
		if(e->lifted && e->selected < 0)
		{
			/* The lifted song itself started playing */
			e->lifted = e->dragging = e->touching = false;
			e->selected = 0;
		}
		if(e->liftFrom < 0)
			e->liftFrom = 0;
		e->pops = qq->pops;
		e->dirty = true;
	}
	if(qq->version != e->version)
	{
		e->version = qq->version;
		clampSelection(e);
		e->dirty = true;
	}
	if(playerPlaylistVersion() != e->plVersion)
	{
		refreshPlaylist(e, true);
		clampSelection(e);
		e->dirty = true;
	}
	if(titlesGeneration() != e->titles)
		e->dirty = true;

	if(e->dirty)
		draw(e);
}

/**
 * ctrmus - 3DS Music Player
 * Bottom-screen file browser. See browser.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "all.h"
#include "browser.h"
#include "settings.h"
#include "themes.h"

void browserInit(struct browser* b, PrintConsole* console)
{
	memset(b, 0, sizeof(*b));
	b->console = console;
}

int browserOpen(struct browser* b)
{
	chdir(DEFAULT_DIR);
	chdir(MUSIC_SUBDIR);

	consoleSelect(b->console);
	consoleClear();

	if(dirListLoad(&b->list, false) < 0)
		return -1;

	b->count = dirListCountEntries();
	b->cursor = 0;
	b->from = 0;
	browserDraw(b);
	return 0;
}

void browserExit(struct browser* b)
{
	dirListFree(&b->list);
	free(b->playlistDir);
	b->playlistDir = NULL;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define CON_COLS		40
#define FOOTER_ROW		(MAX_LIST + 1)		/* 0-based console row */
/* A stylus drag starting this far right (pixels) grabs the scrollbar:
 * wider than the 8px bar itself, so it's easy to hit. */
#define SCROLL_TOUCH_X	(CON_COLS * 8 - 24)

/* libctru console escapes take 1-based row/column. */
static void moveTo(int row, int col)
{
	printf("\033[%d;%dH", row + 1, col + 1);
}

static void setColors(const struct themeColor* fg, const struct themeColor* bg)
{
	printf("\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um", fg->r, fg->g, fg->b,
	       bg->r, bg->g, bg->b);
}

/*
 * A full-width bar: " c> /MUSIC/Album" in the background colour on `bar`.
 * "sdmc:" is dropped, and a path that doesn't fit is cut from the left
 * ("...ic/Album"), so the end of it, the part that matters, stays.
 */
static void drawBar(int row, const char* tag, const char* path,
                    const struct themeColor* bar, int reserve)
{
	const struct theme_t*	t = getCurrentTheme();
	char					line[CON_COLS + 1];
	int						avail, len, n;

	if(path == NULL)
		path = "-";
	else if(strncmp(path, "sdmc:", 5) == 0 && path[5] != '\0')
		path += 5;

	n = snprintf(line, sizeof(line), " %s ", tag);
	avail = CON_COLS - n - 1 - reserve;	/* one space at the right end */
	len = (int)strlen(path);
	if(len > avail)
		n += snprintf(line + n, sizeof(line) - n, "...%s",
		              path + len - (avail - 3));
	else
		n += snprintf(line + n, sizeof(line) - n, "%s", path);
	while(n < CON_COLS)
		line[n++] = ' ';
	line[CON_COLS] = '\0';

	/* 40 characters end exactly at the edge: the console only wraps when
	 * the next character is printed, and we always move first. */
	moveTo(row, 0);
	setColors(&t->bg, bar);
	printf("%s", line);
	themeSetActiveColor();
}

/*
 * The bottom bar: " p> /MUSIC/Album" (the playlist's folder) and, on the
 * right, two stylus buttons: " find " (search) and " queue 3 " (the queue
 * editor), in the highlight colour.
 */
static void drawFooter(struct browser* b)
{
	const struct theme_t*	t = getCurrentTheme();
	char					queue[24];
	int						qlen, col;

	if(b->queueCount > 0)
		snprintf(queue, sizeof(queue), " queue %d ", b->queueCount);
	else
		snprintf(queue, sizeof(queue), " queue ");
	qlen = (int)strlen(queue);

	b->queueCol1 = CON_COLS;
	b->queueCol0 = CON_COLS - qlen;
	b->findCol1 = b->queueCol0 - 1;
	b->findCol0 = b->findCol1 - 6;
	drawBar(FOOTER_ROW, "p>", b->playlistDir, &t->textInactive,
	        CON_COLS - b->findCol0);

	col = b->findCol0;
	moveTo(FOOTER_ROW, col);
	setColors(&t->bg, &t->highlight);
	printf(" find ");
	setColors(&t->bg, &t->textInactive);
	printf(" ");
	setColors(&t->bg, &t->highlight);
	printf("%s", queue);
	themeSetActiveColor();
}

void browserSetQueueCount(struct browser* b, int count)
{
	if(count == b->queueCount)
		return;
	b->queueCount = count;
	if(!b->hidden)
	{
		consoleSelect(b->console);
		drawFooter(b);
	}
}

enum browser_button browserFooterButtonAt(const struct browser* b, int x,
                                          int y)
{
	int col = x / 8;

	if(y / 8 != FOOTER_ROW)
		return BROWSER_BTN_NONE;
	if(col >= b->findCol0 && col < b->findCol1)
		return BROWSER_BTN_FIND;
	if(col >= b->queueCol0 && col < b->queueCol1)
		return BROWSER_BTN_QUEUE;
	return BROWSER_BTN_NONE;
}

/* Total rows in the list ("../" included). */
static int totalRows(const struct browser* b)
{
	return b->count + 1;
}

static bool needsScrollbar(const struct browser* b)
{
	return totalRows(b) > MAX_LIST;
}

/* Index of the first row on screen (see listDir() for the odd `from`). */
static int topRow(const struct browser* b)
{
	return b->from == 0 ? 0 : b->from + 1;
}

static int maxTopRow(const struct browser* b)
{
	int m = totalRows(b) - MAX_LIST;

	return m > 0 ? m : 0;
}

/* Scrollbar thumb: first cell and length, in list rows. */
static void thumb(const struct browser* b, int* pos, int* len)
{
	int total = totalRows(b);
	int maxTop = maxTopRow(b);

	*len = MAX_LIST * MAX_LIST / total;
	if(*len < 1)
		*len = 1;
	*pos = maxTop > 0 ?
	       (topRow(b) * (MAX_LIST - *len) + maxTop / 2) / maxTop : 0;
}

static void drawScrollbar(const struct browser* b)
{
	const struct theme_t*	t = getCurrentTheme();
	int						pos, len;

	thumb(b, &pos, &len);
	for(int i = 0; i < MAX_LIST; i++)
	{
		moveTo(1 + i, CON_COLS - 1);
		if(i >= pos && i < pos + len)
		{
			setColors(&t->highlight, &t->highlight);
			putchar(' ');
		}
		else
		{
			setColors(&t->textInactive, &t->bg);
			putchar('|');
		}
	}
	themeSetActiveColor();
}

/* A file whose display name is its filename (no title tag) is shown
 * per the "artists in filenames" setting (settingsShortName()). */
static const char* shownName(const char* name, const char* file, char* buf,
                             size_t len)
{
	return strcmp(name, file) == 0 ? settingsShortName(name, buf, len) :
	       name;
}

/**
 * List current directory on bottom screen.
 * Shows display names (metadata titles) instead of raw filenames.
 * `nameMax` is the most characters a name may take.
 */
static int listDir(int from, int max, int select,
                   const struct dirList_t* dirList, int nameMax)
{
	int fileNum = 0;
	int listed = 0;

	moveTo(1, 0);

	if(from == 0)
	{
		printf("\33[2K%c../\n", select == 0 ? '>' : ' ');
		listed++;
		max--;
	}

	while(dirList->fileNum + dirList->dirNum > fileNum)
	{
		fileNum++;

		if(fileNum <= from)
			continue;

		listed++;

		if(dirList->dirNum >= fileNum)
		{
			printf("\33[2K%c", select == fileNum ? '>' : ' ');
			themeSetHighlightColor();
			printf("%.*s/", nameMax, dirList->directories[fileNum - 1]);
			themeSetActiveColor();
			printf("\n");
		}

		if(dirList->dirNum < fileNum)
		{
			int fileIdx = fileNum - dirList->dirNum - 1;
			char buf[256];
			const char* name = (dirList->displayNames &&
			                    fileIdx < dirList->fileNum &&
			                    dirList->displayNames[fileIdx])
			                   ? dirList->displayNames[fileIdx]
			                   : dirList->files[fileIdx];

			name = shownName(name, dirList->files[fileIdx], buf,
			                 sizeof(buf));
			printf("\33[2K%c%.*s\n",
			       select == fileNum ? '>' : ' ', nameMax, name);
		}

		if(fileNum == max + from)
			break;
	}

	return listed;
}

void browserDraw(struct browser* b)
{
	bool	bar;
	int		listed;

	if(b->hidden)
		return;

	consoleSelect(b->console);
	bar = needsScrollbar(b);

	drawBar(0, "c>", b->list.currentDir, &getCurrentTheme()->highlight, 0);

	/* With a scrollbar, names stop two columns short of it. */
	listed = listDir(b->from, MAX_LIST, b->cursor, &b->list, bar ? 36 : 37);
	if(listed < 0)
		err_print("Unable to list directory.");

	/* Blank the rows below a short list (the folder may have shrunk). */
	for(int i = listed < 0 ? 0 : listed; i < MAX_LIST; i++)
	{
		moveTo(1 + i, 0);
		printf("\33[2K");
	}

	if(bar)
		drawScrollbar(b);
	drawFooter(b);
}

void browserSetVisible(struct browser* b, bool visible)
{
	b->hidden = !visible;
	b->dragging = false;
	if(visible)
	{
		consoleSelect(b->console);
		consoleClear();
		browserDraw(b);
	}
}

void browserSetPlaylistDir(struct browser* b, const char* dir)
{
	if(dir == b->playlistDir ||
	   (dir != NULL && b->playlistDir != NULL &&
	    strcmp(dir, b->playlistDir) == 0))
		return;

	free(b->playlistDir);
	b->playlistDir = dir != NULL ? strdup(dir) : NULL;

	if(!b->hidden)
	{
		consoleSelect(b->console);
		drawFooter(b);
	}
}

int browserShowFile(struct browser* b, const char* dir, const char* name,
                    int levels)
{
	int oldCursor = b->cursor, oldFrom = b->from;
	int idx;

	if(levels > 0)
	{
		if(chdir(dir) != 0)
			return -1;
		b->count = dirListLoad(&b->list, false);
		b->cursor = 0;
		b->from = 0;

		/* Going back up: the folder we came from gets its old cursor
		 * back, the ones in between start at the top. */
		for(int k = 0; k < levels; k++)
		{
			for(int i = MAX_DIRECTORIES - 1; i > 0; i--)
			{
				b->prevCursor[i] = b->prevCursor[i - 1];
				b->prevFrom[i] = b->prevFrom[i - 1];
			}
			b->prevCursor[0] = k == 0 ? oldCursor : 0;
			b->prevFrom[0] = k == 0 ? oldFrom : 0;
		}
	}

	if((idx = browserFindFile(b, name)) >= 0)
		browserSelectFile(b, idx);
	return idx;
}

bool browserTouch(struct browser* b, u32 kDown, u32 kHeld)
{
	touchPosition	tp;
	int				pos, len, track, top, maxTop, row, from;
	float			frac;

	if(!(kHeld & KEY_TOUCH) || b->hidden || !needsScrollbar(b))
	{
		b->dragging = false;
		return false;
	}

	hidTouchRead(&tp);
	if(kDown & KEY_TOUCH)
		b->dragging = tp.px >= SCROLL_TOUCH_X;
	if(!b->dragging)
		return false;

	/* Where the thumb's middle should be, as a fraction of the track
	 * (rows 1..MAX_LIST, 8 px each). */
	thumb(b, &pos, &len);
	track = (MAX_LIST - len) * 8;
	frac = track > 0 ?
	       ((float)tp.py - 8.0f - len * 4.0f) / (float)track : 0.0f;
	if(frac < 0.0f)
		frac = 0.0f;
	if(frac > 1.0f)
		frac = 1.0f;

	maxTop = maxTopRow(b);
	top = (int)(frac * maxTop + 0.5f);

	/* Same screen row for the cursor, in the new window. */
	row = b->cursor - topRow(b);
	if(row < 0)
		row = 0;
	if(row > MAX_LIST - 1)
		row = MAX_LIST - 1;

	/* `from` 0 shows rows 0.., `from` k shows k+1.. (row 1 can't be at
	 * the top; it scrolls straight from 0 to 2, as with the buttons). */
	from = top <= 1 ? 0 : top - 1;
	if(from == b->from)
		return false;

	b->from = from;
	b->cursor = topRow(b) + row;
	if(b->cursor > b->count)
		b->cursor = b->count;

	browserDraw(b);
	return true;
}

enum dirlist_poll browserPoll(struct browser* b, bool visible)
{
	int sel = browserSelectedFile(b);
	int track = sel;
	enum dirlist_poll r = dirListPollMetadata(&b->list, &track);

	switch(r)
	{
		case DIRLIST_POLL_IDLE:
			return r;

		case DIRLIST_POLL_UPDATED:
			break;

		case DIRLIST_POLL_RESORTED:
			/* Keep the cursor on the same file, and on the same screen
			 * row where possible. */
			if(sel >= 0 && track >= 0)
			{
				int newCursor = b->list.dirNum + 1 + track;
				int newFrom = b->from + (newCursor - b->cursor);
				int maxFrom = b->count - MAX_LIST;

				if(maxFrom < 0)
					maxFrom = 0;

				/* listDir shows rows 0..MAX_LIST-1 when from == 0, else
				 * from+1..from+MAX_LIST: keep the cursor inside that. */
				if(newFrom <= 0 && newCursor > MAX_LIST - 1)
					newFrom = newCursor - MAX_LIST + 1;
				if(newFrom > 0 && newCursor < newFrom + 1)
					newFrom = newCursor - 1;
				if(newFrom > 0 && newCursor > newFrom + MAX_LIST)
					newFrom = newCursor - MAX_LIST;
				if(newFrom > maxFrom)
					newFrom = maxFrom;
				if(newFrom < 0)
					newFrom = 0;

				b->cursor = newCursor;
				b->from = newFrom;
			}
			break;
	}

	if(visible)
		browserDraw(b);

	return r;
}

void browserRefresh(struct browser* b, bool forceMeta)
{
	int maxFrom;

	b->count = dirListLoad(&b->list, forceMeta);

	/* The folder may have shrunk. */
	if(b->cursor > b->count)
		b->cursor = b->count;
	maxFrom = b->count - MAX_LIST;
	if(maxFrom < 0)
		maxFrom = 0;
	if(b->from > maxFrom)
		b->from = maxFrom;
}

bool browserMoveUp(struct browser* b)
{
	if(b->cursor <= 0)
		return false;

	b->cursor--;
	if(b->count - b->cursor > MAX_LIST - 2 && b->from != 0)
		b->from--;
	browserDraw(b);
	return true;
}

bool browserMoveDown(struct browser* b)
{
	if(b->cursor >= b->count)
		return false;

	b->cursor++;
	if(b->cursor >= MAX_LIST && b->count - b->cursor >= 0 &&
	   b->from < b->count - MAX_LIST)
		b->from++;
	browserDraw(b);
	return true;
}

bool browserPageUp(struct browser* b)
{
	int skip = MAX_LIST / 2;

	if(b->cursor <= 0)
		return false;

	if(b->cursor < skip) skip = b->cursor;
	b->cursor -= skip;
	if(b->count - b->cursor > MAX_LIST - 2 && b->from != 0)
	{
		b->from -= skip;
		if(b->from < 0) b->from = 0;
	}
	browserDraw(b);
	return true;
}

bool browserPageDown(struct browser* b)
{
	int skip = b->count - b->cursor;

	if(b->cursor >= b->count)
		return false;

	if(skip > MAX_LIST / 2) skip = MAX_LIST / 2;
	b->cursor += skip;
	if(b->cursor >= MAX_LIST && b->count - b->cursor >= 0 &&
	   b->from < b->count - MAX_LIST)
	{
		b->from += skip;
		if(b->from > b->count - MAX_LIST)
			b->from = b->count - MAX_LIST;
	}
	browserDraw(b);
	return true;
}

bool browserOnParentEntry(const struct browser* b)
{
	return b->from == 0 && b->cursor == 0;
}

void browserGoUp(struct browser* b)
{
	chdir("..");
	consoleSelect(b->console);
	consoleClear();
	b->count = dirListLoad(&b->list, false);

	b->cursor = b->prevCursor[0];
	b->from = b->prevFrom[0];
	for(int i = 0; i < MAX_DIRECTORIES - 1; i++)
	{
		b->prevCursor[i] = b->prevCursor[i+1];
		b->prevFrom[i] = b->prevFrom[i+1];
	}
	b->prevCursor[MAX_DIRECTORIES-1] = 0;
	b->prevFrom[MAX_DIRECTORIES-1] = 0;

	browserDraw(b);
}

bool browserEnterSelectedDir(struct browser* b)
{
	int oldCursor, oldFrom;

	if(b->cursor < 1 || b->list.dirNum < b->cursor)
		return false;

	chdir(b->list.directories[b->cursor - 1]);
	consoleSelect(b->console);
	consoleClear();
	b->count = dirListLoad(&b->list, false);

	oldCursor = b->cursor;
	oldFrom = b->from;
	b->cursor = 0;
	b->from = 0;

	browserDraw(b);

	for(int i = MAX_DIRECTORIES-1; i > 0; i--)
	{
		b->prevCursor[i] = b->prevCursor[i-1];
		b->prevFrom[i] = b->prevFrom[i-1];
	}
	b->prevCursor[0] = oldCursor;
	b->prevFrom[0] = oldFrom;
	return true;
}

int browserSelectedFile(const struct browser* b)
{
	if(b->cursor > 0 && b->list.dirNum < b->cursor)
		return b->cursor - b->list.dirNum - 1;
	return -1;
}

int browserFileCount(const struct browser* b)
{
	return b->list.fileNum;
}

const char* browserFileName(const struct browser* b, int idx)
{
	return b->list.files[idx];
}

const char* browserFileTitle(const struct browser* b, int idx)
{
	return (b->list.displayNames && b->list.displayNames[idx])
	       ? b->list.displayNames[idx]
	       : b->list.files[idx];
}

void browserFilePath(const struct browser* b, int idx, char* out,
                     size_t outLen)
{
	snprintf(out, outLen, "%s/%s", b->list.currentDir, b->list.files[idx]);
}

bool browserStepToNextFile(struct browser* b)
{
	if(!(b->cursor < b->count && b->list.dirNum < b->cursor))
		return false;

	b->cursor += 1;
	if(b->cursor >= MAX_LIST && b->from < b->count - MAX_LIST)
		b->from++;
	return true;
}

bool browserWrapToFirstFile(struct browser* b)
{
	if(b->list.fileNum <= 0)
		return false;

	b->cursor = b->list.dirNum + 1;
	b->from = 0;
	return true;
}

int browserFindFile(const struct browser* b, const char* path)
{
	const char* name = path;
	const char* slash = strrchr(path, '/');

	if(slash != NULL)
	{
		const char*	dir = b->list.currentDir;
		size_t		dirLen, pathDirLen = (size_t)(slash - path);

		if(dir == NULL)
			return -1;

		/* Compare the folders without trailing slashes ("sdmc:/" vs
		 * "sdmc://song.mp3" from browserFilePath()). */
		dirLen = strlen(dir);
		while(dirLen > 0 && dir[dirLen - 1] == '/')
			dirLen--;
		while(pathDirLen > 0 && path[pathDirLen - 1] == '/')
			pathDirLen--;

		if(dirLen != pathDirLen || strncmp(dir, path, dirLen) != 0)
			return -1;
		name = slash + 1;
	}

	for(int i = 0; i < b->list.fileNum; i++)
		if(strcmp(b->list.files[i], name) == 0)
			return i;

	return -1;
}

void browserSelectFile(struct browser* b, int idx)
{
	int first, last;

	if(idx < 0 || idx >= b->list.fileNum)
		return;

	b->cursor = b->list.dirNum + 1 + idx;

	/* Rows on screen: 0..MAX_LIST-1 when from == 0, else
	 * from+1..from+MAX_LIST (see listDir()). */
	first = b->from == 0 ? 0 : b->from + 1;
	last = b->from == 0 ? MAX_LIST - 1 : b->from + MAX_LIST;
	if(b->cursor < first || b->cursor > last)
		b->from = b->cursor < MAX_LIST ? 0 : b->cursor - MAX_LIST + 1;
}

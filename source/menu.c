/**
 * ctrmus - 3DS Music Player
 * The menu. See menu.h.
 *
 * Bottom screen (40x30 console):
 *   row 0      title bar ("Menu" / "Menu > Effects", version on the right)
 *   rows 2..   sections and items; the selected item is a highlight bar
 *   row 27     what the selected item does
 *   row 29     key hints
 * Items are rows of label + value; sliders have a bar the stylus can
 * drag. Everything is drawn over the old contents (no clearing between
 * redraws) so the screen doesn't flicker; it's only cleared when the
 * menu opens, the page changes or the theme changes.
 */

#include <stdio.h>
#include <string.h>

#include "brightness.h"
#include "browser.h"
#include "effects.h"
#include "menu.h"
#include "playback.h"
#include "player.h"
#include "queue.h"
#include "settings.h"
#include "themes.h"
#include "ui.h"
#include "version.h"

#define COLS		40
#define FIRST_ROW	2
#define HELP_ROW	27
#define HINT_ROW	29
#define LABEL_COL	3
#define SLIDER_COL	15		/* slider cells: SLIDER_COL..+SLIDER_W-1 */
#define SLIDER_W	17
#define VALUE_END	39		/* values end just before this column */
#define MAX_ITEMS	24

/* Held Up/Down/Left/Right repeat (ms). */
#define REPEAT_DELAY_MS	400
#define REPEAT_EVERY_MS	80

enum page_id
{
	PAGE_MAIN,
	PAGE_EFFECTS,
	PAGE_THEME,		/* Edit theme */
	PAGE_COUNT
};

enum item_id
{
	IT_NONE,
	IT_SHUFFLE,
	IT_REPEAT,
	IT_EFFECTS,
	IT_THEME,
	IT_BRIGHTNESS,
	IT_ARTISTS,
	IT_TIME,
	IT_ART,
	IT_DISC,
	IT_DISC_SPEED,
	IT_TITLES,
	IT_SEEK,
	IT_PERFORMANCE,
	IT_FAST,
	IT_QUEUE,
	IT_SEARCH,
	IT_REFRESH,
	IT_EXIT,
	IT_SPEED,
	IT_DISTORTION,
	IT_REVERB,
	IT_REVERB_TIME,
	IT_THEME_EDIT,
	/* Edit theme: colour slot * 3 + channel (R, G, B) */
	IT_TC_BASE = 100
};

#define THEME_CHANNELS	3
#define TC_SLOTS		THEME_NUM_COLORS
#define TC_STEPS		64		/* 0..255 in steps of 4 (the top is 255) */

static bool isThemeChannel(enum item_id id)
{
	return id >= IT_TC_BASE && id < IT_TC_BASE + TC_SLOTS * THEME_CHANNELS;
}

enum item_kind
{
	K_HEADER,	/* section title, not selectable */
	K_ITEM,		/* label + value */
	K_SLIDER,	/* label + slider + value */
	K_SOON		/* not implemented yet: shown dim */
};

/* Shown instead of an item's own help while performance mode forces it. */
static const char forcedHelp[] = "Set by performance mode (SYSTEM).";

struct item
{
	enum item_kind	kind;
	enum item_id	id;
	const char*		label;
	const char*		help;	/* at most 38 characters */
};

struct page
{
	const char*			title;
	const struct item*	items;
	int					count;
};

static const struct item mainItems[] =
{
	{ K_HEADER, IT_NONE, "PLAYBACK", NULL },
	{ K_ITEM, IT_SHUFFLE, "Shuffle", "Random order. START works anywhere." },
	{ K_ITEM, IT_REPEAT, "Repeat", "Off, the whole playlist, or one song." },
	{ K_ITEM, IT_QUEUE, "Queue...", "Play, move or remove queued songs." },
	{ K_ITEM, IT_SEARCH, "Search...", "Filenames here and in subfolders." },
	{ K_ITEM, IT_EFFECTS, "Effects...", "Speed, distortion, reverb." },
	{ K_HEADER, IT_NONE, "DISPLAY", NULL },
	{ K_ITEM, IT_THEME, "Theme", "A: pick from a list. </>: cycle." },
	{ K_ITEM, IT_THEME_EDIT, "Edit theme...",
	  "Make your own colours (\"Custom\" theme)." },
	{ K_SLIDER, IT_BRIGHTNESS, "Brightness", "Both screens. Also R+Up/Down." },
	{ K_ITEM, IT_ARTISTS, "Artists in names",
	  "How \"A, B - Song.mp3\" names show." },
	{ K_ITEM, IT_TIME, "Time", "Time played, or time left (-0:12:34)." },
	{ K_ITEM, IT_ART, "Cover art", "Placeholder: never load covers." },
	{ K_ITEM, IT_DISC, "Spinning disc",
	  "Round cover that spins while playing." },
	{ K_SLIDER, IT_DISC_SPEED, "Disc speed",
	  "How fast it turns (record speeds)." },
	{ K_ITEM, IT_TITLES, "Titles in lists",
	  "Queue/playlist: tag titles or files." },
	{ K_HEADER, IT_NONE, "CONTROLS", NULL },
	{ K_SLIDER, IT_SEEK, "Seek step", "How far ZL/ZR jump." },
	{ K_HEADER, IT_NONE, "SYSTEM", NULL },
	{ K_ITEM, IT_PERFORMANCE, "Performance mode",
	  "Turns off tags, covers, reverb, disc." },
	{ K_ITEM, IT_FAST, "New 3DS fast mode",
	  "804 MHz CPU. Uses more battery." },
	{ K_ITEM, IT_REFRESH, "Force refresh",
	  "Re-reads this folder's song titles." },
	{ K_ITEM, IT_EXIT, "Exit Papaya", "Quit (HOME > Close also works)." },
};

static const struct item effectsItems[] =
{
	{ K_SLIDER, IT_SPEED, "Speed", "Changes pitch too. A: back to 1.0x." },
	{ K_SLIDER, IT_DISTORTION, "Distortion",
	  "Intensity (soft clip). A: off." },
	{ K_SLIDER, IT_REVERB, "Reverb", "How much reverb is mixed in. A: off." },
	{ K_SLIDER, IT_REVERB_TIME, "Reverb time",
	  "How long the echo rings out." },
};

#define TC_HELP "</> change, hold to run. Shown live."
#define TC(slot, ch, name) \
	{ K_SLIDER, IT_TC_BASE + (slot) * 3 + (ch), name, TC_HELP }
#define TC_GROUP(slot, head) \
	{ K_HEADER, IT_NONE, head, NULL }, \
	TC(slot, 0, "Red"), TC(slot, 1, "Green"), TC(slot, 2, "Blue")

static const struct item themeItems[] =
{
	TC_GROUP(THEME_COLOR_BG, "BACKGROUND"),
	TC_GROUP(THEME_COLOR_TEXT_ACTIVE, "TEXT"),
	TC_GROUP(THEME_COLOR_TEXT_INACTIVE, "DIM TEXT"),
	TC_GROUP(THEME_COLOR_PROGRESS, "PROGRESS BAR"),
	TC_GROUP(THEME_COLOR_HIGHLIGHT, "HIGHLIGHT"),
};

static const struct page pages[PAGE_COUNT] =
{
	{ "Menu", mainItems, sizeof(mainItems) / sizeof(mainItems[0]) },
	{ "Menu > Effects", effectsItems,
	  sizeof(effectsItems) / sizeof(effectsItems[0]) },
	{ "Menu > Edit theme", themeItems,
	  sizeof(themeItems) / sizeof(themeItems[0]) },
};

/* The item on the main page that opens a page (B goes back to it). */
static enum item_id pageParent(int page)
{
	return page == PAGE_THEME ? IT_THEME_EDIT : IT_EFFECTS;
}

static const struct page* curPage(const struct menu* m)
{
	return &pages[m->page];
}

/* Screen row of each item: one blank row before every section but the
 * first. */
static void layout(const struct page* p, int* rows)
{
	int r = FIRST_ROW;

	for(int k = 0; k < p->count && k < MAX_ITEMS; k++)
	{
		if(k > 0 && p->items[k].kind == K_HEADER)
			r++;
		rows[k] = r++;
	}
}

/* ------------------------------------------------------------------ */
/* Values and sliders                                                  */
/* ------------------------------------------------------------------ */

/* Items performance mode overrides right now (drawn dim, can't change). */
static bool forced(enum item_id id)
{
	if(!settingsPerformanceMode())
		return false;
	return id == IT_ART || id == IT_TITLES ||
	       id == IT_REVERB || id == IT_REVERB_TIME || id == IT_DISC ||
	       id == IT_DISC_SPEED;
}

static struct themeColor themeChannelColor(enum item_id id, int* chan)
{
	*chan = (id - IT_TC_BASE) % THEME_CHANNELS;
	return themeEditGet((id - IT_TC_BASE) / THEME_CHANNELS);
}

static int sliderSteps(enum item_id id)
{
	if(isThemeChannel(id))
		return TC_STEPS;
	switch(id)
	{
		case IT_SPEED:		return 30;		/* 0.1x .. 3.0x */
		case IT_BRIGHTNESS:	return 5;
		case IT_SEEK:		return SEEK_STEP_COUNT;
		case IT_DISC_SPEED:	return DISC_SPEED_COUNT;
		case IT_DISTORTION:	return 21;		/* 0% .. 100% by 5 */
		case IT_REVERB:		return 11;		/* 0% .. 100% by 10 */
		case IT_REVERB_TIME:
			return (EFFECTS_REVERB_TIME_MAX - EFFECTS_REVERB_TIME_MIN) / 5 + 1;
		default:			return 1;
	}
}

static bool zeroIsOff(enum item_id id)
{
	return id == IT_DISTORTION || id == IT_REVERB;
}

static int sliderIndex(enum item_id id)
{
	if(isThemeChannel(id))
	{
		int					chan;
		struct themeColor	c = themeChannelColor(id, &chan);
		int					v = chan == 0 ? c.r : chan == 1 ? c.g : c.b;

		return v >= 255 ? TC_STEPS - 1 : (v + 2) / 4;
	}
	switch(id)
	{
		case IT_SPEED:
			return (int)(getPlaybackSpeed() * 10.0f + 0.5f) - 1;
		case IT_BRIGHTNESS:
			return brightnessLevel() < 0 ? 2 : brightnessLevel() - 1;
		case IT_SEEK:
			return settingsSeekStepIndex();
		case IT_DISC_SPEED:
			return settingsDiscSpeedIndex();
		case IT_DISTORTION:
			return effectsDistortion() / 5;
		case IT_REVERB:
			return effectsReverbActiveMix() / 10;
		case IT_REVERB_TIME:
			return (effectsReverbTime() - EFFECTS_REVERB_TIME_MIN) / 5;
		default:
			return 0;
	}
}

static void sliderSet(enum item_id id, int idx)
{
	int steps = sliderSteps(id);

	if(idx < 0)
		idx = 0;
	if(idx > steps - 1)
		idx = steps - 1;

	if(isThemeChannel(id))
	{
		int					chan;
		struct themeColor	c = themeChannelColor(id, &chan);
		uint8_t				v = idx >= TC_STEPS - 1 ? 255 : (uint8_t)(idx * 4);

		if(chan == 0)
			c.r = v;
		else if(chan == 1)
			c.g = v;
		else
			c.b = v;
		themeEditSet((id - IT_TC_BASE) / THEME_CHANNELS, c);
		return;
	}

	switch(id)
	{
		case IT_SPEED:		setPlaybackSpeed((idx + 1) / 10.0f); break;
		case IT_BRIGHTNESS:	brightnessSetLevel(idx + 1); break;
		case IT_SEEK:		settingsSetSeekStepIndex(idx); break;
		case IT_DISC_SPEED:	settingsSetDiscSpeedIndex(idx); break;
		case IT_DISTORTION:	effectsSetDistortion(idx * 5); break;
		case IT_REVERB:		effectsSetReverbMix(idx * 10); break;
		case IT_REVERB_TIME:
			effectsSetReverbTime(EFFECTS_REVERB_TIME_MIN + idx * 5);
			break;
		default:			break;
	}
}

static void valueText(enum item_id id, char* buf, size_t len)
{
	if(isThemeChannel(id))
	{
		int					chan;
		struct themeColor	c = themeChannelColor(id, &chan);

		snprintf(buf, len, "%d", chan == 0 ? c.r : chan == 1 ? c.g : c.b);
		return;
	}
	static const char* const repeatStr[] = { "off", "all", "one" };

	buf[0] = '\0';
	switch(id)
	{
		case IT_SHUFFLE:
			snprintf(buf, len, "%s", playerShuffle() ? "on" : "off");
			break;
		case IT_REPEAT:
			snprintf(buf, len, "%s", repeatStr[playerRepeat()]);
			break;
		case IT_EFFECTS:
			snprintf(buf, len, ">");
			break;
		case IT_THEME:
			snprintf(buf, len, "%.20s %d/%d", getCurrentTheme()->name,
			         getCurrentThemeIndex() + 1, themeCount());
			break;
		case IT_BRIGHTNESS:
			if(brightnessPercent() < 0)
				snprintf(buf, len, "--");
			else
				snprintf(buf, len, "%d%%", brightnessPercent());
			break;
		case IT_ARTISTS:
			snprintf(buf, len, "%s",
			         filenameArtistsStr(settingsFilenameArtists()));
			break;
		case IT_TIME:
			snprintf(buf, len, "%s",
			         settingsTimeRemaining() ? "remaining" : "played");
			break;
		case IT_ART:
			snprintf(buf, len, "%s",
			         settingsPlaceholderArt() ? "placeholder" : "shown");
			break;
		case IT_TITLES:
			snprintf(buf, len, "%s", settingsListTitles() ? "tags" :
			         "filenames");
			break;
		case IT_DISC:
			snprintf(buf, len, "%s", settingsSpinningDisc() ? "on" : "off");
			break;
		case IT_DISC_SPEED:
			snprintf(buf, len, "%s", discSpeedStr(settingsDiscSpeedIndex()));
			break;
		case IT_SEEK:
			snprintf(buf, len, "%ds", settingsSeekStep());
			break;
		case IT_PERFORMANCE:
			snprintf(buf, len, "%s", settingsPerformanceMode() ? "on" :
			         "off");
			break;
		case IT_FAST:
			snprintf(buf, len, "%s", !settingsFastModeAvailable() ? "n/a" :
			         settingsFastMode() ? "on" : "off");
			break;
		case IT_QUEUE:
			if(queueSize(playerQueue()) > 0)
				snprintf(buf, len, "%d song%s >", queueSize(playerQueue()),
				         queueSize(playerQueue()) == 1 ? "" : "s");
			else
				snprintf(buf, len, "empty >");
			break;
		case IT_SEARCH:
			snprintf(buf, len, ">");
			break;
		case IT_SPEED:
			snprintf(buf, len, "%.1fx", (double)getPlaybackSpeed());
			break;
		case IT_DISTORTION:
			if(effectsDistortion() == 0)
				snprintf(buf, len, "off");
			else
				snprintf(buf, len, "%d%%", effectsDistortion());
			break;
		case IT_REVERB:
			if(effectsReverbActiveMix() == 0)
				snprintf(buf, len, "off");
			else
				snprintf(buf, len, "%d%%", effectsReverbActiveMix());
			break;
		case IT_REVERB_TIME:
			snprintf(buf, len, "%d.%ds", effectsReverbTime() / 10,
			         effectsReverbTime() % 10);
			break;
		default:
			break;
	}
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static PrintConsole* console = NULL;
static struct screens* menuScreens = NULL;	/* for the live theme editor */

static void moveTo(int row, int col)
{
	printf("\033[%d;%dH", row + 1, col + 1);	/* 1-based */
}

static void setColors(const struct themeColor* fg, const struct themeColor* bg)
{
	printf("\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um", fg->r, fg->g, fg->b,
	       bg->r, bg->g, bg->b);
}

/* Print exactly COLS characters (padded) at the start of `row`. The
 * console only wraps when the next character prints, and every draw
 * moves first, so a full row is safe. */
static void fullRow(int row, const char* s)
{
	moveTo(row, 0);
	printf("%-*.*s", COLS, COLS, s);
}

static void drawItem(const struct item* it, int row, bool sel)
{
	const struct theme_t*		t = getCurrentTheme();
	const struct themeColor*	fg;
	const struct themeColor*	bg;
	char						line[COLS + 1];
	char						value[64];

	if(it->kind == K_HEADER)
	{
		setColors(&t->highlight, &t->bg);
		snprintf(line, sizeof(line), " %s", it->label);
		fullRow(row, line);
		return;
	}

	fg = sel ? &t->bg : ((it->kind == K_SOON || forced(it->id)) ?
	                     &t->textInactive : &t->textActive);
	bg = sel ? &t->highlight : &t->bg;

	valueText(it->id, value, sizeof(value));
	memset(line, ' ', COLS);
	line[COLS] = '\0';
	if(sel)
		line[1] = '>';
	memcpy(line + LABEL_COL, it->label, strlen(it->label));
	{
		int vlen = (int)strlen(value);
		int at = VALUE_END - vlen;

		if(at < LABEL_COL + (int)strlen(it->label) + 1)
			at = LABEL_COL + (int)strlen(it->label) + 1;
		memcpy(line + at, value, (size_t)(COLS - at < vlen ? COLS - at :
		                                  vlen));
	}

	setColors(fg, bg);
	if(it->kind != K_SLIDER)
	{
		fullRow(row, line);
		return;
	}

	/* Slider row: label part, the cells, then the value part. */
	{
		int steps = sliderSteps(it->id);
		/* Sliders that start at "off" are empty there. */
		int filled = zeroIsOff(it->id) ?
		             sliderIndex(it->id) * SLIDER_W / (steps - 1) :
		             (sliderIndex(it->id) + 1) * SLIDER_W / steps;

		moveTo(row, 0);
		printf("%.*s", SLIDER_COL, line);
		for(int i = 0; i < SLIDER_W; i++)
		{
			if(i < filled)
			{
				setColors(&t->progressBar, &t->progressBar);
				putchar(' ');
			}
			else
			{
				setColors(&t->textInactive, &t->bg);
				putchar('-');
			}
		}
		setColors(fg, bg);
		printf("%s", line + SLIDER_COL + SLIDER_W);
	}
}

/* Title bar with the version on the right */
static void drawTitle(const char* title)
{
	static const char		version[] = "v" CTRMUS_VERSION " ";
	const struct theme_t*	t = getCurrentTheme();
	char					line[COLS + 1];
	int						n = snprintf(line, sizeof(line), " %s", title);

	while(n < COLS)
		line[n++] = ' ';
	line[COLS] = '\0';
	memcpy(line + COLS - (sizeof(version) - 1), version,
	       sizeof(version) - 1);
	setColors(&t->bg, &t->highlight);
	fullRow(0, line);
}

/*
 * Theme picker (Menu > Themes): every built-in theme, one per row, with
 * a sample of its colours on the right (its text on its background, its
 * progress bar and highlight colours). The selected row is drawn in the
 * theme in use, like the browser's cursor.
 */
#define SWATCH_COL	31

static void drawThemes(const struct menu* m)
{
	const struct theme_t*	t = getCurrentTheme();
	char					line[COLS + 1];

	consoleSelect(console);
	drawTitle("Menu > Themes");

	for(int i = 0; i < themeCount(); i++)
	{
		struct theme_t				th;
		bool						sel = i == m->selected;
		const struct themeColor*	fg = sel ? &t->bg : &t->textActive;
		const struct themeColor*	bg = sel ? &t->highlight : &t->bg;
		int							n;

		if(getBuiltinTheme(i, &th) != 0)
			continue;
		memset(line, ' ', COLS);
		line[COLS] = '\0';
		if(sel)
			line[1] = '>';
		n = snprintf(line + 3, (size_t)(SWATCH_COL - 3), "%.20s%s", th.name,
		             i == getCurrentThemeIndex() ? "  (now)" : "");
		line[3 + (n < SWATCH_COL - 4 ? n : SWATCH_COL - 4)] = ' ';

		setColors(fg, bg);
		moveTo(FIRST_ROW + i, 0);
		printf("%.*s", SWATCH_COL, line);
		setColors(&th.textActive, &th.bg);
		printf(" Aa ");
		setColors(&th.progressBar, &th.progressBar);
		printf("  ");
		setColors(&th.highlight, &th.highlight);
		printf("  ");
		setColors(fg, bg);
		printf("%*s", COLS - SWATCH_COL - 8, "");
	}

	setColors(&t->textInactive, &t->bg);
	fullRow(HELP_ROW, " Y: try it on. A: use it. B: cancel.");
	setColors(&t->bg, &t->textInactive);
	fullRow(HINT_ROW, " A use  Y preview  B cancel  stylus");
	themeSetActiveColor();
}

/*
 * The page is taller than the screen when it has many items (the main
 * page): scroll so the selected item is on screen, with its section
 * header if it's the first in the section. Returns true if it scrolled.
 */
static bool ensureVisible(struct menu* m, const int* rows)
{
	const struct page*	p = curPage(m);
	int					last = rows[(p->count < MAX_ITEMS ? p->count :
	                                 MAX_ITEMS) - 1];
	int					room = HELP_ROW - FIRST_ROW;
	int					top = rows[m->selected];
	int					old = m->scroll;

	if(m->selected > 0 && p->items[m->selected - 1].kind == K_HEADER)
		top = rows[m->selected - 1];

	if(top - m->scroll < FIRST_ROW)
		m->scroll = top - FIRST_ROW;
	if(rows[m->selected] - m->scroll >= HELP_ROW)
		m->scroll = rows[m->selected] - HELP_ROW + 1;
	if(m->scroll > last - (HELP_ROW - 1))
		m->scroll = last - (HELP_ROW - 1);
	if(m->scroll < 0)
		m->scroll = 0;
	return m->scroll != old;
}

static uint16_t pack565(struct themeColor c)
{
	return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

/*
 * Edit theme: the red/green/blue sliders drawn as pixels over the text
 * console: each position of the bar shows the colour you'd get by moving
 * the slider there (the other two channels as they are now), and the
 * current position has a white 1px box around it.
 */
static void drawChannelBars(const struct menu* m, const int* rows)
{
	const struct page*	p = curPage(m);
	uint16_t*			fb = console != NULL ? (uint16_t*)console->frameBuffer :
	                         NULL;
	const int			x0 = SLIDER_COL * 8, w = SLIDER_W * 8;

	if(fb == NULL)
		return;

	for(int k = 0; k < p->count && k < MAX_ITEMS; k++)
	{
		int					row = rows[k] - m->scroll;
		int					chan, y0, xm, value;
		struct themeColor	c;

		if(!isThemeChannel(p->items[k].id) || row < FIRST_ROW ||
		   row >= HELP_ROW)
			continue;
		c = themeChannelColor(p->items[k].id, &chan);
		value = chan == 0 ? c.r : chan == 1 ? c.g : c.b;
		y0 = row * 8;
		xm = (value * (w - 1) + 127) / 255;

		for(int dx = 0; dx < w; dx++)
		{
			struct themeColor	g = c;
			uint8_t				v = (uint8_t)(dx * 255 / (w - 1));
			uint16_t			px;

			if(chan == 0)
				g.r = v;
			else if(chan == 1)
				g.g = v;
			else
				g.b = v;
			px = pack565(g);
			for(int dy = 1; dy <= 6; dy++)
				fb[(x0 + dx) * 240 + (239 - (y0 + dy))] = px;
		}

		/* White box around the current position */
		for(int dx = xm - 2; dx <= xm + 2; dx++)
		{
			if(dx < 0 || dx >= w)
				continue;
			fb[(x0 + dx) * 240 + (239 - y0)] = 0xFFFF;
			fb[(x0 + dx) * 240 + (239 - (y0 + 7))] = 0xFFFF;
		}
		for(int dy = 0; dy <= 7; dy++)
		{
			if(xm - 2 >= 0)
				fb[(x0 + xm - 2) * 240 + (239 - (y0 + dy))] = 0xFFFF;
			if(xm + 2 < w)
				fb[(x0 + xm + 2) * 240 + (239 - (y0 + dy))] = 0xFFFF;
		}
	}
}

static void drawMenu(struct menu* m)
{
	const struct theme_t*	t = getCurrentTheme();
	const struct page*		p = curPage(m);
	int						rows[MAX_ITEMS];
	char					line[COLS + 1];
	int						n = p->count < MAX_ITEMS ? p->count : MAX_ITEMS;

	if(m->picking)
	{
		drawThemes(m);
		return;
	}

	consoleSelect(console);
	layout(p, rows);
	if(ensureVisible(m, rows))
		consoleClear();		/* the rows moved: no leftovers */
	drawTitle(p->title);

	for(int k = 0; k < n; k++)
	{
		int row = rows[k] - m->scroll;

		if(row >= FIRST_ROW && row < HELP_ROW)
			drawItem(&p->items[k], row, k == m->selected);
	}

	/* Edit theme: each colour's hex value and a swatch on its header */
	if(m->page == PAGE_THEME)
		for(int k = 0; k < n; k++)
		{
			int row = rows[k] - m->scroll;

			if(p->items[k].kind == K_HEADER && row >= FIRST_ROW &&
			   row < HELP_ROW)
			{
				struct themeColor	c = themeEditGet((k / 4));
				char				hex[16];

				snprintf(hex, sizeof(hex), "#%02X%02X%02X", c.r, c.g, c.b);
				setColors(&t->textActive, &t->bg);
				moveTo(row, 17);
				printf("%s", hex);
				setColors(&c, &c);
				moveTo(row, 26);
				printf("          ");
			}
		}

	/* More above / below: an arrow at the right edge */
	setColors(&t->highlight, &t->bg);
	if(m->scroll > 0)
	{
		moveTo(FIRST_ROW, COLS - 1);
		putchar(0x18);
	}
	if(rows[n - 1] - m->scroll >= HELP_ROW)
	{
		moveTo(HELP_ROW - 1, COLS - 1);
		putchar(0x19);
	}

	/* What the selected item does */
	setColors(&t->textInactive, &t->bg);
	snprintf(line, sizeof(line), " %s",
	         forced(p->items[m->selected].id) ? forcedHelp :
	         p->items[m->selected].help != NULL ?
	         p->items[m->selected].help : "");
	fullRow(HELP_ROW, line);

	/* Key hints */
	setColors(&t->bg, &t->textInactive);
	fullRow(HINT_ROW, m->page == PAGE_MAIN ?
	        " A select  B close  </> change  stylus" :
	        " A select  B back  </> change  stylus");

	themeSetActiveColor();

	if(m->page == PAGE_THEME)
		drawChannelBars(m, rows);
}

static void clearAndDraw(struct menu* m)
{
	consoleSelect(console);
	consoleClear();
	drawMenu(m);
}

void menuRedraw(struct menu* m, struct screens* s)
{
	console = &s->bottom;
	drawMenu(m);
}

void menuResume(struct menu* m, struct screens* s)
{
	console = &s->bottom;
	m->drag = -1;
	m->picking = false;
	clearAndDraw(m);
}

void menuOpen(struct menu* m, struct screens* s)
{
	console = &s->bottom;
	menuScreens = s;
	m->page = PAGE_MAIN;
	m->selected = 1;	/* first item after the PLAYBACK header */
	m->drag = -1;
	m->picking = false;
	m->scroll = 0;
	clearAndDraw(m);
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static void openPage(struct menu* m, enum page_id page, enum item_id select)
{
	const struct page* p = &pages[page];

	m->page = page;
	m->picking = false;
	m->selected = 0;
	for(int k = 0; k < p->count; k++)
		if(p->items[k].kind != K_HEADER &&
		   (select == IT_NONE || p->items[k].id == select))
		{
			m->selected = k;
			break;
		}
	m->drag = -1;
	m->scroll = 0;
	clearAndDraw(m);
}

/* Left/Right (delta -1/+1), or A (delta +1) on a value. */
static void change(struct menu* m, struct screens* s, enum item_id id,
                   int delta)
{
	if(forced(id))
		return;

	if(isThemeChannel(id))
	{
		/* Live: both screens take the new colour now */
		sliderSet(id, sliderIndex(id) + delta);
		uiApplyTheme(s, getCurrentTheme());
		clearAndDraw(m);
		return;
	}

	switch(id)
	{
		case IT_SHUFFLE:
			playerToggleShuffle();
			break;

		case IT_REPEAT:
			playerCycleRepeat();
			if(delta < 0)
			{
				playerCycleRepeat();
				playerCycleRepeat();
			}
			break;

		case IT_EFFECTS:
			if(delta > 0)
				openPage(m, PAGE_EFFECTS, IT_NONE);
			return;

		case IT_THEME_EDIT:
			if(delta > 0)
			{
				themeEditBegin();
				openPage(m, PAGE_THEME, IT_NONE);
			}
			return;

		case IT_THEME:
		{
			const struct theme_t* t;

			cycleTheme(delta);
			t = getCurrentTheme();

			uiApplyTheme(s, t);		/* clears the bottom screen, redraws
			                         * the top one */
			clearAndDraw(m);
			return;
		}

		case IT_BRIGHTNESS:
		case IT_SEEK:
		case IT_DISC_SPEED:
		case IT_SPEED:
		case IT_DISTORTION:
		case IT_REVERB:
		case IT_REVERB_TIME:
			sliderSet(id, sliderIndex(id) + delta);
			break;

		case IT_ARTISTS:
			settingsSetFilenameArtists((enum filename_artists)
				((settingsFilenameArtists() + ARTISTS_COUNT + delta) %
				 ARTISTS_COUNT));
			break;

		case IT_TIME:
			settingsSetTimeRemaining(!settingsTimeRemaining());
			break;

		case IT_ART:
			settingsSetPlaceholderArt(!settingsPlaceholderArt());
			uiArtSettingChanged();
			break;

		case IT_TITLES:
			settingsSetListTitles(!settingsListTitles());
			break;

		case IT_DISC:
			settingsSetSpinningDisc(!settingsSpinningDisc());
			break;

		case IT_PERFORMANCE:
			/* The cover art is dropped or loaded here. Folders skip/load tags from the
			 * next one opened. */
			settingsSetPerformanceMode(!settingsPerformanceMode());
			uiArtSettingChanged();
			break;

		case IT_FAST:
			if(!settingsFastModeAvailable())
				return;
			settingsSetFastMode(!settingsFastMode());
			break;

		default:
			return;
	}
	drawMenu(m);
	uiRequestRedraw();
}

static enum menu_result activate(struct menu* m, struct screens* s,
                                 struct browser* b, enum item_id id)
{
	switch(id)
	{
		case IT_SPEED:
			setPlaybackSpeed(1.0f);
			drawMenu(m);
			uiRequestRedraw();
			break;

		case IT_DISTORTION:
		case IT_REVERB:
			if(forced(id))
				break;
			sliderSet(id, 0);	/* A: off */
			drawMenu(m);
			uiRequestRedraw();
			break;

		case IT_BRIGHTNESS:
		case IT_SEEK:
		case IT_DISC_SPEED:
		case IT_REVERB_TIME:
			break;

		case IT_THEME:
			/* Open the theme picker on the theme in use */
			m->picking = true;
			m->themeOrig = getCurrentThemeIndex();
			m->selected = m->themeOrig;
			m->drag = -1;
			clearAndDraw(m);
			break;

		case IT_THEME_EDIT:
			themeEditBegin();
			openPage(m, PAGE_THEME, IT_NONE);
			break;

		case IT_QUEUE:
			return MENU_QUEUE;

		case IT_SEARCH:
			return MENU_SEARCH;

		case IT_REFRESH:
			/* Close, re-scan the folder (re-reading all tags, in case
			 * the cache missed a change) and show the browser again. */
			browserRefresh(b, true);
			browserSetVisible(b, true);
			return MENU_CLOSED;

		case IT_EXIT:
			return MENU_QUIT;

		default:
			change(m, s, id, 1);
			break;
	}
	return MENU_OPEN;
}

/* Stylus: tap a row to select it; press on a slider and drag to set it.
 * Taps never activate anything. */
static void touch(struct menu* m, u32 kDown)
{
	const struct page*	p = curPage(m);
	touchPosition		tp;
	int					rows[MAX_ITEMS];
	bool				redraw = false;

	hidTouchRead(&tp);
	layout(p, rows);

	if(kDown & KEY_TOUCH)
	{
		int row = tp.py / 8;
		int col = tp.px / 8;

		m->drag = -1;
		for(int k = 0; k < p->count && k < MAX_ITEMS; k++)
		{
			if(rows[k] - m->scroll != row || p->items[k].kind == K_HEADER)
				continue;
			if(m->selected != k)
				redraw = true;
			m->selected = k;
			if(p->items[k].kind == K_SLIDER && !forced(p->items[k].id) &&
			   col >= SLIDER_COL - 1 && col <= SLIDER_COL + SLIDER_W)
				m->drag = k;
		}
	}

	if(m->drag >= 0)
	{
		enum item_id	id = p->items[m->drag].id;
		int				steps = sliderSteps(id);
		float			frac = ((float)tp.px - SLIDER_COL * 8) /
		                       (float)(SLIDER_W * 8 - 1);
		int				idx;

		if(frac < 0.0f)
			frac = 0.0f;
		if(frac > 1.0f)
			frac = 1.0f;
		idx = (int)(frac * (steps - 1) + 0.5f);
		if(idx != sliderIndex(id))
		{
			sliderSet(id, idx);
			if(isThemeChannel(id) && menuScreens != NULL)
			{
				uiApplyTheme(menuScreens, getCurrentTheme());
				consoleSelect(console);
				consoleClear();
			}
			uiRequestRedraw();
			redraw = true;
		}
	}

	if(redraw)
		drawMenu(m);
}

/* Up/Down/Left/Right pressed now, or held long enough to repeat. */
static u32 repeatKeys(u32 kDown, u32 kHeld)
{
	static u64	since = 0, last = 0;
	const u32	dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
	u64			now = osGetTime();

	if(kDown & dirs)
	{
		since = last = now;
		return kDown & dirs;
	}
	if((kHeld & dirs) && now - since >= REPEAT_DELAY_MS &&
	   now - last >= REPEAT_EVERY_MS)
	{
		last = now;
		return kHeld & dirs;
	}
	return 0;
}

/* Switch to theme i now (both screens), or do nothing if it's in use. */
static void useTheme(struct menu* m, struct screens* s, int i)
{
	if(i == getCurrentThemeIndex())
		return;
	setThemeIndex(i);
	uiApplyTheme(s, getCurrentTheme());		/* clears the bottom screen */
	clearAndDraw(m);
}

static enum menu_result themesInput(struct menu* m, u32 kDown, u32 dirs,
                                    struct screens* s, struct browser* b)
{
	/* Cancel (B, or SELECT which also closes): back to the old theme */
	if(kDown & (KEY_B | KEY_SELECT))
	{
		useTheme(m, s, m->themeOrig);
		if(kDown & KEY_SELECT)
		{
			m->picking = false;
			browserSetVisible(b, true);
			return MENU_CLOSED;
		}
		openPage(m, PAGE_MAIN, IT_THEME);
		return MENU_OPEN;
	}

	/* Use it: back to the menu with it */
	if(kDown & KEY_A)
	{
		useTheme(m, s, m->selected);
		openPage(m, PAGE_MAIN, IT_THEME);
		uiRequestRedraw();
		return MENU_OPEN;
	}

	/* Try it on */
	if(kDown & KEY_Y)
		useTheme(m, s, m->selected);

	if(dirs & (KEY_UP | KEY_DOWN))
	{
		m->selected = (m->selected + ((dirs & KEY_UP) ? -1 : 1) +
		               themeCount()) % themeCount();
		drawMenu(m);
	}

	/* Stylus: tap a row to select it, tap it again to try it on */
	if(kDown & KEY_TOUCH)
	{
		touchPosition	tp;
		int				i;

		hidTouchRead(&tp);
		i = tp.py / 8 - FIRST_ROW;
		if(i >= 0 && i < themeCount())
		{
			if(i == m->selected)
				useTheme(m, s, i);
			else
			{
				m->selected = i;
				drawMenu(m);
			}
		}
	}

	return MENU_OPEN;
}

enum menu_result menuHandleInput(struct menu* m, u32 kDown, u32 kHeld,
                                 struct screens* s, struct browser* b)
{
	const struct page*	p = curPage(m);
	u32					dirs = repeatKeys(kDown, kHeld);

	console = &s->bottom;
	menuScreens = s;

	if(m->picking)
		return themesInput(m, kDown, dirs, s, b);

	if(kHeld & KEY_TOUCH)
		touch(m, kDown);
	else
		m->drag = -1;

	/* Close: SELECT, or B on the main page (B goes back from a page) */
	if((kDown & KEY_SELECT) || ((kDown & KEY_B) && m->page == PAGE_MAIN))
	{
		browserSetVisible(b, true);
		return MENU_CLOSED;
	}
	if(kDown & KEY_B)
	{
		openPage(m, PAGE_MAIN, pageParent(m->page));
		return MENU_OPEN;
	}

	if(dirs & (KEY_UP | KEY_DOWN))
	{
		int step = (dirs & KEY_UP) ? -1 : 1;
		int k = m->selected;

		do
			k = (k + step + p->count) % p->count;
		while(p->items[k].kind == K_HEADER);
		m->selected = k;
		drawMenu(m);
	}

	if(dirs & (KEY_LEFT | KEY_RIGHT))
		change(m, s, p->items[m->selected].id,
		       (dirs & KEY_RIGHT) ? 1 : -1);

	if(kDown & KEY_A)
		return activate(m, s, b, curPage(m)->items[m->selected].id);

	return MENU_OPEN;
}

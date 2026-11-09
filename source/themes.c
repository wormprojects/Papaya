/**
 * ctrmus - 3DS Music Player
 * Theme implementation.
 *
 * Provides 16 built-in themes and the ability to load a custom theme
 * from /3ds/papaya/themes/theme.cfg.
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "paths.h"
#include "saver.h"
#include "themes.h"

static struct theme_t currentTheme;
static int currentThemeIndex = 0;

/* 16 built-in themes */
static const struct theme_t builtinThemes[NUM_BUILTIN_THEMES] =
{
	/* 0: Classic Dark */
	{
		.name = "Classic Dark",
		.bg           = { 0x1A, 0x1A, 0x2E },
		.textActive   = { 0xFF, 0xFF, 0xFF },
		.textInactive = { 0x80, 0x80, 0x90 },
		.progressBar  = { 0x00, 0xAA, 0xFF },
		.highlight    = { 0x00, 0x77, 0xCC },
	},
	/* 1: Ocean Blue */
	{
		.name = "Ocean Blue",
		.bg           = { 0x0A, 0x1A, 0x3A },
		.textActive   = { 0xCC, 0xEE, 0xFF },
		.textInactive = { 0x55, 0x77, 0x99 },
		.progressBar  = { 0x00, 0x99, 0xDD },
		.highlight    = { 0x00, 0x55, 0xAA },
	},
	/* 2: Forest Green */
	{
		.name = "Forest Green",
		.bg           = { 0x0A, 0x2A, 0x0A },
		.textActive   = { 0xCC, 0xFF, 0xCC },
		.textInactive = { 0x55, 0x88, 0x55 },
		.progressBar  = { 0x33, 0xCC, 0x33 },
		.highlight    = { 0x00, 0x88, 0x44 },
	},
	/* 3: Royal Purple */
	{
		.name = "Royal Purple",
		.bg           = { 0x1A, 0x0A, 0x2E },
		.textActive   = { 0xEE, 0xCC, 0xFF },
		.textInactive = { 0x88, 0x55, 0xAA },
		.progressBar  = { 0xBB, 0x55, 0xFF },
		.highlight    = { 0x77, 0x22, 0xCC },
	},
	/* 4: Neon Synthwave */
	{
		.name = "Neon Synthwave",
		.bg           = { 0x0D, 0x0B, 0x1A },
		.textActive   = { 0xFF, 0x00, 0xFF },
		.textInactive = { 0x66, 0x33, 0x99 },
		.progressBar  = { 0x00, 0xFF, 0xFF },
		.highlight    = { 0xFF, 0x44, 0x88 },
	},
	/* 5: Sunset Orange */
	{
		.name = "Sunset Orange",
		.bg           = { 0x2A, 0x15, 0x0A },
		.textActive   = { 0xFF, 0xDD, 0x99 },
		.textInactive = { 0x99, 0x66, 0x44 },
		.progressBar  = { 0xFF, 0x88, 0x00 },
		.highlight    = { 0xCC, 0x44, 0x00 },
	},
	/* 6: Cherry Blossom */
	{
		.name = "Cherry Blossom",
		.bg           = { 0x2E, 0x15, 0x22 },
		.textActive   = { 0xFF, 0xCC, 0xDD },
		.textInactive = { 0xAA, 0x66, 0x88 },
		.progressBar  = { 0xFF, 0x66, 0x99 },
		.highlight    = { 0xDD, 0x33, 0x77 },
	},
	/* 7: Arctic White */
	{
		.name = "Arctic White",
		.bg           = { 0xE8, 0xE8, 0xF0 },
		.textActive   = { 0x22, 0x22, 0x33 },
		.textInactive = { 0x88, 0x88, 0x99 },
		.progressBar  = { 0x44, 0x88, 0xCC },
		.highlight    = { 0x33, 0x66, 0xAA },
	},
	/* 8: Midnight Galaxy */
	{
		.name = "Midnight Galaxy",
		.bg           = { 0x05, 0x05, 0x15 },
		.textActive   = { 0xDD, 0xDD, 0xFF },
		.textInactive = { 0x44, 0x44, 0x77 },
		.progressBar  = { 0x88, 0x44, 0xFF },
		.highlight    = { 0x44, 0x22, 0xAA },
	},
	/* 9: Retro Amber */
	{
		.name = "Retro Amber",
		.bg           = { 0x18, 0x12, 0x00 },
		.textActive   = { 0xFF, 0xBB, 0x00 },
		.textInactive = { 0x88, 0x66, 0x00 },
		.progressBar  = { 0xFF, 0xDD, 0x33 },
		.highlight    = { 0xCC, 0x88, 0x00 },
	},
	/* 10: Bubblegum */
	{
		.name = "Bubblegum",
		.bg           = { 0x2E, 0x0A, 0x2E },
		.textActive   = { 0xFF, 0x99, 0xFF },
		.textInactive = { 0x99, 0x55, 0x99 },
		.progressBar  = { 0xFF, 0x44, 0xCC },
		.highlight    = { 0xCC, 0x22, 0x99 },
	},
	/* 11: Crimson Night */
	{
		.name = "Crimson Night",
		.bg           = { 0x1A, 0x05, 0x05 },
		.textActive   = { 0xFF, 0xCC, 0xCC },
		.textInactive = { 0x88, 0x44, 0x44 },
		.progressBar  = { 0xFF, 0x22, 0x22 },
		.highlight    = { 0xCC, 0x00, 0x00 },
	},
	/* 12: Solarized Dark */
	{
		.name = "Solarized Dark",
		.bg           = { 0x00, 0x2B, 0x36 },
		.textActive   = { 0x83, 0x94, 0x96 },
		.textInactive = { 0x58, 0x6E, 0x75 },
		.progressBar  = { 0x26, 0x8B, 0xD2 },
		.highlight    = { 0x2A, 0xA1, 0x98 },
	},
	/* 13: Matcha */
	{
		.name = "Matcha",
		.bg           = { 0x1C, 0x20, 0x1C },
		.textActive   = { 0xB8, 0xDB, 0x9A },
		.textInactive = { 0x6A, 0x7F, 0x5A },
		.progressBar  = { 0x7E, 0xC8, 0x50 },
		.highlight    = { 0x4E, 0xA8, 0x30 },
	},
	/* 14: Frost */
	{
		.name = "Frost",
		.bg           = { 0xD8, 0xE8, 0xF0 },
		.textActive   = { 0x2E, 0x3D, 0x4F },
		.textInactive = { 0x7A, 0x8A, 0x9A },
		.progressBar  = { 0x5E, 0x9C, 0xD6 },
		.highlight    = { 0x3B, 0x7D, 0xD8 },
	},
	/* 15: Dracula */
	{
		.name = "Dracula",
		.bg           = { 0x28, 0x2A, 0x36 },
		.textActive   = { 0xF8, 0xF8, 0xF2 },
		.textInactive = { 0x62, 0x72, 0xA4 },
		.progressBar  = { 0xBD, 0x93, 0xF9 },
		.highlight    = { 0xFF, 0x79, 0xC6 },
	},
};

/* The user's own theme (THEME_CUSTOM): edited in the menu, kept in
 * themes/custom.cfg. */
static struct theme_t customTheme;
static bool haveCustom = false;
static bool customUnsaved = false;

/* The theme changed (cycleTheme, the editor) and isn't saved yet. */
static bool themeUnsaved = false;

int themeCount(void)
{
	return NUM_BUILTIN_THEMES + (haveCustom ? 1 : 0);
}

int getBuiltinTheme(int index, struct theme_t* out)
{
	if(out == NULL)
		return -1;
	if(index == THEME_CUSTOM && haveCustom)
	{
		*out = customTheme;
		return 0;
	}
	if(index < 0 || index >= NUM_BUILTIN_THEMES)
		return -1;

	*out = builtinThemes[index];
	return 0;
}

/**
 * Parse a hex color string (#RRGGBB or RRGGBB) into a themeColor.
 */
static int parseHexColor(const char* str, struct themeColor* out)
{
	unsigned int r, g, b;
	const char* hex = str;

	if(hex == NULL || out == NULL)
		return -1;

	/* Skip leading '#' if present */
	if(*hex == '#')
		hex++;

	/* Skip leading "0x" or "0X" */
	if(hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
		hex += 2;

	if(strlen(hex) < 6)
		return -1;

	if(sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3)
		return -1;

	out->r = (uint8_t)r;
	out->g = (uint8_t)g;
	out->b = (uint8_t)b;
	return 0;
}

/**
 * Strip leading/trailing whitespace in place by returning pointer offset
 * and null terminating.
 */
static char* stripWhitespace(char* str)
{
	char* end;

	while(*str == ' ' || *str == '\t')
		str++;

	if(*str == '\0')
		return str;

	end = str + strlen(str) - 1;
	while(end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
		end--;

	*(end + 1) = '\0';
	return str;
}

/*
 * Read a theme file: `theme_index = N`, `name = ...` and the five colours.
 * `index` gets N (left alone if there's none), `colors` is set if any
 * colour was given. Returns false if the file isn't there.
 */
static bool readThemeFile(const char* path, struct theme_t* out, int* index,
                          bool* colors)
{
	FILE*	f = fopen(path, "r");
	char	line[128];

	if(f == NULL)
		return false;

	while(fgets(line, sizeof(line), f) != NULL)
	{
		char* eq;
		char* key;
		char* value;

		/* Skip comments and blank lines */
		if(line[0] == '#' || line[0] == ';' || line[0] == '\n' ||
		   line[0] == '\r')
			continue;

		eq = strchr(line, '=');
		if(eq == NULL)
			continue;

		*eq = '\0';
		key = stripWhitespace(line);
		value = stripWhitespace(eq + 1);

		if(strcasecmp(key, "theme_index") == 0)
		{
			int idx = atoi(value);

			if(index != NULL && idx >= 0 && idx <= NUM_BUILTIN_THEMES)
				*index = idx;
		}
		else if(strcasecmp(key, "name") == 0)
		{
			snprintf(out->name, sizeof(out->name), "%s", value);
		}
		else if(strcasecmp(key, "bg") == 0)
		{
			if(parseHexColor(value, &out->bg) == 0)
				*colors = true;
		}
		else if(strcasecmp(key, "text_active") == 0)
		{
			if(parseHexColor(value, &out->textActive) == 0)
				*colors = true;
		}
		else if(strcasecmp(key, "text_inactive") == 0)
		{
			if(parseHexColor(value, &out->textInactive) == 0)
				*colors = true;
		}
		else if(strcasecmp(key, "progress_bar") == 0)
		{
			if(parseHexColor(value, &out->progressBar) == 0)
				*colors = true;
		}
		else if(strcasecmp(key, "highlight") == 0)
		{
			if(parseHexColor(value, &out->highlight) == 0)
				*colors = true;
		}
	}
	fclose(f);
	return true;
}

int loadTheme(struct theme_t* out)
{
	struct theme_t	file;
	int				index = -1;
	bool			colors = false;
	bool			found;

	if(out == NULL)
		return -1;

	/* The user's own theme, if they made one */
	customTheme = builtinThemes[0];
	snprintf(customTheme.name, sizeof(customTheme.name), "Custom");
	haveCustom = false;
	readThemeFile(PAPAYA_DIR "/themes/custom.cfg", &customTheme, NULL,
	              &haveCustom);

	/* The one in use: a built-in (or custom) number. A theme.cfg from
	 * before custom.cfg existed has the colours itself: they become the
	 * custom theme. */
	file = builtinThemes[0];
	found = readThemeFile(PAPAYA_DIR "/themes/theme.cfg", &file, &index,
	                      &colors);
	if(found && colors && index < 0)
	{
		customTheme = file;
		snprintf(customTheme.name, sizeof(customTheme.name), "Custom");
		haveCustom = true;
		index = THEME_CUSTOM;
		themeUnsaved = true;
		customUnsaved = true;
	}

	if(index < 0 || (index == THEME_CUSTOM && !haveCustom))
		index = 0;
	currentThemeIndex = index;
	getBuiltinTheme(index, out);
	currentTheme = *out;
	return found && index >= 0 ? 0 : 1;
}


/* Written by saver.c in the background (see settingsSaveIfChanged()). */
int saveTheme(int themeIndex)
{
	static const char* const dirs[] = {
		"sdmc:/3ds", PAPAYA_DIR, PAPAYA_DIR "/themes", NULL
	};
	char	text[1024];
	int		n;

	n = snprintf(text, sizeof(text),
	             "# Papaya theme configuration\n"
	             "# Set theme_index to a value 0-%d for built-in themes,\n"
	             "# or %d for your own theme (themes/custom.cfg, made in\n"
	             "# Menu > Edit theme).\n"
	             "#\n"
	             "# Built-in themes:\n", NUM_BUILTIN_THEMES - 1,
	             THEME_CUSTOM);
	for(int i = 0; i < NUM_BUILTIN_THEMES && n > 0 &&
	    n < (int)sizeof(text); i++)
		n += snprintf(text + n, sizeof(text) - (size_t)n, "#   %d: %s\n", i,
		              builtinThemes[i].name);
	if(n > 0 && n < (int)sizeof(text))
		n += snprintf(text + n, sizeof(text) - (size_t)n,
		              "\ntheme_index = %d\n", themeIndex);
	if(n <= 0 || n >= (int)sizeof(text))
		return -1;

	saverWrite(PAPAYA_DIR "/themes/theme.cfg", dirs, text, (size_t)n);

	if(haveCustom && customUnsaved)
	{
		const struct theme_t* c = &customTheme;

		n = snprintf(text, sizeof(text),
		             "# Your own theme, edited in Menu > Edit theme.\n"
		             "name = %s\n"
		             "bg = #%02X%02X%02X\n"
		             "text_active = #%02X%02X%02X\n"
		             "text_inactive = #%02X%02X%02X\n"
		             "progress_bar = #%02X%02X%02X\n"
		             "highlight = #%02X%02X%02X\n", c->name,
		             c->bg.r, c->bg.g, c->bg.b,
		             c->textActive.r, c->textActive.g, c->textActive.b,
		             c->textInactive.r, c->textInactive.g,
		             c->textInactive.b,
		             c->progressBar.r, c->progressBar.g, c->progressBar.b,
		             c->highlight.r, c->highlight.g, c->highlight.b);
		if(n > 0 && n < (int)sizeof(text))
			saverWrite(PAPAYA_DIR "/themes/custom.cfg", dirs, text,
			           (size_t)n);
		customUnsaved = false;
	}

	currentThemeIndex = themeIndex;
	themeUnsaved = false;
	return 0;
}

void themeSaveIfChanged(void)
{
	if(themeUnsaved)
		saveTheme(currentThemeIndex);
}

void applyTheme(PrintConsole* screen, const struct theme_t* theme)
{
	if(screen == NULL || theme == NULL)
		return;

	/*
	 * Convert RGB888 theme colors to RGB565 and use custom color mode.
	 * libctru's console uses u16 RGB565 values when CONSOLE_FG_CUSTOM and
	 * CONSOLE_BG_CUSTOM flags are set in the flags field.
	 */
	screen->fg = ((theme->textActive.r >> 3) << 11) |
			((theme->textActive.g >> 2) << 5) |
			(theme->textActive.b >> 3);
	screen->bg = ((theme->bg.r >> 3) << 11) |
			((theme->bg.g >> 2) << 5) |
			(theme->bg.b >> 3);
	screen->flags = CONSOLE_FG_CUSTOM | CONSOLE_BG_CUSTOM;

	/* Select this console, clear to apply bg color, then restore previous */
	{
		PrintConsole* prev = consoleSelect(screen);
		consoleClear();
		consoleSelect(prev);
	}
}

const struct theme_t* getCurrentTheme(void)
{
	return &currentTheme;
}

int getCurrentThemeIndex(void)
{
	return currentThemeIndex;
}

int cycleTheme(int delta)
{
	int count = themeCount();

	currentThemeIndex = ((currentThemeIndex + delta) % count + count) % count;

	if(getBuiltinTheme(currentThemeIndex, &currentTheme) != 0)
	{
		currentThemeIndex = 0;
		getBuiltinTheme(0, &currentTheme);
	}

	/* Not saved here: writing to the SD card on every press made cycling
	 * slow. themeSaveIfChanged() saves it when the menu closes. */
	themeUnsaved = true;
	return currentThemeIndex;
}

int setThemeIndex(int index)
{
	return cycleTheme(index - currentThemeIndex);
}

void themeSetActiveColor(void)
{
	printf("\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um",
		currentTheme.textActive.r, currentTheme.textActive.g, currentTheme.textActive.b,
		currentTheme.bg.r, currentTheme.bg.g, currentTheme.bg.b);
}

void themeSetHighlightColor(void)
{
	printf("\x1b[38;2;%u;%u;%um",
		currentTheme.highlight.r, currentTheme.highlight.g, currentTheme.highlight.b);
}

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

static struct themeColor* slotOf(struct theme_t* t, int slot)
{
	switch(slot)
	{
		case THEME_COLOR_BG:			return &t->bg;
		case THEME_COLOR_TEXT_ACTIVE:	return &t->textActive;
		case THEME_COLOR_TEXT_INACTIVE:	return &t->textInactive;
		case THEME_COLOR_PROGRESS:		return &t->progressBar;
		default:						return &t->highlight;
	}
}

void themeEditBegin(void)
{
	if(currentThemeIndex == THEME_CUSTOM && haveCustom)
		return;

	/* Start from the theme in use */
	customTheme = currentTheme;
	snprintf(customTheme.name, sizeof(customTheme.name), "Custom");
	haveCustom = true;
	currentThemeIndex = THEME_CUSTOM;
	currentTheme = customTheme;
	themeUnsaved = true;
	customUnsaved = true;
}

struct themeColor themeEditGet(int slot)
{
	return *slotOf(&customTheme, slot);
}

void themeEditSet(int slot, struct themeColor c)
{
	*slotOf(&customTheme, slot) = c;
	currentTheme = customTheme;
	currentThemeIndex = THEME_CUSTOM;
	themeUnsaved = true;
	customUnsaved = true;
}

/**
 * ctrmus - 3DS Music Player
 * Theme support system.
 *
 * Allows loading color themes from /3ds/papaya/themes/theme.cfg
 * and provides 16 built-in themes plus one of the user's own.
 */

#ifndef ctrmus_themes_h
#define ctrmus_themes_h

#include <stdint.h>
#include <stdbool.h>

/**
 * ANSI color indices used by the 3DS console.
 * The 3DS console supports 8 standard colors (0-7).
 * We remap them via palette to achieve theming.
 */

/* Theme color slots */
#define THEME_COLOR_BG          0
#define THEME_COLOR_TEXT_ACTIVE  1
#define THEME_COLOR_TEXT_INACTIVE 2
#define THEME_COLOR_PROGRESS    3
#define THEME_COLOR_HIGHLIGHT   4
#define THEME_NUM_COLORS        5

/**
 * ANSI escape helpers for theme colors.
 * The 3DS console maps ANSI colors 30-37 for foreground
 * and 40-47 for background. We'll use direct RGB for our
 * print functions.
 */

/* RGB888 color */
struct themeColor
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct theme_t
{
	char name[32];
	struct themeColor bg;
	struct themeColor textActive;
	struct themeColor textInactive;
	struct themeColor progressBar;
	struct themeColor highlight;
};

/* Number of built-in themes */
#define NUM_BUILTIN_THEMES 16

/* Theme index of the user's own theme (made in Menu > Edit theme): the
 * one after the built-in ones, there once they've made it. */
#define THEME_CUSTOM NUM_BUILTIN_THEMES

/** Themes to choose from: the built-in ones, plus the custom one if any. */
int themeCount(void);

/**
 * Get a built-in theme by index.
 *
 * \param	index	Theme index (0 to NUM_BUILTIN_THEMES-1).
 * \param	out		Output theme structure.
 * \return			0 on success, -1 if index out of range.
 */
int getBuiltinTheme(int index, struct theme_t* out);

/**
 * Try to load a theme from /3ds/papaya/themes/theme.cfg.
 * Falls back to built-in theme 0 (Classic Dark) on failure.
 *
 * \param	out	Output theme structure.
 * \return		0 on success, 1 if fell back to default.
 */
int loadTheme(struct theme_t* out);

/**
 * Save current theme index to /3ds/papaya/themes/theme.cfg.
 *
 * \param	themeIndex	Index of the theme to save.
 * \return				0 on success, -1 on failure.
 */
int saveTheme(int themeIndex);

/**
 * Apply a theme to the 3DS console output.
 * Sets the ANSI color palette entries to match the theme.
 *
 * \param	screen	PrintConsole to apply the theme to.
 * \param	theme	Theme to apply.
 */
void applyTheme(PrintConsole* screen, const struct theme_t* theme);

/**
 * Print a string using theme active text color.
 */
void themePrintActive(const char* str);

/**
 * Print a string using theme inactive text color.
 */
void themePrintInactive(const char* str);

/**
 * Get the currently active theme.
 */
const struct theme_t* getCurrentTheme(void);

/**
 * Get the current theme index.
 */
int getCurrentThemeIndex(void);

/**
 * Move `delta` themes forward (or back, if negative), wrapping around.
 * Not saved yet: see themeSaveIfChanged().
 *
 * \return	Index of the new theme.
 */
int cycleTheme(int delta);

/**
 * Start editing the custom theme: it becomes the theme in use, starting
 * from the one that was (unless it was the custom one already).
 */
void themeEditBegin(void);

/** A colour (THEME_COLOR_*) of the custom theme. */
struct themeColor themeEditGet(int slot);

/** Change a colour of the custom theme, which is in use (not saved until
 * themeSaveIfChanged()). */
void themeEditSet(int slot, struct themeColor c);

/** Switch to built-in theme `index` (not saved yet, like cycleTheme). */
int setThemeIndex(int index);

/** Save the theme if cycleTheme() changed it since the last save. */
void themeSaveIfChanged(void);

/**
 * Emit ANSI escape to set console fg/bg to current theme's active
 * text color and background color.
 */
void themeSetActiveColor(void);

/**
 * Emit ANSI escape to set console fg to current theme's highlight color.
 * Background is not changed.
 */
void themeSetHighlightColor(void);

#endif

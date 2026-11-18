/* The Mac app's frame loop (engine/app.c). */
#ifndef papaya_app_h
#define papaya_app_h

void appInit(void);
/** Once per display frame, after the input was set (macSetInput). */
void appTick(void);
void appShutdown(void);

#include <stdbool.h>
#include <stddef.h>

/** The window is now w x h logical pixels: re-lay out the top screen. */
void appResizeTop(int w, int h);

/** The bottom screen is now cols x rows characters (a text console). */
void appResizeBottom(int cols, int rows);

/** Scroll the open list by `rows` (wheel). False: nothing to scroll here. */
bool appScroll(int rows);

/* System media controls (media keys, Now Playing, headphones) */
enum appCommand
{
	APP_PLAY_PAUSE, APP_PLAY, APP_PAUSE, APP_NEXT, APP_PREVIOUS,
	APP_SEEK_BACK, APP_SEEK_FORWARD
};
void appDoCommand(enum appCommand cmd);
void appSeekTo(double seconds);

struct appState
{
	bool		playing;		/* a song is loaded (maybe paused) */
	bool		paused;
	unsigned	songId;			/* changes when a song starts */
	double		elapsed, total;	/* seconds; total 0 = unknown */
	double		speed;
	char		title[256], artist[256], album[256];
	char		path[1024];
};
void appGetState(struct appState* st);
bool appCoverArt(const char* path, unsigned char** data, size_t* len);

#endif

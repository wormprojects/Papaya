/* What shim/os.c needs from the Mac app (replaces desktop/src/platform.h). */
#ifndef papaya_mac_platform_h
#define papaya_mac_platform_h
#include <stdbool.h>

/** The app asked to quit (menu "Exit Papaya", Cmd-Q). */
bool	videoQuitRequested(void);
void	videoRequestQuit(void);
/** The machine's battery charge 0-100, or -1 (a desktop without one). */
int		nativeBatteryPercent(void);

#endif

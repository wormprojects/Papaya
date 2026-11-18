/**
 * Papaya for macOS - the end of playFile(): tell player.c's watchdog thread
 * the song finished (error -1) or failed (errno / error.h code), then end
 * the thread. Separate file because it needs <3ds.h>, which can't be mixed
 * with CoreAudio's headers (both define `Handle`).
 */

#include <3ds.h>

#include "error.h"
#include "playback.h"

void macPlaybackDone(struct playbackInfo_t* info, int error)
{
	*info->errInfo->error = error;
	svcSignalEvent(*info->errInfo->failEvent);
	threadExit(0);
}

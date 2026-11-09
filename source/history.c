/**
 * ctrmus - 3DS Music Player
 * Playback history. See history.h.
 */

#include <limits.h>
#include <string.h>

#include "history.h"

static char historyPaths[MAX_HISTORY][PATH_MAX];
static int historyCount = 0;

void historyPush(const char* path)
{
	if(path == NULL || path[0] == '\0')
		return;
	if(historyCount < MAX_HISTORY)
	{
		strncpy(historyPaths[historyCount], path,
		        PATH_MAX - 1);
		historyPaths[historyCount][PATH_MAX - 1] = '\0';
		historyCount++;
	}
	else
	{
		/* Shift everything down, drop oldest */
		for(int i = 0; i < MAX_HISTORY - 1; i++)
			memcpy(historyPaths[i], historyPaths[i+1], PATH_MAX);
		strncpy(historyPaths[MAX_HISTORY - 1], path,
		        PATH_MAX - 1);
		historyPaths[MAX_HISTORY - 1][PATH_MAX - 1] = '\0';
	}
}

int historyPop(char* out, size_t outLen)
{
	if(historyCount <= 0)
		return -1;
	historyCount--;
	strncpy(out, historyPaths[historyCount], outLen - 1);
	out[outLen - 1] = '\0';
	historyPaths[historyCount][0] = '\0';
	return 0;
}

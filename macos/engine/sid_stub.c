/** No SID player on the Mac yet: a .sid fails like any unsupported file. */
#include "playback.h"
#include "sid.h"

static int sidInit(const char* f) { (void)f; return -1; }
static uint32_t sidRate(void) { return 44100; }
static uint8_t sidChannels(void) { return 2; }
static uint64_t sidFill(void* o) { (void)o; return 0; }
static void sidExit(void) {}

void setSid(struct decoder_fn* d)
{
	d->init = sidInit; d->rate = sidRate; d->channels = sidChannels;
	d->buffSize = 4096; d->decode = sidFill; d->exit = sidExit;
	d->getFileSamples = NULL; d->seek = NULL;
}

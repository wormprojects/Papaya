/**
 * Papaya desktop: the part of libctru the app uses, for the host.
 *
 * The 3DS sources in ../source are compiled unchanged against this header
 * (it is found as <3ds.h> because desktop/shim is first on the include
 * path). Everything declared here is implemented in desktop/src/:
 *
 *   os.c       threads, locks, events, time, apt/os/svc odds and ends
 *   console.c  the 40x30 text console (the bottom screen's browser/menu)
 *   video.c    the two SDL windows, keyboard/mouse as buttons/stylus
 *   audio.c    ndsp on SDL audio
 *
 * Keep it to what the app really calls: a missing function is a compile
 * error on purpose (add it here and in the matching .c), not a silent stub.
 */

#ifndef papaya_shim_3ds_h
#define papaya_shim_3ds_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;
typedef int8_t		s8;
typedef int16_t		s16;
typedef int32_t		s32;
typedef int64_t		s64;
typedef volatile u8		vu8;
typedef volatile u16	vu16;
typedef volatile u32	vu32;

typedef s32	Result;
typedef u32	Handle;

#define BIT(n)			(1U << (n))
#define U64_MAX			UINT64_MAX
#define R_SUCCEEDED(res)	((res) >= 0)
#define R_FAILED(res)		((res) < 0)

/* ------------------------------------------------------------------ */
/* Threads, locks, events, time (os.c)                                 */
/* ------------------------------------------------------------------ */

typedef void (*ThreadFunc)(void*);
typedef struct Thread_tag* Thread;

#define CUR_THREAD_HANDLE	0xFFFF8000u

typedef enum
{
	RESET_ONESHOT = 0,
	RESET_STICKY = 1,
	RESET_PULSE = 2
} ResetType;

/* Opaque storage; os.c casts it. Sized for a pthread mutex + cond. */
typedef struct { u64 opaque[8]; } LightLock;
typedef struct { u64 opaque[16]; } LightEvent;

void	LightLock_Init(LightLock* lock);
void	LightLock_Lock(LightLock* lock);
int		LightLock_TryLock(LightLock* lock);
void	LightLock_Unlock(LightLock* lock);

void	LightEvent_Init(LightEvent* event, ResetType reset);
void	LightEvent_Signal(LightEvent* event);
void	LightEvent_Wait(LightEvent* event);
void	LightEvent_Clear(LightEvent* event);

Thread	threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size,
                     int prio, int affinity, bool detached);
Result	threadJoin(Thread thread, u64 timeout_ns);
void	threadFree(Thread thread);
void	threadExit(int rc);

Result	svcGetThreadPriority(s32* out, Handle thread);
void	svcSleepThread(s64 ns);
Result	svcCreateEvent(Handle* event, ResetType reset);
Result	svcSignalEvent(Handle event);
Result	svcClearEvent(Handle event);
Result	svcWaitSynchronization(Handle handle, s64 timeout_ns);

/** Milliseconds (a monotonic-ish clock; the app only subtracts them). */
u64		osGetTime(void);

void*	linearAlloc(size_t size);
void	linearFree(void* mem);
Result	DSP_FlushDataCache(const void* address, u32 size);

/* ------------------------------------------------------------------ */
/* System services that don't exist here (os.c)                        */
/* ------------------------------------------------------------------ */

bool	aptMainLoop(void);
void	aptSetSleepAllowed(bool allowed);
Result	APT_CheckNew3DS(bool* out);
void	osSetSpeedupEnable(bool enable);
bool	osIsHeadsetConnected(void);

/* The 3DS's sound-volume slider. Always fails here: the app then hides
 * its volume line (the OS has its own volume). */
Result	HIDUSER_GetSoundVolume(u8* volume);

/* Battery: the laptop's, if it has one (mcuHwcInit() fails otherwise) */
Result	mcuHwcInit(void);
void	mcuHwcExit(void);
Result	MCUHWC_GetBatteryLevel(u8* level);

/* Backlight and config savegame: not here (the calls fail) */
#define GSPLCD_SCREEN_TOP	1
#define GSPLCD_SCREEN_BOTH	3
Result	gspLcdInit(void);
void	gspLcdExit(void);
Result	GSPLCD_GetBrightness(u32 screen, u32* brightness);
Result	GSPLCD_SetBrightness(u32 screen, u32 brightness);
Result	cfguInit(void);
void	cfguExit(void);
Result	CFGU_GetConfigInfoBlk2(u32 size, u32 block, void* out);
Result	CFG_GetConfigInfoBlk8(u32 size, u32 block, void* out);

/** Modification time of a file, seconds since 1970 (3DS: since 1900). */
Result	archive_getmtime(const char* path, u64* mtime);

/* ------------------------------------------------------------------ */
/* Buttons and stylus (video.c)                                        */
/* ------------------------------------------------------------------ */

enum
{
	KEY_A		= BIT(0),
	KEY_B		= BIT(1),
	KEY_SELECT	= BIT(2),
	KEY_START	= BIT(3),
	KEY_DRIGHT	= BIT(4),
	KEY_DLEFT	= BIT(5),
	KEY_DUP		= BIT(6),
	KEY_DDOWN	= BIT(7),
	KEY_R		= BIT(8),
	KEY_L		= BIT(9),
	KEY_X		= BIT(10),
	KEY_Y		= BIT(11),
	KEY_ZL		= BIT(14),
	KEY_ZR		= BIT(15),
	KEY_TOUCH	= BIT(20),
	KEY_UP		= KEY_DUP,
	KEY_DOWN	= KEY_DDOWN,
	KEY_LEFT	= KEY_DLEFT,
	KEY_RIGHT	= KEY_DRIGHT
};

typedef struct
{
	u16	px;
	u16	py;
} touchPosition;

void	hidScanInput(void);
u32		hidKeysDown(void);
u32		hidKeysHeld(void);
u32		hidKeysUp(void);
void	hidTouchRead(touchPosition* pos);
/** (macOS) The next character typed on the keyboard while a text field is
 * open (digits, letters, '\b' backspace, '\n' Return), or 0 if none. */
int		macTypedChar(void);

/* ------------------------------------------------------------------ */
/* Screens (video.c)                                                   */
/* ------------------------------------------------------------------ */

/* GFX_OVERLAY (macOS): a third, 320x240 surface the menu and the pages are
 * drawn on; the window shows it over whichever panel is up. */
typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1, GFX_OVERLAY = 2 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;
typedef enum { GSP_RGB565_OES = 2 } GSPGPU_FramebufferFormat;

void	gfxInitDefault(void);
void	gfxExit(void);
void	gfxSetScreenFormat(gfxScreen_t screen, GSPGPU_FramebufferFormat fmt);
void	gfxSetDoubleBuffering(gfxScreen_t screen, bool enable);
/** Column-major RGB565 like the 3DS: w = 240, h = 400 (top) / 320. */
u8*		gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width,
                          u16* height);
void	gfxFlushBuffers(void);
void	gfxSwapBuffers(void);
/** Presents both windows, then waits for the next 60 Hz frame. */
void	gspWaitForVBlank(void);

/* ------------------------------------------------------------------ */
/* Text console (console.c)                                            */
/* ------------------------------------------------------------------ */

#define CONSOLE_COLOR_BOLD		BIT(0)
#define CONSOLE_COLOR_FAINT		BIT(1)
#define CONSOLE_ITALIC			BIT(2)
#define CONSOLE_UNDERLINE		BIT(3)
#define CONSOLE_BLINK_SLOW		BIT(4)
#define CONSOLE_BLINK_FAST		BIT(5)
#define CONSOLE_COLOR_REVERSE	BIT(6)
#define CONSOLE_CONCEAL			BIT(7)
#define CONSOLE_CROSSED_OUT		BIT(8)
#define CONSOLE_FG_CUSTOM		BIT(9)
#define CONSOLE_BG_CUSTOM		BIT(10)

typedef struct
{
	const u8*	gfx;			/* 8x8 glyphs, 8 bytes each, MSB = left */
	u16			asciiOffset;
	u16			numChars;
} ConsoleFont;

typedef struct PrintConsole
{
	ConsoleFont	font;
	u16*		frameBuffer;
	int			cursorX, cursorY;
	int			prevCursorX, prevCursorY;
	int			consoleWidth, consoleHeight;
	int			windowX, windowY, windowWidth, windowHeight;
	int			tabSize;
	int			fg, bg;			/* palette index, or RGB565 if _CUSTOM */
	int			flags;
	bool		consoleInitialised;
} PrintConsole;

PrintConsole*	consoleInit(gfxScreen_t screen, PrintConsole* console);
PrintConsole*	consoleSelect(PrintConsole* console);
PrintConsole*	consoleGetDefault(void);
void			consoleClear(void);
/** (macOS) The window was resized: cols x rows characters now. */
void			consoleResize(PrintConsole* console, int cols, int rows);

/* ------------------------------------------------------------------ */
/* Sound (audio.c): one stereo/mono PCM16 channel is all the app uses  */
/* ------------------------------------------------------------------ */

#define NDSP_WBUF_FREE		0
#define NDSP_WBUF_QUEUED	1
#define NDSP_WBUF_PLAYING	2
#define NDSP_WBUF_DONE		3

enum { NDSP_OUTPUT_MONO = 0, NDSP_OUTPUT_STEREO = 1, NDSP_OUTPUT_SURROUND = 2 };
enum { NDSP_INTERP_POLYPHASE = 0, NDSP_INTERP_LINEAR = 1, NDSP_INTERP_NONE = 2 };
enum
{
	NDSP_FORMAT_MONO_PCM16 = 1,
	NDSP_FORMAT_STEREO_PCM16 = 5
};

typedef struct ndspWaveBuf ndspWaveBuf;
struct ndspWaveBuf
{
	union
	{
		s8*			data_pcm8;
		s16*		data_pcm16;
		const void*	data_vaddr;
	};
	u32			nsamples;		/* per channel */
	void*		adpcm_data;
	u32			offset;
	bool		looping;
	vu8			status;
	u16			sequence_id;
	ndspWaveBuf* next;
};

Result	ndspInit(void);
void	ndspExit(void);
void	ndspSetOutputMode(int mode);
void	ndspChnReset(int id);
void	ndspChnSetInterp(int id, int type);
void	ndspChnSetRate(int id, float rate);
void	ndspChnSetFormat(int id, u16 format);
void	ndspChnSetMix(int id, float mix[12]);
void	ndspChnSetPaused(int id, bool paused);
bool	ndspChnIsPaused(int id);
bool	ndspChnIsPlaying(int id);
void	ndspChnWaveBufAdd(int id, ndspWaveBuf* buf);
void	ndspChnWaveBufClear(int id);

#ifdef __cplusplus
}
#endif

#endif

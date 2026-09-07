#ifndef STUB_3DS_H
#define STUB_3DS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;

typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } GFXScreen;

typedef struct
{
	unsigned long _pad[2];
} PrintConsole;

void gfxInitDefault(void);
void gfxExit(void);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gspWaitForVBlank(void);
int  aptMainLoop(void);

int  socInit(u32* ctx, u32 size);
void socExit(void);

int  consoleInit(GFXScreen screen, PrintConsole* console);
int  consoleSelect(PrintConsole* console);

void hidScanInput(void);
u32  hidKeysDown(void);

#define KEY_A      0x00000001
#define KEY_B      0x00000002
#define KEY_X      0x00000004
#define KEY_Y      0x00000008
#define KEY_DRIGHT 0x00000010
#define KEY_DLEFT  0x00000020
#define KEY_L      0x00000200
#define KEY_R      0x00000400
#define KEY_START  0x00000800

#define SWKBD_TYPE_NORMAL        0
#define SWKBD_BUTTON_CONFIRM     0
#define SWKBD_NOTEMPTY_NOTBLANK  0
#define SWKBD_DARKEN_TOP_SCREEN  0

typedef struct
{
	int _pad;
} SwkbdState;

typedef int SwkbdButton;

void swkbdInit(SwkbdState* s, int type, int numButtons, int maxLen);
void swkbdSetHintText(SwkbdState* s, const char* hint);
void swkbdSetValidation(SwkbdState* s, int mode, int a, int b);
void swkbdSetFeatures(SwkbdState* s, u32 features);
int  swkbdInputText(SwkbdState* s, char* out, size_t outSize);

/* test hooks */
void console_reset_capture(void);
const char* console_capture(GFXScreen screen);
int  console_capture_contains(GFXScreen screen, const char* needle);
int  console_capture_max_col(GFXScreen screen);
void hid_feed(const u32* keys, int n);
void swkbd_feed(const char** answers, int n);

#if defined(__GNUC__)
#define STUB_PRINTF_ATTR(n, m) __attribute__((format(printf, n, m)))
#else
#define STUB_PRINTF_ATTR(n, m)
#endif

/* printf-like console entry points. Declaring the printf format positions
   lets GCC retain sign/format checking on the redirected calls. */
int  printConsole(PrintConsole* console, const char* fmt, ...)
	STUB_PRINTF_ATTR(2, 3);
int  fprintfConsole(PrintConsole* console, FILE* f, const char* fmt, ...)
	STUB_PRINTF_ATTR(3, 4);
int  vfprintfConsole(PrintConsole* console, FILE* f, const char* fmt, va_list ap)
	STUB_PRINTF_ATTR(3, 0);

/* Redirect app-level printf to the console selected via consoleSelect.
   (The real device does this in libctru; the host stub needs the macro.) */
#define printf(...) printConsole(0, __VA_ARGS__)

#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include "3ds.h"

#define CAP_SZ (1 << 18)

static char g_cap[2][CAP_SZ];
static size_t g_used[2];
static int g_cur = 0;

void console_reset_capture(void)
{
	for (int i = 0; i < 2; i++)
	{
		g_cap[i][0] = '\0';
		g_used[i] = 0;
	}
	g_cur = 0;
}

const char* console_capture(GFXScreen screen)
{
	return g_cap[screen];
}

int console_capture_contains(GFXScreen screen, const char* needle)
{
	return strstr(g_cap[screen], needle) != NULL;
}

int console_capture_max_col(GFXScreen screen)
{
	const char* c = g_cap[screen];
	int curc = 0;
	int max = 0;
	int i = 0;
	while (c[i])
	{
		if (c[i] == 0x1b && c[i + 1] == '[')
		{
			int j = i + 2;
			int row = 0, col = 0, first = 1;
			char cmd = 0;
			while (c[j])
			{
				unsigned q = (unsigned char)c[j];
				if (q >= '0' && q <= '9')
				{
					int* v = first ? &row : &col;
					*v = *v * 10 + (q - '0');
				}
				else if (q == ';')
					first = 0;
				else if (q >= 0x40 && q <= 0x7e)
				{
					cmd = (char)q;
					break;
				}
				else
					break;
				j++;
			}
			if (cmd == 'H')
				curc = col ? col : 1;
			else if (cmd == 'J' && row == 2)
				curc = 0;
			i = (cmd && c[j]) ? j + 1 : j;
			continue;
		}
		if ((unsigned char)c[i] >= 0x20)
		{
			curc++;
			if (curc - 1 > max)
				max = curc - 1;
		}
		i++;
	}
	return max;
}

static void append(int idx, const char* fmt, va_list ap)
{
	char tmp[2048];
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap2);
	va_end(ap2);
	if (n <= 0)
		return;
	if (n > (int)sizeof(tmp) - 1)
		n = (int)sizeof(tmp) - 1;
	if (g_used[idx] + (size_t)n >= CAP_SZ)
		n = (int)(CAP_SZ - 1 - g_used[idx]);
	if (n > 0)
	{
		memcpy(g_cap[idx] + g_used[idx], tmp, (size_t)n);
		g_used[idx] += (size_t)n;
		g_cap[idx][g_used[idx]] = '\0';
	}
}

static const PrintConsole* g_topCon = NULL;
static const PrintConsole* g_botCon = NULL;

int consoleInit(GFXScreen screen, PrintConsole* console)
{
	if (screen == GFX_TOP)
		g_topCon = console;
	else
		g_botCon = console;
	g_cur = screen;
	return screen;
}

int consoleSelect(PrintConsole* console)
{
	if (console == g_topCon)
		g_cur = 0;
	else if (console == g_botCon)
		g_cur = 1;
	else
		g_cur = 0;
	return g_cur;
}

int printConsole(PrintConsole* con, const char* fmt, ...)
{
	(void)con;
	int which = g_cur;
	va_list ap;
	va_start(ap, fmt);
	append(which, fmt, ap);
	va_end(ap);
	return 0;
}

int fprintfConsole(PrintConsole* con, FILE* f, const char* fmt, ...)
{
	(void)f;
	int which = (con == NULL) ? 0 : 1;
	va_list ap;
	va_start(ap, fmt);
	append(which, fmt, ap);
	va_end(ap);
	return 0;
}

int vfprintfConsole(PrintConsole* con, FILE* f, const char* fmt, va_list ap)
{
	(void)f;
	int which = (con == NULL) ? 0 : 1;
	append(which, fmt, ap);
	return 0;
}

void gfxInitDefault(void) {}
void gfxExit(void) {}
void gfxFlushBuffers(void) {}
void gfxSwapBuffers(void) {}
void gspWaitForVBlank(void) {}

int socInit(u32* ctx, u32 size) { (void)ctx; (void)size; return 0; }
void socExit(void) {}

static int g_apt = 1;
int aptMainLoop(void)
{
	return g_apt;
}

static u32 g_keys[16];
static int g_keyN = 0;
static int g_keyIdx = 0;

void hid_feed(const u32* keys, int n)
{
	g_keyN = n;
	g_keyIdx = 0;
	for (int i = 0; i < n && i < 16; i++)
		g_keys[i] = keys[i];
}

void hidScanInput(void) {}

u32 hidKeysDown(void)
{
	if (g_keys[g_keyIdx] == 0 || g_keyN == 0)
		return 0;
	return g_keys[g_keyIdx++];
}

void swkbdInit(SwkbdState* s, int type, int numButtons, int maxLen)
{
	(void)s; (void)type; (void)numButtons; (void)maxLen;
}
void swkbdSetHintText(SwkbdState* s, const char* hint)
{
	(void)s; (void)hint;
}
void swkbdSetValidation(SwkbdState* s, int mode, int a, int b)
{
	(void)s; (void)mode; (void)a; (void)b;
}
void swkbdSetFeatures(SwkbdState* s, u32 features)
{
	(void)s; (void)features;
}

static const char* g_answers[8];
static int g_answerN = 0;
static int g_answerIdx = 0;

void swkbd_feed(const char** answers, int n)
{
	g_answerN = n;
	g_answerIdx = 0;
	for (int i = 0; i < n && i < 8; i++)
		g_answers[i] = answers[i];
}

int swkbdInputText(SwkbdState* s, char* out, size_t outSize)
{
	(void)s;
	if (g_answerIdx < g_answerN && g_answers[g_answerIdx] != NULL)
	{
		snprintf(out, outSize, "%s", g_answers[g_answerIdx]);
		g_answerIdx++;
		return SWKBD_BUTTON_CONFIRM;
	}
	return 1;
}
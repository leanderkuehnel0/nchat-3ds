#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <malloc.h>

#include <3ds.h>

#include "bvg.h"

static PrintConsole topConsole, bottomConsole;

#define TOP_COLS 50
#define BOT_COLS 40

/* Standard output is ASCII; the console font cannot show umlauts, so
   transliterate them ("oe" for ö, "ae" for ä, "ue" for ü, "ss" for ß) and
   drop the trailing "(Berlin)" style suffix VBB adds. One source char ==
   one or two ASCII chars, so names stay short and fit cleanly. */
static void repl_text(char* dst, size_t dstsz, const char* src)
{
	size_t o = 0;
	const unsigned char* p = (const unsigned char*)src;
	while (*p && o + 1 < dstsz)
	{
		if (*p < 0x80)
		{
			dst[o++] = (char)*p;
			p += 1;
			continue;
		}
		const char* rep = "";
		size_t len = 1;
		if (*p == 0xC3 && (p[1] & 0xC0) == 0x80)
		{
			switch (p[1])
			{
			case 0x84: rep = "Ae"; break; /* Ä */
			case 0x96: rep = "Oe"; break; /* Ö */
			case 0x9C: rep = "Ue"; break; /* Ü */
			case 0xA4: rep = "ae"; break; /* ä */
			case 0xB6: rep = "oe"; break; /* ö */
			case 0xBC: rep = "ue"; break; /* ü */
			case 0x9F: rep = "ss"; break; /* ß */
			}
			len = 2;
		}
		size_t rn = strlen(rep);
		if (o + rn >= dstsz)
			rn = dstsz - 1 - o;
		if (rn)
		{
			memcpy(dst + o, rep, rn);
			o += rn;
		}
		p += len;
	}
	dst[o] = '\0';

	static const char* sufs[] = { " (Berlin)", " (Brandenburg)", " (DE)" };
	for (int i = 0; i < 3; i++)
	{
		size_t n = strlen(sufs[i]);
		if (o >= n && strcmp(dst + o - n, sufs[i]) == 0)
		{
			dst[o - n] = '\0';
			o -= n;
		}
	}
}

static void flip(void)
{
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

static struct tm clock_now(void)
{
	time_t now = time(NULL);
	struct tm lt;
	localtime_r(&now, &lt);
	return lt;
}

static void draw_clock(int y)
{
	struct tm lt = clock_now();
	consoleSelect(&topConsole);
	printf("\x1b[%d;%dH\x1b[96m%02d:%02d\x1b[0m", y, TOP_COLS - 4,
	       lt.tm_hour, lt.tm_min);
}

static void draw_title(int y)
{
	consoleSelect(&topConsole);
	printf("\x1b[%d;1H\x1b[37;1mBVG Navigator\x1b[0m\x1b[K", y);
}

/* A solid two-row box: first row with a tag and the station name, second
   row a blank fill so the colored slab reads as a card. */
static void draw_stop_slab(int y, int bg, const char* tag, const char* name)
{
	char txt[TOP_COLS + 1];
	repl_text(txt, sizeof(txt), name);
	char visible[TOP_COLS + 1];
	snprintf(visible, sizeof(visible), "%s  %.*s", tag,
		 TOP_COLS - (int)strlen(tag) - 2, txt);
	consoleSelect(&topConsole);
	printf("\x1b[%d;1H\x1b[%dm\x1b[30m%-*s\x1b[0m", y, bg, TOP_COLS, "");
	printf("\x1b[%d;1H\x1b[%dm\x1b[30;1m %s\x1b[0m", y, bg, visible);
	printf("\x1b[%d;1H\x1b[%dm\x1b[30m%-*s\x1b[0m", y + 1, bg, TOP_COLS, "");
}

static void draw_top_footer(void)
{
	consoleSelect(&topConsole);
	printf("\x1b[26;1H\x1b[90m%.*s\x1b[0m", TOP_COLS,
	       "--------------------------------------------------");
	printf("\x1b[28;1H\x1b[36mL\x1b[0m/\x1b[36mR\x1b[0m  Previous / Next journey\x1b[K");
	printf("\x1b[29;1H\x1b[32mA\x1b[0m  New route   \x1b[31mB\x1b[0m  Exit\x1b[K");
}

static void draw_top_header(const BvgStop* start, const BvgStop* dest)
{
	consoleSelect(&topConsole);
	printf("\x1b[2J");
	draw_title(1);
	draw_clock(1);
	draw_stop_slab(3, 106, "START", start->name);
	draw_stop_slab(7, 103, "DEST", dest->name);
}

static void draw_route_top(const BvgStop* start, const BvgStop* dest,
			   const BvgJourney* j, int idx, int total)
{
	draw_top_header(start, dest);
	consoleSelect(&topConsole);
	printf("\x1b[10;1H\x1b[37;1mJourney \x1b[36m%d/%d\x1b[0m\x1b[K", idx, total);
	printf("\x1b[11;1H\x1b[33m%02d:%02d -> %02d:%02d\x1b[0m   %d min   %d change(s)\x1b[K",
	       j->depH, j->depM, j->arrH, j->arrM, j->durationMin, j->transfers);
	{
		char dir[64];
		repl_text(dir, sizeof(dir), j->direction);
		if (dir[0])
			printf("\x1b[12;1H\x1b[33m%s\x1b[0m \x1b[90m->\x1b[0m %.*s\x1b[K",
			       j->line, TOP_COLS - (int)strlen(j->line) - 4, dir);
	}
	draw_top_footer();
}

static void draw_route_bottom(const BvgJourney* j, int idx, int total)
{
	consoleSelect(&bottomConsole);
	printf("\x1b[2J");
	printf("\x1b[1;1H\x1b[33mRoute %d/%d\x1b[0m   \x1b[96m%02d:%02d -> %02d:%02d\x1b[0m\x1b[K",
	       idx, total, j->depH, j->depM, j->arrH, j->arrM);

	int y = 3;
	for (int i = 0; i < j->legCount; i++)
	{
		const BvgLeg* l = &j->legs[i];
		const char* lbl = l->walking ? "walk" : l->label;
		int lc = l->walking ? 32 : 33;

		char from[48], dir[64];
		repl_text(from, sizeof(from), l->from);
		repl_text(dir, sizeof(dir), l->direction);

		printf("\x1b[%d;1H\x1b[96m%02d:%02d\x1b[0m \x1b[%dm%-6s\x1b[0m %.*s\x1b[K",
		       y, l->depH, l->depM, lc, lbl, BOT_COLS - 17, from);
		y++;
		if (!l->walking && dir[0])
		{
			printf("\x1b[%d;1H      \x1b[90m->\x1b[0m %.*s\x1b[K",
			       y, BOT_COLS - 9, dir);
			y++;
		}
		if (i + 1 < j->legCount)
		{
			printf("\x1b[%d;1H      \x1b[90m|\x1b[0m\x1b[K", y);
			y++;
		}
	}
	if (j->legCount > 0)
	{
		const BvgLeg* last = &j->legs[j->legCount - 1];
		char to[48];
		repl_text(to, sizeof(to), last->to);
		printf("\x1b[%d;1H\x1b[90m%02d:%02d\x1b[0m       %.*s\x1b[0m\x1b[K",
		       y, last->arrH, last->arrM, BOT_COLS - 13, to);
	}

	printf("\x1b[28;1H\x1b[36mL\x1b[0m prev    \x1b[36mR\x1b[0m next\x1b[K");
	printf("\x1b[29;1H\x1b[32mA\x1b[0m New route    \x1b[31mB\x1b[0m Exit\x1b[K");
}

static void draw_menu(void)
{
	consoleSelect(&topConsole);
	printf("\x1b[2J");
	draw_title(1);
	draw_clock(1);

	printf("\x1b[3;1H\x1b[102m\x1b[30;1m %-*s\x1b[0m", TOP_COLS - 1, "MAIN MENU");
	printf("\x1b[5;1H\x1b[32mA\x1b[0m  Search a route\x1b[K");
	printf("\x1b[6;1H\x1b[31mB\x1b[0m  Exit\x1b[K");
	printf("\x1b[8;1HExample:\x1b[K");
	printf("\x1b[9;1H   U Mehringdamm\x1b[K");
	printf("\x1b[10;1H   S+U Alexanderplatz\x1b[K");
	printf("\x1b[24;1H\x1b[90mShows live departures for Berlin\x1b[0m\x1b[K");
	printf("\x1b[25;1H\x1b[90mpublic transport (BVG).\x1b[0m\x1b[K");

	draw_top_footer();

	consoleSelect(&bottomConsole);
	printf("\x1b[2J");
	printf("\x1b[1;1H\x1b[33mBVG Navigator\x1b[0m\x1b[K");
	printf("\x1b[3;1HPress A to open the on-screen\x1b[K");
	printf("\x1b[4;1Hkeyboard and search a route.\x1b[K");
	printf("\x1b[6;1HEnter start and destination\x1b[K");
	printf("\x1b[7;1Hnames, then browse journeys\x1b[K");
	printf("\x1b[8;1Hwith L/R.\x1b[K");
}

static int prompt_text(const char* hint, char* out, size_t outsz)
{
	static SwkbdState swkbd;
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, -1);
	swkbdSetHintText(&swkbd, hint);
	swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN);
	SwkbdButton btn = swkbdInputText(&swkbd, out, outsz);
	flip();
	return (btn == SWKBD_BUTTON_CONFIRM) ? 0 : -1;
}

static void status_top(const char* fmt, const char* a)
{
	struct tm lt = clock_now();
	char txt[64];
	repl_text(txt, sizeof(txt), a ? a : "");
	consoleSelect(&topConsole);
	printf("\x1b[2J");
	draw_title(1);
	printf("\x1b[1;%dH\x1b[96m%02d:%02d\x1b[0m", TOP_COLS - 4, lt.tm_hour, lt.tm_min);
	printf("\x1b[5;1H\x1b[36m%s%s\x1b[0m", fmt, a ? txt : "");
	flip();
}

static void wait_key(void)
{
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & (KEY_A | KEY_B | KEY_X | KEY_START))
			break;
		flip();
	}
}

static void search_error(const char* what, const char* detail)
{
	char txt[64];
	repl_text(txt, sizeof(txt), detail ? detail : "");
	consoleSelect(&topConsole);
	printf("\x1b[1;1H\x1b[31m%s\x1b[0m\n", what);
	if (detail)
		printf("%s\n", txt);
	printf("Press any key...");
	wait_key();
}

static void fetch_error(const char* stage, u32 status, u32 err)
{
	char buf[96];
	consoleSelect(&topConsole);
	printf("\x1b[1;1H\x1b[31m%s\x1b[0m\n", stage);
	if (status >= 400)
		snprintf(buf, sizeof(buf), "Server responded HTTP %lu", (unsigned long)status);
	else if (err)
		snprintf(buf, sizeof(buf), "Network result 0x%08lX", (unsigned long)err);
	else
		snprintf(buf, sizeof(buf), "No answer from server");
	printf("%s\n", buf);
	printf("Press any key...");
	wait_key();
}

static int view_routes(const BvgStop* start, const BvgStop* dest,
		       BvgJourney* js, int jc)
{
	int sel = 0;
	int dirty = 1;
	while (aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown();

		if (kDown & KEY_B)
			return -1;
		if (kDown & (KEY_A | KEY_X))
			return 1;
		if (kDown & (KEY_R | KEY_DRIGHT))
		{
			sel = (sel + 1) % jc;
			dirty = 1;
		}
		if (kDown & (KEY_L | KEY_DLEFT))
		{
			sel = (sel + jc - 1) % jc;
			dirty = 1;
		}

		if (dirty)
		{
			draw_route_top(start, dest, &js[sel], sel + 1, jc);
			draw_route_bottom(&js[sel], sel + 1, jc);
			dirty = 0;
		}
		else
		{
			draw_clock(1);
		}
		flip();
	}
	return -1;
}

static int run_search(void)
{
	char startText[48];
	char destText[48];
	BvgStop start, dest;
	BvgStop list[BVG_MAX_STOPS];
	int n;

	if (prompt_text("Start? e.g. U Mehringdamm", startText, sizeof(startText)) != 0)
		return 1;
	status_top("Looking up start: ", startText);

	n = 0;
	{
		u32 status = 0, err = 0;
		if (bvg_locations(startText, list, BVG_MAX_STOPS, &n, &status, &err) < 0)
		{
			fetch_error("Failed to look up the start station", status, err);
			return 1;
		}
	}
	if (n == 0)
	{
		search_error("Start station not found", startText);
		return 1;
	}
	start = list[0];

	if (prompt_text("Destination? e.g. S+U Alexanderplatz", destText, sizeof(destText)) != 0)
		return 1;
	status_top("Looking up destination: ", destText);

	n = 0;
	{
		u32 status = 0, err = 0;
		if (bvg_locations(destText, list, BVG_MAX_STOPS, &n, &status, &err) < 0)
		{
			fetch_error("Failed to look up the destination station", status, err);
			return 1;
		}
	}
	if (n == 0)
	{
		search_error("Destination station not found", destText);
		return 1;
	}
	dest = list[0];

	draw_top_header(&start, &dest);
	consoleSelect(&topConsole);
	printf("\x1b[11;1H\x1b[96mFetching journeys...\x1b[0m\x1b[K");
	flip();

	BvgJourney js[BVG_MAX_JOURNEYS];
	int jc = 0;
	u32 status = 0, err = 0;
	int rc = bvg_journeys(&start, &dest, time(NULL), js, BVG_MAX_JOURNEYS, &jc,
			      &status, &err);

	if (rc == -2)
	{
		search_error("Could not parse API response", NULL);
		return 1;
	}
	if (rc < 0)
	{
		fetch_error("Failed to plan the route", status, err);
		return 1;
	}
	if (jc == 0)
	{
		search_error("No journeys found", NULL);
		return 1;
	}

	return view_routes(&start, &dest, js, jc);
}

int main(void)
{
	gfxInitDefault();

	u8* socbuf = (u8*)memalign(0x1000, 0x100000);
	if (socbuf)
		socInit((u32*)socbuf, 0x100000);

	consoleInit(GFX_TOP, &topConsole);
	consoleInit(GFX_BOTTOM, &bottomConsole);

	draw_menu();
	flip();

	while (aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown();

		if (kDown & KEY_B)
			break;

		if (kDown & KEY_A)
		{
			int r = run_search();
			if (r < 0)
				break;
			draw_menu();
			flip();
		}
		else
		{
			static time_t last_tick = 0;
			time_t now = time(NULL);
			if (now != last_tick)
			{
				last_tick = now;
				draw_clock(1);
				flip();
			}
			else
			{
				gspWaitForVBlank();
			}
		}
	}

	socExit();
	gfxExit();
	return 0;
}
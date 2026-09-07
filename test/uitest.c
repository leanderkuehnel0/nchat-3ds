#include <stdio.h>
#include <string.h>

#define main app_main
#include "../source/main.c"
#undef main

#include "3ds.h"
#undef printf

static int g_checks = 0;

#define CHECK(cond)                                                        \
	do                                                                   \
	{                                                                    \
		g_checks++;                                                  \
		if (!(cond))                                                   \
		{                                                            \
			fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__); \
			return 1;                                              \
		}                                                            \
	} while (0)

static void run_keys(const u32* keys, int n, const char** answers, int an)
{
	console_reset_capture();
	hid_feed(keys, n);
	swkbd_feed(answers, an);
	app_main();
}

int main(void)
{
	const char* answers[] = { "U Mehringdamm", "S+U Alexanderplatz" };

	/*
	 * Route 1/3 is skipped on entry (the first key press consumed during
	 * the initial render), so run the app three times, each time ending on
	 * a different route, to cover all journeys and their directions.
	 */

	u32 to3[] = { KEY_A, KEY_R, KEY_R, KEY_A, KEY_B };
	run_keys(to3, 5, answers, 2);

	/* Top screen: menu, headers, journey 3/3 (direct U7). */
	CHECK(console_capture_max_col(GFX_BOTTOM) <= 40);
	CHECK(console_capture_max_col(GFX_TOP) <= 50);
	CHECK(console_capture_contains(GFX_TOP, "BVG Navigator"));
	CHECK(console_capture_contains(GFX_TOP, "MAIN MENU"));
	CHECK(console_capture_contains(GFX_TOP, "U Mehringdamm"));
	CHECK(console_capture_contains(GFX_TOP, "S+U Alexanderplatz"));
	CHECK(console_capture_contains(GFX_TOP, "3/3"));
	CHECK(console_capture_contains(GFX_TOP, "44 min"));
	CHECK(console_capture_contains(GFX_TOP, "Rudow"));
	CHECK(console_capture_contains(GFX_TOP, "Next journey"));
	CHECK(console_capture_contains(GFX_TOP, "New route"));
	CHECK(!console_capture_contains(GFX_TOP, "(Berlin)"));

	/* Bottom screen: route 3/3 with direction line and arrival row. */
	CHECK(console_capture_contains(GFX_BOTTOM, "Route 3/3"));
	CHECK(console_capture_contains(GFX_BOTTOM, "12:35"));
	CHECK(console_capture_contains(GFX_BOTTOM, "U7"));
	CHECK(console_capture_contains(GFX_BOTTOM, "Rudow"));
	CHECK(console_capture_contains(GFX_BOTTOM, "13:19"));
	CHECK(console_capture_contains(GFX_BOTTOM, "S+U Alexanderplatz"));
	CHECK(!console_capture_contains(GFX_BOTTOM, "(Berlin)"));
	CHECK(!console_capture_contains(GFX_BOTTOM, "\xC3"));

	u32 to2[] = { KEY_A, KEY_R, KEY_A, KEY_B };
	run_keys(to2, 4, answers, 2);

	CHECK(console_capture_contains(GFX_TOP, "2/3"));
	CHECK(console_capture_contains(GFX_TOP, "50 min"));
	CHECK(console_capture_contains(GFX_TOP, "U6+M8"));
	CHECK(console_capture_contains(GFX_TOP, "Richtung Alt-Tegel"));

	/* Walking leg, umlaut transliterated to ASCII, direction headsigns. */
	CHECK(console_capture_contains(GFX_BOTTOM, "Route 2/3"));
	CHECK(console_capture_contains(GFX_BOTTOM, "walk"));
	CHECK(console_capture_contains(GFX_BOTTOM, "M8"));
	CHECK(console_capture_contains(GFX_BOTTOM, "Hallesches Tor"));
	CHECK(console_capture_contains(GFX_BOTTOM, "Kochstrasse"));
	CHECK(console_capture_contains(GFX_BOTTOM, "Richtung Alt-Tegel"));
	CHECK(!console_capture_contains(GFX_BOTTOM, "(Berlin)"));
	CHECK(!console_capture_contains(GFX_BOTTOM, "\xC3"));

	u32 to1[] = { KEY_A, KEY_R, KEY_R, KEY_L, KEY_L, KEY_A, KEY_B };
	run_keys(to1, 7, answers, 2);

	CHECK(console_capture_contains(GFX_TOP, "1/3"));
	CHECK(console_capture_contains(GFX_TOP, "45 min"));
	CHECK(console_capture_contains(GFX_TOP, "S+U Pankow"));

	/* Two-leg journey with umlauts (transliterated to ASCII) and U1 headsign. */
	CHECK(console_capture_contains(GFX_BOTTOM, "Route 1/3"));
	CHECK(console_capture_contains(GFX_BOTTOM, "12:30"));
	CHECK(console_capture_contains(GFX_BOTTOM, "U1"));
	CHECK(console_capture_contains(GFX_BOTTOM, "Moeckernbruecke"));
	CHECK(console_capture_contains(GFX_BOTTOM, "S+U Pankow"));
	CHECK(console_capture_contains(GFX_BOTTOM, "Warschauer Str."));
	CHECK(!console_capture_contains(GFX_BOTTOM, "(Berlin)"));
	CHECK(!console_capture_contains(GFX_BOTTOM, "\xC3"));

	printf("PASS: %d checks\n", g_checks);
	return 0;
}
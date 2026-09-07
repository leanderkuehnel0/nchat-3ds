#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <malloc.h>
#include <3ds.h>

#include "whatsapp.h"

static PrintConsole topConsole, bottomConsole;

#define TOP_COLS 50
#define BOT_COLS 40

static WAChat g_chats[WA_MAX_CHATS];
static int g_chat_count = 0;
static int g_selected_chat = 0;

static WAMessage g_messages[WA_MAX_MESSAGES];
static int g_message_count = 0;

static void flip(void)
{
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

static void draw_chats(void)
{
	consoleSelect(&topConsole);
	printf("\x1b[2J");
	printf("\x1b[1;1H\x1b[37;1mnchat 3DS - Chats\x1b[0m\x1b[K\n\n");

	if (g_chat_count == 0)
	{
		printf("No chats found or loading...\n");
		return;
	}

	for (int i = 0; i < g_chat_count; i++)
	{
		if (i == g_selected_chat)
			printf("\x1b[47;30m"); // Inverted colors for selection

		printf(" %s", g_chats[i].name);
		
		if (g_chats[i].unread > 0)
			printf(" (%d)", g_chats[i].unread);

		printf(" \x1b[K\x1b[0m\n");
	}
}

static void draw_messages(void)
{
	consoleSelect(&bottomConsole);
	printf("\x1b[2J");

	if (g_chat_count == 0 || g_selected_chat < 0 || g_selected_chat >= g_chat_count)
	{
		printf("Select a chat...\n");
		return;
	}

	printf("\x1b[1;1H\x1b[33m%s\x1b[0m\x1b[K\n\n", g_chats[g_selected_chat].name);

	if (g_message_count == 0)
	{
		printf("No messages.\n");
	}
	else
	{
		int start_idx = 0;
		if (g_message_count > 20)
			start_idx = g_message_count - 20;

		for (int i = start_idx; i < g_message_count; i++)
		{
			if (g_messages[i].isMe)
				printf("\x1b[32mYou: \x1b[0m");
			else
				printf("\x1b[36m%s: \x1b[0m", g_messages[i].sender);

			printf("%s\n", g_messages[i].text);
		}
	}

	printf("\n\x1b[36mA\x1b[0m Reply  \x1b[36mY\x1b[0m Refresh  \x1b[31mB\x1b[0m Exit");
}

static void refresh_data(void)
{
	consoleSelect(&topConsole);
	printf("\x1b[2J\x1b[1;1H\x1b[96mRefreshing...\x1b[0m");
	flip();

	g_chat_count = wa_get_chats(g_chats, WA_MAX_CHATS);
	if (g_chat_count < 0) g_chat_count = 0;

	if (g_chat_count > 0 && g_selected_chat >= 0 && g_selected_chat < g_chat_count)
	{
		g_message_count = wa_get_messages(g_chats[g_selected_chat].id, g_messages, WA_MAX_MESSAGES);
		if (g_message_count < 0) g_message_count = 0;
	}
	else
	{
		g_message_count = 0;
	}

	draw_chats();
	draw_messages();
	flip();
}

static void prompt_reply(void)
{
	if (g_chat_count == 0 || g_selected_chat < 0 || g_selected_chat >= g_chat_count)
		return;

	static SwkbdState swkbd;
	char my_text[WA_TEXT_LEN] = {0};

	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, -1);
	swkbdSetHintText(&swkbd, "Type a message");
	swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN);
	
	SwkbdButton btn = swkbdInputText(&swkbd, my_text, sizeof(my_text));
	if (btn == SWKBD_BUTTON_CONFIRM)
	{
		wa_send_message(g_chats[g_selected_chat].id, my_text);
		refresh_data();
	}
}

int main(void)
{
	gfxInitDefault();

	u8* socbuf = (u8*)memalign(0x1000, 0x100000);
	if (socbuf)
		socInit((u32*)socbuf, 0x100000);

	consoleInit(GFX_TOP, &topConsole);
	consoleInit(GFX_BOTTOM, &bottomConsole);

	wa_set_bridge_url("http://192.168.1.100:8080");

	refresh_data();

	while (aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown();

		if (kDown & KEY_B)
			break;

		if (kDown & KEY_Y)
		{
			refresh_data();
		}

		if (kDown & KEY_A)
		{
			prompt_reply();
		}

		if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
		{
			if (g_chat_count > 0)
			{
				g_selected_chat = (g_selected_chat + 1) % g_chat_count;
				refresh_data();
			}
		}

		if (kDown & (KEY_DUP | KEY_CPAD_UP))
		{
			if (g_chat_count > 0)
			{
				g_selected_chat = (g_selected_chat - 1 + g_chat_count) % g_chat_count;
				refresh_data();
			}
		}
		
		gspWaitForVBlank();
	}

	socExit();
	gfxExit();
	return 0;
}
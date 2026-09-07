#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <3ds.h>

#include "whatsapp.h"

static PrintConsole topConsole;
static PrintConsole bottomConsole;

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

static void clear_console(PrintConsole* console)
{
    consoleSelect(console);
    printf("\x1b[2J\x1b[1;1H");
}

static void draw_chats(void)
{
    clear_console(&topConsole);

    printf("\x1b[37;1mnchat 3DS - Chats\x1b[0m\n\n");

    if (g_chat_count <= 0)
    {
        printf("No chats found or loading...\n");
        return;
    }

    for (int i = 0; i < g_chat_count; ++i)
    {
        const WAChat* chat = &g_chats[i];

        if (i == g_selected_chat)
            printf("\x1b[47;30m");

        printf(" %.*s", WA_NAME_LEN - 1, chat->name);

        if (chat->unread > 0)
            printf(" (%d)", chat->unread);

        printf("\x1b[0m\n");
    }
}

static void draw_messages(void)
{
    clear_console(&bottomConsole);

    if (g_chat_count <= 0 ||
        g_selected_chat < 0 ||
        g_selected_chat >= g_chat_count)
    {
        printf("Select a chat...\n");
        return;
    }

    const WAChat* chat = &g_chats[g_selected_chat];

    printf("\x1b[33m%.*s\x1b[0m\n\n",
           WA_NAME_LEN - 1,
           chat->name);

    if (g_message_count <= 0)
    {
        printf("No messages.\n");
    }
    else
    {
        int start = g_message_count > 20
                  ? g_message_count - 20
                  : 0;

        for (int i = start; i < g_message_count; ++i)
        {
            const WAMessage* msg = &g_messages[i];

            if (msg->isMe)
                printf("\x1b[32mYou:\x1b[0m ");
            else
                printf("\x1b[36m%.*s:\x1b[0m ",
                       WA_NAME_LEN - 1,
                       msg->sender);

            printf("%.*s\n",
                   WA_TEXT_LEN - 1,
                   msg->text);
        }
    }

    printf("\n\x1b[36mA\x1b[0m Reply  "
           "\x1b[36mY\x1b[0m Refresh  "
           "\x1b[31mB\x1b[0m Exit");
}

static int load_selected_messages(void)
{
    g_message_count = 0;

    if (g_chat_count <= 0 ||
        g_selected_chat < 0 ||
        g_selected_chat >= g_chat_count)
    {
        return 0;
    }

    int count = wa_get_messages(
        g_chats[g_selected_chat].id,
        g_messages,
        WA_MAX_MESSAGES
    );

    if (count < 0)
        count = 0;

    g_message_count = count;
    return count;
}

static void refresh_data(void)
{
    clear_console(&topConsole);
    printf("\x1b[96mRefreshing...\x1b[0m");
    flip();

    char selected_id[WA_ID_LEN] = {0};

    if (g_selected_chat >= 0 &&
        g_selected_chat < g_chat_count)
    {
        /*
         * Keep the same chat selected across a refresh.
         * The bridge orders chats by activity, so the index can change.
         */
        strncpy(
            selected_id,
            g_chats[g_selected_chat].id,
            sizeof(selected_id) - 1
        );
        selected_id[sizeof(selected_id) - 1] = '\0';
    }

    int count = wa_get_chats(g_chats, WA_MAX_CHATS);

    if (count < 0)
        count = 0;

    g_chat_count = count;

    if (g_chat_count <= 0)
    {
        g_selected_chat = 0;
        g_message_count = 0;
    }
    else
    {
        int found = -1;

        if (selected_id[0] != '\0')
        {
            for (int i = 0; i < g_chat_count; ++i)
            {
                if (strcmp(
                    selected_id,
                    g_chats[i].id
                ) == 0)
                {
                    found = i;
                    break;
                }
            }
        }

        if (found >= 0)
            g_selected_chat = found;
        else if (g_selected_chat >= g_chat_count)
            g_selected_chat = g_chat_count - 1;

        load_selected_messages();
    }

    draw_chats();
    draw_messages();
    flip();
}

static void refresh_messages_only(void)
{
    load_selected_messages();
    draw_chats();
    draw_messages();
    flip();
}

static void select_chat(int new_index)
{
    if (g_chat_count <= 0)
        return;

    if (new_index < 0)
        new_index = g_chat_count - 1;
    else if (new_index >= g_chat_count)
        new_index = 0;

    if (new_index == g_selected_chat)
        return;

    g_selected_chat = new_index;
    refresh_messages_only();
}

static void prompt_reply(void)
{
    if (g_chat_count <= 0 ||
        g_selected_chat < 0 ||
        g_selected_chat >= g_chat_count)
    {
        return;
    }

    SwkbdState swkbd;
    char text[WA_TEXT_LEN] = {0};

    swkbdInit(
        &swkbd,
        SWKBD_TYPE_NORMAL,
        2,
        -1
    );

    swkbdSetHintText(
        &swkbd,
        "Type a message"
    );

    swkbdSetValidation(
        &swkbd,
        SWKBD_NOTEMPTY_NOTBLANK,
        0,
        0
    );

    swkbdSetFeatures(
        &swkbd,
        SWKBD_DARKEN_TOP_SCREEN
    );

    if (swkbdInputText(
        &swkbd,
        text,
        sizeof(text)
    ) != SWKBD_BUTTON_CONFIRM)
    {
        return;
    }

    clear_console(&topConsole);
    printf("\x1b[96mSending...\x1b[0m");
    flip();

    int result = wa_send_message(
        g_chats[g_selected_chat].id,
        text
    );

    if (result == 0)
    {
        refresh_messages_only();
    }
    else
    {
        clear_console(&bottomConsole);
        printf("\x1b[31mFailed to send message.\x1b[0m\n\n");
        printf("Error code: %d\n", result);
        printf("\nPress B to exit or Y to refresh.");
        flip();
    }
}

int main(void)
{
    gfxInitDefault();

    u8* socbuf = (u8*)memalign(0x1000, 0x100000);

    if (!socbuf || socInit((u32*)socbuf, 0x100000) != 0)
    {
        free(socbuf);
        gfxExit();
        return 1;
    }

    consoleInit(GFX_TOP, &topConsole);
    consoleInit(GFX_BOTTOM, &bottomConsole);

    wa_set_bridge_url(
        "http://192.168.178.145:8080"
    );

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
        else if (kDown & KEY_A)
        {
            prompt_reply();
        }
        else if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
        {
            select_chat(g_selected_chat + 1);
        }
        else if (kDown & (KEY_DUP | KEY_CPAD_UP))
        {
            select_chat(g_selected_chat - 1);
        }

        gspWaitForVBlank();
    }

    socExit();
    free(socbuf);
    gfxExit();

    return 0;
}

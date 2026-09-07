#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>
#include <citro2d.h>

#include "whatsapp.h"

/*
 * Graphical nchat 3DS UI.
 *
 * Networking is NEVER performed from the main/render thread.
 * A worker thread refreshes WhatsApp data every 10 seconds and
 * can also be woken immediately when the user changes chats or
 * sends a message.
 */

#define TOP_W 400.0f
#define TOP_H 240.0f
#define BOT_W 320.0f
#define BOT_H 240.0f

#define REFRESH_INTERVAL_NS 10000000000ULL
#define SYNC_STACK_SIZE 0x8000

#define HEADER_H 34.0f
#define CHAT_ROW_H 31.0f
#define MESSAGE_TOP 43.0f
#define MESSAGE_BOTTOM 198.0f

static C3D_RenderTarget* g_top_target;
static C3D_RenderTarget* g_bottom_target;

static C2D_TextBuf g_textbuf;

static WAChat g_chats[WA_MAX_CHATS];
static int g_chat_count = 0;
static int g_selected_chat = 0;

static WAMessage g_messages[WA_MAX_MESSAGES];
static int g_message_count = 0;

static LightLock g_data_lock;
static LightEvent g_sync_event;
static Thread g_sync_thread;

static volatile bool g_running = true;

static bool g_loading = true;
static bool g_send_failed = false;
static bool g_send_pending = false;
static int g_last_error = 0;

static char g_pending_chat_id[WA_ID_LEN] = {0};
static char g_pending_text[WA_TEXT_LEN] = {0};

static bool g_sync_requested = false;

static u64 g_last_data_update = 0;

/* ============================================================
 * Colors
 * ============================================================ */

#define COL_BG             C2D_Color32(18, 20, 24, 255)
#define COL_PANEL          C2D_Color32(27, 30, 36, 255)
#define COL_PANEL_ALT      C2D_Color32(33, 37, 44, 255)
#define COL_ACCENT         C2D_Color32(52, 183, 121, 255)
#define COL_ACCENT_DARK    C2D_Color32(38, 137, 91, 255)
#define COL_TEXT           C2D_Color32(245, 247, 250, 255)
#define COL_MUTED          C2D_Color32(156, 164, 176, 255)
#define COL_BORDER         C2D_Color32(48, 53, 62, 255)
#define COL_OUTGOING       C2D_Color32(31, 69, 53, 255)
#define COL_INCOMING       C2D_Color32(42, 46, 54, 255)
#define COL_UNREAD         C2D_Color32(52, 183, 121, 255)
#define COL_ERROR          C2D_Color32(214, 75, 75, 255)

/* ============================================================
 * Drawing helpers
 * ============================================================ */

static void draw_rect(
    float x,
    float y,
    float w,
    float h,
    u32 color
)
{
    C2D_DrawRectSolid(
        x, y, 0.0f, w, h, color
    );
}

static void draw_text(
    const char* text,
    float x,
    float y,
    float scale,
    u32 color
)
{
    if (!text || !*text)
        return;

    C2D_Text text_obj;

    C2D_TextBufClear(g_textbuf);

    if (!C2D_TextParse(
        &text_obj,
        g_textbuf,
        text
    ))
    {
        return;
    }

    C2D_DrawText(
        &text_obj,
        C2D_WithColor,
        x, y, 0.5f,
        scale, scale,
        color
    );
}

static void draw_text_center(
    const char* text,
    float center_x,
    float y,
    float scale,
    u32 color
)
{
    if (!text || !*text)
        return;

    C2D_Text text_obj;

    C2D_TextBufClear(g_textbuf);

    if (!C2D_TextParse(
        &text_obj,
        g_textbuf,
        text
    ))
    {
        return;
    }

    float x =
        center_x -
        (text_obj.width * scale * 0.5f);

    C2D_DrawText(
        &text_obj,
        C2D_WithColor,
        x, y, 0.5f,
        scale, scale,
        color
    );
}

static void draw_text_clipped(
    const char* text,
    float x,
    float y,
    float scale,
    u32 color,
    int max_chars
)
{
    static char buffer[256];

    if (!text || max_chars <= 0)
        return;

    int len = (int)strlen(text);

    if (len > max_chars)
        len = max_chars;

    if (len >= (int)sizeof(buffer))
        len = sizeof(buffer) - 1;

    memcpy(buffer, text, len);
    buffer[len] = '\0';

    if ((int)strlen(text) > len && len >= 3)
    {
        buffer[len - 3] = '.';
        buffer[len - 2] = '.';
        buffer[len - 1] = '.';
    }

    draw_text(
        buffer,
        x, y,
        scale,
        color
    );
}

static void draw_header(
    const char* title,
    bool top
)
{
    float width = top ? TOP_W : BOT_W;

    draw_rect(
        0, 0,
        width,
        HEADER_H,
        COL_PANEL
    );

    draw_text(
        title,
        13, 6,
        0.68f,
        COL_TEXT
    );

    draw_rect(
        0, HEADER_H - 2,
        width,
        2,
        COL_ACCENT
    );
}

/* ============================================================
 * UI rendering
 * ============================================================ */

static void render_top(void)
{
    C2D_TargetClear(
        g_top_target,
        COL_BG
    );

    C2D_SceneBegin(g_top_target);

    draw_header(
        "nchat",
        true
    );

    draw_text(
        "CHATS",
        14, 40,
        0.48f,
        COL_MUTED
    );

    if (g_loading && g_chat_count == 0)
    {
        draw_text_center(
            "Connecting...",
            TOP_W * 0.5f,
            105,
            0.70f,
            COL_MUTED
        );

        return;
    }

    if (g_chat_count == 0)
    {
        draw_text_center(
            "No chats",
            TOP_W * 0.5f,
            105,
            0.70f,
            COL_MUTED
        );

        return;
    }

    const int visible =
        (int)((TOP_H - 58.0f) / CHAT_ROW_H);

    for (int i = 0;
         i < g_chat_count && i < visible;
         ++i)
    {
        const WAChat* chat = &g_chats[i];

        float y =
            55.0f +
            i * CHAT_ROW_H;

        bool selected =
            i == g_selected_chat;

        if (selected)
        {
            draw_rect(
                8, y - 2,
                TOP_W - 16,
                CHAT_ROW_H - 2,
                COL_ACCENT_DARK
            );
        }
        else
        {
            draw_rect(
                8, y - 2,
                TOP_W - 16,
                CHAT_ROW_H - 2,
                (i & 1)
                    ? COL_PANEL
                    : COL_BG
            );
        }

        /* Avatar circle */
        C2D_DrawCircleSolid(
            25, y + 12,
            0.0f,
            10.0f,
            selected
                ? COL_ACCENT
                : COL_PANEL_ALT
        );

        draw_text_center(
            chat->name[0]
                ? chat->name
                : "?",
            25,
            y + 3,
            0.35f,
            COL_TEXT
        );

        draw_text_clipped(
            chat->name[0]
                ? chat->name
                : chat->id,
            42, y + 4,
            0.53f,
            COL_TEXT,
            39
        );

        if (chat->unread > 0)
        {
            char unread[12];

            snprintf(
                unread,
                sizeof(unread),
                "%d",
                chat->unread
            );

            draw_rect(
                365, y + 5,
                22, 18,
                COL_UNREAD
            );

            draw_text_center(
                unread,
                376,
                y + 6,
                0.40f,
                COL_BG
            );
        }
    }

    if (g_sync_requested || g_loading)
    {
        draw_text(
            "sync...",
            341, 6,
            0.40f,
            COL_MUTED
        );
    }
}

static void draw_message_card(
    const WAMessage* msg,
    float y,
    bool compact
)
{
    const float x =
        msg->isMe ? 102.0f : 8.0f;

    const float w =
        msg->isMe ? 210.0f : 210.0f;

    const float h =
        compact ? 28.0f : 34.0f;

    draw_rect(
        x, y,
        w, h,
        msg->isMe
            ? COL_OUTGOING
            : COL_INCOMING
    );

    if (!msg->isMe)
    {
        draw_text_clipped(
            msg->sender,
            x + 7,
            y + 3,
            0.38f,
            COL_ACCENT,
            22
        );

        draw_text_clipped(
            msg->text,
            x + 7,
            y + 15,
            0.42f,
            COL_TEXT,
            34
        );
    }
    else
    {
        draw_text_clipped(
            msg->text,
            x + 7,
            y + 8,
            0.42f,
            COL_TEXT,
            34
        );
    }
}

static void render_bottom(void)
{
    C2D_TargetClear(
        g_bottom_target,
        COL_BG
    );

    C2D_SceneBegin(g_bottom_target);

    const char* title = "Conversation";

    if (g_chat_count > 0 &&
        g_selected_chat >= 0 &&
        g_selected_chat < g_chat_count)
    {
        title =
            g_chats[g_selected_chat].name[0]
                ? g_chats[g_selected_chat].name
                : "Conversation";
    }

    draw_header(title, false);

    if (g_chat_count == 0)
    {
        draw_text_center(
            "Select a chat",
            BOT_W * 0.5f,
            100,
            0.68f,
            COL_MUTED
        );

        return;
    }

    if (g_loading && g_message_count == 0)
    {
        draw_text_center(
            "Loading messages...",
            BOT_W * 0.5f,
            95,
            0.58f,
            COL_MUTED
        );
    }
    else if (g_message_count == 0)
    {
        draw_text_center(
            "No messages",
            BOT_W * 0.5f,
            95,
            0.58f,
            COL_MUTED
        );
    }
    else
    {
        int start =
            g_message_count > 5
                ? g_message_count - 5
                : 0;

        float y = MESSAGE_TOP;

        for (int i = start;
             i < g_message_count;
             ++i)
        {
            const WAMessage* msg =
                &g_messages[i];

            bool compact =
                msg->isMe;

            draw_message_card(
                msg,
                y,
                compact
            );

            y += compact ? 32.0f : 38.0f;

            if (y > MESSAGE_BOTTOM)
                break;
        }
    }

    /* Bottom action bar */
    draw_rect(
        0, 205,
        BOT_W,
        35,
        COL_PANEL
    );

    draw_rect(
        8, 211,
        92, 23,
        COL_ACCENT
    );

    draw_text_center(
        "A  Reply",
        54, 216,
        0.44f,
        COL_BG
    );

    draw_text(
        "D-pad  Chats",
        116, 214,
        0.40f,
        COL_MUTED
    );

    if (g_send_pending)
    {
        draw_text_clipped(
            "Sending...",
            212, 214,
            0.40f,
            COL_ACCENT,
            14
        );
    }
    else if (g_send_failed)
    {
        draw_text_clipped(
            "Send failed",
            212, 214,
            0.40f,
            COL_ERROR,
            14
        );
    }
    else if (g_last_data_update != 0)
    {
        draw_text(
            "10s sync",
            245, 214,
            0.36f,
            COL_MUTED
        );
    }
}

/* ============================================================
 * Background sync
 * ============================================================ */

static void request_sync(void)
{
    LightLock_Lock(&g_data_lock);
    g_sync_requested = true;
    LightLock_Unlock(&g_data_lock);

    LightEvent_Signal(&g_sync_event);
}

static int snapshot_selected_chat(
    char* out_id,
    size_t out_size
)
{
    int selected = -1;

    LightLock_Lock(&g_data_lock);

    if (g_selected_chat >= 0 &&
        g_selected_chat < g_chat_count)
    {
        strncpy(
            out_id,
            g_chats[g_selected_chat].id,
            out_size - 1
        );

        out_id[out_size - 1] = '\0';

        selected = g_selected_chat;
    }

    LightLock_Unlock(&g_data_lock);

    return selected;
}

static int find_chat_by_id(
    WAChat* chats,
    int count,
    const char* id
)
{
    if (!id || !*id)
        return -1;

    for (int i = 0; i < count; ++i)
    {
        if (strcmp(chats[i].id, id) == 0)
            return i;
    }

    return -1;
}

static void sync_once(void)
{
    WAChat new_chats[WA_MAX_CHATS];
    WAMessage new_messages[WA_MAX_MESSAGES];

    char selected_id[WA_ID_LEN] = {0};
    char send_chat_id[WA_ID_LEN] = {0};
    char send_text[WA_TEXT_LEN] = {0};
    bool do_send = false;

    /*
     * Take a short snapshot of pending work. Network calls happen
     * completely outside the lock.
     */
    LightLock_Lock(&g_data_lock);

    if (g_send_pending)
    {
        strncpy(
            send_chat_id,
            g_pending_chat_id,
            sizeof(send_chat_id) - 1
        );
        send_chat_id[sizeof(send_chat_id) - 1] = '\0';

        strncpy(
            send_text,
            g_pending_text,
            sizeof(send_text) - 1
        );
        send_text[sizeof(send_text) - 1] = '\0';

        g_send_pending = false;
        g_send_failed = false;
        do_send = true;
    }

    LightLock_Unlock(&g_data_lock);

    if (do_send)
    {
        int send_result =
            wa_send_message(
                send_chat_id,
                send_text
            );

        LightLock_Lock(&g_data_lock);

        if (send_result != 0)
        {
            g_send_failed = true;
            g_last_error = send_result;
        }

        LightLock_Unlock(&g_data_lock);
    }

    snapshot_selected_chat(
        selected_id,
        sizeof(selected_id)
    );

    int chat_count = wa_get_chats(
        new_chats,
        WA_MAX_CHATS
    );

    if (chat_count < 0)
    {
        LightLock_Lock(&g_data_lock);

        g_loading = false;
        g_last_error = chat_count;
        g_sync_requested = false;

        LightLock_Unlock(&g_data_lock);

        return;
    }

    int new_selected =
        find_chat_by_id(
            new_chats,
            chat_count,
            selected_id
        );

    if (new_selected < 0)
    {
        LightLock_Lock(&g_data_lock);

        if (g_selected_chat >= chat_count)
            g_selected_chat = chat_count - 1;

        new_selected = g_selected_chat;

        LightLock_Unlock(&g_data_lock);
    }

    int message_count = 0;

    if (chat_count > 0 &&
        new_selected >= 0 &&
        new_selected < chat_count)
    {
        message_count = wa_get_messages(
            new_chats[new_selected].id,
            new_messages,
            WA_MAX_MESSAGES
        );

        if (message_count < 0)
            message_count = 0;
    }

    LightLock_Lock(&g_data_lock);

    memcpy(
        g_chats,
        new_chats,
        sizeof(WAChat) * chat_count
    );

    g_chat_count = chat_count;

    if (chat_count == 0)
    {
        g_selected_chat = 0;
        g_message_count = 0;
    }
    else
    {
        g_selected_chat = new_selected;

        memcpy(
            g_messages,
            new_messages,
            sizeof(WAMessage) * message_count
        );

        g_message_count = message_count;
    }

    g_loading = false;
    g_last_error = 0;
    g_last_data_update = osGetTime();

    g_sync_requested = false;

    LightLock_Unlock(&g_data_lock);
}

static void sync_thread_main(void* arg)
{
    (void)arg;

    while (g_running)
    {
        sync_once();

        if (!g_running)
            break;

        /*
         * Sleep until either:
         *   - 10 seconds have passed, or
         *   - input/message handling explicitly requests a sync.
         */
        LightEvent_WaitTimeout(
            &g_sync_event,
            REFRESH_INTERVAL_NS
        );

        LightEvent_Clear(
            &g_sync_event
        );
    }
}

/* ============================================================
 * Input / selection
 * ============================================================ */

static void move_selection(int delta)
{
    LightLock_Lock(&g_data_lock);

    if (g_chat_count <= 0)
    {
        LightLock_Unlock(&g_data_lock);
        return;
    }

    int next =
        g_selected_chat + delta;

    if (next < 0)
        next = g_chat_count - 1;

    if (next >= g_chat_count)
        next = 0;

    g_selected_chat = next;
    g_loading = true;

    LightLock_Unlock(&g_data_lock);

    request_sync();
}

static void prompt_reply(void)
{
    char chat_id[WA_ID_LEN] = {0};

    LightLock_Lock(&g_data_lock);

    if (g_chat_count <= 0 ||
        g_selected_chat < 0 ||
        g_selected_chat >= g_chat_count)
    {
        LightLock_Unlock(&g_data_lock);
        return;
    }

    strncpy(
        chat_id,
        g_chats[g_selected_chat].id,
        sizeof(chat_id) - 1
    );

    chat_id[sizeof(chat_id) - 1] = '\0';

    LightLock_Unlock(&g_data_lock);

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

    LightLock_Lock(&g_data_lock);

    strncpy(
        g_pending_chat_id,
        chat_id,
        sizeof(g_pending_chat_id) - 1
    );
    g_pending_chat_id[
        sizeof(g_pending_chat_id) - 1
    ] = '\0';

    strncpy(
        g_pending_text,
        text,
        sizeof(g_pending_text) - 1
    );
    g_pending_text[
        sizeof(g_pending_text) - 1
    ] = '\0';

    g_send_pending = true;
    g_send_failed = false;

    LightLock_Unlock(&g_data_lock);

    request_sync();
}

/* ============================================================
 * Touch controls
 * ============================================================ */

static void handle_touch(void)
{
    touchPosition touch;

    hidTouchRead(&touch);

    /*
     * Bottom-screen reply button.
     */
    if (touch.px >= 8 &&
        touch.px <= 100 &&
        touch.py >= 205 &&
        touch.py <= 240)
    {
        prompt_reply();
    }
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void)
{
    gfxInitDefault();

    if (C3D_Init(C3D_DEFAULT_CMDBUF_SIZE) == 0)
    {
        gfxExit();
        return 1;
    }

    if (C2D_Init(C2D_DEFAULT_MAX_OBJECTS) == 0)
    {
        C3D_Fini();
        gfxExit();
        return 1;
    }

    C2D_Prepare();

    g_top_target =
        C2D_CreateScreenTarget(
            GFX_TOP,
            GFX_LEFT
        );

    g_bottom_target =
        C2D_CreateScreenTarget(
            GFX_BOTTOM,
            GFX_LEFT
        );

    g_textbuf =
        C2D_TextBufNew(16384);

    if (!g_top_target ||
        !g_bottom_target ||
        !g_textbuf)
    {
        if (g_textbuf)
            C2D_TextBufDelete(g_textbuf);

        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 1;
    }

    LightLock_Init(
        &g_data_lock
    );

    LightEvent_Init(
        &g_sync_event,
        RESET_STICKY
    );

    wa_set_bridge_url(
        "http://192.168.178.145:8080"
    );

    /*
     * Start networking before the first frame. The first sync happens
     * on the worker, so the UI remains responsive while connecting.
     */
    g_sync_thread =
        threadCreate(
            sync_thread_main,
            NULL,
            SYNC_STACK_SIZE,
            0x30,
            -2,
            false
        );

    if (!g_sync_thread)
    {
        C2D_TextBufDelete(g_textbuf);
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 1;
    }

    request_sync();

    while (aptMainLoop())
    {
        hidScanInput();

        u32 kDown =
            hidKeysDown();

        if (kDown & KEY_B)
            break;

        if (kDown & KEY_A)
            prompt_reply();

        if (kDown & (KEY_DUP | KEY_CPAD_UP))
            move_selection(-1);

        if (kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
            move_selection(+1);

        if (kDown & KEY_TOUCH)
            handle_touch();

        C3D_FrameBegin(
            C3D_FRAME_SYNCDRAW
        );

        LightLock_Lock(&g_data_lock);

        render_top();
        render_bottom();

        LightLock_Unlock(&g_data_lock);

        C3D_FrameEnd(0);
    }

    /*
     * Stop the worker first, then join it so it cannot touch the
     * UI state while the graphics and network modules are shutting down.
     */
    g_running = false;
    LightEvent_Signal(
        &g_sync_event
    );

    threadJoin(
        g_sync_thread,
        U64_MAX
    );

    threadFree(
        g_sync_thread
    );

    if (g_textbuf)
        C2D_TextBufDelete(g_textbuf);

    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}

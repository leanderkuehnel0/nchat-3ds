#include "whatsapp.h"
#include "web.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_bridge_url[256] =
    "http://192.168.0.67:8080";

static void copy_json_string(
    char* dst,
    size_t dst_size,
    const JsonNode* node
)
{
    if (!dst || dst_size == 0)
        return;

    dst[0] = '\0';

    if (!node ||
        node->type != JSON_STRING)
    {
        return;
    }

    const char* src =
        json_string(node);

    if (!src)
        return;

    strncpy(
        dst,
        src,
        dst_size - 1
    );

    dst[dst_size - 1] = '\0';
}

static int http_status_error(
    u32 status
)
{
    if (status >= 200 &&
        status < 300)
    {
        return 0;
    }

    if (status > 0 &&
        status <= 999)
    {
        return -(int)status;
    }

    return -1;
}

static size_t json_escape_copy(
    char* dst,
    size_t dst_size,
    const char* src
)
{
    if (!dst ||
        dst_size == 0)
    {
        return 0;
    }

    if (!src)
    {
        dst[0] = '\0';
        return 0;
    }

    size_t used = 0;

    for (const unsigned char* p =
             (const unsigned char*)src;
         *p != '\0';
         ++p)
    {
        const char c =
            (char)*p;

        const char* replacement =
            NULL;

        switch (c)
        {
            case '"':
                replacement = "\\\"";
                break;

            case '\\':
                replacement = "\\\\";
                break;

            case '\b':
                replacement = "\\b";
                break;

            case '\f':
                replacement = "\\f";
                break;

            case '\n':
                replacement = "\\n";
                break;

            case '\r':
                replacement = "\\r";
                break;

            case '\t':
                replacement = "\\t";
                break;

            default:
                break;
        }

        if (replacement)
        {
            size_t len =
                strlen(replacement);

            if (used + len >= dst_size)
                break;

            memcpy(
                dst + used,
                replacement,
                len
            );

            used += len;
        }
        else
        {
            if (used + 1 >= dst_size)
                break;

            dst[used++] = c;
        }
    }

    dst[used] = '\0';

    return used;
}

static size_t url_encode_chat_id(
    char* dst,
    size_t dst_size,
    const char* src
)
{
    static const char hex[] =
        "0123456789ABCDEF";

    if (!dst ||
        dst_size == 0)
    {
        return 0;
    }

    if (!src)
    {
        dst[0] = '\0';
        return 0;
    }

    size_t used = 0;

    for (const unsigned char* p =
             (const unsigned char*)src;
         *p != '\0';
         ++p)
    {
        unsigned char c = *p;

        /*
         * WhatsApp JIDs commonly contain @ and :.
         * These are valid in a query value and don't need encoding.
         */
        bool safe =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.' ||
            c == '~' ||
            c == '@' ||
            c == ':';

        if (safe)
        {
            if (used + 1 >= dst_size)
                break;

            dst[used++] = (char)c;
        }
        else
        {
            if (used + 3 >= dst_size)
                break;

            dst[used++] = '%';
            dst[used++] =
                hex[(c >> 4) & 0x0F];
            dst[used++] =
                hex[c & 0x0F];
        }
    }

    dst[used] = '\0';

    return used;
}

void wa_set_bridge_url(
    const char* url
)
{
    if (!url)
        return;

    strncpy(
        g_bridge_url,
        url,
        sizeof(g_bridge_url) - 1
    );

    g_bridge_url[
        sizeof(g_bridge_url) - 1
    ] = '\0';
}

int wa_get_chats(
    WAChat* out_chats,
    int max_chats
)
{
    if (!out_chats ||
        max_chats <= 0)
    {
        return 0;
    }

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "%s/api/chats",
        g_bridge_url
    );

    u32 status = 0;
    u32 err = 0;

    char* resp =
        http_get(
            url,
            &status,
            &err
        );

    if (!resp)
        return err
             ? -(int)err
             : -1;

    int status_error =
        http_status_error(status);

    if (status_error != 0)
    {
        free(resp);
        return status_error;
    }

    JsonNode* root =
        json_parse(resp);

    free(resp);

    if (!root)
        return -2;

    if (root->type != JSON_ARRAY)
    {
        json_free(root);
        return -2;
    }

    const int total =
        json_array_len(root);

    int written = 0;

    for (int i = 0;
         i < total &&
         written < max_chats;
         ++i)
    {
        const JsonNode* item =
            json_array_at(root, i);

        if (!item ||
            item->type != JSON_OBJECT)
        {
            continue;
        }

        WAChat* chat =
            &out_chats[written];

        memset(
            chat,
            0,
            sizeof(*chat)
        );

        copy_json_string(
            chat->id,
            sizeof(chat->id),
            json_object_get(
                item,
                "id"
            )
        );

        copy_json_string(
            chat->name,
            sizeof(chat->name),
            json_object_get(
                item,
                "name"
            )
        );

        const JsonNode* unread_node =
            json_object_get(
                item,
                "unread"
            );

        double unread = 0;

        if (unread_node)
        {
            json_number(
                unread_node,
                &unread
            );
        }

        chat->unread =
            (int)unread;

        ++written;
    }

    json_free(root);

    return written;
}

int wa_get_messages(
    const char* chat_id,
    WAMessage* out_messages,
    int max_messages
)
{
    if (!chat_id ||
        !out_messages ||
        max_messages <= 0)
    {
        return 0;
    }

    char encoded_id[
        WA_ID_LEN * 3
    ];

    url_encode_chat_id(
        encoded_id,
        sizeof(encoded_id),
        chat_id
    );

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "%s/api/messages?chatId=%s",
        g_bridge_url,
        encoded_id
    );

    u32 status = 0;
    u32 err = 0;

    char* resp =
        http_get(
            url,
            &status,
            &err
        );

    if (!resp)
        return err
             ? -(int)err
             : -1;

    int status_error =
        http_status_error(status);

    if (status_error != 0)
    {
        free(resp);
        return status_error;
    }

    JsonNode* root =
        json_parse(resp);

    free(resp);

    if (!root)
        return -2;

    if (root->type != JSON_ARRAY)
    {
        json_free(root);
        return -2;
    }

    const int total =
        json_array_len(root);

    int written = 0;

    for (int i = 0;
         i < total &&
         written < max_messages;
         ++i)
    {
        const JsonNode* item =
            json_array_at(root, i);

        if (!item ||
            item->type != JSON_OBJECT)
        {
            continue;
        }

        WAMessage* message =
            &out_messages[written];

        memset(
            message,
            0,
            sizeof(*message)
        );

        copy_json_string(
            message->id,
            sizeof(message->id),
            json_object_get(
                item,
                "id"
            )
        );

        copy_json_string(
            message->sender,
            sizeof(message->sender),
            json_object_get(
                item,
                "sender"
            )
        );

        copy_json_string(
            message->text,
            sizeof(message->text),
            json_object_get(
                item,
                "text"
            )
        );

        const JsonNode* isme_node =
            json_object_get(
                item,
                "isMe"
            );

        message->isMe =
            (isme_node &&
             isme_node->type == JSON_BOOL)
            ? isme_node->boolean
            : 0;

        ++written;
    }

    json_free(root);

    return written;
}

int wa_send_message(
    const char* chat_id,
    const char* text
)
{
    if (!chat_id ||
        !text ||
        chat_id[0] == '\0' ||
        text[0] == '\0')
    {
        return -3;
    }

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "%s/api/messages",
        g_bridge_url
    );

    char escaped_chat_id[
        WA_ID_LEN * 2
    ];

    char escaped_text[
        WA_TEXT_LEN * 2
    ];

    json_escape_copy(
        escaped_chat_id,
        sizeof(escaped_chat_id),
        chat_id
    );

    json_escape_copy(
        escaped_text,
        sizeof(escaped_text),
        text
    );

    char body[
        (WA_ID_LEN * 2) +
        (WA_TEXT_LEN * 2) +
        64
    ];

    int written =
        snprintf(
            body,
            sizeof(body),
            "{\"chatId\":\"%s\",\"text\":\"%s\"}",
            escaped_chat_id,
            escaped_text
        );

    if (written < 0 ||
        (size_t)written >= sizeof(body))
    {
        return -4;
    }

    u32 status = 0;
    u32 err = 0;

    char* resp =
        http_post(
            url,
            body,
            &status,
            &err
        );

    if (!resp)
        return err
             ? -(int)err
             : -1;

    int status_error =
        http_status_error(status);

    free(resp);

    return status_error;
}

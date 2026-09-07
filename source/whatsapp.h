#ifndef WHATSAPP_H
#define WHATSAPP_H

#include <3ds.h>

#define WA_MAX_CHATS 64
#define WA_MAX_MESSAGES 128
#define WA_ID_LEN 64
#define WA_NAME_LEN 64
#define WA_TEXT_LEN 512

typedef struct {
	char id[WA_ID_LEN];
	char name[WA_NAME_LEN];
	int unread;
} WAChat;

typedef struct {
	char id[WA_ID_LEN];
	char sender[WA_NAME_LEN];
	char text[WA_TEXT_LEN];
	int isMe;
} WAMessage;

/* Set the base URL for the bridge API */
void wa_set_bridge_url(const char* url);

/* Fetch chats. Returns number of chats or <0 on error */
int wa_get_chats(WAChat* out_chats, int max_chats);

/* Fetch messages for a chat. Returns number of messages or <0 on error */
int wa_get_messages(const char* chat_id, WAMessage* out_messages, int max_messages);

/* Send a message. Returns 0 on success, <0 on error */
int wa_send_message(const char* chat_id, const char* text);

#endif

#include "whatsapp.h"
#include "web.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_bridge_url[256] = "http://192.168.1.100:8080";

void wa_set_bridge_url(const char* url)
{
	strncpy(g_bridge_url, url, sizeof(g_bridge_url) - 1);
	g_bridge_url[sizeof(g_bridge_url) - 1] = '\0';
}

int wa_get_chats(WAChat* out_chats, int max_chats)
{
	char url[512];
	snprintf(url, sizeof(url), "%s/api/chats", g_bridge_url);

	u32 status = 0, err = 0;
	char* resp = http_get(url, &status, &err);
	if (!resp)
		return -1;

	JsonNode* root = json_parse(resp);
	free(resp);

	if (!root || root->type != JSON_ARRAY)
	{
		json_free(root);
		return -2;
	}

	int count = json_array_len(root);
	if (count > max_chats)
		count = max_chats;

	for (int i = 0; i < count; i++)
	{
		const JsonNode* item = json_array_at(root, i);
		if (!item || item->type != JSON_OBJECT) continue;

		const JsonNode* id_node = json_object_get(item, "id");
		const JsonNode* name_node = json_object_get(item, "name");
		const JsonNode* unread_node = json_object_get(item, "unread");

		if (id_node) strncpy(out_chats[i].id, json_string(id_node), WA_ID_LEN - 1);
		else out_chats[i].id[0] = '\0';
		
		if (name_node) strncpy(out_chats[i].name, json_string(name_node), WA_NAME_LEN - 1);
		else out_chats[i].name[0] = '\0';

		double unread = 0;
		if (unread_node) json_number(unread_node, &unread);
		out_chats[i].unread = (int)unread;
	}

	json_free(root);
	return count;
}

int wa_get_messages(const char* chat_id, WAMessage* out_messages, int max_messages)
{
	char url[512];
	snprintf(url, sizeof(url), "%s/api/messages?chatId=%s", g_bridge_url, chat_id);

	u32 status = 0, err = 0;
	char* resp = http_get(url, &status, &err);
	if (!resp)
		return -1;

	JsonNode* root = json_parse(resp);
	free(resp);

	if (!root || root->type != JSON_ARRAY)
	{
		json_free(root);
		return -2;
	}

	int count = json_array_len(root);
	if (count > max_messages)
		count = max_messages;

	for (int i = 0; i < count; i++)
	{
		const JsonNode* item = json_array_at(root, i);
		if (!item || item->type != JSON_OBJECT) continue;

		const JsonNode* id_node = json_object_get(item, "id");
		const JsonNode* sender_node = json_object_get(item, "sender");
		const JsonNode* text_node = json_object_get(item, "text");
		const JsonNode* isme_node = json_object_get(item, "isMe");

		if (id_node) strncpy(out_messages[i].id, json_string(id_node), WA_ID_LEN - 1);
		else out_messages[i].id[0] = '\0';
		
		if (sender_node) strncpy(out_messages[i].sender, json_string(sender_node), WA_NAME_LEN - 1);
		else out_messages[i].sender[0] = '\0';
		
		if (text_node) strncpy(out_messages[i].text, json_string(text_node), WA_TEXT_LEN - 1);
		else out_messages[i].text[0] = '\0';

		out_messages[i].isMe = (isme_node && isme_node->type == JSON_BOOL) ? isme_node->boolean : 0;
	}

	json_free(root);
	return count;
}

int wa_send_message(const char* chat_id, const char* text)
{
	char url[512];
	snprintf(url, sizeof(url), "%s/api/messages", g_bridge_url);

	// Construct simple JSON body
	// Warning: This does not escape quotes in `text`. In a real app, JSON escaping is needed.
	char body[1024];
	snprintf(body, sizeof(body), "{\"chatId\":\"%s\",\"text\":\"%s\"}", chat_id, text);

	u32 status = 0, err = 0;
	char* resp = http_post(url, body, &status, &err);
	if (!resp)
		return -1;
	
	free(resp);
	return (status >= 200 && status < 300) ? 0 : -2;
}

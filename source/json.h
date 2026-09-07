#ifndef BVG_JSON_H
#define BVG_JSON_H

#include <stddef.h>

typedef enum
{
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_STRING,
	JSON_NUMBER,
	JSON_BOOL,
	JSON_NULL
} JsonType;

typedef struct JsonNode JsonNode;
struct JsonNode
{
	JsonType type;
	char* key;
	char* str;
	double num;
	int boolean;
	JsonNode* first;
	JsonNode* next;
};

JsonNode* json_parse(const char* text);
void json_free(JsonNode* root);

const JsonNode* json_object_get(const JsonNode* obj, const char* key);
const JsonNode* json_array_at(const JsonNode* arr, int index);
int json_array_len(const JsonNode* arr);

const char* json_string(const JsonNode* node);
int json_number(const JsonNode* node, double* out);

#endif
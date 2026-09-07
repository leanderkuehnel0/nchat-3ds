#include "json.h"

#include <stdlib.h>
#include <string.h>

static JsonNode* parse_value(const char** s);

static JsonNode* node_new(JsonType type)
{
	JsonNode* n = (JsonNode*)calloc(1, sizeof(JsonNode));
	if (n)
		n->type = type;
	return n;
}

static void skip_ws(const char** s)
{
	while (**s == ' ' || **s == '\t' || **s == '\n' || **s == '\r')
		(*s)++;
}

static int is_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_val(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

static int grow(char** out, size_t* cap, size_t len)
{
	size_t ncap = (*cap) * 2;
	char* n = (char*)realloc(*out, ncap);
	if (!n)
		return -1;
	*out = n;
	*cap = ncap;
	(void)len;
	return 0;
}

static char* parse_string(const char** s)
{
	if (**s != '"')
		return NULL;
	(*s)++;
	size_t cap = 32, len = 0;
	char* out = (char*)malloc(cap);
	if (!out)
		return NULL;

	while (**s)
	{
		char c = **s;
		if (c == '"')
		{
			(*s)++;
			out[len] = '\0';
			return out;
		}
		if (c == '\\')
		{
			(*s)++;
			char rep = 0;
			switch (**s)
			{
			case '"': rep = '"'; (*s)++; break;
			case '\\': rep = '\\'; (*s)++; break;
			case '/': rep = '/'; (*s)++; break;
			case 'b': rep = '\b'; (*s)++; break;
			case 'f': rep = '\f'; (*s)++; break;
			case 'n': rep = '\n'; (*s)++; break;
			case 'r': rep = '\r'; (*s)++; break;
			case 't': rep = '\t'; (*s)++; break;
			case 'u':
			{
				(*s)++;
				if (!(is_hex((*s)[0]) && is_hex((*s)[1]) &&
				      is_hex((*s)[2]) && is_hex((*s)[3])))
				{
					free(out);
					return NULL;
				}
				unsigned cp = 0;
				int i;
				for (i = 0; i < 4; i++)
					cp = (cp << 4) | (unsigned)hex_val((*s)[i]);
				(*s) += 4;
				unsigned char tmp[3];
				int n = 0;
				if (cp < 0x80)
				{
					tmp[n++] = (unsigned char)cp;
				}
				else if (cp < 0x800)
				{
					tmp[n++] = 0xC0 | (cp >> 6);
					tmp[n++] = 0x80 | (cp & 0x3F);
				}
				else
				{
					tmp[n++] = 0xE0 | (cp >> 12);
					tmp[n++] = 0x80 | ((cp >> 6) & 0x3F);
					tmp[n++] = 0x80 | (cp & 0x3F);
				}
				for (i = 0; i < n; i++)
				{
					if (len + 1 >= cap && grow(&out, &cap, len))
					{
						free(out);
						return NULL;
					}
					out[len++] = (char)tmp[i];
				}
				continue;
			}
			default:
				free(out);
				return NULL;
			}
			if (len + 1 >= cap && grow(&out, &cap, len))
			{
				free(out);
				return NULL;
			}
			out[len++] = rep;
			continue;
		}
		if (len + 1 >= cap && grow(&out, &cap, len))
		{
			free(out);
			return NULL;
		}
		out[len++] = c;
		(*s)++;
	}
	free(out);
	return NULL;
}

static double parse_number(const char** s)
{
	char* end = NULL;
	double d = strtod(*s, &end);
	*s = end;
	return d;
}

static JsonNode* parse_value(const char** s)
{
	skip_ws(s);
	char c = **s;

	if (c == '"')
	{
		JsonNode* n = node_new(JSON_STRING);
		if (!n)
			return NULL;
		n->str = parse_string(s);
		return n;
	}

	if (c == '{')
	{
		(*s)++;
		JsonNode* n = node_new(JSON_OBJECT);
		if (!n)
			return NULL;
		JsonNode** tail = &n->first;
		skip_ws(s);
		if (**s == '}')
		{
			(*s)++;
			return n;
		}
		for (;;)
		{
			skip_ws(s);
			if (**s != '"')
			{
				json_free(n);
				return NULL;
			}
			char* key = parse_string(s);
			if (!key)
			{
				json_free(n);
				return NULL;
			}
			skip_ws(s);
			if (**s != ':')
			{
				free(key);
				json_free(n);
				return NULL;
			}
			(*s)++;
			JsonNode* v = parse_value(s);
			if (!v)
			{
				free(key);
				json_free(n);
				return NULL;
			}
			v->key = key;
			*tail = v;
			tail = &v->next;
			skip_ws(s);
			if (**s == ',')
			{
				(*s)++;
				continue;
			}
			if (**s == '}')
			{
				(*s)++;
				return n;
			}
			json_free(n);
			return NULL;
		}
	}

	if (c == '[')
	{
		(*s)++;
		JsonNode* n = node_new(JSON_ARRAY);
		if (!n)
			return NULL;
		JsonNode** tail = &n->first;
		skip_ws(s);
		if (**s == ']')
		{
			(*s)++;
			return n;
		}
		for (;;)
		{
			JsonNode* v = parse_value(s);
			if (!v)
			{
				json_free(n);
				return NULL;
			}
			*tail = v;
			tail = &v->next;
			skip_ws(s);
			if (**s == ',')
			{
				(*s)++;
				continue;
			}
			if (**s == ']')
			{
				(*s)++;
				return n;
			}
			json_free(n);
			return NULL;
		}
	}

	if (c == 't')
	{
		if (strncmp(*s, "true", 4) == 0)
		{
			*s += 4;
			JsonNode* n = node_new(JSON_BOOL);
			if (n)
				n->boolean = 1;
			return n;
		}
	}
	if (c == 'f')
	{
		if (strncmp(*s, "false", 5) == 0)
		{
			*s += 5;
			JsonNode* n = node_new(JSON_BOOL);
			if (n)
				n->boolean = 0;
			return n;
		}
	}
	if (c == 'n')
	{
		if (strncmp(*s, "null", 4) == 0)
		{
			*s += 4;
			return node_new(JSON_NULL);
		}
	}
	if (c == '-' || (c >= '0' && c <= '9'))
	{
		JsonNode* n = node_new(JSON_NUMBER);
		if (!n)
			return NULL;
		n->num = parse_number(s);
		return n;
	}
	return NULL;
}

JsonNode* json_parse(const char* text)
{
	if (!text)
		return NULL;
	const char* s = text;
	skip_ws(&s);
	JsonNode* v = parse_value(&s);
	if (!v)
		return NULL;
	skip_ws(&s);
	return v;
}

void json_free(JsonNode* n)
{
	while (n)
	{
		JsonNode* next = n->next;
		free(n->key);
		free(n->str);
		json_free(n->first);
		free(n);
		n = next;
	}
}

const JsonNode* json_object_get(const JsonNode* obj, const char* key)
{
	if (!obj || obj->type != JSON_OBJECT || !key)
		return NULL;
	for (const JsonNode* c = obj->first; c; c = c->next)
		if (c->key && strcmp(c->key, key) == 0)
			return c;
	return NULL;
}

const JsonNode* json_array_at(const JsonNode* arr, int index)
{
	if (!arr || arr->type != JSON_ARRAY || index < 0)
		return NULL;
	int i = 0;
	for (const JsonNode* c = arr->first; c; c = c->next)
		if (i++ == index)
			return c;
	return NULL;
}

int json_array_len(const JsonNode* arr)
{
	if (!arr || arr->type != JSON_ARRAY)
		return 0;
	int i = 0;
	for (const JsonNode* c = arr->first; c; c = c->next)
		i++;
	return i;
}

const char* json_string(const JsonNode* node)
{
	if (!node || node->type != JSON_STRING || !node->str)
		return NULL;
	return node->str;
}

int json_number(const JsonNode* node, double* out)
{
	if (!node || node->type != JSON_NUMBER || !out)
		return -1;
	*out = node->num;
	return 0;
}
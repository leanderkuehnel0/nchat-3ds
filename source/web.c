#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#define MAX_BODY (1u << 20)
#define MAX_RETRIES 3

typedef struct
{
	char* data;
	size_t used;
	size_t cap;
} buf_t;

static size_t collect(char* ptr, size_t size, size_t count, void* userdata)
{
	buf_t* b = (buf_t*)userdata;
	size_t n = size * count;
	if (n == 0)
		return 0;

	if (b->used + n + 1 > b->cap)
	{
		size_t nc = b->cap ? b->cap : 8192;
		while (nc < b->used + n + 1)
		{
			if (nc >= MAX_BODY)
				return 0;
			nc *= 2;
			if (nc > MAX_BODY)
				nc = MAX_BODY;
		}
		char* nd = (char*)realloc(b->data, nc);
		if (!nd)
			return 0;
		b->data = nd;
		b->cap = nc;
	}

	memcpy(b->data + b->used, ptr, n);
	b->used += n;
	return n;
}

static void init_once(void)
{
	static int done = 0;
	if (!done)
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);
		done = 1;
	}
}

char* http_get(const char* url, u32* status_out, u32* err_out)
{
	if (status_out)
		*status_out = 0;
	if (err_out)
		*err_out = 0;

	init_once();

	for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
	{
		buf_t b = { NULL, 0, 0 };
		CURL* c = curl_easy_init();
		if (!c)
		{
			if (err_out)
				*err_out = 0xFFFFFFFFu;
			return NULL;
		}

		curl_easy_setopt(c, CURLOPT_URL, url);
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(c, CURLOPT_MAXREDIRS, 4L);
		curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 45L);
		curl_easy_setopt(c, CURLOPT_USERAGENT, "bvg-navigator-3ds/0.1");
		curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "identity");
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);

		CURLcode rc = curl_easy_perform(c);
		long status = 0;
		if (rc == CURLE_OK)
			curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(c);

		if (!b.data)
			b.data = (char*)malloc(1);
		if (!b.data)
		{
			if (err_out)
				*err_out = 0xFFFFFFFEu;
			return NULL;
		}

		if (rc != CURLE_OK)
		{
			free(b.data);
			if (err_out)
				*err_out = (u32)rc;
			svcSleepThread(1000000000LL);
			continue;
		}

		if (status_out)
			*status_out = (u32)status;

		if (status >= 400)
		{
			free(b.data);
			return NULL;
		}

		b.data[b.used] = '\0';
		return b.data;
	}

	return NULL;
}
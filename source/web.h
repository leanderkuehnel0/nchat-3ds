#ifndef BVG_WEB_H
#define BVG_WEB_H

#include <3ds.h>
#include <stddef.h>

/* Performs a HTTPS GET (following redirects, retrying transient failures).
   Returns a malloc()'d, NUL-terminated body on a successful (< 400) response,
   or NULL on any failure.
   If status_out is non-NULL it receives the final HTTP status code (0 if the
   request failed before a response). If err_out is non-NULL it receives the
   failing libcurl result code when the request failed. */
char* http_get(const char* url, u32* status_out, u32* err_out);

#endif
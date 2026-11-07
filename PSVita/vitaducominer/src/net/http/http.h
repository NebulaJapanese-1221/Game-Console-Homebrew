#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stddef.h>

int http_init(void);
void http_term(void);
int http_get(const char *url, char *out_buf, size_t out_size, int *status_out);

#endif

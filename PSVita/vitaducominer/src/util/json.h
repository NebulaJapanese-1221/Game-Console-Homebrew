#ifndef JSON_H
#define JSON_H

#include <stddef.h>

int json_get_string(const char *json, const char *key, char *out, int out_size);
int json_get_number(const char *json, const char *key, long *out);
int json_get_bool(const char *json, const char *key, int *out);

#endif

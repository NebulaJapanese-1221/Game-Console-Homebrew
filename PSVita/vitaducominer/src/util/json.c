#include "json.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *skip_string(const char *p) {
    if (*p != '"') return NULL;
    p++;
    while (*p) {
        if (*p == '\\') {
            if (!p[1]) return NULL;
            p += 2;
        } else if (*p++ == '"') {
            return p;
        }
    }
    return NULL;
}

static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') return skip_string(p);

    const char *start = p;
    int depth = 0;
    int in_string = 0;
    while (*p) {
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (*p == '"') in_string = 0;
        } else if (*p == '"') {
            in_string = 1;
        } else if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (depth == 0) break;
            depth--;
        } else if (*p == ',' && depth == 0) {
            break;
        }
        p++;
    }

    while (p > start && isspace((unsigned char)p[-1])) p--;
    return p == start ? NULL : p;
}

static int find_value(const char *json, const char *key, const char **value) {
    const char *p = skip_ws(json);
    if (*p++ != '{') return 0;
    p = skip_ws(p);

    while (*p && *p != '}') {
        char name[128];
        size_t used = 0;
        if (*p != '"') return 0;
        p++;

        while (*p && *p != '"') {
            char ch = *p++;
            if (ch == '\\') {
                if (!*p) return 0;
                ch = *p++;
            }
            if (used + 1 < sizeof(name)) name[used++] = ch;
        }
        if (*p++ != '"') return 0;
        name[used] = '\0';
        p = skip_ws(p);
        if (*p++ != ':') return 0;
        p = skip_ws(p);

        if (strcmp(name, key) == 0) {
            *value = p;
            return 1;
        }

        p = skip_value(p);
        if (!p) return 0;
        p = skip_ws(p);
        if (*p == ',') {
            p = skip_ws(p + 1);
        } else if (*p != '}') {
            return 0;
        }
    }
    return 0;
}

int json_get_string(const char *json, const char *key, char *out, int out_size) {
    if (!out || out_size <= 0) return 0;
    out[0] = '\0';
    if (!json || !key) return 0;

    const char *p;
    if (!find_value(json, key, &p) || *p != '"') return 0;
    p++;
    int used = 0;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\') {
            if (!*p) return 0;
            ch = (unsigned char)*p++;
            switch (ch) {
                case '"': case '\\': case '/': break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u':
                    if (!p[0] || !p[1] || !p[2] || !p[3]) return 0;
                    p += 4;
                    ch = '?';
                    break;
                default: return 0;
            }
        }
        if (used < out_size - 1) out[used++] = (char)ch;
    }
    if (*p != '"') {
        out[0] = '\0';
        return 0;
    }
    out[used] = '\0';
    return 1;
}

int json_get_number(const char *json, const char *key, long *out) {
    if (!out || !json || !key) return 0;
    *out = 0;

    const char *p;
    if (!find_value(json, key, &p)) return 0;
    errno = 0;
    char *end;
    long value = strtol(p, &end, 10);
    if (p == end || errno == ERANGE) return 0;
    end = (char *)skip_ws(end);
    if (*end != ',' && *end != '}' && *end != '\0') return 0;
    *out = value;
    return 1;
}

int json_get_bool(const char *json, const char *key, int *out) {
    if (!out || !json || !key) return 0;
    *out = 0;

    const char *p;
    if (!find_value(json, key, &p)) return 0;
    if (strncmp(p, "true", 4) == 0 &&
        (*skip_ws(p + 4) == ',' || *skip_ws(p + 4) == '}' || *skip_ws(p + 4) == '\0')) {
        *out = 1;
        return 1;
    }
    if (strncmp(p, "false", 5) == 0 &&
        (*skip_ws(p + 5) == ',' || *skip_ws(p + 5) == '}' || *skip_ws(p + 5) == '\0')) {
        return 1;
    }
    long number;
    if (json_get_number(json, key, &number)) {
        *out = number != 0;
        return 1;
    }
    return 0;
}

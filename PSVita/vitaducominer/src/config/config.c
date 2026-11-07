#include "config.h"
#include "json.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CONFIG_PATH "ux0:/vitaducominer/config.json"

char g_set_server[128] = "server.duinocoin.com";
char g_set_port[8] = "12181";
char g_set_user[64] = "vita_miner";
char g_set_worker[64] = "";
char g_set_key[64] = "";
char g_set_donation[64] = "";
bool g_set_auto_start = false;

static char g_log_lines[8][96];
static int g_log_count = 0;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static int ensure_dir(const char *path) {
    char tmp[256];
    int n = snprintf(tmp, sizeof(tmp), "%s", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return -1;
    char *p = tmp + 1;
    while ((p = strchr(p, '/')) != NULL) {
        *p = 0;
        sceIoMkdir(tmp, 0777);
        *p = '/';
        p++;
    }
    return 0;
}

static int read_file(const char *path, char *buf, int buf_size) {
    int fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    int total = 0;
    while (total < buf_size - 1) {
        int rd = sceIoRead(fd, buf + total, buf_size - 1 - total);
        if (rd <= 0) break;
        total += rd;
    }
    buf[total] = 0;
    sceIoClose(fd);
    return total;
}

static int write_file(const char *path, const char *buf) {
    if (ensure_dir(path) < 0) return -1;
    int fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return -1;
    int len = (int)strlen(buf);
    int total = 0;
    while (total < len) {
        int written = sceIoWrite(fd, buf + total, len - total);
        if (written <= 0) {
            sceIoClose(fd);
            return -1;
        }
        total += written;
    }
    sceIoClose(fd);
    return 0;
}

void config_set_defaults(Config *c) {
    memset(c, 0, sizeof(*c));
    snprintf(c->server, sizeof(c->server), "server.duinocoin.com");
    c->port = 12181;
    snprintf(c->username, sizeof(c->username), "vita_miner");
    c->worker[0] = 0;
    c->mining_key[0] = 0;
    c->donation_addr[0] = 0;
    c->auto_start = false;
    c->pool_fetched = false;
    c->pool_host[0] = 0;
    c->pool_port = 0;
    snprintf(g_set_server, sizeof(g_set_server), "%s", c->server);
    snprintf(g_set_port, sizeof(g_set_port), "%u", c->port);
    snprintf(g_set_user, sizeof(g_set_user), "%s", c->username);
    g_set_worker[0] = '\0';
    g_set_key[0] = '\0';
    g_set_donation[0] = '\0';
    g_set_auto_start = false;
}

int config_load(Config *c) {
    config_set_defaults(c);
    char buf[2048];
    int len = read_file(CONFIG_PATH, buf, sizeof(buf));
    if (len <= 0) return -1;

    char tmp[256];
    if (json_get_string(buf, "server", tmp, sizeof(tmp))) {
        snprintf(c->server, sizeof(c->server), "%s", tmp);
        snprintf(g_set_server, sizeof(g_set_server), "%s", tmp);
    }
    long port = 0;
    if (json_get_number(buf, "port", &port) && port > 0 && port <= 65535) {
        c->port = (uint16_t)port;
        snprintf(g_set_port, sizeof(g_set_port), "%u", c->port);
    }
    if (json_get_string(buf, "username", tmp, sizeof(tmp))) {
        snprintf(c->username, sizeof(c->username), "%s", tmp);
        snprintf(g_set_user, sizeof(g_set_user), "%s", tmp);
    }
    if (json_get_string(buf, "worker", tmp, sizeof(tmp))) {
        snprintf(c->worker, sizeof(c->worker), "%s", tmp);
        snprintf(g_set_worker, sizeof(g_set_worker), "%s", tmp);
    }
    if (json_get_string(buf, "mining_key", tmp, sizeof(tmp))) {
        snprintf(c->mining_key, sizeof(c->mining_key), "%s", tmp);
        snprintf(g_set_key, sizeof(g_set_key), "%s", tmp);
    }
    if (json_get_string(buf, "donation_addr", tmp, sizeof(tmp))) {
        snprintf(c->donation_addr, sizeof(c->donation_addr), "%s", tmp);
        snprintf(g_set_donation, sizeof(g_set_donation), "%s", tmp);
    }
    int auto_start = 0;
    if (json_get_bool(buf, "auto_start", &auto_start)) {
        c->auto_start = auto_start != 0;
        g_set_auto_start = c->auto_start;
    }
    if (json_get_string(buf, "pool_host", tmp, sizeof(tmp)) && tmp[0]) {
        snprintf(c->pool_host, sizeof(c->pool_host), "%s", tmp);
    }
    long pool_port = 0;
    if (json_get_number(buf, "pool_port", &pool_port) &&
        pool_port > 0 && pool_port <= 65535) {
        c->pool_port = (uint16_t)pool_port;
    }
    c->pool_fetched = c->pool_host[0] != '\0' && c->pool_port != 0;
    return 0;
}

static int append_text(char *buf, size_t size, size_t *used, const char *text) {
    size_t len = strlen(text);
    if (len >= size - *used) return -1;
    memcpy(buf + *used, text, len + 1);
    *used += len;
    return 0;
}

static int append_json_string(char *buf, size_t size, size_t *used, const char *text) {
    static const char hex[] = "0123456789abcdef";
    if (append_text(buf, size, used, "\"") < 0) return -1;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        char escaped[7];
        const char *value = NULL;
        switch (*p) {
            case '"': value = "\\\""; break;
            case '\\': value = "\\\\"; break;
            case '\b': value = "\\b"; break;
            case '\f': value = "\\f"; break;
            case '\n': value = "\\n"; break;
            case '\r': value = "\\r"; break;
            case '\t': value = "\\t"; break;
            default:
                if (*p < 0x20) {
                    escaped[0] = '\\';
                    escaped[1] = 'u';
                    escaped[2] = '0';
                    escaped[3] = '0';
                    escaped[4] = hex[*p >> 4];
                    escaped[5] = hex[*p & 0x0f];
                    escaped[6] = '\0';
                    value = escaped;
                } else {
                    escaped[0] = (char)*p;
                    escaped[1] = '\0';
                    value = escaped;
                }
                break;
        }
        if (append_text(buf, size, used, value) < 0) return -1;
    }
    return append_text(buf, size, used, "\"");
}

static int append_number(char *buf, size_t size, size_t *used, const char *format,
                         unsigned int value) {
    char text[32];
    int n = snprintf(text, sizeof(text), format, value);
    if (n < 0 || (size_t)n >= sizeof(text)) return -1;
    return append_text(buf, size, used, text);
}

int config_save(const Config *c) {
    char buf[2048];
    size_t n = 0;
    if (append_text(buf, sizeof(buf), &n, "{\n  \"server\": ") < 0 ||
        append_json_string(buf, sizeof(buf), &n, c->server) < 0 ||
        append_number(buf, sizeof(buf), &n, ",\n  \"port\": %u,\n", c->port) < 0 ||
        append_text(buf, sizeof(buf), &n, "  \"username\": ") < 0 ||
        append_json_string(buf, sizeof(buf), &n, c->username) < 0 ||
        append_text(buf, sizeof(buf), &n, ",\n  \"worker\": ") < 0 ||
        append_json_string(buf, sizeof(buf), &n, c->worker) < 0 ||
        append_text(buf, sizeof(buf), &n, ",\n  \"mining_key\": ") < 0 ||
        append_json_string(buf, sizeof(buf), &n, c->mining_key) < 0 ||
        append_text(buf, sizeof(buf), &n, ",\n  \"donation_addr\": ") < 0 ||
        append_json_string(buf, sizeof(buf), &n, c->donation_addr) < 0 ||
        append_text(buf, sizeof(buf), &n, c->auto_start
            ? ",\n  \"auto_start\": true,\n  \"pool_host\": "
            : ",\n  \"auto_start\": false,\n  \"pool_host\": ") < 0 ||
        append_json_string(buf, sizeof(buf), &n, c->pool_host) < 0 ||
        append_number(buf, sizeof(buf), &n, ",\n  \"pool_port\": %u\n}\n", c->pool_port) < 0) {
        return -1;
    }
    return write_file(CONFIG_PATH, buf);
}

const char *config_field_buf(int field) {
    switch (field) {
        case 0: return g_set_server;
        case 1: return g_set_port;
        case 2: return g_set_user;
        case 3: return g_set_worker;
        case 4: return g_set_key;
        case 5: return g_set_donation;
        default: return "";
    }
}

char *config_field_buf_write(int field) {
    switch (field) {
        case 0: return g_set_server;
        case 1: return g_set_port;
        case 2: return g_set_user;
        case 3: return g_set_worker;
        case 4: return g_set_key;
        case 5: return g_set_donation;
        default: return NULL;
    }
}

int config_field_max_len(int field) {
    switch (field) {
        case 0: return sizeof(g_set_server);
        case 1: return sizeof(g_set_port);
        case 2: return sizeof(g_set_user);
        case 3: return sizeof(g_set_worker);
        case 4: return sizeof(g_set_key);
        case 5: return sizeof(g_set_donation);
        default: return 0;
    }
}

void config_commit_field(int field, const char *text) {
    char *buf = config_field_buf_write(field);
    int max_len = config_field_max_len(field);
    if (buf && text && max_len > 0) snprintf(buf, (size_t)max_len, "%s", text);
}

void config_set_auto_start(bool enabled) { g_set_auto_start = enabled; }
bool config_get_auto_start(void) { return g_set_auto_start; }

void config_log(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&g_log_lock);
    for (int i = 7; i > 0; i--) {
        snprintf(g_log_lines[i], 96, "%s", g_log_lines[i - 1]);
    }
    snprintf(g_log_lines[0], 96, "%s", buf);
    if (g_log_count < 8) g_log_count++;
    pthread_mutex_unlock(&g_log_lock);
}

const char *config_log_line(int i) {
    if (i < 0 || i >= 8) return "";
    return g_log_lines[i];
}

int config_log_count(void) {
    return g_log_count;
}

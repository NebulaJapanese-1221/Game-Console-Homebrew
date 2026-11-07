#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char server[128];
    uint16_t port;
    char username[64];
    char worker[64];
    char mining_key[64];
    char donation_addr[64];
    bool auto_start;
    bool pool_fetched;
    char pool_host[128];
    uint16_t pool_port;
} Config;

int config_load(Config *c);
int config_save(const Config *c);
void config_set_defaults(Config *c);

/* Editable settings fields (synced to/from Config on load/save) */
extern char g_set_server[128];
extern char g_set_port[8];
extern char g_set_user[64];
extern char g_set_worker[64];
extern char g_set_key[64];
extern char g_set_donation[64];
extern bool g_set_auto_start;

const char *config_field_buf(int field);
char *config_field_buf_write(int field);
int config_field_max_len(int field);
void config_commit_field(int field, const char *text);
void config_set_auto_start(bool enabled);
bool config_get_auto_start(void);

/* Simple activity log */
void config_log(const char *fmt, ...);
const char *config_log_line(int i);
int config_log_count(void);

#endif

#ifndef MINER_H
#define MINER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

typedef struct {
    char username[64];
    char worker[64];
    char mining_key[64];
    char server[128];
    uint16_t port;
    uint32_t difficulty;
    bool connected;
    bool mining;
    bool auto_start;
    bool running;
    bool pool_fetched;
    uint64_t hashes;
    double hashrate;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    char reject_reason[128];
    uint32_t nonce;
    uint8_t last_digest[20];
    uint64_t uptime_ms;
    uint64_t start_time_ms;
    uint64_t reconnect_backoff_ms;
} Miner;

int miner_start(void);
void miner_stop(void);
void miner_join(void);
void miner_get_stats(Miner *out);
void miner_set_server(const char *server, uint16_t port);
void miner_set_username(const char *username);
void miner_set_worker(const char *worker);
void miner_set_mining_key(const char *key);
void miner_set_auto_start(bool enabled);
bool miner_is_mining(void);
bool miner_get_auto_start(void);
void miner_toggle_mining(void);
int miner_fetch_pool(void);
const char *miner_pool_host(void);
uint16_t miner_pool_port(void);
bool miner_pool_valid(void);

#endif

#include "miner.h"
#include "net.h"
#include "duco-s1.h"
#include "pool.h"
#include "config.h"
#include <psp2/kernel/threadmgr.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define BACKOFF_BASE_MS 1000ull
#define BACKOFF_MAX_MS 30000ull
#define MINER_VERSION "VitaDucoMiner 1.0"

static Miner g_miner;
static pthread_t g_miner_thread;
static bool g_thread_started = false;
static pthread_mutex_t g_miner_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t get_time_ms(void) {
    return sceKernelGetSystemTimeWide() / 1000ull;
}

static bool miner_active(void) {
    bool active;
    pthread_mutex_lock(&g_miner_lock);
    active = g_miner.running && g_miner.mining;
    pthread_mutex_unlock(&g_miner_lock);
    return active;
}

static bool miner_running(void) {
    bool running;
    pthread_mutex_lock(&g_miner_lock);
    running = g_miner.running;
    pthread_mutex_unlock(&g_miner_lock);
    return running;
}

static void set_reject_reason(const char *reason) {
    pthread_mutex_lock(&g_miner_lock);
    snprintf(g_miner.reject_reason, sizeof(g_miner.reject_reason), "%s", reason);
    pthread_mutex_unlock(&g_miner_lock);
}

static int parse_job(char *line, char *last_hash, size_t last_hash_size,
                     char expected_hash[41], uint32_t *difficulty) {
    char *first = strchr(line, '|');
    if (!first) return -1;
    *first++ = '\0';
    char *second = strchr(first, '|');
    if (!second) return -1;
    *second++ = '\0';

    char *end;
    unsigned long parsed_difficulty = strtoul(second, &end, 10);
    if (second == end || *end != '\0' || parsed_difficulty == 0 ||
        parsed_difficulty > (UINT32_MAX - 1u) / 100u) {
        return -1;
    }
    if (!line[0] || strlen(line) >= last_hash_size || strlen(first) != 40) return -1;
    for (size_t i = 0; i < 40; i++) {
        char ch = first[i];
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) {
            return -1;
        }
    }
    snprintf(last_hash, last_hash_size, "%s", line);
    snprintf(expected_hash, 41, "%s", first);
    *difficulty = (uint32_t)parsed_difficulty;
    return 0;
}

static void *miner_loop(void *arg) {
    (void)arg;

    while (miner_running()) {
        if (!miner_active()) {
            pthread_mutex_lock(&g_miner_lock);
            g_miner.connected = false;
            g_miner.start_time_ms = 0;
            g_miner.uptime_ms = 0;
            g_miner.hashrate = 0.0;
            g_miner.reconnect_backoff_ms = BACKOFF_BASE_MS;
            pthread_mutex_unlock(&g_miner_lock);
            sceKernelDelayThread(100000);
            continue;
        }

        char server[sizeof(g_miner.server)];
        char username[sizeof(g_miner.username)];
        char worker[sizeof(g_miner.worker)];
        char mining_key[sizeof(g_miner.mining_key)];
        uint16_t port;
        pthread_mutex_lock(&g_miner_lock);
        snprintf(server, sizeof(server), "%s", g_miner.server);
        snprintf(username, sizeof(username), "%s", g_miner.username);
        snprintf(worker, sizeof(worker), "%s", g_miner.worker);
        snprintf(mining_key, sizeof(mining_key), "%s", g_miner.mining_key);
        port = g_miner.port;
        pthread_mutex_unlock(&g_miner_lock);

        if (net_connect(server, port) < 0) {
            pthread_mutex_lock(&g_miner_lock);
            g_miner.connected = false;
            snprintf(g_miner.reject_reason, sizeof(g_miner.reject_reason), "%s", "connect failed");
            if (g_miner.reconnect_backoff_ms < BACKOFF_MAX_MS) {
                g_miner.reconnect_backoff_ms *= 2;
                if (g_miner.reconnect_backoff_ms > BACKOFF_MAX_MS) {
                    g_miner.reconnect_backoff_ms = BACKOFF_MAX_MS;
                }
            }
            uint64_t backoff = g_miner.reconnect_backoff_ms;
            pthread_mutex_unlock(&g_miner_lock);
            for (uint64_t elapsed = 0; elapsed < backoff && miner_active(); elapsed += 100) {
                sceKernelDelayThread(100000);
            }
            continue;
        }

        pthread_mutex_lock(&g_miner_lock);
        g_miner.connected = true;
        g_miner.start_time_ms = get_time_ms();
        g_miner.reconnect_backoff_ms = BACKOFF_BASE_MS;
        pthread_mutex_unlock(&g_miner_lock);

        uint8_t line[512];
        while (miner_active()) {
            char request[320];
            int request_len = snprintf(request, sizeof(request), "JOB|%s|1|%s|\n",
                                       username, mining_key);
            if (request_len < 0 || (size_t)request_len >= sizeof(request) ||
                net_send((const uint8_t *)request, request_len) != request_len) {
                set_reject_reason("job request failed");
                break;
            }

            int line_len = net_recv_line(line, sizeof(line));
            if (line_len <= 0) {
                set_reject_reason(line_len == -2 ? "server response too long" : "connection lost");
                break;
            }

            char last_hash[128];
            char expected_hash[41];
            uint32_t difficulty;
            if (parse_job((char *)line, last_hash, sizeof(last_hash),
                          expected_hash, &difficulty) < 0) {
                set_reject_reason("invalid job");
                break;
            }

            uint32_t nonce = 0;
            uint8_t digest[20];
            uint32_t max_iter = difficulty * 100u + 1u;
            uint64_t hash_start = sceKernelGetSystemTimeWide();
            if (!duco_s1_mine(last_hash, expected_hash, &nonce, digest, max_iter)) {
                set_reject_reason("job solution not found");
                continue;
            }
            uint64_t elapsed_us = sceKernelGetSystemTimeWide() - hash_start;
            if (elapsed_us == 0) elapsed_us = 1;
            uint64_t hashes = (uint64_t)nonce + 1;
            double hashrate = (double)hashes * 1000000.0 / (double)elapsed_us;

            char share[320];
            int share_len = snprintf(share, sizeof(share),
                                     "%u|%.2f|%s|%s|||0\n",
                                     nonce, hashrate, worker, MINER_VERSION);
            if (share_len < 0 || (size_t)share_len >= sizeof(share) ||
                net_send((const uint8_t *)share, share_len) != share_len) {
                set_reject_reason("share submission failed");
                break;
            }

            line_len = net_recv_line(line, sizeof(line));
            if (line_len <= 0) {
                set_reject_reason(line_len == -2 ? "server response too long" : "connection lost");
                break;
            }

            char *reason = strchr((char *)line, '|');
            if (strcmp((char *)line, "GOOD") == 0 ||
                strncmp((char *)line, "GOOD|", 5) == 0 ||
                strcmp((char *)line, "BLOCK") == 0 ||
                strncmp((char *)line, "BLOCK|", 6) == 0) {
                pthread_mutex_lock(&g_miner_lock);
                g_miner.shares_accepted++;
                g_miner.hashes += hashes;
                g_miner.hashrate = hashrate;
                g_miner.difficulty = difficulty;
                g_miner.nonce = nonce;
                memcpy(g_miner.last_digest, digest, sizeof(digest));
                g_miner.uptime_ms = get_time_ms() - g_miner.start_time_ms;
                g_miner.reject_reason[0] = '\0';
                pthread_mutex_unlock(&g_miner_lock);
            } else if (strcmp((char *)line, "BAD") == 0 ||
                       strncmp((char *)line, "BAD|", 4) == 0) {
                pthread_mutex_lock(&g_miner_lock);
                g_miner.shares_rejected++;
                g_miner.hashes += hashes;
                g_miner.hashrate = hashrate;
                g_miner.difficulty = difficulty;
                g_miner.nonce = nonce;
                memcpy(g_miner.last_digest, digest, sizeof(digest));
                if (reason && reason[1]) {
                    snprintf(g_miner.reject_reason, sizeof(g_miner.reject_reason), "%s", reason + 1);
                } else {
                    snprintf(g_miner.reject_reason, sizeof(g_miner.reject_reason), "%s", "rejected by pool");
                }
                pthread_mutex_unlock(&g_miner_lock);
            } else {
                set_reject_reason("unexpected pool response");
                break;
            }
        }

        net_close();
        pthread_mutex_lock(&g_miner_lock);
        g_miner.connected = false;
        if (g_miner.mining) {
            if (g_miner.reconnect_backoff_ms < BACKOFF_MAX_MS) {
                g_miner.reconnect_backoff_ms *= 2;
                if (g_miner.reconnect_backoff_ms > BACKOFF_MAX_MS) {
                    g_miner.reconnect_backoff_ms = BACKOFF_MAX_MS;
                }
            }
        }
        uint64_t backoff = g_miner.reconnect_backoff_ms;
        pthread_mutex_unlock(&g_miner_lock);
        for (uint64_t elapsed = 0; elapsed < backoff && miner_active(); elapsed += 100) {
            sceKernelDelayThread(100000);
        }
    }

    return NULL;
}

int miner_start(void) {
    pthread_mutex_lock(&g_miner_lock);
    if (g_thread_started) {
        pthread_mutex_unlock(&g_miner_lock);
        return 0;
    }
    memset(&g_miner, 0, sizeof(g_miner));
    snprintf(g_miner.username, sizeof(g_miner.username), "%s", "vita_miner");
    snprintf(g_miner.server, sizeof(g_miner.server), "%s", "server.duinocoin.com");
    g_miner.port = 12181;
    g_miner.difficulty = 1;
    g_miner.running = true;
    g_miner.reconnect_backoff_ms = BACKOFF_BASE_MS;
    pthread_mutex_unlock(&g_miner_lock);

    int ret = pthread_create(&g_miner_thread, NULL, miner_loop, NULL);
    if (ret != 0) {
        pthread_mutex_lock(&g_miner_lock);
        g_miner.running = false;
        pthread_mutex_unlock(&g_miner_lock);
        return ret;
    }
    pthread_mutex_lock(&g_miner_lock);
    g_thread_started = true;
    pthread_mutex_unlock(&g_miner_lock);
    return 0;
}

void miner_stop(void) {
    pthread_mutex_lock(&g_miner_lock);
    g_miner.mining = false;
    pthread_mutex_unlock(&g_miner_lock);
}

void miner_join(void) {
    pthread_mutex_lock(&g_miner_lock);
    bool started = g_thread_started;
    g_miner.running = false;
    g_miner.mining = false;
    pthread_mutex_unlock(&g_miner_lock);
    if (started) {
        net_close();
        pthread_join(g_miner_thread, NULL);
        pthread_mutex_lock(&g_miner_lock);
        g_thread_started = false;
        pthread_mutex_unlock(&g_miner_lock);
    }
    net_close();
}

void miner_get_stats(Miner *out) {
    if (!out) return;
    pthread_mutex_lock(&g_miner_lock);
    memcpy(out, &g_miner, sizeof(*out));
    pthread_mutex_unlock(&g_miner_lock);
}

void miner_set_server(const char *server, uint16_t port) {
    if (!server || !server[0] || port == 0) return;
    pthread_mutex_lock(&g_miner_lock);
    snprintf(g_miner.server, sizeof(g_miner.server), "%s", server);
    g_miner.port = port;
    pthread_mutex_unlock(&g_miner_lock);
}

void miner_set_username(const char *username) {
    if (!username) return;
    pthread_mutex_lock(&g_miner_lock);
    snprintf(g_miner.username, sizeof(g_miner.username), "%s", username);
    pthread_mutex_unlock(&g_miner_lock);
}

void miner_set_worker(const char *worker) {
    if (!worker) return;
    pthread_mutex_lock(&g_miner_lock);
    snprintf(g_miner.worker, sizeof(g_miner.worker), "%s", worker);
    pthread_mutex_unlock(&g_miner_lock);
}

void miner_set_mining_key(const char *key) {
    if (!key) return;
    pthread_mutex_lock(&g_miner_lock);
    snprintf(g_miner.mining_key, sizeof(g_miner.mining_key), "%s", key);
    pthread_mutex_unlock(&g_miner_lock);
}

void miner_set_auto_start(bool enabled) {
    pthread_mutex_lock(&g_miner_lock);
    g_miner.auto_start = enabled;
    pthread_mutex_unlock(&g_miner_lock);
}

bool miner_get_auto_start(void) {
    bool enabled;
    pthread_mutex_lock(&g_miner_lock);
    enabled = g_miner.auto_start;
    pthread_mutex_unlock(&g_miner_lock);
    return enabled;
}

bool miner_is_mining(void) {
    bool mining;
    pthread_mutex_lock(&g_miner_lock);
    mining = g_miner.mining;
    pthread_mutex_unlock(&g_miner_lock);
    return mining;
}

void miner_toggle_mining(void) {
    pthread_mutex_lock(&g_miner_lock);
    g_miner.mining = !g_miner.mining;
    if (g_miner.mining) {
        g_miner.start_time_ms = get_time_ms();
        g_miner.reconnect_backoff_ms = BACKOFF_BASE_MS;
    } else {
        g_miner.start_time_ms = 0;
        g_miner.uptime_ms = 0;
    }
    pthread_mutex_unlock(&g_miner_lock);
}

int miner_fetch_pool(void) {
    PoolInfo pool;
    int ret = pool_fetch_sync(&pool);
    if (ret < 0) {
        pthread_mutex_lock(&g_miner_lock);
        g_miner.pool_fetched = false;
        pthread_mutex_unlock(&g_miner_lock);
        return -1;
    }
    pthread_mutex_lock(&g_miner_lock);
    snprintf(g_miner.server, sizeof(g_miner.server), "%s", pool.host);
    g_miner.port = pool.port;
    g_miner.pool_fetched = true;
    pthread_mutex_unlock(&g_miner_lock);
    return 0;
}

const char *miner_pool_host(void) {
    return g_miner.server;
}

uint16_t miner_pool_port(void) {
    return g_miner.port;
}

bool miner_pool_valid(void) {
    bool valid;
    pthread_mutex_lock(&g_miner_lock);
    valid = g_miner.pool_fetched;
    pthread_mutex_unlock(&g_miner_lock);
    return valid;
}

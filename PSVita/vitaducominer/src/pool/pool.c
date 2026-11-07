#include "pool.h"
#include "http.h"
#include "json.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define GETPOOL_URL "http://server.duinocoin.com/getPool"

static void *pool_fetch_thread(void *arg) {
    PoolState *st = (PoolState *)arg;
    char buf[2048];
    int status = 0;
    int len = http_get(GETPOOL_URL, buf, sizeof(buf), &status);
    memset(&st->pool, 0, sizeof(st->pool));
    snprintf(st->pool.name, sizeof(st->pool.name), "auto");
    snprintf(st->pool.host, sizeof(st->pool.host), "server.duinocoin.com");
    st->pool.port = 12181;
    st->pool.valid = false;

    if (len <= 0 || status != 200) {
        st->done = true;
        st->success = false;
        st->fetching = false;
        return NULL;
    }

    char host[128] = {0};
    long port = 12181;
    int port_valid = 0;
    char name[64] = {0};

    json_get_string(buf, "host", host, sizeof(host));
    if (json_get_number(buf, "port", &port)) {
        if (port > 0 && port < 65536) {
            st->pool.port = (uint16_t)port;
            port_valid = 1;
        }
    }
    if (json_get_string(buf, "name", name, sizeof(name)) && name[0]) {
        snprintf(st->pool.name, sizeof(st->pool.name), "%s", name);
    }

    if (host[0]) {
        snprintf(st->pool.host, sizeof(st->pool.host), "%s", host);
        if (!port_valid) st->pool.port = 12181;
        st->pool.valid = true;
        st->success = true;
    }

    st->done = true;
    st->fetching = false;
    return NULL;
}

int pool_fetch_sync(PoolInfo *pool) {
    char buf[2048];
    int status = 0;
    int len = http_get(GETPOOL_URL, buf, sizeof(buf), &status);
    memset(pool, 0, sizeof(*pool));
    snprintf(pool->name, sizeof(pool->name), "auto");
    snprintf(pool->host, sizeof(pool->host), "server.duinocoin.com");
    pool->port = 12181;
    pool->valid = false;

    if (len <= 0 || status != 200) {
        return -1;
    }

    char host[128] = {0};
    long port = 12181;
    int port_valid = 0;
    char name[64] = {0};

    json_get_string(buf, "host", host, sizeof(host));
    if (json_get_number(buf, "port", &port)) {
        if (port > 0 && port < 65536) {
            pool->port = (uint16_t)port;
            port_valid = 1;
        }
    }
    if (json_get_string(buf, "name", name, sizeof(name)) && name[0]) {
        snprintf(pool->name, sizeof(pool->name), "%s", name);
    }

    if (host[0]) {
        snprintf(pool->host, sizeof(pool->host), "%s", host);
        if (!port_valid) pool->port = 12181;
        pool->valid = true;
        return 0;
    }
    return -1;
}

int pool_fetch_async(PoolState *state) {
    memset(state, 0, sizeof(*state));
    state->fetching = true;
    state->done = false;
    state->success = false;
    int ret = pthread_create(&state->thread, NULL, pool_fetch_thread, state);
    if (ret != 0) {
        state->fetching = false;
        state->done = true;
        return ret;
    }
    state->started = true;
    return 0;
}

void pool_fetch_join(PoolState *state) {
    if (state->started) {
        pthread_join(state->thread, NULL);
        state->started = false;
        state->fetching = false;
    }
}

bool pool_is_valid(const PoolInfo *pool) {
    return pool->valid;
}

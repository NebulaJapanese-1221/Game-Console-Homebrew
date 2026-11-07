#ifndef POOL_H
#define POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    char name[64];
    char host[128];
    uint16_t port;
    bool valid;
} PoolInfo;

typedef struct {
    PoolInfo pool;
    bool fetching;
    bool done;
    bool success;
    bool started;
    pthread_t thread;
} PoolState;

int pool_fetch_sync(PoolInfo *pool);
int pool_fetch_async(PoolState *state);
void pool_fetch_join(PoolState *state);
bool pool_is_valid(const PoolInfo *pool);

#endif

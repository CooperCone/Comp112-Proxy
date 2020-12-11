#include <time.h>

#include "tokenBucket.h"

void tb_expand(TokenBuckets *tb, int targetidx);

TokenBuckets *tb_create(size_t rate /* in tokens (bytes) per minute */) {
    size_t initial_size = 100;
    TokenBuckets *tb;

    tb = malloc(sizeof(TokenBuckets));
    tb->ray = malloc(sizeof(TokenBucket) * initial_size);
    tb->size = initial_size;
    tb->tokensPerTenSeconds = rate;

    for (size_t i = 0; i < initial_size; ++i) {
        tb->ray[i].timestamp = time(NULL);
        tb->ray[i].tokens = tb->tokensPerTenSeconds;
    }

    return tb;
}

bool tb_ratelimit(TokenBuckets *tb, int idx) {
    // Range check
    if (idx >= (int)tb->size) {
        tb_expand(tb, idx);
    }

    // Replenishment check
    if (tb->ray[idx].timestamp + (time_t)10 < time(NULL)) {
        tb->ray[idx].tokens = tb->tokensPerTenSeconds;
    }

    return (tb->ray[idx].tokens == 0);
}

void tb_update(TokenBuckets *tb, int idx, size_t usedTokens) {
    // Range check
    if (idx >= (int)tb->size) {
        tb_expand(tb, idx);
    }

    // Replenishment check
    if (tb->ray[idx].timestamp + (time_t)10 < time(NULL)) {
        tb->ray[idx].tokens = tb->tokensPerTenSeconds;
    }

    if (tb->ray[idx].tokens > usedTokens) {
        tb->ray[idx].tokens -= usedTokens;
    } else {
        tb->ray[idx].tokens = 0;
    }
}

void tb_delete(TokenBuckets *tb) {
    free(tb->ray);
    free(tb);
}

void tb_expand(TokenBuckets *tb, int targetidx) {
    size_t oldsize;

    oldsize = tb->size;
    if (tb->size * 2 > targetidx) {
        tb->size *= 2;
    } else {
        tb->size = targetidx + 100;
    }

    tb->ray = realloc(tb->ray, sizeof(TokenBucket) * tb->size);
    for (size_t i = oldsize; i < tb->size; ++i) {
        tb->ray[i].tokens = tb->tokensPerTenSeconds;
        tb->ray[i].timestamp = time(NULL);
    }
}
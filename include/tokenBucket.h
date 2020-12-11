// For rate limiting

#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct TokenBucket {
    size_t tokens; // In bytes
    time_t timestamp; // Replenishment timestamp
} TokenBucket;

typedef struct TokenBuckets {
    TokenBucket *ray; // mapping from socket number to a token bucket
    size_t size;
    size_t tokensPerTenSeconds;
} TokenBuckets;

TokenBuckets *tb_create(size_t rate);
bool tb_ratelimit(TokenBuckets *tb, int idx); // Whether rate should be limited or not
void tb_update(TokenBuckets *tb, int idx, size_t usedTokens);
void tb_delete(TokenBuckets *tb);
#pragma once

#include <stdlib.h>

#define bf_k 7       // number of hash functions
#define bf_m 2000000 // size of the bit array

typedef struct BloomFilter {
    char* bitArray;
    size_t numElements;
} BloomFilter;

BloomFilter *bf_create();
void bf_add(BloomFilter *bf, char *str);
void bf_delete(BloomFilter *bf);


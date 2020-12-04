#pragma once

#include <time.h>
#include <stdbool.h>

#include "httpData.h"
#include "dynamicArray.h"

// Forward declaration
typedef struct HashTable HashTable;

// A better implementation of an LRU cache is to use
// a HashTable<DoubleLL> where the LRU item is always
// at the end of the DLL. I ran out of time, so I
// stuck with a HashTable. I'm also doing a bunch
// of mallocs, so it would be better to write a custom
// pool allocator for my CacheObj's
typedef struct CacheObj {
    char *data;
    time_t timeCreated;
    int timeToLive;
    int lastAccess;
    int headerSize;
    int dataSize;
} CacheObj;

typedef struct CacheKey {
    char url[2048];
    char port[8];
} CacheKey;

// Cache Methods
void cache_add(Header *clientHeader, Header *servHeader, int dataSize, DynamicArray *buff, HashTable *cache);
CacheObj *cache_get(Header *clientHeader, HashTable *cache);

// Cache object and key helpers
void termCacheObj(CacheObj *record); // frees cache memory
int isStale(CacheObj *obj);
int cacheAccessCmp(CacheObj *a, CacheObj *b); // negative if a is older than b

// These are used in the hash table, but they're specific to the cache usage
int keyCmp(CacheKey *a, CacheKey *b); // value comparison: 1 if same, 0 if not
unsigned long long int keyHash(CacheKey *key);
unsigned long long int strHash(char *str);

// Key value pair for cache hash table
#define Record CacheObj
#define Key CacheKey

// must be included after the above #defines
#include "hashTable.h"
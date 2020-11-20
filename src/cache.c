#include "cache.h"

#include <stdlib.h>
#include <string.h>

void cache_add(Header* clientHeader, Header* servHeader, int dataSize, DynamicArray* buff, HashTable* cache) {
  CacheKey* key = malloc(sizeof(CacheKey));
  strcpy(key->url, clientHeader->url);
  strcpy(key->port, clientHeader->port);

  char* data = malloc(sizeof(char) * dataSize);
  strcpy(data, buff->buff);

  CacheObj* obj = malloc(sizeof(CacheObj));
  obj->data = data;
  obj->timeCreated = time(NULL);
  obj->timeToLive = servHeader->timeToLive;  // TODO: Change this to real time to live
  obj->lastAccess = -1;
  obj->headerSize = servHeader->headerLength;

  if (cache->numElem >= cache->maxElem) {
    int numRemoved = ht_removeAll(cache, isStale);
    if (cache->numElem >= cache->maxElem) {
      CacheKey* minKey = ht_findMin(cache, cacheAccessCmp);
      ht_removeKey(cache, minKey);
    }
  }
  ht_insert(cache, key, obj);
}

CacheObj* cache_get(Header* clientHeader, HashTable* cache) {
  // Create a key to see if we've already seen the page
  CacheKey key;
  strcpy(key.url, clientHeader->url);
  strcpy(key.port, clientHeader->port);

  // Check if the key is in the hash table
  if (ht_hasKey(cache, &key)) {
    CacheObj* record = ht_get(cache, &key);

    // Check if stale
    if (record->timeCreated + record->timeToLive < time(NULL))
      ht_removeKey(cache, &key);
    else
      return record;
  }
  return NULL;
}

int keyCmp(CacheKey* a, CacheKey* b) {
  return (strcmp(a->url, b->url) == 0) && (strcmp(a->port, b->port) == 0);
}

unsigned long long int strHash(char* str) {
  // Since I'm using an int, I know that this calculation can overflow if
  // the string is very big, but that's okay. I'm taking the modulo anyways
  // so I don't think it matters.
  // Consider changing to BigInt
  unsigned long long int hash = 7;
  int i = 0, len = strlen(str);
  for (i; i < len; i++) {
    hash = hash * 31 + str[i];
  }
  return hash;
}

unsigned long long int keyHash(CacheKey* key) {
  // Adding these together probably isn't the best idea
  return strHash(key->url) + strHash(key->port);
}

void termCacheObj(CacheObj* obj) {
  free(obj->data);
}

int isStale(CacheObj* obj) {
  return obj->timeCreated + obj->timeToLive < time(NULL);
}

int cacheAccessCmp(CacheObj* a, CacheObj* b) {
  if (a->lastAccess > b->lastAccess)
    return 1;
  else if (a->lastAccess == b->lastAccess)
    return 0;
  else
    return -1;
}
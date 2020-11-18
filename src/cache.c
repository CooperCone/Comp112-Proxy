#include "cache.h"

#include <string.h>
#include <stdlib.h>

int keyCmp(CacheKey *a, CacheKey *b) {
  return (strcmp(a->url, b->url) == 0) && (strcmp(a->port, b->port) == 0);
}

unsigned long long int strHash(char *str) {
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

unsigned long long int keyHash(CacheKey *key) {
  // Adding these together probably isn't the best idea
  return strHash(key->url) + strHash(key->port);
}

void termCacheObj(CacheObj *obj) {
  free(obj->data);
}

int isStale(CacheObj *obj) {
  return obj->timeCreated + obj->timeToLive < time(NULL);
}

int cacheAccessCmp(CacheObj *a, CacheObj *b) {
  if (a->lastAccess > b->lastAccess)
    return 1;
  else if (a->lastAccess == b->lastAccess)
    return 0;
  else
    return -1;
}
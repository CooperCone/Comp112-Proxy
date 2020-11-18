#pragma once

#include "linkedList.h"

typedef struct HashTable {
  LLNode **table;
  int size; // actual size of table, not how many are filled
  int maxElem; // this is specific to the cache implementation, but I just put
               // it here because I didn't want to have to wrap HashTable
  int numElem;
  unsigned long long int (*hashFun)(Key *key);
  int (*keyCmp)(Key *a, Key *b);
  void (*termRecord)(Record *record); // frees memory
} HashTable;

void ht_init(HashTable *table, int maxSize, unsigned long long int (*hashFun)(Key *key), int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record));
void ht_term(HashTable *table); // frees memory
void ht_insert(HashTable *table, Key *key, Record *value);
int ht_hasKey(HashTable *table, Key *key);
Record *ht_get(HashTable *table, Key *key);
int ht_removeAll(HashTable *table, int (*ifRemove)(Record *record)); // remove all that satisfied "ifRemove"
Key *ht_findMin(HashTable *table, int (*recordCmp)(Record *a, Record *b)); // recordCmp: -1 if a < b, +1 if a > b
void ht_removeKey(HashTable *table, Key *key);
void ht_clear(HashTable *table);
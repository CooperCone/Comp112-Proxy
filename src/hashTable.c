#include "hashTable.h"

#include <stdlib.h>

void ht_init(HashTable *table, int maxSize, unsigned long long int (*hashFun)(Key *key), int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record)) {
  // TODO: multiplying by 1.5 is okay, but a better
  // solution would be to pick the next larger prime number
  table->size = (int)(maxSize * 1.5);
  table->maxElem = maxSize;
  table->numElem = 0;
  table->table = malloc(sizeof(LLNode*) * table->size);
  table->hashFun = hashFun;
  table->keyCmp = keyCmp;
  table->termRecord = termRecord;
  int i;
  for (i = 0; i < table->size; i++)
    table->table[i] = NULL;
}

// terminates hash table and frees memory
void ht_term(HashTable *table) {
  ht_clear(table);
  free(table->table);
}

void ht_insert(HashTable *table, Key *key, Record *record) {
  // Handle duplicates
  if (ht_hasKey(table, key))
    ht_removeKey(table, key);
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  LLNode *newNode = malloc(sizeof(LLNode));
  newNode->key = key;
  newNode->record = record;
  newNode->next = table->table[index];
  table->table[index] = newNode;
  table->numElem++;
}

int ht_hasKey(HashTable *table, Key *key) {
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  LLNode *node = table->table[index];
  while (node != NULL) {
    if (table->keyCmp(key, node->key))
      return 1;
    node = node->next;
  }
  return 0;
}

// Must check if it has key before this,
// otherwise you'll get NULL
Record *ht_get(HashTable *table, Key *key) {
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  LLNode *node = table->table[index];
  while (node != NULL) {
    if (table->keyCmp(key, node->key))
      return node->record;
    node = node->next;
  }
  return NULL;
}

int ht_removeAll(HashTable *table, int (*ifRemove)(Record *record)) {
  int i, numRemoved = 0;
  for (i = 0; i < table->size; i++) {
    table->table[i] = ll_removeAll(table->table[i], &numRemoved, ifRemove, table->termRecord);
  }
  table->numElem -= numRemoved;
  return numRemoved;
}



Key *ht_findMin(HashTable *table, int (*recordCmp)(Record *a, Record *b)) {
  LLNode *min = NULL;
  int i;
  for (i = 0; i < table->size; i++) {
    if (min == NULL && table->table[i] != NULL) {
      min = table->table[i];
    }
    LLNode *cur = table->table[i];
    while (cur != NULL) {
      if (recordCmp(cur->record, min->record) == -1)
        min = cur;
      cur = cur->next;
    }
  }
  return min->key;
}

void ht_removeKey(HashTable *table, Key *key) {
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  table->table[index] = ll_remove(table->table[index], key, table->keyCmp, table->termRecord);
  table->numElem--;
}

int clearFunc(Record *record) {
  return 1;
}

void ht_clear(HashTable *table) {
  ht_removeAll(table, clearFunc);
}
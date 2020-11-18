#pragma once

#ifndef Key
#define Key void*
#endif

#ifndef Record
#define Record void*
#endif

typedef struct LLNode {
  Key *key;
  Record *record;
  struct LLNode *next;
} LLNode;

LLNode *ll_removeAll(LLNode *node, int *nRemoved, int (*ifRemove)(Record *record), void (*termRecord)(Record *record));
LLNode *ll_remove(LLNode *node, Key *key, int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record));

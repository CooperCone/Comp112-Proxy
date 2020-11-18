#include "linkedList.h"

#include <stdlib.h>

LLNode *ll_removeAll(LLNode *node, int *nRemoved, int (*ifRemove)(Record *record), void (*termRecord)(Record *record)) {
  if (node == NULL)
    return NULL;
  if (ifRemove(node->record)) {
    (*nRemoved)++;
    LLNode *next = node->next;
    termRecord(node->record);
    free(node->record);
    free(node->key);
    free(node);
    return ll_removeAll(next, nRemoved, ifRemove, termRecord);
  }
  node->next = ll_removeAll(node->next, nRemoved, ifRemove, termRecord);
  return node;
}

LLNode *ll_remove(LLNode *node, Key *key, int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record)) {
  if (node == NULL)
    return node;
  if (keyCmp(key, node->key)) {
    LLNode *next = node->next;
    termRecord(node->record);
    free(node->record);
    free(node->key);
    free(node);
    return next;
  } else {
    LLNode *new = ll_remove(node->next, key, keyCmp, termRecord);
    node->next = new;
    return node;
  }
}

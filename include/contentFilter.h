#pragma once

#include <stdbool.h>

#include "dynamicArray.h"
#include "httpData.h"

typedef struct ContentFilter {
    struct ContentFilter *trie[26]; // one for each letter
    bool isComplete;
} ContentFilter;

ContentFilter *cf_create(char *fileName);
ContentFilter *cf_init();
void cf_print(ContentFilter *filter);
bool cf_searchText(ContentFilter *filter, char *text, int size);
bool cf_searchString(ContentFilter *filter, char *string);
void cf_delete(ContentFilter *filter);

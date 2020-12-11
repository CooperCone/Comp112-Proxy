#include "contentFilter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

ContentFilter *cf_create(char *fileName) {
    ContentFilter *base = cf_init();

    FILE *file = fopen(fileName, "r");

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strlen(line) - 2] = '\0'; // strip new line

        char *cur = line;
        ContentFilter *trie = base;
        while (*cur != '\0') {
            char c = tolower(*cur);
            int idx = c - 97; // convert 'a' to 0
            
            if (trie->trie[idx] == NULL) {
                trie->trie[idx] = cf_init();
            }
            cur++;
            trie = trie->trie[idx];
        }
        trie->isComplete = true;
    }

    fclose(file);
    return base;
}


ContentFilter *cf_init() {
    ContentFilter *filter = malloc(sizeof(ContentFilter));
    filter->isComplete = false;
    int i;
    for (i = 0; i < 26; i++) {
        filter->trie[i] = NULL;
    }
    return filter;
}


void cf_print(ContentFilter *filter) {
    int i;
    for (i = 0; i < 26; i++) {
        if (filter->trie[i] != NULL) {
            printf("%c", i + 97);

            if (filter->trie[i]->isComplete)
                printf("\n");

            cf_print(filter->trie[i]);
        }
    }
}


bool cf_searchText(ContentFilter *filter, char *text, int size) {
    char *bodyCopy = malloc(size);
    memcpy(bodyCopy, text, size);
    char delims[] = " <>";
    char *token = strtok(bodyCopy, delims);
    while (token != NULL) {
        // printf("Token: %s\n", token);
        if (cf_searchString(filter, token)) {
            return true;
            // printf("Found String: %s\n", token);
        }

        token = strtok(NULL, delims);
    }
    free(bodyCopy);
}


bool cf_searchString(ContentFilter *filter, char *string) {
    char *cur = string;
    while (*cur != '\0') {
        ContentFilter *trie = filter;
        char *matched = cur;
        while (*matched != '\0') {
            char c = tolower(*matched);
            int idx = c - 97;

            // Check for invalid character in string
            if (idx < 0 || idx > 25)
                break;
            else if (trie->trie[idx] == NULL)
                break;
            else if (trie->trie[idx]->isComplete)
                return true;
            
            trie = trie->trie[idx];
            matched++;
        }

        cur++;
    }
    return false;
}


void cf_delete(ContentFilter *filter) {
    if (filter == NULL)
        return;
    else {
        int i;
        for (i = 0; i < 26; i++) {
            cf_delete(filter->trie[i]);
        }
    }
    free(filter);
}
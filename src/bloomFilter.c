#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bloomFilter.h"

unsigned long hash1(char *str);
unsigned long hash2(char *str);
unsigned long hash3(char *str);
unsigned long hash4(char *str);
unsigned long hash5(char *str);
unsigned long hash6(char *str);
unsigned long hash7(char *str);

BloomFilter *bf_create() {
    BloomFilter *bf;

    bf = malloc(sizeof(BloomFilter));
    bf->bitArray = malloc(sizeof(char) * bf_m);
    bf->numElements = 0;

    for (int i = 0; i < bf_m; ++i) {
        bf->bitArray[i] = 0;
    }

    return bf;
}

void bf_add(BloomFilter *bf, char *str) {
    unsigned long h1, h2, h3, h4, h5, h6, h7;

    h1 = hash1(str) % bf_m;
    h2 = hash2(str) % bf_m;
    h3 = hash3(str) % bf_m;
    h4 = hash4(str) % bf_m;
    h5 = hash5(str) % bf_m;
    h6 = hash6(str) % bf_m;
    h7 = hash7(str) % bf_m;

    bf->bitArray[h1] = 1;
    bf->bitArray[h2] = 1;
    bf->bitArray[h3] = 1;
    bf->bitArray[h4] = 1;
    bf->bitArray[h5] = 1;
    bf->bitArray[h6] = 1;
    bf->bitArray[h7] = 1;

    // printf("h1: %lu\nh2: %lu\nh3: %lu\nh4: %lu\nh5: %lu\nh6: %lu\nh7: %lu\n", h1, h2, h3, h4, h5, h6, h7);

    ++bf->numElements;
}

bool bf_query(BloomFilter *bf, char *str) {
    unsigned long h1, h2, h3, h4, h5, h6, h7;

    h1 = hash1(str) % bf_m;
    h2 = hash2(str) % bf_m;
    h3 = hash3(str) % bf_m;
    h4 = hash4(str) % bf_m;
    h5 = hash5(str) % bf_m;
    h6 = hash6(str) % bf_m;
    h7 = hash7(str) % bf_m;

    return (bf->bitArray[h1] && bf->bitArray[h2] && bf->bitArray[h3] &&
            bf->bitArray[h4] && bf->bitArray[h5] && bf->bitArray[h6] &&
            bf->bitArray[h7]);
}

void bf_delete(BloomFilter *bf) {
    free(bf->bitArray);
    free(bf);
}

// The C Programming Language (Kernighan & Ritchie), Section 6.6
unsigned long hash1(char *str) {
    unsigned long hash;

    for (hash = 0; *str != '\0'; str++)
        hash = *str + 31 * hash;

    return hash;
}

// djb2 (Dan Bernstein)
unsigned long hash2(char *str) {
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

// sdbm
unsigned long hash3(char *str) {
    unsigned long hash = 0;
    int c;

    while (c = *str++)
        hash = c + (hash << 6) + (hash << 16) - hash;

    return hash;
}

// Jenkins One At A Time
unsigned long hash4(char *str)
{
    unsigned long hash, i;
    for(hash = i = 0; i < strlen(str); ++i)
    {
        hash += str[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

// Robert Sedgwicks
unsigned long hash5(char* str) {
    unsigned long b = 378551;
    unsigned long a = 63689;
    unsigned long hash = 0;
    unsigned long i = 0;

    for (i = 0; i < strlen(str); ++str, ++i) {
        hash = hash * a + (*str);
        a = a * b;
    }

   return hash;
}

// Donald Knuth
unsigned long hash6(char* str) {
    unsigned long hash = strlen(str);
    unsigned long i = 0;

    for (i = 0; i < strlen(str); ++str, ++i) {
        hash = ((hash << 5) ^ (hash >> 27)) ^ (*str);
    }

    return hash;
}

// Justin Sobel
unsigned long hash7(char* str) {
    unsigned long hash = 1315423911;
    unsigned long i = 0;

    for (i = 0; i < strlen(str); ++str, ++i) {
        hash ^= ((hash << 5) + (*str) + (hash >> 2));
    }

   return hash;
}


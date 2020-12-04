#include <bloomFilter.h>

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

}

void bf_delete(BloomFilter *bf) {
    free(bf->bitArray);
    free(bf);
}
#include "httpData.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "zlib.h"

char *uncompressGzip(char *outBuff, int *outSize, char *inBuff, int inSize) {
    int chunkSize = *outSize;
    int uncomSize = 0;
    int uncomMaxSize = chunkSize;
    char *chunk = malloc(sizeof(char) * chunkSize);

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = inSize;
    stream.next_in = (Bytef*)inBuff;
    stream.avail_out = (uInt)chunkSize;
    stream.next_out = (Bytef*)chunk;

    int res = inflateInit2(&stream, 16);
    if (res != Z_OK) {
        free(chunk);
        return NULL;
    }

    // Continue to inflate until we consummed all input
    do {
        res = inflate(&stream, Z_SYNC_FLUSH);
        memcpy(outBuff + uncomSize, chunk, chunkSize);
        uncomSize += chunkSize;

        // resize outBuff to accomodate increased size
        // from compressed to uncompressed
        if (uncomSize + chunkSize >= uncomMaxSize) {
            uncomMaxSize *= 2;
            outBuff = realloc(outBuff, uncomMaxSize);
        }
        stream.avail_out = (uInt)chunkSize;
        stream.next_out = (Bytef*)chunk;
    } while (res == Z_OK);

    if (res != Z_STREAM_END) {
        fprintf(stderr, "Gzip inflate error: %d\n", res);
        inflateEnd(&stream);
        free(chunk);
        return NULL;
    }

    res = inflateEnd(&stream);
    if (res != Z_OK) {
        free(chunk);
        return NULL;
    }

    // Uncompress worked
    *outSize = stream.total_out;
    free(chunk);
    return outBuff;
}
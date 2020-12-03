#include "httpData.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "zlib.h"


ConnectionData *createConnectionData(int first, int second) {
    ConnectionData *data = malloc(sizeof(ConnectionData));
    data->first = first;
    data->second = second;
    da_init(&(data->buffer), 2048);
    return data;
}


void termConnectionData(ConnectionData *data) {
    da_term(&(data->buffer));
    free(data);
}


bool connSockCmp(ConnectionData *data, int *sock) {
    return data->first == *sock || data->second == *sock;
}


ClientData *createClientData(int sock) {
    ClientData *data = malloc(sizeof(ClientData));
    data->sock = sock;
    da_init(&(data->buffer), 2048);
    return data;
}

void termClientData(ClientData *data) {
    da_term(&(data->buffer));
    free(data);
}


bool clientSockCmp(ClientData *data, int *sock) {
    return data->sock == *sock;
}


ServerData *createServerData(int sock, char *domain) {
    ServerData *data = malloc(sizeof(ServerData));
    data->sock = sock;
    data->domain = malloc(strlen(domain) + 1);
    strcpy(data->domain, domain);
    da_init(&(data->buffer), 2048);
    return data;
}


void termServerData(ServerData *data) {
    free(data->domain);
    da_term(&(data->buffer));
    free(data);
    return;
}


bool servSockCmp(ServerData *data, int *sock) {
    return data->sock == *sock;
}


bool servDomainCmp(ServerData *data, char *domain) {
    return strcmp(data->domain, domain) == 0;
}


PrefetchData *createPrefetchData(char *domain, DynamicArray *buff) {
    PrefetchData *data = malloc(sizeof(PrefetchData));
    data->url = malloc(strlen(domain) + 1);
    strcpy(data->url, domain);
    data->content = malloc(buff->size);
    memcpy(data->content, buff->buff, buff->size);
    data->contentLen = buff->size;
    return data;
}


void termPrefetchData(PrefetchData *data) {
    free(data->url);
    free(data->content);
    free(data);
}


bool prefetchUrlCmp(PrefetchData *data, char *url) {
    return strcmp(data->url, url) == 0;
}


DataList *addData(DataList *list, void *data) {
    DataList *newData = malloc(sizeof(DataList));
    newData->data = data;
    newData->next = list;
    return newData;
}


DataList *findData(DataList *list, bool (*cmp)(void *a, void *b), void *data) {
    if (list == NULL)
        return list;
    else if (cmp(list->data, data))
        return list;
    else
        return findData(list->next, cmp, data);
}


DataList *deleteData(DataList *list, bool (*cmp)(void *a, void *b), void *data, void (*termData)(void *data)) {
    if (list == NULL)
        return list;
    else if (cmp(list->data, data)) {
        DataList *next = list->next;
        termData(list->data);
        free(list);
        return next;
    } else {
        DataList *next = deleteData(list->next, cmp, data, termData);
        list->next = next;
        return list;
    }
}


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
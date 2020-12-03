#pragma once

#include <stdbool.h>
#include "dynamicArray.h"

typedef enum {
    GET,
    CONNECT,
    POST
} Method;

typedef enum {
    NO_ENCODE,
    GZIP
} Encoding;

typedef struct {
    Method method;
    char url[2048];
    char port[8];
    char domain[128];
    int timeToLive;
    int headerLength;
    bool chunkedEncoding;
    int contentLength;
    Encoding encoding;
} Header;

char* uncompressGzip(char *outBuff, int *outSize, char *inBuff, int inSize);

typedef struct {
    int first;
    int second;
    DynamicArray buffer;
} ConnectionData;

ConnectionData *createConnectionData(int first, int second);
void termConnectionData(ConnectionData *data);
bool connSockCmp(ConnectionData *data, int *sock);

typedef struct {
    int sock;
    DynamicArray buffer;
} ClientData;

ClientData *createClientData(int sock);
void termClientData(ClientData *data);
bool clientSockCmp(ClientData *data, int *sock);

typedef struct {
    char *domain;
    int sock;
    DynamicArray buffer;
} ServerData;

ServerData *createServerData(int sock, char *domain);
void termServerData(ServerData *data);
bool servSockCmp(ServerData *data, int *sock);
bool servDomainCmp(ServerData *data, char *domain);

typedef struct {
    char *url;
    char *content;
    int contentLen;
} PrefetchData;

PrefetchData *createPrefetchData(char *domain, DynamicArray *buff);
void termPrefetchData(PrefetchData *data);
bool prefetchUrlCmp(PrefetchData *data, char *url);

typedef struct DataList {
    void *data;
    struct DataList *next;
} DataList;

#define CmpFunc bool (*)(void *, void *)
#define TermFunc void (*)(void *)

DataList *addData(DataList *list, void *data);
DataList *findData(DataList *list, bool (*cmp)(void *a, void *b), void *data);
int dataListLength(DataList *list);
DataList *deleteData(DataList *list, bool (*cmp)(void *a, void *b), void *data, void (*termData)(void *data));

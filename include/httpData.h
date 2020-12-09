#pragma once

#include <stdbool.h>
#include <time.h>
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
    time_t age;
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


// This is the overall data list data structure.
// It has the payload void* and a next pointer,
// making it a singly linked list. The data
// field will be set to one of the structs
// above
typedef struct DataList {
    void *data;
    struct DataList *next;
} DataList;

// These macros make it easy to typecast the
// data functions above to these general uses
#define CmpFunc bool (*)(void *, void *)
#define TermFunc void (*)(void *)

// To perform functionality like find, and delete, we need
// other functionality inherent to the data. Because of this,
// the user can specify their own comparision functions
// and their own terminate functions.
// The comparison will take 2 data points and return true
// if they're the same. These are value and not reference
// comparisons.
// Terminate functions will just delete the data. They will
// be responsible for freeing memory.

// Each struct above has defined a create, compare, and delete
// function. The create is typically used when calling add,
// and the compare functions will be used in find and delete.
DataList *addData(DataList *list, void *data);
DataList *findData(DataList *list, bool (*cmp)(void *a, void *b), void *data);
int dataListLength(DataList *list);
DataList *deleteData(DataList *list, bool (*cmp)(void *a, void *b), void *data, void (*termData)(void *data));

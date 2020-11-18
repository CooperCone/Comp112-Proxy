#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "dynamicArray.h"

/************** Parser Data **************/
typedef enum {
  GET
} Method;

typedef struct {
  Method method;
  char url[2048];
  char domain[128];
  int headerLength;
  bool chunkedEncoding;
  int contentLength;
} Header;

/****************************************/


/************** Cache Data *************/
// A better implementation of an LRU cache is to use
// a HashTable<DoubleLL> where the LRU item is always
// at the end of the DLL. I ran out of time, so I
// stuck with a HashTable. I'm also doing a bunch
// of mallocs, so it would be better to write a custom
// pool allocator for my CacheObj's
typedef struct CacheObj {
  char *data;
  time_t timeCreated;
  int timeToLive;
  int lastAccess;
} CacheObj;

typedef struct CacheKey {
  char url[101];
  char port[6];
} CacheKey;

void termCacheObj(CacheObj *record); // frees cache memory
int isStale(CacheObj *obj);
int cacheAccessCmp(CacheObj *a, CacheObj *b); // negative if a is older than b

// These are used in the hash table, but they're specific to the cache usage
int keyCmp(CacheKey *a, CacheKey *b); // value comparison: 1 if same, 0 if not
unsigned long long int keyHash(CacheKey *key);
unsigned long long int strHash(char *str);

// Key value pair for cache hash table
#define Record CacheObj
#define Key CacheKey
/*****************************************/


/************** Hash Table ***************/
typedef struct LLNode {
  Key *key;
  Record *record;
  struct LLNode *next;
} LLNode;

LLNode *ll_removeAll(LLNode *node, int *nRemoved, int (*ifRemove)(Record *record), void (*termRecord)(Record *record));
LLNode *ll_remove(LLNode *node, Key *key, int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record));

typedef struct HashTable {
  LLNode **table;
  int size; // actual size of table, not how many are filled
  int maxElem; // this is specific to the cache implementation, but I just put
               // it here because I didn't want to have to wrap HashTable
  int numElem;
  unsigned long long int (*hashFun)(Key *key);
  int (*keyCmp)(Key *a, Key *b);
  void (*termRecord)(Record *record); // frees memory
} HashTable;

void ht_init(HashTable *table, int maxSize, unsigned long long int (*hashFun)(Key *key), int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record));
void ht_term(HashTable *table); // frees memory
void ht_insert(HashTable *table, Key *key, Record *value);
int ht_hasKey(HashTable *table, Key *key);
Record *ht_get(HashTable *table, Key *key);
int ht_removeAll(HashTable *table, int (*ifRemove)(Record *record)); // remove all that satisfied "ifRemove"
Key *ht_findMin(HashTable *table, int (*recordCmp)(Record *a, Record *b)); // recordCmp: -1 if a < b, +1 if a > b
void ht_removeKey(HashTable *table, Key *key);
void ht_clear(HashTable *table);
/*******************************************/


/************ Proxy Helpers ************/
int createClientSock(const char *port);
int createServerSock(char *domain, char *port);
bool parseHeader(Header *outHeader, DynamicArray *buff);
void socketError(char *funcName);
/******************************************/


int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments!\n");
    fprintf(stderr, "Try: ./a.out <Port_Number>");
    return 1;
  }

  // TODO: Allow multiple client connections
  int clientSock, clientConn;

  DynamicArray getBuff, reqBuff;
  da_init(&getBuff, 2048);
  da_init(&reqBuff, 2048);

  HashTable cache;
  ht_init(&cache, 10, keyHash, keyCmp, termCacheObj);

  if ((clientSock = createClientSock(argv[1])) == -1)
      return 1;

  int i;
  for(i = 0; i < 1; i++) { // look for client connections forever

    { // Initalize Client Connection
      struct sockaddr_in connAddr;
      socklen_t connSize = sizeof(struct sockaddr_in);
      clientConn = accept(clientSock, (struct sockaddr*)&connAddr, &connSize);
      readAll(clientConn, &getBuff);
    } // Initialize Client Connection

    Header clientHeader;
    parseHeader(&clientHeader, &getBuff);

    CacheKey key;
    strcpy(key.url, clientHeader.url);
    strcpy(key.port, "80");

    // Check if the key is in the hash table
    if (ht_hasKey(&cache, &key)) {
      CacheObj *record = ht_get(&cache, &key);

      // Check if stale
      if (record->timeCreated + record->timeToLive < time(NULL)) {
        ht_removeKey(&cache, &key);
      }
      else {
        char *cur = record->data;

        { // Add age to header. This is a really bad way to do this
          for (;;) {
            char c = *cur;
            if (c == '\n') {
              char txt[40];
              sprintf(txt, "\nAge: %d\r\n", (int)time(NULL) - (int)record->timeCreated);
              write(clientConn, txt, strlen(txt));
              cur++;
              write(clientConn, cur, strlen(cur));
              break;
            }
            else {
              write(clientConn, &c, 1);
              cur++;
            }
          }
        } // Add age to header

        record->lastAccess = time(NULL);
        close(clientConn);
        da_clear(&getBuff);
        continue;
      }
    }

    // If we get to this point, either the key wasn't in the cache, or it was stale
    // So connect to the server, and send them the request
    int serverSock;
    if ((serverSock = createServerSock(clientHeader.domain, "80")) == -1) {
      close(clientConn);
      close(clientSock);
      return 1;
    }
    write(serverSock, getBuff.buff, getBuff.size);
    int responseSize = readAll(serverSock, &reqBuff);

    Header serverHeader;
    parseHeader(&serverHeader, &reqBuff);

    // A couple things could happen here.
    // If content length set, then just read content length
    // If chunked encoding, find chunk size and read chunks
    // Right now we're only handling chunked encoding
    int start = serverHeader.headerLength;
    while (serverHeader.chunkedEncoding) {
      char *endOfChunkLine = strstr(reqBuff.buff + start, "\r\n");
      int size = endOfChunkLine - (reqBuff.buff + start);
      
      char chunkSizeBuff[20];
      memcpy(chunkSizeBuff, (reqBuff.buff + start), size);
      chunkSizeBuff[size] = '\0';

      int chunkSize = (int)strtol(chunkSizeBuff, NULL, 16);

      if (chunkSize == 0)
        break;

      start += 2 + size;

      while (reqBuff.size - start < chunkSize) {
        int bytesRead = readAll(serverSock, &reqBuff);
        if (bytesRead == -1) {
          break;
        }
        responseSize += bytesRead;
      }
      start += chunkSize + 2;
    }    
  
    { // add to cache
      CacheKey *key = malloc(sizeof(CacheKey));
      strcpy(key->url, clientHeader.url);
      strcpy(key->port, "80");

      char *data = malloc(sizeof(char) * responseSize);
      strcpy(data, reqBuff.buff);

      CacheObj *obj = malloc(sizeof(CacheObj));
      obj->data = data;
      obj->timeCreated = time(NULL);
      obj->timeToLive = 60; // TODO: Change this to real time to live
      obj->lastAccess = -1;

      if (cache.numElem >= cache.maxElem) {
        int numRemoved = ht_removeAll(&cache, isStale);
        if (cache.numElem >= cache.maxElem) {
          CacheKey *minKey = ht_findMin(&cache, cacheAccessCmp);
          ht_removeKey(&cache, minKey);
        }
      }
      ht_insert(&cache, key, obj);
    }
    write(clientConn, reqBuff.buff, reqBuff.size);
    close(serverSock);
    close(clientConn);
    da_clear(&getBuff);
    da_clear(&reqBuff);
  }

  // terminate buffers and free memory
  da_term(&reqBuff);
  da_term(&getBuff);
  close(clientSock);
  return 0;
}


/************ Proxy Helpers ****************/
int createClientSock(const char *port) {
    struct addrinfo hints, *proxyAddr;

    memset(&hints, 0, sizeof(struct sockaddr_in));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(NULL, port, &hints, &proxyAddr);

    int clientSock;
    if ((clientSock = socket(proxyAddr->ai_family, proxyAddr->ai_socktype, 0)) == -1) {
      socketError("Socket");
      return -1;
    }
    int option = 1;
    setsockopt(clientSock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    if (bind(clientSock, proxyAddr->ai_addr, proxyAddr->ai_addrlen) == -1) {
      socketError("Bind");
      close(clientSock);
      return -1;
    }

    if (listen(clientSock, 5) == -1) {
      socketError("Listen");
      close(clientSock);
      return -1;
    }

    freeaddrinfo(proxyAddr);
    return clientSock;
}

int createServerSock(char *domain, char *port) {
  struct addrinfo hints, *serverInfo;

  memset(&hints, 0, sizeof(struct sockaddr_in));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int serverSock;
  int status;
  if ((status = getaddrinfo(domain, port, &hints, &serverInfo)) != 0)
    fprintf(stderr, "Addr Error: %s\n", gai_strerror(status));

  if ((serverSock = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol)) == -1) {
    socketError("Socket");
    return -1;
  }
  int option = 1;
  setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  if (connect(serverSock, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
    socketError("Connect");
    close(serverSock);
    return -1;
  }

  freeaddrinfo(serverInfo);
  return serverSock;
}

bool parseHeader(Header *outHeader, DynamicArray *buff) {
  outHeader->contentLength = -1;
  outHeader->chunkedEncoding = false;
  int headerLen = 0;

  char *copiedStr = malloc(buff->size);
  memcpy(copiedStr, buff->buff, buff->size);

  char *line = strtok(copiedStr, "\r\n");
  while (line) {
    // printf("%s\n", line);

    headerLen += (strlen(line) + 2);

    if (strstr(line, "GET ") != NULL) {
      outHeader->method = GET;

      char *getUrl = line + 4;
      char *urlEnd;
      if ((urlEnd = strstr(getUrl, " ")) == NULL) {
        free(copiedStr);
        return false;
      }

      size_t urlLen = urlEnd - getUrl;

      memcpy(outHeader->url, getUrl, urlLen);
      outHeader->url[urlLen] = '\0';
    } 
    
    else if (strstr(line, "Host: ") != NULL) {
      char *domain = line + 6;
      strcpy(outHeader->domain, domain);
    }

    else if (strstr(line, "Transfer-Encoding: chunked") != NULL) {
      outHeader->chunkedEncoding = true;
    }

    else if (outHeader->chunkedEncoding && atoi(line) != 0) {
      outHeader->headerLength = headerLen - strlen(line);
      free(copiedStr);
      return true;
    }
    
    else if (strlen(line) <= 1) {
      outHeader->headerLength = headerLen + 2;
      free(copiedStr);
      return true; // reached header end
    }

    // else
    //   printf("Unknown Header: %s\n", line);

    line = strtok(NULL, "\r\n");
  }
  free(copiedStr);
  return true;
}

// TODO: add in variadic arg for sd's to close.
// This will make it much easier to exit.
void socketError(char *funcName) {
  fprintf(stderr, "%s Error: %s\n", funcName, strerror(errno));
  fprintf(stderr, "Exiting\n");
}
/****************************************************/


/************* Cache Data ***********************/
int keyCmp(CacheKey *a, CacheKey *b) {
  return (strcmp(a->url, b->url) == 0) && (strcmp(a->port, b->port) == 0);
}

unsigned long long int strHash(char *str) {
  // Since I'm using an int, I know that this calculation can overflow if
  // the string is very big, but that's okay. I'm taking the modulo anyways
  // so I don't think it matters.
  // Consider changing to BigInt
  unsigned long long int hash = 7;
  int i = 0, len = strlen(str);
  for (i; i < len; i++) {
    hash = hash * 31 + str[i];
  }
  return hash;
}

unsigned long long int keyHash(CacheKey *key) {
  // Adding these together probably isn't the best idea
  return strHash(key->url) + strHash(key->port);
}

void termCacheObj(CacheObj *obj) {
  free(obj->data);
}

int isStale(CacheObj *obj) {
  return obj->timeCreated + obj->timeToLive < time(NULL);
}

int cacheAccessCmp(CacheObj *a, CacheObj *b) {
  if (a->lastAccess > b->lastAccess)
    return 1;
  else if (a->lastAccess == b->lastAccess)
    return 0;
  else
    return -1;
}
/************************************************/


/******************* Hash Table ******************/
void ht_init(HashTable *table, int maxSize, unsigned long long int (*hashFun)(Key *key), int (*keyCmp)(Key *a, Key *b), void (*termRecord)(Record *record)) {
  // TODO: multiplying by 1.5 is okay, but a better
  // solution would be to pick the next larger prime number
  table->size = (int)(maxSize * 1.5);
  table->maxElem = maxSize;
  table->numElem = 0;
  table->table = malloc(sizeof(Record) * table->size);
  table->hashFun = hashFun;
  table->keyCmp = keyCmp;
  table->termRecord = termRecord;
  int i;
  for (i = 0; i < table->size; i++)
    table->table[i] = NULL;
}

// terminates hash table and frees memory
void ht_term(HashTable *table) {
  ht_clear(table);
  free(table->table);
}

void ht_insert(HashTable *table, Key *key, Record *record) {
  // Handle duplicates
  if (ht_hasKey(table, key))
    ht_removeKey(table, key);
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  LLNode *newNode = malloc(sizeof(LLNode));
  newNode->key = key;
  newNode->record = record;
  newNode->next = table->table[index];
  table->table[index] = newNode;
  table->numElem++;
}

int ht_hasKey(HashTable *table, Key *key) {
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  LLNode *node = table->table[index];
  while (node != NULL) {
    if (table->keyCmp(key, node->key))
      return 1;
    node = node->next;
  }
  return 0;
}

// Must check if it has key before this,
// otherwise you'll get NULL
Record *ht_get(HashTable *table, Key *key) {
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  LLNode *node = table->table[index];
  while (node != NULL) {
    if (table->keyCmp(key, node->key))
      return node->record;
    node = node->next;
  }
  return NULL;
}

int ht_removeAll(HashTable *table, int (*ifRemove)(Record *record)) {
  int i, numRemoved = 0;
  for (i = 0; i < table->size; i++) {
    table->table[i] = ll_removeAll(table->table[i], &numRemoved, ifRemove, table->termRecord);
  }
  table->numElem -= numRemoved;
  return numRemoved;
}

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

Key *ht_findMin(HashTable *table, int (*recordCmp)(Record *a, Record *b)) {
  LLNode *min = NULL;
  int i;
  for (i = 0; i < table->size; i++) {
    if (min == NULL && table->table[i] != NULL) {
      min = table->table[i];
    }
    LLNode *cur = table->table[i];
    while (cur != NULL) {
      if (recordCmp(cur->record, min->record) == -1)
        min = cur;
      cur = cur->next;
    }
  }
  return min->key;
}

void ht_removeKey(HashTable *table, Key *key) {
  unsigned long long int hash = table->hashFun(key);
  int index = hash % table->size;
  table->table[index] = ll_remove(table->table[index], key, table->keyCmp, table->termRecord);
  table->numElem--;
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

int clearFunc(Record *record) {
  return 1;
}

void ht_clear(HashTable *table) {
  ht_removeAll(table, clearFunc);
}
/***************************************************/

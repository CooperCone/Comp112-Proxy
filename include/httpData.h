#pragma once

#include <stdbool.h>

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


char *uncompressGzip(char *outBuff, int *outSize, char *inBuff, int inSize);
#pragma once

typedef enum {
  GET,
  CONNECT
} Method;

typedef struct {
  Method method;
  char url[2048];
  char port[8];
  char domain[128];
  int timeToLive;
  int headerLength;
  bool chunkedEncoding;
  int contentLength;
} Header;
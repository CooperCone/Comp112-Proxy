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


int createClientSock(const char *port);
void socketError(char *funcName);

/************* Socket Buffer ************/
// TODO: Rename to Dynamic Array
// This started as just a normal buffer,
// but I added in automatic resizing that now
// makes it a dynamic array.
typedef struct DynamicArray {
  char *buff;
  int size, maxSize;
} DynamicArray;

int readAll(int sd, DynamicArray *buffer);
void da_init(DynamicArray *buffer, int maxSize);
void da_clear(DynamicArray *buffer);
void da_term(DynamicArray *buffer);
/**************************************/


int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments!\n");
    fprintf(stderr, "Try: ./main <Port_Number>");
  }

  int clientSock, clientConn;
  char *port = argv[1];

  DynamicArray getBuff;

  da_init(&getBuff, 2048);

  if ((clientSock = createClientSock(port)) == -1)
    return 1;

  { // Initialize Client Connections
    struct sockaddr_in connAddr;
    socklen_t connSize = sizeof(struct sockaddr_in);

    clientConn = accept(clientSock, (struct sockaddr*)&connAddr, &connSize);

    readAll(clientConn, &getBuff);
  } // Initialize Client Connections

  write(1, getBuff.buff, getBuff.size);

  close(clientConn);
  close(clientSock);

  return 0;
}


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


void socketError(char *funcName) {
  fprintf(stderr, "%s Error: %s\n", funcName, strerror(errno));
  fprintf(stderr, "Exiting\n");
}




 /************ Socket Buffer ********************/
int readAll(int sd, DynamicArray *buffer) {
  int totalRead = 0;

  for(;;) {
    if (buffer->size + 1024 > buffer->maxSize) {
      // resize buff
      buffer->maxSize *= 2;
      buffer->buff = realloc(buffer->buff, buffer->maxSize * sizeof(char));
    }

    int bytesRead = read(sd, (buffer->buff + buffer->size), 1024);

    if (bytesRead == -1) {
      socketError("Read");
      return -1;
    }

    buffer->size += bytesRead;
    totalRead += bytesRead;

    if (bytesRead < 1024)
      break;
  }

  return totalRead;
}

void da_init(DynamicArray *buffer, int size) {
  buffer->buff = malloc(size * sizeof(char));
  buffer->size = 0;
  buffer->maxSize = size;
}

void da_clear(DynamicArray *buffer) {
  buffer->size = 0;
  memset(buffer->buff, 0, buffer->maxSize);
}

void da_term(DynamicArray *buffer) {
  free(buffer->buff);
}
/***********************************************/

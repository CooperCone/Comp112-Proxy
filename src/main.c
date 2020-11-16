#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#include "dynamicArray.h"


int createClientSock(const char *port);
void socketError(char *funcName);


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

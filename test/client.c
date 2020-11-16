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

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments!\n");
    fprintf(stderr, "Try: ./client <Port_Number>");
  }

  int sock;
  char *port = argv[1];

  char msg[] = "This is a message!\n";

  struct addrinfo hints, *proxyAddr;
  memset(&hints, 0, sizeof(struct sockaddr_in));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  getaddrinfo(NULL, port, &hints, &proxyAddr);

  if ((sock = socket(proxyAddr->ai_family, proxyAddr->ai_socktype, 0)) == -1) {
    printf("Client socket error.\n");
    return -1;
  }

  if (connect(sock, proxyAddr->ai_addr, proxyAddr->ai_addrlen) == -1) {
    printf("Client connect errof.\n");
    close(sock);
    return -1;
  }

  freeaddrinfo(proxyAddr);

  write(sock, msg, strlen(msg));

  close(sock);
  return 0;
}
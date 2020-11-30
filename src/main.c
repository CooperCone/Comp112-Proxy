#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "cache.h"
#include "dynamicArray.h"
#include "httpData.h"

/************ Proxy Helpers ************/
int createClientSock(const char* port);
int createServerSock(char* domain, char* port);
bool parseHeader(Header* outHeader, DynamicArray* buff);
int readBody(int sock, Header* header, DynamicArray* buffer);
void socketError(char* funcName);
/******************************************/

#define MAX_EVENTS 100  // For epoll_wait()
#define LOOP_SIZE 12    // Should be infinite theoretically, but this is for testing

typedef struct ConnectionData {
    int first;
    int second;
    struct ConnectionData* next;
} ConnectionData;

ConnectionData* addConnectionPair(ConnectionData* data, int first, int second);
int findConnectionPair(ConnectionData* data, int pair);
ConnectionData* deleteConnectionPair(ConnectionData* data, int pair);

int main(int argc, char** argv) {
    int epollfd;
    struct epoll_event ev;                  // epoll_ctl()
    struct epoll_event events[MAX_EVENTS];  // epoll_wait()
    int lc;                                 // loop counter
    int n, nfds;

    ConnectionData* connections = NULL;

    if (argc != 2) {
        fprintf(stderr, "Invalid arguments!\n");
        fprintf(stderr, "Try: %s <Port_Number>\n", argv[0]);
        return 1;
    }

    int clientSock, clientConn;

    // These are used to store the data that comes in from the
    // client and server respectively
    // There will probably need to be one associated with
    // each client
    DynamicArray getBuff, reqBuff;
    da_init(&getBuff, 2048);
    da_init(&reqBuff, 2048);

    HashTable *cache = malloc(sizeof(HashTable));
    ht_init(cache, 10, keyHash, keyCmp, termCacheObj);

    if ((clientSock = createClientSock(argv[1])) == -1)
        return 1;

    // Create epoll instance
    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        fprintf(stderr, "Error on epoll_create1()\n");
        exit(EXIT_FAILURE);
    }

    // Register clientSock to the epoll instance
    ev.events = EPOLLIN;
    ev.data.fd = clientSock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientSock, &ev) == -1) {
        fprintf(stderr, "Error on epoll_ctl() on clientSock\n");
        exit(EXIT_FAILURE);
    }

    for (lc = 0; lc < LOOP_SIZE; ++lc) {
        // printf("LC: %d\n", lc);
        // Blocking wait, waits for events to happen
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            fprintf(stderr, "Error on epoll_wait()\n");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == clientSock) {
                // Initialize Client Connection
                struct sockaddr_in connAddr;
                socklen_t connSize = sizeof(struct sockaddr_in);
                clientConn = accept(clientSock, (struct sockaddr*)&connAddr,
                                    &connSize);
                if (clientConn == -1) {
                    fprintf(stderr, "Error on accept()\n");
                    exit(EXIT_FAILURE);
                }

                // Set the clientConn as a non-block socket. This is necessary,
                // since the clientConn socket would be registered to the epoll
                // instance as edge-triggered (i.e. EPOLLET)
                if (fcntl(clientConn, F_SETFL,
                          fcntl(clientConn, F_GETFL, 0) | O_NONBLOCK) == -1) {
                    fprintf(stderr, "Error on fcntl()\n");
                }

                // Register the clientConn socket to the epoll instance
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = clientConn;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientConn, &ev) == -1) {
                    fprintf(stderr, "Error on epoll_ctl() on clientConn\n");
                }
            } else {
                // TODO: Figure out What state this connection is in
                // They could be a new client with a GET or CONNECT
                // They could be sending more HTTPS data to be forwarded
                // They could be an old client that's sending another GET

                clientConn = events[n].data.fd;

                // Get data from the client and parse the header
                int bytesRead = readAll(clientConn, &getBuff);

                // TODO: First check to see if it's an active connection
                // If so, forward data between
                int otherSock = findConnectionPair(connections, clientConn);
                if (otherSock != -1) {
                    write(otherSock, getBuff.buff, getBuff.size);
                    da_clear(&getBuff);
                    continue;
                }

                Header clientHeader;
                parseHeader(&clientHeader, &getBuff);

                // Check to see if record is cached
                // TODO: Make sure that caching actually works
                CacheObj* record = cache_get(&clientHeader, cache);
                if (record != NULL) {
                    printf("\nFound Data in cache\n");

                    char* cur = record->data;
                    // TODO: probably should know the size instead of strlen
                    write(clientConn, cur, strlen(cur));
                    // TOOD: Figure out a way to add the age to the header
                    // The old way didn't work because it's possible that we get
                    // a header from a website with the age header already
                    // filled in

                    record->lastAccess = time(NULL);
                    close(clientConn);
                    da_clear(&getBuff);
                    continue;
                }

                // If we get to this point, either the key wasn't in the cache,
                // or it was stale
                // So connect to the server, and send them the request
                int serverSock;
                if ((serverSock = createServerSock(clientHeader.domain,
                                                   clientHeader.port)) == -1) {
                    close(clientConn);  // TODO: Handle this error more gracefully
                    continue;
                }

                // Connection successful
                // TODO: Maybe refactor this?
                switch (clientHeader.method) {
                    case GET: {
                        write(serverSock, getBuff.buff, getBuff.size);
                        int servBytesRead = readAll(serverSock, &reqBuff);

                        Header serverHeader;
                        parseHeader(&serverHeader, &reqBuff);

                        int responseSize = serverHeader.headerLength;
                        responseSize += readBody(serverSock, &serverHeader,
                                                 &reqBuff);

                        clientHeader.timeToLive = 60;
                        cache_add(&clientHeader, &serverHeader, responseSize,
                                  &reqBuff, cache);

                        write(clientConn, reqBuff.buff, reqBuff.size);

                        close(serverSock);
                        close(clientConn);
                        break;
                    }
                    case CONNECT: {
                        char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
                        write(clientConn, ok, strlen(ok));
                        da_clear(&getBuff);

                        connections = addConnectionPair(connections, clientConn, serverSock);

                        ev.events = EPOLLIN | EPOLLET;
                        ev.data.fd = serverSock;
                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSock, &ev) == -1) {
                            fprintf(stderr, "Error on epoll_ctl() on serverSock\n");
                        }
                    }
                }  // switch

                da_clear(&getBuff);
                da_clear(&reqBuff);
            }  // if (events[n].data.fd != clientSock)
        }
    }

    // terminate buffers and free memory
    free(cache);

    da_term(&reqBuff);
    da_term(&getBuff);
    close(clientSock);
    close(epollfd);
    return 0;
}

/************ Proxy Helpers ****************/
int createClientSock(const char* port) {
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

int createServerSock(char* domain, char* port) {
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

// TODO: add content-length header
bool parseHeader(Header* outHeader, DynamicArray* buff) {
    outHeader->contentLength = -1;
    outHeader->chunkedEncoding = false;
    int headerLen = 0;

    char* copiedStr = malloc(buff->size);
    memcpy(copiedStr, buff->buff, buff->size);

    char* line = strtok(copiedStr, "\r\n");
    while (line) {
        // printf("Line: %s\n", line);

        headerLen += (strlen(line) + 2);

        if (strstr(line, "GET ") != NULL ||
            strstr(line, "CONNECT ") != NULL) {
            bool useSSL = strstr(line, "CONNECT ") != NULL;
            outHeader->method = useSSL ? CONNECT : GET;

            char* getUrl = useSSL ? line + 8 : line + 4;
            char* urlPortSep;
            char* urlEnd;
            urlPortSep = strstr(strstr(getUrl, ":") + 1, ":");  // Second occurence
            if ((urlEnd = strstr(getUrl, " ")) == NULL) {
                free(copiedStr);
                return false;
            }

            size_t urlLen;
            if (urlPortSep == NULL) {
                urlLen = urlEnd - getUrl;
                strcpy(outHeader->port, useSSL ? "443" : "80");
            } else {
                urlLen = urlPortSep - getUrl;

                size_t portLen = urlEnd - (urlPortSep + 1);
                memcpy(outHeader->port, urlPortSep + 1, portLen);
                outHeader->port[portLen] = '\0';
            }

            // printf("Port: %s\n", outHeader->port);

            memcpy(outHeader->url, getUrl, urlLen);
            outHeader->url[urlLen] = '\0';
        } else if (strstr(line, "Host: ") != NULL) {
            char* domain = line + 6;
            char* portSep = strstr(domain, ":");

            int domainLen = portSep == NULL ? strlen(domain) : portSep - domain;
            memcpy(outHeader->domain, domain, domainLen);
            outHeader->domain[domainLen] = '\0';
            // printf("Domain: %s\n", outHeader->domain);
        } else if (strstr(line, "Transfer-Encoding: chunked") != NULL) {
            outHeader->chunkedEncoding = true;
        } else if (outHeader->chunkedEncoding && atoi(line) != 0) {
            outHeader->headerLength = headerLen - strlen(line);
            free(copiedStr);
            return true;
        } else if (strlen(line) <= 1) {
            outHeader->headerLength = headerLen + 2;
            free(copiedStr);
            return true;  // reached header end
        }
        // else
        //   printf("Unknown Header: %s\n", line);
        line = strtok(NULL, "\r\n");
    }

    outHeader->headerLength = headerLen;
    free(copiedStr);
    return true;
}

int readBody(int sock, Header* header, DynamicArray* buffer) {
    // A couple things could happen here.
    // If content length set, then just read content length
    // If chunked encoding, find chunk size and read chunks
    // Right now we're only handling chunked encoding
    int bodySize = 0;

    int start = header->headerLength;
    while (header->chunkedEncoding) {
        // Figure out how many characters are in the chunk size
        char* endOfChunkLine = strstr(buffer->buff + start, "\r\n");
        int size = endOfChunkLine - (buffer->buff + start);

        // Get the chunk size string
        char chunkSizeBuff[20];
        memcpy(chunkSizeBuff, (buffer->buff + start), size);
        chunkSizeBuff[size] = '\0';

        // Convert the chunk size string to int. It's a hex number,
        // so we use base 16
        int chunkSize = (int)strtol(chunkSizeBuff, NULL, 16);

        bodySize += 2 + chunkSize + 2 + size;

        // This means there are no more chunks, so we read all the data
        if (chunkSize == 0)
            break;

        // Add 2 for the \r\n
        start += 2 + size;

        // While the amount of bytes that we've read in the chunk
        // is less than the size of the chunk, read more data
        while (buffer->size - start < chunkSize) {
            int bytesRead = readAll(sock, buffer);
            if (bytesRead == -1) {
                break;
            }
        }

        // There's a blank line between chunks
        start += chunkSize + 2;
    }
    return bodySize + 2;  // TODO: Check if this is correct
}

// TODO: add in variadic arg for sd's to close.
// This will make it much easier to exit.
void socketError(char* funcName) {
    fprintf(stderr, "%s Error: %s\n", funcName, strerror(errno));
    fprintf(stderr, "Exiting\n");
}
/****************************************************/

ConnectionData* addConnectionPair(ConnectionData* data, int first, int second) {
    ConnectionData* newData = malloc(sizeof(ConnectionData));
    newData->first = first;
    newData->second = second;
    newData->next = data;
    return newData;
}

int findConnectionPair(ConnectionData* data, int pair) {
    if (data == NULL)
        return -1;
    else if (data->first == pair)
        return data->second;
    else if (data->second == pair)
        return data->first;
    else
        return findConnectionPair(data->next, pair);
}

ConnectionData* deleteConnectionPair(ConnectionData* data, int pair) {
    if (data == NULL)
        return NULL;
    else if (data->first == pair || data->second == pair) {
        // TODO: Should we close sockets here?
        ConnectionData* next = data->next;
        free(data);
        return next;
    } else {
        ConnectionData* next = deleteConnectionPair(data->next, pair);
        data->next = next;
        return data;
    }
}

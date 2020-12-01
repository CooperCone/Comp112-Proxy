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
    DynamicArray buffer;
    struct ConnectionData *next;
} ConnectionData;

typedef struct ClientData {
    int sock;
    DynamicArray buffer;
    struct ClientData *next;
} ClientData;

typedef struct ServerData {
    char *domain;
    int sock;
    int dest;
    DynamicArray buffer;
    struct ServerData *next, *prev;
} ServerData;

ConnectionData* addConnectionPair(ConnectionData* data, int first, int second);
int findConnectionPair(ConnectionData* data, DynamicArray **out, int pair);
ConnectionData* deleteConnectionPair(ConnectionData* data, int pair);

ClientData *addClientData(ClientData *data, int sock);
ClientData *findClientData(ClientData *data, int sock);
ClientData *deleteClientData(ClientData *data, int sock);

ServerData *addServerData(ServerData *data, int sock, int dest, char *domain);
ServerData *findServerDataBySock(ServerData *data, int sock);
ServerData *findServerDataByDomain(ServerData *data, char *domain);
ServerData *deleteServerData(ServerData *data, int sock);

int main(int argc, char** argv) {
    int epollfd;
    struct epoll_event ev;                  // epoll_ctl()
    struct epoll_event events[MAX_EVENTS];  // epoll_wait()
    int lc;                                 // loop counter
    int n, nfds;

    ConnectionData *connections = NULL;
    ClientData *clients = NULL;
    ServerData *servers = NULL;

    if (argc != 2) {
        fprintf(stderr, "Invalid arguments!\n");
        fprintf(stderr, "Try: %s <Port_Number>\n", argv[0]);
        return 1;
    }

    int clientSock, clientConn;

    DynamicArray reqBuff;
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

    for (;;) {
        // for (lc = 0; lc < LOOP_SIZE; ++lc) {
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

                clients = addClientData(clients, clientConn);

                // Register the clientConn socket to the epoll instance
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = clientConn;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientConn, &ev) == -1) {
                    fprintf(stderr, "Error on epoll_ctl() on clientConn\n");
                }
            } else {
                clientConn = events[n].data.fd;

                // First check to see if it's an active connection
                // If so, forward data between
                DynamicArray *connectionBuff;
                int otherSock = findConnectionPair(connections, &connectionBuff, clientConn);
                if (otherSock != -1) {
                    int bytesRead = readAll(clientConn, connectionBuff);

                    write(otherSock, connectionBuff->buff, connectionBuff->size);
                    da_clear(connectionBuff);
                    continue;
                }

                ClientData *clientData = findClientData(clients, clientConn);
                int bytesRead = readAll(clientConn, &(clientData->buffer));

                Header clientHeader;
                parseHeader(&clientHeader, &(clientData->buffer));
                // TODO: do we need to read bodies for requests?

                // TODO: should we handle POST differently?
                if (clientHeader.method == POST) {
                    close(clientConn); // TODO: remove from epoll
                    da_clear(&clientData->buffer);
                    continue;
                }

                // Check to see if record is cached
                // TODO: Fix Caching that breaks images
                // CacheObj* record = cache_get(&clientHeader, cache);
                // if (record != NULL) {
                //     printf("\nFound Data in cache\n");

                //     char* cur = record->data;
                //     write(clientConn, cur, strlen(cur));
                //     // TOOD: Figure out a way to add the age to the header
                //     // The old way didn't work because it's possible that we get
                //     // a header from a website with the age header already
                //     // filled in

                //     record->lastAccess = time(NULL);
                //     close(clientConn);
                //     da_clear(&getBuff);
                //     continue;
                // }

                // If we get to this point, either the key wasn't in the cache,
                // or it was stale
                // So connect to the server, and send them the request
                int serverSock;
                ServerData *servData;
                if (clientHeader.method == GET && (servData = findServerDataByDomain(servers, clientHeader.domain)) != NULL) {
                    serverSock = servData->sock;
                    printf("Reusing socket for %s\n", clientHeader.domain);
                }
                else {
                    printf("Opening new socket for %s:%s\n", clientHeader.domain, clientHeader.port);
                    if ((serverSock = createServerSock(clientHeader.domain,
                                                   clientHeader.port)) == -1) {
                        close(clientConn);  // TODO: Handle this error more gracefully
                        continue;
                    }
                    servers = addServerData(servers, serverSock, -1, clientHeader.domain);
                    servData = servers;
                }

                // Connection successful
                switch (clientHeader.method) {
                    case GET: {
                        while (clientData->buffer.size > 0) {                            
                            write(serverSock, clientData->buffer.buff, clientHeader.headerLength);
                            int servBytesRead = readAll(serverSock, &reqBuff);

                            Header serverHeader;
                            memset(&serverHeader, 0, sizeof(Header));
                            parseHeader(&serverHeader, &reqBuff);

                            int responseSize = serverHeader.headerLength;
                            int bodySize = readBody(serverSock, &serverHeader,
                                                    &reqBuff); 
                            responseSize += bodySize;

                            clientHeader.timeToLive = 60;

                            // TODO: Fix cache
                            // cache_add(&clientHeader, &serverHeader, responseSize,
                            //           &reqBuff, cache);

                            write(clientConn, reqBuff.buff, reqBuff.size);

                            da_clear(&reqBuff);
                            da_shift(&(clientData->buffer), clientHeader.headerLength);
                        }
                        break;
                    }
                    case CONNECT: {
                        char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
                        write(clientConn, ok, strlen(ok));

                        clients = deleteClientData(clients, clientConn);
                        connections = addConnectionPair(connections, clientConn, serverSock);

                        ev.events = EPOLLIN | EPOLLET;
                        ev.data.fd = serverSock;
                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSock, &ev) == -1) {
                            fprintf(stderr, "Error on epoll_ctl() on serverSock: %s\n", strerror(errno));
                        }
                    }
                }  // switch

                da_clear(&reqBuff);
            }  // if (events[n].data.fd != clientSock)
        }
    }

    // terminate buffers and free memory
    free(cache);

    da_term(&reqBuff);
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

    // TODO: Handle connect errors better
    if (connect(serverSock, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
        socketError("Connect");
        close(serverSock);
        return -1;
    }

    freeaddrinfo(serverInfo);
    return serverSock;
}

bool parseHeader(Header* outHeader, DynamicArray* buff) {
    outHeader->contentLength = -1;
    outHeader->chunkedEncoding = false;
    int headerLen = 0;

    const char delim[] = "\r";
    char* line = buff->buff;
    unsigned long lineLen = strstr(line, delim) - line;

    while (line) {
        // printf("Line: %s\n", line);

        headerLen += (lineLen + 2);

        char *getStr = strstr(line, "GET ");
        char *connectStr = strstr(line, "CONNECT ");
        if ((getStr != NULL && getStr - line <= lineLen) ||
            (connectStr != NULL && connectStr - line <= lineLen)) {
            bool useSSL = connectStr != NULL;
            outHeader->method = useSSL ? CONNECT : GET;

            char* getUrl = useSSL ? line + 8 : line + 4;
            char* urlPortSep;
            char* urlEnd;
            urlPortSep = strstr(strstr(getUrl, ":") + 1, ":");  // Second occurence
            if ((urlEnd = strstr(getUrl, " ")) == NULL) {
                return false;
            }

            size_t urlLen;
            if ((urlPortSep - line) >= lineLen) {
                urlLen = urlEnd - getUrl;
                strcpy(outHeader->port, useSSL ? "443" : "80");
            } else {
                urlLen = urlPortSep - getUrl;

                size_t portLen = urlEnd - (urlPortSep + 1);
                memcpy(outHeader->port, urlPortSep + 1, portLen);
                outHeader->port[portLen] = '\0';
            }

            memcpy(outHeader->url, getUrl, urlLen);
            outHeader->url[urlLen] = '\0';
        } 
        
        char *postStr = strstr(line, "POST"); 
        if (postStr != NULL && postStr - line <= lineLen) {
            outHeader->method = POST;
        } 
        
        char *hostStr = strstr(line, "Host: ");
        if (hostStr != NULL && hostStr - line <= lineLen) {
            char* domain = line + 6;
            char* portSep = strstr(domain, ":");

            int domainLen = (portSep - line) > lineLen ? lineLen - 6 : portSep - domain;
            memcpy(outHeader->domain, domain, domainLen);
            outHeader->domain[domainLen] = '\0';
        }
        
        char *chunkStr = strstr(line, "Transfer-Encoding: chunked");
        if (chunkStr != NULL && chunkStr - line <= lineLen) {
            outHeader->chunkedEncoding = true;
        } 
        
        char *contentLenStr = strstr(line, "Content-Length: ");
        if (contentLenStr != NULL && contentLenStr - line <= lineLen) {
            char* start = strstr(line, ":") + 2;

            int contentLength = atoi(start);
            outHeader->contentLength = contentLength;
        }
        
        if (outHeader->chunkedEncoding && atoi(line) != 0) {
            outHeader->headerLength = headerLen - lineLen;
            return true;
        } 
        
        if (lineLen == 0 || line - buff->buff + lineLen + 2 > buff->size) {
            if (headerLen > buff->size)
                headerLen = buff->size;
            outHeader->headerLength = headerLen;
            return true;  // reached header end
        }
        // else
        //   printf("Unknown Header: %s\n", line);
        line += lineLen + 2;
        char *next = strstr(line, delim);
        if (next == NULL)
            break;
        lineLen = next - line;
    }

    outHeader->headerLength = headerLen;
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

        printf("Chunk Size: %s\n", chunkSizeBuff);

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

    if (!header->chunkedEncoding && header->contentLength != -1) {
        // I have no idea why we have to subtract 3 here. I thought it
        // would only be 2, but it didn't work unless it was 3
        while (buffer->size <
               (header->headerLength + header->contentLength - 3)) {
            readAll(sock, buffer);
        }
        bodySize = header->contentLength - 2;
    }
    
    if (!header->chunkedEncoding && header->contentLength == -1) {
        // while (true) {
        //     int bytesRead = readAll(sock, buffer);
        //     if (bytesRead == -1)
        //         break;
        //     bodySize += bytesRead;
        // }
        // bodySize -= 2;
        return 0;
    }

    return bodySize + 2;
}

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
    da_init(&newData->buffer, 2048);
    return newData;
}

int findConnectionPair(ConnectionData* data, DynamicArray **out, int pair) {
    if (data == NULL)
        return -1;
    else if (data->first == pair) {
        *out = &data->buffer;
        return data->second;
    }
    else if (data->second == pair) {
        *out = &data->buffer;
        return data->first;
    }
    else
        return findConnectionPair(data->next, out, pair);
}

ConnectionData* deleteConnectionPair(ConnectionData* data, int pair) {
    if (data == NULL)
        return NULL;
    else if (data->first == pair || data->second == pair) {
        // TODO: Should we close sockets here?
        ConnectionData* next = data->next;
        da_term(&data->buffer);
        free(data);
        return next;
    } else {
        ConnectionData* next = deleteConnectionPair(data->next, pair);
        data->next = next;
        return data;
    }
}


ClientData *addClientData(ClientData *data, int sock) {
    ClientData *newData = malloc(sizeof(ClientData));
    newData->sock = sock;
    newData->next = data;
    da_init(&(newData->buffer), 2048);
    return newData;
}


ClientData *findClientData(ClientData *data, int sock) {
    if (data == NULL)
        return NULL;
    else if (data->sock = sock)
        return data;
    else return findClientData(data->next, sock);
}


ClientData *deleteClientData(ClientData *data, int sock) {
    if (data == NULL)
        return NULL;
    else if (data->sock == sock) {
        da_term(&(data->buffer));
        ClientData *next = data->next;
        free(data);
        return next;
    }
    else {
        ClientData *next = deleteClientData(data->next, sock);
        data->next = next;
        return data;
    }
}


ServerData *addServerData(ServerData *data, int sock, int dest, char *domain) {
    ServerData *newData = malloc(sizeof(ServerData));
    da_init(&(newData->buffer), 2048);
    newData->domain = malloc(sizeof(char) + strlen(domain) + 1);
    strcpy(newData->domain, domain);
    newData->sock = sock;
    newData->dest = dest;
    newData->next = data;
    return newData;
}

ServerData *findServerDataBySock(ServerData *data, int sock) {
    if (data == NULL)
        return data;
    else if (data->sock == sock)
        return data;
    else
        return findServerDataBySock(data->next, sock);
}

ServerData *findServerDataByDomain(ServerData *data, char *domain) {
    if (data == NULL)
        return data;
    else if (strcmp(data->domain, domain) == 0)
        return data;
    else
        return findServerDataByDomain(data->next, domain);
}

ServerData *deleteServerData(ServerData *data, int sock) {
    if (data == NULL)
        return data;
    else if (data->sock == sock) {
        ServerData *next = data->next;
        da_term(&(data->buffer));
        free(data->domain);
        free(data);
        return next;
    }
    else {
        ServerData *next = deleteServerData(data->next, sock);
        data->next = next;
        return data;
    }
}

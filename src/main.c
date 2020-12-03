#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// #include "compression.h"
#include "cache.h"
#include "dynamicArray.h"
#include "httpData.h"

/************ Proxy Helpers ************/
int createClientSock(const char *port);
int createServerSock(char *domain, char *port);
bool parseHeader(Header *outHeader, DynamicArray *buff);
int readBody(int sock, Header *header, DynamicArray *buffer);
void prefetchImgTags(char *html, DataList **imageServers, int epollfd);
void socketError(char *funcName);
/******************************************/

#define MAX_EVENTS 100  // For epoll_wait()
#define LOOP_SIZE 12    // Should be infinite theoretically, but this is for testing

int main(int argc, char **argv) {
    int epollfd;
    struct epoll_event ev;                  // epoll_ctl()
    struct epoll_event events[MAX_EVENTS];  // epoll_wait()
    int lc;                                 // loop counter
    int n, nfds;

    DataList *connections = NULL; // ConnectionData
    DataList *clients = NULL; // ClientData
    DataList *servers = NULL; // ServerData

    DataList *images = NULL; // PrefetchData
    DataList *imageServers = NULL; // ServerData

    signal(SIGPIPE, SIG_IGN);  // ignore sigpipe, handle with write call

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

                clients = addData(clients, createClientData(clientConn));

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
                DataList *connDl = findData(connections, (CmpFunc)connSockCmp, &clientConn);
                if (connDl) {
                    ConnectionData *connData = connDl->data;
                    int otherSock = connData->first == clientConn ? connData->second : connData->first;
                    int bytesRead = readAll(clientConn, &(connData->buffer));

                    if (bytesRead == -1) {
                        printf("\n\n----------------------------------------------------------\n\n");
                        // TODO: Close https connections
                    }

                    write(otherSock, connData->buffer.buff, connData->buffer.size);
                    da_clear(&(connData->buffer));
                    continue;
                }


                DataList *imgServDl = findData(imageServers, (CmpFunc)servSockCmp, &clientConn);
                if (imgServDl != NULL) {
                    ServerData *imgData = imgServDl->data;
                    // printf("%d - Received Image For: %s\n", clientConn, imgData->domain);

                    int amt = readAll(clientConn, &reqBuff);

                    Header imgHeader;
                    parseHeader(&imgHeader, &reqBuff);
                    readBody(clientConn, &imgHeader, &reqBuff);

                    images = addData(images, createPrefetchData(imgData->domain, &reqBuff));

                    da_clear(&reqBuff);
                    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientConn, NULL) == -1) {
                        fprintf(stderr, "Error on epoll_ctl() delete on clientConn %s\n", strerror(errno));
                    }
                    close(clientConn);

                    imageServers = deleteData(imageServers, (CmpFunc)servSockCmp, &clientConn, (TermFunc)termServerData);
                    continue;
                }

                // This can throw an error if clients doesn't contain clientConn
                ClientData *clientData = findData(clients, (CmpFunc)clientSockCmp, &clientConn)->data;
                int bytesRead = readAll(clientConn, &(clientData->buffer));

                Header clientHeader;
                parseHeader(&clientHeader, &(clientData->buffer));
                // TODO: do we need to read bodies for requests?

                do {
                    printf("Client Url: %s\n", clientHeader.url);

                    // TODO: should we handle POST differently?
                    if (clientHeader.method == POST) {
                        // close(clientConn);  // TODO: remove from epoll
                        int bodyLen = readBody(clientConn, &clientHeader, &(clientData->buffer));
                        da_shift(&(clientData->buffer), clientHeader.headerLength + bodyLen);
                        continue;
                    }

                    // Check to see if record is an image that was already received
                    DataList *imgDl = findData(images, (CmpFunc)prefetchUrlCmp, clientHeader.url);
                    if (imgDl) {
                        PrefetchData *imgData = imgDl->data;
                        printf("Found Url in Prefetch Images\n\n");
                        write(clientConn, imgData->content, imgData->contentLen);
                        
                        images = deleteData(images, (CmpFunc)prefetchUrlCmp, clientHeader.url, (TermFunc)termPrefetchData);
                        da_shift(&(clientData->buffer), clientHeader.headerLength);
                        continue;
                    }

                    // Check to see if record is cached
                    CacheObj *record = cache_get(&clientHeader, cache);
                    if (record != NULL) {
                        printf("Found Data in cache\n\n");

                        char *cur = record->data;
                        write(clientConn, cur, strlen(cur));
                        // TOOD: Figure out a way to add the age to the header
                        // The old way didn't work because it's possible that we get
                        // a header from a website with the age header already
                        // filled in

                        record->lastAccess = time(NULL);
                        da_shift(&clientData->buffer, clientHeader.headerLength);
                        continue;
                    }

                    // If we get to this point, either the key wasn't in the cache,
                    // or it was stale
                    // So connect to the server, and send them the request
                    int serverSock;
                    DataList *servDl;
                    if (clientHeader.method == GET && (servDl = findData(servers, (CmpFunc)servDomainCmp, clientHeader.domain)) != NULL) {
                        serverSock = ((ServerData*)servDl->data)->sock;
                        printf("Reusing socket for %s\n", clientHeader.domain);
                    } else {
                        printf("Opening new socket for %s:%s\n", clientHeader.domain, clientHeader.port);
                        if ((serverSock = createServerSock(clientHeader.domain,
                                                        clientHeader.port)) == -1) {
                            close(clientConn);  // TODO: Handle this error more gracefully
                            continue;
                        }
                        servers = addData(servers, createServerData(serverSock, clientHeader.domain));
                        servDl = servers;
                    }
                    ServerData *servData = servDl->data;

                    // Connection successful
                    switch (clientHeader.method) {
                        case GET: {
                            int val = write(serverSock, clientData->buffer.buff, clientHeader.headerLength);
                            if (val == -1) {  // This means SIGPIPE
                                // Server closed, so open up a new one
                                close(serverSock);
                                serverSock = createServerSock(clientHeader.domain, clientHeader.port);
                                if (serverSock == -1)
                                    break;
                                servData->sock = serverSock;
                                write(serverSock, clientData->buffer.buff, clientHeader.headerLength);
                            }

                            int servBytesRead = readAll(serverSock, &reqBuff);

                            if (servBytesRead == 0)
                                continue;

                            Header serverHeader;
                            memset(&serverHeader, 0, sizeof(Header));
                            parseHeader(&serverHeader, &reqBuff);

                            printf("Sending Data to client\n\n");

                            int responseSize = serverHeader.headerLength;
                            int bodySize = readBody(serverSock, &serverHeader,
                                                    &reqBuff);
                            responseSize += bodySize;

                            // Search for IMG tags in html and pull them before client asks
                            // If data is compressed, we need to uncompress it
                            if (serverHeader.encoding == GZIP && serverHeader.contentLength > 0) {
                                int uncompressSize = serverHeader.contentLength;
                                char *uncompressed = malloc(sizeof(char) * uncompressSize);
                                uncompressed = uncompressGzip(uncompressed, &uncompressSize, reqBuff.buff + serverHeader.headerLength, serverHeader.contentLength);
                                prefetchImgTags(uncompressed, &imageServers, epollfd);
                                free(uncompressed);
                            }
                            else
                                prefetchImgTags(reqBuff.buff, &imageServers, epollfd);

                            clientHeader.timeToLive = 60;
                            cache_add(&clientHeader, &serverHeader, responseSize,
                                      &reqBuff, cache);

                            write(clientConn, reqBuff.buff, reqBuff.size);

                            da_clear(&reqBuff);
                            break;
                        }
                        case CONNECT: {
                            char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
                            write(clientConn, ok, strlen(ok));

                            clients = deleteData(clients, (CmpFunc)clientSockCmp, &clientConn, (TermFunc)termClientData);
                            connections = addData(connections, createConnectionData(clientConn, serverSock));

                            ev.events = EPOLLIN | EPOLLET;
                            ev.data.fd = serverSock;
                            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSock, &ev) == -1) {
                                fprintf(stderr, "Error on epoll_ctl() on serverSock: %s\n", strerror(errno));
                            }

                            break;
                        }
                    }  // switch

                    da_clear(&reqBuff);

                    DataList *lst = findData(clients, (CmpFunc)clientSockCmp, &clientConn);
                    if (lst) {
                        clientData = lst->data;
                        da_shift(&(clientData->buffer), clientHeader.headerLength);
                    } 
                    else
                        clientData = NULL;                    
                }  while (clientData && clientData->buffer.size > 0);
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

    // TODO: Handle connect errors better
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
    outHeader->encoding = NO_ENCODE;
    int headerLen = 0;

    const char delim[] = "\r";
    char *line = buff->buff;
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

            char *getUrl = useSSL ? line + 8 : line + 4;
            char *urlPortSep;
            char *urlEnd;
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
            char *domain = line + 6;
            char *portSep = strstr(domain, ":");

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
            char *start = strstr(line, ":") + 2;

            int contentLength = atoi(start);
            outHeader->contentLength = contentLength;
        }

        char *encodingStr = strstr(line, "Content-Encoding: gzip");
        if (encodingStr != NULL && encodingStr - line <= lineLen) {
            outHeader->encoding = GZIP;
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

int readBody(int sock, Header *header, DynamicArray *buffer) {
    // A couple things could happen here.
    // If content length set, then just read content length
    // If chunked encoding, find chunk size and read chunks
    // Right now we're only handling chunked encoding
    int bodySize = 0;

    int start = header->headerLength;
    while (header->chunkedEncoding) {
        // Figure out how many characters are in the chunk size
        char *endOfChunkLine = strstr(buffer->buff + start, "\r\n");
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


void prefetchImgTags(char *html, DataList **imageServers, int epollfd) {
    char *cur = html;

    if (strstr(cur, "<!DOCTYPE html") != NULL) {
        while (true) {
            char *img = strstr(cur, "<img");
            if (img == NULL)
                break;
            char *endImg = strstr(img, ">");
            
            char *srcStart = strstr(img, "src=");

            if (srcStart == NULL)
                continue; // Image has no source

            // Extract image src between quotes
            char *imgUrlStart = strstr(srcStart, "\"");
            char *imgUrlEnd = strstr(imgUrlStart + 1, "\"");

            int urlLen = (int)(imgUrlEnd - imgUrlStart) - 1;
            char urlBuff[urlLen + 1];
            memcpy(urlBuff, imgUrlStart + 1, urlLen);
            urlBuff[urlLen] = '\0';

            char domainBuff[64];
            sscanf(urlBuff, "http://%[^/]", domainBuff);

            char httpGetBuff[urlLen + 200];
            int numChar = sprintf(httpGetBuff,
                            "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",
                            urlBuff,
                            domainBuff);

            int imgSock = createServerSock(domainBuff, "80");
            (*imageServers) = addData(*imageServers, createServerData(imgSock, urlBuff));

            // printf("Adding Image Serv: %d - %s\n", imgSock, urlBuff);
                                   
            write(imgSock, httpGetBuff, numChar);

            if (fcntl(imgSock, F_SETFL,
                    fcntl(imgSock, F_GETFL, 0) | O_NONBLOCK) == -1) {
                fprintf(stderr, "Error on fcntl()\n");
            }

            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = imgSock;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, imgSock, &ev) == -1) {
                fprintf(stderr, "Error on epoll_ctl() on imgSock: %s\n", strerror(errno));
            }

            cur = endImg + 1;
        }
    }
}


void socketError(char *funcName) {
    fprintf(stderr, "%s Error: %s\n", funcName, strerror(errno));
    fprintf(stderr, "Exiting\n");
}
/****************************************************/

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

#include "cache.h"
#include "dynamicArray.h"
#include "httpData.h"
#include "bloomFilter.h"
#include "tokenBucket.h"
#include "contentFilter.h"

/************ Proxy Helpers ************/
int createClientSock(const char* port);
int createServerSock(char* domain, char* port);
bool parseHeader(Header* outHeader, DynamicArray* buff);
int readBody(int sock, Header* header, DynamicArray* buffer);
void prefetchImgTags(char *html, DataList **imageServers, int epollfd);
void socketError(char* funcName);
ssize_t writeResponseWithAge(int writeSock, char *data, int headerSize, int dataSize, time_t age);
char *getErrorHTML();
void getBlockedHttp(char *out, char *html);
/******************************************/

#define MAX_EVENTS 100  // For epoll_wait()
#define BYTES_PER_MIN 40000 // For rate-limiting

int main(int argc, char **argv) {
    // For epoll
    int epollfd;
    struct epoll_event ev;                  // epoll_ctl()
    struct epoll_event events[MAX_EVENTS];  // epoll_wait()
    int nfds;

    // Caching, filtering, and rate-limiting
    ContentFilter *filter;
    HashTable *cache;
    BloomFilter *oneHitBloom; // a set of URLs that had at least one hit
    TokenBuckets *rateLimitTB;

    // Client-side communication
    int clientSock, clientConn;

    // Server-side communication
    DynamicArray reqBuff;

    // DataLists
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

    // Data structures initialization
    da_init(&reqBuff, 2048);
    filter = cf_create("res/contentBlacklist.txt");
    cache = malloc(sizeof(HashTable));
    ht_init(cache, 10, keyHash, keyCmp, termCacheObj);
    oneHitBloom = bf_create();
    rateLimitTB = tb_create(BYTES_PER_MIN);

    // Create socket for client-side communication
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
        // Blocking wait, waits for events to happen
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            fprintf(stderr, "Error on epoll_wait()\n");
            exit(EXIT_FAILURE);
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == clientSock) { // Connection request from a client
                // Initialize Client Connection
                struct sockaddr_in connAddr;
                socklen_t connSize = sizeof(struct sockaddr_in);
                clientConn = accept(clientSock, (struct sockaddr*)&connAddr, &connSize);
                if (clientConn == -1) {
                    fprintf(stderr, "Error on accept()\n");
                    exit(EXIT_FAILURE);
                }

                // Set the clientConn as a non-block socket. This is necessary,
                // since the clientConn socket would be registered to the epoll
                // instance as edge-triggered (i.e. EPOLLET)
                if (fcntl(clientConn, F_SETFL, fcntl(clientConn, F_GETFL, 0) | O_NONBLOCK) == -1) {
                    fprintf(stderr, "Error on fcntl()\n");
                }

                clients = addData(clients, createClientData(clientConn));

                // Register the clientConn socket to the epoll instance
                ev.events = EPOLLIN; // | EPOLLET;
                ev.data.fd = clientConn;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientConn, &ev) == -1) {
                    fprintf(stderr, "Error on epoll_ctl() on clientConn\n");
                }
            } else { // HTTP request from a client
                clientConn = events[n].data.fd;
                //printf("clientConn: %d\n", clientConn);
                // First check to see if it's an active connection
                // If so, forward data between
                DataList *connDl = findData(connections, (CmpFunc)connSockCmp, &clientConn);
                if (connDl) {
                    ConnectionData *connData = connDl->data;
                    int otherSock = connData->first == clientConn ? connData->second : connData->first;
                    if (tb_ratelimit(rateLimitTB, clientConn)) {
                        // printf("Rate limited\n");
                        continue;
                    } else {
                        int bytesRead = readAll(clientConn, &(connData->buffer));
                        //printf("bytesRead: %d\n", bytesRead);
                        if (bytesRead == -1) {
                            printf("\n\n----------------------------------------------------------\n\n");
                            // TODO: Close https connections
                        }

                        write(otherSock, connData->buffer.buff, connData->buffer.size);
                        da_clear(&(connData->buffer));
                        tb_update(rateLimitTB, clientConn, bytesRead);
                        continue;
                    }
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

                if (bytesRead == 0)
                    continue;

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
                        printf("Found Url in Prefetch Images of size %d\n\n", imgData->contentLen);
                        write(clientConn, imgData->content, imgData->contentLen);
                        
                        images = deleteData(images, (CmpFunc)prefetchUrlCmp, clientHeader.url, (TermFunc)termPrefetchData);
                        da_shift(&(clientData->buffer), clientHeader.headerLength);
                        continue;
                    }

                    // Check to see if record is cached
                    CacheObj *record = cache_get(&clientHeader, cache);
                    if (record != NULL) {
                        printf("Found Data in cache\n\n");

                        time_t age = time(NULL) - record->timeCreated;

                        char* cur = record->data;
                        writeResponseWithAge(clientConn, record->data, record->headerSize, record->dataSize, age);

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
                    }
                    else {
                        printf("Opening new socket for %s:%s\n", clientHeader.domain, clientHeader.port);
                        if ((serverSock = createServerSock(clientHeader.domain, clientHeader.port)) == -1) {
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

                            int servBytesRead = 0;
                            do {
                                servBytesRead = readAll(serverSock, &reqBuff);
                            } while (servBytesRead == 0);

                            Header serverHeader;
                            memset(&serverHeader, 0, sizeof(Header));
                            parseHeader(&serverHeader, &reqBuff);

                            serverHeader.timeToLive = 7200;

                            int responseSize = serverHeader.headerLength;

                            int bodySize = readBody(serverSock, &serverHeader, &reqBuff);
                            responseSize += bodySize;

                            bool foundBadContent = false;

                            // Search for IMG tags in html and pull them before client asks
                            // If data is compressed, we need to uncompress it
                            if (serverHeader.encoding == GZIP && serverHeader.contentLength > 0) {
                                // printf("\nCompressed: \n");
                                // write(1, reqBuff.buff + serverHeader.headerLength, 30);
                                int uncompressSize = serverHeader.contentLength;
                                // printf("Uncom size: %d\n", uncompressSize);
                                char *uncompressed = malloc(sizeof(char) * uncompressSize);
                                uncompressed = uncompressGzip(uncompressed, &uncompressSize, reqBuff.buff + serverHeader.headerLength, serverHeader.contentLength);
                                // printf("Uncom size: %d\n", uncompressSize);
                                // printf("\nUncompressed: \n\n");
                                // write(1, uncompressed, 30);
                                // printf("\n");

                                foundBadContent = cf_searchText(filter, uncompressed, uncompressSize);
                                
                                if (!foundBadContent)
                                    prefetchImgTags(uncompressed, &imageServers, epollfd);
                                
                                free(uncompressed);
                            }
                            else {
                                char *bodyStart = reqBuff.buff + serverHeader.headerLength;
                                int length = reqBuff.size - serverHeader.headerLength;
                                foundBadContent = cf_searchText(filter, bodyStart, length);

                                if (!foundBadContent)
                                    prefetchImgTags(reqBuff.buff, &imageServers, epollfd);
                            }

                            if (foundBadContent) {
                                // printf("Found Blocked Content\n");
                                char blacklistText[512];
                                getBlockedHttp(blacklistText, getErrorHTML());
                                write(clientConn, blacklistText, strlen(blacklistText));
                                if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientConn, NULL) == -1) {
                                    fprintf(stderr, "Error on epoll_ctl() delete on clientConn %s\n", strerror(errno));
                                }
                                close(clientConn);
                                da_clear(&reqBuff);
                                break;
                            }

                            printf("Sending Data to client\n\n");

                            clientHeader.timeToLive = 60;

                            // Add to cache only when the URL has been through at least once
                            if (bf_query(oneHitBloom, clientHeader.url))
                                cache_add(&clientHeader, &serverHeader, responseSize, &reqBuff, cache);
                            else
                                bf_add(oneHitBloom, clientHeader.url);

                            writeResponseWithAge(clientConn, reqBuff.buff, serverHeader.headerLength, reqBuff.size, serverHeader.age);

                            da_clear(&reqBuff);
                            break;
                        }
                        case CONNECT: {
                            char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
                            write(clientConn, ok, strlen(ok));

                            clients = deleteData(clients, (CmpFunc)clientSockCmp, &clientConn, (TermFunc)termClientData);
                            connections = addData(connections, createConnectionData(clientConn, serverSock));

                            ev.events = EPOLLIN; // | EPOLLET;
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
                } while (clientData && clientData->buffer.size > 0);
            }  // if (events[n].data.fd != clientSock)
        } // for (n = 0; n < nfds; ++n)
    } // for (;;)

    // terminate buffers and free memory
    cf_delete(filter);
    ht_term(cache);
    bf_delete(oneHitBloom);
    tb_delete(rateLimitTB);
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
    outHeader->age = 0;
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

        char *ageStr = strstr(line, "Age: ");
        if (ageStr != NULL && ageStr - line <= lineLen) {
            // If the age field is already here, we remove it. This reduces the
            // cost of re-iterating through the header to find the age field
            // again, when we send the response

            // Log size
            char *start = strstr(line, ":") + 2;
            int age = atoi(start);
            outHeader->age = age;

            // Remove age field
            memcpy(line, line + lineLen + 2, buff->size - headerLen);

            // Update lengths
            headerLen -= (lineLen + 2);
            int oldSize = buff->size;
            buff->size -= (lineLen + 2);

            // Clean the tailing data
            for (int i = buff->size; i < oldSize; ++i)
            {
                buff->buff[i] = 0; // null
            }
            line -= (lineLen + 2);
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
        while (buffer->size < (header->headerLength + header->contentLength - 3)) {
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

ssize_t writeResponseWithAge(int writeSock, char *data, int headerSize, int dataSize, time_t age) {
    char *response;
    char ageStr[256];
    char ageField[6] = "Age: ";
    char lineDelim[3] = "\r\n";
    size_t ageStrLen, ageLineLen;
    size_t offset;
    ssize_t retval;

    // Create age string
    sprintf(ageStr, "%ld", age);
    ageLineLen = strlen(ageField) + strlen(ageStr) + strlen(lineDelim);

    // Make a new response with the age field
    offset = 0;
    response = malloc(dataSize + ageLineLen);
    memcpy(response + offset, data, headerSize - 2);
    offset += (headerSize - 2);
    memcpy(response + offset, ageField, strlen(ageField));
    offset += strlen(ageField);
    memcpy(response + offset, ageStr, strlen(ageStr));
    offset += strlen(ageStr);
    memcpy(response + offset, lineDelim, strlen(lineDelim));
    offset += strlen(lineDelim);
    memcpy(response + offset, data + headerSize - 2, dataSize - headerSize + 2);

    // Write to the socket
    retval = write(writeSock, response, dataSize + ageLineLen);
    return retval;
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

            write(imgSock, httpGetBuff, numChar);

            if (fcntl(imgSock, F_SETFL,
                    fcntl(imgSock, F_GETFL, 0) | O_NONBLOCK) == -1) {
                fprintf(stderr, "Error on fcntl()\n");
            }

            struct epoll_event ev;
            ev.events = EPOLLIN; // | EPOLLET;
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

char *getErrorHTML() {
    static char blockedHtml[] =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
      "<head>"
        "<title>Test</title>"
        "<style>"
          "div {"
            "text-align: center;"
            "position: absolute;"
            "top: 20%;"
            "left: 50%;"
            "transform: translate(-50%, -50%);"
          "}"
        "</style>"
      "</head>"
      "<body>"
        "<div>"
          "<h1>Content Blocked</h1>"
          "<p>This page is blocked by your proxy's blacklist</p>"
        "</div>"
      "</body>"
    "</html>";
    return blockedHtml;
}

void getBlockedHttp(char *out, char *html) {
    static char blockHttp[] =
        "HTTP/1.1 403 Forbidden\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "\r\n"
        "%s";
    
    sprintf(out, blockHttp, strlen(html), html);
}

/****************************************************/

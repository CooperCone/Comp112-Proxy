#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dynamicArray.h"

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
      fprintf(stderr, "Read Error: %s\n", strerror(errno));
      return -1;
    }

    buffer->size += bytesRead;
    totalRead += bytesRead;

    if (bytesRead < 1024)
      break;
  }

  return totalRead;
}

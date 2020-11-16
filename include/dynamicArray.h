#pragma once

typedef struct DynamicArray {
  char *buff;
  int size, maxSize;
} DynamicArray;

int readAll(int sd, DynamicArray *buffer);
void da_init(DynamicArray *buffer, int maxSize);
void da_clear(DynamicArray *buffer);
void da_term(DynamicArray *buffer);
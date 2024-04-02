#include "chunk.h"

#include <stdlib.h>

#include "memory.h"
#include "vm.h"

void hl_initChunk(struct hl_Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  hl_initValueArray(&chunk->constants);
}

void hl_freeChunk(struct hl_Chunk* chunk) {
  hl_FREE_ARRAY(u8, chunk->code, chunk->capacity);
  hl_FREE_ARRAY(s32, chunk->lines, chunk->capacity);
  hl_freeValueArray(&chunk->constants);
  hl_initChunk(chunk);
}

void hl_writeChunk(struct hl_Chunk* chunk, u8 byte, s32 line) {
  if (chunk->capacity < chunk->count + 1) {
    s32 oldCapacity = chunk->capacity;
    chunk->capacity = hl_GROW_CAPACITY(oldCapacity);
    chunk->code = hl_GROW_ARRAY(u8, chunk->code, oldCapacity, chunk->capacity);
    chunk->lines = hl_GROW_ARRAY(s32, chunk->lines, oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

s32 hl_addConstant(struct hl_Chunk* chunk, hl_Value value) {
  hl_push(value);
  hl_writeValueArray(&chunk->constants, value);
  hl_pop();
  return chunk->constants.count - 1;
}


#ifndef _HOBBYL_CHUNK_H
#define _HOBBYL_CHUNK_H

#include "common.h"
#include "value.h"

enum hl_OpCode {
  hl_OP_CONSTANT,
  hl_OP_NIL,
  hl_OP_TRUE,
  hl_OP_FALSE,
  hl_OP_POP,
  hl_OP_ARRAY,
  hl_OP_GET_SUBSCRIPT,
  hl_OP_SET_SUBSCRIPT,
  hl_OP_DEFINE_GLOBAL,
  hl_OP_GET_GLOBAL,
  hl_OP_SET_GLOBAL,
  hl_OP_GET_UPVALUE,
  hl_OP_SET_UPVALUE,
  hl_OP_GET_LOCAL,
  hl_OP_SET_LOCAL,
  hl_OP_INIT_PROPERTY,
  hl_OP_GET_STATIC,
  hl_OP_PUSH_PROPERTY,
  hl_OP_GET_PROPERTY,
  hl_OP_SET_PROPERTY,
  hl_OP_DESTRUCT_ARRAY,
  hl_OP_EQUAL,
  hl_OP_NOT_EQUAL,
  hl_OP_GREATER,
  hl_OP_GREATER_EQUAL,
  hl_OP_LESSER,
  hl_OP_LESSER_EQUAL,
  hl_OP_CONCAT,
  hl_OP_ADD,
  hl_OP_SUBTRACT,
  hl_OP_MULTIPLY,
  hl_OP_DIVIDE,
  hl_OP_MODULO,
  hl_OP_POW,
  hl_OP_NEGATE,
  hl_OP_NOT,
  hl_OP_JUMP,
  hl_OP_JUMP_IF_FALSE,
  hl_OP_INEQUALITY_JUMP,
  hl_OP_LOOP,
  hl_OP_CALL,
  hl_OP_INSTANCE,
  hl_OP_CLOSURE,
  hl_OP_CLOSE_UPVALUE,
  hl_OP_RETURN,
  hl_OP_ENUM,
  hl_OP_ENUM_VALUE,
  hl_OP_STRUCT,
  hl_OP_STRUCT_FIELD,
  hl_OP_METHOD,
  hl_OP_STATIC_METHOD,
  hl_OP_INVOKE,
  hl_OP_BREAK,
};

struct hl_Chunk {
  s32 count;
  s32 capacity;
  u8* code;
  s32* lines;
  struct hl_ValueArray constants;
};

void hl_initChunk(struct hl_Chunk* chunk);
void hl_freeChunk(struct hl_Chunk* chunk);
void hl_writeChunk(struct hl_Chunk* chunk, u8 byte, s32 line);
s32 hl_addConstant(struct hl_Chunk* chunk, hl_Value value);

#endif // _HOBBYL_CHUNK_H

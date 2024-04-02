#include "debug.h"

#include <stdio.h>

#include "chunk.h"
#include "object.h"
#include "value.h"

void hl_disassembleChunk(struct hl_Chunk* chunk, void* functionPointer, const char* name) {
  printf("== %s (%p) ==\n", name, functionPointer);

  for (int offset = 0; offset < chunk->count;) {
    offset = hl_disassembleInstruction(chunk, offset);
  }
}

static s32 simpleInstruction(const char* name, s32 offset) {
  printf("%s\n", name);
  return offset + 1;
}

static s32 byteInstruction(const char* name, struct hl_Chunk* chunk, s32 offset) {
  u8 slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2; 
}

static int jumpInstruction(const char* name, s32 sign, struct hl_Chunk* chunk, s32 offset) {
  u16 jump = (u16)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-16s %4d -> %4d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

static s32 constantInstruction(const char* name, struct hl_Chunk* chunk, s32 offset) {
  u8 constant = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constant);
  hl_printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

static s32 invokeInstruction(const char* name, struct hl_Chunk* chunk, s32 offset) {
  u8 constant = chunk->code[offset + 1];
  u8 argCount = chunk->code[offset + 2];
  printf("%-16s (%d args) %4d '", name, argCount, constant);
  hl_printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

s32 hl_disassembleInstruction(struct hl_Chunk* chunk, s32 offset) {
  printf("%04d ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  u8 instruction = chunk->code[offset];
  switch (instruction) {
    case hl_OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case hl_OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case hl_OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case hl_OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case hl_OP_POP:
      return simpleInstruction("OP_POP", offset);
    case hl_OP_ARRAY:
      return byteInstruction("OP_ARRAY", chunk, offset);
    case hl_OP_GET_SUBSCRIPT:
      return simpleInstruction("OP_GET_SUBSCRIPT", offset);
    case hl_OP_SET_SUBSCRIPT:
      return simpleInstruction("OP_SET_SUBSCRIPT", offset);
    case hl_OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case hl_OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case hl_OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case hl_OP_GET_UPVALUE:
      return byteInstruction("OP_GET_UPVALUE", chunk, offset);
    case hl_OP_SET_UPVALUE:
      return byteInstruction("OP_SET_UPVALUE", chunk, offset);
    case hl_OP_GET_LOCAL:
      return byteInstruction("OP_GET_LOCAL", chunk, offset);
    case hl_OP_SET_LOCAL:
      return byteInstruction("OP_SET_LOCAL", chunk, offset);
    case hl_OP_INIT_PROPERTY:
      return byteInstruction("OP_INIT_PROPERTY", chunk, offset);
    case hl_OP_GET_STATIC:
      return constantInstruction("OP_GET_STATIC_METHOD", chunk, offset);
    case hl_OP_PUSH_PROPERTY:
      return constantInstruction("OP_PUSH_PROPERTY", chunk, offset);
    case hl_OP_GET_PROPERTY:
      return constantInstruction("OP_GET_PROPERTY", chunk, offset);
    case hl_OP_SET_PROPERTY:
      return constantInstruction("OP_SET_PROPERTY", chunk, offset);
    case hl_OP_DESTRUCT_ARRAY:
      return byteInstruction("OP_DESTRUCT_ARRAY", chunk, offset);
    case hl_OP_STRUCT_FIELD:
      return simpleInstruction("OP_SET_STRUCT_FIELD", offset);
    case hl_OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case hl_OP_NOT_EQUAL:
      return simpleInstruction("OP_NOT_EQUAL", offset);
    case hl_OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case hl_OP_GREATER_EQUAL:
      return simpleInstruction("OP_GREATER_EQUAL", offset);
    case hl_OP_LESSER:
      return simpleInstruction("OP_LESSER", offset);
    case hl_OP_LESSER_EQUAL:
      return simpleInstruction("OP_LESSER_EQUAL", offset);
    case hl_OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case hl_OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case hl_OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case hl_OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);
    case hl_OP_MODULO:
      return simpleInstruction("OP_MODULO", offset);
    case hl_OP_POW:
      return simpleInstruction("OP_POW", offset);
    case hl_OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);
    case hl_OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case hl_OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case hl_OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case hl_OP_INEQUALITY_JUMP:
      return jumpInstruction("OP_INEQUALITY_JUMP", 1, chunk, offset);
    case hl_OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case hl_OP_CALL:
      return byteInstruction("OP_CALL", chunk, offset);
    case hl_OP_INSTANCE:
      return simpleInstruction("OP_INSTANCE", offset);
    case hl_OP_CLOSURE: {
      offset++;
      u8 constant = chunk->code[offset++];
      printf("%-16s %4d ", "OP_CLOSURE", constant);
      hl_printValue(chunk->constants.values[constant]);
      printf("\n");
      struct hl_Function* function = hl_AS_FUNCTION(chunk->constants.values[constant]);

      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf("%04d      |                     %s %d\n",
            offset - 2, isLocal ? "local" : "upvalue", index);
      }

      return offset;
    }
    case hl_OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case hl_OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    case hl_OP_ENUM:
      return constantInstruction("OP_ENUM", chunk, offset);
    case hl_OP_ENUM_VALUE:
      return byteInstruction("OP_ENUM_VALUE", chunk, offset);
    case hl_OP_STRUCT:
      return constantInstruction("OP_STRUCT", chunk, offset);
    case hl_OP_METHOD:
      return constantInstruction("OP_METHOD", chunk, offset);
    case hl_OP_STATIC_METHOD:
      return constantInstruction("OP_STATIC_METHOD", chunk, offset);
    case hl_OP_INVOKE:
      return invokeInstruction("OP_INVOKE", chunk, offset);
    case hl_OP_BREAK:
      return simpleInstruction("OP_BREAK", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}

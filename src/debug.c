#include "debug.h"

#include <stdio.h>

#include "opcodes.h"
#include "object.h"
#include "value.h"

void hl_disassembleChunk(struct hl_Function* function, void* functionPointer, const char* name) {
  printf("== %s (%p) ==\n", name, functionPointer);

  for (int offset = 0; offset < function->bcCount;) {
    offset = hl_disassembleInstruction(function, offset);
  }
}

static s32 simpleInstruction(const char* name, s32 offset) {
  printf("%s\n", name);
  return offset + 1;
}

static s32 byteInstruction(const char* name, struct hl_Function* function, s32 offset) {
  u8 slot = function->bc[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2; 
}

static int jumpInstruction(const char* name, s32 sign, struct hl_Function* function, s32 offset) {
  u16 jump = (u16)(function->bc[offset + 1] << 8);
  jump |= function->bc[offset + 2];
  printf("%-16s %4d -> %4d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

static s32 constantInstruction(const char* name, struct hl_Function* function, s32 offset) {
  u8 constant = function->bc[offset + 1];
  printf("%-16s %4d '", name, constant);
  hl_printValue(function->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

static s32 invokeInstruction(const char* name, struct hl_Function* function, s32 offset) {
  u8 constant = function->bc[offset + 1];
  u8 argCount = function->bc[offset + 2];
  printf("%-16s (%d args) %4d '", name, argCount, constant);
  hl_printValue(function->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

s32 hl_disassembleInstruction(struct hl_Function* function, s32 offset) {
  printf("%04d ", offset);
  if (offset > 0 && function->lines[offset] == function->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", function->lines[offset]);
  }

  u8 instruction = function->bc[offset];
  switch (instruction) {
    case hl_OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", function, offset);
    case hl_OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case hl_OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case hl_OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case hl_OP_POP:
      return simpleInstruction("OP_POP", offset);
    case hl_OP_ARRAY:
      return byteInstruction("OP_ARRAY", function, offset);
    case hl_OP_GET_SUBSCRIPT:
      return simpleInstruction("OP_GET_SUBSCRIPT", offset);
    case hl_OP_SET_SUBSCRIPT:
      return simpleInstruction("OP_SET_SUBSCRIPT", offset);
    case hl_OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", function, offset);
    case hl_OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", function, offset);
    case hl_OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", function, offset);
    case hl_OP_GET_UPVALUE:
      return byteInstruction("OP_GET_UPVALUE", function, offset);
    case hl_OP_SET_UPVALUE:
      return byteInstruction("OP_SET_UPVALUE", function, offset);
    case hl_OP_GET_LOCAL:
      return byteInstruction("OP_GET_LOCAL", function, offset);
    case hl_OP_SET_LOCAL:
      return byteInstruction("OP_SET_LOCAL", function, offset);
    case hl_OP_INIT_PROPERTY:
      return byteInstruction("OP_INIT_PROPERTY", function, offset);
    case hl_OP_GET_STATIC:
      return constantInstruction("OP_GET_STATIC_METHOD", function, offset);
    case hl_OP_PUSH_PROPERTY:
      return constantInstruction("OP_PUSH_PROPERTY", function, offset);
    case hl_OP_GET_PROPERTY:
      return constantInstruction("OP_GET_PROPERTY", function, offset);
    case hl_OP_SET_PROPERTY:
      return constantInstruction("OP_SET_PROPERTY", function, offset);
    case hl_OP_DESTRUCT_ARRAY:
      return byteInstruction("OP_DESTRUCT_ARRAY", function, offset);
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
      return jumpInstruction("OP_JUMP", 1, function, offset);
    case hl_OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, function, offset);
    case hl_OP_INEQUALITY_JUMP:
      return jumpInstruction("OP_INEQUALITY_JUMP", 1, function, offset);
    case hl_OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, function, offset);
    case hl_OP_CALL:
      return byteInstruction("OP_CALL", function, offset);
    case hl_OP_INSTANCE:
      return simpleInstruction("OP_INSTANCE", offset);
    case hl_OP_CLOSURE: {
      offset++;
      u8 constant = function->bc[offset++];
      printf("%-16s %4d ", "OP_CLOSURE", constant);
      hl_printValue(function->constants.values[constant]);
      printf("\n");
      struct hl_Function* inner = hl_AS_FUNCTION(function->constants.values[constant]);

      for (int j = 0; j < inner->upvalueCount; j++) {
        int isLocal = function->bc[offset++];
        int index = function->bc[offset++];
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
      return constantInstruction("OP_ENUM", function, offset);
    case hl_OP_ENUM_VALUE:
      return byteInstruction("OP_ENUM_VALUE", function, offset);
    case hl_OP_STRUCT:
      return constantInstruction("OP_STRUCT", function, offset);
    case hl_OP_METHOD:
      return constantInstruction("OP_METHOD", function, offset);
    case hl_OP_STATIC_METHOD:
      return constantInstruction("OP_STATIC_METHOD", function, offset);
    case hl_OP_INVOKE:
      return invokeInstruction("OP_INVOKE", function, offset);
    case hl_OP_BREAK:
      return simpleInstruction("OP_BREAK", offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}

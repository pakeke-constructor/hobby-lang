#ifndef _HOBBYL_VM_H
#define _HOBBYL_VM_H

#include "common.h"
#include "table.h"
#include "value.h"
#include "object.h"
#include "compiler.h"

#define hl_FRAMES_MAX 64
#define hl_STACK_MAX (hl_FRAMES_MAX * hl_U8_COUNT)

struct hl_CallFrame {
  struct hl_Closure* closure;
  u8* ip;
  hl_Value* slots;
};

struct hl_Vm {
  struct hl_CallFrame frames[hl_FRAMES_MAX];
  s32 frameCount;

  hl_Value stack[hl_STACK_MAX];
  hl_Value* stackTop;
  struct hl_Table globals;
  struct hl_Table strings;
  struct hl_Upvalue* openUpvalues;

  size_t bytesAllocated;
  size_t nextGc;

  struct hl_Obj* objects;

  s32 grayCount;
  s32 grayCapacity;
  struct hl_Obj** grayStack;

  struct hl_Parser parser;
};

enum hl_InterpretResult {
  hl_RES_INTERPRET_OK,
  hl_RES_COMPILE_ERR,
  hl_RES_RUNTIME_ERR,
};

extern struct hl_Vm vm;

void hl_initVm();
void hl_freeVm();
void hl_bindCFunction(const char* name, hl_CFunction cFunction);
enum hl_InterpretResult hl_interpret(const char* source);
void hl_push(hl_Value value);
hl_Value hl_pop();

#endif // _HOBBYL_VM_H

#ifndef _HOBBYL_VM_H
#define _HOBBYL_VM_H

#include "object.h"

enum InterpretResult {
  INTERPRET_OK,
  COMPILE_ERR,
  RUNTIME_ERR,
};

void initState(struct State* H);
void freeState(struct State* H);
void bindCFunction(struct State* H, const char* name, CFunction cFunction);
enum InterpretResult interpret(struct State* H, const char* source);
void push(struct State* H, Value value);
Value pop(struct State* H);

#endif // _HOBBYL_VM_H

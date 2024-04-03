#ifndef _HOBBYL_VM_H
#define _HOBBYL_VM_H

#include "value.h"
#include "object.h"

enum hl_InterpretResult {
  hl_RES_INTERPRET_OK,
  hl_RES_COMPILE_ERR,
  hl_RES_RUNTIME_ERR,
};

void hl_initState(struct hl_State* H);
void hl_freeState(struct hl_State* H);
void hl_bindCFunction(struct hl_State* H, const char* name, hl_CFunction cFunction);
enum hl_InterpretResult hl_interpret(struct hl_State* H, const char* source);
void hl_push(struct hl_State* H, hl_Value value);
hl_Value hl_pop(struct hl_State* H);

#endif // _HOBBYL_VM_H

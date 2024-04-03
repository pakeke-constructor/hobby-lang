#ifndef _HOBBYL_DEBUG_H
#define _HOBBYL_DEBUG_H

#include "common.h"
#include "object.h"

void hl_disassembleFunction(struct hl_Function* function, void* functionPointer, const char* name);
s32 hl_disassembleInstruction(struct hl_Function* function, s32 offset);

#endif // _HOBBYL_DEBUG_H

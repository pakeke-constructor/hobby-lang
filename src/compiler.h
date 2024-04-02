#ifndef _HOBBYL_COMPILER_H
#define _HOBBYL_COMPILER_H

#include "vm.h"
#include "object.h"

struct hl_Function* hl_compile(const char* source);
void hl_markCompilerRoots();

#endif // _HOBBYL_COMPILER_H

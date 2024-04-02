#ifndef _HOBBYL_DEBUG_H
#define _HOBBYL_DEBUG_H

#include "common.h"
#include "chunk.h"

void hl_disassembleChunk(struct hl_Chunk* chunk, void* functionPointer, const char* name);
s32 hl_disassembleInstruction(struct hl_Chunk* chunk, s32 offset);

#endif // _HOBBYL_DEBUG_H

#ifndef _HOBBYL_MEMORY_H
#define _HOBBYL_MEMORY_H

#include "common.h"
#include "value.h"
#include "object.h"

#define hl_ALLOCATE(H, type, count) \
    (type*)hl_reallocate(H, NULL, 0, sizeof(type) * (count))
#define hl_FREE(H, type, pointer) hl_reallocate(H, pointer, sizeof(type), 0)
#define hl_GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define hl_GROW_ARRAY(H, type, pointer, oldCount, newCount) \
    (type*)hl_reallocate( \
        H, pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))
#define hl_FREE_ARRAY(H, type, pointer, oldCount) \
    hl_reallocate(H, pointer, sizeof(type) * (oldCount), 0)

void* hl_reallocate(struct hl_State* H, void* pointer, size_t oldSize, size_t newSize);
void hl_markObject(struct hl_State* H, struct hl_Obj* object);
void hl_markValue(struct hl_State* H, hl_Value value);
void hl_collectGarbage(struct hl_State* H);
void hl_freeObjects(struct hl_State* H);

#endif // _HOBBYL_MEMORY_H

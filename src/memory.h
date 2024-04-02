#ifndef _HOBBYL_MEMORY_H
#define _HOBBYL_MEMORY_H

#include "common.h"
#include "value.h"
#include "object.h"

#define hl_ALLOCATE(type, count) (type*)hl_reallocate(NULL, 0, sizeof(type) * (count))
#define hl_FREE(type, pointer) hl_reallocate(pointer, sizeof(type), 0)
#define hl_GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define hl_GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)hl_reallocate( \
        pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))
#define hl_FREE_ARRAY(type, pointer, oldCount) \
    hl_reallocate(pointer, sizeof(type) * (oldCount), 0)

void* hl_reallocate(void* pointer, size_t oldSize, size_t newSize);
void hl_markObject(struct hl_Obj* object);
void hl_markValue(hl_Value value);
void hl_collectGarbage();
void hl_freeObjects();

#endif // _HOBBYL_MEMORY_H

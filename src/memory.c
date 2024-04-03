#include "memory.h"

#include <stdlib.h>

#ifdef hl_DEBUG_LOG_GC
#include <stdio.h>

#include "debug.h"
#endif

#include "object.h"
#include "value.h"
#include "compiler.h"
#include "table.h"

#define GC_HEAP_GROW_FACTOR 2

void* hl_reallocate(struct hl_State* H, void* pointer, size_t oldSize, size_t newSize) {
  H->bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef hl_DEBUG_STRESS_GC
    hl_collectGarbage();
#else
    if (H->bytesAllocated > H->nextGc) {
      hl_collectGarbage(H);
    }
#endif
  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* newAllocation = realloc(pointer, newSize);
  if (newAllocation == NULL) {
    exit(1);
  }
  return newAllocation;
}

static void freeObject(struct hl_State* H, struct hl_Obj* object) {
#ifdef hl_DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case hl_OBJ_ARRAY: {
      struct hl_Array* array = (struct hl_Array*)object;
      hl_freeValueArray(H, &array->values);
      hl_FREE(H, struct hl_Array, array);
      break;
    }
    case hl_OBJ_ENUM: {
      struct hl_Enum* enoom = (struct hl_Enum*)object;
      hl_freeTable(H, &enoom->values);
      hl_FREE(H, struct hl_Enum, object);
      break;
    }
    case hl_OBJ_STRUCT: {
      struct hl_Struct* strooct = (struct hl_Struct*)object;
      hl_freeTable(H, &strooct->defaultFields);
      hl_freeTable(H, &strooct->methods);
      hl_freeTable(H, &strooct->staticMethods);
      hl_FREE(H, struct hl_Struct, object);
      break;
    }
    case hl_OBJ_INSTANCE: {
      struct hl_Instance* instance = (struct hl_Instance*)object;
      hl_freeTable(H, &instance->fields);
      hl_FREE(H, struct hl_Instance, object);
      break;
    }
    case hl_OBJ_CLOSURE: {
      struct hl_Closure* closure = (struct hl_Closure*)object;
      hl_FREE_ARRAY(
          H, struct hl_Upvalue*, closure->upvalues, closure->upvalueCount);
      hl_FREE(H, struct hl_Closure, object);
      break;
    }
    case hl_OBJ_UPVALUE: {
      hl_FREE(H, struct hl_Upvalue, object);
      break;
    }
    case hl_OBJ_FUNCTION: {
      struct hl_Function* function = (struct hl_Function*)object;
      hl_FREE_ARRAY(H, u8, function->bc, function->bcCapacity);
      hl_FREE_ARRAY(H, s32, function->lines, function->bcCapacity);
      hl_freeValueArray(H, &function->constants);
      hl_FREE(H, struct hl_Function, object);
      break;
    }
    case hl_OBJ_BOUND_METHOD: {
      hl_FREE(H, struct hl_BoundMethod, object);
      break;
    }
    case hl_OBJ_CFUNCTION: {
      hl_FREE(H, struct hl_CFunctionBinding, object);
      break;
    }
    case hl_OBJ_STRING: {
      struct hl_String* string = (struct hl_String*)object;
      hl_FREE_ARRAY(H, char, string->chars, string->length + 1);
      hl_FREE(H, struct hl_String, object);
      break;
    }
  }
}

void hl_markObject(struct hl_State* H, struct hl_Obj* object) {
  if (object == NULL) {
    return;
  }

  if (object->isMarked) {
    return;
  }

#ifdef hl_DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  hl_printValue(hl_NEW_OBJ(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (H->grayCapacity < H->grayCount + 1) {
    H->grayCapacity = hl_GROW_CAPACITY(H->grayCapacity);
    H->grayStack = (struct hl_Obj**)realloc(
        H->grayStack, sizeof(struct hl_Obj*) * H->grayCapacity);
    if (H->grayStack == NULL) {
      exit(1);
    }
  }

  H->grayStack[H->grayCount++] = object;
}

void hl_markValue(struct hl_State* H, hl_Value value) {
  if (hl_IS_OBJ(value)) {
    hl_markObject(H, hl_AS_OBJ(value));
  }
}

static void markArray(struct hl_State* H, struct hl_ValueArray* array) {
  for (s32 i = 0; i < array->count; i++) {
    hl_markValue(H, array->values[i]);
  }
}

static void blackenObject(struct hl_State* H, struct hl_Obj* object) {
#ifdef hl_DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  hl_printValue(hl_NEW_OBJ(object));
  printf("\n");
#endif

  switch (object->type) {
    // No references.
    case hl_OBJ_CFUNCTION:
    case hl_OBJ_STRING:
      break;
    case hl_OBJ_UPVALUE:
      hl_markValue(H, ((struct hl_Upvalue*)object)->closed);
      break;
    case hl_OBJ_FUNCTION: {
      struct hl_Function* function = (struct hl_Function*)object;
      hl_markObject(H, (struct hl_Obj*)function->name);
      markArray(H, &function->constants);
      break;
    }
    case hl_OBJ_BOUND_METHOD: {
      struct hl_BoundMethod* bound = (struct hl_BoundMethod*)object;
      hl_markValue(H, bound->receiver);
      hl_markObject(H, (struct hl_Obj*)bound->method);
      break;
    }
    case hl_OBJ_CLOSURE: {
      struct hl_Closure* closure = (struct hl_Closure*)object;
      hl_markObject(H, (struct hl_Obj*)closure->function);
      for (s32 i = 0; i < closure->upvalueCount; i++) {
        hl_markObject(H, (struct hl_Obj*)closure->upvalues[i]);
      }
      break;
    }
    case hl_OBJ_STRUCT: {
      struct hl_Struct* strooct = (struct hl_Struct*)object;
      hl_markObject(H, (struct hl_Obj*)strooct->name);
      hl_markTable(H, &strooct->defaultFields);
      hl_markTable(H, &strooct->methods);
      hl_markTable(H, &strooct->staticMethods);
      break;
    }
    case hl_OBJ_INSTANCE: {
      struct hl_Instance* instance = (struct hl_Instance*)object;
      hl_markObject(H, (struct hl_Obj*)instance->strooct);
      hl_markTable(H, &instance->fields);
      break;
    }
    case hl_OBJ_ENUM: {
      struct hl_Enum* enoom = (struct hl_Enum*)object;
      hl_markObject(H, (struct hl_Obj*)enoom->name);
      hl_markTable(H, &enoom->values);
      break;
    }
    case hl_OBJ_ARRAY: {
      struct hl_Array* array = (struct hl_Array*)object;
      markArray(H, &array->values);
      break;
    }
  }
}

static void markRoots(struct hl_State* H) {
  for (hl_Value* slot = H->stack; slot < H->stackTop; slot++) {
    hl_markValue(H, *slot);
  }

  for (s32 i = 0; i < H->frameCount; i++) {
    hl_markObject(H, (struct hl_Obj*)H->frames[i].closure);
  }

  for (struct hl_Upvalue* upvalue = H->openUpvalues;
      upvalue != NULL;
      upvalue = upvalue->next) {
    hl_markObject(H, (struct hl_Obj*)upvalue);
  }

  hl_markTable(H, &H->globals);
  hl_markCompilerRoots(H, H->parser);
}

static void traceReferences(struct hl_State* H) {
  while (H->grayCount > 0) {
    struct hl_Obj* object = H->grayStack[--H->grayCount];
    blackenObject(H, object);
  }
}

static void sweep(struct hl_State* H) {
  struct hl_Obj* previous = NULL;
  struct hl_Obj* current = H->objects;

  while (current != NULL) {
    if (current->isMarked) {
      current->isMarked = false;
      previous = current;
      current = current->next;
    } else {
      struct hl_Obj* unreached = current;
      current = current->next;
      if (previous != NULL) {
        previous->next = current;
      } else {
        H->objects = current;
      }

      freeObject(H, unreached);
    }
  }
}

void hl_collectGarbage(struct hl_State* H) {
#ifdef hl_DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  markRoots(H);
  traceReferences(H);
  hl_tableRemoveUnmarked(&H->strings);
  sweep(H);

  H->nextGc = H->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef hl_DEBUG_LOG_GC
  printf("Collected %zu bytes (from %zu to %zu) next at %zu.\n",
      before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGc);
  printf("-- gc end\n");
#endif
}

void hl_freeObjects(struct hl_State* H) {
  struct hl_Obj* object = H->objects;
  while (object != NULL) {
    struct hl_Obj* next = object->next;
    freeObject(H, object);
    object = next;
  }

  free(H->grayStack);
}

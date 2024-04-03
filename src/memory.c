#include "memory.h"

#include <stdlib.h>

#ifdef DEBUG_LOG_GC
#include <stdio.h>

#include "debug.h"
#endif

#include "object.h"
#include "compiler.h"
#include "table.h"

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(struct State* H, void* pointer, size_t oldSize, size_t newSize) {
  H->bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage(H);
#else
    if (H->bytesAllocated > H->nextGc) {
      collectGarbage(H);
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

static void freeObject(struct State* H, struct Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case OBJ_ARRAY: {
      struct Array* array = (struct Array*)object;
      freeValueArray(H, &array->values);
      FREE(H, struct Array, array);
      break;
    }
    case OBJ_ENUM: {
      struct Enum* enoom = (struct Enum*)object;
      freeTable(H, &enoom->values);
      FREE(H, struct Enum, object);
      break;
    }
    case OBJ_STRUCT: {
      struct Struct* strooct = (struct Struct*)object;
      freeTable(H, &strooct->defaultFields);
      freeTable(H, &strooct->methods);
      freeTable(H, &strooct->staticMethods);
      FREE(H, struct Struct, object);
      break;
    }
    case OBJ_INSTANCE: {
      struct Instance* instance = (struct Instance*)object;
      freeTable(H, &instance->fields);
      FREE(H, struct Instance, object);
      break;
    }
    case OBJ_CLOSURE: {
      struct Closure* closure = (struct Closure*)object;
      FREE_ARRAY(
          H, struct Upvalue*, closure->upvalues, closure->upvalueCount);
      FREE(H, struct Closure, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(H, struct Upvalue, object);
      break;
    }
    case OBJ_FUNCTION: {
      struct Function* function = (struct Function*)object;
      FREE_ARRAY(H, u8, function->bc, function->bcCapacity);
      FREE_ARRAY(H, s32, function->lines, function->bcCapacity);
      freeValueArray(H, &function->constants);
      FREE(H, struct Function, object);
      break;
    }
    case OBJ_BOUND_METHOD: {
      FREE(H, struct BoundMethod, object);
      break;
    }
    case OBJ_CFUNCTION: {
      FREE(H, struct CFunctionBinding, object);
      break;
    }
    case OBJ_STRING: {
      struct String* string = (struct String*)object;
      FREE_ARRAY(H, char, string->chars, string->length + 1);
      FREE(H, struct String, object);
      break;
    }
  }
}

void markObject(struct State* H, struct Obj* object) {
  if (object == NULL) {
    return;
  }

  if (object->isMarked) {
    return;
  }

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(NEW_OBJ(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (H->grayCapacity < H->grayCount + 1) {
    H->grayCapacity = GROW_CAPACITY(H->grayCapacity);
    H->grayStack = (struct Obj**)realloc(
        H->grayStack, sizeof(struct Obj*) * H->grayCapacity);
    if (H->grayStack == NULL) {
      exit(1);
    }
  }

  H->grayStack[H->grayCount++] = object;
}

void markValue(struct State* H, Value value) {
  if (IS_OBJ(value)) {
    markObject(H, AS_OBJ(value));
  }
}

static void markArray(struct State* H, struct ValueArray* array) {
  for (s32 i = 0; i < array->count; i++) {
    markValue(H, array->values[i]);
  }
}

static void blackenObject(struct State* H, struct Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(NEW_OBJ(object));
  printf("\n");
#endif

  switch (object->type) {
    // No references.
    case OBJ_CFUNCTION:
    case OBJ_STRING:
      break;
    case OBJ_UPVALUE:
      markValue(H, ((struct Upvalue*)object)->closed);
      break;
    case OBJ_FUNCTION: {
      struct Function* function = (struct Function*)object;
      markObject(H, (struct Obj*)function->name);
      markArray(H, &function->constants);
      break;
    }
    case OBJ_BOUND_METHOD: {
      struct BoundMethod* bound = (struct BoundMethod*)object;
      markValue(H, bound->receiver);
      markObject(H, (struct Obj*)bound->method);
      break;
    }
    case OBJ_CLOSURE: {
      struct Closure* closure = (struct Closure*)object;
      markObject(H, (struct Obj*)closure->function);
      for (s32 i = 0; i < closure->upvalueCount; i++) {
        markObject(H, (struct Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_STRUCT: {
      struct Struct* strooct = (struct Struct*)object;
      markObject(H, (struct Obj*)strooct->name);
      markTable(H, &strooct->defaultFields);
      markTable(H, &strooct->methods);
      markTable(H, &strooct->staticMethods);
      break;
    }
    case OBJ_INSTANCE: {
      struct Instance* instance = (struct Instance*)object;
      markObject(H, (struct Obj*)instance->strooct);
      markTable(H, &instance->fields);
      break;
    }
    case OBJ_ENUM: {
      struct Enum* enoom = (struct Enum*)object;
      markObject(H, (struct Obj*)enoom->name);
      markTable(H, &enoom->values);
      break;
    }
    case OBJ_ARRAY: {
      struct Array* array = (struct Array*)object;
      markArray(H, &array->values);
      break;
    }
  }
}

static void markRoots(struct State* H) {
  for (Value* slot = H->stack; slot < H->stackTop; slot++) {
    markValue(H, *slot);
  }

  for (s32 i = 0; i < H->frameCount; i++) {
    markObject(H, (struct Obj*)H->frames[i].closure);
  }

  for (struct Upvalue* upvalue = H->openUpvalues;
      upvalue != NULL;
      upvalue = upvalue->next) {
    markObject(H, (struct Obj*)upvalue);
  }

  markTable(H, &H->globals);
  markCompilerRoots(H, H->parser);
}

static void traceReferences(struct State* H) {
  while (H->grayCount > 0) {
    struct Obj* object = H->grayStack[--H->grayCount];
    blackenObject(H, object);
  }
}

static void sweep(struct State* H) {
  struct Obj* previous = NULL;
  struct Obj* current = H->objects;

  while (current != NULL) {
    if (current->isMarked) {
      current->isMarked = false;
      previous = current;
      current = current->next;
    } else {
      struct Obj* unreached = current;
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

void collectGarbage(struct State* H) {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = H->bytesAllocated;
#endif

  markRoots(H);
  traceReferences(H);
  tableRemoveUnmarked(&H->strings);
  sweep(H);

  H->nextGc = H->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("Collected %zu bytes (from %zu to %zu) next at %zu.\n",
      before - H->bytesAllocated, before, H->bytesAllocated, H->nextGc);
  printf("-- gc end\n");
#endif
}

void freeObjects(struct State* H) {
  struct Obj* object = H->objects;
  while (object != NULL) {
    struct Obj* next = object->next;
    freeObject(H, object);
    object = next;
  }

  free(H->grayStack);
}

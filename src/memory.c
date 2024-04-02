#include "memory.h"

#include <stdlib.h>

#ifdef hl_DEBUG_LOG_GC
#include <stdio.h>

#include "debug.h"
#endif

#include "compiler.h"
#include "chunk.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

void* hl_reallocate(void* pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef hl_DEBUG_STRESS_GC
    hl_collectGarbage();
#else
    if (vm.bytesAllocated > vm.nextGc) {
      hl_collectGarbage();
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

static void freeObject(struct hl_Obj* object) {
#ifdef hl_DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case hl_OBJ_ARRAY: {
      struct hl_Array* array = (struct hl_Array*)object;
      hl_freeValueArray(&array->values);
      hl_FREE(struct hl_Array, array);
      break;
    }
    case hl_OBJ_ENUM: {
      struct hl_Enum* enoom = (struct hl_Enum*)object;
      hl_freeTable(&enoom->values);
      hl_FREE(struct hl_Enum, object);
      break;
    }
    case hl_OBJ_STRUCT: {
      struct hl_Struct* strooct = (struct hl_Struct*)object;
      hl_freeTable(&strooct->defaultFields);
      hl_freeTable(&strooct->methods);
      hl_freeTable(&strooct->staticMethods);
      hl_FREE(struct hl_Struct, object);
      break;
    }
    case hl_OBJ_INSTANCE: {
      struct hl_Instance* instance = (struct hl_Instance*)object;
      hl_freeTable(&instance->fields);
      hl_FREE(struct hl_Instance, object);
      break;
    }
    case hl_OBJ_CLOSURE: {
      struct hl_Closure* closure = (struct hl_Closure*)object;
      hl_FREE_ARRAY(
          struct hl_Upvalue*, closure->upvalues, closure->upvalueCount);
      hl_FREE(struct hl_Closure, object);
      break;
    }
    case hl_OBJ_UPVALUE: {
      hl_FREE(struct hl_Upvalue, object);
      break;
    }
    case hl_OBJ_FUNCTION: {
      struct hl_Function* function = (struct hl_Function*)object;
      hl_freeChunk(&function->chunk);
      hl_FREE(struct hl_Function, object);
      break;
    }
    case hl_OBJ_BOUND_METHOD: {
      hl_FREE(struct hl_BoundMethod, object);
      break;
    }
    case hl_OBJ_CFUNCTION: {
      hl_FREE(struct hl_CFunctionBinding, object);
      break;
    }
    case hl_OBJ_STRING: {
      struct hl_String* string = (struct hl_String*)object;
      hl_FREE_ARRAY(char, string->chars, string->length + 1);
      hl_FREE(struct hl_String, object);
      break;
    }
  }
}

void hl_markObject(struct hl_Obj* object) {
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

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = hl_GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (struct hl_Obj**)realloc(
        vm.grayStack, sizeof(struct hl_Obj*) * vm.grayCapacity);
    if (vm.grayStack == NULL) {
      exit(1);
    }
  }

  vm.grayStack[vm.grayCount++] = object;
}

void hl_markValue(hl_Value value) {
  if (hl_IS_OBJ(value)) {
    hl_markObject(hl_AS_OBJ(value));
  }
}

static void markArray(struct hl_ValueArray* array) {
  for (s32 i = 0; i < array->count; i++) {
    hl_markValue(array->values[i]);
  }
}

static void blackenObject(struct hl_Obj* object) {
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
      hl_markValue(((struct hl_Upvalue*)object)->closed);
      break;
    case hl_OBJ_FUNCTION: {
      struct hl_Function* function = (struct hl_Function*)object;
      hl_markObject((struct hl_Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }
    case hl_OBJ_BOUND_METHOD: {
      struct hl_BoundMethod* bound = (struct hl_BoundMethod*)object;
      hl_markValue(bound->receiver);
      hl_markObject((struct hl_Obj*)bound->method);
      break;
    }
    case hl_OBJ_CLOSURE: {
      struct hl_Closure* closure = (struct hl_Closure*)object;
      hl_markObject((struct hl_Obj*)closure->function);
      for (s32 i = 0; i < closure->upvalueCount; i++) {
        hl_markObject((struct hl_Obj*)closure->upvalues[i]);
      }
      break;
    }
    case hl_OBJ_STRUCT: {
      struct hl_Struct* strooct = (struct hl_Struct*)object;
      hl_markObject((struct hl_Obj*)strooct->name);
      hl_markTable(&strooct->defaultFields);
      hl_markTable(&strooct->methods);
      hl_markTable(&strooct->staticMethods);
      break;
    }
    case hl_OBJ_INSTANCE: {
      struct hl_Instance* instance = (struct hl_Instance*)object;
      hl_markObject((struct hl_Obj*)instance->strooct);
      hl_markTable(&instance->fields);
      break;
    }
    case hl_OBJ_ENUM: {
      struct hl_Enum* enoom = (struct hl_Enum*)object;
      hl_markObject((struct hl_Obj*)enoom->name);
      hl_markTable(&enoom->values);
      break;
    }
    case hl_OBJ_ARRAY: {
      struct hl_Array* array = (struct hl_Array*)object;
      markArray(&array->values);
      break;
    }
  }
}

static void markRoots() {
  for (hl_Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    hl_markValue(*slot);
  }

  for (s32 i = 0; i < vm.frameCount; i++) {
    hl_markObject((struct hl_Obj*)vm.frames[i].closure);
  }

  for (struct hl_Upvalue* upvalue = vm.openUpvalues;
      upvalue != NULL;
      upvalue = upvalue->next) {
    hl_markObject((struct hl_Obj*)upvalue);
  }

  hl_markTable(&vm.globals);
  hl_markCompilerRoots(&vm.parser);
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    struct hl_Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void sweep() {
  struct hl_Obj* previous = NULL;
  struct hl_Obj* current = vm.objects;

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
        vm.objects = current;
      }

      freeObject(unreached);
    }
  }
}

void hl_collectGarbage() {
#ifdef hl_DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  markRoots();
  traceReferences();
  hl_tableRemoveUnmarked(&vm.strings);
  sweep();

  vm.nextGc = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef hl_DEBUG_LOG_GC
  printf("Collected %zu bytes (from %zu to %zu) next at %zu.\n",
      before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGc);
  printf("-- gc end\n");
#endif
}

void hl_freeObjects() {
  struct hl_Obj* object = vm.objects;
  while (object != NULL) {
    struct hl_Obj* next = object->next;
    freeObject(object);
    object = next;
  }

  free(vm.grayStack);
}

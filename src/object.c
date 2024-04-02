#include "object.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) (type*)allocateObject(sizeof(type), objectType)

static struct hl_Obj* allocateObject(size_t size, enum hl_ObjType type) {
  struct hl_Obj* object = (struct hl_Obj*)hl_reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;
  
  object->next = vm.objects;
  vm.objects = object;

#ifdef hl_DEBUG_LOG_GC
  printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

  return object;
}

struct hl_Array* hl_newArray() {
  struct hl_Array* array = ALLOCATE_OBJ(struct hl_Array, hl_OBJ_ARRAY);
  hl_initValueArray(&array->values);
  return array;
}

struct hl_Enum* hl_newEnum(struct hl_String* name) {
  struct hl_Enum* enoom = ALLOCATE_OBJ(struct hl_Enum, hl_OBJ_ENUM);
  enoom->name = name;
  hl_initTable(&enoom->values);
  return enoom;
}

struct hl_BoundMethod* hl_newBoundMethod(
    hl_Value receiver, struct hl_Closure* method) {
  struct hl_BoundMethod* bound = ALLOCATE_OBJ(struct hl_BoundMethod, hl_OBJ_BOUND_METHOD);
  bound->receiver = receiver;
  bound->method = method;
  return bound;
}

struct hl_Struct* hl_newStruct(struct hl_String* name) {
  struct hl_Struct* strooct = ALLOCATE_OBJ(struct hl_Struct, hl_OBJ_STRUCT);

  strooct->name = name;
  hl_initTable(&strooct->defaultFields);
  hl_initTable(&strooct->methods);
  hl_initTable(&strooct->staticMethods);
  return strooct;
}

struct hl_Instance* hl_newInstance(struct hl_Struct* strooct) {
  struct hl_Instance* instance = ALLOCATE_OBJ(struct hl_Instance, hl_OBJ_INSTANCE);
  instance->strooct = strooct;
  hl_initTable(&instance->fields);
  hl_copyTable(&instance->fields, &strooct->defaultFields);
  return instance;
}

struct hl_Closure* hl_newClosure(struct hl_Function* function) {
  struct hl_Upvalue** upvalues = hl_ALLOCATE(
      struct hl_Upvalue*, function->upvalueCount);
  for (s32 i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  struct hl_Closure* closure = ALLOCATE_OBJ(struct hl_Closure, hl_OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

struct hl_Upvalue* hl_newUpvalue(hl_Value* slot) {
  struct hl_Upvalue* upvalue = ALLOCATE_OBJ(struct hl_Upvalue, hl_OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->closed = hl_NEW_NIL;
  upvalue->next = NULL;
  return upvalue;
}

struct hl_Function* hl_newFunction() {
  struct hl_Function* function = ALLOCATE_OBJ(struct hl_Function, hl_OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  hl_initChunk(&function->chunk);
  return function;
}

struct hl_CFunctionBinding* hl_newCFunctionBinding(hl_CFunction cFunc) {
  struct hl_CFunctionBinding* cFunction = ALLOCATE_OBJ(struct hl_CFunctionBinding, hl_OBJ_CFUNCTION);
  cFunction->cFunc = cFunc;
  return cFunction;
}

static struct hl_String* allocateString(char* chars, s32 length, u32 hash) {
  struct hl_String* string = ALLOCATE_OBJ(struct hl_String, hl_OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  
  hl_push(hl_NEW_OBJ(string));
  hl_tableSet(&vm.strings, string, hl_NEW_NIL);
  hl_pop();

  return string;
}

static u32 hashString(const char* key, int length) {
  u32 hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (u8)key[i];
    hash *= 16777619;
  }
  return hash;
}

struct hl_String* hl_copyString(const char* chars, s32 length) {
  u32 hash = hashString(chars, length);
  struct hl_String* interned = hl_tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    return interned;
  }
  char* ownedChars = hl_ALLOCATE(char, length + 1);
  memcpy(ownedChars, chars, length);
  ownedChars[length] = '\0';
  return allocateString(ownedChars, length, hash);
}

struct hl_String* hl_takeString(char* chars, s32 length) {
  u32 hash = hashString(chars, length);
  struct hl_String* interned = hl_tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    hl_FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocateString(chars, length, hash);
}

static void printFunction(struct hl_Function* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<function %s %p>", function->name->chars, function);
}

void hl_printObject(hl_Value value) {
  switch (hl_OBJ_TYPE(value)) {
    case hl_OBJ_CLOSURE:
      printFunction(hl_AS_CLOSURE(value)->function);
      break;
    case hl_OBJ_UPVALUE:
      printf("<upvalue %p>", hl_AS_OBJ(value));
      break;
    case hl_OBJ_FUNCTION:
      printFunction(hl_AS_FUNCTION(value));
      break;
    case hl_OBJ_BOUND_METHOD:
      printFunction(hl_AS_BOUND_METHOD(value)->method->function);
      break;
    case hl_OBJ_CFUNCTION:
      printf("<cfunction %p>", hl_AS_OBJ(value));
      break;
    case hl_OBJ_STRING:
      printf("%s", hl_AS_CSTRING(value));
      break;
    case hl_OBJ_STRUCT:
      printf("<struct %s>", hl_AS_STRUCT(value)->name->chars);
      break;
    case hl_OBJ_INSTANCE:
      printf("<%s instance %p>",
          hl_AS_INSTANCE(value)->strooct->name->chars, hl_AS_OBJ(value));
      break;
    case hl_OBJ_ENUM:
      printf("<enum %s>", hl_AS_ENUM(value)->name->chars);
      break;
    case hl_OBJ_ARRAY:
      printf("<array %p>", hl_AS_OBJ(value));
      break;
  }
}

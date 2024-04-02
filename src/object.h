#ifndef _HOBBYL_OBJECT_H
#define _HOBBYL_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define hl_OBJ_TYPE(value)        (hl_AS_OBJ(value)->type)

#define hl_IS_CLOSURE(value)      isObjOfType(value, hl_OBJ_CLOSURE)
#define hl_IS_FUNCTION(value)     isObjOfType(value, hl_OBJ_FUNCTION)
#define hl_IS_CFUNCTION(value)    isObjOfType(value, hl_OBJ_CFUNCTION)
#define hl_IS_BOUND_METHOD(value) isObjOfType(value, hl_OBJ_BOUND_METHOD)
#define hl_IS_STRING(value)       isObjOfType(value, hl_OBJ_STRING)
#define hl_IS_STRUCT(value)       isObjOfType(value, hl_OBJ_STRUCT)
#define hl_IS_INSTANCE(value)     isObjOfType(value, hl_OBJ_INSTANCE)
#define hl_IS_ENUM(value)         isObjOfType(value, hl_OBJ_ENUM)
#define hl_IS_ARRAY(value)        isObjOfType(value, hl_OBJ_ARRAY)

#define hl_AS_CLOSURE(value)      ((struct hl_Closure*)hl_AS_OBJ(value))
#define hl_AS_FUNCTION(value)     ((struct hl_Function*)hl_AS_OBJ(value))
#define hl_AS_CFUNCTION(value)    (((struct hl_CFunctionBinding*)hl_AS_OBJ(value))->cFunc)
#define hl_AS_BOUND_METHOD(value) ((struct hl_BoundMethod*)hl_AS_OBJ(value))
#define hl_AS_STRING(value)       ((struct hl_String*)hl_AS_OBJ(value))
#define hl_AS_CSTRING(value)      (hl_AS_STRING(value)->chars)
#define hl_AS_STRUCT(value)       ((struct hl_Struct*)hl_AS_OBJ(value))
#define hl_AS_INSTANCE(value)     ((struct hl_Instance*)hl_AS_OBJ(value))
#define hl_AS_ENUM(value)         ((struct hl_Enum*)hl_AS_OBJ(value))
#define hl_AS_ARRAY(value)        ((struct hl_Array*)hl_AS_OBJ(value))

enum hl_ObjType {
  hl_OBJ_CLOSURE,
  hl_OBJ_UPVALUE,
  hl_OBJ_FUNCTION,
  hl_OBJ_CFUNCTION,
  hl_OBJ_BOUND_METHOD,
  hl_OBJ_STRING,
  hl_OBJ_STRUCT,
  hl_OBJ_INSTANCE,
  hl_OBJ_ENUM,
  hl_OBJ_ARRAY,
};

struct hl_Obj {
  enum hl_ObjType type;
  bool isMarked;
  struct hl_Obj* next;
};

struct hl_Function {
  struct hl_Obj obj;
  u8 arity;
  u8 upvalueCount;
  struct hl_Chunk chunk;
  struct hl_String* name;
};

struct hl_Closure {
  struct hl_Obj obj;
  struct hl_Function* function;
  struct hl_Upvalue** upvalues;
  u8 upvalueCount;
};

struct hl_Upvalue {
  struct hl_Obj obj;
  hl_Value* location;
  hl_Value closed;
  struct hl_Upvalue* next;
};

typedef hl_Value (*hl_CFunction)(s32 argCount, hl_Value* value, bool* failed);

struct hl_CFunctionBinding {
  struct hl_Obj obj;
  hl_CFunction cFunc;
};

struct hl_String {
  struct hl_Obj obj;
  s32 length;
  char* chars;
  u32 hash;
};

struct hl_Struct {
  struct hl_Obj obj;
  struct hl_String* name;
  struct hl_Table defaultFields;
  struct hl_Table methods;
  struct hl_Table staticMethods;
};

struct hl_Instance {
  struct hl_Obj obj;
  struct hl_Struct* strooct;
  struct hl_Table fields;
};

struct hl_BoundMethod {
  struct hl_Obj obj;
  hl_Value receiver;
  struct hl_Closure* method;
};

struct hl_Enum {
  struct hl_Obj obj;
  struct hl_String* name;
  struct hl_Table values;
};

struct hl_Array {
  struct hl_Obj obj;
  struct hl_ValueArray values;
};

struct hl_Array* hl_newArray();
struct hl_Enum* hl_newEnum(struct hl_String* name);
struct hl_Closure* hl_newClosure(struct hl_Function* function);
struct hl_Upvalue* hl_newUpvalue(hl_Value* slot);
struct hl_Function* hl_newFunction();
struct hl_CFunctionBinding* hl_newCFunctionBinding(hl_CFunction cFunc);
struct hl_BoundMethod* hl_newBoundMethod(
    hl_Value receiver, struct hl_Closure* method);
struct hl_String* hl_copyString(const char* chars, int length);
struct hl_String* hl_takeString(char* chars, int length);
struct hl_Struct* hl_newStruct(struct hl_String* name);
struct hl_Instance* hl_newInstance(struct hl_Struct* strooct);
void hl_printObject(hl_Value value);

static inline bool isObjOfType(hl_Value value, enum hl_ObjType type) {
  return hl_IS_OBJ(value) && hl_AS_OBJ(value)->type == type;
}

#endif // _HOBBYL_OBJECT_H

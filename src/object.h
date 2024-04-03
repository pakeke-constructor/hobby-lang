#ifndef _HOBBYL_OBJECT_H
#define _HOBBYL_OBJECT_H

#include <string.h>

#include "common.h"
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

#ifdef NAN_BOXING

#define hl_SIGN_BIT ((uint64_t)0x8000000000000000)
#define hl_QNAN     ((u64)0x7ffc000000000000)
#define hl_TAG_NIL   1
#define hl_TAG_FALSE 2
#define hl_TAG_TRUE  3

typedef u64 hl_Value;

#define hl_IS_BOOL(value)     (((value) | 1) == hl_NEW_TRUE)
#define hl_IS_NIL(value)      ((value) == hl_NEW_NIL)
#define hl_IS_NUMBER(value)   (((value) & hl_QNAN) != hl_QNAN)
#define hl_IS_OBJ(value) (((value) & (hl_QNAN | hl_SIGN_BIT)) == (hl_QNAN | hl_SIGN_BIT))

#define hl_AS_BOOL(value)     ((value) == hl_NEW_TRUE)
#define hl_AS_NUMBER(value)   valueToNumber(value)
#define hl_AS_OBJ(value) ((struct hl_Obj*)(uintptr_t)((value) & ~(hl_SIGN_BIT | hl_QNAN)))

#define hl_NEW_FALSE          ((hl_Value)(u64)(hl_QNAN | hl_TAG_FALSE))
#define hl_NEW_TRUE           ((hl_Value)(u64)(hl_QNAN | hl_TAG_TRUE))
#define hl_NEW_NIL            ((hl_Value)(u64)(hl_QNAN | hl_TAG_NIL))
#define hl_NEW_BOOL(boolean)  (boolean ? hl_NEW_TRUE : hl_NEW_FALSE)
#define hl_NEW_NUMBER(number) numberToValue(number)
#define hl_NEW_OBJ(obj) (hl_Value)(hl_SIGN_BIT | hl_QNAN | (u64)(uintptr_t)(obj))

static inline f64 valueToNumber(hl_Value value) {
  f64 number;
  memcpy(&number, &value, sizeof(hl_Value));
  return number;
}

static inline hl_Value numberToValue(f64 number) {
  hl_Value value;
  memcpy(&value, &number, sizeof(f64));
  return value;
}

#else

enum hl_ValueType {
  hl_VALTYPE_BOOL,
  hl_VALTYPE_NIL,
  hl_VALTYPE_NUMBER,
  hl_VALTYPE_OBJ,
};

typedef struct {
  enum hl_ValueType type;
  union {
    bool boolean;
    f64 number;
    struct hl_Obj* obj;
  } as;
} hl_Value;

#define hl_IS_BOOL(value)    ((value).type == hl_VALTYPE_BOOL)
#define hl_IS_NIL(value)     ((value).type == hl_VALTYPE_NIL)
#define hl_IS_NUMBER(value)  ((value).type == hl_VALTYPE_NUMBER)
#define hl_IS_OBJ(value)     ((value).type == hl_VALTYPE_OBJ)

#define hl_AS_BOOL(value)    ((value).as.boolean)
#define hl_AS_NUMBER(value)  ((value).as.number)
#define hl_AS_OBJ(value)     ((value).as.obj)

#define hl_NEW_BOOL(value)   ((struct hl_Value){hl_VALTYPE_BOOL, {.boolean = value}})
#define hl_NEW_NIL           ((struct hl_Value){hl_VALTYPE_NIL, {.number = 0}})
#define hl_NEW_NUMBER(value) ((struct hl_Value){hl_VALTYPE_NUMBER, {.number = value}})
#define hl_NEW_OBJ(value)    ((struct hl_Value){hl_VALTYPE_OBJ, {.obj = (struct hl_Obj*)value}})

#endif

struct hl_ValueArray {
  s32 capacity;
  s32 count;
  hl_Value* values;
};

#define hl_FRAMES_MAX 64
#define hl_STACK_MAX (hl_FRAMES_MAX * hl_U8_COUNT)

struct hl_Entry {
  struct hl_String* key;
  hl_Value value;
};

struct hl_Table {
  s32 count;
  s32 capacity;
  struct hl_Entry* entries;
};

struct hl_CallFrame {
  struct hl_Closure* closure;
  u8* ip;
  hl_Value* slots;
};

struct hl_State {
  struct hl_CallFrame frames[hl_FRAMES_MAX];
  s32 frameCount;

  hl_Value stack[hl_STACK_MAX];
  hl_Value* stackTop;
  struct hl_Table globals;
  struct hl_Table strings;
  struct hl_Upvalue* openUpvalues;

  size_t bytesAllocated;
  size_t nextGc;

  struct hl_Obj* objects;

  s32 grayCount;
  s32 grayCapacity;
  struct hl_Obj** grayStack;

  struct hl_Parser* parser;
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

  s32 bcCount;
  s32 bcCapacity;
  u8* bc;
  s32* lines;

  struct hl_ValueArray constants;
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


typedef hl_Value (*hl_CFunction)(struct hl_State* H);

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

void hl_initValueArray(struct hl_ValueArray* array);
void hl_copyValueArray(struct hl_State* H, struct hl_ValueArray* dest, struct hl_ValueArray* src);
void hl_writeValueArray(struct hl_State* H, struct hl_ValueArray* array, hl_Value value);
void hl_freeValueArray(struct hl_State* H, struct hl_ValueArray* array);
void hl_reserveValueArray(struct hl_State* H, struct hl_ValueArray* array, s32 size);
void hl_printValue(hl_Value value);
bool hl_valuesEqual(hl_Value a, hl_Value b);

struct hl_Array* hl_newArray(struct hl_State* H);
struct hl_Enum* hl_newEnum(struct hl_State* H, struct hl_String* name);
struct hl_String* hl_copyString(struct hl_State* H, const char* chars, int length);
struct hl_String* hl_takeString(struct hl_State* H, char* chars, int length);
struct hl_Struct* hl_newStruct(struct hl_State* H, struct hl_String* name);
struct hl_Instance* hl_newInstance(struct hl_State* H, struct hl_Struct* strooct);

struct hl_Closure* hl_newClosure(struct hl_State* H, struct hl_Function* function);
struct hl_Upvalue* hl_newUpvalue(struct hl_State* H, hl_Value* slot);
struct hl_Function* hl_newFunction(struct hl_State* H);
struct hl_CFunctionBinding* hl_newCFunctionBinding(struct hl_State* H, hl_CFunction cFunc);
struct hl_BoundMethod* hl_newBoundMethod(
    struct hl_State* H, hl_Value receiver, struct hl_Closure* method);
void hl_writeBytecode(struct hl_State* H, struct hl_Function* function, u8 byte, s32 line);
s32 hl_addFunctionConstant(
    struct hl_State* H, struct hl_Function* function, hl_Value value);

void hl_printObject(hl_Value value);

static inline bool isObjOfType(hl_Value value, enum hl_ObjType type) {
  return hl_IS_OBJ(value) && hl_AS_OBJ(value)->type == type;
}

#endif // _HOBBYL_OBJECT_H

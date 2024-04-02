#ifndef _HOBBYL_VALUE_H
#define _HOBBYL_VALUE_H

#include "common.h"

#ifdef NAN_BOXING

#include <string.h>

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

void hl_initValueArray(struct hl_ValueArray* array);
void hl_copyValueArray(struct hl_ValueArray* dest, struct hl_ValueArray* src);
void hl_writeValueArray(struct hl_ValueArray* array, hl_Value value);
void hl_freeValueArray(struct hl_ValueArray* array);
void hl_printValue(hl_Value value);
void hl_reserveValueArray(struct hl_ValueArray* array, s32 size);
bool hl_valuesEqual(hl_Value a, hl_Value b);

#endif // _HOBBYL_VALUE_H

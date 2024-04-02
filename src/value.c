#include "value.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void hl_initValueArray(struct hl_ValueArray* array) {
  array->capacity = 0;
  array->count = 0;
  array->values = NULL;
}

void hl_copyValueArray(struct hl_ValueArray* dest, struct hl_ValueArray* src) {
  hl_initValueArray(dest);

  dest->count = src->count;
  dest->capacity = src->capacity;
  dest->values = hl_ALLOCATE(hl_Value, dest->capacity);

  for (s32 i = 0; i < dest->count; i++) {
    dest->values[i] = src->values[i];
  }
}

void hl_writeValueArray(struct hl_ValueArray* array, hl_Value value) {
  if (array->capacity < array->count + 1) {
    s32 oldCapacity = array->capacity;
    array->capacity = hl_GROW_CAPACITY(oldCapacity);
    array->values = hl_GROW_ARRAY(
        hl_Value, array->values, oldCapacity, array->capacity);
  }

  array->values[array->count++] = value;
}

void hl_freeValueArray(struct hl_ValueArray* array) {
  hl_FREE_ARRAY(hl_Value, array->values, array->capacity);
  hl_initValueArray(array);
}

hl_Value hl_valueToChars(hl_Value value) {
#ifdef NAN_BOXING
  if (hl_IS_BOOL(value)) {
    return hl_NEW_OBJ(hl_copyString(
      hl_AS_BOOL(value) ? "true" : "false",
      hl_AS_BOOL(value) ? 4 : 5));
  } else if (hl_IS_NIL(value)) {
    return hl_NEW_OBJ(hl_copyString("nil", 3));
  } else if (hl_IS_NUMBER(value)) {
    f64 number = hl_AS_NUMBER(value);
    if (isnan(number)) {
      return hl_NEW_OBJ(hl_takeString("nan", 3));
    }

    if (isinf(number)) {
      if (number > 0) {
        return hl_NEW_OBJ(hl_takeString("inf", 3));
      } else {
        return hl_NEW_OBJ(hl_takeString("-inf", 4));
      }
    }

    char buffer[24];
    s32 length = sprintf(buffer, "%.14g", number);
    return hl_NEW_OBJ(hl_copyString(buffer, length));
  } else if (hl_IS_OBJ(value)) {
    return hl_objectToChars(value);
  }
#else
  switch (value.type) {
    case hl_VALTYPE_NUMBER: {
      f64 number = hl_AS_NUMBER(value);
      if (isnan(number)) {
        return hl_NEW_OBJ(hl_takeString("nan", 3));
      }

      if (isinf(number)) {
        if (number > 0) {
          return hl_NEW_OBJ(hl_takeString("inf", 3));
        } else {
          return hl_NEW_OBJ(hl_takeString("-inf", 4));
        }
      }

      char buffer[24];
      s32 length = sprintf(buffer, "%.14g", number);
      return hl_NEW_OBJ(hl_copyString(buffer, length));
    }
    case hl_VALTYPE_BOOL:
      return hl_NEW_OBJ(hl_copyString(
        hl_AS_BOOL(value) ? "true" : "false",
        hl_AS_BOOL(value) ? 4 : 5));
    case hl_VALTYPE_NIL:
      return hl_NEW_OBJ(hl_copyString("nil", 3));
    case hl_VALTYPE_OBJ:
      return hl_objectToChars(value);
  }
#endif
  return hl_NEW_NIL;
}

void hl_printValue(hl_Value value) {
  printf("%s", hl_AS_CSTRING(hl_valueToChars(value)));
}

bool hl_valuesEqual(hl_Value a, hl_Value b) {
#ifdef NAN_BOXING
  return a == b;
#else
  if (a.type != b.type) {
    return false;
  }
  
  switch (a.type) {
    case hl_VALTYPE_BOOL:   return hl_AS_BOOL(a) == hl_AS_BOOL(b);
    case hl_VALTYPE_NIL:    return true;
    case hl_VALTYPE_NUMBER: return hl_AS_NUMBER(a) == hl_AS_NUMBER(b);
    case hl_VALTYPE_OBJ:    return hl_AS_OBJ(a) == hl_AS_OBJ(b);
    default: return false;
  }
#endif
}

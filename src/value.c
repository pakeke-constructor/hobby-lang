#include "object.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"

void hl_initValueArray(struct hl_ValueArray* array) {
  array->capacity = 0;
  array->count = 0;
  array->values = NULL;
}

void hl_copyValueArray(struct hl_State* H, struct hl_ValueArray* dest, struct hl_ValueArray* src) {
  hl_initValueArray(dest);

  dest->count = src->count;
  dest->capacity = src->capacity;
  dest->values = hl_ALLOCATE(H, hl_Value, dest->capacity);

  for (s32 i = 0; i < dest->count; i++) {
    dest->values[i] = src->values[i];
  }
}

void hl_writeValueArray(struct hl_State* H, struct hl_ValueArray* array, hl_Value value) {
  if (array->capacity < array->count + 1) {
    s32 oldCapacity = array->capacity;
    array->capacity = hl_GROW_CAPACITY(oldCapacity);
    array->values = hl_GROW_ARRAY(
        H, hl_Value, array->values, oldCapacity, array->capacity);
  }

  array->values[array->count++] = value;
}

void hl_reserveValueArray(struct hl_State* H, struct hl_ValueArray* array, s32 size) {
  // Make sure the numbers stays at a power of 2.
  // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2Float
  size--;
  size |= size >> 1;
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
  size++;

  s32 newCapacity = size < 8 ? 8 : size;
  s32 oldCapacity = array->capacity;
  array->capacity = newCapacity;
  array->values = hl_GROW_ARRAY(
      H, hl_Value, array->values, oldCapacity, array->capacity);
}

void hl_freeValueArray(struct hl_State* H, struct hl_ValueArray* array) {
  hl_FREE_ARRAY(H, hl_Value, array->values, array->capacity);
  hl_initValueArray(array);
}

void hl_printValue(hl_Value value) {
  if (hl_IS_BOOL(value)) {
    printf(hl_AS_BOOL(value) ? "true" : "false");
  } else if (hl_IS_NIL(value)) {
    printf("nil");
  } else if (hl_IS_NUMBER(value)) {
    printf("%.14g", hl_AS_NUMBER(value));
  } else if (hl_IS_OBJ(value)) {
    hl_printObject(value);
  }
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

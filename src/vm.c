#include "vm.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "opcodes.h"
#include "table.h"

#include "debug.h"

static void resetStack(struct hl_State* H) {
  H->stackTop = H->stack;
  H->frameCount = 0;
  H->openUpvalues = NULL;
}

static void runtimeError(struct hl_State* H, const char* format, ...) {
  for (s32 i = 0; i < H->frameCount; i++) {
    struct hl_CallFrame* frame = &H->frames[i];
    struct hl_Function* function = frame->closure->function;
    size_t instruction = frame->ip - function->bc - 1;
    fprintf(stderr, "[line #%d] in ", function->lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s\n", function->name->chars);
    }
  }

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  resetStack(H);
}

void hl_push(struct hl_State* H, hl_Value value) {
  *H->stackTop = value;
  H->stackTop++;
}

hl_Value hl_pop(struct hl_State* H) {
  H->stackTop--;
  return *H->stackTop;
}

static hl_Value peek(struct hl_State* H, s32 distance) {
  return H->stackTop[-1 - distance];
}

void hl_bindCFunction(struct hl_State* H, const char* name, hl_CFunction cFunction) {
  hl_push(H, hl_NEW_OBJ(hl_copyString(H, name, (s32)strlen(name))));
  hl_push(H, hl_NEW_OBJ(hl_newCFunctionBinding(H, cFunction)));
  hl_tableSet(H, &H->globals, hl_AS_STRING(H->stack[0]), H->stack[1]);
  hl_pop(H);
  hl_pop(H);
}

static hl_Value wrap_print(struct hl_State* H) {
  hl_printValue(peek(H, 0));
  printf("\n");
  return hl_NEW_NIL;
}

static hl_Value wrap_clock(hl_UNUSED struct hl_State* H) {
  return hl_NEW_NUMBER((f64)clock() / CLOCKS_PER_SEC);
}

void hl_initState(struct hl_State* H) {
  H->objects = NULL;

  H->bytesAllocated = 0;
  H->nextGc = 1024 * 1024;

  H->grayCount = 0;
  H->grayCapacity = 0;
  H->grayStack = NULL;

  resetStack(H);

  hl_initTable(&H->strings);
  hl_initTable(&H->globals);

  hl_bindCFunction(H, "clock", wrap_clock);
  hl_bindCFunction(H, "print", wrap_print);

  H->parser = hl_ALLOCATE(H, struct hl_Parser, 1);
}

void hl_freeState(struct hl_State* H) {
  hl_freeTable(H, &H->strings);
  hl_freeTable(H, &H->globals);
  hl_freeObjects(H);
  hl_FREE(H, struct hl_Parser, H->parser);
}

static bool call(struct hl_State* H, struct hl_Closure* closure, s32 argCount) {
  if (argCount != closure->function->arity) {
    runtimeError(H, "Expected %d arguments, but got %d.", closure->function->arity, argCount);
    return false;
  }

  if (H->frameCount == hl_FRAMES_MAX) {
    runtimeError(H, "Stack overflow.");
    return false;
  }

  struct hl_CallFrame* frame = &H->frames[H->frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->bc;
  frame->slots = H->stackTop - argCount - 1;
  return true;
}

static bool callValue(struct hl_State* H, hl_Value callee, s32 argCount) {
  if (hl_IS_OBJ(callee)) {
    switch (hl_OBJ_TYPE(callee)) {
      case hl_OBJ_BOUND_METHOD: {
        struct hl_BoundMethod* bound = hl_AS_BOUND_METHOD(callee);
        H->stackTop[-argCount - 1] = bound->receiver;
        return call(H, bound->method, argCount);
      }
      case hl_OBJ_CLOSURE:
        return call(H, hl_AS_CLOSURE(callee), argCount);
      case hl_OBJ_CFUNCTION: {
        hl_CFunction cFunction = hl_AS_CFUNCTION(callee);
        hl_Value result = cFunction(H);
        H->stackTop -= argCount + 1;
        hl_push(H, result);
        return true;
      }
      default:
        break;
    }
  }

  runtimeError(H, "Can only call functions.");
  return false;
}

static bool invokeFromStruct(
    struct hl_State* H,
    struct hl_Struct* strooct, struct hl_String* name, s32 argCount) {
  hl_Value method;
  if (!hl_tableGet(&strooct->methods, name, &method)) {
    runtimeError(H, "Undefined property '%d'.", name->chars);
    return false;
  }

  return call(H, hl_AS_CLOSURE(method), argCount);
}

static bool invoke(struct hl_State* H, struct hl_String* name, s32 argCount) {
  hl_Value receiver = peek(H, argCount);
  if (!hl_IS_INSTANCE(receiver)) {
    runtimeError(H, "Only instances have methods.");
    return false;
  }

  struct hl_Instance* instance = hl_AS_INSTANCE(receiver);

  hl_Value value;
  if (hl_tableGet(&instance->fields, name, &value)) {
    H->stackTop[-argCount - 1] = value;
    return callValue(H, value, argCount);
  }

  return invokeFromStruct(H, instance->strooct, name, argCount);
}

static bool bindMethod(struct hl_State* H, struct hl_Struct* strooct, struct hl_String* name) {
  hl_Value method;
  if (!hl_tableGet(&strooct->methods, name, &method)) {
    runtimeError(H, "Undefined property '%s'.", name->chars);
    return false;
  }

  struct hl_BoundMethod* bound = hl_newBoundMethod(H, peek(H, 0), hl_AS_CLOSURE(method));
  hl_pop(H);
  hl_push(H, hl_NEW_OBJ(bound));
  return true;
}

static struct hl_Upvalue* captureUpvalue(struct hl_State* H, hl_Value* local) {
  struct hl_Upvalue* previous = NULL;
  struct hl_Upvalue* current = H->openUpvalues;
  while (current != NULL && current->location > local) {
    previous = current;
    current = current->next;
  }

  if (current != NULL && current->location == local) {
    return current;
  }

  struct hl_Upvalue* newUpvalue = hl_newUpvalue(H, local);

  newUpvalue->next = current;
  if (previous == NULL) {
    H->openUpvalues = newUpvalue;
  } else {
    previous->next = newUpvalue;
  }

  return newUpvalue;
}

static void closeUpvalues(struct hl_State* H, hl_Value* last) {
  while (H->openUpvalues != NULL && H->openUpvalues->location >= last) {
    struct hl_Upvalue* upvalue = H->openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    H->openUpvalues = upvalue->next;
  }
}

static void defineMethod(struct hl_State* H, struct hl_String* name, struct hl_Table* table) {
  hl_Value method = peek(H, 0);
  hl_tableSet(H, table, name, method);
  hl_pop(H);
}

static bool setProperty(struct hl_State* H, struct hl_String* name) {
  if (!hl_IS_INSTANCE(peek(H, 1))) {
    runtimeError(H, "Can only use dot operator on instances.");
    return false;
  }

  struct hl_Instance* instance = hl_AS_INSTANCE(peek(H, 1));
  if (hl_tableSet(H, &instance->fields, name, peek(H, 0))) {
    runtimeError(H, "Cannot create new properties on instances at runtime.");
    return false;
  }

  return true;
}

static bool getProperty(struct hl_State* H, hl_Value object, struct hl_String* name, bool popValue) {
  if (hl_IS_OBJ(object)) {
    switch (hl_OBJ_TYPE(object)) {
      case hl_OBJ_INSTANCE: {
        struct hl_Instance* instance = hl_AS_INSTANCE(object);

        hl_Value value;
        if (hl_tableGet(&instance->fields, name, &value)) {
          if (popValue) {
            hl_pop(H); // Instance
          }
          hl_push(H, value);
          return true;
        }

        if (!bindMethod(H, instance->strooct, name)) {
          return false;
        }
        return true;
      }
      default:
        break;
    }
  }

  runtimeError(H, "Invalid target for the dot operator.");
  return false;
}

static bool getStatic(struct hl_State* H, hl_Value object, struct hl_String* name) {
  if (hl_IS_OBJ(object)) {
    switch (hl_OBJ_TYPE(object)) {
      case hl_OBJ_STRUCT: {
        struct hl_Struct* strooct = hl_AS_STRUCT(object);

        hl_Value value;
        if (hl_tableGet(&strooct->staticMethods, name, &value)) {
          hl_pop(H); // struct
          hl_push(H, value);
          return true;
        }

        runtimeError(H, "Static method '%s' does not exist.", name->chars);
        return false;
      }
      case hl_OBJ_ENUM: {
        struct hl_Enum* enoom = hl_AS_ENUM(object);

        hl_Value value;
        if (hl_tableGet(&enoom->values, name, &value)) {
          hl_pop(H); // enum
          hl_push(H, value);
          return true;
        }

        runtimeError(H, "Enum value '%s' does not exist.", name->chars);
        return false;
      }
      default:
        break;
    }
  }

  runtimeError(H, "Invalid target for the static operator.");
  return false;
}

static bool isFalsey(hl_Value value) {
  return hl_IS_NIL(value) || (hl_IS_BOOL(value) && !hl_AS_BOOL(value));
}

static void concatenate(struct hl_State* H) {
  struct hl_String* b = hl_AS_STRING(peek(H, 0));
  struct hl_String* a = hl_AS_STRING(peek(H, 1));

  s32 length = a->length + b->length;
  char* chars = hl_ALLOCATE(H, char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  struct hl_String* result = hl_takeString(H, chars, length);

  hl_pop(H);
  hl_pop(H);
  hl_push(H, hl_NEW_OBJ(result));
}

static enum hl_InterpretResult run(struct hl_State* H) {
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (u16)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->constants.values[READ_BYTE()])
#define READ_STRING() hl_AS_STRING(READ_CONSTANT())
#define BINARY_OP(outType, op) \
    do { \
      if (!hl_IS_NUMBER(peek(H, 0)) || !hl_IS_NUMBER(peek(H, 1))) { \
        runtimeError(H, "Operands must be numbers."); \
        return hl_RES_RUNTIME_ERR; \
      } \
      f32 b = hl_AS_NUMBER(hl_pop(H)); \
      f32 a = hl_AS_NUMBER(hl_pop(H)); \
      hl_push(H, outType(a op b)); \
    } while (false)

  struct hl_CallFrame* frame = &H->frames[H->frameCount - 1];

  while (true) {
#ifdef hl_DEBUG_TRACE_EXECUTION
    printf("        | ");
    for (hl_Value* slot = H->stack; slot < H->stackTop; slot++) {
      printf("[ ");
      hl_printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    hl_disassembleInstruction(
        frame->closure->function, (s32)(frame->ip - frame->closure->function->bc));
#endif
    u8 instruction;
    switch (instruction = READ_BYTE()) {
      case hl_OP_CONSTANT: {
        hl_Value constant = READ_CONSTANT();
        hl_push(H, constant);
        break;
      }
      case hl_OP_NIL:   hl_push(H, hl_NEW_NIL); break;
      case hl_OP_TRUE:  hl_push(H, hl_NEW_BOOL(true)); break;
      case hl_OP_FALSE: hl_push(H, hl_NEW_BOOL(false)); break;
      case hl_OP_POP: hl_pop(H); break;
      case hl_OP_ARRAY: {
        u8 elementCount = READ_BYTE();
        struct hl_Array* array = hl_newArray(H);
        hl_push(H, hl_NEW_OBJ(array));
        hl_reserveValueArray(H, &array->values, elementCount);
        for (u8 i = 1; i <= elementCount; i++) {
          hl_writeValueArray(H, &array->values, peek(H, elementCount - i + 1));
        }
        H->stackTop -= elementCount + 1;
        hl_push(H, hl_NEW_OBJ(array));
        break;
      }
      case hl_OP_GET_SUBSCRIPT: {
        if (!hl_IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Can only use subscript operator with numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        s32 index = hl_AS_NUMBER(peek(H, 0));

        if (!hl_IS_ARRAY(peek(H, 1))) {
          runtimeError(H, "Invalid target for subscript operator.");
          return hl_RES_RUNTIME_ERR;
        }

        struct hl_Array* array = hl_AS_ARRAY(peek(H, 1));

        if (index < 0 || index > array->values.count) {
          runtimeError(H, "Index out of bounds. Array size is %d, but tried accessing %d",
              array->values.count, index);
          return hl_RES_RUNTIME_ERR;
        }

        hl_pop(H); // Index
        hl_pop(H); // Array
        hl_push(H, array->values.values[index]);
        break;
      }
      case hl_OP_SET_SUBSCRIPT: {
        if (!hl_IS_NUMBER(peek(H, 1))) {
          runtimeError(H, "Can only use subscript operator with numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        s32 index = hl_AS_NUMBER(peek(H, 1));

        if (!hl_IS_ARRAY(peek(H, 2))) {
          runtimeError(H, "Invalid target for subscript operator.");
          return hl_RES_RUNTIME_ERR;
        }

        struct hl_Array* array = hl_AS_ARRAY(peek(H, 2));

        if (index < 0 || index > array->values.count) {
          runtimeError(H, "Index out of bounds. Array size is %d, but tried accessing %d",
              array->values.count, index);
          return hl_RES_RUNTIME_ERR;
        }

        array->values.values[index] = hl_pop(H);
        hl_pop(H); // Index
        hl_pop(H); // Array
        hl_push(H, array->values.values[index]);
        break;
      }
      case hl_OP_GET_GLOBAL: {
        struct hl_String* name = READ_STRING();
        hl_Value value;
        if (!hl_tableGet(&H->globals, name, &value)) {
          runtimeError(H, "Undefined variable '%s'.", name->chars);
          return hl_RES_RUNTIME_ERR;
        }
        hl_push(H, value);
        break;
      }
      case hl_OP_SET_GLOBAL: {
        struct hl_String* name = READ_STRING();
        if (hl_tableSet(H, &H->globals, name, peek(H, 0))) {
          hl_tableDelete(&H->globals, name);
          runtimeError(H, "Undefined variable '%s'.", name->chars);
          return hl_RES_RUNTIME_ERR;
        }
        break;
      }
      case hl_OP_DEFINE_GLOBAL: {
        struct hl_String* name = READ_STRING();
        hl_tableSet(H, &H->globals, name, peek(H, 0));
        hl_pop(H);
        break;
      }
      case hl_OP_GET_UPVALUE: {
        u8 slot = READ_BYTE();
        hl_push(H, *frame->closure->upvalues[slot]->location);
        break;
      }
      case hl_OP_SET_UPVALUE: {
        u8 slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(H, 0);
        break;
      }
      case hl_OP_GET_LOCAL: {
        u8 slot = READ_BYTE();
        hl_push(H, frame->slots[slot]);
        break;
      }
      case hl_OP_SET_LOCAL: {
        u8 slot = READ_BYTE();
        frame->slots[slot] = peek(H, 0);
        break;
      }
      case hl_OP_INIT_PROPERTY: {
        if (!setProperty(H, READ_STRING())) {
          return hl_RES_RUNTIME_ERR;
        }

        hl_pop(H); // Value
        break;
      }
      case hl_OP_GET_STATIC: {
        if (!getStatic(H, peek(H, 0), READ_STRING())) {
          return hl_RES_RUNTIME_ERR;
        }
        break;
      }
      case hl_OP_PUSH_PROPERTY:
      case hl_OP_GET_PROPERTY: {
        if (!getProperty(
            H, peek(H, 0), READ_STRING(), instruction == hl_OP_GET_PROPERTY)) {
          return hl_RES_RUNTIME_ERR;
        }
        break;
      }
      case hl_OP_SET_PROPERTY: {
        if (!setProperty(H, READ_STRING())) {
          return hl_RES_RUNTIME_ERR;
        }

        // Removing the instance while keeping the rhs value on top.
        hl_Value value = hl_pop(H);
        hl_pop(H);
        hl_push(H, value);
        break;
      }
      case hl_OP_DESTRUCT_ARRAY: {
        u8 index = READ_BYTE();

        if (!hl_IS_ARRAY(peek(H, 0))) {
          runtimeError(H, "Can only destruct arrays");
          return hl_RES_RUNTIME_ERR;
        }
        struct hl_Array* array = hl_AS_ARRAY(peek(H, 0));

        hl_push(H, array->values.values[index]);
        break;
      }
      case hl_OP_EQUAL: {
        hl_Value b = hl_pop(H);
        hl_Value a = hl_pop(H);
        hl_push(H, hl_NEW_BOOL(hl_valuesEqual(a, b)));
        break;
      }
      case hl_OP_NOT_EQUAL: {
        hl_Value b = hl_pop(H);
        hl_Value a = hl_pop(H);
        hl_push(H, hl_NEW_BOOL(!hl_valuesEqual(a, b)));
        break;
      }
      case hl_OP_CONCAT: {
        if (!hl_IS_STRING(peek(H, 0)) || !hl_IS_STRING(peek(H, 1))) {
          runtimeError(H, "Operands must be strings.");
          return hl_RES_RUNTIME_ERR;
        }
        concatenate(H);
        break;
      }
      case hl_OP_GREATER:       BINARY_OP(hl_NEW_BOOL, >); break;
      case hl_OP_GREATER_EQUAL: BINARY_OP(hl_NEW_BOOL, >=); break;
      case hl_OP_LESSER:        BINARY_OP(hl_NEW_BOOL, <); break;
      case hl_OP_LESSER_EQUAL:  BINARY_OP(hl_NEW_BOOL, <=); break;
      case hl_OP_ADD:           BINARY_OP(hl_NEW_NUMBER, +); break;
      case hl_OP_SUBTRACT:      BINARY_OP(hl_NEW_NUMBER, -); break;
      case hl_OP_MULTIPLY:      BINARY_OP(hl_NEW_NUMBER, *); break;
      case hl_OP_DIVIDE:        BINARY_OP(hl_NEW_NUMBER, /); break;
      case hl_OP_MODULO: {
        if (!hl_IS_NUMBER(peek(H, 0)) || !hl_IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Operands must be numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        f64 b = hl_AS_NUMBER(hl_pop(H));
        f64 a = hl_AS_NUMBER(hl_pop(H));
        hl_push(H, hl_NEW_NUMBER(fmod(a, b)));
        break;
      }
      case hl_OP_POW: {
        if (!hl_IS_NUMBER(peek(H, 0)) || !hl_IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Operands must be numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        f64 b = hl_AS_NUMBER(hl_pop(H));
        f64 a = hl_AS_NUMBER(hl_pop(H));
        hl_push(H, hl_NEW_NUMBER(pow(a, b)));
        break;
      }
      case hl_OP_NEGATE: {
        if (!hl_IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Operand must be a number.");
          return hl_RES_RUNTIME_ERR;
        }
        hl_push(H, hl_NEW_NUMBER(-hl_AS_NUMBER(hl_pop(H))));
        break;
      }
      case hl_OP_NOT: {
        hl_push(H, hl_NEW_BOOL(isFalsey(hl_pop(H))));
        break;
      }
      case hl_OP_JUMP: {
        u16 offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case hl_OP_JUMP_IF_FALSE: {
        u16 offset = READ_SHORT();
        if (isFalsey(peek(H, 0))) {
          frame->ip += offset;
        }
        break;
      }
      case hl_OP_INEQUALITY_JUMP: {
        u16 offset = READ_SHORT();
        hl_Value b = hl_pop(H);
        hl_Value a = peek(H, 0);
        if (!hl_valuesEqual(a, b)) {
          frame->ip += offset;
        }
        break;
      }
      case hl_OP_LOOP: {
        u16 offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case hl_OP_CALL: {
        s32 argCount = READ_BYTE();
        if (!callValue(H, peek(H, argCount), argCount)) {
          return hl_RES_RUNTIME_ERR;
        }
        frame = &H->frames[H->frameCount - 1];
        break;
      }
      case hl_OP_INSTANCE: {
        if (!hl_IS_STRUCT(peek(H, 0))) {
          runtimeError(H, "Can only use struct initialization on structs.");
          return hl_RES_RUNTIME_ERR;
        }
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(H, 0));
        hl_Value instance = hl_NEW_OBJ(hl_newInstance(H, strooct));
        hl_pop(H); // Struct
        hl_push(H, instance);
        break;
      }
      case hl_OP_CLOSURE: {
        struct hl_Function* function = hl_AS_FUNCTION(READ_CONSTANT());
        struct hl_Closure* closure = hl_newClosure(H, function);
        hl_push(H, hl_NEW_OBJ(closure));
        for (s32 i = 0; i < closure->upvalueCount; i++) {
          u8 isLocal = READ_BYTE();
          u8 index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(H, frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case hl_OP_CLOSE_UPVALUE: {
        closeUpvalues(H, H->stackTop - 1);
        hl_pop(H);
        break;
      }
      case hl_OP_RETURN: {
        hl_Value result = hl_pop(H);
        closeUpvalues(H, frame->slots);
        H->frameCount--;
        if (H->frameCount == 0) {
          hl_pop(H);
          return hl_RES_INTERPRET_OK;
        }

        H->stackTop = frame->slots;
        hl_push(H, result);
        frame = &H->frames[H->frameCount - 1];
        break;
      }
      case hl_OP_ENUM: {
        hl_push(H, hl_NEW_OBJ(hl_newEnum(H, READ_STRING())));
        break;
      }
      case hl_OP_ENUM_VALUE: {
        struct hl_Enum* enoom = hl_AS_ENUM(peek(H, 0));
        struct hl_String* name = READ_STRING();
        f64 value = (f64)READ_BYTE();
        hl_tableSet(H, &enoom->values, name, hl_NEW_NUMBER(value));
        break;
      }
      case hl_OP_STRUCT: {
        hl_push(H, hl_NEW_OBJ(hl_newStruct(H, READ_STRING())));
        break;
      }
      case hl_OP_METHOD: {
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(H, 1));
        defineMethod(H, READ_STRING(), &strooct->methods);
        break;
      }
      case hl_OP_STATIC_METHOD: {
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(H, 1));
        defineMethod(H, READ_STRING(), &strooct->staticMethods);
        break;
      }
      case hl_OP_INVOKE: {
        struct hl_String* method = READ_STRING();
        s32 argCount = READ_BYTE();
        if (!invoke(H, method, argCount)) {
          return hl_RES_RUNTIME_ERR;
        }
        frame = &H->frames[H->frameCount - 1];
        break;
      }
      case hl_OP_STRUCT_FIELD: {
        struct hl_String* key = READ_STRING();
        hl_Value defaultValue = hl_pop(H);
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(H, 0));
        hl_tableSet(H, &strooct->defaultFields, key, defaultValue);
        break;
      }
      // This opcode is only a placeholder for a jump instruction
      case hl_OP_BREAK: {
        runtimeError(H, "Invalid Opcode");
        return hl_RES_RUNTIME_ERR;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

enum hl_InterpretResult hl_interpret(struct hl_State* H, const char* source) {
  struct hl_Function* function = hl_compile(H, H->parser, source);
  if (function == NULL) {
    return hl_RES_COMPILE_ERR;
  }

  hl_push(H, hl_NEW_OBJ(function));
  struct hl_Closure* closure = hl_newClosure(H, function);
  hl_pop(H);
  hl_push(H, hl_NEW_OBJ(closure));
  call(H, closure, 0);

  return run(H);
}


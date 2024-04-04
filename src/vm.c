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

static void resetStack(struct State* H) {
  H->stackTop = H->stack;
  H->frameCount = 0;
  H->openUpvalues = NULL;
}

static void runtimeError(struct State* H, const char* format, ...) {
  for (s32 i = 0; i < H->frameCount; i++) {
    struct CallFrame* frame = &H->frames[i];
    struct Function* function = frame->closure->function;
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

void push(struct State* H, Value value) {
  *H->stackTop = value;
  H->stackTop++;
}

Value pop(struct State* H) {
  H->stackTop--;
  return *H->stackTop;
}

static Value peek(struct State* H, s32 distance) {
  return H->stackTop[-1 - distance];
}

void bindCFunction(struct State* H, const char* name, CFunction cFunction) {
  push(H, NEW_OBJ(copyString(H, name, (s32)strlen(name))));
  push(H, NEW_OBJ(newCFunctionBinding(H, cFunction)));
  tableSet(H, &H->globals, AS_STRING(H->stack[0]), H->stack[1]);
  pop(H);
  pop(H);
}

static Value wrap_print(struct State* H) {
  printValue(peek(H, 0));
  printf("\n");
  return NEW_NIL;
}

static Value wrap_clock(UNUSED struct State* H) {
  return NEW_NUMBER((f64)clock() / CLOCKS_PER_SEC);
}

static Value wrap_explode(UNUSED struct State* H) {
  // explodes the interpreter.
  // Returns true on success :^)
  *((int*)(size_t)rand()) = 0;
  return NEW_BOOL(true);
}

void initState(struct State* H) {
  H->objects = NULL;
  H->parser = NULL;

  H->bytesAllocated = 0;
  H->nextGc = 1024 * 1024;

  H->grayCount = 0;
  H->grayCapacity = 0;
  H->grayStack = NULL;

  resetStack(H);

  initTable(&H->strings);
  initTable(&H->globals);

  bindCFunction(H, "clock", wrap_clock);
  bindCFunction(H, "explode", wrap_explode);
  bindCFunction(H, "print", wrap_print);

  H->parser = ALLOCATE(H, struct Parser, 1);
}

void freeState(struct State* H) {
  freeTable(H, &H->strings);
  freeTable(H, &H->globals);
  freeObjects(H);
  FREE(H, struct Parser, H->parser);
}

static bool call(struct State* H, struct Closure* closure, s32 argCount) {
  if (argCount != closure->function->arity) {
    runtimeError(H, "Expected %d arguments, but got %d.", closure->function->arity, argCount);
    return false;
  }

  if (H->frameCount == FRAMES_MAX) {
    runtimeError(H, "Stack overflow.");
    return false;
  }

  struct CallFrame* frame = &H->frames[H->frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->bc;
  frame->slots = H->stackTop - argCount - 1;
  return true;
}

static bool callValue(struct State* H, Value callee, s32 argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_BOUND_METHOD: {
        struct BoundMethod* bound = AS_BOUND_METHOD(callee);
        H->stackTop[-argCount - 1] = bound->receiver;
        return call(H, bound->method, argCount);
      }
      case OBJ_CLOSURE:
        return call(H, AS_CLOSURE(callee), argCount);
      case OBJ_CFUNCTION: {
        CFunction cFunction = AS_CFUNCTION(callee);
        Value result = cFunction(H);
        H->stackTop -= argCount + 1;
        push(H, result);
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
    struct State* H,
    struct Struct* strooct, struct String* name, s32 argCount) {
  Value method;
  if (!tableGet(&strooct->methods, name, &method)) {
    runtimeError(H, "Undefined property '%d'.", name->chars);
    return false;
  }

  return call(H, AS_CLOSURE(method), argCount);
}

static bool invoke(struct State* H, struct String* name, s32 argCount) {
  Value receiver = peek(H, argCount);
  if (!IS_INSTANCE(receiver)) {
    runtimeError(H, "Only instances have methods.");
    return false;
  }

  struct Instance* instance = AS_INSTANCE(receiver);

  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    H->stackTop[-argCount - 1] = value;
    return callValue(H, value, argCount);
  }

  return invokeFromStruct(H, instance->strooct, name, argCount);
}

static bool bindMethod(struct State* H, struct Struct* strooct, struct String* name) {
  Value method;
  if (!tableGet(&strooct->methods, name, &method)) {
    runtimeError(H, "Undefined property '%s'.", name->chars);
    return false;
  }

  struct BoundMethod* bound = newBoundMethod(H, peek(H, 0), AS_CLOSURE(method));
  pop(H);
  push(H, NEW_OBJ(bound));
  return true;
}

static struct Upvalue* captureUpvalue(struct State* H, Value* local) {
  struct Upvalue* previous = NULL;
  struct Upvalue* current = H->openUpvalues;
  while (current != NULL && current->location > local) {
    previous = current;
    current = current->next;
  }

  if (current != NULL && current->location == local) {
    return current;
  }

  struct Upvalue* createdUpvalue = newUpvalue(H, local);

  createdUpvalue->next = current;
  if (previous == NULL) {
    H->openUpvalues = createdUpvalue;
  } else {
    previous->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(struct State* H, Value* last) {
  while (H->openUpvalues != NULL && H->openUpvalues->location >= last) {
    struct Upvalue* upvalue = H->openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    H->openUpvalues = upvalue->next;
  }
}

static void defineMethod(struct State* H, struct String* name, struct Table* table) {
  Value method = peek(H, 0);
  tableSet(H, table, name, method);
  pop(H);
}

static bool setProperty(struct State* H, struct String* name) {
  if (!IS_INSTANCE(peek(H, 1))) {
    runtimeError(H, "Can only use dot operator on instances.");
    return false;
  }

  struct Instance* instance = AS_INSTANCE(peek(H, 1));
  if (tableSet(H, &instance->fields, name, peek(H, 0))) {
    runtimeError(H, "Cannot create new properties on instances at runtime.");
    return false;
  }

  return true;
}

static bool getProperty(struct State* H, Value object, struct String* name, bool popValue) {
  if (IS_OBJ(object)) {
    switch (OBJ_TYPE(object)) {
      case OBJ_INSTANCE: {
        struct Instance* instance = AS_INSTANCE(object);

        Value value;
        if (tableGet(&instance->fields, name, &value)) {
          if (popValue) {
            pop(H); // Instance
          }
          push(H, value);
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

static bool getStatic(struct State* H, Value object, struct String* name) {
  if (IS_OBJ(object)) {
    switch (OBJ_TYPE(object)) {
      case OBJ_STRUCT: {
        struct Struct* strooct = AS_STRUCT(object);

        Value value;
        if (tableGet(&strooct->staticMethods, name, &value)) {
          pop(H); // struct
          push(H, value);
          return true;
        }

        runtimeError(H, "Static method '%s' does not exist.", name->chars);
        return false;
      }
      case OBJ_ENUM: {
        struct Enum* enoom = AS_ENUM(object);

        Value value;
        if (tableGet(&enoom->values, name, &value)) {
          pop(H); // enum
          push(H, value);
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

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(struct State* H) {
  struct String* b = AS_STRING(peek(H, 0));
  struct String* a = AS_STRING(peek(H, 1));

  s32 length = a->length + b->length;
  char* chars = ALLOCATE(H, char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  struct String* result = takeString(H, chars, length);

  pop(H);
  pop(H);
  push(H, NEW_OBJ(result));
}

static enum InterpretResult run(struct State* H) {
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (u16)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(outType, op) \
    do { \
      if (!IS_NUMBER(peek(H, 0)) || !IS_NUMBER(peek(H, 1))) { \
        runtimeError(H, "Operands must be numbers."); \
        return RUNTIME_ERR; \
      } \
      f32 b = AS_NUMBER(pop(H)); \
      f32 a = AS_NUMBER(pop(H)); \
      push(H, outType(a op b)); \
    } while (false)

  struct CallFrame* frame = &H->frames[H->frameCount - 1];

  while (true) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        | ");
    for (Value* slot = H->stack; slot < H->stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(
        frame->closure->function, (s32)(frame->ip - frame->closure->function->bc));
#endif
    u8 instruction;
    switch (instruction = READ_BYTE()) {
      case BC_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(H, constant);
        break;
      }
      case BC_NIL:   push(H, NEW_NIL); break;
      case BC_TRUE:  push(H, NEW_BOOL(true)); break;
      case BC_FALSE: push(H, NEW_BOOL(false)); break;
      case BC_POP: pop(H); break;
      case BC_ARRAY: {
        u8 elementCount = READ_BYTE();
        struct Array* array = newArray(H);
        push(H, NEW_OBJ(array));
        reserveValueArray(H, &array->values, elementCount);
        for (u8 i = 1; i <= elementCount; i++) {
          writeValueArray(H, &array->values, peek(H, elementCount - i + 1));
        }
        H->stackTop -= elementCount + 1;
        push(H, NEW_OBJ(array));
        break;
      }
      case BC_GET_SUBSCRIPT: {
        if (!IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Can only use subscript operator with numbers.");
          return RUNTIME_ERR;
        }
        s32 index = AS_NUMBER(peek(H, 0));

        if (!IS_ARRAY(peek(H, 1))) {
          runtimeError(H, "Invalid target for subscript operator.");
          return RUNTIME_ERR;
        }

        struct Array* array = AS_ARRAY(peek(H, 1));

        if (index < 0 || index > array->values.count) {
          runtimeError(H, "Index out of bounds. Array size is %d, but tried accessing %d",
              array->values.count, index);
          return RUNTIME_ERR;
        }

        pop(H); // Index
        pop(H); // Array
        push(H, array->values.values[index]);
        break;
      }
      case BC_SET_SUBSCRIPT: {
        if (!IS_NUMBER(peek(H, 1))) {
          runtimeError(H, "Can only use subscript operator with numbers.");
          return RUNTIME_ERR;
        }
        s32 index = AS_NUMBER(peek(H, 1));

        if (!IS_ARRAY(peek(H, 2))) {
          runtimeError(H, "Invalid target for subscript operator.");
          return RUNTIME_ERR;
        }

        struct Array* array = AS_ARRAY(peek(H, 2));

        if (index < 0 || index > array->values.count) {
          runtimeError(H, "Index out of bounds. Array size is %d, but tried accessing %d",
              array->values.count, index);
          return RUNTIME_ERR;
        }

        array->values.values[index] = pop(H);
        pop(H); // Index
        pop(H); // Array
        push(H, array->values.values[index]);
        break;
      }
      case BC_GET_GLOBAL: {
        struct String* name = READ_STRING();
        Value value;
        if (!tableGet(&H->globals, name, &value)) {
          runtimeError(H, "Undefined variable '%s'.", name->chars);
          return RUNTIME_ERR;
        }
        push(H, value);
        break;
      }
      case BC_SET_GLOBAL: {
        struct String* name = READ_STRING();
        if (tableSet(H, &H->globals, name, peek(H, 0))) {
          tableDelete(&H->globals, name);
          runtimeError(H, "Undefined variable '%s'.", name->chars);
          return RUNTIME_ERR;
        }
        break;
      }
      case BC_DEFINE_GLOBAL: {
        struct String* name = READ_STRING();
        if (!tableSet(H, &H->globals, name, peek(H, 0))) {
          tableDelete(&H->globals, name);
          runtimeError(H, "Redefinition of '%s'.", name->chars);
          return RUNTIME_ERR;
        }
        pop(H);
        break;
      }
      case BC_GET_UPVALUE: {
        u8 slot = READ_BYTE();
        push(H, *frame->closure->upvalues[slot]->location);
        break;
      }
      case BC_SET_UPVALUE: {
        u8 slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(H, 0);
        break;
      }
      case BC_GET_LOCAL: {
        u8 slot = READ_BYTE();
        push(H, frame->slots[slot]);
        break;
      }
      case BC_SET_LOCAL: {
        u8 slot = READ_BYTE();
        frame->slots[slot] = peek(H, 0);
        break;
      }
      case BC_INIT_PROPERTY: {
        if (!setProperty(H, READ_STRING())) {
          return RUNTIME_ERR;
        }

        pop(H); // Value
        break;
      }
      case BC_GET_STATIC: {
        if (!getStatic(H, peek(H, 0), READ_STRING())) {
          return RUNTIME_ERR;
        }
        break;
      }
      case BC_PUSH_PROPERTY:
      case BC_GET_PROPERTY: {
        if (!getProperty(
            H, peek(H, 0), READ_STRING(), instruction == BC_GET_PROPERTY)) {
          return RUNTIME_ERR;
        }
        break;
      }
      case BC_SET_PROPERTY: {
        if (!setProperty(H, READ_STRING())) {
          return RUNTIME_ERR;
        }

        // Removing the instance while keeping the rhs value on top.
        Value value = pop(H);
        pop(H);
        push(H, value);
        break;
      }
      case BC_DESTRUCT_ARRAY: {
        u8 index = READ_BYTE();

        if (!IS_ARRAY(peek(H, 0))) {
          runtimeError(H, "Can only destruct arrays");
          return RUNTIME_ERR;
        }
        struct Array* array = AS_ARRAY(peek(H, 0));

        push(H, array->values.values[index]);
        break;
      }
      case BC_EQUAL: {
        Value b = pop(H);
        Value a = pop(H);
        push(H, NEW_BOOL(valuesEqual(a, b)));
        break;
      }
      case BC_NOT_EQUAL: {
        Value b = pop(H);
        Value a = pop(H);
        push(H, NEW_BOOL(!valuesEqual(a, b)));
        break;
      }
      case BC_CONCAT: {
        if (!IS_STRING(peek(H, 0)) || !IS_STRING(peek(H, 1))) {
          runtimeError(H, "Operands must be strings.");
          return RUNTIME_ERR;
        }
        concatenate(H);
        break;
      }
      case BC_GREATER:       BINARY_OP(NEW_BOOL, >); break;
      case BC_GREATER_EQUAL: BINARY_OP(NEW_BOOL, >=); break;
      case BC_LESSER:        BINARY_OP(NEW_BOOL, <); break;
      case BC_LESSER_EQUAL:  BINARY_OP(NEW_BOOL, <=); break;
      case BC_ADD:           BINARY_OP(NEW_NUMBER, +); break;
      case BC_SUBTRACT:      BINARY_OP(NEW_NUMBER, -); break;
      case BC_MULTIPLY:      BINARY_OP(NEW_NUMBER, *); break;
      case BC_DIVIDE:        BINARY_OP(NEW_NUMBER, /); break;
      case BC_MODULO: {
        if (!IS_NUMBER(peek(H, 0)) || !IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Operands must be numbers.");
          return RUNTIME_ERR;
        }
        f64 b = AS_NUMBER(pop(H));
        f64 a = AS_NUMBER(pop(H));
        push(H, NEW_NUMBER(fmod(a, b)));
        break;
      }
      case BC_POW: {
        if (!IS_NUMBER(peek(H, 0)) || !IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Operands must be numbers.");
          return RUNTIME_ERR;
        }
        f64 b = AS_NUMBER(pop(H));
        f64 a = AS_NUMBER(pop(H));
        push(H, NEW_NUMBER(pow(a, b)));
        break;
      }
      case BC_NEGATE: {
        if (!IS_NUMBER(peek(H, 0))) {
          runtimeError(H, "Operand must be a number.");
          return RUNTIME_ERR;
        }
        push(H, NEW_NUMBER(-AS_NUMBER(pop(H))));
        break;
      }
      case BC_NOT: {
        push(H, NEW_BOOL(isFalsey(pop(H))));
        break;
      }
      case BC_JUMP: {
        u16 offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case BC_JUMP_IF_FALSE: {
        u16 offset = READ_SHORT();
        if (isFalsey(peek(H, 0))) {
          frame->ip += offset;
        }
        break;
      }
      case BC_INEQUALITY_JUMP: {
        u16 offset = READ_SHORT();
        Value b = pop(H);
        Value a = peek(H, 0);
        if (!valuesEqual(a, b)) {
          frame->ip += offset;
        }
        break;
      }
      case BC_LOOP: {
        u16 offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case BC_CALL: {
        s32 argCount = READ_BYTE();
        if (!callValue(H, peek(H, argCount), argCount)) {
          return RUNTIME_ERR;
        }
        frame = &H->frames[H->frameCount - 1];
        break;
      }
      case BC_INSTANCE: {
        if (!IS_STRUCT(peek(H, 0))) {
          runtimeError(H, "Can only use struct initialization on structs.");
          return RUNTIME_ERR;
        }
        struct Struct* strooct = AS_STRUCT(peek(H, 0));
        Value instance = NEW_OBJ(newInstance(H, strooct));
        pop(H); // Struct
        push(H, instance);
        break;
      }
      case BC_CLOSURE: {
        struct Function* function = AS_FUNCTION(READ_CONSTANT());
        struct Closure* closure = newClosure(H, function);
        push(H, NEW_OBJ(closure));
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
      case BC_CLOSE_UPVALUE: {
        closeUpvalues(H, H->stackTop - 1);
        pop(H);
        break;
      }
      case BC_RETURN: {
        Value result = pop(H);
        closeUpvalues(H, frame->slots);
        H->frameCount--;
        if (H->frameCount == 0) {
          pop(H);
          return INTERPRET_OK;
        }

        H->stackTop = frame->slots;
        push(H, result);
        frame = &H->frames[H->frameCount - 1];
        break;
      }
      case BC_ENUM: {
        push(H, NEW_OBJ(newEnum(H, READ_STRING())));
        break;
      }
      case BC_ENUM_VALUE: {
        struct Enum* enoom = AS_ENUM(peek(H, 0));
        struct String* name = READ_STRING();
        f64 value = (f64)READ_BYTE();
        tableSet(H, &enoom->values, name, NEW_NUMBER(value));
        break;
      }
      case BC_STRUCT: {
        push(H, NEW_OBJ(newStruct(H, READ_STRING())));
        break;
      }
      case BC_METHOD: {
        struct Struct* strooct = AS_STRUCT(peek(H, 1));
        defineMethod(H, READ_STRING(), &strooct->methods);
        break;
      }
      case BC_STATIC_METHOD: {
        struct Struct* strooct = AS_STRUCT(peek(H, 1));
        defineMethod(H, READ_STRING(), &strooct->staticMethods);
        break;
      }
      case BC_INVOKE: {
        struct String* method = READ_STRING();
        s32 argCount = READ_BYTE();
        if (!invoke(H, method, argCount)) {
          return RUNTIME_ERR;
        }
        frame = &H->frames[H->frameCount - 1];
        break;
      }
      case BC_STRUCT_FIELD: {
        struct String* key = READ_STRING();
        Value defaultValue = pop(H);
        struct Struct* strooct = AS_STRUCT(peek(H, 0));
        tableSet(H, &strooct->defaultFields, key, defaultValue);
        break;
      }
      // This opcode is only a placeholder for a jump instruction
      case BC_BREAK: {
        runtimeError(H, "Invalid Opcode");
        return RUNTIME_ERR;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

enum InterpretResult interpret(struct State* H, const char* source) {
  struct Function* function = compile(H, H->parser, source);
  if (function == NULL) {
    return COMPILE_ERR;
  }

  push(H, NEW_OBJ(function));
  struct Closure* closure = newClosure(H, function);
  pop(H);
  push(H, NEW_OBJ(closure));
  call(H, closure, 0);

  return run(H);
}


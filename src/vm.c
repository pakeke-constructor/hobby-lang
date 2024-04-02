#include "vm.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#include "debug.h"

struct hl_Vm vm;

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
  for (s32 i = 0; i < vm.frameCount; i++) {
    struct hl_CallFrame* frame = &vm.frames[i];
    struct hl_Function* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line #%d] in ", function->chunk.lines[instruction]);
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

  resetStack();
}

void hl_bindCFunction(const char* name, hl_CFunction cFunction) {
  hl_push(hl_NEW_OBJ(hl_copyString(name, (s32)strlen(name))));
  hl_push(hl_NEW_OBJ(hl_newCFunctionBinding(cFunction)));
  hl_tableSet(&vm.globals, hl_AS_STRING(vm.stack[0]), vm.stack[1]);
  hl_pop();
  hl_pop();
}

static hl_Value wrap_error(s32 argCount, hl_Value* args, bool* failed) {
  *failed = true;

  if (argCount != 1) {
    runtimeError("Expected 1 argument, got %d.", argCount);
    return hl_NEW_NIL;
  }

  runtimeError("User error: %s", hl_AS_CSTRING(hl_valueToChars(args[0])));
  return hl_NEW_NIL;
}

static hl_Value wrap_print(s32 argCount, hl_Value* args, hl_UNUSED bool* failed) {
  for (s32 i = 0; i < argCount; i++) {
    hl_printValue(args[i]);
    if (i != argCount - 1) {
      printf("\t");
    }
  }
  printf("\n");
  
  return hl_NEW_NIL;
}

static hl_Value wrap_clock(
      hl_UNUSED s32 argCount, hl_UNUSED hl_Value* args, hl_UNUSED bool* failed) {
  return hl_NEW_NUMBER((f64)clock() / CLOCKS_PER_SEC);
}

static hl_Value wrap_toString(s32 argCount, hl_Value* args, bool* failed) {
  if (argCount != 1) {
    runtimeError("Expected 1 argument, got %d", argCount);
    *failed = true;
    return hl_NEW_NIL;
  }
  return hl_valueToChars(args[0]);
}

void hl_initVm() {
  resetStack();
  vm.objects = NULL;

  vm.bytesAllocated = 0;
  vm.nextGc = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  hl_initTable(&vm.strings);
  hl_initTable(&vm.globals);

  hl_bindCFunction("clock", wrap_clock);
  hl_bindCFunction("print", wrap_print);
  hl_bindCFunction("error", wrap_error);
  hl_bindCFunction("toString", wrap_toString);
}

void hl_freeVm() {
  hl_freeTable(&vm.strings);
  hl_freeTable(&vm.globals);
  hl_freeObjects();
}

void hl_push(hl_Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

hl_Value hl_pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static hl_Value peek(s32 distance) {
  return vm.stackTop[-1 - distance];
}

static bool call(struct hl_Closure* closure, s32 argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments, but got %d.", closure->function->arity, argCount);
    return false;
  }

  if (vm.frameCount == hl_FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  struct hl_CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(hl_Value callee, s32 argCount) {
  if (hl_IS_OBJ(callee)) {
    switch (hl_OBJ_TYPE(callee)) {
      case hl_OBJ_BOUND_METHOD: {
        struct hl_BoundMethod* bound = hl_AS_BOUND_METHOD(callee);
        vm.stackTop[-argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
      }
      case hl_OBJ_CLOSURE:
        return call(hl_AS_CLOSURE(callee), argCount);
      case hl_OBJ_CFUNCTION: {
        hl_CFunction cFunction = hl_AS_CFUNCTION(callee);
        bool failed = false;
        hl_Value result = cFunction(argCount, vm.stackTop - argCount, &failed);
        vm.stackTop -= argCount + 1;
        hl_push(result);
        return !failed;
      }
      default:
        break;
    }
  }

  runtimeError("Can only call functions.");
  return false;
}

static bool invokeFromStruct(
    struct hl_Struct* strooct, struct hl_String* name, s32 argCount) {
  hl_Value method;
  if (!hl_tableGet(&strooct->methods, name, &method)) {
    runtimeError("Undefined property '%d'.", name->chars);
    return false;
  }

  return call(hl_AS_CLOSURE(method), argCount);
}

static bool invoke(struct hl_String* name, s32 argCount) {
  hl_Value receiver = peek(argCount);
  if (!hl_IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  struct hl_Instance* instance = hl_AS_INSTANCE(receiver);

  hl_Value value;
  if (hl_tableGet(&instance->fields, name, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  return invokeFromStruct(instance->strooct, name, argCount);
}

static bool bindMethod(struct hl_Struct* strooct, struct hl_String* name) {
  hl_Value method;
  if (!hl_tableGet(&strooct->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  struct hl_BoundMethod* bound = hl_newBoundMethod(peek(0), hl_AS_CLOSURE(method));
  hl_pop();
  hl_push(hl_NEW_OBJ(bound));
  return true;
}

static struct hl_Upvalue* captureUpvalue(hl_Value* local) {
  struct hl_Upvalue* previous = NULL;
  struct hl_Upvalue* current = vm.openUpvalues;
  while (current != NULL && current->location > local) {
    previous = current;
    current = current->next;
  }

  if (current != NULL && current->location == local) {
    return current;
  }

  struct hl_Upvalue* newUpvalue = hl_newUpvalue(local);

  newUpvalue->next = current;
  if (previous == NULL) {
    vm.openUpvalues = newUpvalue;
  } else {
    previous->next = newUpvalue;
  }

  return newUpvalue;
}

static void closeUpvalues(hl_Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    struct hl_Upvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static void defineMethod(struct hl_String* name, struct hl_Table* table) {
  hl_Value method = peek(0);
  hl_tableSet(table, name, method);
  hl_pop();
}

static bool setProperty(struct hl_String* name) {
  if (!hl_IS_INSTANCE(peek(1))) {
    runtimeError("Can only use dot operator on instances.");
    return false;
  }

  struct hl_Instance* instance = hl_AS_INSTANCE(peek(1));
  if (hl_tableSet(&instance->fields, name, peek(0))) {
    runtimeError("Cannot create new properties on instances at runtime.");
    return false;
  }

  return true;
}

static bool getProperty(hl_Value object, struct hl_String* name, bool popValue) {
  if (hl_IS_OBJ(object)) {
    switch (hl_OBJ_TYPE(object)) {
      case hl_OBJ_INSTANCE: {
        struct hl_Instance* instance = hl_AS_INSTANCE(object);

        hl_Value value;
        if (hl_tableGet(&instance->fields, name, &value)) {
          if (popValue) {
            hl_pop(); // Instance
          }
          hl_push(value);
          return true;
        }

        if (!bindMethod(instance->strooct, name)) {
          return false;
        }
        return true;
      }
      default:
        break;
    }
  }

  runtimeError("Invalid target for the dot operator.");
  return false;
}

static bool getStatic(hl_Value object, struct hl_String* name) {
  if (hl_IS_OBJ(object)) {
    switch (hl_OBJ_TYPE(object)) {
      case hl_OBJ_STRUCT: {
        struct hl_Struct* strooct = hl_AS_STRUCT(object);

        hl_Value value;
        if (hl_tableGet(&strooct->staticMethods, name, &value)) {
          hl_pop(); // struct
          hl_push(value);
          return true;
        }

        runtimeError("Static method '%s' does not exist.", name->chars);
        return false;
      }
      case hl_OBJ_ENUM: {
        struct hl_Enum* enoom = hl_AS_ENUM(object);

        hl_Value value;
        if (hl_tableGet(&enoom->values, name, &value)) {
          hl_pop(); // enum
          hl_push(value);
          return true;
        }

        runtimeError("Enum value '%s' does not exist.", name->chars);
        return false;
      }
      default:
        break;
    }
  }

  runtimeError("Invalid target for the static operator.");
  return false;
}

static bool isFalsey(hl_Value value) {
  return hl_IS_NIL(value) || (hl_IS_BOOL(value) && !hl_AS_BOOL(value));
}

static void concatenate() {
  struct hl_String* b = hl_AS_STRING(peek(0));
  struct hl_String* a = hl_AS_STRING(peek(1));

  s32 length = a->length + b->length;
  char* chars = hl_ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  struct hl_String* result = hl_takeString(chars, length);

  hl_pop();
  hl_pop();
  hl_push(hl_NEW_OBJ(result));
}

static enum hl_InterpretResult run() {
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (u16)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() hl_AS_STRING(READ_CONSTANT())
#define BINARY_OP(outType, op) \
    do { \
      if (!hl_IS_NUMBER(peek(0)) || !hl_IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return hl_RES_RUNTIME_ERR; \
      } \
      f32 b = hl_AS_NUMBER(hl_pop()); \
      f32 a = hl_AS_NUMBER(hl_pop()); \
      hl_push(outType(a op b)); \
    } while (false)

  struct hl_CallFrame* frame = &vm.frames[vm.frameCount - 1];

  while (true) {
#ifdef hl_DEBUG_TRACE_EXECUTION
    printf("        | ");
    for (hl_Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      hl_printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    hl_disassembleInstruction(
        &frame->closure->function->chunk, (s32)(frame->ip - frame->closure->function->chunk.code));
#endif
    u8 instruction;
    switch (instruction = READ_BYTE()) {
      case hl_OP_CONSTANT: {
        hl_Value constant = READ_CONSTANT();
        hl_push(constant);
        break;
      }
      case hl_OP_NIL:   hl_push(hl_NEW_NIL); break;
      case hl_OP_TRUE:  hl_push(hl_NEW_BOOL(true)); break;
      case hl_OP_FALSE: hl_push(hl_NEW_BOOL(false)); break;
      case hl_OP_POP: hl_pop(); break;
      case hl_OP_ARRAY: {
        struct hl_Array* array = hl_newArray();
        u8 elementCount = READ_BYTE();
        for (u8 i = 1; i <= elementCount; i++) {
          hl_writeValueArray(&array->values, peek(elementCount - i));
        }
        for (u8 i = 0; i < elementCount; i++) {
          hl_pop();
        }
        hl_push(hl_NEW_OBJ(array));
        break;
      }
      case hl_OP_GET_SUBSCRIPT: {
        if (!hl_IS_NUMBER(peek(0))) {
          runtimeError("Can only use subscript operator with numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        s32 index = hl_AS_NUMBER(peek(0));

        if (!hl_IS_ARRAY(peek(1))) {
          runtimeError("Invalid target for subscript operator.");
          return hl_RES_RUNTIME_ERR;
        }

        struct hl_Array* array = hl_AS_ARRAY(peek(1));

        if (index < 0 || index > array->values.count) {
          runtimeError("Index out of bounds. Array size is %d, but tried accessing %d",
              array->values.count, index);
          return hl_RES_RUNTIME_ERR;
        }

        hl_pop(); // Index
        hl_pop(); // Array
        hl_push(array->values.values[index]);
        break;
      }
      case hl_OP_SET_SUBSCRIPT: {
        runtimeError("");
        return hl_RES_RUNTIME_ERR;
      }
      case hl_OP_GET_GLOBAL: {
        struct hl_String* name = READ_STRING();
        hl_Value value;
        if (!hl_tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return hl_RES_RUNTIME_ERR;
        }
        hl_push(value);
        break;
      }
      case hl_OP_SET_GLOBAL: {
        struct hl_String* name = READ_STRING();
        if (hl_tableSet(&vm.globals, name, peek(0))) {
          hl_tableDelete(&vm.globals, name);
          runtimeError("Undefined variable '%s'.", name->chars);
          return hl_RES_RUNTIME_ERR;
        }
        break;
      }
      case hl_OP_DEFINE_GLOBAL: {
        struct hl_String* name = READ_STRING();
        hl_tableSet(&vm.globals, name, peek(0));
        hl_pop();
        break;
      }
      case hl_OP_GET_UPVALUE: {
        u8 slot = READ_BYTE();
        hl_push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case hl_OP_SET_UPVALUE: {
        u8 slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case hl_OP_GET_LOCAL: {
        u8 slot = READ_BYTE();
        hl_push(frame->slots[slot]);
        break;
      }
      case hl_OP_SET_LOCAL: {
        u8 slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case hl_OP_INIT_PROPERTY: {
        if (!setProperty(READ_STRING())) {
          return hl_RES_RUNTIME_ERR;
        }

        hl_pop(); // Value
        break;
      }
      case hl_OP_GET_STATIC: {
        if (!getStatic(peek(0), READ_STRING())) {
          return hl_RES_RUNTIME_ERR;
        }
        break;
      }
      case hl_OP_PUSH_PROPERTY:
      case hl_OP_GET_PROPERTY: {
        if (!getProperty(
            peek(0), READ_STRING(), instruction == hl_OP_GET_PROPERTY)) {
          return hl_RES_RUNTIME_ERR;
        }
        break;
      }
      case hl_OP_SET_PROPERTY: {
        if (!setProperty(READ_STRING())) {
          return hl_RES_RUNTIME_ERR;
        }

        // Removing the instance while keeping the rhs value on top.
        hl_Value value = hl_pop();
        hl_pop();
        hl_push(value);
        break;
      }
      case hl_OP_EQUAL: {
        hl_Value b = hl_pop();
        hl_Value a = hl_pop();
        hl_push(hl_NEW_BOOL(hl_valuesEqual(a, b)));
        break;
      }
      case hl_OP_NOT_EQUAL: {
        hl_Value b = hl_pop();
        hl_Value a = hl_pop();
        hl_push(hl_NEW_BOOL(!hl_valuesEqual(a, b)));
        break;
      }
      case hl_OP_CONCAT: {
        if (!hl_IS_STRING(peek(0)) || !hl_IS_STRING(peek(1))) {
          runtimeError("Operands must be strings.");
          return hl_RES_RUNTIME_ERR;
        }
        concatenate();
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
        if (!hl_IS_NUMBER(peek(0)) || !hl_IS_NUMBER(peek(0))) {
          runtimeError("Operands must be numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        f64 b = hl_AS_NUMBER(hl_pop());
        f64 a = hl_AS_NUMBER(hl_pop());
        hl_push(hl_NEW_NUMBER(fmod(a, b)));
        break;
      }
      case hl_OP_POW: {
        if (!hl_IS_NUMBER(peek(0)) || !hl_IS_NUMBER(peek(0))) {
          runtimeError("Operands must be numbers.");
          return hl_RES_RUNTIME_ERR;
        }
        f64 b = hl_AS_NUMBER(hl_pop());
        f64 a = hl_AS_NUMBER(hl_pop());
        hl_push(hl_NEW_NUMBER(pow(a, b)));
        break;
      }
      case hl_OP_NEGATE: {
        if (!hl_IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return hl_RES_RUNTIME_ERR;
        }
        hl_push(hl_NEW_NUMBER(-hl_AS_NUMBER(hl_pop())));
        break;
      }
      case hl_OP_NOT: {
        hl_push(hl_NEW_BOOL(isFalsey(hl_pop())));
        break;
      }
      case hl_OP_JUMP: {
        u16 offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case hl_OP_JUMP_IF_FALSE: {
        u16 offset = READ_SHORT();
        if (isFalsey(peek(0))) {
          frame->ip += offset;
        }
        break;
      }
      case hl_OP_INEQUALITY_JUMP: {
        u16 offset = READ_SHORT();
        hl_Value b = hl_pop();
        hl_Value a = peek(0);
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
        if (!callValue(peek(argCount), argCount)) {
          return hl_RES_RUNTIME_ERR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case hl_OP_INSTANCE: {
        if (!hl_IS_STRUCT(peek(0))) {
          runtimeError("Can only use struct initialization on structs.");
          return hl_RES_RUNTIME_ERR;
        }
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(0));
        hl_Value instance = hl_NEW_OBJ(hl_newInstance(strooct));
        hl_pop(); // Struct
        hl_push(instance);
        break;
      }
      case hl_OP_CLOSURE: {
        struct hl_Function* function = hl_AS_FUNCTION(READ_CONSTANT());
        struct hl_Closure* closure = hl_newClosure(function);
        hl_push(hl_NEW_OBJ(closure));
        for (s32 i = 0; i < closure->upvalueCount; i++) {
          u8 isLocal = READ_BYTE();
          u8 index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case hl_OP_CLOSE_UPVALUE: {
        closeUpvalues(vm.stackTop - 1);
        hl_pop();
        break;
      }
      case hl_OP_RETURN: {
        hl_Value result = hl_pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
          hl_pop();
          return hl_RES_INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        hl_push(result);
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case hl_OP_ENUM: {
        hl_push(hl_NEW_OBJ(hl_newEnum(READ_STRING())));
        break;
      }
      case hl_OP_ENUM_VALUE: {
        struct hl_Enum* enoom = hl_AS_ENUM(peek(0));
        struct hl_String* name = READ_STRING();
        f64 value = (f64)READ_BYTE();
        hl_tableSet(&enoom->values, name, hl_NEW_NUMBER(value));
        break;
      }
      case hl_OP_STRUCT: {
        hl_push(hl_NEW_OBJ(hl_newStruct(READ_STRING())));
        break;
      }
      case hl_OP_METHOD: {
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(1));
        defineMethod(READ_STRING(), &strooct->methods);
        break;
      }
      case hl_OP_STATIC_METHOD: {
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(1));
        defineMethod(READ_STRING(), &strooct->staticMethods);
        break;
      }
      case hl_OP_INVOKE: {
        struct hl_String* method = READ_STRING();
        s32 argCount = READ_BYTE();
        if (!invoke(method, argCount)) {
          return hl_RES_RUNTIME_ERR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case hl_OP_STRUCT_FIELD: {
        struct hl_String* key = READ_STRING();
        hl_Value defaultValue = hl_pop();
        struct hl_Struct* strooct = hl_AS_STRUCT(peek(0));
        hl_tableSet(&strooct->defaultFields, key, defaultValue);
        break;
      }
      // This opcode is only a placeholder for a jump instruction
      case hl_OP_BREAK: {
        runtimeError("Invalid Opcode");
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

enum hl_InterpretResult hl_interpret(const char* source) {
  struct hl_Function* function = hl_compile(source);
  if (function == NULL) {
    return hl_RES_COMPILE_ERR;
  }

  hl_push(hl_NEW_OBJ(function));
  struct hl_Closure* closure = hl_newClosure(function);
  hl_pop();
  hl_push(hl_NEW_OBJ(closure));
  call(closure, 0);

  return run();
}


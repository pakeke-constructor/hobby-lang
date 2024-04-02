#include "compiler.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "tokenizer.h"
#include "object.h"
#include "value.h"
#include "memory.h"

#ifdef hl_DEBUG_PRINT_CODE
#include "debug.h"
#endif

struct Parser {
  struct hl_Token current;
  struct hl_Token previous;
  bool hadError;
  bool panicMode;
};

enum Precedence {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_EXPONENT,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY,
};

typedef void (*ParseFn)(bool canAssign);

struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  enum Precedence precedence;
};

struct Loop {
  s32 start;
  s32 bodyStart;
  s32 scopeDepth;
  struct Loop* enclosing;
};

struct Local {
  struct hl_Token name;
  s32 depth;
  bool isCaptured;
};

struct CompilerUpvalue {
  u8 index;
  bool isLocal;
};

enum FunctionType {
  FUNCTION_TYPE_FUNCTION,
  FUNCTION_TYPE_METHOD,
  FUNCTION_TYPE_SCRIPT,
};

struct Compiler {
  struct Compiler* enclosing;

  struct hl_Function* function;
  enum FunctionType type;

  struct Local locals[hl_U8_COUNT];
  struct Loop* loop;
  s32 localCount;
  struct CompilerUpvalue upvalues[hl_U8_COUNT];
  s32 scopeDepth;
};

struct StructField {
  struct hl_Token name;
};

struct StructCompiler {
  struct StructCompiler* enclosing;
};

struct Parser parser;
struct Compiler* currentCompiler = NULL;
struct StructCompiler* currentStruct = NULL;

static struct hl_Chunk* currentChunk() {
  return &currentCompiler->function->chunk;
}

static void errorAt(struct hl_Token* token, const char* message) {
  if (parser.panicMode) {
    return;
  }
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == hl_TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type != hl_TOKEN_ERROR) {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  while (true) {
    parser.current = hl_nextToken();
    if (parser.current.type != hl_TOKEN_ERROR) {
      break;
    }

    errorAtCurrent(parser.current.start);
  }
}

static void consume(enum hl_TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(enum hl_TokenType type) {
  return parser.current.type == type;
}

static bool match(enum hl_TokenType type) {
  if (!check(type)) {
    return false;
  }
  advance();
  return true;
}

static void emitByte(u8 byte) {
  hl_writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(u8 byte1, u8 byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static s32 emitJump(u8 byte) {
  emitByte(byte);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void patchJump(s32 offset) {
  s32 jump = currentChunk()->count - offset - 2;
  if (jump >= UINT16_MAX) {
    error("Too much code to jump over. Why?");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(s32 loopStart) {
  emitByte(hl_OP_LOOP);

  s32 offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) {
    error("Loop is too big. I'm not quite sure why you made a loop this big.");
  }

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static void emitReturn() {
  emitByte(hl_OP_NIL);
  emitByte(hl_OP_RETURN);
}

static u8 makeConstant(hl_Value value) {
  s32 constant = hl_addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in the global scope or functions.");
    return 0;
  }

  return (u8)constant;
}

static void emitConstant(hl_Value value) {
  emitBytes(hl_OP_CONSTANT, makeConstant(value));
}

static void initCompiler(struct Compiler* compiler, enum FunctionType type) {
  compiler->enclosing = currentCompiler;

  compiler->function = NULL;
  compiler->type = type;

  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = hl_newFunction();
  compiler->loop = NULL;
  currentCompiler = compiler;

  if (type != FUNCTION_TYPE_SCRIPT) {
    struct hl_Token name = parser.previous;
    if (name.type == hl_TOKEN_IDENTIFIER) {
      currentCompiler->function->name = hl_copyString(
          name.start, name.length);
    } else if (name.type == hl_TOKEN_FUNC) { // lambda
      currentCompiler->function->name = hl_copyString("@lambda@", 8);
    }
  }

  struct Local* local = &currentCompiler->locals[currentCompiler->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  if (type != FUNCTION_TYPE_FUNCTION) {
    local->name.start = "self";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static struct hl_Function* endCompiler() {
  emitReturn();
  struct hl_Function* function = currentCompiler->function;

#ifdef hl_DEBUG_PRINT_CODE
  if (!parser.hadError) {
    hl_disassembleChunk(
        currentChunk(),
        function, function->name != NULL ? function->name->chars : "<script>");
  }
#endif

  currentCompiler = currentCompiler->enclosing;
  return function;
}

s32 opCodeArgCount(enum hl_OpCode opCode, s32 ip) {
  switch (opCode) {
    case hl_OP_BREAK:
    case hl_OP_NIL:
    case hl_OP_TRUE:
    case hl_OP_FALSE:
    case hl_OP_POP:
    case hl_OP_CLOSE_UPVALUE:
    case hl_OP_EQUAL:
    case hl_OP_NOT_EQUAL:
    case hl_OP_GREATER:
    case hl_OP_GREATER_EQUAL:
    case hl_OP_LESSER:
    case hl_OP_LESSER_EQUAL:
    case hl_OP_CONCAT:
    case hl_OP_ADD:
    case hl_OP_SUBTRACT:
    case hl_OP_MULTIPLY:
    case hl_OP_DIVIDE:
    case hl_OP_MODULO:
    case hl_OP_POW:
    case hl_OP_NEGATE:
    case hl_OP_NOT:
    case hl_OP_RETURN:
    case hl_OP_STRUCT_FIELD:
    case hl_OP_GET_SUBSCRIPT:
    case hl_OP_SET_SUBSCRIPT:
    case hl_OP_INSTANCE:
      return 0;
    case hl_OP_CONSTANT:
    case hl_OP_DEFINE_GLOBAL:
    case hl_OP_GET_GLOBAL:
    case hl_OP_SET_GLOBAL:
    case hl_OP_GET_UPVALUE:
    case hl_OP_SET_UPVALUE:
    case hl_OP_GET_LOCAL:
    case hl_OP_SET_LOCAL:
    case hl_OP_GET_STATIC:
    case hl_OP_GET_PROPERTY:
    case hl_OP_SET_PROPERTY:
    case hl_OP_PUSH_PROPERTY:
    case hl_OP_CALL:
    case hl_OP_STRUCT:
    case hl_OP_INIT_PROPERTY:
    case hl_OP_METHOD:
    case hl_OP_STATIC_METHOD:
    case hl_OP_ARRAY:
    case hl_OP_ENUM:
      return 1;
    case hl_OP_LOOP:
    case hl_OP_JUMP:
    case hl_OP_JUMP_IF_FALSE:
    case hl_OP_INEQUALITY_JUMP:
    case hl_OP_INVOKE:
    case hl_OP_ENUM_VALUE:
      return 2;
    case hl_OP_CLOSURE: {
      u8 index = currentChunk()->code[ip + 1];
      struct hl_Function* function = hl_AS_FUNCTION(
          currentChunk()->constants.values[index]);
      return 1 + function->upvalueCount * 2;
    }
  }
  return -1;
}

static s32 discardLocals() {
  s32 discarded = 0;
  while (currentCompiler->localCount > 0
      && currentCompiler->locals[currentCompiler->localCount - 1].depth
         > currentCompiler->scopeDepth) {
    if (currentCompiler->locals[currentCompiler->localCount - discarded - 1].isCaptured) {
      emitByte(hl_OP_CLOSE_UPVALUE);
    } else {
      emitByte(hl_OP_POP);
    }
    discarded++;
  }

  return discarded;
}

static void beginLoop(struct Loop* loop) {
  loop->start = currentChunk()->count;
  loop->scopeDepth = currentCompiler->scopeDepth;
  loop->enclosing = currentCompiler->loop;
  currentCompiler->loop = loop;
}

static void endLoop(struct Loop* loop) {
  s32 end = currentChunk()->count;

  // Go through the whole body of the loop, find any jumps to the end and patch
  // them in.
  for (s32 instruction = loop->bodyStart; instruction < end;) {
    enum hl_OpCode opCode = currentChunk()->code[instruction];
    if (opCode == hl_OP_BREAK) {
      currentChunk()->code[instruction] = hl_OP_JUMP;
      patchJump(instruction + 1);
      instruction += 3;
    } else {
      instruction += opCodeArgCount(opCode, instruction) + 1;
    }
  }

  currentCompiler->loop = loop->enclosing;
}

static void beginScope() {
  currentCompiler->scopeDepth++;
}

static void endScope() {
  s32 discarded = discardLocals();
  currentCompiler->localCount -= discarded;
  currentCompiler->scopeDepth--;
}

static void expression();
static void statement();
static void declaration();
static struct ParseRule* getRule(enum hl_TokenType type);
static void parsePrecedence(enum Precedence precedence);
static void block();

static void markInitialized() {
  if (currentCompiler->scopeDepth == 0) {
    return;
  }

  currentCompiler->locals[currentCompiler->localCount - 1].depth
      = currentCompiler->scopeDepth;
}

static void defineVariable(u8 global) {
  if (currentCompiler->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(hl_OP_DEFINE_GLOBAL, global);
}

static u8 identifierConstant(struct hl_Token* name) {
  return makeConstant(hl_NEW_OBJ(hl_copyString(name->start, name->length)));
}

static bool identifiersEqual(struct hl_Token* a, struct hl_Token* b) {
  if (a->length != b->length) {
    return false;
  }
  return memcmp(a->start, b->start, a->length) == 0;
}

static s32 resolveLocal(struct Compiler* compiler, struct hl_Token* name) {
  for (s32 i = compiler->localCount - 1; i >= 0; i--) {
    struct Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in it's own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static s32 addUpvalue(struct Compiler* compiler, u8 index, bool isLocal) {
  s32 upvalueCount = compiler->function->upvalueCount;

  for (s32 i = 0; i < upvalueCount; i++) {
    struct CompilerUpvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == hl_U8_COUNT) {
    error("Too many upvalues in a function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static s32 resolveUpvalue(struct Compiler* compiler, struct hl_Token* name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }

  s32 local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (u8)local, true);
  }

  s32 upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (u8)upvalue, false);
  }

  return -1;
}

static void addLocal(struct hl_Token name) {
  if (currentCompiler->localCount == hl_U8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  struct Local* local = &currentCompiler->locals[currentCompiler->localCount++];
  local->name = name;
  local->isCaptured = false;
  local->depth = -1;
  local->depth = currentCompiler->scopeDepth;
}

static void declareVariable() {
  if (currentCompiler->scopeDepth == 0) {
    return;
  }
  
  struct hl_Token* name = &parser.previous;

  for (s32 i = currentCompiler->localCount - 1; i >= 0; i--) {
    struct Local* local = &currentCompiler->locals[i];
    if (local->depth != -1 && local->depth < currentCompiler->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Redefinition of variable.");
    }
  }

  addLocal(*name);
}

static u8 parseVariable(const char* errorMessage) {
  consume(hl_TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (currentCompiler->scopeDepth > 0) {
    return 0;
  }

  return identifierConstant(&parser.previous);
}

static void grouping(hl_UNUSED bool canAssign) {
  expression();
  consume(hl_TOKEN_RPAREN, "Expected ')' after expression.");
}

static void number(hl_UNUSED bool canAssign) {
  f64 value = strtod(parser.previous.start, NULL);
  emitConstant(hl_NEW_NUMBER(value));
}

static void string(hl_UNUSED bool canAssign) {
  emitConstant(
      hl_NEW_OBJ(
          hl_copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void namedVariable(struct hl_Token name, bool canAssign) {
  u8 getter, setter;
  s32 arg = resolveLocal(currentCompiler, &name);

  if (arg != -1) {
    getter = hl_OP_GET_LOCAL;
    setter = hl_OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(currentCompiler, &name)) != -1) {
    getter = hl_OP_GET_UPVALUE;
    setter = hl_OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getter = hl_OP_GET_GLOBAL;
    setter = hl_OP_SET_GLOBAL;
  }

#define COMPOUND_ASSIGNMENT(operator) \
    do { \
      emitBytes(getter, (u8)arg); \
      expression(); \
      emitByte(operator); \
      emitBytes(setter, (u8)arg); \
    } while (false)

  if (canAssign && match(hl_TOKEN_EQUAL)) {
    expression();
    emitBytes(setter, (u8)arg);
  } else if (canAssign && match(hl_TOKEN_PLUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_ADD);
  } else if (canAssign && match(hl_TOKEN_MINUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_SUBTRACT);
  } else if (canAssign && match(hl_TOKEN_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MULTIPLY);
  } else if (canAssign && match(hl_TOKEN_SLASH_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_DIVIDE);
  } else if (canAssign && match(hl_TOKEN_STAR_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_POW);
  } else if (canAssign && match(hl_TOKEN_PERCENT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MODULO);
  } else if (canAssign && match(hl_TOKEN_DOT_DOT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_CONCAT);
  } else {
    emitBytes(getter, (u8)arg);
  }
#undef COMPOUND_ASSIGNMENT
}

static void variable(hl_UNUSED bool canAssign) {
  struct hl_Token name = parser.previous;
  if (match(hl_TOKEN_LBRACE)) { // Struct initalization
    emitBytes(hl_OP_GET_GLOBAL, (u8)identifierConstant(&name));
    emitByte(hl_OP_INSTANCE);

    do {
      consume(hl_TOKEN_DOT, "Expected '.' before identifier.");
      consume(hl_TOKEN_IDENTIFIER, "Expected identifier.");
      struct hl_Token name = parser.previous;
      consume(hl_TOKEN_EQUAL, "Expected '=' after identifier.");
      expression();

      emitBytes(hl_OP_INIT_PROPERTY, identifierConstant(&name));

      if (!match(hl_TOKEN_COMMA) && !check(hl_TOKEN_RBRACE)) {
        error("Expected ','.");
      }
    } while (!check(hl_TOKEN_RBRACE) && !check(hl_TOKEN_EOF));

    consume(hl_TOKEN_RBRACE, "Unterminated struct initializer.");
  } else { // Variable reference
    namedVariable(name, canAssign);
  }
}

static void self(hl_UNUSED bool canAssign) {
  if (currentStruct == NULL) {
    error("Can only use 'self' inside struct methods.");
    return;
  }

  variable(false);
}

static void unary(hl_UNUSED bool canAssign) {
  enum hl_TokenType op = parser.previous.type; 

  parsePrecedence(PREC_UNARY);

  switch (op) {
    case hl_TOKEN_MINUS: emitByte(hl_OP_NEGATE); break;
    case hl_TOKEN_BANG:  emitByte(hl_OP_NOT); break;
    default: return;
  }
}

static void binary(hl_UNUSED bool canAssign) {
  enum hl_TokenType op = parser.previous.type;
  struct ParseRule* rule = getRule(op);
  parsePrecedence((enum Precedence)(rule->precedence + 1));

  switch (op) {
    case hl_TOKEN_PLUS:          emitByte(hl_OP_ADD); break;
    case hl_TOKEN_MINUS:         emitByte(hl_OP_SUBTRACT); break;
    case hl_TOKEN_STAR:          emitByte(hl_OP_MULTIPLY); break;
    case hl_TOKEN_SLASH:         emitByte(hl_OP_DIVIDE); break;
    case hl_TOKEN_PERCENT:       emitByte(hl_OP_MODULO); break;
    case hl_TOKEN_DOT_DOT:       emitByte(hl_OP_CONCAT); break;
    case hl_TOKEN_STAR_STAR:     emitByte(hl_OP_POW); break;
    case hl_TOKEN_EQUAL_EQUAL:   emitByte(hl_OP_EQUAL); break;
    case hl_TOKEN_BANG_EQUAL:    emitByte(hl_OP_NOT_EQUAL); break;
    case hl_TOKEN_GREATER:       emitByte(hl_OP_GREATER); break;
    case hl_TOKEN_LESS:          emitByte(hl_OP_LESSER); break;
    case hl_TOKEN_GREATER_EQUAL: emitByte(hl_OP_GREATER_EQUAL); break;
    case hl_TOKEN_LESS_EQUAL:    emitByte(hl_OP_LESSER_EQUAL); break;
    default: return;
  }
}

static u8 argumentList() {
  u8 argCount = 0;
  if (!check(hl_TOKEN_RPAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      argCount++;
    } while (match(hl_TOKEN_COMMA));
  }
  consume(hl_TOKEN_RPAREN, "Unclosed call.");
  return argCount;
}

static void call(hl_UNUSED bool canAssign) {
  u8 argCount = argumentList();
  emitBytes(hl_OP_CALL, argCount);
}

static void dot(bool canAssign) {
  consume(hl_TOKEN_IDENTIFIER, "Expected property name.");
  u8 name = identifierConstant(&parser.previous);

#define COMPOUND_ASSIGNMENT(operator) \
    do { \
      emitBytes(hl_OP_PUSH_PROPERTY, name); \
      expression(); \
      emitByte(operator); \
      emitBytes(hl_OP_SET_PROPERTY, name); \
    } while (false)

  if (canAssign && match(hl_TOKEN_EQUAL)) {
    expression();
    emitBytes(hl_OP_SET_PROPERTY, name);
  } else if (match(hl_TOKEN_LPAREN)) {
    u8 argCount = argumentList();
    emitBytes(hl_OP_INVOKE, name);
    emitByte(argCount);
  } else if (canAssign && match(hl_TOKEN_PLUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_ADD);
  } else if (canAssign && match(hl_TOKEN_MINUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_SUBTRACT);
  } else if (canAssign && match(hl_TOKEN_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MULTIPLY);
  } else if (canAssign && match(hl_TOKEN_SLASH_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_DIVIDE);
  } else if (canAssign && match(hl_TOKEN_STAR_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_POW);
  } else if (canAssign && match(hl_TOKEN_PERCENT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MODULO);
  } else if (canAssign && match(hl_TOKEN_DOT_DOT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_CONCAT);
  } else {
    emitBytes(hl_OP_GET_PROPERTY, name);
  }
#undef COMPOUND_ASSIGNMENT
}

static void subscript(bool canAssign) {
  expression();
  consume(hl_TOKEN_RBRACKET, "Unterminated subscript operator.");

  if (canAssign && match(hl_TOKEN_EQUAL)) {
    expression();
    emitByte(hl_OP_SET_SUBSCRIPT);
  } else {
    emitByte(hl_OP_GET_SUBSCRIPT);
  }
}

static void staticDot(hl_UNUSED bool canAssign) {
  consume(hl_TOKEN_IDENTIFIER, "Expected static method name.");
  u8 name = identifierConstant(&parser.previous);
  emitBytes(hl_OP_GET_STATIC, name);
}

static void function(enum FunctionType type, bool isLambda) {
  struct Compiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  if (match(hl_TOKEN_LPAREN)) {
    if (!check(hl_TOKEN_RPAREN)) {
      do {
        currentCompiler->function->arity++;
        if (currentCompiler->function->arity > 255) {
          errorAtCurrent("Too many parameters. Max is 255.");
        }
        u8 constant = parseVariable("Expected variable name.");
        defineVariable(constant);
      } while (match(hl_TOKEN_COMMA));
    }
    consume(hl_TOKEN_RPAREN, "Expected ')'.");
  }
  
  if (match(hl_TOKEN_LBRACE)) {
    block();
  } else if (match(hl_TOKEN_RIGHT_ARROW)) {
    expression();
    emitByte(hl_OP_RETURN);
    if (!isLambda) {
      consume(hl_TOKEN_SEMICOLON, "Expected ';' after expression.");
    }
  } else {
    error("Expected '{' or '=>'.");
  }

  struct hl_Function* function = endCompiler();
  emitBytes(hl_OP_CLOSURE, makeConstant(hl_NEW_OBJ(function)));

  for (s32 i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void lambda(hl_UNUSED bool canAssign) {
  function(FUNCTION_TYPE_FUNCTION, true);
}

static void literal(hl_UNUSED bool canAssign) {
  switch (parser.previous.type) {
    case hl_TOKEN_FALSE: emitByte(hl_OP_FALSE); break;
    case hl_TOKEN_TRUE: emitByte(hl_OP_TRUE); break;
    case hl_TOKEN_NIL: emitByte(hl_OP_NIL); break;
    default: return;
  }
}

static void array(hl_UNUSED bool canAssign) {
  u8 count = 0;
  if (!check(hl_TOKEN_RBRACKET)) {
    do {
      expression();
      if (count == 255) {
        error("Can't have more than 255 elements in an array literal.");
      }
      count++;

      if (!match(hl_TOKEN_COMMA) && !check(hl_TOKEN_RBRACKET)) {
        error("Expected ','.");
      }
    } while (!check(hl_TOKEN_RBRACKET));
  }
  consume(hl_TOKEN_RBRACKET, "Unterminated array literal.");

  emitBytes(hl_OP_ARRAY, count);
}

static void and_(hl_UNUSED bool canAssign) {
  s32 endJump = emitJump(hl_OP_JUMP_IF_FALSE);
  emitByte(hl_OP_POP);
  parsePrecedence(PREC_AND);
  patchJump(endJump);
}

static void or_(hl_UNUSED bool canAssign) {
  s32 elseJump = emitJump(hl_OP_JUMP_IF_FALSE);
  s32 endJump = emitJump(hl_OP_JUMP);

  patchJump(elseJump);
  emitByte(hl_OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static struct ParseRule rules[] = {
  [hl_TOKEN_LPAREN]        = {grouping, call,       PREC_CALL},
  [hl_TOKEN_RPAREN]        = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_LBRACE]        = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_RBRACE]        = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_LBRACKET]      = {array,    subscript,  PREC_CALL},
  [hl_TOKEN_RBRACKET]      = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_COMMA]         = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_DOT]           = {NULL,     dot,        PREC_CALL},
  [hl_TOKEN_SEMICOLON]     = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_COLON]         = {NULL,     staticDot,  PREC_CALL},
  [hl_TOKEN_MINUS]         = {unary,    binary,     PREC_TERM},
  [hl_TOKEN_PLUS]          = {NULL,     binary,     PREC_TERM},
  [hl_TOKEN_STAR]          = {NULL,     binary,     PREC_FACTOR},
  [hl_TOKEN_SLASH]         = {NULL,     binary,     PREC_FACTOR},
  [hl_TOKEN_STAR_STAR]     = {NULL,     binary,     PREC_EXPONENT},
  [hl_TOKEN_PERCENT]       = {NULL,     binary,     PREC_FACTOR},
  [hl_TOKEN_DOT_DOT]       = {NULL,     binary,     PREC_TERM},
  [hl_TOKEN_EQUAL]         = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_EQUAL_EQUAL]   = {NULL,     binary,     PREC_EQUALITY},
  [hl_TOKEN_BANG]          = {unary,    NULL,       PREC_NONE},
  [hl_TOKEN_BANG_EQUAL]    = {NULL,     binary,     PREC_EQUALITY},
  [hl_TOKEN_LESS]          = {NULL,     binary,     PREC_COMPARISON},
  [hl_TOKEN_LESS_EQUAL]    = {NULL,     binary,     PREC_COMPARISON},
  [hl_TOKEN_GREATER]       = {NULL,     binary,     PREC_COMPARISON},
  [hl_TOKEN_GREATER_EQUAL] = {NULL,     binary,     PREC_COMPARISON},
  [hl_TOKEN_AMP_AMP]       = {NULL,     and_,       PREC_AND},
  [hl_TOKEN_PIPE_PIPE]     = {NULL,     or_,        PREC_OR},
  [hl_TOKEN_VAR]           = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_WHILE]         = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_FOR]           = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_LOOP]          = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_IF]            = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_ELSE]          = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_MATCH]         = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_CASE]          = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_STRUCT]        = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_SELF]          = {self,     NULL,       PREC_NONE},
  [hl_TOKEN_FUNC]          = {lambda,   NULL,       PREC_NONE},
  [hl_TOKEN_TRUE]          = {literal,  NULL,       PREC_NONE},
  [hl_TOKEN_FALSE]         = {literal,  NULL,       PREC_NONE},
  [hl_TOKEN_NIL]           = {literal,  NULL,       PREC_NONE},
  [hl_TOKEN_IDENTIFIER]    = {variable, NULL,       PREC_NONE},
  [hl_TOKEN_STRING]        = {string,   NULL,       PREC_NONE},
  [hl_TOKEN_NUMBER]        = {number,   NULL,       PREC_NONE},
  [hl_TOKEN_ERROR]         = {NULL,     NULL,       PREC_NONE},
  [hl_TOKEN_EOF]           = {NULL,     NULL,       PREC_NONE},
};

static void parsePrecedence(enum Precedence precedence) {
  advance();

  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expected expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);
  
  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(hl_TOKEN_EQUAL)) {
    error("Cannot assign to that expression.");
  }
}

static struct ParseRule* getRule(enum hl_TokenType type) {
  return &rules[type];
}

static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
  while (!check(hl_TOKEN_RBRACE) && !check(hl_TOKEN_EOF)) {
    declaration();
  }

  consume(hl_TOKEN_RBRACE, "Unterminated block.");
}

static void method(bool isStatic) {
  consume(hl_TOKEN_IDENTIFIER, "Expected method name.");
  u8 constant = identifierConstant(&parser.previous);

  enum FunctionType type = isStatic 
      ? FUNCTION_TYPE_FUNCTION
      : FUNCTION_TYPE_METHOD;
  function(type, false);
  emitBytes(isStatic ? hl_OP_STATIC_METHOD : hl_OP_METHOD, constant);
}

static void functionDeclaration() {
  u8 global = parseVariable("Expected function name.");
  markInitialized();
  function(FUNCTION_TYPE_FUNCTION, false);
  defineVariable(global);
}

static void varDeclaration() {
  u8 global = parseVariable("Expected identifier.");

  if (match(hl_TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(hl_OP_NIL);
  }

  consume(hl_TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  defineVariable(global);
}

static void structDeclaration() {
  if (currentCompiler->scopeDepth != 0) {
    error("Structs must be defined in top-level code.");
  }

  struct StructCompiler structCompiler;
  structCompiler.enclosing = currentStruct;
  currentStruct = &structCompiler;

  consume(hl_TOKEN_IDENTIFIER, "Expected struct identifier.");
  struct hl_Token structName = parser.previous;
  u8 nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(hl_OP_STRUCT, nameConstant);
  defineVariable(nameConstant);

  namedVariable(structName, false);

  consume(hl_TOKEN_LBRACE, "Expected struct body.");

  while (!check(hl_TOKEN_RBRACE) && !check(hl_TOKEN_EOF)) {
    if (match(hl_TOKEN_VAR)) {
      // Default value can't be done at compile time, so we'll need an
      // instruction to do that
      consume(hl_TOKEN_IDENTIFIER, "Expected field identifier.");
      struct hl_Token name = parser.previous;

      if (match(hl_TOKEN_EQUAL)) {
        expression();
      } else {
        emitByte(hl_OP_NIL);
      }

      emitBytes(hl_OP_STRUCT_FIELD, identifierConstant(&name));

      consume(hl_TOKEN_SEMICOLON, "Expected ';' after field.");
    } else if (match(hl_TOKEN_FUNC)) {
      method(false);
    } else if (match(hl_TOKEN_STATIC)) {
      consume(hl_TOKEN_FUNC, "Expected 'func' after 'static'.");
      method(true);
    }
  }

  consume(hl_TOKEN_RBRACE, "Unterminated struct declaration.");

  emitByte(hl_OP_POP); // Struct

  currentStruct = currentStruct->enclosing;
}

void enumDeclaration() {
  if (currentCompiler->scopeDepth != 0) {
    error("Enums must be defined in top-level code.");
  }

  consume(hl_TOKEN_IDENTIFIER, "Expected enum identifier.");
  struct hl_Token enumName = parser.previous;
  u8 nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(hl_OP_ENUM, nameConstant);
  defineVariable(nameConstant);

  namedVariable(enumName, false);

  consume(hl_TOKEN_LBRACE, "Expected '{'.");

  u8 enumValue = 0;
  if (!check(hl_TOKEN_RBRACE)) {
    do {
      if (enumValue == 255) {
        error("Cannot have more than 255 enum values.");
      }

      consume(hl_TOKEN_IDENTIFIER, "Expected enum value.");
      emitByte(hl_OP_ENUM_VALUE);
      emitByte(identifierConstant(&parser.previous));
      emitByte(enumValue++);

      if (!match(hl_TOKEN_COMMA) && !check(hl_TOKEN_RBRACE)) {
        error("Expected ','.");
      }
    } while (!check(hl_TOKEN_RBRACE) && !check(hl_TOKEN_EOF));
  }

  consume(hl_TOKEN_RBRACE, "Unterminated enum declaration.");

  emitByte(hl_OP_POP); // Enum
}

static void expressionStatement() {
  expression();
  emitByte(hl_OP_POP);
  consume(hl_TOKEN_SEMICOLON, "Expected ';' after expression.");
}

static void ifStatement() {
  consume(hl_TOKEN_LPAREN, "Expected '('.");
  expression();
  consume(hl_TOKEN_RPAREN, "Expected ')'.");
  
  s32 thenJump = emitJump(hl_OP_JUMP_IF_FALSE);
  emitByte(hl_OP_POP);

  statement();

  s32 elseJump = emitJump(hl_OP_JUMP);
  patchJump(thenJump);
  emitByte(hl_OP_POP);

  if (match(hl_TOKEN_ELSE)) {
    statement();
  }

  patchJump(elseJump);
}

void matchStatement() {
  consume(hl_TOKEN_LPAREN, "Expected '('.");
  expression();
  consume(hl_TOKEN_RPAREN, "Expected ')'.");

  s32 caseEnds[256];
  s32 caseCount = 0;

  consume(hl_TOKEN_LBRACE, "Expected '{'");

  if (match(hl_TOKEN_CASE)) {
    do {
      expression();

      s32 inequalityJump = emitJump(hl_OP_INEQUALITY_JUMP);

      consume(hl_TOKEN_RIGHT_ARROW, "Expected '=>' after case expression.");
      statement();

      caseEnds[caseCount++] = emitJump(hl_OP_JUMP);
      if (caseCount == 256) {
        error("Cannot have more than 256 cases in a single match statement.");
      }

      patchJump(inequalityJump);
    } while (match(hl_TOKEN_CASE));
  }

  if (match(hl_TOKEN_CASE_DEFAULT)) {
    consume(hl_TOKEN_RIGHT_ARROW, "Expected '=>' after 'default'.");
    statement();
  }

  if (match(hl_TOKEN_CASE)) {
    error("Default case must be the last case.");
  }

  for (s32 i = 0; i < caseCount; i++) {
    patchJump(caseEnds[i]);
  }

  emitByte(hl_OP_POP); // Expression

  consume(hl_TOKEN_RBRACE, "Expected '}'");
}

static void continueStatement() {
  if (currentCompiler->loop == NULL) {
    error("Cannot use 'continue' outside of a loop.");
  }

  discardLocals();
  emitLoop(currentCompiler->loop->start);
  consume(hl_TOKEN_SEMICOLON, "Expected semicolon after 'break'.");
}

static void breakStatement() {
  if (currentCompiler->loop == NULL) {
    error("Cannot use 'break' outside of a loop.");
  }

  discardLocals();
  emitJump(hl_OP_BREAK);
  consume(hl_TOKEN_SEMICOLON, "Expected semicolon after 'break'.");
}

static void whileStatement() {
  struct Loop loop;
  beginLoop(&loop);

  consume(hl_TOKEN_LPAREN, "Expected '(' before while condition.");
  expression();
  consume(hl_TOKEN_RPAREN, "Expected ')' after while condition.");

  s32 exitJump = emitJump(hl_OP_JUMP_IF_FALSE);
  emitByte(hl_OP_POP);

  loop.bodyStart = currentChunk()->count;
  statement();

  emitLoop(loop.start);

  patchJump(exitJump);
  emitByte(hl_OP_POP);

  endLoop(&loop);
}

static void loopStatement() {
  struct Loop loop;
  beginLoop(&loop);

  loop.start = currentChunk()->count;
  statement();
  emitLoop(loop.start);

  endLoop(&loop);
}

static void returnStatement() {
  if (currentCompiler->type == FUNCTION_TYPE_SCRIPT) {
    error("Can only return in functions.");
  }

  if (match(hl_TOKEN_SEMICOLON)) {
    emitReturn();
    return;
  }

  expression();
  consume(hl_TOKEN_SEMICOLON, "Expected ';' after return value.");
  emitByte(hl_OP_RETURN);
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != hl_TOKEN_EOF) {
    if (parser.previous.type == hl_TOKEN_SEMICOLON) {
      return;
    }

    switch (parser.current.type) {
      case hl_TOKEN_STRUCT:
      case hl_TOKEN_STATIC:
      case hl_TOKEN_FUNC:
      case hl_TOKEN_ENUM:
      case hl_TOKEN_MATCH:
      case hl_TOKEN_VAR:
      case hl_TOKEN_FOR:
      case hl_TOKEN_IF:
      case hl_TOKEN_WHILE:
      case hl_TOKEN_LOOP:
      case hl_TOKEN_RETURN:
        return;
      default:
        break;
    }

    advance();
  }
}

static void declaration() {
  if (match(hl_TOKEN_VAR)) {
    varDeclaration();
  } else if (match(hl_TOKEN_FUNC)) {
    functionDeclaration();
  } else if (match(hl_TOKEN_STRUCT)) {
    structDeclaration();
  } else if (match(hl_TOKEN_ENUM)) {
    enumDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) {
    synchronize();
  }
}

static void statement() {
  if (match(hl_TOKEN_IF)) {
    ifStatement();
  } else if (match(hl_TOKEN_MATCH)) {
    matchStatement();
  } else if (match(hl_TOKEN_WHILE)) {
    whileStatement();
  } else if (match(hl_TOKEN_LOOP)) {
    loopStatement();
  } else if (match(hl_TOKEN_LBRACE)) {
    beginScope();
    block();
    endScope();
  } else if (match(hl_TOKEN_BREAK)) {
    breakStatement();
  } else if (match(hl_TOKEN_CONTINUE)) {
    continueStatement();
  } else if (match(hl_TOKEN_RETURN)) {
    returnStatement();
  } else {
    expressionStatement();
  }
}

struct hl_Function* hl_compile(const char* source) {
  hl_initTokenizer(source);

  struct Compiler compiler;
  initCompiler(&compiler, FUNCTION_TYPE_SCRIPT);

  parser.hadError = false;
  parser.panicMode = false;

  advance();
  while (!match(hl_TOKEN_EOF)) {
    declaration();
  }

  struct hl_Function* function = endCompiler();
  return parser.hadError ? NULL : function;
}

void hl_markCompilerRoots() {
  struct Compiler* compiler = currentCompiler;
  while (compiler != NULL) {
    hl_markObject((struct hl_Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}

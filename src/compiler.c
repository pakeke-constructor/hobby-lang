#include "compiler.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opcodes.h"
#include "common.h"
#include "compiler.h"
#include "tokenizer.h"
#include "object.h"
#include "memory.h"

#ifdef hl_DEBUG_PRINT_CODE
#include "debug.h"
#endif

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

typedef void (*ParseFn)(struct hl_Parser* parser, bool canAssign);

struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  enum Precedence precedence;
};

static struct hl_Function* currentFunction(struct hl_Parser* parser) {
  return parser->compiler->function;
}

static void errorAt(struct hl_Parser* parser, struct hl_Token* token, const char* message) {
  if (parser->panicMode) {
    return;
  }
  parser->panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == hl_TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type != hl_TOKEN_ERROR) {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser->hadError = true;
}

static void error(struct hl_Parser* parser, const char* message) {
  errorAt(parser, &parser->previous, message);
}

static void errorAtCurrent(struct hl_Parser* parser, const char* message) {
  errorAt(parser, &parser->current, message);
}

static void advance(struct hl_Parser* parser) {
  parser->previous = parser->current;

  while (true) {
    parser->current = hl_nextToken();
    if (parser->current.type != hl_TOKEN_ERROR) {
      break;
    }

    errorAtCurrent(parser, parser->current.start);
  }
}

static void consume(struct hl_Parser* parser, enum hl_TokenType type, const char* message) {
  if (parser->current.type == type) {
    advance(parser);
    return;
  }

  errorAtCurrent(parser, message);
}

static bool check(struct hl_Parser* parser, enum hl_TokenType type) {
  return parser->current.type == type;
}

static bool match(struct hl_Parser* parser, enum hl_TokenType type) {
  if (!check(parser, type)) {
    return false;
  }
  advance(parser);
  return true;
}

static void emitByte(struct hl_Parser* parser, u8 byte) {
  hl_writeBytecode(parser->H, currentFunction(parser), byte, parser->previous.line);
}

static void emitBytes(struct hl_Parser* parser, u8 byte1, u8 byte2) {
  emitByte(parser, byte1);
  emitByte(parser, byte2);
}

static s32 emitJump(struct hl_Parser* parser, u8 byte) {
  emitByte(parser, byte);
  emitByte(parser, 0xff);
  emitByte(parser, 0xff);
  return currentFunction(parser)->bcCount - 2;
}

static void patchJump(struct hl_Parser* parser, s32 offset) {
  s32 jump = currentFunction(parser)->bcCount - offset - 2;
  if (jump >= UINT16_MAX) {
    error(parser, "Too much code to jump over. Why?");
  }

  currentFunction(parser)->bc[offset] = (jump >> 8) & 0xff;
  currentFunction(parser)->bc[offset + 1] = jump & 0xff;
}

static void emitLoop(struct hl_Parser* parser, s32 loopStart) {
  emitByte(parser, hl_OP_LOOP);

  s32 offset = currentFunction(parser)->bcCount - loopStart + 2;
  if (offset > UINT16_MAX) {
    error(parser, "Loop is too big. I'm not quite sure why you made a loop this big.");
  }

  emitByte(parser, (offset >> 8) & 0xff);
  emitByte(parser, offset & 0xff);
}

static void emitReturn(struct hl_Parser* parser) {
  emitByte(parser, hl_OP_NIL);
  emitByte(parser, hl_OP_RETURN);
}

static u8 makeConstant(struct hl_Parser* parser, hl_Value value) {
  s32 constant = hl_addFunctionConstant(parser->H, currentFunction(parser), value);
  if (constant > UINT8_MAX) {
    error(parser, "Too many constants in the global scope or functions.");
    return 0;
  }

  return (u8)constant;
}

static void emitConstant(struct hl_Parser* parser, hl_Value value) {
  emitBytes(parser, hl_OP_CONSTANT, makeConstant(parser, value));
}

static void initCompiler(struct hl_Parser* parser,
                         struct hl_Compiler* compiler,
                         enum hl_FunctionType type) {
  compiler->enclosing = parser->compiler;

  compiler->function = NULL;
  compiler->type = type;

  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = hl_newFunction(parser->H);
  compiler->loop = NULL;
  parser->compiler = compiler;

  if (type != FUNCTION_TYPE_SCRIPT) {
    struct hl_Token name = parser->previous;
    if (name.type == hl_TOKEN_IDENTIFIER) {
      parser->compiler->function->name = hl_copyString(
          parser->H, name.start, name.length);
    } else if (name.type == hl_TOKEN_FUNC) { // lambda
      parser->compiler->function->name = hl_copyString(parser->H, "@lambda@", 8);
    }
  }

  struct hl_Local* local = 
      &parser->compiler->locals[parser->compiler->localCount++];
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

static struct hl_Function* endCompiler(struct hl_Parser* parser) {
  emitReturn(parser);
  struct hl_Function* function = parser->compiler->function;

#ifdef hl_DEBUG_PRINT_CODE
  if (!parser->hadError) {
    hl_disassembleChunk(
        currentChunk(),
        function, function->name != NULL ? function->name->chars : "<script>");
  }
#endif

  parser->compiler = parser->compiler->enclosing;
  return function;
}

s32 opCodeArgCount(struct hl_Parser* parser, enum hl_OpCode opCode, s32 ip) {
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
    case hl_OP_DESTRUCT_ARRAY:
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
      u8 index = currentFunction(parser)->bc[ip + 1];
      struct hl_Function* function = hl_AS_FUNCTION(
          currentFunction(parser)->constants.values[index]);
      return 1 + function->upvalueCount * 2;
    }
  }
  return -1;
}

static s32 discardLocals(struct hl_Parser* parser) {
  s32 discarded = 0;
  while (parser->compiler->localCount > 0
      && parser->compiler->locals[parser->compiler->localCount - 1].depth
         > parser->compiler->scopeDepth) {
    if (parser->compiler->locals
        [parser->compiler->localCount - discarded - 1].isCaptured) {
      emitByte(parser, hl_OP_CLOSE_UPVALUE);
    } else {
      emitByte(parser, hl_OP_POP);
    }
    discarded++;
  }

  return discarded;
}

static void beginLoop(struct hl_Parser* parser, struct hl_Loop* loop) {
  loop->start = currentFunction(parser)->bcCount;
  loop->scopeDepth = parser->compiler->scopeDepth;
  loop->enclosing = parser->compiler->loop;
  parser->compiler->loop = loop;
}

static void endLoop(struct hl_Parser* parser, struct hl_Loop* loop) {
  s32 end = currentFunction(parser)->bcCount;

  // Go through the whole body of the loop, find any jumps to the end and patch
  // them in.
  for (s32 instruction = loop->bodyStart; instruction < end;) {
    enum hl_OpCode opCode = currentFunction(parser)->bc[instruction];
    if (opCode == hl_OP_BREAK) {
      currentFunction(parser)->bc[instruction] = hl_OP_JUMP;
      patchJump(parser, instruction + 1);
      instruction += 3;
    } else {
      instruction += opCodeArgCount(parser, opCode, instruction) + 1;
    }
  }

  parser->compiler->loop = loop->enclosing;
}

static void beginScope(struct hl_Parser* parser) {
  parser->compiler->scopeDepth++;
}

static void endScope(struct hl_Parser* parser) {
  s32 discarded = discardLocals(parser);
  parser->compiler->localCount -= discarded;
  parser->compiler->scopeDepth--;
}

static void expression(struct hl_Parser* parser);
static void statement(struct hl_Parser* parser);
static void declaration(struct hl_Parser* parser);
static struct ParseRule* getRule(enum hl_TokenType type);
static void parsePrecedence(struct hl_Parser* parser, enum Precedence precedence);
static void block(struct hl_Parser* parser);

static void markInitialized(struct hl_Parser* parser) {
  if (parser->compiler->scopeDepth == 0) {
    return;
  }

  parser->compiler->locals[parser->compiler->localCount - 1].depth
      = parser->compiler->scopeDepth;
}

static void defineVariable(struct hl_Parser* parser, u8 global) {
  if (parser->compiler->scopeDepth > 0) {
    markInitialized(parser);
    return;
  }

  emitBytes(parser, hl_OP_DEFINE_GLOBAL, global);
}

static u8 identifierConstant(struct hl_Parser* parser, struct hl_Token* name) {
  return makeConstant(parser, hl_NEW_OBJ(hl_copyString(parser->H, name->start, name->length)));
}

static bool identifiersEqual(struct hl_Token* a, struct hl_Token* b) {
  if (a->length != b->length) {
    return false;
  }
  return memcmp(a->start, b->start, a->length) == 0;
}

static s32 resolveLocal(
    struct hl_Parser* parser, struct hl_Compiler* compiler, struct hl_Token* name) {
  for (s32 i = compiler->localCount - 1; i >= 0; i--) {
    struct hl_Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error(parser, "Can't read local variable in it's own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static s32 addUpvalue(struct hl_Parser* parser, struct hl_Compiler* compiler, u8 index, bool isLocal) {
  s32 upvalueCount = compiler->function->upvalueCount;

  for (s32 i = 0; i < upvalueCount; i++) {
    struct hl_CompilerUpvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == hl_U8_COUNT) {
    error(parser, "Too many upvalues in a function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static s32 resolveUpvalue(struct hl_Parser* parser, struct hl_Compiler* compiler, struct hl_Token* name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }

  s32 local = resolveLocal(parser, compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(parser, compiler, (u8)local, true);
  }

  s32 upvalue = resolveUpvalue(parser, compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(parser, compiler, (u8)upvalue, false);
  }

  return -1;
}

static void addLocal(struct hl_Parser* parser, struct hl_Token name) {
  if (parser->compiler->localCount == hl_U8_COUNT) {
    error(parser, "Too many local variables in function.");
    return;
  }

  struct hl_Local* local = 
      &parser->compiler->locals[parser->compiler->localCount++];
  local->name = name;
  local->isCaptured = false;
  local->depth = -1;
  local->depth = parser->compiler->scopeDepth;
}

static void declareVariable(struct hl_Parser* parser) {
  if (parser->compiler->scopeDepth == 0) {
    return;
  }
  
  struct hl_Token* name = &parser->previous;

  for (s32 i = parser->compiler->localCount - 1; i >= 0; i--) {
    struct hl_Local* local = &parser->compiler->locals[i];
    if (local->depth != -1 && local->depth < parser->compiler->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error(parser, "Redefinition of variable.");
    }
  }

  addLocal(parser, *name);
}

static u8 parseVariable(struct hl_Parser* parser, const char* errorMessage) {
  consume(parser, hl_TOKEN_IDENTIFIER, errorMessage);

  declareVariable(parser);
  if (parser->compiler->scopeDepth > 0) {
    return 0;
  }

  return identifierConstant(parser, &parser->previous);
}

static void grouping(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  expression(parser);
  consume(parser, hl_TOKEN_RPAREN, "Expected ')' after expression.");
}

static void number(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  f64 value = strtod(parser->previous.start, NULL);
  emitConstant(parser, hl_NEW_NUMBER(value));
}

static void string(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  emitConstant(
      parser, 
      hl_NEW_OBJ(hl_copyString(
          parser->H, parser->previous.start + 1, parser->previous.length - 2)));
}

static void namedVariable(struct hl_Parser* parser, struct hl_Token name, bool canAssign) {
  u8 getter, setter;
  s32 arg = resolveLocal(parser, parser->compiler, &name);

  if (arg != -1) {
    getter = hl_OP_GET_LOCAL;
    setter = hl_OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(parser, parser->compiler, &name)) != -1) {
    getter = hl_OP_GET_UPVALUE;
    setter = hl_OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(parser, &name);
    getter = hl_OP_GET_GLOBAL;
    setter = hl_OP_SET_GLOBAL;
  }

#define COMPOUND_ASSIGNMENT(operator) \
    do { \
      emitBytes(parser, getter, (u8)arg); \
      expression(parser); \
      emitByte(parser, operator); \
      emitBytes(parser, setter, (u8)arg); \
    } while (false)

  if (canAssign && match(parser, hl_TOKEN_EQUAL)) {
    expression(parser);
    emitBytes(parser, setter, (u8)arg);
  } else if (canAssign && match(parser, hl_TOKEN_PLUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_ADD);
  } else if (canAssign && match(parser, hl_TOKEN_MINUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_SUBTRACT);
  } else if (canAssign && match(parser, hl_TOKEN_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MULTIPLY);
  } else if (canAssign && match(parser, hl_TOKEN_SLASH_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_DIVIDE);
  } else if (canAssign && match(parser, hl_TOKEN_STAR_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_POW);
  } else if (canAssign && match(parser, hl_TOKEN_PERCENT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MODULO);
  } else if (canAssign && match(parser, hl_TOKEN_DOT_DOT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_CONCAT);
  } else {
    emitBytes(parser, getter, (u8)arg);
  }
#undef COMPOUND_ASSIGNMENT
}

static void variable(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  struct hl_Token name = parser->previous;
  if (match(parser, hl_TOKEN_LBRACE)) { // Struct initalization
    emitBytes(parser, hl_OP_GET_GLOBAL, (u8)identifierConstant(parser, &name));
    emitByte(parser, hl_OP_INSTANCE);

    if (check(parser, hl_TOKEN_DOT)) {
      do {
        consume(parser, hl_TOKEN_DOT, "Expected '.' before identifier.");
        consume(parser, hl_TOKEN_IDENTIFIER, "Expected identifier.");
        struct hl_Token name = parser->previous;
        consume(parser, hl_TOKEN_EQUAL, "Expected '=' after identifier.");
        expression(parser);

        emitBytes(parser, hl_OP_INIT_PROPERTY, identifierConstant(parser, &name));

        if (!match(parser, hl_TOKEN_COMMA) && !check(parser, hl_TOKEN_RBRACE)) {
          error(parser, "Expected ','.");
        }
      } while (!check(parser, hl_TOKEN_RBRACE) && !check(parser, hl_TOKEN_EOF));
    }

    consume(parser, hl_TOKEN_RBRACE, "Unterminated struct initializer.");
  } else { // Variable reference
    namedVariable(parser, name, canAssign);
  }
}

static void self(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  if (parser->structCompiler == NULL) {
    error(parser, "Can only use 'self' inside struct methods.");
    return;
  }

  variable(parser, false);
}

static void unary(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  enum hl_TokenType op = parser->previous.type; 

  parsePrecedence(parser, PREC_UNARY);

  switch (op) {
    case hl_TOKEN_MINUS: emitByte(parser, hl_OP_NEGATE); break;
    case hl_TOKEN_BANG:  emitByte(parser, hl_OP_NOT); break;
    default: return;
  }
}

static void binary(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  enum hl_TokenType op = parser->previous.type;
  struct ParseRule* rule = getRule(op);
  parsePrecedence(parser, (enum Precedence)(rule->precedence + 1));

  switch (op) {
    case hl_TOKEN_PLUS:          emitByte(parser, hl_OP_ADD); break;
    case hl_TOKEN_MINUS:         emitByte(parser, hl_OP_SUBTRACT); break;
    case hl_TOKEN_STAR:          emitByte(parser, hl_OP_MULTIPLY); break;
    case hl_TOKEN_SLASH:         emitByte(parser, hl_OP_DIVIDE); break;
    case hl_TOKEN_PERCENT:       emitByte(parser, hl_OP_MODULO); break;
    case hl_TOKEN_DOT_DOT:       emitByte(parser, hl_OP_CONCAT); break;
    case hl_TOKEN_STAR_STAR:     emitByte(parser, hl_OP_POW); break;
    case hl_TOKEN_EQUAL_EQUAL:   emitByte(parser, hl_OP_EQUAL); break;
    case hl_TOKEN_BANG_EQUAL:    emitByte(parser, hl_OP_NOT_EQUAL); break;
    case hl_TOKEN_GREATER:       emitByte(parser, hl_OP_GREATER); break;
    case hl_TOKEN_LESS:          emitByte(parser, hl_OP_LESSER); break;
    case hl_TOKEN_GREATER_EQUAL: emitByte(parser, hl_OP_GREATER_EQUAL); break;
    case hl_TOKEN_LESS_EQUAL:    emitByte(parser, hl_OP_LESSER_EQUAL); break;
    default: return;
  }
}

static u8 argumentList(struct hl_Parser* parser) {
  u8 argCount = 0;
  if (!check(parser, hl_TOKEN_RPAREN)) {
    do {
      expression(parser);
      if (argCount == 255) {
        error(parser, "Can't have more than 255 arguments.");
      }
      argCount++;
    } while (match(parser, hl_TOKEN_COMMA));
  }
  consume(parser, hl_TOKEN_RPAREN, "Unclosed call.");
  return argCount;
}

static void call(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  u8 argCount = argumentList(parser);
  emitBytes(parser, hl_OP_CALL, argCount);
}

static void dot(struct hl_Parser* parser, bool canAssign) {
  consume(parser, hl_TOKEN_IDENTIFIER, "Expected property name.");
  u8 name = identifierConstant(parser, &parser->previous);

#define COMPOUND_ASSIGNMENT(operator) \
    do { \
      emitBytes(parser, hl_OP_PUSH_PROPERTY, name); \
      expression(parser); \
      emitByte(parser, operator); \
      emitBytes(parser, hl_OP_SET_PROPERTY, name); \
    } while (false)

  if (canAssign && match(parser, hl_TOKEN_EQUAL)) {
    expression(parser);
    emitBytes(parser, hl_OP_SET_PROPERTY, name);
  } else if (match(parser, hl_TOKEN_LPAREN)) {
    u8 argCount = argumentList(parser);
    emitBytes(parser, hl_OP_INVOKE, name);
    emitByte(parser, argCount);
  } else if (canAssign && match(parser, hl_TOKEN_PLUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_ADD);
  } else if (canAssign && match(parser, hl_TOKEN_MINUS_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_SUBTRACT);
  } else if (canAssign && match(parser, hl_TOKEN_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MULTIPLY);
  } else if (canAssign && match(parser, hl_TOKEN_SLASH_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_DIVIDE);
  } else if (canAssign && match(parser, hl_TOKEN_STAR_STAR_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_POW);
  } else if (canAssign && match(parser, hl_TOKEN_PERCENT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_MODULO);
  } else if (canAssign && match(parser, hl_TOKEN_DOT_DOT_EQUAL)) {
    COMPOUND_ASSIGNMENT(hl_OP_CONCAT);
  } else {
    emitBytes(parser, hl_OP_GET_PROPERTY, name);
  }
#undef COMPOUND_ASSIGNMENT
}

static void subscript(struct hl_Parser* parser, bool canAssign) {
  expression(parser);
  consume(parser, hl_TOKEN_RBRACKET, "Unterminated subscript operator.");

  if (canAssign && match(parser, hl_TOKEN_EQUAL)) {
    expression(parser);
    emitByte(parser, hl_OP_SET_SUBSCRIPT);
  } else {
    emitByte(parser, hl_OP_GET_SUBSCRIPT);
  }
}

static void staticDot(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  consume(parser, hl_TOKEN_IDENTIFIER, "Expected static method name.");
  u8 name = identifierConstant(parser, &parser->previous);
  emitBytes(parser, hl_OP_GET_STATIC, name);
}

static void function(struct hl_Parser* parser, enum hl_FunctionType type, bool isLambda) {
  struct hl_Compiler compiler;
  initCompiler(parser, &compiler, type);
  beginScope(parser);

  if (match(parser, hl_TOKEN_LPAREN)) {
    if (!check(parser, hl_TOKEN_RPAREN)) {
      do {
        parser->compiler->function->arity++;
        if (parser->compiler->function->arity == 255) {
          errorAtCurrent(parser, "Too many parameters. Max is 255.");
        }
        u8 constant = parseVariable(parser, "Expected variable name.");
        defineVariable(parser, constant);
      } while (match(parser, hl_TOKEN_COMMA));
    }
    consume(parser, hl_TOKEN_RPAREN, "Expected ')'.");
  }
  
  if (match(parser, hl_TOKEN_LBRACE)) {
    block(parser);
  } else if (match(parser, hl_TOKEN_RIGHT_ARROW)) {
    expression(parser);
    emitByte(parser, hl_OP_RETURN);
    if (!isLambda) {
      consume(parser, hl_TOKEN_SEMICOLON, "Expected ';' after expression.");
    }
  } else {
    error(parser, "Expected '{' or '=>'.");
  }

  struct hl_Function* function = endCompiler(parser);
  emitBytes(parser, hl_OP_CLOSURE, makeConstant(parser, hl_NEW_OBJ(function)));

  for (s32 i = 0; i < function->upvalueCount; i++) {
    emitByte(parser, compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(parser, compiler.upvalues[i].index);
  }
}

static void lambda(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  function(parser, FUNCTION_TYPE_FUNCTION, true);
}

static void literal(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  switch (parser->previous.type) {
    case hl_TOKEN_FALSE: emitByte(parser, hl_OP_FALSE); break;
    case hl_TOKEN_TRUE: emitByte(parser, hl_OP_TRUE); break;
    case hl_TOKEN_NIL: emitByte(parser, hl_OP_NIL); break;
    default: return;
  }
}

static void array(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  u8 count = 0;
  if (!check(parser, hl_TOKEN_RBRACKET)) {
    do {
      expression(parser);
      if (count == 255) {
        error(parser, "Can't have more than 255 elements in an array literal.");
      }
      count++;

      if (!match(parser, hl_TOKEN_COMMA) && !check(parser, hl_TOKEN_RBRACKET)) {
        error(parser, "Expected ','.");
      }
    } while (!check(parser, hl_TOKEN_RBRACKET));
  }
  consume(parser, hl_TOKEN_RBRACKET, "Unterminated array literal.");

  emitBytes(parser, hl_OP_ARRAY, count);
}

static void and_(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  s32 endJump = emitJump(parser, hl_OP_JUMP_IF_FALSE);
  emitByte(parser, hl_OP_POP);
  parsePrecedence(parser, PREC_AND);
  patchJump(parser, endJump);
}

static void or_(struct hl_Parser* parser, hl_UNUSED bool canAssign) {
  s32 elseJump = emitJump(parser, hl_OP_JUMP_IF_FALSE);
  s32 endJump = emitJump(parser, hl_OP_JUMP);

  patchJump(parser, elseJump);
  emitByte(parser, hl_OP_POP);

  parsePrecedence(parser, PREC_OR);
  patchJump(parser, endJump);
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

static void parsePrecedence(struct hl_Parser* parser, enum Precedence precedence) {
  advance(parser);

  ParseFn prefixRule = getRule(parser->previous.type)->prefix;
  if (prefixRule == NULL) {
    error(parser, "Expected expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(parser, canAssign);
  
  while (precedence <= getRule(parser->current.type)->precedence) {
    advance(parser);
    ParseFn infixRule = getRule(parser->previous.type)->infix;
    infixRule(parser, canAssign);
  }

  if (canAssign && match(parser, hl_TOKEN_EQUAL)) {
    error(parser, "Cannot assign to that expression.");
  }
}

static struct ParseRule* getRule(enum hl_TokenType type) {
  return &rules[type];
}

static void expression(struct hl_Parser* parser) {
  parsePrecedence(parser, PREC_ASSIGNMENT);
}

static void block(struct hl_Parser* parser) {
  while (!check(parser, hl_TOKEN_RBRACE) && !check(parser, hl_TOKEN_EOF)) {
    declaration(parser);
  }

  consume(parser, hl_TOKEN_RBRACE, "Unterminated block.");
}

static void method(struct hl_Parser* parser, bool isStatic) {
  consume(parser, hl_TOKEN_IDENTIFIER, "Expected method name.");
  u8 constant = identifierConstant(parser, &parser->previous);

  enum hl_FunctionType type = isStatic 
      ? FUNCTION_TYPE_FUNCTION
      : FUNCTION_TYPE_METHOD;
  function(parser, type, false);
  emitBytes(parser, isStatic ? hl_OP_STATIC_METHOD : hl_OP_METHOD, constant);
}

static void functionDeclaration(struct hl_Parser* parser) {
  u8 global = parseVariable(parser, "Expected function name.");
  markInitialized(parser);
  function(parser, FUNCTION_TYPE_FUNCTION, false);
  defineVariable(parser, global);
}

static void arrayDestructAssignment(struct hl_Parser* parser) {
  u8 setters[UINT8_MAX];
  u8 variables[UINT8_MAX];
  u8 variableCount = 0;
  do {
    if (variableCount == 255) {
      error(parser, "Cannot have more than 255 variables per assignment.");
      return;
    }

    consume(parser, hl_TOKEN_IDENTIFIER, "Expected identifier.");
    struct hl_Token name = parser->previous;

    u8 setter;
    s32 arg = resolveLocal(parser, parser->compiler, &name);

    if (arg != -1) {
      setter = hl_OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(parser, parser->compiler, &name)) != -1) {
      setter = hl_OP_SET_UPVALUE;
    } else {
      arg = identifierConstant(parser, &name);
      setter = hl_OP_SET_GLOBAL;
    }

    variables[variableCount] = arg;
    setters[variableCount] = setter;

    variableCount++;
  } while (match(parser, hl_TOKEN_COMMA));

  consume(parser, hl_TOKEN_RBRACKET, "Expected ']'.");
  consume(parser, hl_TOKEN_EQUAL, "Expected '='.");
  expression(parser);

  for (u8 i = 0; i < variableCount; i++) {
    emitBytes(parser, hl_OP_DESTRUCT_ARRAY, i);
    emitBytes(parser, setters[i], variables[i]);
    emitByte(parser, hl_OP_POP);
  }

  emitByte(parser, hl_OP_POP);
  consume(parser, hl_TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
}

static void varDeclaration(struct hl_Parser* parser) {
  if (match(parser, hl_TOKEN_LBRACKET)) {
    u8 variables[UINT8_MAX];
    u8 variableCount = 0;
    do {
      if (variableCount == 255) {
        error(parser, "Cannot have more than 255 variables per var.");
        return;
      }

      consume(parser, hl_TOKEN_IDENTIFIER, "Expected identifier.");
      u8 nameConstant = identifierConstant(parser, &parser->previous);
      variables[variableCount++] = nameConstant;
      declareVariable(parser);
    } while (match(parser, hl_TOKEN_COMMA));

    consume(parser, hl_TOKEN_RBRACKET, "Expected ']'.");
    consume(parser, hl_TOKEN_EQUAL, "Expected '='.");
    expression(parser);

    for (u8 i = 0; i < variableCount; i++) {
      emitBytes(parser, hl_OP_DESTRUCT_ARRAY, i);
      defineVariable(parser, variables[i]);
    }

    emitByte(parser, hl_OP_POP);
  } else {
    u8 global = parseVariable(parser, "Expected identifier.");

    if (match(parser, hl_TOKEN_EQUAL)) {
      expression(parser);
    } else {
      emitByte(parser, hl_OP_NIL);
    }

    defineVariable(parser, global);
  }

  consume(parser, hl_TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
}

static void structDeclaration(struct hl_Parser* parser) {
  if (parser->compiler->scopeDepth != 0) {
    error(parser, "Structs must be defined in top-level code.");
  }

  struct hl_StructCompiler structCompiler;
  structCompiler.enclosing = parser->structCompiler;
  parser->structCompiler = &structCompiler;

  consume(parser, hl_TOKEN_IDENTIFIER, "Expected struct identifier.");
  struct hl_Token structName = parser->previous;
  u8 nameConstant = identifierConstant(parser, &parser->previous);
  declareVariable(parser);

  emitBytes(parser, hl_OP_STRUCT, nameConstant);
  defineVariable(parser, nameConstant);

  namedVariable(parser, structName, false);

  consume(parser, hl_TOKEN_LBRACE, "Expected struct body.");

  while (!check(parser, hl_TOKEN_RBRACE) && !check(parser, hl_TOKEN_EOF)) {
    if (match(parser, hl_TOKEN_VAR)) {
      // Default value can't be done at compile time, so we'll need an
      // instruction to do that
      consume(parser, hl_TOKEN_IDENTIFIER, "Expected field identifier.");
      struct hl_Token name = parser->previous;

      if (match(parser, hl_TOKEN_EQUAL)) {
        expression(parser);
      } else {
        emitByte(parser, hl_OP_NIL);
      }

      emitBytes(parser, hl_OP_STRUCT_FIELD, identifierConstant(parser, &name));

      consume(parser, hl_TOKEN_SEMICOLON, "Expected ';' after field.");
    } else if (match(parser, hl_TOKEN_FUNC)) {
      method(parser, false);
    } else if (match(parser, hl_TOKEN_STATIC)) {
      consume(parser, hl_TOKEN_FUNC, "Expected 'func' after 'static'.");
      method(parser, true);
    }
  }

  consume(parser, hl_TOKEN_RBRACE, "Unterminated struct declaration.");

  emitByte(parser, hl_OP_POP); // Struct

  parser->structCompiler = parser->structCompiler->enclosing;
}

void enumDeclaration(struct hl_Parser* parser) {
  if (parser->compiler->scopeDepth != 0) {
    error(parser, "Enums must be defined in top-level code.");
  }

  consume(parser, hl_TOKEN_IDENTIFIER, "Expected enum identifier.");
  struct hl_Token enumName = parser->previous;
  u8 nameConstant = identifierConstant(parser, &parser->previous);
  declareVariable(parser);

  emitBytes(parser, hl_OP_ENUM, nameConstant);
  defineVariable(parser, nameConstant);

  namedVariable(parser, enumName, false);

  consume(parser, hl_TOKEN_LBRACE, "Expected '{'.");

  u8 enumValue = 0;
  if (!check(parser, hl_TOKEN_RBRACE)) {
    do {
      if (enumValue == 255) {
        error(parser, "Cannot have more than 255 enum values.");
      }

      consume(parser, hl_TOKEN_IDENTIFIER, "Expected enum value.");
      emitByte(parser, hl_OP_ENUM_VALUE);
      emitByte(parser, identifierConstant(parser, &parser->previous));
      emitByte(parser, enumValue++);

      if (!match(parser, hl_TOKEN_COMMA) && !check(parser, hl_TOKEN_RBRACE)) {
        error(parser, "Expected ','.");
      }
    } while (!check(parser, hl_TOKEN_RBRACE) && !check(parser, hl_TOKEN_EOF));
  }

  consume(parser, hl_TOKEN_RBRACE, "Unterminated enum declaration.");

  emitByte(parser, hl_OP_POP); // Enum
}

static void expressionStatement(struct hl_Parser* parser) {
  expression(parser);
  emitByte(parser, hl_OP_POP);
  consume(parser, hl_TOKEN_SEMICOLON, "Expected ';' after expression.");
}

static void ifStatement(struct hl_Parser* parser) {
  consume(parser, hl_TOKEN_LPAREN, "Expected '('.");
  expression(parser);
  consume(parser, hl_TOKEN_RPAREN, "Expected ')'.");
  
  s32 thenJump = emitJump(parser, hl_OP_JUMP_IF_FALSE);
  emitByte(parser, hl_OP_POP);

  statement(parser);

  s32 elseJump = emitJump(parser, hl_OP_JUMP);
  patchJump(parser, thenJump);
  emitByte(parser, hl_OP_POP);

  if (match(parser, hl_TOKEN_ELSE)) {
    statement(parser);
  }

  patchJump(parser, elseJump);
}

void matchStatement(struct hl_Parser* parser) {
  consume(parser, hl_TOKEN_LPAREN, "Expected '('.");
  expression(parser);
  consume(parser, hl_TOKEN_RPAREN, "Expected ')'.");

  s32 caseEnds[256];
  s32 caseCount = 0;

  consume(parser, hl_TOKEN_LBRACE, "Expected '{'");

  if (match(parser, hl_TOKEN_CASE)) {
    do {
      expression(parser);

      s32 inequalityJump = emitJump(parser, hl_OP_INEQUALITY_JUMP);

      consume(parser, hl_TOKEN_RIGHT_ARROW, "Expected '=>' after case expression.");
      statement(parser);

      caseEnds[caseCount++] = emitJump(parser, hl_OP_JUMP);
      if (caseCount == 256) {
        error(parser, "Cannot have more than 256 cases in a single match statement.");
      }

      patchJump(parser, inequalityJump);
    } while (match(parser, hl_TOKEN_CASE));
  }

  if (match(parser, hl_TOKEN_ELSE)) {
    consume(parser, hl_TOKEN_RIGHT_ARROW, "Expected '=>' after 'else'.");
    statement(parser);
  }

  if (match(parser, hl_TOKEN_CASE)) {
    error(parser, "Default case must be the last case.");
  }

  for (s32 i = 0; i < caseCount; i++) {
    patchJump(parser, caseEnds[i]);
  }

  emitByte(parser, hl_OP_POP); // Expression

  consume(parser, hl_TOKEN_RBRACE, "Expected '}'");
}

static void continueStatement(struct hl_Parser* parser) {
  if (parser->compiler->loop == NULL) {
    error(parser, "Cannot use 'continue' outside of a loop.");
  }

  discardLocals(parser);
  emitLoop(parser, parser->compiler->loop->start);
  consume(parser, hl_TOKEN_SEMICOLON, "Expected semicolon after 'break'.");
}

static void breakStatement(struct hl_Parser* parser) {
  if (parser->compiler->loop == NULL) {
    error(parser, "Cannot use 'break' outside of a loop.");
  }

  discardLocals(parser);
  emitJump(parser, hl_OP_BREAK);
  consume(parser, hl_TOKEN_SEMICOLON, "Expected semicolon after 'break'.");
}

static void whileStatement(struct hl_Parser* parser) {
  struct hl_Loop loop;
  beginLoop(parser, &loop);

  consume(parser, hl_TOKEN_LPAREN, "Expected '(' before while condition.");
  expression(parser);
  consume(parser, hl_TOKEN_RPAREN, "Expected ')' after while condition.");

  s32 exitJump = emitJump(parser, hl_OP_JUMP_IF_FALSE);
  emitByte(parser, hl_OP_POP);

  loop.bodyStart = currentFunction(parser)->bcCount;
  statement(parser);

  emitLoop(parser, loop.start);

  patchJump(parser, exitJump);
  emitByte(parser, hl_OP_POP);

  endLoop(parser, &loop);
}

static void loopStatement(struct hl_Parser* parser) {
  struct hl_Loop loop;
  beginLoop(parser, &loop);

  loop.start = currentFunction(parser)->bcCount;
  statement(parser);
  emitLoop(parser, loop.start);

  endLoop(parser, &loop);
}

static void returnStatement(struct hl_Parser* parser) {
  if (parser->compiler->type == FUNCTION_TYPE_SCRIPT) {
    error(parser, "Can only return in functions.");
  }

  if (match(parser, hl_TOKEN_SEMICOLON)) {
    emitReturn(parser);
    return;
  }

  expression(parser);
  consume(parser, hl_TOKEN_SEMICOLON, "Expected ';' after return value.");
  emitByte(parser, hl_OP_RETURN);
}

static void synchronize(struct hl_Parser* parser) {
  parser->panicMode = false;

  while (parser->current.type != hl_TOKEN_EOF) {
    if (parser->previous.type == hl_TOKEN_SEMICOLON) {
      return;
    }

    switch (parser->current.type) {
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

    advance(parser);
  }
}

static void declaration(struct hl_Parser* parser) {
  if (match(parser, hl_TOKEN_VAR)) {
    varDeclaration(parser);
  } else if (match(parser, hl_TOKEN_LBRACKET)) {
    arrayDestructAssignment(parser);
  } else if (match(parser, hl_TOKEN_FUNC)) {
    functionDeclaration(parser);
  } else if (match(parser, hl_TOKEN_STRUCT)) {
    structDeclaration(parser);
  } else if (match(parser, hl_TOKEN_ENUM)) {
    enumDeclaration(parser);
  } else {
    statement(parser);
  }

  if (parser->panicMode) {
    synchronize(parser);
  }
}

static void statement(struct hl_Parser* parser) {
  if (match(parser, hl_TOKEN_IF)) {
    ifStatement(parser);
  } else if (match(parser, hl_TOKEN_MATCH)) {
    matchStatement(parser);
  } else if (match(parser, hl_TOKEN_WHILE)) {
    whileStatement(parser);
  } else if (match(parser, hl_TOKEN_LOOP)) {
    loopStatement(parser);
  } else if (match(parser, hl_TOKEN_LBRACE)) {
    beginScope(parser);
    block(parser);
    endScope(parser);
  } else if (match(parser, hl_TOKEN_BREAK)) {
    breakStatement(parser);
  } else if (match(parser, hl_TOKEN_CONTINUE)) {
    continueStatement(parser);
  } else if (match(parser, hl_TOKEN_RETURN)) {
    returnStatement(parser);
  } else {
    expressionStatement(parser);
  }
}

struct hl_Function* hl_compile(struct hl_State* H, struct hl_Parser* parser, const char* source) {
  hl_initTokenizer(source);

  parser->compiler = NULL;
  parser->H = H;
  parser->hadError = false;
  parser->panicMode = false;

  struct hl_Compiler compiler;
  initCompiler(parser, &compiler, FUNCTION_TYPE_SCRIPT);

  advance(parser);
  while (!match(parser, hl_TOKEN_EOF)) {
    declaration(parser);
  }

  struct hl_Function* function = endCompiler(parser);
  return parser->hadError ? NULL : function;
}

void hl_markCompilerRoots(struct hl_State* H, struct hl_Parser* parser) {
  struct hl_Compiler* compiler = parser->compiler;
  while (compiler != NULL) {
    hl_markObject(H, (struct hl_Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}

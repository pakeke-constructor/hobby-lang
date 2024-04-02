#ifndef _HOBBYL_COMPILER_H
#define _HOBBYL_COMPILER_H

#include "tokenizer.h"
#include "object.h"

struct hl_Loop {
  s32 start;
  s32 bodyStart;
  s32 scopeDepth;
  struct hl_Loop* enclosing;
};

struct hl_Local {
  struct hl_Token name;
  s32 depth;
  bool isCaptured;
};

struct hl_CompilerUpvalue {
  u8 index;
  bool isLocal;
};

enum hl_FunctionType {
  FUNCTION_TYPE_FUNCTION,
  FUNCTION_TYPE_METHOD,
  FUNCTION_TYPE_SCRIPT,
};

struct hl_Compiler {
  struct hl_Compiler* enclosing;

  struct hl_Function* function;
  enum hl_FunctionType type;

  struct hl_Local locals[hl_U8_COUNT];
  struct hl_Loop* loop;
  s32 localCount;
  struct hl_CompilerUpvalue upvalues[hl_U8_COUNT];
  s32 scopeDepth;
};

struct hl_StructField {
  struct hl_Token name;
};

struct hl_StructCompiler {
  struct hl_StructCompiler* enclosing;
};

struct hl_Parser {
  struct hl_Token current;
  struct hl_Token previous;
  struct hl_Compiler* compiler;
  struct hl_StructCompiler* structCompiler;
  bool hadError;
  bool panicMode;
};

struct hl_Function* hl_compile(struct hl_Parser* parser, const char* source);
void hl_markCompilerRoots(struct hl_Parser* parser);

#endif // _HOBBYL_COMPILER_H

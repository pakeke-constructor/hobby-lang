#ifndef _HOBBYL_TOKENIZER_H
#define _HOBBYL_TOKENIZER_H

#include "common.h"

enum hl_TokenType {
  hl_TOKEN_LPAREN, hl_TOKEN_RPAREN, // ( )
  hl_TOKEN_LBRACE, hl_TOKEN_RBRACE, // { }
  hl_TOKEN_LBRACKET, hl_TOKEN_RBRACKET, // [ ]
  hl_TOKEN_RIGHT_ARROW, // =>
  hl_TOKEN_COMMA, hl_TOKEN_DOT, hl_TOKEN_SEMICOLON, hl_TOKEN_COLON, // , . ; :
  hl_TOKEN_MINUS, hl_TOKEN_PLUS, hl_TOKEN_STAR, hl_TOKEN_SLASH, // - + * /
  hl_TOKEN_STAR_STAR, hl_TOKEN_PERCENT, // ** %
  hl_TOKEN_DOT_DOT, // ..
  
  hl_TOKEN_PLUS_EQUAL, hl_TOKEN_MINUS_EQUAL, // += -=
  hl_TOKEN_STAR_EQUAL, hl_TOKEN_SLASH_EQUAL, // *= /=
  hl_TOKEN_STAR_STAR_EQUAL, hl_TOKEN_PERCENT_EQUAL, // **= %=
  hl_TOKEN_DOT_DOT_EQUAL, // ..=

  // Conditions
  hl_TOKEN_EQUAL, hl_TOKEN_EQUAL_EQUAL, // = ==
  hl_TOKEN_BANG, hl_TOKEN_BANG_EQUAL, // ! !=
  hl_TOKEN_LESS, hl_TOKEN_LESS_EQUAL, // < <=
  hl_TOKEN_GREATER, hl_TOKEN_GREATER_EQUAL, // > >=
  hl_TOKEN_AMP_AMP, hl_TOKEN_PIPE_PIPE, // && ||

  // Keywords
  hl_TOKEN_VAR, // var
  hl_TOKEN_WHILE, hl_TOKEN_FOR, hl_TOKEN_LOOP, // while for loop
  hl_TOKEN_CONTINUE, hl_TOKEN_BREAK, hl_TOKEN_RETURN, // continue break return
  hl_TOKEN_IF, hl_TOKEN_ELSE, hl_TOKEN_MATCH, hl_TOKEN_CASE, // if else match case
  hl_TOKEN_STRUCT, hl_TOKEN_SELF, hl_TOKEN_FUNC, hl_TOKEN_STATIC, // struct self func static
  hl_TOKEN_ENUM, // enum

  // Built-in Types
  hl_TOKEN_NUMBER_TYPE, hl_TOKEN_STRING_TYPE, // Number String
  hl_TOKEN_BOOL_TYPE, // Bool

  // literals
  hl_TOKEN_TRUE, hl_TOKEN_FALSE, hl_TOKEN_NIL, // true false nil
  hl_TOKEN_IDENTIFIER, hl_TOKEN_STRING, hl_TOKEN_NUMBER,

  hl_TOKEN_ERROR, hl_TOKEN_EOF,
};

struct hl_Token {
  enum hl_TokenType type;
  const char* start;
  s32 length;
  s32 line;
};

void hl_initTokenizer(const char* source);
struct hl_Token hl_nextToken();

#endif // _HOBBYL_TOKENIZER_H

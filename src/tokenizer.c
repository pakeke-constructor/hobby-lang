#include "tokenizer.h"

#include <stdio.h>
#include <string.h>

#include "common.h"

struct Tokenizer {
  const char* start;
  const char* current;
  s32 line;
};

struct Tokenizer tokenizer;

void hl_initTokenizer(const char* source) {
  tokenizer.start = source;
  tokenizer.current = source;
  tokenizer.line = 1;
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z')
      || (c >= 'A' && c <= 'Z')
      ||  c == '_';
}

static bool isAtEnd() {
  return *tokenizer.current == '\0';
}

static char advance() {
  tokenizer.current++;
  return tokenizer.current[-1];
}

static char peek() {
  return *tokenizer.current;
}

static char peekNext() {
  if (isAtEnd()) {
    return '\0';
  }
  return *(tokenizer.current + 1);
}

static bool match(char expected) {
  if (isAtEnd()) {
    return false;
  }
  if (*tokenizer.current != expected) {
    return false;
  }
  tokenizer.current++;
  return true;
}

static struct hl_Token makeToken(enum hl_TokenType type) {
  struct hl_Token token;
  token.type = type;
  token.start = tokenizer.start;
  token.length = (s32)(tokenizer.current - tokenizer.start);
  token.line = tokenizer.line;
  return token;
}

static struct hl_Token errorToken(const char* message) {
  struct hl_Token token;
  token.type = hl_TOKEN_ERROR;
  token.start = message;
  token.length = (s32)strlen(message);
  token.line = tokenizer.line;
  return token;
}

static void skipWhitespace() {
  while (true) {
    char c = peek();
    switch (c) {
      case '\n':
        tokenizer.line++;
        hl_FALLTHROUGH;
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '/':
        if (peekNext() == '/') {
          while (peek() != '\n' && !isAtEnd()) {
            advance();
          }
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static enum hl_TokenType checkKeyword(
    s32 start, s32 length, const char* rest, enum hl_TokenType type) {
  if (tokenizer.current - tokenizer.start == start + length &&
      memcmp(tokenizer.start + start, rest, length) == 0) {
    return type;
  }

  return hl_TOKEN_IDENTIFIER;
}

static enum hl_TokenType identifierType() {
  switch (*tokenizer.start) {
    case 'v': return checkKeyword(1, 2, "ar", hl_TOKEN_VAR);
    case 'b': return checkKeyword(1, 4, "reak", hl_TOKEN_BREAK);
    case 'c': {
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 'a': return checkKeyword(2, 2, "se", hl_TOKEN_CASE);
          case 'o': return checkKeyword(2, 6, "ntinue", hl_TOKEN_CONTINUE);
        }
      }
      break;
    }
    case 'w': return checkKeyword(1, 4, "hile", hl_TOKEN_WHILE);
    case 'f': {
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 'a': return checkKeyword(2, 3, "lse", hl_TOKEN_FALSE);
          case 'o': return checkKeyword(2, 2, "or", hl_TOKEN_FOR);
          case 'u': return checkKeyword(2, 2, "nc", hl_TOKEN_FUNC);
        }
      }
      break;
    }
    case 'l': return checkKeyword(1, 3, "oop", hl_TOKEN_LOOP);
    case 'i': return checkKeyword(1, 1, "f", hl_TOKEN_IF);
    case 'e':
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 'l': return checkKeyword(2, 2, "se", hl_TOKEN_ELSE);
          case 'n': return checkKeyword(2, 2, "um", hl_TOKEN_ENUM);
        }
      }
      break;
    case 'm': return checkKeyword(1, 4, "atch", hl_TOKEN_MATCH);
    case 's': {
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 't': {
            if (tokenizer.current - tokenizer.start > 1) {
              switch (*(tokenizer.start + 2)) {
                case 'a': return checkKeyword(3, 3, "tic", hl_TOKEN_STATIC);
                case 'r': return checkKeyword(3, 3, "uct", hl_TOKEN_STRUCT);
              }
            }
            break;
          }
          case 'e': return checkKeyword(2, 2, "lf", hl_TOKEN_SELF);
        }
      }
      break;
    }
    case 't': return checkKeyword(1, 3, "rue", hl_TOKEN_TRUE);
    case 'n': return checkKeyword(1, 2, "il", hl_TOKEN_NIL);
    case 'r': return checkKeyword(1, 5, "eturn", hl_TOKEN_RETURN);
    case 'N': return checkKeyword(1, 5, "umber", hl_TOKEN_NUMBER_TYPE);
    case 'S': return checkKeyword(1, 5, "tring", hl_TOKEN_STRING_TYPE);
    case 'B': return checkKeyword(1, 3, "ool", hl_TOKEN_BOOL_TYPE);
  }

  return hl_TOKEN_IDENTIFIER;
}

static struct hl_Token identifierOrKeyword() {
  while (isAlpha(peek()) || isDigit(peek())) {
    advance();
  }

  return makeToken(identifierType());
}

static struct hl_Token number() {
  while (isDigit(peek())) {
    advance();
  }

  if (peek() == '.' && isDigit(peekNext())) {
    advance();

    while (isDigit(peek())) {
      advance();
    }
  }

  return makeToken(hl_TOKEN_NUMBER);
}

static struct hl_Token string(char terminator) {
  while (peek() != terminator && !isAtEnd()) {
    if (peek() == '\n') {
      tokenizer.line++;
    }
    advance();
  }

  if (isAtEnd()) {
    return errorToken("Unterminated string.");
  }

  advance();
  return makeToken(hl_TOKEN_STRING);
}

struct hl_Token hl_nextToken() {
  skipWhitespace();
  tokenizer.start = tokenizer.current;

  if (isAtEnd()) {
    return makeToken(hl_TOKEN_EOF);
  }

  char c = advance();

  if (isAlpha(c)) {
    return identifierOrKeyword();
  }
  if (isDigit(c)) {
    return number();
  }

  switch (c) {
    case '(': return makeToken(hl_TOKEN_LPAREN);
    case ')': return makeToken(hl_TOKEN_RPAREN);
    case '{': return makeToken(hl_TOKEN_LBRACE);
    case '}': return makeToken(hl_TOKEN_RBRACE);
    case '[': return makeToken(hl_TOKEN_LBRACKET);
    case ']': return makeToken(hl_TOKEN_RBRACKET);
    case ';': return makeToken(hl_TOKEN_SEMICOLON);
    case ',': return makeToken(hl_TOKEN_COMMA);
    case '.': {
      if (match('.')) { // Concat operator
        return makeToken(match('=') ? hl_TOKEN_DOT_DOT_EQUAL : hl_TOKEN_DOT_DOT);
      }
      return makeToken(hl_TOKEN_DOT);
    }
    case ':': return makeToken(hl_TOKEN_COLON);
    case '+': return makeToken(match('=') ? hl_TOKEN_PLUS_EQUAL : hl_TOKEN_PLUS);
    case '-': return makeToken(match('=') ? hl_TOKEN_MINUS_EQUAL : hl_TOKEN_MINUS);
    case '*': {
      if (match('*')) { // Pow operator
        return makeToken(match('=') ? hl_TOKEN_STAR_STAR_EQUAL : hl_TOKEN_STAR_STAR);
      }
      return makeToken(match('=') ? hl_TOKEN_STAR_EQUAL : hl_TOKEN_STAR);
    }
    case '/': return makeToken(match('=') ? hl_TOKEN_SLASH_EQUAL : hl_TOKEN_SLASH);
    case '%': return makeToken(match('=') ? hl_TOKEN_PERCENT_EQUAL : hl_TOKEN_PERCENT);
    case '&': return match('&')
        ? makeToken(hl_TOKEN_AMP_AMP)
        : errorToken("Did you mean '&&'? Bitwise operators not supported.");
    case '|': return match('|')
        ? makeToken(hl_TOKEN_PIPE_PIPE)
        : errorToken("Did you mean '||'? Bitwise operators not supported.");
    case '!': return makeToken(match('=') ? hl_TOKEN_BANG_EQUAL : hl_TOKEN_BANG);
    case '=': {
      if (match('>')) {
        return makeToken(hl_TOKEN_RIGHT_ARROW);
      }
      return makeToken(match('=') ? hl_TOKEN_EQUAL_EQUAL : hl_TOKEN_EQUAL);
    }
    case '>': return makeToken(match('=') ? hl_TOKEN_GREATER_EQUAL : hl_TOKEN_GREATER);
    case '<': return makeToken(match('=') ? hl_TOKEN_LESS_EQUAL : hl_TOKEN_LESS);
    case '\'':
    case '"': return string(c);
  }

  return errorToken("Unexpected character.");
}

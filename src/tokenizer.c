#include "tokenizer.h"

#include <string.h>

#include "common.h"

struct Tokenizer {
  const char* start;
  const char* current;
  s32 line;
};

struct Tokenizer tokenizer;

void initTokenizer(const char* source) {
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

static struct Token makeToken(enum TokenType type) {
  struct Token token;
  token.type = type;
  token.start = tokenizer.start;
  token.length = (s32)(tokenizer.current - tokenizer.start);
  token.line = tokenizer.line;
  return token;
}

static struct Token errorToken(const char* message) {
  struct Token token;
  token.type = TOKEN_ERROR;
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
        FALLTHROUGH;
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

static enum TokenType checkKeyword(
    s32 start, s32 length, const char* rest, enum TokenType type) {
  if (tokenizer.current - tokenizer.start == start + length &&
      memcmp(tokenizer.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

static enum TokenType identifierType() {
  switch (*tokenizer.start) {
    case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
    case 'c': {
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 'a': return checkKeyword(2, 2, "se", TOKEN_CASE);
          case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
        }
      }
      break;
    }
    case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    case 'f': {
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o': return checkKeyword(2, 2, "or", TOKEN_FOR);
          case 'u': return checkKeyword(2, 2, "nc", TOKEN_FUNC);
        }
      }
      break;
    }
    case 'l': return checkKeyword(1, 3, "oop", TOKEN_LOOP);
    case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'e':
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 'l': return checkKeyword(2, 2, "se", TOKEN_ELSE);
          case 'n': return checkKeyword(2, 2, "um", TOKEN_ENUM);
        }
      }
      break;
    case 'm': return checkKeyword(1, 4, "atch", TOKEN_MATCH);
    case 's': {
      if (tokenizer.current - tokenizer.start > 1) {
        switch (*(tokenizer.start + 1)) {
          case 't': {
            if (tokenizer.current - tokenizer.start > 1) {
              switch (*(tokenizer.start + 2)) {
                case 'a': return checkKeyword(3, 3, "tic", TOKEN_STATIC);
                case 'r': return checkKeyword(3, 3, "uct", TOKEN_STRUCT);
              }
            }
            break;
          }
          case 'e': return checkKeyword(2, 2, "lf", TOKEN_SELF);
        }
      }
      break;
    }
    case 't': return checkKeyword(1, 3, "rue", TOKEN_TRUE);
    case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
  }

  return TOKEN_IDENTIFIER;
}

static struct Token identifierOrKeyword() {
  while (isAlpha(peek()) || isDigit(peek())) {
    advance();
  }

  return makeToken(identifierType());
}

static struct Token number() {
  while (isDigit(peek())) {
    advance();
  }

  if (peek() == '.' && isDigit(peekNext())) {
    advance();

    while (isDigit(peek())) {
      advance();
    }
  }

  return makeToken(TOKEN_NUMBER);
}

static struct Token string(char terminator) {
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
  return makeToken(TOKEN_STRING);
}

struct Token nextToken() {
  skipWhitespace();
  tokenizer.start = tokenizer.current;

  if (isAtEnd()) {
    return makeToken(TOKEN_EOF);
  }

  char c = advance();

  if (isAlpha(c)) {
    return identifierOrKeyword();
  }
  if (isDigit(c)) {
    return number();
  }

  switch (c) {
    case '(': return makeToken(TOKEN_LPAREN);
    case ')': return makeToken(TOKEN_RPAREN);
    case '{': return makeToken(TOKEN_LBRACE);
    case '}': return makeToken(TOKEN_RBRACE);
    case '[': return makeToken(TOKEN_LBRACKET);
    case ']': return makeToken(TOKEN_RBRACKET);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': {
      if (match('.')) { // Concat operator
        return makeToken(match('=') ? TOKEN_DOT_DOT_EQUAL : TOKEN_DOT_DOT);
      }
      return makeToken(TOKEN_DOT);
    }
    case ':': return makeToken(TOKEN_COLON);
    case '+': return makeToken(match('=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
    case '-': return makeToken(match('=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
    case '*': {
      if (match('*')) { // Pow operator
        return makeToken(match('=') ? TOKEN_STAR_STAR_EQUAL : TOKEN_STAR_STAR);
      }
      return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
    }
    case '/': return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
    case '%': return makeToken(match('=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);
    case '&': return match('&')
        ? makeToken(TOKEN_AMP_AMP)
        : errorToken("Did you mean '&&'? Bitwise operators not supported.");
    case '|': return match('|')
        ? makeToken(TOKEN_PIPE_PIPE)
        : errorToken("Did you mean '||'? Bitwise operators not supported.");
    case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=': {
      if (match('>')) {
        return makeToken(TOKEN_RIGHT_ARROW);
      }
      return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    }
    case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '\'':
    case '"': return string(c);
  }

  return errorToken("Unexpected character.");
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "vm.h"

static void repl(struct State* H) {
  char line[1024];

  while (true) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(H, line);
  }
}

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(1);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(1);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(1);
  }
  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

static void runFile(struct State* H, const char* path) {
  char* source = readFile(path);
  enum InterpretResult result = interpret(H, source);
  free(source);

  if (result != INTERPRET_OK) {
    exit(1);
  }
}

s32 main(s32 argc, const char* args[]) {
  struct State H;
  initState(&H);

  if (argc == 1) {
    repl(&H);
  } else if (argc == 2) {
    runFile(&H, args[1]);
  } else {
    fprintf(stderr, "Usage: %s [path]\n", args[0]);
    exit(1);
  }

  freeState(&H);
  return 0;
}


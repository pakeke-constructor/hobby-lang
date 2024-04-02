CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror
CFLAGS += -Isrc
LDFLAGS = -lm

RM = rm
RMDIR = rm -r

ifndef PROFILE
	PROFILE = debug
endif

ifeq ($(PROFILE), debug)
	CFLAGS += -O0 -g -fsanitize=address
endif

ifeq ($(PROFILE), release)
	CFLAGS += -O3
endif

BUILD = bin

SRC = src/main.c src/chunk.c src/memory.c src/debug.c src/value.c src/vm.c \
			src/compiler.c src/tokenizer.c src/object.c src/table.c

OBJ = $(SRC:%.c=$(BUILD)/%_$(PROFILE).o)

DEPENDS = $(OBJ:.o=.d)
EXE = $(BUILD)/hl_$(PROFILE)

.PHONY: clean compile_flags

$(EXE): $(OBJ)
	@mkdir -p $(BUILD)
	@echo "Compiling $(EXE)..."
	@$(CC) -o $(EXE) $(OBJ) $(CFLAGS) $(LDFLAGS)
	@echo "Compile args: $(CC) $(CFLAGS)"

$(BUILD)/%_$(PROFILE).o: %.c
	@mkdir -p $(@D)
	@echo "Compiling $< -> $@..."
	@$(CC) -o $@ -c $< $(CFLAGS) -MMD -MP

clean:
	$(RMDIR) $(BUILD)

compile_flags:
	@echo "" > compile_flags.txt
	@$(foreach flag,$(CFLAGS),echo $(flag) >> compile_flags.txt;)

-include $(DEPENDS)
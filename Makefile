CC      = gcc
MRON_VERSION ?= 0.1.0
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 -Isrc -DMRON_VERSION='"$(MRON_VERSION)"'
LDFLAGS =

SHARED_SRC = src/util.c src/ast.c src/ast_ops.c src/lexer.c src/parser.c \
             src/json_emit.c src/mron_emit.c src/json_parse.c \
             src/yaml_emit.c src/toml_emit.c
SHARED_OBJ = $(SHARED_SRC:.c=.o)

SRC = $(SHARED_SRC) src/main.c
OBJ = $(SRC:.c=.o)
TARGET = mronc

LSP_SRC = src/lsp_transport.c src/lsp_json.c src/lsp_server.c src/lsp_features.c src/lsp_main.c
LSP_OBJ = $(LSP_SRC:.c=.o)
LSP_TARGET = mron-lsp

ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
    LSP_TARGET := $(LSP_TARGET).exe
    RMDIR = rmdir /S /Q
else
    RM = rm -f
    RMDIR = rm -rf
endif

.PHONY: all clean test release lsp

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

release: clean
	$(MAKE) CFLAGS="-O3 -flto=auto -DNDEBUG -s -Wall -Wextra -Wpedantic -std=c99 -Isrc -DMRON_VERSION='\"$(MRON_VERSION)\"'" LDFLAGS="-O3 -flto=auto -s"

lsp: $(LSP_TARGET)

$(LSP_TARGET): $(SHARED_OBJ) $(LSP_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
ifeq ($(OS),Windows_NT)
	-del /Q $(subst /,\,$(OBJ)) $(subst /,\,$(LSP_OBJ)) $(TARGET) $(LSP_TARGET) 2>nul
else
	$(RM) $(OBJ) $(LSP_OBJ) $(TARGET) $(LSP_TARGET)
endif

test: $(TARGET)
ifeq ($(OS),Windows_NT)
	@echo Running tests...
	@tests\run_tests.bat
else
	@echo "Running tests..."
	@bash tests/run_tests.sh
endif

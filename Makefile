CC      = gcc
MRON_VERSION ?= 0.1.0
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 -Isrc -DMRON_VERSION='"$(MRON_VERSION)"'
LDFLAGS =

SRC = src/util.c src/ast.c src/lexer.c src/parser.c \
      src/json_emit.c src/mron_emit.c src/json_parse.c src/main.c
OBJ = $(SRC:.c=.o)
TARGET = mronc

ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
    RMDIR = rmdir /S /Q
else
    RM = rm -f
    RMDIR = rm -rf
endif

.PHONY: all clean test release

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

release: clean
	$(MAKE) CFLAGS="-O3 -flto -DNDEBUG -s -Wall -Wextra -Wpedantic -std=c99 -Isrc -DMRON_VERSION='\"$(MRON_VERSION)\"'" LDFLAGS="-O3 -flto -s"

clean:
ifeq ($(OS),Windows_NT)
	-del /Q $(subst /,\,$(OBJ)) $(TARGET) 2>nul
else
	$(RM) $(OBJ) $(TARGET)
endif

test: $(TARGET)
ifeq ($(OS),Windows_NT)
	@echo Running tests...
	@tests\run_tests.bat
else
	@echo "Running tests..."
	@bash tests/run_tests.sh
endif

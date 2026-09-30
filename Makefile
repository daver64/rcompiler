CC       := gcc
CFLAGS   := -Wall
SRC_DIR  := src
SOURCES  := $(wildcard $(SRC_DIR)/*.c)
BIN      := compile

.PHONY: all test clean

all: $(BIN)

$(BIN): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

test: $(BIN)
	./tests/run_tests.sh

clean:
	rm -f $(BIN) out.asm

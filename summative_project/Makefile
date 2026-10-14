CC      := gcc
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g
LDLIBS  := -lcrypto

SRC_DIR   := src
BUILD_DIR := build
BIN       := library_chain

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean asan test run unit

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# AddressSanitizer + UndefinedBehaviorSanitizer build, used by the memory-safety tests
asan: CFLAGS += -fsanitize=address,undefined -O0
asan: clean all

run: $(BIN)
	./$(BIN)

# Unit tests for the cryptographic core (everything except main.c and cli.c)
UNIT_SRCS := $(filter-out $(SRC_DIR)/main.c $(SRC_DIR)/cli.c,$(SRCS))
unit: $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_core tests/test_core.c $(UNIT_SRCS) $(LDLIBS)

test:
	bash tests/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(BIN)

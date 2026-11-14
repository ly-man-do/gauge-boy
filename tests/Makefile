# Host unit tests. Built with the host C compiler, not devkitARM.
# Compiles every test file here plus the platform-free modules
# (source/elm327, source/obd) into one test binary.

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -pedantic -g -I../source -I.
BUILD   := build

SRCS    := $(wildcard *.c) $(wildcard ../source/elm327/*.c) $(wildcard ../source/obd/*.c)
HDRS    := $(wildcard *.h) $(wildcard ../source/*.h) $(wildcard ../source/elm327/*.h) $(wildcard ../source/obd/*.h)

.PHONY: run clean

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests

$(BUILD)/run_tests: $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	@rm -rf $(BUILD)

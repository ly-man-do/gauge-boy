# Host unit tests. Built with the host C compiler, not devkitARM.
# Compiles every test file here plus the platform-free modules
# (source/elm327, source/obd, source/data), the POSIX-portable TCP
# transport, the connection-test answers and the spike actions into one
# test binary.

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -pedantic -g -I../source -I.
BUILD   := build

SRCS    := $(wildcard *.c) $(wildcard ../source/elm327/*.c) $(wildcard ../source/obd/*.c) \
           $(wildcard ../source/data/*.c) $(wildcard ../source/fun/*.c) ../source/transport/tcp_transport.c \
           ../source/transport/conntest.c ../source/spike_actions.c \
           ../source/ui/home_layout.c ../source/ui/home_top.c ../source/ui/live_ui.c ../source/ui/diag_ui.c ../source/ui/trim_ui.c ../source/ui/track_ui.c ../source/ui/log_ui.c
HDRS    := $(wildcard *.h) $(wildcard ../source/*.h) $(wildcard ../source/elm327/*.h) $(wildcard ../source/obd/*.h) \
           $(wildcard ../source/data/*.h) $(wildcard ../source/fun/*.h) ../source/transport/tcp_transport.h \
           ../source/transport/conntest.h ../source/spike_actions.h
LDLIBS  := -pthread -lm

.PHONY: run clean emulator-check

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests

$(BUILD)/run_tests: $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

# Driver + spike actions against a running ELM327 emulator (tools/run-emulator.sh).
EMU_HOST ?= 127.0.0.1
EMU_PORT ?= 35000
EMU_PROTOCOL ?=
EMU_SRCS := ../tools/emulator_check.c $(filter ../source/%,$(SRCS))

emulator-check: $(BUILD)/emulator_check
	./$(BUILD)/emulator_check $(EMU_HOST) $(EMU_PORT) $(EMU_PROTOCOL)

$(BUILD)/emulator_check: $(EMU_SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(EMU_SRCS) $(LDLIBS)

clean:
	@rm -rf $(BUILD)

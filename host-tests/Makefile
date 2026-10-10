# SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
# SPDX-License-Identifier: GPL-3.0-only

# Host build of the firmware's Unity suites.
#
#   make -C host-tests test                             build and run
#   make -C host-tests SANITIZE=address,undefined test  same, with sanitizers
#
# Two suites run in one process:
#   ../test/*.cpp   the shared suite that the device also runs (setup()).
#   tests/*.cpp     host-only tests that need the NVS fault injection (runHostSuite()).
# Hardware-only code is not compiled. The Arduino, NVS and mbedtls APIs come
# from shim/. The HMAC shim needs the OpenSSL development headers (libssl-dev).

CXX      ?= g++
CC       ?= gcc
SANITIZE ?=
BUILD    := build
BIN      := $(BUILD)/host_tests

WARNINGS := -Wall -Wextra -Wpedantic -Werror
# -MMD -MP writes header dependencies, so editing a header rebuilds its users.
DEPFLAGS := -MMD -MP
CXXFLAGS := -std=gnu++17 -g -O1 $(WARNINGS) $(DEPFLAGS) -Ishim -Ivendor/unity -I../include -I../src
CFLAGS   := -std=c99 -g -O1 -Wall -Wextra -Werror $(DEPFLAGS) -Ivendor/unity
LDFLAGS  :=
LDLIBS   := -lcrypto

ifneq ($(SANITIZE),)
  SANFLAGS := -fsanitize=$(SANITIZE) -fno-omit-frame-pointer -fno-sanitize-recover=all
  CXXFLAGS += $(SANFLAGS)
  CFLAGS   += $(SANFLAGS)
  LDFLAGS  += $(SANFLAGS)
endif

SHARED_SRCS := $(wildcard ../test/*.cpp)
APP_SRCS    := ../src/storage/outbox_store.cpp
HOST_SRCS   := $(wildcard tests/*.cpp) main.cpp

OBJS := $(patsubst ../%.cpp,$(BUILD)/%.o,$(SHARED_SRCS) $(APP_SRCS)) \
        $(patsubst %.cpp,$(BUILD)/host/%.o,$(HOST_SRCS)) \
        $(BUILD)/vendor/unity/unity.o

# Objects depend on the compiler and sanitizer setting. Changing either one
# writes a new stamp and removes the others, so every object is rebuilt.
CONFIG_KEY   := $(shell printf '%s|%s' '$(CXX)' '$(SANITIZE)' | tr -c 'A-Za-z0-9' '_')
CONFIG_STAMP := $(BUILD)/config.$(CONFIG_KEY)

.PHONY: test build clean

build: $(BIN)

test: $(BIN)
	./$(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJS): $(CONFIG_STAMP)

$(CONFIG_STAMP):
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/config.*
	@touch $@

-include $(OBJS:.o=.d)

$(BUILD)/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/host/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vendor/unity/unity.o: vendor/unity/unity.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

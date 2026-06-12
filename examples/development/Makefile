# Build-only development examples for MrHakOS.
#
# These examples prove that C/C++ source files in the repository can be checked
# with freestanding-friendly flags. They are not linked into the kernel image and
# cannot run inside MrHakOS until user-space loading/syscalls exist.

BUILD_DIR ?= ../../bin/dev-examples
CC ?= gcc
CXX ?= g++
PYTHON ?= python3

CFLAGS := -std=c11 -ffreestanding -fno-builtin -Wall -Wextra -Werror
CXXFLAGS := -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-builtin -Wall -Wextra -Werror

C_SRC := c/hello.c
CPP_SRC := cpp/hello.cpp
PY_SRC := python/hello.py

C_OBJ := $(BUILD_DIR)/c-hello.o
CPP_OBJ := $(BUILD_DIR)/cpp-hello.o
PY_STAMP := $(BUILD_DIR)/python-hello.checked

.PHONY: all c cpp python

all: c cpp python

c: $(C_OBJ)

cpp: $(CPP_OBJ)

python: $(PY_STAMP)

$(BUILD_DIR):
	@mkdir -p $@

$(C_OBJ): $(C_SRC) | $(BUILD_DIR)
	@echo "=> Checking freestanding C example: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(CPP_OBJ): $(CPP_SRC) | $(BUILD_DIR)
	@echo "=> Checking freestanding C++ example: $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(PY_STAMP): $(PY_SRC) | $(BUILD_DIR)
	@echo "=> Syntax-checking Python roadmap source: $<"
	@if command -v $(PYTHON) >/dev/null 2>&1; then \
		PYTHONPYCACHEPREFIX=$(BUILD_DIR)/pycache $(PYTHON) -m py_compile $<; \
		printf 'ok\n' > $@; \
	else \
		echo "python3 not found; skipping Python syntax check"; \
		printf 'skipped\n' > $@; \
	fi


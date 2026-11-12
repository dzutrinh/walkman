# walkman.c on Linux, macOS and Windows.
#
#   make            walkman, from the Walkman-shaped walkman.c
#   make readable   walkman-readable, from walkman.readable.c
#   make dev        walkman-dev, the readable version with the bot (-DDEV)
#   make test       the bot plays 20 levels for 90 seconds each
#   make run        build walkman and play it
#   make shape      rewrite walkman.min.c and walkman.c (needs python3 and unifdef)
#   make clean
#
# On Windows, run it from an MSYS2 MinGW shell (UCRT64, MINGW64 or CLANG64)
# with make or mingw32-make. To build the Windows game from Linux or macOS:
#
#   make CC=x86_64-w64-mingw32-gcc

CFLAGS ?= -O2
LDLIBS  = -lm
PYTHON ?= python3

# What the compiler builds for decides the rest, so cross-compiling works too.
TARGET := $(shell $(CC) -dumpmachine)

ifneq (,$(findstring mingw,$(TARGET))$(findstring windows-gnu,$(TARGET)))
# Windows has no ioctl, stty, fork or aplay: win32/ stands in for them.
EXE     = .exe
COMPAT  = win32/compat.c
CPPFLAGS += -Iwin32
LDLIBS  += -lwinmm
else ifneq (,$(findstring msys,$(TARGET))$(findstring cygwin,$(TARGET)))
# MSYS and Cygwin have all of them; the compiler adds the .exe.
EXE     = .exe
endif

# mingw32-make outside an MSYS2 shell runs recipes in cmd.exe.
ifeq ($(SHELL),sh.exe)
RM  = del /Q
RUN =
else
RUN = ./
endif

BUILD = $(CC) $(CPPFLAGS) $(CFLAGS) $< $(COMPAT) -o $@ $(LDFLAGS) $(LDLIBS)
BINS  = walkman$(EXE) walkman-readable$(EXE) walkman-dev$(EXE)

all: walkman$(EXE)
readable: walkman-readable$(EXE)
dev: walkman-dev$(EXE)

walkman$(EXE): walkman.c $(COMPAT) $(wildcard win32/sys/*.h)
	$(BUILD)

walkman-readable$(EXE): walkman.readable.c $(COMPAT) $(wildcard win32/sys/*.h)
	$(BUILD)

walkman-dev$(EXE): walkman.readable.c dev.h $(COMPAT) $(wildcard win32/sys/*.h)
	$(BUILD) -DDEV

run: walkman$(EXE)
	$(RUN)walkman$(EXE)

# Fails unless the bot survives every level. Needs a POSIX shell.
test: walkman-dev$(EXE)
	./walkman-dev$(EXE) --test 5400 20 | tee test.txt
	grep -q '^0 of 20 seeds died' test.txt

shape:
	$(PYTHON) minify.py
	$(PYTHON) shape.py

clean:
	-$(RM) $(BINS) test.txt

.PHONY: all readable dev run test shape clean

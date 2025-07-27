# Makefile for the Global Seismic Monitor (Console Version)
#
# This Makefile is configured for native compilation on a Debian-based
# Linux system, such as Raspberry Pi OS.

# The C compiler to use.
CC = gcc

# The name of the final executable.
TARGET = monitor

# All C source files used in the project.
SRCS = main.c

# CFLAGS: Flags passed to the C compiler.
# -std=c99 is used for compatibility with Jansson.
CFLAGS = -Wall -O2 -std=c99

# LDFLAGS: Flags passed to the linker.
# We need to link libcurl for web requests and jansson for JSON parsing.
LDFLAGS = -lcurl -ljansson

# --- Build Rules ---

# The default rule, which builds the final executable.
all: $(TARGET)

# The rule to compile and link the project.
$(TARGET): $(SRCS)
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

# The rule to clean up the compiled executable.
clean:
	rm -f $(TARGET)


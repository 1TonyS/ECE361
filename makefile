# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

# Targets and source files
TARGETS = server deliver
SOURCES = server.c deliver.c

# Default target
all: $(TARGETS)

# Rules for each target
server: server.c
	$(CC) $(CFLAGS) -o server server.c

deliver: deliver.c
	$(CC) $(CFLAGS) -o deliver deliver.c

# Clean up generated files
clean:
	rm -f $(TARGETS)

# Phony targets
.PHONY: all clean

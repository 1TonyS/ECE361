# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

# Targets and source files
TARGETS = server client
SOURCES = server.c client.c

# Default target
all: $(TARGETS)

# Rules for each target
server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

# Clean up generated files
clean:
	rm -f $(TARGETS)

# Phony targets
.PHONY: all clean

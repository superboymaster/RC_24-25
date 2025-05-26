# Compiler and flags
CC = gcc
CFLAGS = -Wall
DEBUG_FLAGS = -g -DDEBUG

# Source files and target
SRC = main.c ftp.c
TARGET = download

# Default rule
all: $(TARGET)

# Rule to build the target
$(TARGET): $(SRC)
ifeq ($(DEBUG), 1)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(SRC) -o $(TARGET)
else
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
endif

# Clean rule
clean:
	rm -f $(TARGET)

# Clean and rebuild rule
rebuild: clean all

# Phony targets
.PHONY: all clean rebuild
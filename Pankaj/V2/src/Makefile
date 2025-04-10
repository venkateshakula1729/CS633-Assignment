# Compiler and flags
MPICC = mpicc
CFLAGS = -Wall -O3 -std=c99
DEBUG_FLAGS = -g -O0 -DDEBUG

# Directories
SRC_DIR = implementations
BIN_DIR = bin

# Find all implementation source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Generate binary names from source files (replacing .c with executable name)
BINS = $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(SRCS))

# Default target
all: dirs $(BINS)

# Make sure bin directory exists
dirs:
	mkdir -p $(BIN_DIR)

# Rule to build implementation binaries (each file is self-contained)
$(BIN_DIR)/%: $(SRC_DIR)/%.c
	$(MPICC) $(CFLAGS) $< -o $@ -lm

# Target for building with debug flags
debug: CFLAGS = $(DEBUG_FLAGS)
debug: all

# Clean target
clean:
	rm -f $(BINS)
	rm -rf $(BIN_DIR)

# Help target
help:
	@echo "Available targets:"
	@echo "  all     - Build all implementations in $(SRC_DIR) (default)"
	@echo "  debug   - Build all with debug flags"
	@echo "  clean   - Remove all compiled files"
	@echo "  help    - Display this help message"
	@echo ""
	@echo "Available implementations:"
	@for impl in $(notdir $(SRCS)); do \
		echo "  $${impl%.c}"; \
	done

.PHONY: all dirs debug clean help

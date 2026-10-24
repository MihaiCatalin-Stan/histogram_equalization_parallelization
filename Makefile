CC     ?= cc
STD    := -std=c11
WARN   := -Wall -Wextra
LDLIBS := -lm
SRC    := histogram_equalization.c

BIN       := histogram
DEBUG_BIN := histogram_debug

.PHONY: all debug clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(STD) -O2 -g $(WARN) $(SRC) -o $(BIN) $(LDLIBS)

debug: $(DEBUG_BIN)

$(DEBUG_BIN): $(SRC)
	$(CC) $(STD) -O0 -g $(WARN) $(SRC) -o $(DEBUG_BIN) $(LDLIBS)

clean:
	rm -f $(BIN) $(DEBUG_BIN)
	rm -rf $(BIN).dSYM $(DEBUG_BIN).dSYM

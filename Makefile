CC     ?= cc
STD    := -std=c11
WARN   := -Wall -Wextra
LDLIBS := -lm
SRC    := histogram_equalization.c
OPT_SRC := histogram_equalization_optimized.c

BIN       := histogram
DEBUG_BIN := histogram_debug
PROF_BIN  := histogram_profile
OPT_BIN   := histogram_optimized
OPT_PROF_BIN := histogram_optimized_profile

.PHONY: all debug profile optimized profile-optimized clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(STD) -O2 -g $(WARN) $(SRC) -o $(BIN) $(LDLIBS)

debug: $(DEBUG_BIN)

$(DEBUG_BIN): $(SRC)
	$(CC) $(STD) -O0 -g $(WARN) $(SRC) -o $(DEBUG_BIN) $(LDLIBS)

profile: $(PROF_BIN)

$(PROF_BIN): $(SRC)
	$(CC) $(STD) -O2 -g -DPROFILE $(WARN) $(SRC) -o $(PROF_BIN) $(LDLIBS)

optimized: $(OPT_BIN)

$(OPT_BIN): $(OPT_SRC)
	$(CC) $(STD) -O2 -g $(WARN) $(OPT_SRC) -o $(OPT_BIN) $(LDLIBS)

profile-optimized: $(OPT_PROF_BIN)

$(OPT_PROF_BIN): $(OPT_SRC)
	$(CC) $(STD) -O2 -g -DPROFILE $(WARN) $(OPT_SRC) -o $(OPT_PROF_BIN) $(LDLIBS)

clean:
	rm -f $(BIN) $(DEBUG_BIN) $(PROF_BIN) $(OPT_BIN) $(OPT_PROF_BIN)
	rm -rf $(BIN).dSYM $(DEBUG_BIN).dSYM $(PROF_BIN).dSYM $(OPT_BIN).dSYM $(OPT_PROF_BIN).dSYM

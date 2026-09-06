CC=gcc
SRC=$(wildcard ./src/*.c)
HEADERS=$(wildcard ./src/*.h)
CFLAGS=-O3 -Wall -Wextra
DFLAGS=-O0 -g -Wall -Wextra
LFLAGS=-lreadline
BUILD=build
EXE=$(BUILD)/termc
DEBUG=$(BUILD)/termc-deb

all: $(BUILD) $(EXE) $(DEBUG)

$(EXE): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $(EXE) $(SRC) $(LFLAGS)

$(DEBUG): $(SRC) $(HEADERS)
	$(CC) $(DFLAGS) -o $(DEBUG) $(SRC) $(LFLAGS)

run: $(EXE)
	./$(EXE)

debug: $(DEBUG)
	./$(DEBUG)

$(BUILD):
	mkdir -p $(BUILD)

.PHONY: clean
clean:
	rm -rf $(BUILD)

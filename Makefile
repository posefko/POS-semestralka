CC=gcc
CFLAGS=-std=c11 -O2 -Wall -Wextra -pedantic -pthread
BIN=bin

all: $(BIN)/server $(BIN)/client

$(BIN):
	mkdir -p $(BIN)

$(BIN)/server: server/server.c | $(BIN)
	$(CC) $(CFLAGS) -Icommon server/server.c -o $@

$(BIN)/client: client/client.c | $(BIN)
	$(CC) $(CFLAGS) -Icommon client/client.c -o $@

clean:
	rm -rf $(BIN)

.PHONY: all clean

CC=gcc
CFLAGS=-std=c11 -O2 -Wall -Wextra -pedantic -pthread
BIN=bin

all: server client

server: $(BIN)/server
client: $(BIN)/client

$(BIN):
	mkdir -p $(BIN)

$(BIN)/server: server/server.c server/game.c server/game.h | $(BIN)
	$(CC) $(CFLAGS) -Icommon -Iserver server/server.c server/game.c -o $@

$(BIN)/client: client/client.c | $(BIN)
	$(CC) $(CFLAGS) -Icommon client/client.c -o $@

clean:
	rm -rf $(BIN)

.PHONY: all clean server client

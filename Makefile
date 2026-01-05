CC=gcc
CFLAGS=-std=c11 -O2 -Wall -Wextra -pedantic -pthread
BIN=bin

all: server client

server: $(BIN)/server
client: $(BIN)/client

$(BIN):
	mkdir -p $(BIN)

$(BIN)/server: Server/server.c Server/game.c Server/game.h | $(BIN)
	$(CC) $(CFLAGS) -ICommon -IServer Server/server.c Server/game.c -o $@

$(BIN)/client: Client/client.c | $(BIN)
	$(CC) $(CFLAGS) -ICommon Client/client.c -o $@

clean:
	rm -rf $(BIN)

.PHONY: all clean server client

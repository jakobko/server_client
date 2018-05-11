CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic -std=c99
BINARIES=server, client

all:
	$(CC) $(CFLAGS) server.c -o server
	$(CC) $(CFLAGS) client.c -o client

server: server.c
	$(CC) $(CFLAGS) $^ -o $@

client: client.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(BINARIES)

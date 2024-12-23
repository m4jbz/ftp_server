CC=gcc
CFLAGS=-Wall -Wextra

server: server.c
	$(CC) -o server server.c $(CFLAGS)

client: client.c
	$(CC) -o client client.c $(CFLAGS)

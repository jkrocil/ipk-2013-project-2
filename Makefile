CC = gcc
CFLAGS = -Wall -std=gnu99 -O3 -pthread

ALL: build

build: clean
	$(CC) $(CFLAGS) -o client client.c
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f client server

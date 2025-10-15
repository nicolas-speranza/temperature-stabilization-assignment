.RECIPEPREFIX := >
CC = gcc
CFLAGS = -O2 -Wall -Wextra -pedantic
LDFLAGS =

all: server client

server: server.c
> $(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
> $(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
> rm -f server client

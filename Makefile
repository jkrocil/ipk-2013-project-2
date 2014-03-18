CC = gcc
CFLAGS = -Wall -std=gnu99 -g -pthread
EXECS = client server

define \n


endef

ALL: build

build: clean
	$(foreach exec,$(EXECS), $(CC) $(CFLAGS) -o $(exec) $(exec).c$(\n))

clean:
	$(foreach exec,$(EXECS),rm -f $(exec)$(\n))

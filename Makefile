CC = gcc
CFLAGS = -Wall -std=c99 -g
EXECS = client server

define \n


endef

ALL: build 

build: clean
	$(foreach exec,$(EXECS), $(CC) $(CFLAGS) -o $(exec) $(exec).c$(\n))

clean:
	$(foreach exec,$(EXECS),rm -rf $(exec)$(\n))

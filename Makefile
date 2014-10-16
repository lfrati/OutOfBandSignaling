CC = gcc
CFLAGS = -Wall -pedantic -O2# aggiungiamo un pizzico di performance
DEBUG = -DDEBUG
LDFLAGS = -pthread
TARGETS = my_client my_server my_supervisor

.PHONY: all clean test

.DEFAULT: all

all: $(TARGETS)

clean:
	rm -f $(TARGETS) *.o client_log supervisor_log

test: all
	@./test.sh 10 5 8 20

my_%: my_%.c utility.o
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -o $@

utility.o: utility.c utility.h
	$(CC) $< $(CFLAGS) -c
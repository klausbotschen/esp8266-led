CC = gcc
CCFLAGS = -Wall -g -std=c99 -pedantic
DEFS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L

LDFLAGS= -pthread
LIBS = -lrt -lm

.PHONY : all clean 

all: sender

sender: sender.o adafruit.o patterns.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CCFLAGS) $(DEFS) -c -o $@ $<

clean:
	rm -f sender
	rm -f *.o
	rm -f core

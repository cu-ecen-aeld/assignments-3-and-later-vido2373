ifeq ($(CC),)
	CC = $(CROSS_COMPILE)gcc
endif

ifeq ($(CFLAGS),)
	CFLAGS = -g -Wall -Werror
endif

ifeq ($(LDFLAGS),)
	LDFLAGS = -lrt -pthread
endif

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -c -o aesdsocket.o aesdsocket.c $(LDFLAGS)
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.o $(LDFLAGS)

clean:
	rm -f aesdsocket *.o

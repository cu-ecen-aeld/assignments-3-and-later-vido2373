CROSS_COMPILE=
CC=gcc
CFLAGS = -g -Wall -Werror

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	${CROSS_COMPILE}${CC} -c -o aesdsocket.o aesdsocket.c ${CFLAGS}
	${CROSS_COMPILE}${CC} -o aesdsocket aesdsocket.o

clean:
	rm -f aesdsocket *.o

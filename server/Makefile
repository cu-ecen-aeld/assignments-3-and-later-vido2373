CROSS_COMPILE=
CC=gcc
CFLAGS = -g -Wall -Werror

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	${CROSS_COMPILE}${CC} aesdsocket.c -o aesdsocket ${CFLAGS}

clean:
	rm aesdsocket

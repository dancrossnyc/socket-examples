PROGS=		mpaccept sdpass selecty

CC=		cc
CFLAGS=		-g -Werror -Wall -Wextra -pedantic -std=c11

all:		${PROGS}

clean:
		rm -f ${PROGS}

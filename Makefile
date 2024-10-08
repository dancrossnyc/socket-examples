PROGS=		mpaccept sdpass

CC=		cc
CFLAGS=		-g -Werror -Wall -Wextra -pedantic -std=c2x

all:		${PROGS}

clean:
		rm -f ${PROGS}

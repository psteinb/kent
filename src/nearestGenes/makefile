include /afs/cs.stanford.edu/u/cmclean/code/kent/use_kent.mk

CC  = gcc
CC2 = g++
COPT= -O2 -Wall  #-ggdb

O = nearestGenes.o

CFLAGS = ${COPT} ${INCS}

nearestGenes: $O
	${CC} ${COPT} -o $@ $O ${LIBS}

nearestGenes.o: nearestGenes.c

clean:
	rm -f *.o nearestGenes

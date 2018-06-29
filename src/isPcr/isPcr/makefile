kentSrc = ../..
include ../../inc/common.mk

L = ${MYSQLLIBS}
MYLIBDIR = ../../lib/${MACHTYPE}
MYLIBS =  ${MYLIBDIR}/jkOwnLib.a ${MYLIBDIR}/jkweb.a ${kentSrc}/htslib/libhts.a

O = isPcr.o

isPcr: $O ${MYLIBS}
	${CC} ${COPT} -o ${DESTDIR}${BINDIR}/isPcr $O ${MYLIBS} $L
	${STRIP} ${DESTDIR}${BINDIR}/isPcr${EXE}

clean:
	rm -f $O

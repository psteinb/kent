kentSrc = ../../../..
include ../../../../inc/common.mk

L = ${kentSrc}/htslib/libhts.a ${MYSQLLIBS}
MYLIBDIR = ../../../../lib/${MACHTYPE}
MYLIBS =  ${MYLIBDIR}/jkhgap.a ${MYLIBDIR}/jkweb.a

A = encode2Manifest
O = encode2Manifest.o

encode2Manifest: ${O} ${MYLIBS}
	${CC} ${COPT} -o ${DESTDIR}${BINDIR}/${A}${EXE} $O ${MYLIBS} $L
	${STRIP} ${DESTDIR}${BINDIR}/${A}${EXE}

compile:: ${O}
	${CC} ${COPT} -o ${A}${EXE} ${O} ${MYLIBS} $L

clean::
	rm -f ${A}${EXE} ${O}

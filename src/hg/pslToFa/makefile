kentSrc = ../..
include ../../inc/common.mk

L = ${kentSrc}/htslib/libhts.a ${MYSQLLIBS}
MYLIBDIR = ../../lib/${MACHTYPE}
MYLIBS =  $(MYLIBDIR)/jkhgap.a $(MYLIBDIR)/jkweb.a

O = pslToFa.o
CFLAGS=-ggdb

pslToFa: $O $(MYLIBS)
	${CC} ${COPT} ${CFLAGS} -o ${BINDIR}/pslToFa $O $(MYLIBS) $L
#	${STRIP} ${BINDIR}/pslToFa${EXE}

clean::
	rm -f ${O}

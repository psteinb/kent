kentSrc = ../../..
include ../../../inc/common.mk

L = ${kentSrc}/htslib/libhts.a ${MYSQLLIBS}
MYLIBDIR = ../../../lib/${MACHTYPE}
MYLIBS =  $(MYLIBDIR)/jkOwnLib.a $(MYLIBDIR)/jkhgap.a $(MYLIBDIR)/jkweb.a

A1 = kgPrepBestMrna2
O1 = ${A1}.o

A2 = kgResultBestMrna2
O2 = ${A2}.o

all::
	${MAKE} ${A1}
	${MAKE} ${A2}

${A1}: ${O1} ${MYLIBS}
	${CC} ${COPT} ${HG_WARN} ${HG_DEFS} \
		-o ${BINDIR}/${A1} ${O1} ${MYLIBS} $L
	${STRIP} ${BINDIR}/${A1}${EXE}

${A2}: ${O2} ${MYLIBS}
	${CC} ${COPT} ${HG_WARN} ${HG_DEFS} \
		-o ${BINDIR}/${A2} ${O2} ${MYLIBS} $L
	${STRIP} ${BINDIR}/${A2}${EXE}

compile: ${O1} ${O2}
	${CC} -Wall ${O1} ${MYLIBS} ${L}
	mv ${AOUT} ${A1}
	chmod a+rx ${A1}
	${CC} ${O2} ${MYLIBS} ${L}
	mv ${AOUT} ${A2}
	chmod a+rx ${A2}

clean::
	rm -f ${O1} ${A1} ${O2} ${A2}


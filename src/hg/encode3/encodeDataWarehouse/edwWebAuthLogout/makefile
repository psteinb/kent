kentSrc = ../../../..
include $(kentSrc)/inc/common.mk

L = ${kentSrc}/htslib/libhts.a ${MYSQLLIBS}
MYLIBDIR = $(kentSrc)/lib/${MACHTYPE}
MYLIBS =  ../lib/$(MACHTYPE)/edwlib.a ${MYLIBDIR}/jkhgap.a ${MYLIBDIR}/jkweb.a

A = edwWebAuthLogout

O = $A.o 

include $(kentSrc)/inc/cgi_build_rules.mk

compile: $O 
	${CC} $O ${MYLIBS} ${L} 
	mv ${AOUT} $A${EXE}

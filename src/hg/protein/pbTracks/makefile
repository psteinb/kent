kentSrc = ../../..
include ../../../inc/common.mk

L = ${kentSrc}/htslib/libhts.a ${MYSQLLIBS} 
MYLIBDIR = ../../../lib/${MACHTYPE}
MYLIBS = ${MYLIBDIR}/jkpbap.a ${MYLIBDIR}/jkhgap.a ${MYLIBDIR}/jkweb.a

A = pbTracks

O = $A.o

include ../../../inc/cgi_build_rules.mk

lib::
	cd ../../../lib && ${MAKE}

compile: $O 
	${CC} -o $A $O ${MYLIBS} ${L}

mgsc: strip
	mv $A ${CGI_BIN}/$A

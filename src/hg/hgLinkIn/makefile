kentSrc = ../..
include ../../inc/common.mk

L = ${kentSrc}/htslib/libhts.a ${MYSQLLIBS}
MYLIBDIR = ../../lib/${MACHTYPE}
MYLIBS =  $(MYLIBDIR)/jkhgap.a ${MYLIBDIR}/jkweb.a ../../htslib/libhts.a $(MYSQLLIBS)

A = hgLinkIn
O = hgLinkIn.o handlerList.o linkInHandlers.o

A = hgLinkIn

include ../../inc/cgi_build_rules.mk

compile:: $O
	${CC} $O ${MYLIBS} #${L}
	mv ${AOUT} $A${EXE}


kentSrc = ../..
include ../../inc/common.mk

L = ../../htslib/libhts.a $(MYSQLLIBS)# -lz -lm

MYLIBDIR = ../../lib/$(MACHTYPE)
MYLIBS =  $(MYLIBDIR)/jkhgap.a $(MYLIBDIR)/jkweb.a 

O = affyPairsToSample normalizeSampleFile avgTranscriptomeExps maxTranscriptomeExps scaleSampleFiles averageZoomLevels \
	groupSamples

%: %.c 
	${CC} ${COPT} ${CFLAGS} -Wimplicit  -I../inc -I../../inc  -o ${DESTDIR}${BINDIR}/$@ $(addsuffix .c,$@) $(MYLIBS) ${L}

all : $(O)

clean::
	rm -f ${O}

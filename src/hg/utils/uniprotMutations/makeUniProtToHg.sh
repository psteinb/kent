#!/bin/bash
# create mapping from uniProt protein positions to genome positions

# *ALL* was stolen from Markd's script LS-SNP pipeline
# file: snpProtein/build/Makefile

if [ "$1" == "" ]; then
        echo Please specify a db
        exit 0
fi
hgDb=$1

# if you change BLASTDIR, also must change the cluster script mapUniprot_doBlast
BLASTDIR=/cluster/bin/blast/x86_64/blast-2.2.16/bin

# this was created by the uniprot parser as part of the publications source code
UNIPROTFAGZ=/hive/data/inside/pubs/parsedDbs/uniprot.9606.fa.gz

CLUSTER=ku
# stop on errors
set -e
# show commands
set -x 

rm -rf upMap/work/*
mkdir -p upMap/work

# get data
zcat ${UNIPROTFAGZ} > upMap/work/uniProt.fa
hgsql -Ne 'select name, chrom, strand, txStart, txEnd, cdsStart, cdsEnd, exonCount, exonStarts, exonEnds from knownGene where cdsStart < cdsEnd' $hgDb  > upMap/work/ucscGene.gp
genePredToFakePsl $hgDb upMap/work/ucscGene.gp upMap/work/ucscMRna.psl upMap/work/ucscMRna.cds
hgsql -Ne 'select spID,kgID from kgXref where spID!=""' $hgDb > upMap/work/ucscUniProt.pairs
getRnaPred $hgDb upMap/work/ucscGene.gp all upMap/work/ucscMRna.fa

# setup blast uniprot -> ucsc genes
mkdir upMap/work/queries 
mkdir upMap/work/aligns 
faSplit about upMap/work/uniProt.fa 2500 upMap/work/queries/
${BLASTDIR}/formatdb -i upMap/work/ucscMRna.fa -p F

# create joblist and run
set +x # quiet for now
>upMap/work/jobList
for i in upMap/work/queries/*.fa; do
        echo "../../mapUniprot_doBlast ucscMRna.fa queries/`basename $i` {check out exists aligns/`basename $i .fa`.psl}" >> upMap/work/jobList
done; 
set -x
ssh $CLUSTER "cd `pwd`/upMap/work && para make jobList"

# sort, pick the best alignments for each protein and then pslMap through them
find upMap/work/aligns -name '*.psl' | xargs cat | pslSelect -qtPairs=upMap/work/ucscUniProt.pairs stdin stdout | sort -k 14,14 -k 16,16n -k 17,17n > upMap/work/uniProtVsUcscMRna.psl
pslMap upMap/work/uniProtVsUcscMRna.psl upMap/work/ucscMRna.psl upMap/work/uniProtVsGenome.psl
sort -k10,10 upMap/work/uniProtVsGenome.psl | pslCDnaFilter stdin -globalNearBest=0  -bestOverlap -filterWeirdOverlapped stdout | sort | uniq > ./uniProtTo$hgDb.psl

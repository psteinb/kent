#!/bin/sh -ex

PATH=/cluster/bin/x86_64:$PATH
EMAIL="braney@soe.ucsc.edu"
WORKDIR="/hive/data/outside/otto/isca"

cd $WORKDIR
./checkISCA.sh $WORKDIR 2>&1 |  mail -s "ISCA Build" $EMAIL

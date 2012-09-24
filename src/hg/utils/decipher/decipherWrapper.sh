#!/bin/sh -e

PATH=/cluster/bin/x86_64:$PATH
EMAIL="braney@soe.ucsc.edu"
WORKDIR="/hive/data/outside/otto/decipher"

cd $WORKDIR
./checkDecipher.sh $WORKDIR 2>&1 |  mail -s "DECIPHER Build" $EMAIL

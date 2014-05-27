/* genePredToFakePsl - create fake .psl of mRNA aligned to dna from genePred file or table. */

/* Copyright (C) 2013 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */
#include "common.h"
#include "options.h"
#include "portable.h"
#include "hash.h"
#include "hdb.h"
#include "genePred.h"
#include "genePredReader.h"
#include "psl.h"


/* Command line switches. */
char *chromSizes = NULL;  /* read chrom sizes from file instead of database . */

/* Command line option specifications */
static struct optionSpec optionSpecs[] = {
    {"chromSize", OPTION_STRING},
    {NULL, 0}
};

void usage()
/* Explain usage and exit. */
{
errAbort(
  "genePredToFakePsl - Create a psl of fake-mRNA aligned to gene-preds from a file or table.\n"
  "usage:\n"
  "   genePredToFakePsl [options] db fileTbl pslOut cdsOut\n"
  "\n"
  "If fileTbl is an existing file, then it is used.\n"
  "Otherwise, the table by this name is used.\n"
  "\n"
  "pslOut specifies the fake-mRNA output psl filename.\n"
  "\n"
  "cdsOut specifies the output cds tab-separated file which contains\n"
  "genbank-style CDS records showing cdsStart..cdsEnd\n"  
  "e.g. NM_123456 34..305\n"
  "options:\n"
  "   -chromSize=sizefile\tRead chrom sizes from file instead of database\n"
  "             sizefile contains two white space separated fields per line:\n"
  "		chrom name and size\n"
  "\n");
}

static void cnvGenePredCds(struct genePred *gp, int qSize, FILE *cdsFh)
/* determine CDS and output */
{
int e, off = 0;
int qCdsStart = -1, qCdsEnd = -1;
int eCdsStart, eCdsEnd;

for (e = 0; e < gp->exonCount; ++e)
    {
    if (genePredCdsExon(gp, e, &eCdsStart, &eCdsEnd))
        {
        if (qCdsStart < 0)
            qCdsStart = off + (eCdsStart - gp->exonStarts[e]);
        qCdsEnd = off + (eCdsEnd - gp->exonStarts[e]);
        }
    off += gp->exonEnds[e] - gp->exonStarts[e];
    } 
if (gp->strand[0] == '-')
    reverseIntRange(&qCdsStart, &qCdsEnd, qSize);
fprintf(cdsFh,"%s\t%d..%d\n", gp->name, qCdsStart+1, qCdsEnd); /* genbank cds is closed 1-based */
}

static void cnvGenePred(struct hash *chromHash, struct genePred *gp, FILE *pslFh, FILE *cdsFh)
/* convert a genePred to a psl and CDS */
{
int chromSize = hashIntValDefault(chromHash, gp->chrom, 0);
if (chromSize == 0)
    errAbort("Couldn't find chromosome/scaffold '%s' in chromInfo", gp->chrom);
int e = 0, qSize=0;

for (e = 0; e < gp->exonCount; ++e)
    qSize+=(gp->exonEnds[e] - gp->exonStarts[e]);
struct psl *psl = pslNew(gp->name, qSize, 0, qSize,
                         gp->chrom, chromSize, gp->txStart, gp->txEnd,
                         gp->strand, gp->exonCount, 0);
psl->blockCount = gp->exonCount;		    
for (e = 0; e < gp->exonCount; ++e)
    {
    psl->blockSizes[e] = (gp->exonEnds[e] - gp->exonStarts[e]);
    psl->qStarts[e] = e==0 ? 0 : psl->qStarts[e-1] + psl->blockSizes[e-1];
    psl->tStarts[e] = gp->exonStarts[e];
    }
psl->match = qSize;	
psl->tNumInsert = psl->blockCount-1; 
psl->tBaseInsert = (gp->txEnd - gp->txStart) - qSize;
pslTabOut(psl, pslFh);
pslFree(&psl);
if (gp->cdsStart < gp->cdsEnd)
    cnvGenePredCds(gp, qSize, cdsFh);
}

static struct hash *getChromHash(char *db)
/* Return a hash of chrom names and sizes, from either -chromSize=file or db */
{
struct hash *chromHash = NULL;
if (chromSizes != NULL)
    chromHash = hChromSizeHashFromFile(chromSizes);
else
    chromHash = hChromSizeHash(db);
return chromHash;
}

static void fakePslFromGenePred(char *db, char *fileTbl, char *pslOut, char *cdsOut)
/* check a genePred */
{
struct genePredReader *gpr;
struct genePred *gp;
FILE *pslFh = mustOpen(pslOut, "w");
FILE *cdsFh = mustOpen(cdsOut, "w");

struct hash *chromHash = getChromHash(db);

if (fileExists(fileTbl))
    {
    gpr = genePredReaderFile(fileTbl, NULL);
    }
else
    {
    struct sqlConnection *conn = hAllocConn(db);
    gpr = genePredReaderQuery(conn, fileTbl, NULL);
    hFreeConn(&conn);
    }

while ((gp = genePredReaderNext(gpr)) != NULL)
    {
    cnvGenePred(chromHash, gp, pslFh, cdsFh);
    }
genePredReaderFree(&gpr);
carefulClose(&pslFh);
carefulClose(&cdsFh);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, optionSpecs);
chromSizes = optionVal("chromSize", NULL);
if (argc != 5)
    usage();
fakePslFromGenePred(argv[1],argv[2],argv[3],argv[4]);
return 0;
}

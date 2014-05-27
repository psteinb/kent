/* fastqStatsAndSubsample - Go through a fastq file doing sanity checks and collecting 
 * statistics,  and also producing a smaller fastq out of a sample of the data. */

/* Copyright (C) 2013 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "portable.h"
#include "obscure.h"
#include "hmmstats.h"

/* A note on randomness: This program is used on paired end data.  This data is represented
 * as two separate fastq files where the forward reads are in one file and the reverse in
 * the other.  The files are in the same order, which is how we know which forward read goes
 * with which reverse read.   As a result it is very important that this program sample
 * the same records from files that have the same number of records. 
 *
 * This is implemented in two passes - the first pass calculates the statistics and
 * produces a file with 1/10 the number of reads in it.  The second pass produces the
 * final output by downsampling the 1/10 size file if it is big enough, or the original
 * file if not. 
 *
 * Earlier versions of this program estimated the amount to reduce in the first pass
 * and were more efficient, but the estimates were based on the file sizes, and thus
 * sometimes varied when dealing with compressed input files, and this would break the
 * correspondence between read pairs, so now the estimate is always 1/10. */

int sampleSize = 100000;
int seed = 0;
boolean smallOk = FALSE;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "fastqStatsAndSubsample v2 - Go through a fastq file doing sanity checks and collecting stats\n"
  "and also producing a smaller fastq out of a sample of the data.  The fastq input may be\n"
  "compressed with gzip or bzip2.  Unfortunately the fastq input can't be in a pipe\n"
  "usage:\n"
  "   fastqStatsAndSubsample in.fastq out.stats out.fastq\n"
  "options:\n"
  "   -sampleSize=N - default %d\n"
  "   -seed=N - Use given seed for random number generator.  Default %d.\n"
  "   -smallOk - Not an error if less than sampleSize reads.  out.fastq will be entire in.fastq\n"
  , sampleSize, seed
  );
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {"sampleSize", OPTION_INT},
   {"seed", OPTION_INT},
   {"smallOk", OPTION_BOOLEAN},
   {NULL, 0},
};

static boolean nextLineMustMatchChar(struct lineFile *lf, char match, boolean noEof)
/* Get next line and make sure, other than whitespace, it matches 'match'.
 * Return FALSE on EOF, unless noEof is set, in which case abort */
{
char *line;
if (!lineFileNextReal(lf, &line))
    {
    if (noEof)
        errAbort("Expecting %c got end of file in %s", match, lf->fileName);
    else
        return FALSE;
    }
if (line[0] != match)
    errAbort("Expecting %c got %s line %d of %s", match, line, lf->lineIx, lf->fileName);
return TRUE;
}

/* A bunch of statistics gathering variables set by oneFastqRecord below. */
#define MAX_READ_SIZE 100000	/* This is fastq, right now only get 160 base reads max. */
int maxReadBases, minReadBases, readCount;
long long sumReadBases;
double sumSquaredReadBases;
int aCount[MAX_READ_SIZE], cCount[MAX_READ_SIZE], gCount[MAX_READ_SIZE], tCount[MAX_READ_SIZE];
int nCount[MAX_READ_SIZE];
double sumQuals[MAX_READ_SIZE], sumSquaredQuals[MAX_READ_SIZE];
int maxQual, minQual;

double sumDoubleArray(double *array, int arraySize)
/* Return sum of all items in array */
{
double total = 0;
int i;
for (i=0; i<arraySize; ++i)
    total += array[i];
return total;
}

long long sumIntArray(int *array, int arraySize)
/* Return sum of all items in array */
{
long long total = 0;
int i;
for (i=0; i<arraySize; ++i)
    total += array[i];
return total;
}

void printAveDoubleArray(FILE *f, char *label, double *a, long long *totalAtPos, int aSize)
/* Print a[i]/counts[i] for all elements in array */
{
fprintf(f, "%s ", label);
int i;
for (i=0; i<aSize; ++i)
    fprintf(f, "%g,", a[i]/totalAtPos[i]);
fprintf(f, "\n");
}

void printAveIntArray(FILE *f, char *label, int *a, long long *totalAtPos, int aSize)
/* Print a[i]/totalAtPos[i] for all elements in array */
{
fprintf(f, "%s ", label);
int i;
for (i=0; i<aSize; ++i)
    fprintf(f, "%g,", ((double)a[i])/totalAtPos[i]);
fprintf(f, "\n");
}

static boolean isAllSpace(char *s, int size)
/* Return TRUE if all characters in s are whitespace */
{
while (--size >= 0)
    {
    char c = *s++;
    if (!isspace(c))
        return FALSE;
    }
return TRUE;
}

boolean lineFileNextRealWithSize(struct lineFile *lf, char **retStart, int *retSize)
/* Fetch next line from file that is not blank and
 * does not start with a '#'. Return size of line. 
 * Not putting this into library because it's hard to get right for both retSize NULL
 * and non-NULL.  This case only works with retSize non-NULL. */
{
while (lineFileNext(lf, retStart, retSize))
    {
    if (isAllSpace(*retStart, *retSize))
        continue;
    return TRUE;
    }
return FALSE;
}


boolean oneFastqRecord(struct lineFile *lf, FILE *f, boolean copy, boolean firstTime)
/* Read next fastq record from LF, and optionally copy it to f.  Return FALSE at end of file 
 * Do a _little_ error checking on record while we're at it.  The format has already been
 * validated on the client side fairly thoroughly. */
{
char *line;
int lineSize;

/* Treat NULL file same as non-copy, so only have one condition to check on . */
if (f == NULL)
    copy = FALSE;

/* Deal with initial line starting with '@' */
if (!lineFileNextRealWithSize(lf, &line, &lineSize))
    return FALSE;
if (line[0] != '@')
    {
    errAbort("Expecting line starting with '@' got %s line %d of %s (ugh!)", 
	line, lf->lineIx, lf->fileName);
    }
if (copy)
    mustWrite(f, line, lineSize);

/* Deal with line containing sequence. */
if (!lineFileNext(lf, &line, &lineSize))
    errAbort("%s truncated in middle of record", lf->fileName);

/* Get size and add it to stats */
int seqSize = lineSize-1;
if (seqSize > MAX_READ_SIZE)
    errAbort("Sequence size %d too long line %d of %s.  Max is %d", seqSize, 
	lf->lineIx, lf->fileName, MAX_READ_SIZE);
if (firstTime)
    {
    maxReadBases = minReadBases = seqSize;
    }
else
    {
    if (maxReadBases < seqSize)
        maxReadBases = seqSize;
    if (minReadBases > seqSize)
        minReadBases = seqSize;
    }
sumReadBases += seqSize;
sumSquaredReadBases += seqSize*seqSize;
++readCount;

/* Save up nucleotide stats and abort on bogus nucleotides. */
int i;
for (i=0; i<seqSize; ++i)
    {
    char c = tolower(line[i]);
    switch (c)
        {
	case 'a':
	    aCount[i] += 1;
	    break;
	case 'c':
	    cCount[i] += 1;
	    break;
	case 'g':
	    gCount[i] += 1;
	    break;
	case 't':
	    tCount[i] += 1;
	    break;
	case 'n':
	case '.':
	    nCount[i] += 1;
	    break;
	default:
	    errAbort("Unrecognized nucleotide character %c line %d of %s", c,
		lf->lineIx, lf->fileName);
	    break;
	}
    }


if (copy)
    mustWrite(f, line, lineSize);

/* Deal with line containing just '+' that separates sequence from quality. */
nextLineMustMatchChar(lf, '+', TRUE);
if (copy)
    fprintf(f, "+\n");

/* Deal with quality score line. */
if (!lineFileNext(lf, &line, &lineSize))
    errAbort("%s truncated in middle of record", lf->fileName);
int qualSize = lineSize-1;

/* Make sure it is same size */
if (seqSize != qualSize)
    errAbort("Sequence and quality size differ line %d and %d of %s", 
	lf->lineIx-2, lf->lineIx, lf->fileName);

if (firstTime)
    {
    minQual = maxQual = line[0];
    }

/* Do stats */
for (i=0; i<seqSize; ++i)
    {
    int qual = line[i];
    if (maxQual < qual)
        maxQual = qual;
    if (minQual > qual)
        minQual = qual;
    sumQuals[i] += qual;
    sumSquaredQuals[i] += qual*qual;
    }

if (copy)
    mustWrite(f, line, lineSize);


return TRUE;
}

boolean maybeCopyFastqRecord(struct lineFile *lf, FILE *f, boolean copy, int *retSeqSize)
/* Read next fastq record from LF, and optionally copy it to f.  Return FALSE at end of file 
 * Do a _little_ error checking on record while we're at it.  The format has already been
 * validated on the client side fairly thoroughly. Similar to oneFastq record but with
 * fewer side effects. */
{
char *line;
int lineSize;

/* Deal with initial line starting with '@' */
if (!lineFileNextRealWithSize(lf, &line, &lineSize))
    return FALSE;
if (line[0] != '@')
    errAbort("Expecting line starting with '@' got %s line %d of %s", 
	line, lf->lineIx, lf->fileName);
if (copy)
    mustWrite(f, line, lineSize);


/* Deal with line containing sequence. */
if (!lineFileNext(lf, &line, &lineSize))
    errAbort("%s truncated in middle of record", lf->fileName);
if (copy)
    mustWrite(f, line, lineSize);
int seqSize = lineSize-1;

/* Deal with line containing just '+' that separates sequence from quality. */
/* Deal with line containing just '+' that separates sequence from quality. */
nextLineMustMatchChar(lf, '+', TRUE);
if (copy)
    fprintf(f, "+\n");

/* Deal with quality score line. */
if (!lineFileNext(lf, &line, &lineSize))
    errAbort("%s truncated in middle of record", lf->fileName);
if (copy)
    mustWrite(f, line, lineSize);
int qualSize = lineSize-1;

if (seqSize != qualSize)
    errAbort("Sequence and quality size differ line %d and %d of %s", 
	lf->lineIx-2, lf->lineIx, lf->fileName);

*retSeqSize = seqSize;
return TRUE;
}

long long reduceFastqSample(char *source, FILE *f, int oldSize, int newSize)
/* Copy newSize samples from source into open output f.  */
{
long long basesInSample = 0;

/* Make up an array that tells us which random parts of the source file to use. */
assert(oldSize >= newSize);
char *randomizer = needMem(oldSize);
memset(randomizer, TRUE, newSize);
shuffleArrayOfChars(randomizer, oldSize);

struct lineFile *lf = lineFileOpen(source, FALSE);
int i;
for (i=0; i<oldSize; ++i)
    {
    int seqSize;
    boolean doIt = randomizer[i];
    if (!maybeCopyFastqRecord(lf, f, doIt, &seqSize))
         internalErr();
    if (doIt)
	basesInSample += seqSize;
    }
freez(&randomizer);
lineFileClose(&lf);
return basesInSample;
}

void fastqStatsAndSubsample(char *inFastq, char *outStats, char *outFastq)
/* fastqStatsAndSubsample - Go through a fastq file doing sanity checks and collecting 
 * statistics,  and also producing a smaller fastq out of a sample of the data. */
{
/* Temporary file if any */
FILE *smallF = NULL;

/* Make this work without making input. */
if (sameString("/dev/null", outFastq))
    outFastq = NULL;


/* Open up temp output file.  This one will be for the initial scaling.  We'll do
 * a second round of scaling as well. */
char smallFastqName[PATH_LEN] = "";
char *smallishName = smallFastqName;
if (outFastq != NULL)
    {
    /* Split up outFastq path, so we can make a temp file in the same dir. */
    char outDir[PATH_LEN];
    if (outFastq)
	splitPath(outFastq, outDir, NULL, NULL);

    safef(smallFastqName, PATH_LEN, "%sfastqSubsampleXXXXXX", outDir);
    int smallFd = mkstemp(smallFastqName);
    smallF = fdopen(smallFd, "w");
    }

/* Scan through input, collecting stats, validating, and creating a subset file. */
int downStep = 10;
struct lineFile *lf = lineFileOpen(inFastq, FALSE);
boolean done = FALSE;
int readsCopied = 0, totalReads = 0;
long long basesInSample = 0;
boolean firstTime = TRUE;
while (!done)
    {
    int hotPosInCycle = rand()%downStep; 
    int cycle;
    for (cycle=0; cycle<downStep; ++cycle)
        {
	boolean hotPos = (cycle == hotPosInCycle);
	if (!oneFastqRecord(lf, smallF, hotPos, firstTime))
	    {
	    done = TRUE;
	    break;
	    }
	if (hotPos)
	    {
	    ++readsCopied;
	    }
	firstTime = FALSE;
	++totalReads;
	}
    }
lineFileClose(&lf);
carefulClose(&smallF);

if (outFastq != NULL && readsCopied <  sampleSize)
    {
    /* Our sample isn't big enough.  We'll have to reread the main file.  Good
     * news at least is that it isn't too big. */
    smallishName = inFastq;
    readsCopied = totalReads;
    if (sampleSize > totalReads)
	{
	if (smallOk)
	    {
	    warn("%d reads total in %s, so sample is less than %d", 
		totalReads, inFastq, sampleSize);
	    }
	else
	    {
	    remove(smallFastqName);
	    errAbort("SampleSize is set to %d reads, but there are only %d reads in %s",
		    sampleSize, totalReads, inFastq);
	    }
	sampleSize = totalReads;
	}
    }

char *qualType = "solexa";
int qualZero = 64;
if (minQual <= 58)
    {
    qualType = "sanger";
    qualZero = 33;
    }

if (outFastq != NULL)
    {
    FILE *f = mustOpen(outFastq, "w");
    basesInSample = reduceFastqSample(smallishName, f, readsCopied, sampleSize);
    carefulClose(&f);
    remove(smallFastqName);
    }

FILE *f = mustOpen(outStats, "w");
int posCount = maxReadBases;
fprintf(f, "readCount %d\n", totalReads);
fprintf(f, "baseCount %lld\n", sumReadBases);
fprintf(f, "sampleCount %d\n", sampleSize);
fprintf(f, "basesInSample %lld\n", basesInSample);
fprintf(f, "readSizeMean %g\n", (double)sumReadBases/totalReads);
if (minReadBases != maxReadBases)
    fprintf(f, "readSizeStd %g\n", calcStdFromSums(sumReadBases, sumSquaredReadBases, totalReads));
else
    fprintf(f, "readSizeStd 0\n");
fprintf(f, "readSizeMin %d\n", minReadBases);
fprintf(f, "readSizeMax %d\n", maxReadBases);
double qSum = sumDoubleArray(sumQuals, maxReadBases);
double qSumSquared = sumDoubleArray(sumSquaredQuals, maxReadBases);
fprintf(f, "qualMean %g\n", qSum/sumReadBases - qualZero);
if (minQual != maxQual)
    fprintf(f, "qualStd %g\n", calcStdFromSums(qSum, qSumSquared, sumReadBases));
else
    fprintf(f, "qualStd 0\n");
fprintf(f, "qualMin %d\n", minQual - qualZero);
fprintf(f, "qualMax %d\n", maxQual - qualZero);
fprintf(f, "qualType %s\n", qualType);
fprintf(f, "qualZero %d\n", qualZero);

/* Compute overall total nucleotide stats from count arrays. */
long long aSum = sumIntArray(aCount, maxReadBases);
long long cSum = sumIntArray(cCount, maxReadBases);
long long gSum = sumIntArray(gCount, maxReadBases);
long long tSum = sumIntArray(tCount, maxReadBases);
long long nSum = sumIntArray(nCount, maxReadBases);
fprintf(f, "atRatio %g\n", (double)(aSum + tSum)/(aSum + cSum + gSum + tSum));
fprintf(f, "aRatio %g\n", (double)aSum/sumReadBases);
fprintf(f, "cRatio %g\n", (double)cSum/sumReadBases);
fprintf(f, "gRatio %g\n", (double)gSum/sumReadBases);
fprintf(f, "tRatio %g\n", (double)tSum/sumReadBases);
fprintf(f, "nRatio %g\n", (double)nSum/sumReadBases);

/* Now deal with array outputs.   First make up count of all bases we've seen. */
fprintf(f, "posCount %d\n", posCount);
long long totalAtPos[posCount];
int pos;
for (pos=0; pos<posCount; ++pos)
    totalAtPos[pos] = aCount[pos] + cCount[pos] + gCount[pos] + tCount[pos] + nCount[pos];

/* Offset quality by scale */
for (pos=0; pos<posCount; ++pos)
    sumQuals[pos] -= totalAtPos[pos] * qualZero;

printAveDoubleArray(f, "qualPos", sumQuals, totalAtPos, posCount);
printAveIntArray(f, "aAtPos", aCount, totalAtPos, posCount);
printAveIntArray(f, "cAtPos", cCount, totalAtPos, posCount);
printAveIntArray(f, "gAtPos", gCount, totalAtPos, posCount);
printAveIntArray(f, "tAtPos", tCount, totalAtPos, posCount);
printAveIntArray(f, "nAtPos", nCount, totalAtPos, posCount);

}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 4)
    usage();
sampleSize = optionInt("sampleSize", sampleSize);
seed = optionInt("seed", seed);
srand(seed);
smallOk = optionExists("smallOk");
fastqStatsAndSubsample(argv[1], argv[2], argv[3]);
return 0;
}

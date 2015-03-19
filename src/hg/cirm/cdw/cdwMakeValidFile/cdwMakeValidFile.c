/* cdwMakeValidFile - Add range of ids to valid file table. */

/* Copyright (C) 2014 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "errCatch.h"
#include "localmem.h"
#include "errAbort.h"
#include "sqlNum.h"
#include "cheapcgi.h"
#include "obscure.h"
#include "jksql.h"
#include "asParse.h"
#include "twoBit.h"
#include "genomeRangeTree.h"
#include "basicBed.h"
#include "bigWig.h"
#include "bigBed.h"
#include "bamFile.h"
#include "htmlPage.h"
#include "portable.h"
#include "gff.h"
#include "cdw.h"
#include "cdwLib.h"
#include "fa.h"
#include "cdwValid.h"
#include "vcf.h"

int maxErrCount = 1;	/* Set from command line. */
int errCount;		/* Set as we run. */
boolean redo = FALSE;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "cdwMakeValidFile - Add range of ids to valid file table.\n"
  "usage:\n"
  "   cdwMakeValidFile startId endId\n"
  "options:\n"
  "   maxErrCount=N - maximum errors allowed before it stops, default %d\n"
  "   -redo - redo validation even if have it already\n"
  , maxErrCount);
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {"maxErrCount", OPTION_INT},
   {"redo", OPTION_BOOLEAN},
   {NULL, 0},
};

void alignFastqMakeBed(struct cdwFile *ef, struct cdwAssembly *assembly,
    char *fastqPath, struct cdwValidFile *vf, FILE *bedF)
/* Take a sample fastq and run bwa on it, and then convert that file to a bed. 
 * Update vf->mapRatio and related fields. */
{
cdwAlignFastqMakeBed(ef, assembly, fastqPath, vf, bedF, 
    &vf->mapRatio, &vf->depth, &vf->sampleCoverage, &vf->uniqueMapRatio);
}

int makeReadableTemp(char *fileName)
/* Make temp file that other people can read. */
{
int fd = mkstemp(fileName);
if (fchmod(fd, 0664) == -1)
    errnoAbort("Couldn't change permissions on temp file %s", fileName);
return fd;
}

void makeValidFastq( struct sqlConnection *conn, char *path, struct cdwFile *ef, 
	struct cdwAssembly *assembly, struct cdwValidFile *vf)
/* Fill out fields of vf.  Create sample subset. */
{
/* Make cdwFastqFile record. */
long long fileId = ef->id;
cdwMakeFastqStatsAndSample(conn, fileId);
struct cdwFastqFile *fqf = cdwFastqFileFromFileId(conn, fileId);
verbose(1, "Made sample fastq with %lld reads\n", fqf->sampleCount);

/* Save some key pieces in vf. */
vf->itemCount = fqf->readCount;
vf->basesInItems = fqf->baseCount;
vf->sampleCount = fqf->sampleCount;
vf->basesInSample = fqf->basesInSample;

/* Align fastq and turn results into bed. */
char sampleBedName[PATH_LEN], temp[PATH_LEN];
safef(sampleBedName, PATH_LEN, "%scdwSampleBedXXXXXX", cdwTempDirForToday(temp));
int bedFd = makeReadableTemp(sampleBedName);
FILE *bedF = fdopen(bedFd, "w");
alignFastqMakeBed(ef, assembly, fqf->sampleFileName, vf, bedF);
carefulClose(&bedF);
vf->sampleBed = cloneString(sampleBedName);

cdwFastqFileFree(&fqf);
}

#define TYPE_BAM  1
#define TYPE_READ 2

#ifdef OLD
struct miniBed
/* Almost a bed record. */
    {
    struct miniBed *next;
    uint32_t tid;   // Target ID in a bam file 
    uint32_t start; // Start position
    uint32_t size;  // Size of read
    char strand;    //  '+' or '-'
    };

int miniBedCmp(const void *va, const void *vb)
/* Compare to sort based on query start. */
{
const struct targetPos *a = *((struct targetPos **)va);
const struct targetPos *b = *((struct targetPos **)vb);
int dif;
dif = a->tid - b->tid;
if (dif == 0)
    dif = a->start - b->start;
return dif;
}

void cdwMakeSampleOfBam(char *inBamName, FILE *outBed, int maxSampleSize, 
    struct cdwAssembly *assembly, struct genomeRangeTree *grt, struct cdwValidFile *vf)
/* Sample every downStep items in inBam and write in simplified bed 5 fashion to outBed. */
{
samfile_t *sf = samopen(inBamName, "rb", NULL);
bam_header_t *bamHeader = sf->header;
struct lm *lm = lmInit(0);
struct miniBed *mbList = NULL, *mb;

bam1_t one;
ZeroVar(&one);	// This seems to be necessary!

/* Pass through collecting counts and making up miniBeds for items. */
long long mappedCount = 0, uniqueMappedCount = 0;
for (;;)
    {
    if (bam_read1(sf->x.bam, &one) < 0)
	break;
    int32_t tid = one.core.tid;
    int l_qseq = one.core.l_qseq;
    if (tid > 0)
	{
	++mappedCount;
	if (one.core.qual > cdwMinMapQual)
	    {
	    ++uniqueMappedCount;
	    lmAllocVar(lm, mb);
	    mb->tid = tid;
	    mb->start = one.core.pos;
	    mb->size = l_qseq;
	    mb-strand = ((one.core.flat & BAM_FREVERSE) ? '-' : '+');
	    slAddHead(&mbList, mb);
	    }
	}
    }

/* Whittle down mini bed list to sample, and crawl through it making bed etc. */
mbList = slListRandomSample(mbList, maxSampleSize);
for (mb = mbList; mb != NULL; mb = mb->next)
    {
    vf->sampleCount += 1;
    vf->basesInSample += mb->size;
    char *chrom = bamHeader->target_name[tid];
       {
       if (tid > 0)
	   {
	   int start = one.core.pos;
	   // Approximate here... can do better if parse cigar.
	   int end = start + l_qseq;	
	   boolean isRc = (one.core.flag & BAM_FREVERSE);
	   char strand = '+';
	   if (isRc)
	       {
	       strand = '-';
	       }
	   if (start < 0) start=0;
	   fprintf(outBed, "%s\t%d\t%d\t.\t0\t%c\n", chrom, start, end, strand);
	   genomeRangeTreeAdd(grt, chrom, start, end);
	   }
       }
    }
vf->mapRatio = (double)mappedCount/vf->itemCount;
vf->uniqueMapRatio = (double)uniqueMappedCount/vf->itemCount;
vf->depth = vf->basesInItems*vf->mapRatio/assembly->baseCount;
samclose(sf);
lmCleanup(&lm);
}
#endif /* OLD */

void makeValidBigBed( struct sqlConnection *conn, char *path, struct cdwFile *ef, 
	struct cdwAssembly *assembly, char *format, struct cdwValidFile *vf)
/* Fill in fields of vf based on bigBed. */
{
struct bbiFile *bbi = bigBedFileOpen(path);
vf->sampleCount = vf->itemCount = bigBedItemCount(bbi);
struct bbiSummaryElement sum = bbiTotalSummary(bbi);
vf->basesInSample = vf->basesInItems = sum.sumData;
vf->sampleCoverage = (double)sum.validCount/assembly->baseCount;
vf->depth = (double)sum.sumData/assembly->baseCount;
vf->mapRatio = 1.0;
bigBedFileClose(&bbi);
}

void makeValidBed( struct sqlConnection *conn, char *path, struct cdwFile *ef, 
	struct cdwAssembly *assembly, char *format, char *asRoot, struct cdwValidFile *vf)
/* Fill in fields of vf based on bed and grind through file checking it. */
{
/* Get structure with info about which fields are true bed. */
struct cdwBedType *bedType = cdwBedTypeFind(asRoot);
int bedFieldCount = bedType->bedFields;

/* Load up as file to check against */
char asPath[PATH_LEN];
cdwAsPath(asRoot, asPath);
struct asObject *as = asParseFile(asPath);

/* Create a row one bigger than expected (so can detect rows too big as well 
 * as too small. */
int colCount = slCount(as->columnList);
int colAlloc = colCount+1;
char *row[colAlloc];

/* Loop through file validating each line and collecting statistics. */
struct lineFile *lf = lineFileOpen(path, TRUE);
struct bed bed = {};
char *line;
int itemCount = 0;
long long baseCount = 0;
while (lineFileNextReal(lf, &line))
    {
    int wordsRead = chopByWhite(line, row, colAlloc);
    lineFileExpectWords(lf, colCount, wordsRead);
    loadAndValidateBed(row, bedFieldCount, colCount, lf, &bed, as, FALSE);
    ++itemCount;
    baseCount += bed.chromEnd - bed.chromStart;
    }

asObjectFreeList(&as);

/* Fill in fields of vf based on statistics */
vf->sampleCount = vf->itemCount = itemCount;
vf->basesInSample = vf->basesInItems = baseCount;
vf->sampleCoverage = vf->depth = (double)baseCount/assembly->baseCount;
vf->mapRatio = 1.0;
}

void makeValidBigWig(struct sqlConnection *conn, char *path, struct cdwFile *ef, 
	struct cdwAssembly *assembly, struct cdwValidFile *vf)
/* Fill in fields of vf based on bigWig. */
{
struct bbiFile *bbi = bigWigFileOpen(path);
struct bbiSummaryElement sum = bbiTotalSummary(bbi);
vf->sampleCount = vf->itemCount = vf->basesInSample = vf->basesInItems = sum.validCount;
vf->sampleCoverage = (double)sum.validCount/assembly->baseCount;
vf->depth = (double)sum.sumData/assembly->baseCount;
vf->mapRatio = 1.0;
bigWigFileClose(&bbi);
}

void cdwValidFileDump(struct cdwValidFile *vf, FILE *f)
/* Write out info about vf, just for debugging really */
{
fprintf(f, "vf->id = %d\n", vf->id);
fprintf(f, "vf->licensePlate = %s\n", vf->licensePlate);
fprintf(f, "vf->fileId = %d\n", vf->fileId);
fprintf(f, "vf->format = %s\n", vf->format);
fprintf(f, "vf->outputType = %s\n", vf->outputType);
fprintf(f, "vf->experiment = %s\n", vf->experiment);
fprintf(f, "vf->replicate = %s\n", vf->replicate);
fprintf(f, "vf->enrichedIn = %s\n", vf->enrichedIn);
fprintf(f, "vf->ucscDb = %s\n", vf->ucscDb);
fprintf(f, "vf->itemCount = %lld\n", vf->itemCount);
fprintf(f, "vf->basesInItems = %lld\n", vf->basesInItems);
fprintf(f, "vf->sampleBed = %s\n", vf->sampleBed);
fprintf(f, "vf->sampleCount = %lld\n", vf->sampleCount);
fprintf(f, "vf->basesInSample = %lld\n", vf->basesInSample);
fprintf(f, "vf->sampleCoverage = %g\n", vf->sampleCoverage);
fprintf(f, "vf->sampleCount = %g\n", vf->depth);
}

void makeValidBam( struct sqlConnection *conn, char *path, struct cdwFile *ef, 
	struct cdwAssembly *assembly, struct cdwValidFile *vf)
/* Fill out fields of vf based on bam.  Create sample subset as a little bed file. */
{
/* Have cdwBamStats do most of the work. */
char sampleFileName[PATH_LEN];
struct cdwBamFile *ebf = cdwMakeBamStatsAndSample(conn, ef->id, sampleFileName);

/* Fill in some of validFile record from bamFile record */
vf->sampleBed = cloneString(sampleFileName);
vf->itemCount = ebf->readCount;
vf->basesInItems = ebf->readBaseCount;
vf->mapRatio = (double)ebf->mappedCount/ebf->readCount;
vf->uniqueMapRatio = (double)ebf->uniqueMappedCount/ebf->readCount;
vf->depth = vf->basesInItems*vf->mapRatio/assembly->baseCount;

/* Scan through the bam file to make up information about the sample bits */
struct genomeRangeTree *grt = genomeRangeTreeNew();
struct lineFile *lf = lineFileOpen(sampleFileName, TRUE);
char *row[3];
while (lineFileRow(lf, row))
    {
    char *chrom = row[0];
    unsigned start = sqlUnsigned(row[1]);
    unsigned end = sqlUnsigned(row[2]);
    vf->sampleCount += 1;
    vf->basesInSample += end - start;
    genomeRangeTreeAdd(grt, chrom, start, end);
    }
lineFileClose(&lf);

/* Fill in last bits that need summing from the genome range tree. */
long long basesHitBySample = genomeRangeTreeSumRanges(grt);
vf->sampleCoverage = (double)basesHitBySample/assembly->baseCount;
genomeRangeTreeFree(&grt);
cdwBamFileFree(&ebf);
}

void makeValid2Bit(struct sqlConnection *conn, char *path, struct cdwFile *ef, 
    struct cdwValidFile *vf)
/* Fill in info about assembly */
{
struct twoBitFile *tbf = twoBitOpen(path);
vf->basesInItems = vf->basesInSample = twoBitTotalSize(tbf);
vf->itemCount = vf->sampleCount = tbf->seqCount;
vf->mapRatio = 1.0;
vf->sampleCoverage = 1.0;
vf->depth = 1.0;
twoBitClose(&tbf);
}

void makeValidFasta(struct sqlConnection *conn, char *path, struct cdwFile *ef, 
    struct cdwValidFile *vf)
/* Fill in info about fasta file */
{
struct lineFile *lf = lineFileOpen(path, FALSE);
DNA *dna;
int size;
char *name;
while (faSpeedReadNext(lf, &dna, &size, &name))
    {
    vf->basesInItems += size;
    vf->itemCount += 1;
    }
lineFileClose(&lf);
}

void genomeRangeTreeWriteAsBed3(struct genomeRangeTree *grt, char *fileName)
/* Write as bed4 file */
{
FILE *f = mustOpen(fileName, "w");
struct hashEl *chrom, *chromList = hashElListHash(grt->hash);
slSort(&chromList, hashElCmpWithEmbeddedNumbers);
for (chrom = chromList; chrom != NULL; chrom = chrom->next)
    {
    char *chromName = chrom->name;
    struct rbTree *rangeTree = chrom->val;
    struct range *range, *rangeList = rangeTreeList(rangeTree);
    for (range = rangeList; range != NULL; range = range->next)
	fprintf(f, "%s\t%d\t%d\n", chromName, range->start, range->end);
    }
carefulClose(&f);
}

void makeValidVcf( struct sqlConnection *conn, char *path, struct cdwFile *ef, 
	struct cdwAssembly *assembly, struct cdwValidFile *vf)
/* Fill out fields of vf from a variant call format (vcf) file.  Create bed file. */
{
/* Have cdwVcfStats do most of the work. */
char sampleFileName[PATH_LEN];
struct cdwVcfFile *vcf = cdwMakeVcfStatsAndSample(conn, ef->id, sampleFileName);

/* Fill in some of validFile record from bamFile record */
vf->sampleBed = cloneString(sampleFileName);
vf->itemCount = vcf->itemCount;
vf->basesInItems = vcf->sumOfSizes;
vf->mapRatio = 1;
vf->uniqueMapRatio = 1;
vf->depth = (double)vcf->sumOfSizes/assembly->baseCount;
vf->sampleCount =  vcf->itemCount;
vf->basesInSample = vcf->sumOfSizes;
vf->sampleCoverage = (double)vcf->basesCovered/assembly->baseCount;
cdwVcfFileFree(&vcf);
}


void makeValidText(struct sqlConnection *conn, char *path, struct cdwFile *ef, 
    struct cdwValidFile *vf)
/* Fill in info about a text file. */
{
struct lineFile *lf = lineFileOpen(path, FALSE);
char *line;
int lineSize;
while (lineFileNext(lf, &line, &lineSize))
    {
    int i;
    for (i=0; i<lineSize; ++i)
         {
	 if (line[i] == 0)
	     errAbort("%s is not a text file,  has binary zeroes.", ef->submitFileName);
	 }
    vf->itemCount += 1;
    }
lineFileClose(&lf);
}

void makeValidHtml(struct sqlConnection *conn, char *path, struct cdwFile *ef, 
    struct cdwValidFile *vf)
/* Fill in info about html file */
{
struct htmlPage *page = htmlPageGet(path);
htmlPageValidateOrAbort(page);
vf->itemCount = slCount(page->tags);
htmlPageFree(&page);
}

void makeValidGtf(struct sqlConnection *conn, char *path, struct cdwFile *ef, 
    struct cdwAssembly *assembly, struct cdwValidFile *vf)
/* Fill in info about a gtf file. */
{
/* Open and read file with generic GFF reader and check it is GTF */
struct gffFile *gff = gffRead(path);
if (!gff->isGtf)
    errAbort("file id %lld (%s) is not in GTF format - check it has gene_id and transcript_id",
	(long long)ef->id, ef->submitFileName);

/* Convert it to a somewhat smaller less informative bed file for sampling purposes. */
char sampleFileName[PATH_LEN], temp[PATH_LEN];
safef(sampleFileName, PATH_LEN, "%scdwGffBedXXXXXX", cdwTempDirForToday(temp));
int sampleFd = makeReadableTemp(sampleFileName);
FILE *f = fdopen(sampleFd, "w");
struct genomeRangeTree *grt = genomeRangeTreeNew();

/* Loop through lines writing out simple bed and adding to genome range tree. */
struct gffLine *gffLine;
long long itemCount = 0;
long long totalSize = 0;
for (gffLine = gff->lineList; gffLine != NULL; gffLine = gffLine->next)
    {
    totalSize += gffLine->end - gffLine->start;
    fprintf(f, "%s\t%d\t%d\n", gffLine->seq, gffLine->start, gffLine->end);
    genomeRangeTreeAdd(grt, gffLine->seq, gffLine->start, gffLine->end);
    ++itemCount;
    }
carefulClose(&f);

/* Fill out what we can of vf with info we've gathered. */
vf->itemCount = vf->sampleCount = itemCount;
vf->basesInItems = vf->basesInSample = totalSize;
vf->sampleBed = cloneString(sampleFileName);
long long basesHitBySample = genomeRangeTreeSumRanges(grt);
genomeRangeTreeFree(&grt);
vf->sampleCoverage = (double)basesHitBySample/assembly->baseCount;
vf->mapRatio = 1.0;
vf->depth = (double)totalSize/assembly->baseCount;
gffFileFree(&gff);
}

void makeValidRcc(struct sqlConnection *conn, char *path, struct cdwFile *ef, struct cdwValidFile *vf)
/* Fill in info about a nanostring RCC file. */
{
cdwValidateRcc(path);
}

void makeValidIdat(struct sqlConnection *conn, char *path, 
    struct cdwFile *ef, struct cdwValidFile *vf)
/* Fill in info about a illumina idac file. */
{
cdwValidateIdat(path);
}

void makeValidPdf(struct sqlConnection *conn, char *path, struct cdwFile *ef, struct cdwValidFile *vf)
/* Check it is really pdf. */
{
cdwValidatePdf(path);
}

void validateVcfGzTbi(struct sqlConnection *conn, char *path, 
    struct cdwFile *ef, struct cdwValidFile *vf)
/* Given a path to a tabix on a vcf, validate it is tabix, and that the 
 * vcf it refers to exists and has correct name */
{
char dir[PATH_LEN], name[FILENAME_LEN], extension[FILEEXT_LEN];
splitPath(path, dir, name, extension);
char vcfPath[PATH_LEN];
safef(vcfPath, sizeof(vcfPath), "%s%s%s", dir, name, extension);
if (!fileExists(vcfPath))
    {
    /* Look for it under cdwPathName */
    if (!cdwFindInSameSubmitDir(conn, ef, vcfPath))
	errAbort("%s, the original of %s doesn't exist", vcfPath, path);
    }
}

void makeValidCustomTrack(struct sqlConnection *conn, char *path, 
    struct cdwFile *ef, struct cdwValidFile *vf)
/* Fill in some info about a BED file of no particular sub-format. This is allowed to have
 * browser and track lines in it, which are ignored. */
{
struct lineFile *lf = lineFileOpen(path, TRUE);
char *line;
char *row[256];
int bedSize = 0;
while (lineFileNextReal(lf, &line))
    {
    if (startsWithWord("browser", line) || startsWithWord("track", line))
        continue;
    int wordCount = chopLine(line, row);
    if (bedSize == 0)
	{
        bedSize = wordCount;
	if (bedSize < 3)
	    {
	    lineFileExpectAtLeast(lf, 3, bedSize);
	    }
	}
    else
	{
        if (bedSize != wordCount)
	    {
	    errAbort("Some lines of %s have %d words, but line %d of has %d words",
		    lf->fileName, bedSize, lf->lineIx, wordCount);
	    }
	}
    int start = lineFileNeedNum(lf, row, 1);
    int end = lineFileNeedNum(lf, row, 2);
    if (end < start)
         errAbort("end before start line %d of %s", lf->lineIx, lf->fileName);
    ++vf->itemCount;
    vf->basesInItems += (end - start);
    }
lineFileClose(&lf);
}

static void needAssembly(struct cdwFile *ef, char *format, struct cdwAssembly *assembly)
/* Require assembly tag be present. */
{
if (assembly == NULL)
    errAbort("file id %lld (%s) is %s format and needs an assembly tag to validate", 
	(long long)ef->id, ef->submitFileName, format);
}


void mustMakeValidFile(struct sqlConnection *conn, struct cdwFile *ef, struct cgiParsedVars *tags,
    long long oldValidId)
/* If possible make a cdwValidFile record for this.  Makes sure all the right tags are there,
 * and then parses file enough to determine itemCount and the like.  For some files, like fastqs,
 * it will take a subset of the file as a sample so can do QA without processing the whole thing. */
{
/* Make up validFile from tags and id */
struct cdwValidFile *vf;
AllocVar(vf);
vf->fileId = ef->id;
cdwValidFileFieldsFromTags(vf, tags);
vf->sampleBed = "";

if (vf->format)	// We only can validate if we have something for format 
    {
    /* Look up assembly. */
    struct cdwAssembly *assembly = NULL;
    if (!isEmpty(vf->ucscDb) && !sameString(vf->ucscDb, "unknown"))
	{
	char *ucscDb = vf->ucscDb;
	char query[256];
	sqlSafef(query, sizeof(query), "select * from cdwAssembly where ucscDb='%s'", vf->ucscDb);
	assembly = cdwAssemblyLoadByQuery(conn, query);
	if (assembly == NULL)
	    errAbort("Couldn't find assembly corresponding to %s", ucscDb);
	}

    /* Make path to file */
    char path[PATH_LEN];
    safef(path, sizeof(path), "%s%s", cdwRootDir, ef->cdwFileName);

    /* And dispatch according to format. */
    char *format = vf->format;
    char *suffix = cdwFindDoubleFileSuffix(path);
    char suffixBuf[128];

    char *bedPrefix = "bed_";
    int bedPrefixSize = 4;

    if (sameString(format, "fastq"))
	{
	needAssembly(ef, format, assembly);
	makeValidFastq(conn, path, ef, assembly, vf);
	suffix = ".fastq.gz";
	}
    else if (cdwIsSupportedBigBedFormat(format))
	{
	needAssembly(ef, format, assembly);
	makeValidBigBed(conn, path, ef, assembly, format, vf);
	if (sameString(format, "bigBed"))
	    suffix = ".bigBed";
	else
	    {
	    safef(suffixBuf, sizeof(suffixBuf), ".%s.bigBed", format);
	    suffix = suffixBuf;
	    }
	}
    else if (startsWith(bedPrefix, format) && cdwIsSupportedBigBedFormat(format+bedPrefixSize))
        {
	char *formatNoBed = format + bedPrefixSize;
	needAssembly(ef, format, assembly);
	makeValidBed(conn, path, ef, assembly, format, formatNoBed, vf);
	safef(suffixBuf, sizeof(suffixBuf), ".%s.bed", format);
	suffix = suffixBuf;
	}
    else if (sameString(format, "bigWig"))
        {
	needAssembly(ef, format, assembly);
	makeValidBigWig(conn, path, ef, assembly, vf);
	suffix = ".bigWig";
	}
    else if (sameString(format, "bam"))
        {
	needAssembly(ef, format, assembly);
	makeValidBam(conn, path, ef, assembly, vf);
	suffix = ".bam";
	}
    else if (sameString(format, "bam.bai"))
        {
	cdwValidateBamIndex(path);
	suffix = ".bam.bai";
	}
    else if (sameString(format, "2bit"))
        {
	makeValid2Bit(conn, path, ef, vf);
	suffix = ".2bit";
	}
    else if (sameString(format, "fasta"))
        {
	makeValidFasta(conn, path, ef, vf);
	suffix = ".fasta.gz";
	}
    else if (sameString(format, "gtf"))
        {
	needAssembly(ef, format, assembly);
	makeValidGtf(conn, path, ef, assembly, vf);
	suffix = ".gtf.gz";
	}
    else if (sameString(format, "rcc"))
        {
	makeValidRcc(conn, path, ef, vf);
	suffix = ".RCC";
	}
    else if (sameString(format, "idat"))
        {
	makeValidIdat(conn, path, ef, vf);
	suffix = ".idat";
	}
    else if (sameString(format, "customTrack"))
        {
	makeValidCustomTrack(conn, path, ef, vf);
	assert(endsWith(ef->submitFileName, ".gz"));
	suffix = cdwFindDoubleFileSuffix(ef->submitFileName);
	}
    else if (sameString(format, "pdf"))
        {
	makeValidPdf(conn, path, ef, vf);
	suffix = ".pdf";
	}
    else if (sameString(format, "vcf"))
        {
	needAssembly(ef, format, assembly);
	makeValidVcf(conn, path, ef, assembly, vf);
	if (endsWith(ef->submitFileName, ".gz"))
	    suffix = ".vcf.gz";
	else
	    suffix = ".vcf";
	}
    else if (sameString(format, "vcf.gz.tbi"))
        {
	validateVcfGzTbi(conn, path, ef, vf);
	suffix = ".vcf.gz.tbi";
	}
    else if (sameString(format, "cram"))
        {
	cdwValidateCram(path);
	suffix = ".cram";
	}
    else if (sameString(format, "jpg"))
        {
	cdwValidateJpg(path);
	suffix = ".jpg";
	}
    else if (sameString(format, "text"))
        {
	makeValidText(conn, path, ef, vf);
	}
    else if (sameString(format, "html"))
        {
	makeValidHtml(conn, path, ef, vf);
	}
    else if (sameString(format, "unknown"))
        {
	/* No specific validation needed for unknown format. */
	}
    else
        {
	errAbort("Unrecognized format %s for %s\n", format, ef->cdwFileName);
	}

    /* Save record except for license plate to DB. */
    if (oldValidId == 0)
	{
	cdwValidFileSaveToDb(conn, vf, "cdwValidFile", 512);
	vf->id = sqlLastAutoId(conn);

	/* Create license plate around our ID.  File in warehouse to use license plate
	 * instead of baby-babble IDs. */
	    {
	    cdwMakeLicensePlate( cdwLicensePlateHead(conn), 
		vf->id, vf->licensePlate, cdwMaxPlateSize);

	    /* Create swapped out version of cdwFileName in newName. */
	    struct dyString *newName = dyStringNew(0);
	    char *fileName = ef->cdwFileName;
	    char *dirEnd = strrchr(fileName, '/');
	    if (dirEnd == NULL)
		dirEnd = fileName;
	    else
		dirEnd += 1;
	    dyStringAppendN(newName, fileName, dirEnd - fileName);
	    dyStringAppend(newName, vf->licensePlate);
	    dyStringAppend(newName, suffix);

	    /* Now build full path names and attempt rename in file system. */
	    char oldPath[PATH_LEN], newPath[PATH_LEN];
	    safef(oldPath, sizeof(oldPath), "%s%s", cdwRootDir, fileName);
	    safef(newPath, sizeof(newPath), "%s%s", cdwRootDir, newName->string);
	    mustRename(oldPath, newPath);
	    verbose(2, "Renamed %s to %s\n", oldPath, newPath);

	    /* Update database with new name - small window of vulnerability here sadly 
	     * two makeValidates running at same time stepping on each other. */
	    char query[PATH_LEN+256];
	    sqlSafef(query, sizeof(query), "update cdwFile set cdwFileName='%s' where id=%lld",
		newName->string, (long long)ef->id);
	    sqlUpdate(conn, query);

	    dyStringFree(&newName);
	    }

	/* Update validFile record with license plate. */
	char query[256];
	sqlSafef(query, sizeof(query), "update cdwValidFile set licensePlate='%s' where id=%lld", 
	    vf->licensePlate, (long long)vf->id);
	sqlUpdate(conn, query);
	}
    else
        {
	cdwValidFileUpdateDb(conn, vf, oldValidId);
	}
    }
freez(&vf);
}

boolean makeValidFile(struct sqlConnection *conn, struct cdwFile *ef, struct cgiParsedVars *tags,
    long long oldValidId)
/* Attempt to make validation.  If it fails catch error and attach it to ef->errorMessage as well
 * as sending it to stderr, and return FALSE.  Otherwise return TRUE. */
{
struct errCatch *errCatch = errCatchNew();
boolean success = TRUE;
if (errCatchStart(errCatch))
    {
    mustMakeValidFile(conn, ef, tags, oldValidId);
    }
errCatchEnd(errCatch);
if (errCatch->gotError)
    {
    cdwWriteErrToStderrAndTable(conn, "cdwFile", ef->id, errCatch->message->string);
    warn("This is from submitted file %s", ef->submitFileName);
    success = FALSE;
    }
else
    {
    warn("%s", errCatch->message->string);  // Make status output legible
    char query[256];
    sqlSafef(query, sizeof(query), "update cdwFile set errorMessage='' where id=%lld",
	(long long)ef->id);
    sqlUpdate(conn, query);
    }
errCatchFree(&errCatch);
return success;
}

void cdwClearFileError(struct sqlConnection *conn, long long fileId)
/* Clear file error message */
{
char query[256];
sqlSafef(query, sizeof(query), "update cdwFile set errorMessage='' where id=%lld", fileId);
sqlUpdate(conn, query);
}

void cdwMakeValidFile(int startId, int endId)
/* cdwMakeValidFile - Add range of ids to valid file table.. */
{
/* Make list with all files in ID range  - don't want to use cdwFileAllIntactInRange because
 * we may be revalidating files that do have errors. */
struct sqlConnection *conn = sqlConnect(cdwDatabase);
char query[512];
sqlSafef(query, sizeof(query),
    "select * from cdwFile where id>=%d and id<=%d and endUploadTime != 0 "
    "and updateTime != 0", 
    startId, endId);
struct cdwFile *ef, *efList = cdwFileLoadByQuery(conn, query);

for (ef = efList; ef != NULL; ef = ef->next)
    {
    char query[256];
    sqlSafef(query, sizeof(query), "select id from cdwValidFile where fileId=%lld", (long long)ef->id);
    long long vfId = sqlQuickLongLong(conn, query);
    if (vfId != 0 && isEmpty(ef->errorMessage) && !redo)
	{
        verbose(2, "already validated %s %s\n", ef->cdwFileName, ef->submitFileName);
	}
    else
	{
	verbose(1, "processing %lld %s %s\n", (long long)ef->id, ef->cdwFileName, ef->submitFileName);
	char path[PATH_LEN];
	safef(path, sizeof(path), "%s%s", cdwRootDir, ef->cdwFileName);
	if (!isEmpty(ef->tags)) // All ones we care about have tags
	    {
	    if (vfId != 0)
	        cdwClearFileError(conn, ef->id);
	    struct cgiParsedVars *tags = cgiParsedVarsNew(ef->tags);
	    struct cgiParsedVars *parentTags = NULL;
	    char query[256];
	    sqlSafef(query, sizeof(query), 
		"select tags from cdwMetaTags where id=%u", ef->metaTagsId);
	    char *metaCgi = sqlQuickString(conn, query);
	    if (metaCgi != NULL)
	        {
		parentTags = cgiParsedVarsNew(metaCgi);
		tags->next = parentTags;
		}
	    if (!makeValidFile(conn, ef, tags, vfId))
	        {
		if (++errCount >= maxErrCount)
		    errAbort("Aborting after %d errors", errCount);
		}
	    cgiParsedVarsFree(&tags);
	    freez(&metaCgi);
	    cgiParsedVarsFree(&parentTags);
	    }
	else
	    {
	    verbose(2, "no tags to validate on %s %s\n", ef->cdwFileName, ef->submitFileName);
	    }
	}
    }

sqlDisconnect(&conn);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 3)
    usage();
maxErrCount = optionInt("maxErrCount", maxErrCount);
redo = optionExists("redo");
cdwMakeValidFile(sqlUnsigned(argv[1]), sqlUnsigned(argv[2]));
return 0;
}

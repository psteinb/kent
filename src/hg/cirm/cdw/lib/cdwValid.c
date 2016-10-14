/* Things to do with CIRM validation. */

/* Copyright (C) 2013 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "hash.h"
#include "hex.h"
#include "linefile.h"
#include "cdwValid.h"


char *cdwCalcValidationKey(char *md5Hex, long long fileSize)
/* calculate validation key to discourage faking of validation.  Do freeMem on 
 *result when done. */
{
if (strlen(md5Hex) != 32)
    errAbort("Invalid md5Hex string: %s\n", md5Hex);
long long sum = 0;
sum += fileSize;
while (*md5Hex)
    {
    unsigned char n = hexToByte(md5Hex);
    sum += n;
    md5Hex += 2;
    }
int vNum = sum % 10000;
char buf[256];
safef(buf, sizeof buf, "V%d", vNum);
return cloneString(buf);
}

static char *fileNameOnly(char *fullName)
/* Return just the fileName part of the path */
{
char *fileName = strrchr(fullName, '/');
if (!fileName)
    fileName = fullName;
return fileName;
}

static void requireStartEndLines(char *fileName, char *startLine, char *endLine)
/* Make sure first real line in file is startLine, and last is endLine.  Tolerate
 * empty lines and white space. */
{
char *reportFileName = fileNameOnly(fileName);
struct lineFile *lf = lineFileOpen(fileName, TRUE);
char *line;

/* Get first real line and make sure it is not empty and matches start line. */
if (!lineFileNextReal(lf, &line))
    errAbort("%s is empty", reportFileName);
line = trimSpaces(line);
if (!sameString(line, startLine))
    errAbort("%s doesn't start with %s as expected", reportFileName, startLine);

boolean lastSame = FALSE;

for (;;)
    {
    if (!lineFileNextReal(lf, &line))
        break;
    line = trimSpaces(line);
    lastSame = sameString(line, endLine);
    }
if (!lastSame)
    errAbort("%s doesn't end with %s as expected", reportFileName, endLine);

lineFileClose(&lf);
}

void cdwValidateRcc(char *path)
/* Validate a nanostring rcc file. */
{
requireStartEndLines(path, "<Header>", "</Messages>");
}

static boolean fileStartsWithOneOfPair(char *fileName,  char *one, char *two)
/* Return TRUE if file starts with either one of two strings. */
{
/* Figure out size of one and two strings. */
int oneLen = strlen(one);
int twoLen = strlen(two);
int maxLen = max(oneLen, twoLen);
assert(maxLen > 0);
int bufLen = maxLen+1;
char buf[bufLen];

/* Open up file and try to read enough data */
FILE *f = fopen(fileName, "r");
if (f == NULL)
   return FALSE;
int sizeRead = fread(buf, 1, maxLen, f);
carefulClose(&f);

/* Return TRUE if we match one or two, otherwise FALSE. */
if (oneLen >= sizeRead && memcmp(buf, one, oneLen) == 0)
    return TRUE;
else if (twoLen >= sizeRead && memcmp(buf, two, twoLen) == 0)
    return TRUE;
return FALSE;
}

static boolean fileStartsWith(char *path, char *string)
/* Make sure file starts with string */
{
return fileStartsWithOneOfPair(path, string, string);
}


void cdwValidateIdat(char *path)
/* Validate illumina idat file. */
{
if (!fileStartsWithOneOfPair(path, "IDAT", "DITA"))
    errAbort("%s is not a valid .idat file, it does not start with IDAT or DITA", fileNameOnly(path));
}

void cdwValidatePdf(char *path)
/* Make sure PDF really is PDF */
{
if (!fileStartsWith(path, "%PDF"))
    errAbort("%s in not a valid .pdf file, it does not start with %%PDF", fileNameOnly(path));
}

void cdwValidateCram(char *path)
/* Validate cram file. */
{
if (!fileStartsWith(path, "CRAM"))
    errAbort("%s is not a valid .cram file, it does not start with CRAM", fileNameOnly(path));
}

void cdwValidateJpg(char *path)
/* Check jpg file is really jpg */
{
if (!fileStartsWithOneOfPair(path, "\xff\xd8\xff\xe0", "\xff\xd8\xff\xe1"))
    errAbort("%s is not a valid .jpeg file", fileNameOnly(path));
}

void cdwValidateBamIndex(char *path)
/* Check .bam.bai really is index. */
{
if (!fileStartsWith(path, "BAI"))
    errAbort("%s is not a valid .bam.bai file", fileNameOnly(path));
}

void cdwValidateTabixIndex(char *path)
/* Check that a tabix index file (used for VCF files among other things) starts with right characters */
{
if (!fileStartsWith(path, "TIDX"))
    errAbort("%s is not a valid TABIX index file", fileNameOnly(path));
}

boolean cdwIsGzipped(char *path)
/* Return TRUE if file at path starts with GZIP signature */
{
FILE *f = mustOpen(path, "r");
int first = fgetc(f);
int second = fgetc(f);
carefulClose(&f);
return first == 0x1F && second == 0x8B;
}

static char *edwSupportedEnrichedIn[] = {"unknown", "exon", "intron", "promoter", "coding",
    "utr", "utr3", "utr5", "open"};
static int edwSupportedEnrichedInCount = ArraySize(edwSupportedEnrichedIn);

boolean cdwCheckEnrichedIn(char *enriched)
/* return TRUE if value is allowed */
{
return (stringArrayIx(enriched, edwSupportedEnrichedIn, edwSupportedEnrichedInCount) >= 0);
}

struct cdwBedType cdwBedTypeTable[] = {
    {"bedLogR", 9, 1},
    {"bedRnaElements", 6, 3},
    {"bedRrbs", 9, 2},
    {"bedMethyl", 9, 2},
    {"narrowPeak", 6, 4},
    {"broadPeak", 6, 3},
};

int cdwBedTypeCount = ArraySize(cdwBedTypeTable);

struct cdwBedType *cdwBedTypeMayFind(char *name)
/* Return cdwBedType of given name, just return NULL if not found. */
{
int i;
for (i=0; i<cdwBedTypeCount; ++i)
    {
    if (sameString(name, cdwBedTypeTable[i].name))
        return &cdwBedTypeTable[i];
    }
return NULL;
}

struct cdwBedType *cdwBedTypeFind(char *name)
/* Return cdwBedType of given name.  Abort if not found */
{
struct cdwBedType *bedType = cdwBedTypeMayFind(name);
if (bedType == NULL)
    errAbort("Couldn't find bed format %s", name);
return bedType;
}

char *cdwAllowedTags[] = {
    "access",
    "age",
    "age_unit",
    "antibody",
    "assay",
    "assay_seq",
    "average_insert_size",
    "biomaterial_provider",
    "biosample_date",
    "cell_count",
    "cell_culture_type",
    "cell_enrichment",
    "cell_line",
    "cell_pair",
    "cell_type",
    "chrom",
    "consent",
    "control",
    "data_set_id",
    "differentiation",
    "disease",
    "disease_stage",
    "dna_concentration",
    "donor",
    "enriched_in",
    "experiment",
    "file",
    "file_part",
    "fluidics_chip",
    "format",
    "geo_sample",
    "geo_series",
    "inputs",
    "ips",
    "karyotype",
    "keywords",
    "lab",
    "lane",
    "life_stage",
    "md5",
    "meta",
    "multiplex_barcode",
    "ncbi_bio_project",
    "ncbi_bio_sample",
    "organ",
    "output",
    "paired_end",
    "passage_number",
    "pcr_cycles",
    "pipeline",
    "pmid",
    "ratio_260_280",
    "replicate",
    "rin",
    "rna_spike_in",
    "sample_label",
    "seq_library",
    "seq_library_prep",
    "seq_library_prep",
    "seq_sample",
    "sequencer",
    "sex",
    "sorting",
    "species",
    "sra_run",
    "sra_sample",
    "sra_study",
    "strain",
    "submission_date",
    "submitter",
    "t",
    "t_unit",
    "target_epitope",
    "target_gene",
    "title",
    "treatment",
    "ucsc_db",
    "update_date",
    "version",
    };

struct hash *cdwAllowedTagsHash()
/* Get hash of all allowed tags */
{
static struct hash *allowedHash = NULL;
if (allowedHash == NULL)
    {
    allowedHash = hashNew(7);
    int i;
    for (i=0; i<ArraySize(cdwAllowedTags); ++i)
	hashAdd(allowedHash, cdwAllowedTags[i], NULL);
    }
return allowedHash;
}

void cdwValidateTagName(char *tag)
/* Make sure that tag is one of the allowed ones. */
{
char *geoPrefix = "GEO_";
// If it's not a legal C symbol, don't let it be a tag
if (!isSymbolString(tag))
    errAbort("Bad tag symbol %s.", tag);
// First see if it is in hash of allowed tags.
struct hash *allowedHash = cdwAllowedTagsHash();
if (hashLookup(allowedHash, tag) != NULL)
    return;
// Otherwise see if it's one of the prefixes that allows anything afterwords 
else if (startsWith("lab_", tag) || startsWith("user_", tag) )
    {
    return;
    }
else if (startsWith(geoPrefix, tag) || startsWith("SRA_", tag))
    {
    // Generally just pass GEO_ and SRA_ tags through, but do check that
    // the case is what we expect to avoid duplicate symbol conflicts between
    // differently cased versions of GEO_ tags in particular.

    // We have a couple of built-in geo_ tags for the major GEO database identifiers.
    int tagLen = strlen(tag);
    char lowerTag[tagLen+1];
    strcpy(lowerTag, tag);
    tolowers(lowerTag);
    if (hashLookup(allowedHash, lowerTag))
        errAbort("Please change %s tag to %s", tag, lowerTag);

    // This will detect a misguided attempt to change case on bits after GEO_ that
    // bit us once.
    int geoPrefixSize = strlen(geoPrefix);
    if (!isupper(tag[geoPrefixSize]))
        errAbort("Looks like %s has been altered, expecting upper case letter after GEO_.", tag);
    return;
    }
// Otherwise see if it's one of our reserved but unimplemented things
else if (sameString("mixin", tag) || sameString("deprecated", tag) 
    || sameString("deprecated_acc", tag) || sameString("children", tag)
    || sameString("replaces_reason", tag) || sameString("replaces_file", tag))
    {
    errAbort("%s not implemented", tag);
    }
// Otherwise, nope, doesn't validate.
errAbort("Unknown tag '%s'", tag);
}

static struct hash *makeStringHash(char **array, int size)
/* Make a hash that contains all elements of array strings of given size */
{
struct hash *hash = hashNew(0);
int i;
for (i=0; i<size; ++i)
    hashAdd(hash, array[i], NULL);
return hash;
}

static struct hash *makeCvHash()
/* Turn a bunch of lists of words into hashes for fast lookup of whether
 * something is in a controlled vocabulary. */
{
/* These are just code generate things pasted in for now.  May do something more elegant and
 * prettier later */
char *assay[] =
{
"ATAC-seq",
"broad-ChIP-seq",
"DNAse-seq",
"exome",
"Hi-C",
"long-RNA-seq",
"methyl-ChIP-seq",
"narrow-ChIP-seq",
"RIP-seq",
"RRBS",
"short-RNA-seq",
"WGBS",
"WGS",
};
char *control[] =
{
"untreated",
"input",
"mock IP",
};
char *disease[] =
{
"acute promyelocytic leukemia",
"chronic myelogenous leukemia (CML)",
"DCM",
"HCM",
"LQT",
"prostate cancer",
"TNM stage IIA, grade 3, ductal carcinoma",
"LQT",
};
char *enriched_in[] =
{
"coding",
"exon",
"genome",
"intron",
"open",
"promoter",
"unknown",
"utr",
"utr3",
"utr5",
};
char *formats[] =
{
"bam",
"bam.bai",
"bed",
"bigBed",
"bigWig",
"cram",
"fasta",
"fastq",
"gtf",
"html",
"idat",
"jpg",
"pdf",
"rcc",
"text",
"vcf",
"unknown",
};
char *sequencer[] =
{
"Illumina HiSeq",
"Illumina HiSeq 2000",
"Illumina HiSeq 2500",
"Illumina HiSeq 3000",
"Illumina HiSeq 4000",
"Illumina HiSeq X Five",
"Illumina HiSeq X Ten",
"Illumina MiSeq",
"Illumina MiSeq Dx",
"Illumina MiSeq FGx",
"Illumina NextSeq 500",
"Illumina (unknown)",
"PacBio RS II",
"Ion Torrent Ion Proton",
"Ion Torrent Ion PGM",
"Ion Torrent Ion Chef",
"454 GS FLX+ ",
"454 GS Junior+ ",
"SN7001226",
"HiSeq G0821 SN605",
"HiSeq at Illumina 700422R",
"MiSeq G0823 M00361",
};
char *species[] =
{
"Homo sapiens",
"Mus musculus",
};
char *strain[] =
{
"C57BL/6",
"BALB/c",
"Sftpc-Cre-ER-T2A-rtta -/- teto-GFP-H2B +/-",
"Aqp5-Cre-ER +/- mtmg-tdTomato -/-",
};
char *target_epitope[] =
{
"H3K4Me1",
"H3K4Me3",
"H3K27Ac",
"H3K27Me3",
"5mC",
"5hmC",
};

struct hash *hash = hashNew(0);
hashAdd(hash, "assay", makeStringHash(assay, ArraySize(assay)));
hashAdd(hash, "control", makeStringHash(control, ArraySize(control)));
hashAdd(hash, "disease", makeStringHash(disease, ArraySize(disease)));
hashAdd(hash, "enriched_in", makeStringHash(enriched_in, ArraySize(enriched_in)));
hashAdd(hash, "formats", makeStringHash(formats, ArraySize(formats)));
hashAdd(hash, "sequencer", makeStringHash(sequencer, ArraySize(sequencer)));
hashAdd(hash, "species", makeStringHash(species, ArraySize(species)));
hashAdd(hash, "strain", makeStringHash(strain, ArraySize(strain)));
hashAdd(hash, "target_epitope", makeStringHash(target_epitope, ArraySize(target_epitope)));
return hash;
}

void cdwValidateTagVal(char *tag, char *val)
/* Make sure that tag is one of the allowed ones and that
 * val is compatible */
{
cdwValidateTagName(tag);
static struct hash *cvHash = NULL;
if (cvHash == NULL)
    cvHash = makeCvHash();
struct hash *hash = hashFindVal(cvHash, tag);
if (hash != NULL)
    if (!hashLookup(hash, val))
       errAbort("%s is not a valid value for tag %s\n", val, tag);
}

struct slPair *cdwFormatList()
/* Return list of formats.  The name of the list items are the format names.
 * The vals are short descriptions. */
{
static struct slPair *list = NULL;
if (list == NULL)
    {
    static char *array[] = 
	{
	"2bit Two bit per base DNA format",
	"bam Short read mapping format",
	"bed Genome browser compatible format for genes and other discrete elements",
	"bigBed	Compressed BED recommended for files with more than 100,000 elements",
	"bigWig	Compressed base by base signal graphs",
	"cram	More highly compressed short read format, currently with less validations",
	"fasta	Standard DNA format. Must be gzipped",
	"fastq	Illumina or sanger formatted short read format.  Must be gzipped",
	"gtf GFF family format for gene and transcript predictions",
	"html	A file in web page format",
	"idat	An Illumina IDAT file",
	"jpg JPEG image format",
	"pdf Postscripts common document format",
	"rcc A Nanostring RCC file",
	"text	Unicode 8-bit formatted text file",
	"vcf Variant call format",
	"kallisto_abundance abundance.txt file output from Kallisto containing RNA abundance info",
	"expression_matrix  Genes/transcripts are rows, samples are columns",
	"unknown	File is in  format unknown to the data hub.  No validations are applied",
	};
    int i;
    for (i=0; i<ArraySize(array); ++i)
        {
	char *buf = cloneString(array[i]);
	char *val = buf;
	char *tag = nextWord(&val);
	assert(tag != NULL && val != NULL);
	struct slPair *pair = slPairNew(tag, cloneString(val));
	slAddHead(&list, pair);
	freeMem(buf);
	}
    slReverse(&list);
    }
return list;
}


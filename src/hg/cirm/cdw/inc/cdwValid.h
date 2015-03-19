/* Things to do with CIRM validation. */

/* Copyright (C) 2013 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#ifndef CDWVALID_H
#define CDWVALID_H

char *cdwCalcValidationKey(char *md5Hex, long long fileSize);
/* calculate validation key to discourage faking of validation.  Do freeMem on 
 *result when done. */

void cdwValidateRcc(char *path);
/* Validate a nanostring rcc file. */

void cdwValidateIdat(char *path);
/* Validate illumina idat file. */

void cdwValidatePdf(char *path);
/* Make sure PDF really is PDF */

void cdwValidateCram(char *path);
/* Validate cram file. */

void cdwValidateJpg(char *path);
/* Check jpg file is really jpg */

void cdwValidateBamIndex(char *path);
/* Check .bam.bai really is index. */

boolean cdwIsGzipped(char *path);
/* Return TRUE if file at path starts with GZIP signature */

boolean cdwCheckEnrichedIn(char *enriched);
/* return TRUE if value is allowed */

struct cdwBedType 
/* Contain information about our beds with defined as files like narrowPeak. */
    {
    char *name;	    /* Base name without .as suffix */
    int bedFields;  /* Fields based on bed spec - come first */
    int extraFields; /* Beds past bed spec */
    };

extern struct cdwBedType cdwBedTypeTable[];	/* Table of supported bed types */
extern int cdwBedTypeCount;			/* Size of above table. */

struct cdwBedType *cdwBedTypeFind(char *name);
/* Return cdwBedType of given name.  Abort if not found */
    
struct cdwBedType *cdwBedTypeMayFind(char *name);
/* Return cdwBedType of given name, just return NULL if not found. */

extern char *cdwAllowedTags[];

struct hash *cdwAllowedTagsHash();
/* Get hash of all allowed tags */

boolean cdwValidateTagName(char *tag);
/* Make sure that tag is one of the allowed ones. */

boolean cdwValidateTagVal(char *tag, char *val);
/* Make sure that tag is one of the allowed ones and that
 * val is compatible */

struct slPair *cdwFormatList();
/* Return list of formats.  The name of the list items are the format names.
 * The vals are short descriptions. */

#endif /* CDWVALID_H */

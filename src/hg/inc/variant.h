/* variant.h -- a generic variant.  Meant to be capture information that's
 *              in VCF or pgSNP  */

#ifndef VARIANT_H
#define VARIANT_H

#include "pgSnp.h"

struct allele   // a single allele in a variant. 
    {
    struct allele *next;
    struct variant *variant;
    int length;
    char *sequence;
    };

struct variant   // a single variant
    {
    struct variant *next;  /* Next in singly linked list. */
    char *chrom;	/* Chromosome */
    unsigned chromStart;	/* Start position in chrom */
    unsigned chromEnd;	/* End position in chrom */
    unsigned numAlleles;   /* the number of alleles */
    struct allele *alleles;	/* alleles */
    };

struct variant *variantFromPgSnp(struct pgSnp *pgSnp);
/* convert pgSnp record to variant record */

struct allele  *alleleClip(struct allele *allele, int sx, int ex);
/* clip allele to be inside region defined by sx..ex.  Returns 
 * pointer to new allele which should be freed by alleleFree, or variantFree
 */

#endif /* VARIANT_H*/

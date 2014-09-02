/* freen - My Pet Freen. */

/* Copyright (C) 2014 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "dlist.h"
#include "hacTree.h"
#include "synQueue.h"
#include "pthreadWrap.h"
#include "pthreadDoList.h"

int gThreadCount = 5; /* NUmber of threads */


void usage()
{
errAbort("freen - test some hairbrained thing.\n"
         "usage:  freen output\n");
}

static struct optionSpec options[] = {
   {NULL, 0},
};

void rDump(struct hacTree *ht, int level, FILE *f)
/* Help dump out results */
{
spaceOut(f, level*2);
struct slDouble *el = (struct slDouble *)ht->itemOrCluster;
if (ht->left || ht->right)
    {
    fprintf(f, "(%g %g)\n", el->val, ht->childDistance);
    rDump(ht->left, level+1, f);
    rDump(ht->right, level+1, f);
    }
else
    fprintf(f, "%g\n", el->val);
}

double dblDistance(const struct slList *item1, const struct slList *item2, void *extraData)
{
struct slDouble *i1 = (struct slDouble *)item1;
struct slDouble *i2 = (struct slDouble *)item2;
double d = fabs(i1->val - i2->val);
uglyf("dblDistance %g %g = %g\n", i1->val, i2->val, d);
return d;
}

struct slList *dblMerge(const struct slList *item1, const struct slList *item2, 
    void *extraData)
{
struct slDouble *i1 = (struct slDouble *)item1;
struct slDouble *i2 = (struct slDouble *)item2;
double d = 0.5 * (i1->val + i2->val);
uglyf("dblMerge %g %g = %g\n", i1->val, i2->val, d);
return (struct slList *)slDoubleNew(d);
}

void freen(char *output)
/* Do something, who knows what really */
{
FILE *f = mustOpen(output, "w");
int i;

/* Make up list of random numbers */
struct slDouble *list = NULL;
for (i=0; i<10; ++i)
    {
    struct slDouble *el = slDoubleNew(rand()%100);
    slAddHead(&list, el);
    }
struct lm *lm = lmInit(0);
#ifdef OLD
struct hacTree *ht = hacTreeForCostlyMerges((struct slList *)list, lm, dblDistance, dblMerge, 
    NULL);
struct hacTree *ht = hacTreeFromItems((struct slList *)list, lm, dblDistance, dblMerge, NULL, NULL);
#endif /* OLD */

struct hacTree *ht = hacTreeMultiThread(10, (struct slList *)list, lm, dblDistance, dblMerge, NULL);

rDump(ht, 0, f);
carefulClose(&f);
}


int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 2)
    usage();
freen(argv[1]);
return 0;
}

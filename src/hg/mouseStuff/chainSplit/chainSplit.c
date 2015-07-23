/* chainSplit - Split chains up by target or query sequence. */

/* Copyright (C) 2011 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "portable.h"
#include "chain.h"


boolean splitOnQ = FALSE;
int lump = 0;
int maxOpenFileHandles = 600;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "chainSplit - Split chains up by target or query sequence\n"
  "usage:\n"
  "   chainSplit outDir inChain(s)\n"
  "options:\n"
  "   -q  - Split on query (default is on target)\n"
  "   -lump=N  Lump together so have only N split files.\n"
  "   -maxOpenFileHandles  - default: %d Set to a lower number if you get the shell error: Too many open files.\n",maxOpenFileHandles
  );
}

static struct optionSpec options[] = {
   {"q", OPTION_BOOLEAN},
   {"lump", OPTION_INT},
   {"maxOpenFileHandles", OPTION_INT},
   {NULL, 0},
};

char *lumpName(char *name)
/* Look for integer part of name (or hash it),  then do mod operation 
 * on it to assign to lump. */
{
char *s = name, c;
static char buf[32];
for (;;)
    {
    c = *s;
    if (c == 0)
	{
	safef(buf, sizeof(buf), "%03d", hashString(name) % lump);
	return buf;
	}
    if (isdigit(c))
        {
	int lumpIx = atoi(s) % lump;
	lumpIx %= lump;
	safef(buf, sizeof(buf), "%03d", lumpIx);
	return buf;
	}
    ++s;
    }
}

void chainSplit(char *outDir, int inCount, char *inFiles[])
/* chainSplit - Split chains up by target or query sequence. */
{
struct hash *hash = newHash(0);
struct hash *FileOpenedAlready = newHash(0);	/* we might have to open some files repeatedly. This hash makes sure the meta data is added to the beginning of the file only once */
int inIx;
char tpath[512];
FILE *meta ;
bool metaOpen = TRUE;
makeDir(outDir);
safef(tpath, sizeof(tpath), "%s/meta.tmp", outDir);
meta = mustOpen(tpath,"w");

for (inIx = 0; inIx < inCount; ++inIx)
    {
    struct lineFile *lf = lineFileOpen(inFiles[inIx], TRUE);
    struct chain *chain;
    FILE *f;
    lineFileSetMetaDataOutput(lf, meta);
    while ((chain = chainRead(lf)) != NULL)
        {
	char *name = (splitOnQ ? chain->qName : chain->tName);
	if (lump > 0)
	    name = lumpName(name);

	if ((f = hashFindVal(hash, name)) == NULL)
	    {
	    char path[512], cmd[512];
	    safef(path, sizeof(path),"%s/%s.chain", outDir, name);

       /* add meta data only when we open the file for the very first time */
       if (hashFindVal(FileOpenedAlready, name) == NULL) {
          if (metaOpen)
             fclose(meta);
          metaOpen = FALSE;
          safef(cmd,sizeof(cmd), "cat %s | sort -u > %s", tpath, path);
          mustSystem(cmd);
       }
	    f = mustOpen(path, "a");
		 /* only save the file handle in the hash if we have less than maxOpenFileHandles (ulimit) files open */
		 if (hash->elCount < maxOpenFileHandles) {
	       hashAdd(hash, name, f);
		 }
		 /* do not add meta data if we open this file again */
		 hashAdd(FileOpenedAlready, name, "1");	
	    }
	chainWrite(chain, f);
	/* close this file if it is not in the hash --> we have more than maxOpenFileHandles (ulimit) files open */
	if (hashFindVal(hash, name) == NULL) {
		fclose(f);
	}
	chainFree(&chain);
	}
    lineFileClose(&lf);
    }
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
splitOnQ = optionExists("q");
lump = optionInt("lump", 0);
maxOpenFileHandles = optionInt("maxOpenFileHandles", 600);

printf("Will use a maximum of %d open file handles\n", maxOpenFileHandles);
if (argc < 3)
    usage();
chainSplit(argv[1], argc-2, argv+2);
return 0;
}

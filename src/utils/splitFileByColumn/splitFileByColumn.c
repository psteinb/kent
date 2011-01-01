/* splitFileByColumn - Split text input into files named by column value. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "dystring.h"
#include "options.h"
#include "obscure.h"

/* Option variables: */
int col = 1;
char *headerText = NULL;
char *tailerText = NULL;
boolean chromDirs = FALSE;
char *ending = NULL;
boolean tab = FALSE;
int maxFiles = 64;

/* Make this global since we will need it when traversing a hash: */
char *outDirName = NULL;

#define MAX_COL_OFFSET 32

/* The file pool. */
struct hash *filePoolHash;
struct hash *createdFilesHash;
FILE **filePool;
char **filePoolName;

void usage()
/* Explain usage and exit. */
{
errAbort(
"splitFileByColumn - Split text input into files named by column value\n"
"usage:\n"
"   splitFileByColumn source outDir\n"
"options:\n"
"   -col=N      - Use the Nth column value (default: N=1, first column)\n"
"   -head=file  - Put head in front of each output\n"
"   -tail=file  - Put tail at end of each output\n"
"   -chromDirs  - Split into subdirs of outDir that are distilled from chrom\n"
"                 names, e.g. chr3_random -> outDir/3/chr3_random.XXX .\n"
"   -ending=XXX - Use XXX as the dot-suffix of split files (default: taken\n"
"                 from source).\n"
"   -tab        - Split by tab characters instead of whitespace.\n"
"   -maxFiles=N - Maximum number of output files to open at one time (default: %d)\n"
"Split source into multiple files in outDir, with each filename determined\n"
"by values from a column of whitespace-separated input in source.\n"
"If source begins with a header, you should pipe \"tail +N source\" to this\n"
"program where N is number of header lines plus 1, or use some similar\n"
"method to strip the header from the input.\n",
maxFiles
);
}

static struct optionSpec options[] = {
    {"col", OPTION_INT},
    {"head", OPTION_STRING},
    {"tail", OPTION_STRING},
    {"chromDirs", OPTION_BOOLEAN},
    {"ending", OPTION_STRING},
    {"tab", OPTION_BOOLEAN},
    {"maxFiles", OPTION_INT},
    {NULL, 0},
};


char *getFileName(char *baseName)
/* Return a complete path given dir and basename (add chromDir if applicable 
 * and ending). */
{
struct dyString *dy = dyStringNew(0);
dyStringPrintf(dy, "%s/", outDirName);
if (chromDirs)
    {
    char *trunc = cloneString(baseName);
    char *ptr = strstr(trunc, "_random");
    if (ptr != NULL)
	*ptr = '\0';
    if (startsWith("chr", trunc))
	strcpy(trunc, trunc+strlen("chr"));
    dyStringPrintf(dy, "%s/", trunc);
    freeMem(trunc);
    makeDirs(dy->string);
    }
dyStringPrintf(dy, "%s%s", baseName, ending);
return dyStringCannibalize(&dy);
}

void allocFilePool()
/* Allocate the file pool. */
{
filePoolHash = hashNew(0);
createdFilesHash = hashNew(0);
AllocArray(filePool, maxFiles);
AllocArray(filePoolName, maxFiles);
}

void freeFilePool()
/* Free the file pool. */
{
freez(&filePool);
freez(&filePoolName);
hashFree(&filePoolHash);
hashFree(&createdFilesHash);
}

FILE *openFileFromPool(char *baseName)
/* Open a file using one of the pool of file pointers
   to ensure not to exceed the OS limit of open files.
   
   If the file is already opened, then just return it.
   If it is not yet open, then grab a file pointer and open it. */
{
FILE *result;
static int fileCount = 0;
char *fileName;
int poolIx;

result = hashFindVal(filePoolHash, baseName);
if (result == NULL) /* not yet open */
    {
    fileName = getFileName(baseName);
    if (fileCount >= maxFiles) /* all file pointers are used */
        {
        /* Choose a random file to evict. */
        poolIx = (rand() % maxFiles);

        verbose(3, "Temporarily closing %s.\n", filePoolName[poolIx]);
        carefulClose(&(filePool[poolIx]));
        hashRemove(filePoolHash, filePoolName[poolIx]);
        }
    else
        poolIx = fileCount;

    /* Determine whether to open the file for write (if its the
       first time its been opened) or append. */
    struct hashEl *hel = hashLookup(createdFilesHash, baseName);
    if (hel == NULL)
        {
        verbose(2, "Creating %s.\n", baseName);
        result = mustOpen(fileName, "w");
        fileCount++;
        if (fileCount >= 1000)
            warn("Created more than 1000 output files.\n");
        hel = hashAddInt(createdFilesHash, baseName, 1);
        }
    else
        {
        verbose(3, "Reopening %s.\n", filePoolName[poolIx]);
        result = mustOpen(fileName, "a");
        }

    /* Record that the file is open and in the pool. */
    filePool[poolIx] = result;
    filePoolName[poolIx] = hel->name;
    hashAdd(filePoolHash, filePoolName[poolIx], result);

    freez(&fileName);
    }

return result;
}

void addTailsAndClose()
/* Add the tail to each file and close all open files. */
{
struct hashCookie cookie;
struct hashEl *hel;
FILE *f;
int i;

/* Write the tail to each file, if appropriate. */
if (tailerText != NULL)
    {
    cookie = hashFirst(createdFilesHash);
    while ((hel = hashNext(&cookie)) != NULL)
        {
        f = openFileFromPool(hel->name);
        fprintf(f, "%s", tailerText);
        }
    }

for (i = 0; i < maxFiles; i++)
    carefulClose(&(filePool[i]));
}


void splitFileByColumn(char *inFileName)
/* splitFileByColumn - Split text input into files named by column value. */
{
struct lineFile *lf = lineFileOpen(inFileName, TRUE);
char *line = NULL;

if (ending == NULL)
    {
    char *ptr = inFileName + strlen(inFileName) - 1;
    while (ptr > inFileName && *ptr != '/')
	ptr--;
    ptr = strchr(ptr, '.');
    if (ptr == NULL)
	ending = cloneString("");
    else
	ending = cloneString(ptr);
    }
else if (ending[0] != '.')
    {
    /* If -ending is given without initial ., prepend .: */
    size_t deSize = sizeof(char) * strlen(ending) + 2;
    char *dotEnding = cloneStringZ(ending, deSize);
    safef(dotEnding, deSize, ".%s", ending);
    ending = dotEnding;
    }

makeDirs(outDirName);
allocFilePool();
while (lineFileNext(lf, &line, NULL))
    {
    char *lineClone = cloneString(line);
    char *words[MAX_COL_OFFSET+1];
    int wordCount = 0;
    char *colVal = NULL;
    FILE *f = NULL;
    
    if (tab)
	wordCount = chopTabs(lineClone, words);
    else
	wordCount = chopLine(lineClone, words);
    if (wordCount < col)
	errAbort("Too few columns line %d of %s (%d, need at least %d)",
		 lf->lineIx, lf->fileName, wordCount, col);

    colVal = words[col-1];
    f = openFileFromPool(colVal);
    fprintf(f, "%s\n", line);
    freeMem(lineClone);
    }
lineFileClose(&lf);
addTailsAndClose();
freeFilePool();
freez(&ending);
}

int main(int argc, char *argv[])
/* Process command line. */
{
char *headFileName = NULL, *tailFileName = NULL;
optionInit(&argc, argv, options);
col = optionInt("col", col);
headFileName = optionVal("head", headFileName);
tailFileName = optionVal("tail", tailFileName);
chromDirs = optionExists("chromDirs");
ending = optionVal("ending", ending);
tab = optionExists("tab");
maxFiles = optionInt("maxFiles", maxFiles);
if (maxFiles <= 0)
    errAbort("-maxFiles must be a positive integer: \"%d\"", maxFiles);
if (headFileName != NULL)
    {
    readInGulp(headFileName, &headerText, NULL);
    }
if (tailFileName != NULL)
    {
    readInGulp(tailFileName, &tailerText, NULL);
    }
if (argc != 3)
    usage();
if (col > MAX_COL_OFFSET)
    errAbort("Sorry, max -col offset currently supported is %d",
	     MAX_COL_OFFSET);
outDirName = argv[2];
splitFileByColumn(argv[1]);
optionFree();
return 0;
}

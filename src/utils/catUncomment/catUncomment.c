/* catUncomment - Concatenate input removing lines that start with '#'. */
#include "common.h"
#include "linefile.h"
#include "options.h"


/* command line options */
static struct optionSpec optionSpecs[] =
{
    {"outFile", OPTION_STRING},
    {"append", OPTION_BOOLEAN},
    {NULL, 0},
};

static char *outFile;
static boolean append;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "catUncomment - Concatenate input, removing lines that start with '#'\n"
  "Output goes to stdout\n"
  "usage:\n"
  "   catUncomment [options] file(s)\n"
  "\n"
  "options:\n"
  "   -outFile=s   Output file (DEFAULT: stdout)\n"
  "   -append      Append to outFile instead of overwriting\n");
}

void catUncomment(int inCount, char *inNames[])
/* catUncomment - Concatenate input removing lines that start with '#'. */
{
struct lineFile *lf;
char *fileName;
char *line;
int i, lineSize;
FILE *fOut;

if (append)
    fOut = mustOpen(outFile, "a");
else
    fOut = mustOpen(outFile, "w");

for (i=0; i<inCount; ++i)
    {
    fileName = inNames[i];
    lf = lineFileOpen(fileName, FALSE);
    while (lineFileNext(lf, &line, &lineSize))
        if (line[0] != '#')
            mustWrite(fOut, line, lineSize);
    lineFileClose(&lf);
    }
carefulClose(&fOut);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, optionSpecs);
if (argc < 2)
    usage();

outFile = optionVal("outFile", "stdout");
append = optionExists("append");
catUncomment(argc-1, argv+1);

/* Free dynamically allocated resources. */
optionFree();

return 0;
}

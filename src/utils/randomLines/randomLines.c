/* randomLines - Pick out random lines from file. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "obscure.h"


int seed;
int salt;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "randomLines - Pick out random lines from file\n"
  "usage:\n"
  "   randomLines inFile count outFile\n"
  "options:\n"
  "   -seed=N - Set seed used for randomizing, useful for debugging.\n"
  "   -decomment - remove blank lines and those starting with #\n"
  "   -salt=N - Add this amount of salt to the random number seed (useful for cluster runs)\n"
  );
}

static struct optionSpec options[] = {
   {"seed", OPTION_INT},
   {"decomment", OPTION_BOOLEAN},
   {"salt", OPTION_INT},
   {NULL, 0},
};

boolean decomment = FALSE;

void randomLines(char *inName, int count, char *outName)
/* randomLines - Pick out random lines from file. */
{
srand(seed+salt);

/* Read all lines of input and put into an array. */
struct slName *slPt, *slList= readAllLines(inName);
int lineCount = slCount(slList);
char **lineArray;
AllocArray(lineArray, lineCount);
int i = 0, validLineCount = 0;
for (slPt=slList; slPt != NULL; slPt = slPt->next)
    {
	if (!decomment && slPt->name)
        {
        lineArray[i++] = slPt->name;
        ++validLineCount;
        }
    else
        {
        // Only add non-blank and non-comment lines
        char *s, c;
        s = skipLeadingSpaces(slPt->name);
        c = s[0];
        if (c != 0 && c != '#')
            {
            lineArray[i++] = slPt->name;
            ++validLineCount;
            }
        }
    }

int maxCount = validLineCount;
if (count > maxCount)
    errAbort("%s has %d eligible lines within its %d total lines.\n"
             "You asked for %d. Sorry.",
    	inName, maxCount, lineCount, count);

FILE *f = mustOpen(outName, "w");
int outCount = 0;
char *tmp;
while (outCount < count)
    {
    // Randomize in place only the number of elements you need to print,
    // using the method from CLRS 2001 pg. 103
    int randomIx = outCount + rand()%(validLineCount-outCount);
    tmp = lineArray[outCount];
    lineArray[outCount] = lineArray[randomIx];
    lineArray[randomIx] = tmp;
    ++outCount;
    }

// print out randomized results
for (outCount = 0; outCount < count; outCount++)
    {
    fprintf(f, "%s\n", lineArray[outCount]);
    }
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 4 || !isdigit(argv[2][0]))
    usage();
seed = optionInt("seed", (int)time(NULL));
salt = optionInt("salt", 0);
decomment = optionExists("decomment");
randomLines(argv[1], atoi(argv[2]), argv[3]);
return 0;
}

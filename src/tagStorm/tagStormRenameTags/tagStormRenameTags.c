/* tagStormRenameTags - rename tags in a tagStorm file. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "obscure.h"

void usage()
/* Explain usage and exit. */
{
errAbort(
  "tagStormRenameTags - rename tags in a tagStorm file\n"
  "usage:\n"
  "   tagStormRenameTags in.tags 2column.tab out.tags\n"
  "where in.tags is the input tagstorm file, out.tags the output\n"
  "and 2column.tab is a tab or white space separated file where the\n"
  "first column is the old tag name, and the second the new tag name.\n"
  "options:\n"
  "   -xxx=XXX\n"
  );
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {NULL, 0},
};

void tagStormRenameTags(char *inName, char *twoColName, char *outName)
/* tagStormRenameTags - rename tags in a tagStorm file. */
{
// Open up input as a lineFile
struct lineFile *lf = lineFileOpen(inName, TRUE);

// Read twoColName into a hash
struct hash *hash = hashTwoColumnFile(twoColName);

// Open up output as a FILE
FILE *f = mustOpen(outName, "w");

// Loop through input lines.  Write them out unchanged unless first real word matches
// something in our hash.
char *line;
while (lineFileNext(lf, &line, NULL))
    {
    // See if a blank line or starts with # - these are comments and record separators passed through
    char *s = skipLeadingSpaces(line);
    int leadingSpaces = s - line;
    char c = s[0];
    if (c == '#' || c == 0)
         fprintf(f, "%s\n", line);
    else
        {
	// Parse out first word
	char *tag = nextWord(&s);
	char *newTag = hashFindVal(hash, tag);
	if (newTag == NULL)
	     newTag = tag;   // Just replace ourselves with ourselves
        mustWrite(f, line, leadingSpaces);
	fprintf(f, "%s %s\n", newTag, emptyForNull(s));
	}
    }

// Close output
carefulClose(&f);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 4)
    usage();
tagStormRenameTags(argv[1], argv[2], argv[3]);
return 0;
}

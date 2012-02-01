/* hubPublicCheck - checks that the labels in hubPublic match what is in the hub labels. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "hdb.h"
#include "udc.h"
#include "options.h"
#include "hubConnect.h"
#include "errCatch.h"
#include "trackHub.h"
#include "dystring.h"


void usage()
/* Explain usage and exit. */
{
errAbort(
  "hubPublicCheck - checks that the labels in hubPublic match what is in the hub labels\n"
  "   outputs SQL statements to put the table into compliance\n"
  "usage:\n"
  "   hubPublicCheck tableName \n"
  "options:\n"
  "   -udcDir=/dir/to/cache - place to put cache for remote bigBed/bigWigs\n"
  "   -addHub=url           - output statments to add url to table\n"
  );
}

static struct optionSpec options[] = {
   {"udcDir", OPTION_STRING},
   {"addHub", OPTION_STRING},
   {NULL, 0},
};

int hubPublicCheck(char *table)
/* hubPublicCheck - checks that the labels in hubPublic match what is in the hub labels. */
{
struct sqlConnection *conn = hConnectCentral();
char query[512];
safef(query, sizeof(query), "select hubUrl, shortLabel,longLabel from %s", 
	table); 
struct sqlResult *sr = sqlGetResult(conn, query);
char **row;
int differences = 0;

while ((row = sqlNextRow(sr)) != NULL)
    {
    char *url = row[0], *shortLabel = row[1], *longLabel = row[2]; 
    struct errCatch *errCatch = errCatchNew();
    boolean gotWarning = FALSE;
    struct trackHub *tHub = NULL;
    
    if (errCatchStart(errCatch))
	tHub = trackHubOpen(url, "1"); 
    errCatchEnd(errCatch);
    if (errCatch->gotError)
	{
	gotWarning = TRUE;
	warn("%s", errCatch->message->string);
	}
    errCatchFree(&errCatch);

    if (gotWarning)
	{
	continue;
	}

    if (!sameString(shortLabel, tHub->shortLabel))
	{
	differences++;

	printf("update %s set shortLabel=\"%s\" where hubUrl=\"%s\";\n",table, tHub->shortLabel, url);
	}
    if (!sameString(longLabel, tHub->longLabel))
	{
	differences++;

	printf("update %s set longLabel=\"%s\" where hubUrl=\"%s\";\n",table, tHub->longLabel, url);
	}
    }
return differences;
}

int hubPublicAdd(char *table, char *url)
/* hubPublicAdd -- add url to hubPublic table */
{
struct errCatch *errCatch = errCatchNew();
boolean gotWarning = FALSE;
struct trackHub *tHub = NULL;
int dbCount = 0;

if (errCatchStart(errCatch))
    tHub = trackHubOpen(url, "1"); 
errCatchEnd(errCatch);
if (errCatch->gotError)
    {
    gotWarning = TRUE;
    warn("%s", errCatch->message->string);
    }
errCatchFree(&errCatch);

if (gotWarning)
    return 1;

struct hashEl *hel;
struct hashCookie cookie = hashFirst(tHub->genomeHash);
struct dyString *dy = newDyString(1024);

while ((hel = hashNext(&cookie)) != NULL)
    {
    dbCount++;
    dyStringPrintf(dy, "%s,", hel->name);
    }

printf("insert into %s (hubUrl,shortLabel,longLabel,registrationTime,dbCount,dbList) values (\"%s\",\"%s\", \"%s\",now(),%d, \"%s\");\n",
    table, url, tHub->shortLabel, tHub->shortLabel, dbCount, dy->string); 

return 0;
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 2)
    usage();
udcSetDefaultDir(optionVal("udcDir", udcDefaultDir()));

char *hubUrl = optionVal("addHub", NULL);

if (hubUrl != NULL)
    return hubPublicAdd(argv[1],hubUrl);
else
    return hubPublicCheck(argv[1]);
}

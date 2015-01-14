/* cdwRunOnIds - Run a cdw command line program (one that takes startId endId as it's two parameters) for a range of ids, putting it on cdwJob queue.. */

/* Copyright (C) 2013 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "jksql.h"
#include "cdw.h"
#include "cdwLib.h"

char *runTable = "cdwTempJob";

void usage()
/* Explain usage and exit. */
{
errAbort(
  "cdwRunOnIds - Run a cdw command line program (one that takes startId endId as it's two parameters) for a range of ids, putting it on cdwJob queue.\n"
  "usage:\n"
  "   cdwRunOnIds program 'queryString'\n"
  "Where queryString is a SQL command that should return a list of fileIds.\n"
  "Example\n"
  "   cdwRunOnIds cdwFixQualScore 'select fileId from cdwFastqFile where qualMean < 0'"
  "options:\n"
  "   -runTable=%s -job table to use\n"
  , runTable
  );
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {"runTable", OPTION_STRING},
   {NULL, 0},
};

void cdwRunOnIds(char *program, char *queryString)
/* cdwRunOnIds - Run a cdw command line program (one that takes startId endId as it's two parameters) for a range of ids, 
 * putting it on cdwJob queue. */
{
struct sqlConnection *conn = cdwConnectReadWrite();
struct slName *id, *idList = sqlQuickList(conn, queryString);
for (id = idList; id != NULL; id = id->next)
    {
    char query[512];
    sqlSafef(query, sizeof(query), "insert into %s (commandLine) values ('%s %s %s')",
	runTable, program, id->name, id->name);
    sqlUpdate(conn, query);
    }

}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 3)
    usage();
runTable = optionVal("runTable", runTable);
cdwRunOnIds(argv[1], argv[2]);
return 0;
}

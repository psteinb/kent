/* edwRunner - Runs pending jobs in edwJob table.. */
#include <sys/wait.h>
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "jksql.h"
#include "portable.h"
#include "obscure.h"
#include "encodeDataWarehouse.h"
#include "edwLib.h"

void usage()
/* Explain usage and exit. */
{
errAbort(
  "edwRunner - Runs pending jobs in edwJob table.\n"
  "REPLACED BY edwRunDaemon\n"
  "usage:\n"
  "   edwRunner now\n"
  "options:\n"
  "   -xxx=XXX\n"
  );
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {NULL, 0},
};

#define maxThreadCount 5

struct runner
/* Keeps track of running process. */
    {
    int pid;	/* Process ID or 0 if none */
    char *errFileName;   /* Standard error file for this process */
    struct edwJob *job;	 /* The job we are running */
    };

struct runner runners[maxThreadCount];  /* Job table */
int curThreads = 0;

void finishRun(struct runner *run, int status)
/* Finish up job. Copy results into database */
{
/* Get job and fill in two easy fields. */
struct edwJob *job = run->job;
job->endTime = edwNow();
job->returnCode = status;

/* Read in stderr */
size_t errorMessageSize;
readInGulp(run->errFileName, &job->stderr, &errorMessageSize);
remove(run->errFileName);

/* Update database with job results */
struct dyString *dy = dyStringNew(0);
dyStringPrintf(dy, "update edwJob set endTime=%lld, stderr='", job->endTime);
char *escaped = sqlEscapeString(trimSpaces(job->stderr));
dyStringAppend(dy, escaped);
dyStringPrintf(dy, "', returnCode=%d where id=%u", job->returnCode, job->id);
struct sqlConnection *conn = sqlConnect(edwDatabase);
sqlUpdate(conn, dy->string);
sqlDisconnect(&conn);
freez(&escaped);
dyStringFree(&dy);

/* Free up runner resources. */
freez(&run->errFileName);
edwJobFree(&job);
run->pid = 0;
--curThreads;
}

struct runner *checkOnChildRunner(boolean doWait)
/* See if a child has finished and optionally wait for it.  Return
 * a pointer to slot child has freed if it has finished. */
{
if (curThreads > 0)
    {
    int status = 0;
    int waitFlags = (doWait ? 0 : WNOHANG);
    int child = waitpid(-1, &status, waitFlags);
    if (child < 0)
	errnoAbort("Couldn't wait");
    if (child != 0)
	{
	int i;
	for (i=0; i<maxThreadCount; ++i)
	    {
	    struct runner *run = &runners[i];
	    if (run->pid == child)
		{
		finishRun(run, status);
		return run;
		}
	    }
	internalErr();
	}
    }
return NULL;
}

struct runner *waitOnChildRunner()
/* Wait for child to finish. */
{
return checkOnChildRunner(TRUE);
}

struct runner *findFreeRunner()
/* Return free runner if there is one,  otherwise wait until there is. */
{
int i;
if (curThreads >= maxThreadCount)
    {
    return waitOnChildRunner();
    }
else
    {
    for (i=0; i<maxThreadCount; ++i)
        {
	struct runner *run = &runners[i];
	if (run->pid == 0)
	    return run;
	}
    }
internalErr();  // Should not get here
return NULL;
}

void runJob(struct runner *runner, struct edwJob *job)
/* Fork off and run job. */
{
/* Create stderr file for child  as a temp file */
char tempFileName[PATH_LEN];
safef(tempFileName, PATH_LEN, "%sedwRunnerXXXXXX", edwTempDir());
int errFd = mkstemp(tempFileName);
if (errFd < 0)
    errnoAbort("Couldn't open temp file %s", tempFileName);

++curThreads;
job->startTime = edwNow();

/* Save start time to database. */
struct sqlConnection *conn = sqlConnect(edwDatabase);
char query[256];
safef(query, sizeof(query), "update edwJob set startTime=%lld where id=%lld", 
    job->startTime, (long long)job->id);
sqlUpdate(conn, query);
sqlDisconnect(&conn);

runner->job = job;
int childId;
if ((childId = mustFork()) == 0)
    {
    /* We be child side - execute command using system call */
    if (dup2(errFd, STDERR_FILENO) < 0)
        errnoAbort("Can't dup2 stderr to %s", tempFileName);
    int status = system(job->commandLine);
    exit(status);
    }
else
    {
    /* We be parent - just fill in job info */
    close(errFd);
    runner->pid = childId;
    runner->errFileName = cloneString(tempFileName);
    }
}

void edwRunner(char *now)
/* edwRunner - Runs pending jobs in edwJob table.. */
{
struct edwJob *jobList = NULL;
long long timeBetweenChecks = 30;
long long lastCheck = edwNow() - timeBetweenChecks;
long long lastId = 0;
for (;;)
    {
    verbose(2, "slCount(jobList)=%d, curThreads %d\n", slCount(jobList), curThreads);
    /* Three main cases each iteration: run a job, wait for a job, or look for new jobs.  If we
     * can't do any of these we go to sleep for a while.  */
    if (jobList != NULL && curThreads < maxThreadCount)  // Open slot and a job to put in it!
        {
	struct runner *runner = findFreeRunner();
	struct edwJob *job = jobList;
	jobList = jobList->next;
	runJob(runner, job);
	}
    else if (curThreads == maxThreadCount)   // Are all slots being used, then wait for one to open
        {
	waitOnChildRunner();
	}
    else if (edwNow() - lastCheck >= timeBetweenChecks)  // Can check database for jobs yet?
        {
	/* Would be nice to have a way to wait on DB rather than poll it like this. */
	struct sqlConnection *conn = sqlConnect(edwDatabase);
	char query[256];
	safef(query, sizeof(query), 
	    "select * from edwJob where startTime = 0 and id > %lld order by id", lastId);
	struct edwJob *newJobs = edwJobLoadByQuery(conn, query);
	int newJobCount = slCount(newJobs);
	if (newJobCount > 0)
	    {
	    struct edwJob *lastJob = slLastEl(newJobs);
	    lastId = lastJob->id;
	    }
	verbose(2, "Got %d new jobs\n", newJobCount);
	sqlDisconnect(&conn);
	jobList = slCat(jobList, newJobs);
	lastCheck = edwNow();
	}
    else if (checkOnChildRunner(FALSE) != NULL)	// See if a child has finished
        {
	}
    else
        {
	sleep(timeBetweenChecks);
	}
    }
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 2)
    usage();
edwRunner(argv[1]);
return 0;
}

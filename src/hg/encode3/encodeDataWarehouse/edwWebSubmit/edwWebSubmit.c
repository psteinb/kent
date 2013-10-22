/* edwWebSubmit - A small self-contained CGI for submitting data to the ENCODE Data Warehouse.. */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "dystring.h"
#include "errabort.h"
#include "cheapcgi.h"
#include "htmshell.h"
#include "obscure.h"
#include "jksql.h"
#include "encodeDataWarehouse.h"
#include "edwLib.h"
#include "portable.h"
#include "net.h"
#include "paraFetch.h"

char *userEmail;	/* Filled in by authentication system. */

void usage()
/* Explain usage and exit. */
{
errAbort(
  "edwWebSubmit - A small self-contained CGI for submitting data to the ENCODE Data Warehouse.\n"
  "usage:\n"
  "   edwWebSubmit cgiVar=value cgiVar2=value2\n"
  );
}

void logIn()
/* Put up name.  No password for now. */
{
printf("Welcome to the prototype ENCODE Data Warehouse submission site.<BR>");
printf("Please sign in via Persona");
printf("<INPUT TYPE=BUTTON NAME=\"signIn\" VALUE=\"sign in\" id=\"signin\">");
}

void getUrl(struct sqlConnection *conn)
/* Put up URL. */
{
edwMustGetUserFromEmail(conn, userEmail);
printf("Please enter a URL for a validated manifest file:<BR>");
printf("URL ");
cgiMakeTextVar("url", emptyForNull(cgiOptionalString("url")), 80);
cgiMakeButton("submitUrl", "submit");
printf("<BR>\n");
cgiMakeCheckBox("update", FALSE);
printf(" Update information associated with files that have already been uploaded.");
printf("<BR>Submission by %s", userEmail);
edwPrintLogOutButton();
}

static char *stopButtonName = "stopUpload";

long long paraFetchedSoFar(char *path)
/* Return amount fetched so far. */
{
struct parallelConn *pcList = NULL;
char *url = NULL;
off_t fileSize = 0;
char *dateString = NULL;
off_t totalDownloaded = 0;
paraFetchReadStatus(path, &pcList, &url, &fileSize, &dateString, &totalDownloaded);
return totalDownloaded;
}

void monitorSubmission(struct sqlConnection *conn)
/* Write out information about submission. */
{
char *url = trimSpaces(cgiString("url"));
cgiMakeHiddenVar("url", url);
struct edwSubmit *sub = edwMostRecentSubmission(conn, url);
time_t startTime = 0, endTime = 0, endUploadTime = 0;
if (sub == NULL)
    {
    int posInQueue = edwSubmitPositionInQueue(conn, url, NULL);
    if (posInQueue == 0)
	printf("%s is first in the submission queue, but upload has not started<BR>\n", url);
    else if (posInQueue > 0)
        printf("%s is in submission queue with %d submissions ahead of it<BR>\n", url, posInQueue);
    else
	{
        printf("%s status unknown.", url);
	}
    }
else
    {
    startTime = sub->startUploadTime;
    endUploadTime = sub->endUploadTime;
    endTime = (endUploadTime ? endUploadTime : edwNow());
    int timeSpan = endTime - startTime;
    long long thisUploadSize = sub->byteCount - sub->oldBytes;
    long long curSize = 0;  // Amount of current file we know we've transferred.

    /* Print title letting them know if upload is done or in progress. */
    printf("<B>Submission by %s is ", userEmail);
    if (!isEmpty(sub->errorMessage))
	{
	if (endUploadTime == 0)
	    printf("having problems...");
	else
	    printf("stopped by uploader request.");
	}
    else if (endUploadTime != 0)  
	{
        printf("uploaded.");
	}
    else
	printf("in progress...");
    printf("</B><BR>\n");

    /* Print URL and how far along we are at the file level. */
    if (!isEmpty(sub->errorMessage))
	{
	printf("<B>error:</B> %s<BR>\n", sub->errorMessage);
	cgiMakeButton("getUrl", "try submission again");
	printf("<BR>");
	}
    printf("<B>url:</B> %s<BR>\n", sub->url);
    printf("<B>files count:</B> %d<BR>\n", sub->fileCount);
    if (sub->oldFiles > 0)
	printf("<B>files already in warehouse:</B> %u<BR>\n", sub->oldFiles);
    if (sub->metaChangeCount > 0)
        printf("<B>old files with new tags in this submission</B> %d<BR>", sub->metaChangeCount);
    if (sub->oldFiles != sub->fileCount)
	{
	printf("<B>files transferred:</B> %u<BR>\n", sub->newFiles);
	printf("<B>files remaining:</B> %u<BR>\n", sub->fileCount - sub->oldFiles - sub->newFiles);
	}

    /* Report validation status */
    printf("<B>new files validated:</B> %u of %u<BR>\n", edwSubmitCountNewValid(sub, conn), 
	sub->newFiles);

    /* Print error message, and in case of error skip file-in-transfer info. */
    if (isEmpty(sub->errorMessage))
	{
	/* If possible print information about file en route */
	if (endUploadTime == 0)
	    {
	    struct edwFile *ef = edwFileInProgress(conn, sub->id);
	    if (ef != NULL)
		{
		char path[PATH_LEN];
		safef(path, sizeof(path), "%s%s", edwRootDir, ef->edwFileName);
		if (ef->endUploadTime > 0)
		    curSize = ef->size;
		else
		    curSize = paraFetchedSoFar(path);
		printf("<B>file in route:</B> %s",  ef->submitFileName);
		printf(" (%d%% transferred)<BR>\n", (int)(100.0 * curSize / ef->size));
		}
	    }
	}
    /* Report bytes transferred */
    long long transferredThisTime = curSize + sub->newBytes;
    printf("<B>total bytes transferred:</B> ");
    long long totalTransferred = transferredThisTime + sub->oldBytes;
    printLongWithCommas(stdout, totalTransferred);
    printf(" of ");
    printLongWithCommas(stdout, sub->byteCount);
    if (sub->byteCount != 0)
	printf(" (%d%%)<BR>\n", (int)(100.0 * totalTransferred / sub->byteCount));
    else
        printf("<BR>\n");

    /* Report transfer speed if possible */
    if (isEmpty(sub->errorMessage))
	{
	if (timeSpan > 0)
	    {
	    printf("<B>transfer speed:</B> ");
	    printLongWithCommas(stdout, (curSize + sub->newBytes)/timeSpan);
	    printf(" bytes/sec<BR>\n");
	    }

	/* Report start time  and duration */
	printf("<B>submission started:</B> %s<BR>\n", ctime(&startTime));
	struct dyString *duration = edwFormatDuration(timeSpan);

	/* Try and give them an ETA if we aren't finished */
	if (endUploadTime == 0 && timeSpan > 0)
	    {
	    printf("<B>time so far:</B> %s<BR>\n", duration->string);
	    double bytesPerSecond = (double)transferredThisTime/timeSpan;
	    long long bytesRemaining = thisUploadSize - curSize - sub->newBytes;
	    if (bytesPerSecond > 0)
		{
		long long estimatedFinish = bytesRemaining/bytesPerSecond;
		struct dyString *eta = edwFormatDuration(estimatedFinish);
		printf("<B>estimated finish in:</B> %s<BR>\n", eta->string);
		}
	    }
	else
	    {
	    printf("<B>submission time:</B> %s<BR>\n", duration->string);
	    cgiMakeButton("getUrl", "submit another data set");
	    }
	}
    }
cgiMakeButton("monitor", "refresh status");
if (endUploadTime == 0 && isEmpty(sub->errorMessage))
    cgiMakeButton(stopButtonName, "stop upload");
printf(" <input type=\"button\" value=\"browse submissions\" "
       "onclick=\"window.location.href='edwWebBrowse';\">\n");

edwPrintLogOutButton();
}

void submitUrl(struct sqlConnection *conn)
/* Submit validated manifest if it is not already in process.  Show
 * progress once it is in progress. */
{
/* Parse email and URL out of CGI vars. Do a tiny bit of error checking. */
char *url = trimSpaces(cgiString("url"));
if (!stringIn("://", url))
    errAbort("%s doesn't seem to be a valid URL, no '://'", url);

/* Do some reality checks that email and URL actually exist. */
edwMustGetUserFromEmail(conn, userEmail);
int sd = netUrlMustOpenPastHeader(url);
close(sd);

edwAddSubmitJob(conn, userEmail, url, cgiBoolean("update"));

/* Give the system a half second to react and then put up status info about submission */
sleep1000(1000);
monitorSubmission(conn);
}

void stopUpload(struct sqlConnection *conn)
/* Try and stop current upload. */
{
char *url = trimSpaces(cgiString("url"));
cgiMakeHiddenVar("url", url);
struct edwSubmit *sub = edwMostRecentSubmission(conn, url);
if (sub == NULL)
    {
    /* Submission hasn't happened yet - remove it from job queue. */
    unsigned edwSubmitJobId = 0;
    int posInQueue = edwSubmitPositionInQueue(conn, url, &edwSubmitJobId);
    if (posInQueue >= 0)
        {
	char query[256];
	sqlSafef(query, sizeof(query), "delete from edwSubmitJob where id=%u", edwSubmitJobId);
	sqlUpdate(conn, query);
	printf("Removed submission from %s from job queue\n", url);
	}
    }
else
    {
    char query[256];
    sqlSafef(query, sizeof(query), 
	"update edwSubmit set errorMessage='Stopped by user.' where id=%u", sub->id);
    sqlUpdate(conn, query);
    }
monitorSubmission(conn);
}


static void localWarn(char *format, va_list args)
/* A little warning handler to override the one with the button that goes nowhere. */
{
printf("<B>Error:</B> ");
vfprintf(stdout, format, args);
printf("<BR>Please use the back button on your web browser, correct the error, and resubmit.");
}

void doMiddle()
/* doMiddle - put up middle part of web page, not including http and html headers/footers */
{
pushWarnHandler(localWarn);
printf("<FORM ACTION=\"../cgi-bin/edwWebSubmit\" METHOD=GET>\n");
struct sqlConnection *conn = edwConnectReadWrite(edwDatabase);
userEmail = edwGetEmailAndVerify();
if (userEmail == NULL)
    logIn();
else if (cgiVarExists(stopButtonName))
    stopUpload(conn);
else if (cgiVarExists("submitUrl"))
    submitUrl(conn);
else if (cgiVarExists("monitor"))
    monitorSubmission(conn);
else
    getUrl(conn);
printf("</FORM>");
}

int main(int argc, char *argv[])
/* Process command line. */
{
boolean isFromWeb = cgiIsOnWeb();
if (!isFromWeb && !cgiSpoof(&argc, argv))
    usage();

/* Put out HTTP header and HTML HEADER all the way through <BODY> */
edwWebHeaderWithPersona("Submit data to ENCODE Data Warehouse");

/* Call error handling wrapper that catches us so we write /BODY and /HTML to close up page
 * even through an errAbort. */
htmEmptyShell(doMiddle, NULL);

edwWebFooterWithPersona();
return 0;
}

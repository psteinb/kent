/* hgMarkRegion  - User Marked Region Custom track management CGI. */

 /* This file gives the functionality to add and delete the custom tracks 
 * on the fly by marking some region on the visible tracks.
 *
 * Some functions are copied from hgCustom.c. We tried using those directly by linking, 
 * however hgTracks has functions and variables with same name (e.g. doMiddle), therefore we cant link it. */

#include "common.h"
#include "obscure.h"
#include "linefile.h"
#include "hash.h"
#include "cart.h"
#include "cheapcgi.h"
#include "web.h"
#include "htmshell.h"
#include "hdb.h"
#include "hui.h"
#include "hCommon.h"
#include "customTrack.h"
#include "customFactory.h"
#include "portable.h"
#include "errCatch.h"
#if ((defined USE_BAM || defined USE_TABIX) && defined KNETFILE_HOOKS)
#include "knetUdc.h"
#include "udc.h"
#endif//def (USE_BAM || USE_TABIX) && KNETFILE_HOOKS
#include "net.h"
#include "jsHelper.h"
#include <signal.h>
#include "hgMarkRegion.h"

#define enableDebugCode 0

static long loadTime = 0;

static int mflag = 0 ;
int cnt = 0;
int winStart = 0;
bool userMarkedReg=FALSE;
int winEnd = 0;
char *nChromeName,chromeName[20];
int nChromeStart = 0,nChromeEnd = 0;

/* DON'T EDIT THIS -- use CGI param "&measureTiming=." */
static boolean measureTiming = FALSE;

char *database = NULL;
char *organism = NULL;

#define SAVED_LINE_COUNT  50

/* CGI variables */
#define hgCt		 "hgct_"  /* prefix for all control variables */

/* these are shared with other modules */
#define hgCtDataText      CT_CUSTOM_TEXT_ALT_VAR
#define hgCtDataFile      CT_CUSTOM_FILE_VAR
#define hgCtDataFileName  CT_CUSTOM_FILE_NAME_VAR
#define hgCtDocText       CT_CUSTOM_DOC_TEXT_VAR
#define hgCtDocFile       CT_CUSTOM_DOC_FILE_VAR
#define hgCtTable         CT_SELECTED_TABLE_VAR
#define hgCtUpdatedTable  CT_UPDATED_TABLE_VAR

/* misc */
#define hgCtUpdatedTrack "hgct_updatedTrack"
#define hgCtDeletePrefix "hgct_del"
#define hgCtRefreshPrefix "hgct_refresh"
#define hgCtConfigLines   "hgct_configLines"

/* commands */
#define hgCtDo		  hgCt   "do_"	  /* prefix for all commands */
#define hgCtDoAdd	  hgCtDo "add"
#define hgCtDoDelete	  hgCtDo "delete"
#define hgCtDoDeleteSet	  hgCtDo "delete_set"
#define hgCtDoDeleteClr	  hgCtDo "delete_clr"
#define hgCtDoRefresh     hgCtDo "refresh"
#define hgCtDoRefreshSet  hgCtDo "refresh_set"
#define hgCtDoRefreshClr  hgCtDo "refresh_clr"
#define hgCtDoGenomeBrowser	  hgCtDo "gb"
#define hgCtDoTableBrowser	  hgCtDo "tb"
#ifdef PROGRESS_METER
#define hgCtDoProgress	  hgCtDo "progress"
#endif

/* Global variables */
struct cart *cart;
struct hash *oldVars1 = NULL;
struct customTrack *ctrList = NULL;
int timerCounter;
#define TIMER_INTERVAL 10

static void timer(int sig)
{
// Per HTML 4.01 spec (http://www.w3.org/TR/html401/struct/global.html#h-7.1):
//
//      White space (spaces, newlines, tabs, and comments) may appear before or after each section [including the DOCTYPE].
//
// So we print out comments periodically to keep this process from being killed by apache or the user's web browser.

printf("<!-- processing (%d seconds) -->\n", timerCounter++ * TIMER_INTERVAL);
fflush(stdout);
alarm(TIMER_INTERVAL);
}

struct customTrack *ctFromList(struct customTrack *ctList, char *track)
/* return custom track from list */
{
struct customTrack *ct = NULL;
for (ct = ctList; ct != NULL; ct = ct->next)
    if (sameString(track, ct->tdb->track))
        return ct;
return NULL;
}

void addWarning(struct dyString *ds, char *msg)
/* build up a warning message from parts */
{
if (!msg)
    return;
if (isNotEmpty(ds->string))
    dyStringAppend(ds, ". ");
dyStringAppend(ds, msg);
}

boolean customTrackHasConfig(char *text)
/* determine if there are track or browser lines in text */
{
text = skipLeadingSpaces(text);
return startsWith("track ", text) || startsWith("browser ", text);
}



char *saveLines(char *text, int max)
/* save lines from input, up to 'max'.
 * Prepend with comment, if truncated */
{
if (!text)
    return NULL;

char buf[128];
int count = 0;
char *line;
boolean truncated = FALSE;
struct dyString *ds = dyStringNew(0);

safef(buf, sizeof buf, "# Displaying first %d lines of data", max);
struct lineFile *lf = lineFileOnString("saved custom text", TRUE, text);
while (lineFileNext(lf, &line, NULL))
    {
    if (startsWith(buf, line))
        continue;
    if (++count > max)
        {
        truncated = TRUE;
        break;
        }
    dyStringAppend(ds, line);
    dyStringAppend(ds, "\n");
    }
if (truncated)
    {
    struct dyString *dsNew = dyStringNew(0);
    dyStringPrintf(dsNew, "%s\n%s", buf, dyStringCannibalize(&ds));
    return dyStringCannibalize(&dsNew);
    }
return (dyStringCannibalize(&ds));
}



char *fixNewData(struct cart *cart)
/* append a newline to incoming data, to keep custom preprocessor happy */
{
char *customText = cartUsualString(cart, hgCtDataText, "");
if (isNotEmpty(customText))
    {
    struct dyString *ds = dyStringNew(0);
    dyStringPrintf(ds, "%s\n", customText);
    customText = dyStringCannibalize(&ds);
    cartSetString(cart, hgCtDataText, customText);
    }
return customText;
}



struct dbDb *getCustomTrackDatabases()
/* Get list of databases having custom tracks for this user.
 * Dispose of this with dbDbFreeList. */
{
struct dbDb *dbList = NULL, *dbDb;
char *db;

/* Get list of assemblies with custom tracks */
struct hashEl *hels = cartFindPrefix(cart, CT_FILE_VAR_PREFIX);
struct hashEl *hel = NULL;
for (hel = hels; hel != NULL; hel = hel->next)
    {
    /* TODO: chop actual prefix */
    db = chopPrefixAt(cloneString(hel->name), '_');
        /* TODO: check if file exists, if not remove ctfile_ var */
    dbDb = hDbDb(db);
    if (dbDb)
        slAddTail(&dbList, dbDb);
    }
return dbList;
}


void doBrowserLines(struct slName *browserLines, char **retErr)
/*  parse variables from browser lines into the cart */
{
char *err = NULL;
struct slName *bl;
for (bl = browserLines; bl != NULL; bl = bl->next)
    {
    char *words[96];
    int wordCount;

    wordCount = chopLine(bl->name, words);
    if (wordCount > 1)
        {
	char *command = words[1];
	printf("the word is ===========================>%s\n",command);
	fflush(stdout);
	if (sameString(command, "hide")
            || sameString(command, "dense")
            || sameString(command, "pack")
            || sameString(command, "squish")
            || sameString(command, "full"))
	    {
	    if (wordCount > 2)
	        {
		int i;
		for (i=2; i<wordCount; ++i)
		    {
		    char *s = words[i];
		    if (sameWord(s, "all"))
                        {
                        cartSetString(cart, "hgt.visAllFromCt", command);
                        }
                    else
                        {
                        char buf[256];
                        safef(buf, sizeof buf, "hgtct.%s", s);
                        cartSetString(cart, buf, command);
                        }
		    }
		}
	    }
	else if (sameString(command, "position"))
	    {
            char *chrom = NULL;
            int start = 0, end = 0;
	    if (wordCount < 3)
                {
	        err = "Expecting 3 words in browser position line";
                break;
                }
	    if (!hgParseChromRange(database, words[2], &chrom, &start, &end) ||
                start < 0 || end > hChromSize(database, chrom))
                {
	        err ="Invalid browser position (use chrN:123-456 format)";
                break;
                }
            cartSetString(cart, "position", words[2]);
	    }
	}
    printf("word count is not more than 2\n");
    fflush(stdout);
    }
if (retErr)
    *retErr = err;
}



void doDeleteMarkReg()
/* remove user marked  tracks from list based on cart variables */
{
	struct customTrack *ct;
	for (ct = ctrList; ct != NULL; ct = ct->next)
	{
	    if (!strcmp(ct->tdb->shortLabel,"Marked Regions" )) 
	    {		    
		    slRemoveEl(&ctrList, ct);
	    }
	}
}

#if enableDebugCode
void customCartDump(char *place )
{
/* Dump all the variables of the cart into /tmp/cdump/cartDump_ file. This can be used for debugging purpose */
	struct hashEl *elList = hashElListHash(cart->hash);
	struct hashEl *el;
	FILE *fp;
	time_t ltime;
	struct tm *tm;
	ltime=time(NULL);

	tm=localtime(&ltime);
	//fprintf(fp,"%04d %02d %02d %02d %02d %02d\n\n", tm->tm_year+1900, tm->tm_mon, 
	//		    tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	char dumpFile[100]="/tmp/cdump/cartDump_";
	char temp[28];
	//fprintf(fp,"dump file name is==>%s\n",temp);
	strcat(dumpFile,place);
	sprintf(temp,"%02d_%02d_%02d",tm->tm_hour,tm->tm_min,tm->tm_sec);
	//fprintf(fp,"dump file name is==>%s\n",temp);
	strcat(dumpFile,temp);
	//fprintf(fp,"dump file name is==>%s\n",dumpFile);
        mkdir("/tmp/cdump/",0777);	
	fp = fopen(dumpFile,"w");
	 

	slSort(&elList, hashElCmp);
	fprintf(fp,"VAR_NAME                      VAR_VAL\n\n\n");
	for (el = elList; el != NULL; el = el->next)
	{	
		char *var = htmlEncode(el->name);
		char *val = htmlEncode((char *)(el->val));
    		fprintf(fp,"%s \t\t\t\t %s\n", var, val);
		fflush(fp);
    		//printf("%s \t\t\t\t %s\n", var, val);
		fflush(stdout);
		freeMem(var);
		freeMem(val);
	
	}
	hashElFreeList(&elList);
	fclose (fp);

}

#endif 

void getCords()
{
/*Get co-ordinates from cart to add a row in markRegion table */

	nChromeName =  cartString(cart, "c");
	nChromeStart = cartIntExp(cart, "l");
	nChromeEnd = cartIntExp(cart, "r");
}	


void copyCart()
{	
/*Add custom related variables to cart*/
	char *customText;
	char * seqName =  cartString(cart, "c");
	int winStart = cartIntExp(cart, "l");
	int winEnd = cartIntExp(cart, "r");
	customText = malloc(strlen(seqName)+2*sizeof(int));
	sprintf(customText,"%s %d %d",seqName,winStart,winEnd);

	cartSetString(cart, hgCtDataText,customText);
	cartSetString(cart,CT_CUSTOM_TEXT_VAR,customText);
	cartSetString(cart,"position",customText);
	cartSetString(cart, hgCtDocFile,"" );
	cartSetString(cart, hgCtDataFileName,"");
	cartSetString(cart, hgCtDocText,"");
	cartSetString(cart, "hgt.customFile","");
	cartSetString(cart, "hgt.customFile__filename","");
	cartSetString(cart, "ruler","pack");
	cartSetString(cart, "Marked Regions","pack");
}


void addRow ()
{
	/* Add a new row to the user marked region table */
	    struct customTrack *ctt=NULL; 
	    int bin;

	    for (ctt = ctrList; ctt != NULL; ctt = ctt->next)
	    { 
		    if (!strcmp(ctt->tdb->shortLabel,"Marked Regions" )) 
		    {	    
			bin	= hFindBin(winStart,winEnd);
			struct sqlConnection *conn = hAllocConn(CUSTOM_TRASH);

			if (conn)
			    {
				char query[1024];
				safef(query, sizeof(query), "INSERT into %s VALUES(\"%d\",\"%s\",\"%d\",\"%d\")", ctt->dbTableName,bin,nChromeName,nChromeStart,nChromeEnd );
				sqlUpdate(conn,query);

			}	
			break;
			  
		    }

	    }	   
}

void findMarkRegCt()
{
	/*Find are there any user marked region track exists or not*/
	    struct customTrack *ctt=NULL; 
	    for (ctt = ctrList; ctt != NULL ; ctt = ctt->next)
	    {
		  if (!strcmp(ctt->tdb->shortLabel,"Marked Regions" )) 
		  {	    
				char *pch = strtok(ctFirstItemPos(ctt),":");
				strcpy(chromeName,pch);
				while (pch != NULL)
			
				{	
					pch = strtok (NULL, "-");
					if(!winStart)
					{	
						winStart = atoi(pch);
						continue;
					}	
					if(!winEnd)
						winEnd = atoi(pch);
			       }

			userMarkedReg=TRUE;
			break;
			
	   	 }	
	    }		

}


void doAddCustomMarkReg(char *err,boolean  flag)
{
	/*add a new usermarked track*/

	markRegFlag = 1;
	mflag = 1;
	findMarkRegCt();
	if(userMarkedReg)
	{	
		getCords();
		addRow();
	}	
	else
		copyCart();
	markRegion(cart);
	setTrackPackMode();
}

void markRegion(struct cart *theCart)
{
	/* Get the co-ordinates from the user marked region and create a 
	 * metadata file and create a table in data base with seq name and co-ordinates*/

	char *ctFileName = NULL;
	struct slName *browserLines = NULL;
	struct customTrack *replacedCts = NULL;
	char *err = NULL, *warn = NULL;
	char *selectedTable = NULL;
	struct customTrack *ct = NULL;
	boolean ctUpdated = FALSE;
	char *initialDb = NULL;

	long thisTime = clock1000();
	cart = theCart;

	initialDb = cloneString(cartString(cart, "db"));
	getDbAndGenome(cart, &database, &organism, oldVars1);

	setUdcCacheDir();
	customFactoryEnableExtraChecking(TRUE);

#if enableDebugCode
	customCartDump("begin");
#endif

#if ((defined USE_BAM || defined USE_TABIX) && defined KNETFILE_HOOKS)
	knetUdcInstall();
	if (udcCacheTimeout() < 300)
	    udcSetCacheTimeout(300);
#endif//def (USE_BAM || USE_TABIX) && KNETFILE_HOOKS

	if (sameString(initialDb, "0"))
	    {
	    /* when an organism is selected from the custom track management page,
	     * set the database to be the default only if it has custom tracks.
	     * Otherwise, pick an assembly for that organism that does have custom tracks. */
	    struct dbDb *dbDb, *dbDbs = getCustomTrackDatabases();
	    char *dbWithCts = NULL;
	    for (dbDb = dbDbs; dbDb != NULL; dbDb = dbDb->next)
		{
		if (sameString(database, dbDb->name))
		    break;
		if (sameString(organism, dbDb->organism))
		    {
		    if (!dbWithCts)
			dbWithCts = cloneString(dbDb->name);
		    }
		}
	    if (dbWithCts)  // set the database for the selected organism to an assembly that
		{           // has custom tracks
		database = dbWithCts;
		cartSetString(cart, "db", database);
		}
	    }
	if (cartVarExists(cart, hgCtDoAdd))
	{
	   doAddCustomMarkReg(NULL,FALSE);
	} 
#ifdef PROGRESS_METER
	else if (cartVarExists(cart, hgCtDoProgress))
	    {
	    doProgress(NULL);
	    }
#endif
	else if (cartVarExists(cart, hgCtTable))
	    {
	    /* update track */
	    /* need to clone the hgCtTable value, as the ParseCart will remove
	       the variable */
	    selectedTable = cloneString(cartString(cart, hgCtTable));
	    if (isNotEmpty(selectedTable))
		{
		ctrList = customTracksParseCart(database, cart, NULL, NULL);
		ct = ctFromList(ctrList, selectedTable);
		}
	    if (ct)
		;//doUpdateCustom(ct, NULL);
	    else
	    { 
		doAddCustomMarkReg(NULL,TRUE);
	    }
	    }
	else
	    {
	    /* get new and existing custom tracks from cart and decide what to do */

	    // setup a timer to periodically print out something to stdout to make sure apache or the web browser doesn't time us out (see redmine #3002).
	    // e.g. see http://stackoverflow.com/questions/5547166/how-to-avoid-cgi-timeout
	    
	    struct sigaction *act;
	    AllocVar(act);
	    act->sa_handler = timer;
	    act->sa_flags = SA_RESTART;
	    sigaction(SIGALRM, act, NULL);
	    alarm(TIMER_INTERVAL);

	    char *customText = fixNewData(cart);
	    /* save input so we can display if there's an error */
	    char *savedCustomText = saveLines(cloneString(customText),
					SAVED_LINE_COUNT);
	    char *trackConfig = cartOptionalString(cart, hgCtConfigLines);
	    char *savedConfig = cloneString(trackConfig);

	    struct dyString *dsWarn = dyStringNew(0);
	    char *fileName = cartOptionalString(cart, hgCtDataFileName);
	    boolean hasData = (isNotEmpty(customText) || isNotEmpty(fileName));
	    if (cartVarExists(cart, hgCtUpdatedTrack) && hasData)
        	{
			/* from 'update' screen */
			/* prepend config to data for parser */
			struct dyString *dsTrack = dyStringNew(0);
			if (!trackConfig)
			    trackConfig = cartOptionalString(cart, hgCtUpdatedTrack);
			char *fileContents = NULL;
			if (isNotEmpty(fileName))
			    {
			    if (customTrackIsCompressed(fileName))
				fileContents = "Compressed files not supported for data update";
			    else
				fileContents = cartOptionalString(cart, hgCtDataFile);
			    customText = fileContents;
			    }
			/* check for duplicate track config in config and data entry */
			if (customTrackHasConfig(trackConfig) &&
			    customTrackHasConfig(customText))
			    {
			    if (startsWith(trackConfig, customText))
				trackConfig = "";
			    else
				customText = "Duplicate track configuration found - remove track and browser lines from Configuration box or from Data";
			    }
			dyStringPrintf(dsTrack, "%s\n%s\n", trackConfig, customText);
			customText = dyStringCannibalize(&dsTrack);
			cartSetString(cart, hgCtDataText, customText);
			if (isNotEmpty(fileContents))
			    {
			    /* already handled file */
			    cartRemove(cart, hgCtDataFile);
			    cartRemove(cart, hgCtDataFileName);
			    }
		}
	    boolean ctParseError = FALSE;
	    struct errCatch *catch = errCatchNew();
	    if (errCatchStart(catch))
	    { 
		ctrList = customTracksParseCartDetailed(database, cart, &browserLines, &ctFileName,
						       &replacedCts, NULL, &err);
	    }

	    errCatchEnd(catch);
	    if (catch->gotError)
		{
		addWarning(dsWarn, err);
		addWarning(dsWarn, catch->message->string);
		ctParseError = TRUE;
		}
	    errCatchFree(&catch);

	    /* exclude special setting used by table browser to indicate
	     * db assembly for error-handling purposes only */
	    char *db = NULL;
	    if (trackConfig && (db = stringIn("db=", trackConfig)) != NULL)
		{
		db += 3;
		char *nextTok = skipToSpaces(db);
		if (!nextTok)
		    nextTok = strchr(db, 0);
		db = cloneStringZ(db,nextTok-db);
		if (!sameString(db,database))
		    err = "Invalid configuration found - remove db= or return it to it's original value";
		}
	    if (cartVarExists(cart, hgCtUpdatedTrack) && !hasData)
		{
		/* update custom track config and doc, but not data*/
		selectedTable = cartOptionalString(cart, hgCtUpdatedTable);
		if (selectedTable)
		    {
		    ct = ctFromList(ctrList, selectedTable);
		    if (ct)
			{
			struct errCatch *catch = errCatchNew();
			if (errCatchStart(catch))
			    {
			    customTrackUpdateFromConfig(ct, database, trackConfig, &browserLines);
			    ctUpdated = TRUE;
			    }
			errCatchEnd(catch);
			if (catch->gotError)
			    addWarning(dsWarn, catch->message->string);
			errCatchFree(&catch);
			}
		    }
		}
	    //addWarning(dsWarn, replacedTracksMsg(replacedCts));
	    doBrowserLines(browserLines, &warn);
	    addWarning(dsWarn, warn);
	    if (err)
		{
		char *selectedTable = NULL;
		cartSetString(cart, hgCtDataText, savedCustomText);
		cartSetString(cart, hgCtConfigLines, savedConfig);
		if ((selectedTable= cartOptionalString(cart, hgCtUpdatedTable)) != NULL)
		    {
		    ct = ctFromList(ctrList, selectedTable);
		    //doUpdateCustom(ct, err);
		    }
		else
		{	
		    doAddCustomMarkReg(err,TRUE);
		}
		cartRemovePrefix(cart, hgCt);
		return;
		}
	    if (cartVarExists(cart, hgCtDoDelete))
		{
		doDeleteMarkReg();
		ctUpdated = TRUE;
		}
	    if (cartVarExists(cart, hgCtDoRefresh))
		{
		//doRefreshCustom(&warn);
		addWarning(dsWarn, warn);
		ctUpdated = TRUE;
		}

	    if (ctUpdated || ctConfigUpdate(ctFileName))
		{
		customTracksSaveCart(database, cart, ctrList);

		/* refresh ctrList again to pickup remote resource error state */
		struct errCatch *catch = errCatchNew();
		if (errCatchStart(catch))
		{	
		    ctrList = customTracksParseCartDetailed(database, cart, &browserLines, &ctFileName,
						       &replacedCts, NULL, &err);
		} 
		errCatchEnd(catch);
		if (catch->gotError)
		    {
		    addWarning(dsWarn, err);
		    addWarning(dsWarn, catch->message->string);
		    ctParseError = TRUE;
		    }
		errCatchFree(&catch);

		}
	    warn = dyStringCannibalize(&dsWarn);
	    if (measureTiming)
		{
		long lastTime = clock1000();
		loadTime = lastTime - thisTime;
		}
	    if ((!initialDb || ctrList || cartVarExists(cart, hgCtDoDelete)) && mflag)
	    {
	       cartSaveSession(cart);
	       return ;
	    }		
	    else if (ctParseError)
	    {
		doAddCustomMarkReg(warn,TRUE);
	    }	
	    else
	    {
		doAddCustomMarkReg(NULL,TRUE);
	    }
	  }
	cartRemovePrefix(cart, hgCt);
	cartRemove(cart, CT_CUSTOM_TEXT_VAR);
}

void getCtList()
{
	/* Get custom   track list */
	char *ctFileName = NULL;
	struct slName *browserLines = NULL;
	struct customTrack *replacedCts = NULL;
	char *err = NULL;
	ctrList = customTracksParseCartDetailed(database, cart, &browserLines, &ctFileName,
						       &replacedCts, NULL, &err);
}	

void updateCtList()
{
	 /* Update custom track list */

		customTracksSaveCart(database, cart, ctrList);

}		


void setTrackPackMode(){

	    struct customTrack *ctt=NULL; 
	    for (ctt = ctrList; ctt != NULL; ctt = ctt->next)
	    { 
		    if (!strcmp(ctt->tdb->shortLabel,"Marked Regions" )) 
		        cartSetString(cart,ctt->tdb->table,"pack");
	   }		    

}	    

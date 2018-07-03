/* hillerLabView - To store,load,delete track views on the fly*/
/* A view are all the settings of which and how tracks are displayed */

/* This file gives the functionality to store track's present view
 * in data base and loadback (retrieve) any time across species.
 * It also provides functionality to delete views */

#include "common.h"
#include "hCommon.h"
#include "memalloc.h"
#include "localmem.h"
#include "obscure.h"
#include "dystring.h"
#include "hash.h"
#include "jksql.h"
#include "cheapcgi.h"
#include "htmshell.h"
#include "cart.h"
#include "hdb.h"
#include "hui.h"
#include "hgTracks.h"
#include "cutterTrack.h"
#include "wikiTrack.h"
#include "ctgPos.h"
#include "trackHub.h"
#include "hubConnect.h"
#include "pcrResult.h"
#include "jsHelper.h"
#include "hgConfig.h"
#include "chromInfo.h"
#include "hillerLabView.h"

/* global variables */
char *HViewMenu[HVIEWMAXNUMVIEWS] = {" "};
int hViewMode = 0;
int sizeofHviewMenu=8;
/* Pradeeps old code */
/*
char *oldDb = NULL;
struct cart *tmpCart = NULL;	*tmp hash to load view*/



void initializeHviewMenu(){
/* Initialize  the HViewMenu */	
	int i;
	for (i = 1 ; i < HVIEWMAXNUMVIEWS ; i++)
		HViewMenu[i] = NULL;
}	

void makeViewDropDown()
{
/* populate drop down list with view names */
	struct slName *SQLResultList;
	struct sqlConnection *conn = hAllocConn(HVIEWDBNAME);
	char query[100];
	char *tableName = HVIEWTABLE;
	int i = 1;//initial value is a empty string
	sqlSafef(query, sizeof(query), "select view  from %s",tableName);
	SQLResultList = sqlQuickList(conn,query);
	initializeHviewMenu();
	sizeofHviewMenu = 8; 
	while(SQLResultList != NULL)
	{
		HViewMenu[i] = SQLResultList->name;
		sizeofHviewMenu += 8; 
		fflush(stdout);
		SQLResultList = SQLResultList->next;
		i++;
	}	
}


void doHillerLabViewOperations()
{	
	/*Perform view operations if any of them selected*/

	char *vName ;	/* view name */

/* old code. Not used anymore.
	*Get the track list if database changes*
	if (!oldDb)
	{	
		oldDb = cartString(cart,"db");
		getTrackListFromDb(userSeqString);
	}	
	if(!sameWord(oldDb,cartString(cart,"db")))
	{
		oldDb = cartString(cart,"db");
		getTrackListFromDb(userSeqString);
	}	
*/
	if(hTableExists(HVIEWDBNAME,HVIEWTABLE))
		makeViewDropDown();

	if (cartVarExists(cart,"hgt.loadView")) 
	{	
		vName = cartString(cart,"hillerLabView");
		loadView(vName);
		cartRemove(cart,"hgt.loadView");
		cartRemove(cart,"hillerLabView");
		makeViewDropDown();
	}	
	if (cartVarExists(cart,"hgt.saveView")) 
	{
		vName = cartString(cart,"hgt.saveView");
		if(sameWord(vName,"Save View"))
			saveView();
		cartRemove(cart,"hgt.saveView");
		makeViewDropDown();
		cartRemove(cart,"viewName");
	}		
	if (cartVarExists(cart,"hgt.deleteView")) 
	{
		vName = cartString(cart,"hillerLabView");
		deleteView(vName);
		cartRemove(cart,"hgt.deleteView");
		makeViewDropDown();
	}

}	

void loadView(char *viewName)
{
/* Load selected view from database */

	char *tableName = HVIEWTABLE;
	struct sqlConnection *conn = hAllocConn(HVIEWDBNAME);
	struct dyString *query = dyStringNew(0);
	struct slName *SQLResultList;

	/* get the settings from the SQL table. Result is a list */
	sqlSafef(query->string, query->bufSize, "select params from %s where view='%s'",tableName,viewName);
	SQLResultList = sqlQuickList(conn,query->string);

#if debugHillerLabView
	printf("restore from %s :<br>", viewName); 
#endif

	/* convert the list into a dyString object */
	struct slName *el;
	struct dyString *viewSettings = dyStringNew(HVIEWPARAMSIZE);
	for (el = SQLResultList; el != NULL; el = el->next) {
		dyStringAppend(viewSettings,el->name);
#if debugHillerLabView
		printf("&nbsp;&nbsp;&nbsp;&nbsp;%s <br>",el->name);
#endif
	}
	slFreeList(SQLResultList);

	/* now wrap a lineFile object around that string and perform the same operations as hgSession does */
	struct lineFile *lf = lineFileOnString("settingsFromView", TRUE, viewSettings->string);
	if (lf != NULL) {
		cartLoadSettings(lf, cart, NULL, NULL);
		hubConnectLoadHubs(cart);
		cartCopyCustomTracks(cart);
//    checkForCustomTracks(dyMessage);
		lineFileClose(&lf);
	}
	freeDyString(&viewSettings);


/*	Pradeeps old code. This code does not put back all stored key->value settings */
/*
	struct slName *tr;
	char *pch = strtok(name->name,"\n");
	char *varval;

	if(!tmpCart)
		tmpCart = cartOfNothing();

	* create a tempcart hash with stored view data *
	while (pch != NULL)
	{	
		varval = malloc (strlen(pch)+1);
		strcpy(varval,pch);
		char *ptr = strchr(varval,'\t');
		char *var = calloc (ptr-varval+1,sizeof(char));
		strncpy(var,varval,ptr-varval);
		char *val = calloc(varval+strlen(varval)-ptr+1,sizeof(char));
		strncpy(val,varval+(ptr-varval)+1,varval+strlen(varval)-ptr);
		cartSetString(tmpCart,var,val);
		free(varval);
		free (var);
		free (val);
		pch = strtok (NULL, "\n");
	}				

	* hide all the tracks before loading a view *
	hideAllTracks(cart);

	* load cart from tempcart *
	for(tr = slNameCloneList(hvName) ; tr != NULL; tr = tr->next)
	{
		struct slPair *sl  = cartVarsWithPrefix(tmpCart,tr->name);
		while (sl != NULL)
		{
			cartSetString(cart,sl->name,sl->val);
			sl = sl->next;
		}
	}

	 tmpCart = NULL;
*/

	 freeDyString(&query);
	 hFreeConn(&conn);
#if debugHillerLabView
	 customCartDump1(NULL,"loadView");
#endif
}	


void saveView()
{
/* Save view into database */	
	char *tableName = HVIEWTABLE;

	char *tableFormat =
	"CREATE TABLE %s (\n"
	"    view varchar(255) not null, 	  # view name \n"
	"    params longblob not null,       # the parameters of view\n"
	"    PRIMARY KEY(view)\n"
	")";
	struct dyString *createSql = dyStringNew(0);
	struct dyString *params = dyStringNew(HVIEWPARAMSIZE);
	struct sqlConnection *conn = hAllocConn(HVIEWDBNAME);
	char *viewName = cartString(cart,"viewName");
	char tviewName[HVIEWMAXVIEWLEN + 1];


	viewName = skipLeadingSpaces(viewName);
	sqlDyStringPrintf(createSql, tableFormat, tableName);
	
#if debugHillerLabView
	customCartDump1(cart,"saveview");
#endif

	/* chop if viewName is longer than HVIEWMAXVIEWLEN (60) chars */
	if(strlen(viewName) > HVIEWMAXVIEWLEN)
	{
		safencpy(tviewName,HVIEWMAXVIEWLEN + 1,viewName,HVIEWMAXVIEWLEN - 1);
		safencpy(viewName,HVIEWMAXVIEWLEN + 1,tviewName,HVIEWMAXVIEWLEN - 1);//to keep the standard
	}
	if (sqlMaybeMakeTable(conn, tableName, createSql->string))
	{
		dyStringFree(&createSql);
	}

	cartRemove(cart,"hgt.saveView");

#if debugHillerLabView
	printf("FULL CART DUMP for debugging !! <br>");
	struct hashEl *elListDEBUG = hashElListHash(cart->hash);
	cartDumpList(elListDEBUG,1);
	printf("CART DUMP end<br><br>");
#endif

	/* here we store the entire cart, except those prefixes listed in HVIEWBLACKLISTEDCARTENTRIES */
	/* We make a list out of the string. */
	struct slName *blackListedPrefixes = slNameListFromString(HVIEWBLACKLISTEDCARTENTRIES,',');
	struct hashEl *elList = hashElListHash(cart->hash);
	struct hashEl *el;
	slSort(&elList, hashElCmp);
	for (el = elList; el != NULL; el = el->next) {
		if (slNameInList(blackListedPrefixes, el->name)) {
#if debugHillerLabView
			printf("SKIP    &nbsp;&nbsp;&nbsp;&nbsp;sl->name:  %s --> %s<br>",el->name,(char*)el->val);
#endif
		}else{
			dyStringAppend(params,el->name);
			dyStringAppend(params,"\t");
			dyStringAppend(params,el->val);
			dyStringAppend(params,"\n");
#if debugHillerLabView
			printf("&nbsp;&nbsp;&nbsp;&nbsp;sl->name:  %s --> %s<br>",el->name,(char*)el->val);
#endif
		}
	}
	hashElFreeList(&elList);

/* Pradeeps old code. For some reason, some settings such as "33wayVIEWalign.showCfg --> off" were never stored */
/*
	struct slName *tel;
	for (tel = hvName; tel != NULL; tel = tel->next)
	{	
		printf("&nbsp;&nbsp;tel->name %s<br>",tel->name);
		struct slPair *sl  = cartVarsWithPrefix(cart,tel->name);
		while (sl != NULL)
		{
			printf("&nbsp;&nbsp;&nbsp;&nbsp;sl->name:  %s --> %s<br>",sl->name,(char*)sl->val);
			dyStringAppend(params,sl->name);
			dyStringAppend(params,"\t");
			dyStringAppend(params,sl->val);
			dyStringAppend(params,"\n");
			sl = sl->next;
		}
	}
*/
	struct dyString *query = dyStringNew(params->bufSize);
	sqlSafef(query->string, query->bufSize, "INSERT into %s VALUES(\"%s\",\"%s\")", tableName,viewName,params->string );
	sqlUpdate(conn,query->string);
	freeDyString(&params);
	freeDyString(&query);
	hFreeConn(&conn);
}	


void deleteView(char *viewName)
{
	/* Delete unwanted view */
	struct dyString *query = dyStringNew(0);
	char *tableName = HVIEWTABLE;
	viewName = skipLeadingSpaces(viewName);
	struct sqlConnection *conn = hAllocConn(HVIEWDBNAME);
	sqlSafef(query->string, query->bufSize, "delete from %s where view = '%s' ",tableName,viewName);
	sqlUpdate(conn,query->string);
	freeDyString(&query);
	hFreeConn(&conn);
}	
	 
/* 
old code. Not used anymore

* Hide All *
void hideAllTracks(struct cart *ct){
	struct slName *tr;
   for(tr = slNameCloneList(hvName) ; tr != NULL; tr = tr->next){
		cartSetString(ct,tr->name,"hide");
	}
}

void getTrackListFromDb(char *userSeqString)
{
	* Get tracks list for the selected species *
	struct trackDb *tdb,*tdbList = NULL;
	tdbList =  hTrackDb(cartString(cart,"db"));
	//struct track *trackList = hillerViewTrackList;//getTrackList(&groupList,-3);

	for (tdb = tdbList; tdb != NULL; tdb = tdb->next)
	{
		if(tdb->parent != NULL)
		{	
			slNameAddHead (&hvName,tdb->parent->track);
		}		
		slNameAddHead (&hvName,tdb->track);

		getSubTracks(tdb);
	}	

	struct track *track = (struct track*)oligoMatchTg();
	
	slNameAddHead(&hvName,track->tdb->track);
	getSubTracks(track->tdb);   //Add subtracks to list

	if (restrictionEnzymesOk())
	{	
		track = (struct track*) cuttersTg();
		slNameAddHead(&hvName,track->tdb->track);
		getSubTracks(track->tdb);//Add subtracks to list
	}	
	if (userSeqString != NULL)
	{	      
		track = (struct track*) userPslTg();
		slNameAddHead(&hvName,track->tdb->track);
		getSubTracks(track->tdb);//Add subtracks to list
	}       

	if (pcrResultParseCart(database, cart, NULL, NULL, NULL))
	{	
		track = (struct track*) pcrResultTg();
		slNameAddHead(&hvName, track->tdb->track);
		getSubTracks(track->tdb);//Add subtracks to list
	}	
	if (wikiTrackEnabled(database, NULL))
	{
		addWikiTrack(&track);
		struct sqlConnection *conn = wikiConnect();
		if (sqlTableExists(conn, "variome"))
			addVariomeWikiTrack(&track);
		wikiDisconnect(&conn);
		slNameAddHead(&hvName, track->tdb->track);
		getSubTracks(track->tdb);//Add subtracks to list
	}

	*These are not grouped under standard TDB tracks *

	slNameAddHead(&hvName, "ruler");
	slNameAddHead(&hvName, "hgt.");

#if debugHillerLabView 
	*If you want to store custom tracks just use below one it will work*
	slNameAddHead(&hvName, "ct");

	struct slName *el;
	for (el = slNameCloneList(hvName) ; el != NULL ; el = el->next)
	{	
		printf("name  is  ==============>%s\n",el->name);
		fflush(stdout);
	}
#endif

} 

* Get sub tracks *
void getSubTracks (struct trackDb *tdb)
{

	struct slRef *tdbRef, *tdbRefList = trackDbListGetRefsToDescendantLeaves(tdb->subtracks);
	struct trackDb *subtdb;
	for (tdbRef = tdbRefList; tdbRef != NULL; tdbRef = tdbRef->next)
	{
	       subtdb = tdbRef->val;
	       slNameAddHead (&hvName,subtdb->track);
	}	
} 
*/


#if debugHillerLabView 
void customCartDump1(struct cart *ct,char *place )
{
	if (ct != NULL)
	cart = ct;
/* Dump all the variables of the cart,can be used for debugHillerLabViewging  purpose */
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


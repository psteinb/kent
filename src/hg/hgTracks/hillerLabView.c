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
#include "cart.h"
#include "chromInfo.h"
#include "hillerLabView.h"

/* global variables */
char *HViewMenu[HVIEWMAXNUMVIEWS] = {" "};
int sizeofHviewMenu=8;
char *oldDb = NULL;
int hViewMode = 0;
struct slName *name;
struct cart *tmpCart = NULL;	/*tmp hash to load view*/


void initializeHviewMenu(){
/* Initialize  the HViewMenu */	

	int i;
	for (i = 1 ; i < HVIEWMAXNUMVIEWS ; i++)
		HViewMenu[i] = NULL;

}	
	
	
void makeViewDropDown()
{
/*populate drop down list */

	struct sqlConnection *conn = hAllocConn(HVIEWDBNAME);
	char query[100];
	char *tableName = HVIEWTABLE;
	int i = 1;//initial value is a empty string
	safef(query, sizeof(query), "select view  from %s",tableName);
	name = sqlQuickList(conn,query);
	initializeHviewMenu();
	sizeofHviewMenu = 8; 
	while(name != NULL)
	{
		HViewMenu[i]=name->name;
		sizeofHviewMenu += 8; 
		fflush(stdout);
		name = name->next;
		i++;
	}	
}


void doHillerLabViewOperations(char *oldDb,char *userSeqString)
{	
	/*Perform view operations if any of them selected*/

	char *vName ;

	/*Get the track list if database changes*/
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
/* Load  selected view from database */

	char *tableName = HVIEWTABLE;
	struct slName *tr;
	struct sqlConnection *conn = hAllocConn(HVIEWDBNAME);
	struct dyString *query = dyStringNew(0);

	safef(query->string, query->bufSize, "select params from %s where view='%s'",tableName,viewName);
	name = sqlQuickList(conn,query->string);
	

	char *pch = strtok(name->name,"\n");

	char *varval;

	if(!tmpCart)
		tmpCart = cartOfNothing();

	/*create a tempcart hash with stored view data*/

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


	/*load cart from tempcart */
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
	 freeDyString(&query);
#if debugHillerLabView
	 customCartDump1("loadView");
	 hFreeConn(&conn);
#endif
}	




void saveView()
{
/*Save view into database */	
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


	viewName =skipLeadingSpaces(viewName);
	sqlDyStringPrintf(createSql, tableFormat, tableName);
	//chop if viewName is longer than HVIEWMAXVIEWLEN (60) chars
	if(strlen(viewName) > HVIEWMAXVIEWLEN)
	{
		safencpy(tviewName,HVIEWMAXVIEWLEN + 1,viewName,HVIEWMAXVIEWLEN - 1);
		safencpy(viewName,HVIEWMAXVIEWLEN + 1,tviewName,HVIEWMAXVIEWLEN - 1);//to keep the standard
	}
	if (sqlMaybeMakeTable(conn, tableName, createSql->string))
	{
	       dyStringFree(&createSql);
	}	 
	
#if debugHillerLabView 
	struct slName *el;
	for (el = slNameCloneList(hvName) ; el != NULL ; el = el->next)
	{	
		printf("name  is  ==============>%s\n",el->name);
		fflush(stdout);
	}
#endif

	/* here we go to the list of all tracks and store only cart entries referring to existing tracks in the table */
	struct slName *tel;
	for (tel = hvName; tel != NULL; tel = tel->next)
	{	
		struct slPair *sl  = cartVarsWithPrefix(cart,tel->name);
		 while (sl != NULL)
		 {
			dyStringAppend(params,sl->name);
			dyStringAppend(params,"\t");
			dyStringAppend(params,sl->val);
			dyStringAppend(params,"\n");
       			sl = sl->next;
	       }

	}
	struct dyString *query = dyStringNew(params->bufSize);
	safef(query->string, query->bufSize, "INSERT into %s VALUES(\"%s\",\"%s\")", tableName,viewName,params->string );
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
	safef(query->string, query->bufSize, "delete from %s where view = '%s' ",tableName,viewName);
	sqlUpdate(conn,query->string);
	freeDyString(&query);
	hFreeConn(&conn);
}	
	 

void  getTrackListFromDb(char *userSeqString)
{
	/* Get tracks list for the selected species */
	struct trackDb *tr ,*tdbList = NULL;
	tdbList =  hTrackDb(cartString(cart,"db"));

	for (tr = tdbList; tr != NULL; tr = tr->next)
	{
		slNameAddHead (&hvName,tr->table);

	}	

	struct track *track = (struct track*)oligoMatchTg();
	slNameAddHead(&hvName,track->tdb->table);

	if (restrictionEnzymesOk())
	{	
		track = (struct track*) cuttersTg();
		slNameAddHead(&hvName,track->tdb->table);
	}	
	if (userSeqString != NULL)
	{	      
		track = (struct track*) userPslTg();
		slNameAddHead(&hvName,track->tdb->table);
	}       

	if (pcrResultParseCart(database, cart, NULL, NULL, NULL))
	{	
		track = (struct track*) pcrResultTg();
		slNameAddHead(&hvName, track->tdb->table);
	}	
	if (wikiTrackEnabled(database, NULL))
	{
		addWikiTrack(&track);
		struct sqlConnection *conn = wikiConnect();
		if (sqlTableExists(conn, "variome"))
			addVariomeWikiTrack(&track);
		wikiDisconnect(&conn);
		slNameAddHead(&hvName, track->tdb->table);
	}


	slNameAddHead(&hvName, "ruler");

#if debugHillerLabView 
	struct slName *el;
	for (el = slNameCloneList(hvName) ; el != NULL ; el = el->next)
	{	
		printf("name  is  ==============>%s\n",el->name);
		fflush(stdout);
	}
#endif

} 

#if debugHillerLabView 
void customCartDump1(char *place )
{
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

/* hillerLabView.h */ 
#ifndef HILLERLABVIEW_H
#define HILLERLABVIEW_H
#include "cart.h"

#define HVIEWTABLE  "HillerLabViews"
#define HVIEWDBNAME "hgcentraltest"
#define HVIEWPARAMSIZE 5096
#define HVIEWMAXNUMVIEWS 1024
#define HVIEWMAXVIEWLEN 60
#define NUM_HVIEWS 1024    /*Number of views we can save*/
#define HQUERYSIZE 100

/* if set to 1, it creates cart dumps in /tmp/cdump/ for debugging */
#define debugHillerLabView 0

/* define a set of cart prefixes that we will not store in a view. 
Those refer to the current species and the current browser position. A view only stores track settings. */
#define HVIEWBLACKLISTEDCARTENTRIES "c,l,r,_,db,clade,lastPosition,position,pix,org,hgt.positionInput,hillerLabView,viewName,hgsid"

/* global variables */
char *HViewMenu[HVIEWMAXNUMVIEWS];
int hViewMode;
int sizeofHviewMenu;

/* functions */
void doHillerLabViewOperations();
/*Do view operations if any selected*/

void loadView(char *viewName);
/*Load saved view */

void saveView();
/*Save a view */

void deleteView(char *viewName);
/*Delete a view */

void makeViewDropDown();
/*make a dropdown list */

void initializeHviewMenu();
/*set all the views NULL*/

/* old code. Not used anymore. 
struct track *userPslTg();
struct track* oligoMatchTg();	
struct track *pcrResultTg();
char *oldDb;

void getTrackListFromDb(char *string);
*get tracks list from db*

struct slName *hvName;
*list of track names*

void getSubTracks(struct trackDb *tdb);
*get subtracks of a track 
*/

#endif /*HILLERLABVIEW_H*/

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


/* global variables */
struct track *userPslTg();
struct track* oligoMatchTg();	
struct track *pcrResultTg();

char *HViewMenu[HVIEWMAXNUMVIEWS];
int sizeofHviewMenu;
char *oldDb;
int hViewMode;

/* functions */
void doHillerLabViewOperations(char *db,char *string);
/*Do view operations if any selected*/

void loadView(char *viewName);
/*Load saved view */

void saveView();
/*Save a view */

void deleteView(char *viewName);
/*Delete a view */

void getTrackListFromDb(char *string);
/*get tracks list from db*/

struct slName *hvName;
/*list of track names*/

void makeViewDropDown();
/*make a dropdown list */

void initializeHviewMenu();
/*set all the views NULL*/


#endif /*HILLERLABVIEW_H*/

/* hgMarkRegion.h : Functions to add new marked region */ 
#ifndef HGMARKREGION_H
#define HGMARKREGION_H
#include "cart.h"

void doAddCustomMarkReg(char *err,boolean flag);
/* do add custom markReg*/

void customCartDump(char *name);
/* dump cart variables */

void markRegion(struct cart *cart);
/*Create a new meta data file and table in database */

void getCtList();
/*get customTrack List  */

void doDeleteMarkReg();
/*Delete marked Region custom track */

void updateCtList();
/* Update the custom track list ctList */

void setTrackPackMode();
/* set Track to pack mode */

#endif /* HGMARKREGION_H*/ 

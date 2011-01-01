/* hgTracks - Human Genome browser main cgi script. */

#include "common.h"
#include "linefile.h"
#include "portable.h"
#include "memalloc.h"
#include "localmem.h"
#include "hCommon.h"
#include "obscure.h"
#include "dystring.h"
#include "hash.h"
#include "cheapcgi.h"
#include "htmshell.h"
#include "web.h"
#include "cart.h"
#include "hdb.h"
#include "hui.h"
#include "hgFind.h"
#include "hgTracks.h"
#include "versionInfo.h"
#include "searchTracks.h"
#include "imageV2.h"

static char const rcsid[] = "$Id: hgTracks.c,v 1.1651 2010/06/11 17:53:06 larrym Exp $";

/* Other than submit and Submit all these vars should start with hgt.
 * to avoid weeding things out of other program's namespaces.
 * Because the browser is a central program, most of it's cart
 * variables are not hgt. qualified.  It's a good idea if other
 * program's unique variables be qualified with a prefix though. */
char *excludeVars[] = { "submit", "Submit", "hgt.reset",
            "hgt.in1", "hgt.in2", "hgt.in3", "hgt.inBase",
            "hgt.out1", "hgt.out2", "hgt.out3",
            "hgt.left1", "hgt.left2", "hgt.left3",
            "hgt.right1", "hgt.right2", "hgt.right3",
            "hgt.dinkLL", "hgt.dinkLR", "hgt.dinkRL", "hgt.dinkRR",
            "hgt.tui", "hgt.hideAll", "hgt.visAllFromCt",
	    "hgt.psOutput", "hideControls", "hgt.toggleRevCmplDisp",
	    "hgt.chromName", "hgt.winStart", "hgt.winEnd", "hgt.newWinWidth",
	    "hgt.insideX", "hgt.rulerClickHeight", "hgt.dragSelection", "hgt.revCmplDisp",
	    "hgt.collapseGroups", "hgt.expandGroups", "hgt.suggest",
	    "hgt.jump", "hgt.refresh",
	    "hgt.trackImgOnly", "hgt.ideogramToo", "hgt.trackNameFilter", "hgt.imageV1",
             TRACK_SEARCH,         TRACK_SEARCH_ADD_ROW,     TRACK_SEARCH_DEL_ROW, TRACK_SEARCH_PAGER,
            // stanford additions
            "hgt.out4", "hgt.out5", "hgt.to1", "hgt.to2", "hgt.to3", "hgt.to4", "hgt.to5",
            NULL };

int main(int argc, char *argv[])
{
long enteredMainTime = clock1000();
uglyTime(NULL);
browserName = (hIsPrivateHost() ? "Test Browser" : "Genome Browser");
organization = "UCSC";

/* change title if this is for GSID */
browserName = (hIsGsidServer() ? "Sequence View" : browserName);
organization = (hIsGsidServer() ? "GSID" : organization);
organization = (hIsGisaidServer() ? "GISAID" : organization);

/* Push very early error handling - this is just
 * for the benefit of the cgiVarExists, which
 * somehow can't be moved effectively into doMiddle. */
htmlPushEarlyHandlers();
cgiSpoof(&argc, argv);
htmlSetBackground(hBackgroundImage());
char * link = webTimeStampedLinkToResourceOnFirstCall("HGStyle.css",TRUE); // resource file link wrapped in html
if (link)
    htmlSetStyle(link);

oldVars = hashNew(10);
if (hIsGsidServer())
    cartHtmlShell("GSID Sequence View", doMiddle, hUserCookie(), excludeVars, oldVars);
else
    cartHtmlShell("UCSC Genome Browser v"CGI_VERSION, doMiddle, hUserCookie(), excludeVars, oldVars);
if (measureTiming)
    {
    fprintf(stdout, "Overall total time: %ld millis<BR>\n",
    clock1000() - enteredMainTime);
    }
return 0;
}

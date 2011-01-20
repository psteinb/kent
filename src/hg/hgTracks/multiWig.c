/* A container for multiple wiggles with a couple of options for combining them. */

#include "common.h"
#include "hash.h"
#include "linefile.h"
#include "jksql.h"
#include "dystring.h"
#include "hdb.h"
#include "hgTracks.h"
#include "container.h"
#include "wigCommon.h"
#include "hui.h"


static char *aggregateFromCartOrDefault(struct cart *cart, struct track *tg)
/* Return aggregate value for track. */
{
return cartOrTdbString(cart, tg->tdb, "aggregate", WIG_AGGREGATE_TRANSPARENT);
}

static boolean isOverlayTypeAggregate(char *aggregate)
/* Return TRUE if aggregater type is one of the overlay ones. */
{
if (aggregate == NULL)
    return FALSE;
return differentString(aggregate, WIG_AGGREGATE_NONE);
}


static void multiWigDraw(struct track *tg, int seqStart, int seqEnd,
        struct hvGfx *hvg, int xOff, int yOff, int width, 
        MgFont *font, Color color, enum trackVisibility vis)
/* Draw items in multiWig container. */
{
struct track *subtrack;
char *aggregate = aggregateFromCartOrDefault(cart, tg);
boolean overlay = isOverlayTypeAggregate(aggregate);
int y = yOff;
for (subtrack = tg->subtracks; subtrack != NULL; subtrack = subtrack->next)
    {
    if (isSubtrackVisible(subtrack))
	{
	int height = subtrack->totalHeight(subtrack, vis);
	hvGfxSetClip(hvg, xOff, y, width, height);
        if (sameString(WIG_AGGREGATE_TRANSPARENT, aggregate))
            hvGfxSetWriteMode(hvg, MG_WRITE_MODE_MULTIPLY);
	if (overlay)
	    subtrack->lineHeight = tg->lineHeight;
	subtrack->drawItems(subtrack, seqStart, seqEnd, hvg, xOff, y, width, font, color, vis);
	if (!overlay)
	    y += height + 1;
        hvGfxSetWriteMode(hvg, MG_WRITE_MODE_NORMAL);
	hvGfxUnclip(hvg);
	}
    }
char *url = trackUrl(tg->track, chromName);
mapBoxHgcOrHgGene(hvg, seqStart, seqEnd, xOff, y, width, tg->height, tg->track, tg->track, NULL,
	      url, TRUE, NULL);
}

static int multiWigTotalHeight(struct track *tg, enum trackVisibility vis)
/* Return total height of multiWigcontainer. */
{
char *aggregate = aggregateFromCartOrDefault(cart, tg);
boolean overlay = isOverlayTypeAggregate(aggregate);
int totalHeight =  0;
if (overlay)                                                                                       
    totalHeight =  wigTotalHeight(tg, vis);                                                        
struct track *subtrack;
for (subtrack = tg->subtracks; subtrack != NULL; subtrack = subtrack->next)
    {
    if (isSubtrackVisible(subtrack))
	{
	// Logic is slightly complicated by fact we want to call the totalHeight
	// method for each subtrack even if in overlay mode.
	int oneHeight = subtrack->totalHeight(subtrack, vis);
	if (!overlay)
	    {
	    if (totalHeight != 0)
	       totalHeight += 1;
	    totalHeight += oneHeight;
	    }
	}
    }
tg->height = totalHeight;
return totalHeight;
}

static boolean graphLimitsAllSame(struct track *trackList, struct track **retFirstTrack)
/* Return TRUE if graphUpperLimit and graphLowerLimit same for all tracks. */
{
struct track *firstTrack = NULL;
struct track *track;
for (track = trackList; track != NULL; track = track->next)
    {
    if (isSubtrackVisible(track))
        {
	if (firstTrack == NULL)
	    *retFirstTrack = firstTrack = track;
	else if (track->graphUpperLimit != firstTrack->graphUpperLimit 
	    || track->graphLowerLimit != firstTrack->graphLowerLimit)
	    return FALSE;
	}
    }
return firstTrack != NULL;
}

static void multiWigLeftLabels(struct track *tg, int seqStart, int seqEnd,
	struct hvGfx *hvg, int xOff, int yOff, int width, int height,
	boolean withCenterLabels, MgFont *font, Color color,
	enum trackVisibility vis)
/* Draw left labels - by deferring to first subtrack. */
{
char *aggregate = aggregateFromCartOrDefault(cart, tg);
boolean overlay = isOverlayTypeAggregate(aggregate);
if (overlay)
    {
    struct track *firstVisibleSubtrack = NULL;
    boolean showNumbers = graphLimitsAllSame(tg->subtracks, &firstVisibleSubtrack);
    struct track *subtrack = (showNumbers ? firstVisibleSubtrack : tg->subtracks);
    wigLeftAxisLabels(tg, seqStart, seqEnd, hvg, xOff, yOff, width, height, withCenterLabels,
	    font, color, vis, tg->shortLabel, subtrack->graphUpperLimit, 
	    subtrack->graphLowerLimit, showNumbers);
    }
else
    {
    struct track *subtrack;
    int y = yOff;
    if (withCenterLabels)
       y += tl.fontHeight+1;
    for (subtrack = tg->subtracks; subtrack != NULL; subtrack = subtrack->next)
	{
	if (isSubtrackVisible(subtrack))
	    {
	    int height = subtrack->totalHeight(subtrack, vis);
	    wigLeftAxisLabels(subtrack, seqStart, seqEnd, hvg, xOff, y, width, height, withCenterLabels,
	    	font, subtrack->ixColor, vis, subtrack->shortLabel, subtrack->graphUpperLimit,
		subtrack->graphLowerLimit, TRUE);
	    y += height+1;
	    }
	}
    }
}

void multiWigLoadItems(struct track *track)
/* Load multiWig items. */
{
containerLoadItems(track);
struct track *subtrack;
for (subtrack = track->subtracks; subtrack != NULL; subtrack = subtrack->next)
    {
    subtrack->mapsSelf = FALSE;	/* Round about way to tell wig not to do own mapping. */
    }
}

void multiWigContainerMethods(struct track *track)
/* Override general container methods for multiWig. */
{
track->syncChildVisToSelf = TRUE;
track->loadItems = multiWigLoadItems;
track->totalHeight = multiWigTotalHeight;
track->drawItems = multiWigDraw;
track->drawLeftLabels = multiWigLeftLabels;
}

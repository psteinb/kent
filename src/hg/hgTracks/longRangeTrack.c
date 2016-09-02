
#include "common.h"
#include "hgTracks.h"
#include "longRange.h"


static int longRangeHeight(struct track *tg, enum trackVisibility vis)
/* calculate height of all the snakes being displayed */
{
if ( tg->visibility == tvDense)
    return  tl.fontHeight;
char buffer[1024];
safef(buffer, sizeof buffer, "%s.%s", tg->tdb->track, LONG_HEIGHT );
return tg->height = sqlUnsigned(cartUsualString(cart, buffer, LONG_DEFHEIGHT));
}


static void longRangeDraw(struct track *tg, int seqStart, int seqEnd,
        struct hvGfx *hvg, int xOff, int yOff, int width, 
        MgFont *font, Color color, enum trackVisibility vis)
/* Draw a list of longTabix structures. */
{
double scale = scaleForWindow(width, seqStart, seqEnd);
struct bed *beds = tg->items;
unsigned int maxWidth;
struct longRange *longRange;
char buffer[1024];
char itemBuf[2048];
char statusBuf[2048];

safef(buffer, sizeof buffer, "%s.%s", tg->tdb->track, LONG_MINSCORE);
double minScore = sqlDouble(cartUsualString(cart, buffer, LONG_DEFMINSCORE));
struct longRange *longRangeList = parseLongTabix(beds, &maxWidth, minScore);

for(longRange=longRangeList; longRange; longRange=longRange->next)
    {
    safef(itemBuf, sizeof itemBuf, "%d", longRange->id);
    safef(statusBuf, sizeof statusBuf, "%g %s:%d %s:%d", longRange->score, longRange->sChrom, longRange->s, longRange->eChrom, longRange->e);

    boolean sOnScreen = (longRange->s >= seqStart) && (longRange->s < seqEnd);
    unsigned sx = 0, ex = 0;
    if (sOnScreen)
        sx = (longRange->s - seqStart) * scale + xOff;

    if (differentString(longRange->sChrom, longRange->eChrom))
        {
        if (!sOnScreen)
            continue;

        // draw the foot
        int footWidth = scale * (longRange->sw / 2);
        hvGfxLine(hvg, sx - footWidth, yOff, sx + footWidth, yOff, MG_BLUE);

        int height = tg->height/2;
        if (tg->visibility == tvDense)
            height = tg->height;
        unsigned yPos = yOff + height;
        hvGfxLine(hvg, sx, yOff, sx, yPos, MG_BLUE);
        if (tg->visibility == tvFull)
            {
            mapBoxHgcOrHgGene(hvg, longRange->s, longRange->s, sx - 2, yOff, 4, tg->height/2,
                                   tg->track, itemBuf, statusBuf, NULL, TRUE, NULL);

            safef(buffer, sizeof buffer, "%s:%d",  longRange->eChrom, longRange->e);
            hvGfxTextCentered(hvg, sx, yPos + 2, 4, 4, MG_BLUE, font, buffer);
            int width = vgGetFontStringWidth(hvg->vg, font, buffer);
            int height = vgGetFontPixelHeight(hvg->vg, font);
            mapBoxHgcOrHgGene(hvg, longRange->s, longRange->s, sx - width/2, yPos, width, height,
                                   tg->track, itemBuf, statusBuf, NULL, TRUE, NULL);
            }
        }
    else 
        {
        boolean eOnScreen = (longRange->e >= seqStart) && (longRange->e < seqEnd);
        if (!(sOnScreen || eOnScreen))
            continue;

        if (eOnScreen)
            ex = (longRange->e - seqStart) * scale + xOff;

        double longRangeWidth = longRange->e - longRange->s;
        int peak = (tg->height - 15) * ((double)longRangeWidth / maxWidth) + yOff + 10;
        if (tg->visibility == tvDense)
            peak = yOff + tg->height;
        
        if (sOnScreen)
            {
            int footWidth = scale * (longRange->sw / 2);
            hvGfxLine(hvg, sx - footWidth, yOff, sx + footWidth, yOff, color);
            hvGfxLine(hvg, sx, yOff, sx, peak, color);
            }
        if (eOnScreen)
            {
            int footWidth = scale * (longRange->ew / 2);
            hvGfxLine(hvg, ex - footWidth, yOff, ex + footWidth, yOff, color);
            hvGfxLine(hvg, ex, yOff, ex, peak, color);
            }

        if (tg->visibility == tvFull)
            {
            unsigned sPeak = sOnScreen ? sx : xOff;
            unsigned ePeak = eOnScreen ? ex : xOff + width;

            hvGfxLine(hvg, sPeak, peak, ePeak, peak, color);
            safef(statusBuf, sizeof statusBuf, "%g %s:%d %s:%d", longRange->score, longRange->sChrom, longRange->s, longRange->eChrom, longRange->e);

            if (sOnScreen)
                mapBoxHgcOrHgGene(hvg, longRange->s, longRange->e, sx - 2, yOff, 4, peak - yOff,
                                       tg->track, itemBuf, statusBuf, NULL, TRUE, NULL);
            if (eOnScreen)
                mapBoxHgcOrHgGene(hvg, longRange->s, longRange->e, ex - 2, yOff, 4, peak - yOff,
                                       tg->track, itemBuf, statusBuf, NULL, TRUE, NULL);

            mapBoxHgcOrHgGene(hvg, longRange->s, longRange->e, sPeak, peak-2, ePeak - sPeak, 4,
                                   tg->track, itemBuf, statusBuf, NULL, TRUE, NULL);

            }
        }
    }
}

void longRangeDrawLeftLabels(struct track *tg, int seqStart, int seqEnd,
    struct hvGfx *hvg, int xOff, int yOff, int width, int height,
    boolean withCenterLabels, MgFont *font,
    Color color, enum trackVisibility vis)
{
}

void longRangeMethods(struct track *tg, struct trackDb *tdb)
{
tg->drawLeftLabels = longRangeDrawLeftLabels;
tg->drawItems = longRangeDraw;
tg->totalHeight = longRangeHeight; 
tg->mapsSelf = TRUE;
}

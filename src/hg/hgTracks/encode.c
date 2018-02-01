/* encode.c - hgTracks routines that are specific to the ENCODE project */

/* Copyright (C) 2012 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "hCommon.h"
#include "hdb.h"
#include "hui.h"
#include "hgTracks.h"
#include "customTrack.h"
#include "encode.h"
#include "encode/encodeRna.h"
#include "encode/encodePeak.h"

extern struct trackLayout tl;


char *encodeErgeName(struct track *tg, void *item)
/* return the actual data name, in form xx/yyyy cut off xx/ return yyyy */
{
char *name;
struct linkedFeatures *lf = item;
name = strstr(lf->name, "/");
if (name != NULL)
    name ++;
if (name != NULL)
    return name;
return "unknown";
}

void encodeErgeMethods(struct track *tg)
/* setup special methods for ENCODE dbERGE II tracks */
{
tg->itemName = encodeErgeName;
}

Color encodeStanfordNRSFColor(struct track *tg, void *item, struct hvGfx *hvg)
/* color by strand */
{
struct bed *thisItem = item;
int r = tg->color.r;
int g = tg->color.g;
int b = tg->color.b;

if (thisItem->strand[0] == '-')
    {
    r = g;
    g = b;
    b = tg->color.r;
    }
return hvGfxFindColorIx(hvg, r, g, b);
}

void encodeStanfordNRSFMethods(struct track *tg)
/* custom methods for ENCODE Stanford NRSF data */
{
tg->itemColor = encodeStanfordNRSFColor;
tg->itemNameColor = encodeStanfordNRSFColor;
}



void loadEncodeRna(struct track *tg)
/* Load up encodeRna from database table to track items. */
{
bedLoadItem(tg, "encodeRna", (ItemLoader)encodeRnaLoad);
}

void freeEncodeRna(struct track *tg)
/* Free up encodeRna items. */
{
encodeRnaFreeList((struct encodeRna**)&tg->items);
}

Color encodeRnaColor(struct track *tg, void *item, struct hvGfx *hvg)
/* Return color of encodeRna track item. */
{
struct encodeRna *el = item;

if(el->isRmasked)     return MG_BLACK;
if(el->isTranscribed) return hvGfxFindColorIx(hvg, 0x79, 0xaa, 0x3d);
if(el->isPrediction)  return MG_RED;
return MG_BLUE;
}

char *encodeRnaName(struct track *tg, void *item)
/* Return RNA gene name. */
{
struct encodeRna *el = item;
char *full = el->name;
static char abbrev[SMALLBUF];
char *e;

strcpy(abbrev, skipChr(full));
subChar(abbrev, '_', ' ');
abbr(abbrev, " pseudogene");
if ((e = strstr(abbrev, "-related")) != NULL)
    strcpy(e, "-like");
return abbrev;
}

void encodeRnaMethods(struct track *tg)
/* Make track for rna genes . */
{
tg->loadItems = loadEncodeRna;
tg->freeItems = freeEncodeRna;
tg->itemName = encodeRnaName;
tg->itemColor = encodeRnaColor;
tg->itemNameColor = encodeRnaColor;
}

static struct linkedFeatures *lfFromEncodePeak(struct slList *item, struct trackDb *tdb,
                                        int scoreMin, int scoreMax)
/* Translate an {encode,narrow,broad,gapped}Peak item into a linkedFeatures. */
{
struct encodePeak *peak = (struct encodePeak *)item;
struct linkedFeatures *lf;
struct simpleFeature *sfList = NULL;
if (!peak)
    return NULL;
AllocVar(lf);
lf->start = peak->chromStart;
lf->end = peak->chromEnd;
if (peak->peak > -1)
    {
    lf->tallStart = peak->chromStart + peak->peak;
    lf->tallEnd = lf->tallStart + 1;
    }
lf->filterColor = -1;
lf->orientation = orientFromChar(peak->strand[0]);
adjustBedScoreGrayLevel(tdb, (struct bed *)peak, scoreMin, scoreMax);
lf->grayIx = grayInRange((int)peak->score, scoreMin, scoreMax);
lf->name = cloneString(peak->name);
if (peak->blockCount > 0)
    {
    int i;
    for (i = 0; i < peak->blockCount; i++)
	{
	struct simpleFeature *sf;
	AllocVar(sf);
	sf->start = lf->start + peak->blockStarts[i];
	sf->end = lf->start + peak->blockStarts[i] + peak->blockSizes[i];
	sf->grayIx = lf->grayIx;
	slAddHead(&sfList, sf);
	}
    slReverse(&sfList);
    }
else
    {
    AllocVar(sfList);
    sfList->start = lf->start;
    sfList->end = lf->end;
    sfList->grayIx = lf->grayIx;
    }
lf->components = sfList;
return lf;
}


static char *encodePeakFilter(char *trackName, struct trackDb *tdb, boolean isCustom)
{
struct dyString *extraWhere = newDyString(128);
boolean and = FALSE;
extraWhere = dyAddFilterAsInt(cart,tdb,extraWhere,SCORE_FILTER,"0:1000","score",&and);
extraWhere = dyAddFilterAsDouble(cart,tdb,extraWhere,SIGNAL_FILTER,NULL,"signalValue",&and);
extraWhere = dyAddFilterAsDouble(cart,tdb,extraWhere,PVALUE_FILTER,NULL,"pValue",&and);
extraWhere = dyAddFilterAsDouble(cart,tdb,extraWhere,QVALUE_FILTER,NULL,"qValue",&and);

if (sameString(extraWhere->string, ""))
    return NULL;
return dyStringCannibalize(&extraWhere);
}

struct bigBedFilter
/* Filter on a field in a bigBed file. */
{
struct bigBedFilter *next;
int fieldNum;   // the field number
enum {COMPARE_LESS, COMPARE_MORE, COMPARE_BETWEEN} comparisonType;  // the type of the comparison
double value1, value2;
};

struct bigBedFilter *bigBedMakeFilter(struct cart *cart, struct bbiFile *bbi, struct trackDb *tdb, char *filter, char *defaultLimits,  char *field)
/* Make a filter on this column if the trackDb or cart wants us to. */
{
struct bigBedFilter *ret = NULL;
char *setting = trackDbSettingClosestToHome(tdb, filter);
int fieldNum =  bbExtraFieldIndex(bbi, field) + 3;
if (setting)
    {
    boolean invalid = FALSE;
    double minValueTdb = 0,maxValueTdb = NO_VALUE;
    colonPairToDoubles(setting,&minValueTdb,&maxValueTdb);
    double minLimit=NO_VALUE,maxLimit=NO_VALUE,min=minValueTdb,max=maxValueTdb;
    colonPairToDoubles(defaultLimits,&minLimit,&maxLimit);
    getScoreFloatRangeFromCart(cart,tdb,FALSE,filter,&minLimit,&maxLimit,&min,&max);
    if ((int)minLimit != NO_VALUE || (int)maxLimit != NO_VALUE)
        {
        // assume tdb default values within range!
        // (don't give user errors that have no consequence)
        if ((min != minValueTdb && (((int)minLimit != NO_VALUE && min < minLimit)
                                || ((int)maxLimit != NO_VALUE && min > maxLimit)))
        ||  (max != maxValueTdb && (((int)minLimit != NO_VALUE && max < minLimit)
                                || ((int)maxLimit != NO_VALUE && max > maxLimit))))
            {
            invalid = TRUE;
            char value[64];
            if ((int)max == NO_VALUE) // min only is allowed, but max only is not
                safef(value, sizeof(value), "entered minimum (%g)", min);
            else
                safef(value, sizeof(value), "entered range (min:%g and max:%g)", min, max);
            char limits[64];
            if ((int)minLimit != NO_VALUE && (int)maxLimit != NO_VALUE)
                safef(limits, sizeof(limits), "violates limits (%g to %g)", minLimit, maxLimit);
            else if ((int)minLimit != NO_VALUE)
                safef(limits, sizeof(limits), "violates lower limit (%g)", minLimit);
            else //if ((int)maxLimit != NO_VALUE)
                safef(limits, sizeof(limits), "violates uppper limit (%g)", maxLimit);
            warn("invalid filter by %s: %s %s for track %s", field, value, limits, tdb->track);
            }
        }
    if (invalid)
        {
        char filterLimitName[64];
        safef(filterLimitName, sizeof(filterLimitName), "%s%s", filter, _MIN);
        cartRemoveVariableClosestToHome(cart,tdb,FALSE,filterLimitName);
        safef(filterLimitName, sizeof(filterLimitName), "%s%s", filter, _MAX);
        cartRemoveVariableClosestToHome(cart,tdb,FALSE,filterLimitName);
        }
    else if (((int)min != NO_VALUE && ((int)minLimit == NO_VALUE || minLimit != min))
         ||  ((int)max != NO_VALUE && ((int)maxLimit == NO_VALUE || maxLimit != max)))
         // Assumes min==NO_VALUE or min==minLimit is no filter
         // Assumes max==NO_VALUE or max==maxLimit is no filter!
        {
        AllocVar(ret);
        ret->fieldNum = fieldNum;
        if ((int)max == NO_VALUE || ((int)maxLimit != NO_VALUE && maxLimit == max))
            {
            ret->comparisonType = COMPARE_MORE;
            ret->value1 = min;
            }
        else if ((int)min == NO_VALUE || ((int)minLimit != NO_VALUE && minLimit == min))
            {
            ret->comparisonType = COMPARE_LESS;
            ret->value1 = max;
            }
        else
            {
            ret->comparisonType = COMPARE_BETWEEN;
            ret->value1 = min;
            ret->value2 = max;
            }
        }
    }
return ret;
}

boolean bigBedFilterInterval(char **bedRow, struct bigBedFilter *filters)
/* Go through a row and filter based on filters.  Return TRUE if all filters are passed. */
{
struct bigBedFilter *filter;
for(filter = filters; filter; filter = filter->next)
    {
    double val = atof(bedRow[filter->fieldNum]);

    switch(filter->comparisonType)
        {
        case COMPARE_LESS:
            if (!(val <= filter->value1))
                return FALSE;
            break;
        case COMPARE_MORE:
            if (!(val >= filter->value1))
                return FALSE;
            break;
        case COMPARE_BETWEEN:
            if (!((val >= filter->value1) && (val <= filter->value2)))
                return FALSE;
            break;
        }
    }
return TRUE;
}

void bigNarrowPeakLoadItems(struct track *tg)
/* Load a set of narrowPeaks from a bigNarrowPeak file. */
{
struct linkedFeatures *lfList = NULL;
enum encodePeakType pt = 0;
int scoreMin = atoi(trackDbSettingClosestToHomeOrDefault(tg->tdb, "scoreMin", "0"));
int scoreMax = atoi(trackDbSettingClosestToHomeOrDefault(tg->tdb, "scoreMax", "1000"));
pt = narrowPeak;

tg->customInt = pt;
struct bbiFile *bbi =  fetchBbiForTrack(tg);

struct lm *lm = lmInit(0);
struct bigBedInterval *bb, *bbList =  bigBedIntervalQuery(bbi, chromName, winStart, winEnd, 0, lm);
int fieldCount = 10;
char *bedRow[fieldCount];
char startBuf[16], endBuf[16];
struct bigBedFilter *filters = NULL;
struct bigBedFilter *filter;

if ((filter = bigBedMakeFilter(cart, bbi, tg->tdb, SCORE_FILTER, "0:1000", "score")) != NULL)
    slAddHead(&filters, filter);
if ((filter = bigBedMakeFilter(cart, bbi, tg->tdb, SIGNAL_FILTER, NULL, "signalValue")) != NULL)
    slAddHead(&filters, filter);
if ((filter = bigBedMakeFilter(cart, bbi, tg->tdb, PVALUE_FILTER, NULL, "pValue")) != NULL)
    slAddHead(&filters, filter);
if ((filter = bigBedMakeFilter(cart, bbi, tg->tdb, QVALUE_FILTER, NULL, "qValue")) != NULL)
    slAddHead(&filters, filter);

for (bb = bbList; bb != NULL; bb = bb->next)
    {
    bigBedIntervalToRow(bb, chromName, startBuf, endBuf, bedRow, ArraySize(bedRow));
    if (bigBedFilterInterval(bedRow, filters))
        {
        struct encodePeak *peak = encodePeakGeneralLoad(bedRow, pt);
        struct linkedFeatures *lf = lfFromEncodePeak((struct slList *)peak, tg->tdb, scoreMin, scoreMax);

        if (lf)
            slAddHead(&lfList, lf);
        }
    }

slReverse(&lfList);
slSort(&lfList, linkedFeaturesCmp);
tg->items = lfList;
}

static void encodePeakLoadItemsBoth(struct track *tg, struct customTrack *ct)
/* Load up an encodePeak table from the regular database or the customTrash one. */
{
char *db, *table;
struct sqlConnection *conn;
struct sqlResult *sr = NULL;
char **row;
char *filterConstraints = NULL;
int rowOffset;
struct linkedFeatures *lfList = NULL;
enum encodePeakType pt = 0;
int scoreMin = atoi(trackDbSettingClosestToHomeOrDefault(tg->tdb, "scoreMin", "0"));
int scoreMax = atoi(trackDbSettingClosestToHomeOrDefault(tg->tdb, "scoreMax", "1000"));
if (ct)
    {
    db = CUSTOM_TRASH;
    table = ct->dbTableName;
    }
else
    {
    db = database;
    table = tg->tdb->table;
    }
conn = hAllocConn(db);
pt = encodePeakInferTypeFromTable(db, table, tg->tdb->type);
tg->customInt = pt;
filterConstraints = encodePeakFilter(tg->tdb->track, tg->tdb, (ct!=NULL));
sr = hRangeQuery(conn, table, chromName, winStart, winEnd, filterConstraints, &rowOffset);
while ((row = sqlNextRow(sr)) != NULL)
    {
    struct encodePeak *peak = encodePeakGeneralLoad(row + rowOffset, pt);
    struct linkedFeatures *lf = lfFromEncodePeak((struct slList *)peak, tg->tdb, scoreMin, scoreMax);

    if (lf)
        slAddHead(&lfList, lf);
    }
sqlFreeResult(&sr);
hFreeConn(&conn);
slReverse(&lfList);
slSort(&lfList, linkedFeaturesCmp);
tg->items = lfList;
}

static void encodePeakLoadItemsNormal(struct track *tg)
/* Load the encodePeak table form the database. */
{
encodePeakLoadItemsBoth(tg, NULL);
}

static void encodePeakLoadItemsCt(struct track *tg)
/* Load the encodePeak table form the customTrash database. */
{
struct customTrack *ct = tg->customPt;
encodePeakLoadItemsBoth(tg, ct);
}

static void encodePeakDrawAt(struct track *tg, void *item,
        struct hvGfx *hvg, int xOff, int y, double scale,
        MgFont *font, Color color, enum trackVisibility vis)
/* Draw the peak from the linkedFeature.  Currently this doesn't draw any */
/* sorta shading based on the signalValue/pValue. */
{
struct linkedFeatures *lf = item;
int heightPer = tg->heightPer;
int shortOff = heightPer/4;
int shortHeight = heightPer - 2*shortOff;
char *exonArrows = trackDbSettingClosestToHomeOrDefault(tg->tdb, "exonArrows", "off");
boolean drawArrows = FALSE;
if ((exonArrows != NULL) && sameString(exonArrows, "on"))
    drawArrows = TRUE;
Color rangeColor = shadesOfGray[lf->grayIx];
Color peakColor = (tg->ixColor != blackIndex()) ? tg->ixColor : getOrangeColor();
if (drawArrows || lf->tallStart == 0)
    {
    shortOff = 0;
    shortHeight = heightPer;
    }
if (lf->components)
    {
    struct simpleFeature *sf;
    drawScaledBox(hvg, lf->start, lf->end, scale, xOff, y+(heightPer/2), 1, rangeColor);
    for (sf = lf->components; sf != NULL; sf = sf->next)
	{
        drawScaledBox(hvg, sf->start, sf->end, scale, xOff, y+shortOff,
                      shortHeight, rangeColor);
	if (drawArrows)
	    {
	    int x1 = round((double)(sf->start-winStart)*scale) + xOff;
	    int x2 = round((double)(sf->end-winStart)*scale) + xOff;
	    int w = x2-x1;
            if (w < 1)
                w = 1;

            clippedBarbs(hvg, x1, y + heightPer/2, w, 
                    tl.barbHeight, tl.barbSpacing, lf->orientation,
                    MG_WHITE, FALSE);
            }
	}
    }
else
    drawScaledBox(hvg, lf->start, lf->end, scale, xOff, y+shortOff,
		  shortHeight, rangeColor);
if ((lf->tallEnd > 0) && (lf->tallStart < lf->end))
    drawScaledBox(hvg, lf->tallStart, lf->tallEnd, scale, xOff, y,
		  heightPer, peakColor);
}

char *encodePeakItemName(struct track *tg, void *item)
/* Get rid of the '.' names */
{
struct linkedFeatures *lf = item;
if (lf->name && sameString(lf->name, "."))
    return "";
else
    return lf->name;
}

void encodePeakMethods(struct track *tg)
/* Methods for ENCODE peak track uses mostly linkedFeatures. */
{
linkedFeaturesMethods(tg);
tg->loadItems = encodePeakLoadItemsNormal;
tg->drawItemAt = encodePeakDrawAt;
tg->nextPrevItem = linkedFeaturesLabelNextPrevItem;
tg->itemName = encodePeakItemName;
tg->canPack = TRUE;
}

void encodePeakMethodsCt(struct track *tg)
/* Methods for ENCODE peak track uses mostly linkedFeatures. */
{
encodePeakMethods(tg);
tg->loadItems = encodePeakLoadItemsCt;
}

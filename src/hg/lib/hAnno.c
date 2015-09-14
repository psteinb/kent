/* hAnno -- helpers for creating anno{Streamers,Grators,Formatters,Queries} */

#include "common.h"
#include "hAnno.h"
#include "basicBed.h"
#include "bigGenePred.h"
#include "customTrack.h"
#include "factorSource.h"
#include "grp.h"
#include "hdb.h"
#include "hubConnect.h"
#include "hui.h"
#include "jksql.h"
#include "pgSnp.h"
#include "trackHub.h"
#include "vcf.h"
#include "annoGratorQuery.h"
#include "annoGratorGpVar.h"
#include "annoStreamBigBed.h"
#include "annoStreamBigWig.h"
#include "annoStreamDb.h"
#include "annoStreamDbFactorSource.h"
#include "annoStreamDbKnownGene.h"
#include "annoStreamTab.h"
#include "annoStreamVcf.h"
#include "annoStreamWig.h"
#include "annoGrateWigDb.h"
#include "annoFormatTab.h"
#include "annoFormatVep.h"

//#*** duplicated in hgVarAnnoGrator and annoGratorTester
struct annoAssembly *hAnnoGetAssembly(char *db)
/* Make annoAssembly for db. */
{
static struct annoAssembly *aa = NULL;
if (aa == NULL)
    {
    if (trackHubDatabase(db))
        {
        struct trackHubGenome *thg = trackHubGetGenome(db);
        char *url = thg->twoBitPath;
        aa = annoAssemblyNew(db, url);
        }
    else
        {
        char *nibOrTwoBitDir = hDbDbNibPath(db);
        if (nibOrTwoBitDir == NULL)
            errAbort("Can't find .2bit for db '%s'", db);
        char twoBitPath[HDB_MAX_PATH_STRING];
        safef(twoBitPath, sizeof(twoBitPath), "%s/%s.2bit", nibOrTwoBitDir, db);
        char *path = hReplaceGbdb(twoBitPath);
        aa = annoAssemblyNew(db, path);
        freeMem(path);
        }
    }
return aa;
}

static boolean columnsMatch(struct asObject *asObj, struct sqlFieldInfo *fieldList)
/* Return TRUE if asObj's column names match the given SQL fields. */
{
if (asObj == NULL)
    return FALSE;
struct sqlFieldInfo *firstRealField = fieldList;
if (sameString("bin", fieldList->field) && differentString("bin", asObj->columnList->name))
    firstRealField = fieldList->next;
boolean columnsMatch = TRUE;
struct sqlFieldInfo *field = firstRealField;
struct asColumn *asCol = asObj->columnList;
for (;  field != NULL && asCol != NULL;  field = field->next, asCol = asCol->next)
    {
    if (!sameString(field->field, asCol->name))
	{
	columnsMatch = FALSE;
	break;
	}
    }
if (field != NULL || asCol != NULL)
    columnsMatch = FALSE;
return columnsMatch;
}

static struct asObject *asObjectFromFields(char *name, struct sqlFieldInfo *fieldList,
                                           boolean skipBin)
/* Make autoSql text from SQL fields and pass it to asParse. */
{
struct dyString *dy = dyStringCreate("table %s\n"
				     "\"Column names grabbed from mysql\"\n"
				     "    (\n", name);
struct sqlFieldInfo *field;
for (field = fieldList;  field != NULL;  field = field->next)
    {
    if (skipBin && field == fieldList && sameString("bin", field->field))
        continue;
    char *sqlType = field->type;
    // hg19.wgEncodeOpenChromSynthGm12878Pk.pValue has sql type "float unsigned",
    // and I'd rather pretend it's just a float than work unsigned floats into autoSql.
    if (sameString(sqlType, "float unsigned"))
	sqlType = "float";
    char *asType = asTypeNameFromSqlType(sqlType);
    if (asType == NULL)
	errAbort("No asTypeInfo for sql type '%s'!", field->type);
    dyStringPrintf(dy, "    %s %s;\t\"\"\n", asType, field->field);
    }
dyStringAppend(dy, "    )\n");
return asParseText(dy->string);
}

static struct asObject *getAutoSqlForTable(char *db, char *table, struct trackDb *tdb,
                                           boolean skipBin)
/* Get autoSql for db.dbTable from tdb and/or db.tableDescriptions;
 * if it doesn't match columns, make one up from db.table sql fields.
 * Some subtleties are lost in translation from .as to .sql, that's why
 * we try tdb & db.tableDescriptions first.  But ultimately we need to return
 * an asObj whose columns match all fields of the table. */
{
struct sqlConnection *conn = hAllocConn(db);
struct sqlFieldInfo *fieldList = sqlFieldInfoGet(conn, table);
struct asObject *asObj = NULL;
if (tdb != NULL)
    asObj = asForTdb(conn, tdb);
hFreeConn(&conn);
if (columnsMatch(asObj, fieldList))
    return asObj;
else
    return asObjectFromFields(table, fieldList, skipBin);
}

static char *getBigDataFileName(char *db, struct trackDb *tdb, char *selTable, char *chrom)
/* Get fileName from bigBed/bigWig/BAM/VCF database table, or bigDataUrl from custom track. */
{
char *bigDataUrl = trackDbSetting(tdb, "bigDataUrl");
if (isNotEmpty(bigDataUrl))
    {
    return bigDataUrl;
    }
else
    {
    struct sqlConnection *conn = hAllocConn(db);
    char *fileOrUrl = bbiNameFromSettingOrTableChrom(tdb, conn, selTable, chrom);
    hFreeConn(&conn);
    return fileOrUrl;
    }
}

static boolean dbTableMatchesAutoSql(char *db, char *table, struct asObject *asObj)
/* Return true if table exists and its fields match the columns of asObj. */
{
boolean matches = FALSE;
struct sqlConnection *conn = hAllocConn(db);
if (sqlTableExists(conn, table))
    {
    struct sqlFieldInfo *fieldList = sqlFieldInfoGet(conn, table);
    matches = columnsMatch(asObj, fieldList);
    }
hFreeConn(&conn);
return matches;
}

struct annoStreamer *hAnnoStreamerFromTrackDb(struct annoAssembly *assembly, char *selTable,
                                              struct trackDb *tdb, char *chrom, int maxOutRows)
/* Figure out the source and type of data and make an annoStreamer. */
{
struct annoStreamer *streamer = NULL;
char *db = assembly->name, *dataDb = db, *dbTable = selTable;
if (chrom == NULL)
    chrom = hDefaultChrom(db);
if (isCustomTrack(selTable))
    {
    dbTable = trackDbSetting(tdb, "dbTableName");
    if (dbTable != NULL)
	// This is really a database table, not a bigDataUrl CT.
	dataDb = CUSTOM_TRASH;
    }
if (startsWithWord("wig", tdb->type))
    streamer = annoStreamWigDbNew(dataDb, dbTable, assembly, maxOutRows);
else if (sameString("vcfTabix", tdb->type))
    {
    char *fileOrUrl = getBigDataFileName(dataDb, tdb, selTable, chrom);
    streamer = annoStreamVcfNew(fileOrUrl, TRUE, assembly, maxOutRows);
    }
else if (sameString("vcf", tdb->type))
    {
    char *fileOrUrl = getBigDataFileName(dataDb, tdb, dbTable, chrom);
    streamer = annoStreamVcfNew(fileOrUrl, FALSE, assembly, maxOutRows);
    }
else if (sameString("bam", tdb->type))
    {
    warn("Sorry, BAM is not yet supported");
    }
else if (startsWith("bigBed", tdb->type) || sameString("bigGenePred", tdb->type))
    {
    char *fileOrUrl = getBigDataFileName(dataDb, tdb, selTable, chrom);
    streamer = annoStreamBigBedNew(fileOrUrl, assembly, maxOutRows);
    }
else if (startsWith("bigWig", tdb->type))
    {
    char *fileOrUrl = getBigDataFileName(dataDb, tdb, selTable, chrom);
    streamer = annoStreamBigWigNew(fileOrUrl, assembly); //#*** no maxOutRows support
    }
else if (sameString("factorSource", tdb->type) &&
         dbTableMatchesAutoSql(dataDb, tdb->table, factorSourceAsObj()))
    {
    char *sourceTable = trackDbSetting(tdb, "sourceTable");
    char *inputsTable = trackDbSetting(tdb, "inputTrackTable");
    streamer = annoStreamDbFactorSourceNew(dataDb, tdb->track, sourceTable, inputsTable, assembly,
					   maxOutRows);
    }
else if (sameString("knownGene", tdb->track))
    {
    struct sqlConnection *conn = hAllocConn(dataDb);
    if (sqlTableExists(conn, "knownGene") && sqlTableExists(conn, "kgXref"))
        streamer = annoStreamDbKnownGeneNew(dataDb, assembly, maxOutRows);
    hFreeConn(&conn);
    }
else if (trackHubDatabase(db))
    errAbort("Unrecognized type '%s' for hub track '%s'", tdb->type, tdb->track);
if (streamer == NULL)
    {
    char maybeSplitTable[HDB_MAX_TABLE_STRING];
    if (!hFindSplitTable(dataDb, chrom, dbTable, maybeSplitTable, NULL))
        errAbort("hAnnoStreamerFromTrackDb: can't find table (or split table) for '%s.%s'",
                 dataDb, dbTable);
    struct asObject *asObj = getAutoSqlForTable(dataDb, maybeSplitTable, tdb, TRUE);
    streamer = annoStreamDbNew(dataDb, maybeSplitTable, assembly, asObj, maxOutRows);
    }
return streamer;
}

struct annoGrator *hAnnoGratorFromBigFileUrl(char *fileOrUrl, struct annoAssembly *assembly,
                                             int maxOutRows, enum annoGratorOverlap overlapRule)
/* Determine what kind of big data file/url we have and make streamer & grator for it. */
{
struct annoStreamer *streamer = NULL;
struct annoGrator *grator = NULL;
char *type = customTrackTypeFromBigFile(fileOrUrl);
if (sameString(type, "bigBed") || sameString("bigGenePred", type))
    streamer = annoStreamBigBedNew(fileOrUrl, assembly, maxOutRows);
else if (sameString(type, "vcfTabix"))
    streamer = annoStreamVcfNew(fileOrUrl, TRUE, assembly, maxOutRows);
else if (sameString(type, "bigWig"))
    grator = annoGrateBigWigNew(fileOrUrl, assembly, agwmAverage);
else if (sameString(type, "bam"))
    errAbort("Sorry, BAM is not yet supported");
else
    errAbort("Unrecognized bigData type %s of file or url '%s'", type, fileOrUrl);
if (grator == NULL)
    grator = annoGratorNew(streamer);
grator->setOverlapRule(grator, overlapRule);
return grator;
}

struct annoGrator *hAnnoGratorFromTrackDb(struct annoAssembly *assembly, char *selTable,
                                          struct trackDb *tdb, char *chrom, int maxOutRows,
                                          struct asObject *primaryAsObj,
                                          enum annoGratorOverlap overlapRule)
/* Figure out the source and type of data, make an annoStreamer & wrap in annoGrator.
 * If not NULL, primaryAsObj is used to determine whether we can make an annoGratorGpVar. */
{
struct annoGrator *grator = NULL;
boolean primaryIsVariants = (primaryAsObj != NULL &&
                             (asObjectsMatch(primaryAsObj, pgSnpAsObj()) ||
                              asObjectsMatch(primaryAsObj, vcfAsObj())));
char *bigDataUrl = trackDbSetting(tdb, "bigDataUrl");
if (bigDataUrl != NULL)
    {
    if (primaryIsVariants && sameString("bigGenePred", tdb->type))
        {
        struct annoStreamer *streamer = annoStreamBigBedNew(bigDataUrl, assembly, maxOutRows);
        grator = annoGratorGpVarNew(streamer);
        }
    else
        grator = hAnnoGratorFromBigFileUrl(bigDataUrl, assembly, maxOutRows, overlapRule);
    }
else if (startsWithWord("wig", tdb->type))
    grator = annoGrateWigDbNew(assembly->name, selTable, assembly, agwmAverage, maxOutRows);
else if (startsWithWord("bigWig", tdb->type))
    {
    char *fileOrUrl = getBigDataFileName(assembly->name, tdb, tdb->table, chrom);
    grator = annoGrateBigWigNew(fileOrUrl, assembly, agwmAverage);
    }
else
    {
    struct annoStreamer *streamer = hAnnoStreamerFromTrackDb(assembly, selTable, tdb, chrom,
                                                             maxOutRows);
    if (primaryIsVariants &&
        (asColumnNamesMatchFirstN(streamer->asObj, genePredAsObj(), 10) ||
         asObjectsMatch(streamer->asObj, bigGenePredAsObj())))
	grator = annoGratorGpVarNew(streamer);
    else
	grator = annoGratorNew(streamer);
    }
grator->setOverlapRule(grator, overlapRule);
return grator;
}

static struct asObject *getAutoSqlForType(char *db, char *chrom, struct trackDb *tdb)
/* Return an asObject for tdb->type if recognized as a hub or custom track type. */
{
struct asObject * asObj = NULL;
if (startsWith("wig", tdb->type) || startsWith("bigWig", tdb->type))
    asObj = annoStreamBigWigAsObject();
else if (startsWith("vcf", tdb->type))
    asObj = vcfAsObj();
else if (startsWith("bigBed", tdb->type) || sameString("bigGenePred", tdb->type))
    {
    char *fileOrUrl = getBigDataFileName(db, tdb, tdb->table, chrom);
    asObj = bigBedFileAsObjOrDefault(fileOrUrl);
    }
else if (sameString("pgSnp", tdb->type))
    asObj = pgSnpAsObj();
else if (sameString("bam", tdb->type) || sameString("maf", tdb->type))
    warn("Sorry, %s is not yet supported", tdb->type);
else if (startsWithWord("bed", tdb->type) && !strchr(tdb->type, '+'))
    {
    // BED with no + fields; parse bed field count out of type line.
    int bedFieldCount = 3;
    char typeCopy[PATH_LEN];
    safecpy(typeCopy, sizeof(typeCopy), tdb->type);
    char *words[8];
    int wordCount = chopLine(typeCopy, words);
    if (wordCount > 1)
        bedFieldCount = atoi(words[1]);
    asObj = asParseText(bedAsDef(bedFieldCount, bedFieldCount));
    }
else if (sameString(tdb->track, "knownGene"))
    {
    if (hTableExists(db, "knownGene") && hTableExists(db, "kgXref"))
        asObj = annoStreamDbKnownGeneAsObj();
    }
else if (sameString("factorSource", tdb->type) &&
         dbTableMatchesAutoSql(db, tdb->table, factorSourceAsObj()))
    {
    asObj = annoStreamDbFactorSourceAsObj();
    }
return asObj;
}

struct asObject *hAnnoGetAutoSqlForTdb(char *db, char *chrom, struct trackDb *tdb)
/* If possible, return the asObj that a streamer for this track would use, otherwise NULL. */
{
struct asObject *asObj = getAutoSqlForType(db, chrom, tdb);

if (!asObj && !isHubTrack(tdb->track))
    {
    // If none of the above, it must be a database table; deduce autoSql from sql fields.
    char *dataDb = db, *dbTable = tdb->table;
    if (isCustomTrack(tdb->track))
        {
        dbTable = trackDbSetting(tdb, "dbTableName");
        if (dbTable)
            dataDb = CUSTOM_TRASH;
        else
            return NULL;
        }
    char maybeSplitTable[HDB_MAX_TABLE_STRING];
    if (!hFindSplitTable(dataDb, chrom, dbTable, maybeSplitTable, NULL))
        errAbort("hAnnoGetAutoSqlForTdb: can't find table (or split table) for '%s.%s'",
                 dataDb, dbTable);
    asObj = getAutoSqlForTable(dataDb, maybeSplitTable, tdb, TRUE);
    }
return asObj;
}


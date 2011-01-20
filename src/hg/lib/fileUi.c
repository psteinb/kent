/* fileUi.c - human genome file downloads common controls. */

#include "common.h"
#include "hash.h"
#include "cheapcgi.h"
#include "jsHelper.h"
#include "cart.h"
#include "hdb.h"
#include "fileUi.h"
#include "hui.h"
#include "obscure.h"
#include "mdb.h"

// FIXME: Move to hui.h since hui.c also needs this
#define ENCODE_DCC_DOWNLOADS "encodeDCC"


void fileDbFree(struct fileDb **pFileList)
// free one or more fileDb objects
{
while (pFileList && *pFileList)
    {
    struct fileDb *oneFile = slPopHead(pFileList);

    freeMem(oneFile->fileName);
    freeMem(oneFile->fileType);
    freeMem(oneFile->fileDate);
    freeMem(oneFile->sortFields);
    freeMem(oneFile->reverse);
    mdbObjsFree(&(oneFile->mdb));
    freeMem(oneFile);
    }
}

struct fileDb *fileDbGet(char *db, char *dir, char *subDir, char *fileName)
// Returns NULL or if found a fileDb struct with name, size and date filled in.
{
static char *savedDb     = NULL;
static char *savedDir    = NULL;
static char *savedSubDir = NULL;
static struct fileDb *foundFiles = NULL;// Use static list to save excess IO
struct fileDb *oneFile = NULL;
if (foundFiles == NULL
||  savedDb  == NULL    || differentString(savedDb, db)
||  savedDir == NULL    || differentString(savedDir,dir)
||  savedSubDir == NULL || differentString(savedSubDir,subDir))
    {
    // free up any static mem
    freeMem(savedDb);
    freeMem(savedDir);
    freeMem(savedSubDir);
    fileDbFree(&foundFiles);

    FILE *scriptOutput = NULL;
    char buf[1024];
    char cmd[512];
    char *words[10];
    char *server = hDownloadsServer();
    if (sameString(server,"hgdownload-test.cse.ucsc.edu")) // genome-test is different
        {
        // Does not work: rsync -avn rsync://hgdownload-test.cse.ucsc.edu/goldenPath/hg19/encodeDCC/wgEncodeBroadHistone
        // Use ls -log --time=ctime --time-style=long-iso /usr/local/apache/htdocs-hgdownload/goldenPath/hg19/encodeDCC/wgEncodeBroadHistone
        safef(cmd,sizeof(cmd),"ls -log --time-style=long-iso /usr/local/apache/htdocs-hgdownload/goldenPath/%s/%s/%s/", db,dir,subDir);
        }
    else  // genome and hgwbeta can use rsync
        {
        // Works:         rsync -avn rsync://hgdownload.cse.ucsc.edu/goldenPath/hg18/encodeDCC/wgEncodeBroadChipSeq/
        //safef(cmd,sizeof(cmd),"rsync -avn rsync://%s/goldenPath/%s/%s/%s/%s | grep %s", server, db, subDir,dir,fileName,fileName);
        safef(cmd,sizeof(cmd),"rsync -avn rsync://%s/goldenPath/%s/%s/%s/", server, db, dir, subDir);
        }
    //warn("cmd: %s",cmd);
    scriptOutput = popen(cmd, "r");
    while(fgets(buf, sizeof(buf), scriptOutput))
        {
        eraseTrailingSpaces(buf);
        if (!endsWith(buf,".md5sum")) // Just ignore these
            {
            int count = chopLine(buf, words);
            if (count >= 6 && sameString(server,"hgdownload-test.cse.ucsc.edu")) // genome-test is different
                {
                //-rw-rw-r-- 5  502826550 2010-10-22 16:51 /usr/local/apache/htdocs-hgdownload/goldenPath/hg19/encodeDCC/wgEncodeBroadHistone/wgEncodeBroadHistoneGm12878ControlStdRawDataRep1.fastq.gz
                AllocVar(oneFile);
                oneFile->fileSize = sqlUnsignedLong(words[2]);
                oneFile->fileDate = cloneString(words[3]);
                char *atSlash = strrchr(words[5], '/');
                if (atSlash != NULL)
                    oneFile->fileName = cloneString(atSlash + 1);
                else
                    oneFile->fileName = cloneString(words[5]);
                slAddHead(&foundFiles,oneFile);
                }
            else if (count == 5 && differentString(server,"hgdownload-test.cse.ucsc.edu"))// genome and hgwbeta can use rsync
                {
                //-rw-rw-r--    26420982 2009/09/29 14:53:30 wgEncodeBroadChipSeq/wgEncodeBroadChipSeqSignalNhlfH4k20me1.wig.gz
                AllocVar(oneFile);
                oneFile->fileSize = sqlUnsignedLong(words[1]);
                oneFile->fileDate = cloneString(words[2]);
                strSwapChar(oneFile->fileDate,'/','-');// Standardize YYYY-MM-DD, no time
                oneFile->fileName = cloneString(words[4]);
                slAddHead(&foundFiles,oneFile);
                }
            //warn("File:%s  size:%ld",foundFiles->fileName,foundFiles->fileSize);
            }
        }
    pclose(scriptOutput);

    if (foundFiles == NULL)
        {
        AllocVar(oneFile);
        oneFile->fileName = cloneString("No files found!");
        oneFile->fileDate = cloneString(cmd);
        slAddHead(&foundFiles,oneFile);
        warn("No files found for command:\n%s",cmd);
        return NULL;
        }

    // mark this as done to avoid excessive io
    savedDb     = cloneString(db);
    savedDir    = cloneString(dir);
    savedSubDir = cloneString(subDir);
    }

// special code that only gets called in debug mode
if (sameString(fileName,"listAll"))
    {
    for(oneFile=foundFiles;oneFile;oneFile=oneFile->next)
        warn("%s",oneFile->fileName);
    return NULL;
    }
// Look up the file and return it
struct fileDb *newList = NULL;
while (foundFiles)
    {
    oneFile = slPopHead(&foundFiles);
    if (sameString(fileName,oneFile->fileName))
        break;  // Found means removed from list: shorter list for next file.

    slAddHead(&newList,oneFile);
    oneFile = NULL;
    }
if (newList)
    foundFiles = slCat(foundFiles,newList);  // Order does not remain the same

if (oneFile != NULL && oneFile->fileType == NULL)
    {
    char *suffix = strchr(oneFile->fileName, '.');
    if (suffix != NULL && strlen(suffix) > 2)
        {
        oneFile->fileType = cloneString(suffix + 1);
        if (endsWith(oneFile->fileType,".gz"))
            chopSuffix(oneFile->fileType);
        }
    }
return oneFile;
}

static sortOrder_t *fileSortOrderGet(struct cart *cart,struct trackDb *parentTdb,struct mdbObj *mdbObjs)
/* Parses 'fileSortOrder' trackDb/cart instructions and returns a sort order struct or NULL.
   Some trickiness here.  sortOrder->sortOrder is from cart (changed by user action), as is sortOrder->order,
   But columns are in original tdb order (unchanging)!  However, if cart is null, all is from trackDb.ra */
{
int ix;
sortOrder_t *sortOrder = NULL;
char *carveSetting = NULL,*setting = NULL;
if (parentTdb)
    setting = trackDbSetting(parentTdb, FILE_SORT_ORDER);
if(setting)
    {
    sortOrder = needMem(sizeof(sortOrder_t));
    sortOrder->setting = cloneString(setting);
    carveSetting = sortOrder->setting;
    }
else
    {
    if (mdbObjs != NULL)
        {
        struct dyString *dySortFields = dyStringNew(512);
        struct mdbObj *commonVars = mdbObjsCommonVars(mdbObjs);
        // Problem with making common fieds as sorable is that it REQUIRES a fixed sort order
        char *sortables[] = {"grant","lab","dataType","cell","strain","age","obtainedBy", "rnaExtract","localization","phase","treatment","antibody","protocol",
                      "labProtocolId","restrictionEnzyme","control","replicate","expId","labExpId","setType","view","submittedDataVersion","subId",
                      "dateSubmitted","dateResubmitted","dateUnrestricted","dataVersion","origAssembly"};//"labVersion","softwareVersion",
        // Not included:    no:not searchable
        // accession no, annotation no,   bioRep no,     composite no, controlId no, dccInternalNotes no, fileIndex no,  fileName no,
        // fragSize no,  fragLength no,   freezeDate no, geoSample,    geoSeries,    insertLength no,     labVersion,    level no,
        // mapAlgorithm, privacy no,      rank no,       readType,     seqPlatform,  sex,                 size no,       softwareVersion,
        // tableName no, uniqueness no

        int ix = 0, count = sizeof(sortables)/sizeof(char *);
        for (ix=0;ix<count;ix++)
            {
            // If sortables[ix] is in common vars then then add it to the settings field
            if (mdbObjContains(commonVars,sortables[ix],NULL))
                {
                // TODO: This would be good, but valuable information slips away.  This is especially true in fileSearch
                //       Maybe another whiteList?
                //if (mdbRemoveCommonVar(mdbObjs, sortables[ix])) // Don't bother if all the vals are the same
                //    continue;
                dyStringPrintf(dySortFields,"%s=%s ",sortables[ix],strSwapChar(cloneString(cvLabel(sortables[ix])),' ','_'));
                }
            }
        if (dyStringLen(dySortFields))
            {
            dyStringAppend(dySortFields,"fileSize=Size fileType=File_Type");
            setting = dyStringCannibalize(&dySortFields);
            }
        else
            dyStringFree(&dySortFields);
        mdbObjsFree(&commonVars);
        }
    if(setting == NULL) // Must be in trackDb or not a sortable list of files
        {
        #define FILE_SORT_ORDER_DEFAULT "cell=Cell_Line lab=Lab view=View replicate=Rep fileSize=Size fileType=File_Type dateSubmitted=Submitted dateUnrestricted=RESTRICTED<BR>Until"
        setting = FILE_SORT_ORDER_DEFAULT;
        }
    sortOrder = needMem(sizeof(sortOrder_t));
    carveSetting = cloneString(setting);
    sortOrder->setting = NULL;
    }

if (parentTdb)
    {
    sortOrder->htmlId = needMem(strlen(parentTdb->track)+20);
    safef(sortOrder->htmlId, (strlen(parentTdb->track)+20), "%s.%s", parentTdb->track,FILE_SORT_ORDER);
    if(cart != NULL)
        sortOrder->sortOrder = cloneString(cartOptionalString(cart, sortOrder->htmlId));
    }

sortOrder->count = chopByWhite(carveSetting,NULL,0);  // Get size
sortOrder->column  = needMem(sortOrder->count*sizeof(char*));
sortOrder->count = chopByWhite(carveSetting,sortOrder->column,sortOrder->count);
sortOrder->title   = needMem(sortOrder->count*sizeof(char*));
sortOrder->forward = needMem(sortOrder->count*sizeof(boolean));
sortOrder->order   = needMem(sortOrder->count*sizeof(int));
for (ix = 0; ix<sortOrder->count; ix++)
    {
    // separate out mtaDb var in sortColumn from title
    sortOrder->title[ix] = strchr(sortOrder->column[ix],'='); // Could be 'cell=Cell_Line'
    if (sortOrder->title[ix] != NULL)
        {
        sortOrder->title[ix][0] = '\0';
        sortOrder->title[ix]   = strSwapChar(sortOrder->title[ix]+1,'_',' '); // +1 jumps to next char after '='
        }
    else
        sortOrder->title[ix] = sortOrder->column[ix];         // or could be just 'cell'

    // Sort order defaults to forward but may be found in a cart var
    sortOrder->order[ix] = ix+1;
    sortOrder->forward[ix] = TRUE;
    if (sortOrder->sortOrder != NULL)
        {
        char *pos = stringIn(sortOrder->column[ix], sortOrder->sortOrder);// find tdb substr in cart current order string
        if(pos != NULL && pos[strlen(sortOrder->column[ix])] == '=')
            {
            int ord=1;
            char* pos2 = sortOrder->sortOrder;
            for(;*pos2 && pos2 < pos;pos2++)
                {
                if(*pos2 == '=') // Discovering sort order in cart
                    ord++;
                }
            sortOrder->forward[ix] = (pos[strlen(sortOrder->column[ix]) + 1] == '+');
            sortOrder->order[ix] = ord;
            }
        }
    }
if (sortOrder->sortOrder == NULL)
    sortOrder->sortOrder = cloneString(setting);      // no order in cart, all power to trackDb
return sortOrder;  // NOTE cloneString:words[0]==*sortOrder->column[0] and will be freed when sortOrder is freed
}

static int fileDbSortCmp(const void *va, const void *vb)
// Compare two sortable tdb items based upon sort columns.
{
const struct fileDb *a = *((struct fileDb **)va);
const struct fileDb *b = *((struct fileDb **)vb);
char **fieldsA = a->sortFields;
char **fieldsB = b->sortFields;
int ix=0;
int compared = 0;
while(fieldsA[ix] != NULL && fieldsB[ix] != NULL && compared == 0)
    {
    compared = strcmp(fieldsA[ix], fieldsB[ix]) * (a->reverse[ix]? -1: 1);
    ix++;
    }
return compared;
}

static void fileDbSortList(struct fileDb **fileList, sortOrder_t *sortOrder)
{// If sortOrder struct provided, will update sortFields in fileList, then sort it
if (sortOrder && fileList)
    {
    struct fileDb *oneFile = NULL;
    for(oneFile = *fileList;oneFile != NULL;oneFile=oneFile->next)
        {
        oneFile->sortFields = needMem(sizeof(char *)    * (sortOrder->count + 1)); // +1 Null terminated
        oneFile->reverse    = needMem(sizeof(boolean *) *  sortOrder->count);
        int ix;
        for(ix=0;ix<sortOrder->count;ix++)
            {
            char *field = NULL;
            if (sameString("fileSize",sortOrder->column[ix]))
                {
                char niceNumber[32];
                sprintf(niceNumber, "%.15lu", oneFile->fileSize);
                field = cloneString(niceNumber);
                }
            else if (sameString("fileType",sortOrder->column[ix]))
                field = oneFile->fileType;
            else
                field = mdbObjFindValue(oneFile->mdb,sortOrder->column[ix]);

            if (field)
                {
                oneFile->sortFields[sortOrder->order[ix] - 1] = field;
                oneFile->reverse[   sortOrder->order[ix] - 1] = (sortOrder->forward[ix] == FALSE);
                }
            else
                {
                oneFile->sortFields[sortOrder->order[ix] - 1] = NULL;
                oneFile->reverse[   sortOrder->order[ix] - 1] = FALSE;
                }
            oneFile->sortFields[sortOrder->count] = NULL;
            }
        }
    slSort(fileList,fileDbSortCmp);
    }
}

static void filesDownloadsPreamble(char *db, struct trackDb *tdb)
{
// Do not bother getting preamble.html
// 1) It isn't on the RR (yet)
// 2) It will likely refer back to the composite for Description info which is included in this page
// 3) The rsync-to-tmp-file hurdle isn't worth the effort.
puts("<p><B>Data is <A HREF='http://genome.ucsc.edu/ENCODE/terms.html'>RESTRICTED FROM USE</a>");
puts("in publication  until the restriction date noted for the given data file.</B></p>");

struct fileDb *oneFile = fileDbGet(db, ENCODE_DCC_DOWNLOADS, tdb->track, "supplemental");
if (oneFile != NULL)
    {
    printf("<p>\n<B>Supplemental materials</b> may be found <A HREF='http://%s/goldenPath/%s/%s/%s/supplemental/' TARGET=ucscDownloads>here</A>.</p>\n",
          hDownloadsServer(),db,ENCODE_DCC_DOWNLOADS, tdb->track);
    }
puts("<p>\nThere are two files within this directory that contain information about the downloads:");
printf("<BR>&#149;&nbsp;<A HREF='http://%s/goldenPath/%s/%s/%s/files.txt' TARGET=ucscDownloads>files.txt</A> which is a tab-separated file with the name and metadata for each download.</LI>\n",
                hDownloadsServer(),db,ENCODE_DCC_DOWNLOADS, tdb->track);
printf("<BR>&#149;&nbsp;<A HREF='http://%s/goldenPath/%s/%s/%s/md5sum.txt' TARGET=ucscDownloads>md5sum.txt</A> which is a list of the md5sum output for each download.</LI>\n",
                hDownloadsServer(),db,ENCODE_DCC_DOWNLOADS, tdb->track);


puts("<P>");
}

static int filesPrintTable(char *db, struct trackDb *parentTdb, struct fileDb *fileList, sortOrder_t *sortOrder)
// Prints filesList as a sortable table. Returns count
{
// Table class=sortable
int columnCount = 0;
int restrictedColumn = 0;
char *nowrap = (sortOrder->setting != NULL ? " nowrap":""); // Sort order trackDb setting found so rely on <BR> in titles for wrapping
printf("<TABLE class='sortable' style='border: 2px outset #006600;'>\n");
printf("<THEAD class='sortable'>\n");
printf("<TR class='sortable' valign='bottom'>\n");
printf("<TD align='center' valign='center'>&nbsp;");
int filesCount = slCount(fileList);
if (filesCount > 5)
    printf("<i>%d files</i>",filesCount);    //puts("<FONT class='subCBcount'></font>"); // Use this style when filterboxes are up and running
//if (sortOrder) // NOTE: This could be done to preserve sort order   FIXME: However hgFileUi would need form OR changes would need to be ajaxed over AND hgsid would be needed.
//    printf("<INPUT TYPE=HIDDEN NAME='%s' class='sortOrder' VALUE=\"%s\">",sortOrder->htmlId, sortOrder->sortOrder);
printf("</TD>\n");
columnCount++;

/*#define SHOW_FOLDER_FRO_COMPOSITE_DOWNLOADS
#ifdef SHOW_FOLDER_FRO_COMPOSITE_DOWNLOADS
if (parentTdb == NULL)
    {
    printf("<TD align='center' valign='center'>&nbsp;</TD>");
    columnCount++;
    }
#endif///def SHOW_FOLDER_FRO_COMPOSITE_DOWNLOADS
*/
// Now the columns
int curOrder = 0,ix=0;
if (sortOrder)
    {
    curOrder = sortOrder->count;
    for(ix=0;ix<sortOrder->count;ix++)
        {
        char *align = (sameString("labVersion",sortOrder->column[ix]) || sameString("softwareVersion",sortOrder->column[ix]) ? " align='left'":"");
        printf("<TH class='sortable sort%d%s' %s%s%s>%s</TH>\n",
            sortOrder->order[ix],(sortOrder->forward[ix]?"":" sortRev"),
            (sameString("fileSize",sortOrder->column[ix])?"abbr='use' ":""),
            nowrap,align,sortOrder->title[ix]); // keeing track of sortOrder
        columnCount++;
        if (sameWord(sortOrder->column[ix],"dateUnrestricted"))
            restrictedColumn = columnCount;
        }
    }
//#define INCLUDE_FILENAMES
#ifndef INCLUDE_FILENAMES
else
#endif///defn INCLUDE_FILENAMES
    {
    printf("<TH class='sortable sort%d' nowrap>File Name</TH>\n",++curOrder);
    columnCount++;
    }
printf("<TH class='sortable sort%d' align='left' nowrap>Additional Details</TH>\n",++curOrder);
columnCount++;
printf("</TR></THEAD>\n");

// Now the files...
struct fileDb *oneFile = fileList;
printf("<TBODY class='sortable sorting'>\n"); // 'sorting' is a fib but it conveniently greys the list till the table is initialized.
for( ;oneFile!= NULL;oneFile=oneFile->next)
    {
    oneFile->mdb->next = NULL; // mdbs were in list for generating sortOrder, but list no longer needed
    char *field = NULL;

    printf("<TR valign='top'>");   // TODO: BUILD IN THE CLASSES TO ALLOW FILTERBOXES TO WORK!!!

    // Download button
    printf("<TD nowrap>");
    if (parentTdb)
        field = parentTdb->track;
    else
        field = mdbObjFindValue(oneFile->mdb,"composite");
    assert(field != NULL);
    printf("<A HREF='http://%s/goldenPath/%s/%s/%s/%s' title='Download %s ...' TARGET=ucscDownloads>",
                hDownloadsServer(),db,ENCODE_DCC_DOWNLOADS, field, oneFile->fileName, oneFile->fileName);
    printf("<input type='button' value='Download'>");
    printf("</a>");

#define SHOW_FOLDER_FRO_COMPOSITE_DOWNLOADS
#ifdef SHOW_FOLDER_FRO_COMPOSITE_DOWNLOADS
    if (parentTdb == NULL)
        printf("&nbsp;<A href='../cgi-bin/hgFileUi?db=%s&g=%s' title='Navigate to downloads page for %s set...'><IMG SRC='../images/folderC.png'></a>&nbsp;", db,field,field);
#endif///def SHOW_FOLDER_FRO_COMPOSITE_DOWNLOADS
    printf("</TD>\n");

    // Each of the pulled out mdb vars
    if (sortOrder)
        {
        for(ix=0;ix<sortOrder->count;ix++)
            {
            char *align = (sameString("labVersion",sortOrder->column[ix]) || sameString("softwareVersion",sortOrder->column[ix]) ? " align='left'":" align='center'");
            if (sameString("fileSize",sortOrder->column[ix]))
                {
                char niceNumber[128];
                sprintWithGreekByte(niceNumber, sizeof(niceNumber), oneFile->fileSize);
                field = oneFile->sortFields[sortOrder->order[ix] - 1];
                printf("<TD abbr='%s' align='right' nowrap>%s</td>",field,niceNumber);
                }
            else
                {
                field = oneFile->sortFields[sortOrder->order[ix] - 1];
                if (sameString("dateUnrestricted",sortOrder->column[ix]) && field && dateIsOld(field,"%F"))
                    printf("<TD%s nowrap style='color: #BBBBBB;'>%s</td>",align,field);
                else
                    printf("<TD%s nowrap>%s</td>",align,field?field:" &nbsp;");
                if (!sameString("fileType",sortOrder->column[ix]))
                    mdbObjRemoveVars(oneFile->mdb,sortOrder->column[ix]); // Remove this from mdb now so that it isn't displayed in "extras'
                }
            }
        }
#ifndef INCLUDE_FILENAMES
    else
#endif///ndef INCLUDE_FILENAMES
        { // fileName
        printf("<TD nowrap>%s",oneFile->fileName);
        //// FIXME: " The "..." encapsulation could be rebuilt so it could be called here
        //printf("&nbsp;<A HREF='#a_meta_%s' onclick='return metadataShowHide(\"%s\",true,true);' title='Show metadata details...'>...</A>",
        //    oneFile->mdb->obj,oneFile->mdb->obj);
        //printf("<DIV id='div_%s_meta' style='display:none;'></div></td>",oneFile->mdb->obj);
        }

    // Extras  grant=Bernstein; lab=Broad; dataType=ChipSeq; setType=exp; control=std;
    mdbObjRemoveVars(oneFile->mdb,"fileName fileIndex composite project dccInternalNotes"); // Remove this from mdb now so that it isn't displayed in "extras'
    mdbObjReorderVars(oneFile->mdb,"grant lab dataType cell treatment antibody protocol replicate view",FALSE); // Bring to front
    mdbObjReorderVars(oneFile->mdb,"subId submittedDataVersion dateResubmitted dataVersion setType inputType controlId tableName",TRUE); // Send to back
    field = mdbObjVarValPairsAsLine(oneFile->mdb,TRUE);
    printf("<TD nowrap>%s</td>",field?field:" &nbsp;");

    printf("</TR>\n");
    }

printf("</TBODY><TFOOT class='bgLevel1'>\n");
printf("<TR valign='top'>");

// Restriction policy link in first column?
if (restrictedColumn == 1)
    printf("<TH colspan=%d><A HREF='%s' TARGET=BLANK style='font-size:.9em;'>Restriction Policy</A></TH>", (columnCount - restrictedColumn),ENCODE_DATA_RELEASE_POLICY);

printf("<TD colspan=%d>&nbsp;&nbsp;&nbsp;&nbsp;",(restrictedColumn > 1 ? (restrictedColumn - 1) : columnCount));

// Total
if (filesCount > 5)
    printf("<i>%d files</i>\n",filesCount);

// Restriction policy link in later column?
if (restrictedColumn > 1)
    printf("</TD><TH colspan=%d align='left'><A HREF='%s' TARGET=BLANK style='font-size:.9em;'>Restriction Policy</A>", columnCount,ENCODE_DATA_RELEASE_POLICY);

printf("</TD></TR>\n");
printf("</TFOOT></TABLE><BR>\n");

if (parentTdb == NULL)
    printf("<script type='text/javascript'>{$(document).ready(function() {sortTableInitialize($('table.sortable')[0],true,true);});}</script>\n");

return filesCount;
}


void filesDownloadUi(char *db, struct cart *cart, struct trackDb *tdb)
// UI for a "composite like" track: This will list downloadable files associated with
// a single trackDb entry (composite or of type "downloadsOnly". The list of files
// will have links to their download and have metadata information associated.
// The list will be a sortable table and there may be filtering controls.
{
    // The basic idea:
    // 1) tdb of composite or type=downloadsOnly tableless track
    // 2) All mdb Objs associated with "composite=tdb->track" and having fileName
    // 3) Verification of each file in its discovered location
    // 4) Lookup of 'fileSortOrder'
    // 5) TODO: present filter controls
    // 6) Presort of files list
    // 7) make table class=sortable
    // 8) Final file count
    // 9) Use trackDb settings to get at html description
    // Nice to have: Make filtering and sorting persistent (saved to cart)

// FIXME: Trick while developing:
if (tdb->table != NULL)
    tdb->track = tdb->table;

boolean debug = cartUsualBoolean(cart,"debug",FALSE);
int ix;

struct sqlConnection *conn = sqlConnect(db);
char *mdbTable = mdbTableName(conn,TRUE); // Look for sandBox name first
if(mdbTable == NULL)
    errAbort("TABLE NOT FOUND: '%s.%s'.\n",db,MDB_DEFAULT_NAME);

// Get an mdbObj list of all that belong to this track and have a fileName
char buf[256];
safef(buf,sizeof(buf),"composite=%s fileName=?",tdb->track);
struct mdbByVar *mdbVars = mdbByVarsLineParse(buf);
struct mdbObj *mdbList = mdbObjsQueryByVars(conn,mdbTable,mdbVars);

// Now get Indexes  But be sure not to duplicate entries in the list!!!
safef(buf,sizeof(buf),"composite=%s fileIndex= fileName!=",tdb->track);
mdbVars = mdbByVarsLineParse(buf);
mdbList = slCat(mdbList, mdbObjsQueryByVars(conn,mdbTable,mdbVars));
sqlDisconnect(&conn);

if (slCount(mdbList) == 0)
    {
    warn("No files specified in metadata for: %s\n%s",tdb->track,tdb->longLabel);
    return;
    }

// Remove common vars from mdbs grant=Bernstein; lab=Broad; dataType=ChipSeq; setType=exp; control=std;
char *commonTerms[] = { "grant", "lab", "dataType", "control", "setType" };
struct dyString *dyCommon = dyStringNew(256);
for(ix=0;ix<ArraySize(commonTerms);ix++)
    {
    char *val = mdbRemoveCommonVar(mdbList, commonTerms[ix]);// All mdbs have it and have the same val for it.
    if (val)
        dyStringPrintf(dyCommon,"%s=%s ",commonTerms[ix],val);
    }
if (debug && dyStringLen(dyCommon))
    warn("These terms are common:%s",dyStringContents(dyCommon));
dyStringFree(&dyCommon);

// Verify file existance and make fileList of those found
struct fileDb *fileList = NULL, *oneFile = NULL; // Will contain found files
struct mdbObj *mdbFiles = NULL; // Will caontain a list of mdbs for the found files
while(mdbList)
    {
    char buf[512];
    boolean found = FALSE;
    struct mdbObj *mdbFile = slPopHead(&mdbList);
    // First for FileName
    char *fileName = mdbObjFindValue(mdbFile,"fileName");
    if (fileName != NULL)
        {
        oneFile = fileDbGet(db, ENCODE_DCC_DOWNLOADS, tdb->track, fileName);
        if (oneFile)
            {
            slAddHead(&fileList,oneFile);
            oneFile->mdb = mdbFile;
            slAddHead(&mdbFiles,mdbFile);
            found = TRUE;
            }
        else if (debug)
            warn("goldenPath/%s/%s/%s/%s    in mdb but not found in directory",db,ENCODE_DCC_DOWNLOADS, tdb->track,fileName);
        }
    // Now for FileIndexes
    if (fileName && endsWith(fileName,".bam")) // Special to fill in missing .bam.bai's
        {
        safef(buf,sizeof(buf),"%s.bai",fileName);
        fileName = buf;
        }
    else
        fileName = mdbObjFindValue(mdbFile,"fileIndex");
    if (fileName != NULL)
        {
        // Verify existance first
        oneFile = fileDbGet(db, ENCODE_DCC_DOWNLOADS, tdb->track, fileName);
        if (oneFile)
            {
            slAddHead(&fileList,oneFile);
            if (found) // if already found then need two mdbObjs (assertable but then this is metadata)
                oneFile->mdb = mdbObjClone(mdbFile);  // Do we really need to clone this?
            else
                {
                oneFile->mdb = mdbFile;
                slAddHead(&mdbFiles,mdbFile);
                }
            found = TRUE;
            }
        else if (debug)
            warn("goldenPath/%s/%s/%s/%s    in mdb but not found in directory",db,ENCODE_DCC_DOWNLOADS, tdb->track,fileName);
        }
    if (!found)
        mdbObjsFree(&mdbFile);
    }

if (slCount(fileList) == 0)
    {
    warn("No downloadable files currently available for: %s\n%s",tdb->track,tdb->longLabel);
    return;  // No files so nothing to do.
    }
if (debug)
    {
    warn("The following files are in goldenPath/%s/%s/%s/ but NOT in the mdb:",db,ENCODE_DCC_DOWNLOADS, tdb->track);
    fileDbGet(db, ENCODE_DCC_DOWNLOADS, tdb->track, "listAll");
    }

// FilterBoxes ?
    // Dimensions
    // cart contents
    //boolean filterAble = dimensionsExist(tdb);
    //membersForAll_t* membersForAll = membersForAllSubGroupsGet(tdb,cart);

// Now update all files with their sortable fields and sort the list
sortOrder_t *sortOrder = fileSortOrderGet(cart,tdb,mdbFiles);
if (sortOrder != NULL)
    {
    // Fill in and sort fileList
    fileDbSortList(&fileList,sortOrder);
    }

jsIncludeFile("hui.js",NULL);
jsIncludeFile("ajax.js",NULL);

// standard preamble
filesDownloadsPreamble(db,tdb);

// Print table
filesPrintTable(db,tdb,fileList,sortOrder);

//fileDbFree(&fileList); // Why bother on this very long running cgi?
}

int fileSearchResults(char *db, struct sqlConnection *conn, struct slPair *varValPairs, char *fileType)
// Prints list of files in downloads directories matching mdb search terms. Returns count
{
struct sqlConnection *connLocal = conn;
if (conn == NULL)
    connLocal = hAllocConn(db);
struct mdbObj *mdbList = mdbObjRepeatedSearch(connLocal,varValPairs,FALSE,TRUE);
if (conn == NULL)
    hFreeConn(&connLocal);
if (slCount(mdbList) == 0)
    {
    printf("<DIV id='filesFound'><BR>No files found.<BR></DIV><BR>\n");
    return 0;
    }

// Now sort mdbObjs so that composites will stay together and lookup of files will be most efficient
mdbObjsSortOnVars(&mdbList, "composite");

// Verify file existance and make fileList of those found
struct fileDb *fileList = NULL, *oneFile = NULL; // Will contain found files
struct mdbObj *mdbFiles = NULL; // Will caontain a list of mdbs for the found files
while(mdbList)
    {
    boolean found = FALSE;
    struct mdbObj *mdbFile = slPopHead(&mdbList);
    char *composite = mdbObjFindValue(mdbFile,"composite");
    if (composite != NULL)
        {
        // First for FileName
        char *fileName = mdbObjFindValue(mdbFile,"fileName");
        if (fileName != NULL)
            {
            oneFile = fileDbGet(db, ENCODE_DCC_DOWNLOADS, composite, fileName);
            if (oneFile)
                {
                //warn("%s == %s",fileType,oneFile->fileType);
                if (isEmpty(fileType) || sameWord(fileType,"Any")
                || (oneFile->fileType && sameWord(fileType,oneFile->fileType)))
                    {
                    slAddHead(&fileList,oneFile);
                    oneFile->mdb = mdbFile;
                    slAddHead(&mdbFiles,mdbFile);
                    found = TRUE;
                    continue;
                    }
                }
                else
                    fileDbFree(&oneFile);
            }
        // Now for FileIndexes
        fileName = mdbObjFindValue(mdbFile,"fileIndex");
        if (fileName != NULL)
            {
           // Verify existance first
            oneFile = fileDbGet(db, ENCODE_DCC_DOWNLOADS, composite, fileName);
            if (oneFile)
                {
                //warn("%s == %s",fileType,oneFile->fileType);
                if (isEmpty(fileType) || sameWord(fileType,"Any")
                || (oneFile->fileType && sameWord(fileType,oneFile->fileType)))
                   {
                    slAddHead(&fileList,oneFile);
                    if (found) // if already found then need two mdbObjs (assertable but then this is metadata)
                        oneFile->mdb = mdbObjClone(mdbFile);  // Do we really need to clone this?
                    else
                        {
                        oneFile->mdb = mdbFile;
                        slAddHead(&mdbFiles,mdbFile);
                        }
                    found = TRUE;
                    continue;
                    }
                else
                    fileDbFree(&oneFile);
                }
            }
        }
    if (!found)
        mdbObjsFree(&mdbFile);
    }
if (slCount(fileList) == 0)
    {
    printf("<DIV id='filesFound'><BR>No files found.<BR></DIV><BR>\n");
    return 0;  // No files so nothing to do.
    }

// TODO Could sort on varValPairs by creating a sortOrder struct of them
//// Now update all files with their sortable fields and sort the list
sortOrder_t *sortOrder = fileSortOrderGet(NULL,NULL,mdbFiles); // No cart, no tdb
if (sortOrder != NULL)
    {
    // Fill in and sort fileList
    fileDbSortList(&fileList,sortOrder);
    }

mdbObjRemoveVars(mdbFiles,"tableName"); // Remove this from mdb now so that it isn't displayed in "extras'

//jsIncludeFile("hui.js",NULL);
//jsIncludeFile("ajax.js",NULL);

// Print table
int filesCount = filesPrintTable(db,NULL,fileList,sortOrder);

//fileDbFree(&fileList); // Why bother on this very long running cgi?
return filesCount;
}


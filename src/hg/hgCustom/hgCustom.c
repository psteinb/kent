/* hgCustom - Custom track management CGI. */
#include "common.h"
#include "obscure.h"
#include "linefile.h"
#include "hash.h"
#include "cart.h"
#include "cheapcgi.h"
#include "web.h"
#include "htmshell.h"
#include "hdb.h"
#include "hui.h"
#include "hCommon.h"
#include "customTrack.h"
#include "customFactory.h"
#include "portable.h"
#include "errCatch.h"
#if ((defined USE_BAM || defined USE_TABIX) && defined KNETFILE_HOOKS)
#include "knetUdc.h"
#include "udc.h"
#endif//def (USE_BAM || USE_TABIX) && KNETFILE_HOOKS
#include "net.h"
#include "jsHelper.h"
#include <signal.h>

static long loadTime = 0;


void usage()
/* Explain usage and exit. */
{
errAbort(
  "hgCustom - Custom track management CGI\n"
  "usage:\n"
  "   hgCustom <CGI settings>\n"
  );
}

/* DON'T EDIT THIS -- use CGI param "&measureTiming=." */
static boolean measureTiming = FALSE;

#define TEXT_ENTRY_ROWS 7
#define TEXT_ENTRY_COLS 73
#define CONFIG_ENTRY_ROWS 3
#define SAVED_LINE_COUNT  50

/* CGI variables */
#define hgCt		 "hgct_"  /* prefix for all control variables */

/* these are shared with other modules */
#define hgCtDataText      CT_CUSTOM_TEXT_ALT_VAR
#define hgCtDataFile      CT_CUSTOM_FILE_VAR
#define hgCtDataFileName  CT_CUSTOM_FILE_NAME_VAR
#define hgCtDocText       CT_CUSTOM_DOC_TEXT_VAR
#define hgCtDocFile       CT_CUSTOM_DOC_FILE_VAR
#define hgCtTable         CT_SELECTED_TABLE_VAR
#define hgCtUpdatedTable  CT_UPDATED_TABLE_VAR

/* misc */
#define hgCtUpdatedTrack "hgct_updatedTrack"
#define hgCtDeletePrefix "hgct_del"
#define hgCtRefreshPrefix "hgct_refresh"
#define hgCtConfigLines   "hgct_configLines"

/* commands */
#define hgCtDo		  hgCt   "do_"	  /* prefix for all commands */
#define hgCtDoAdd	  hgCtDo "add"
#define hgCtDoDelete	  hgCtDo "delete"
#define hgCtDoDeleteSet	  hgCtDo "delete_set"
#define hgCtDoDeleteClr	  hgCtDo "delete_clr"
#define hgCtDoRefresh     hgCtDo "refresh"
#define hgCtDoRefreshSet  hgCtDo "refresh_set"
#define hgCtDoRefreshClr  hgCtDo "refresh_clr"
#define hgCtDoGenomeBrowser	  hgCtDo "gb"
#define hgCtDoTableBrowser	  hgCtDo "tb"
#ifdef PROGRESS_METER
#define hgCtDoProgress	  hgCtDo "progress"
#endif

/* Global variables */
struct cart *cart;
struct hash *oldVars = NULL;
char *excludeVars[] = {"Submit", "submit", "SubmitFile", NULL};
char *database = NULL;
char *organism = NULL;
struct customTrack *ctList = NULL;

void makeClearButton(char *field)
/* UI button that clears a text field */
{
char javascript[1024];
safef(javascript, sizeof javascript,
        "document.mainForm.%s.value = '';", field);
cgiMakeOnClickButton(javascript, "&nbsp;Clear&nbsp;");
}

void addIntro()
/* display overview and help message for "add" screen */
{
if (hIsGsidServer())
  {
  puts(" Data must be formatted in\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format1'>BED</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bigBed.html'>bigBed</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bedgraph.html'>bedGraph</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format3'>GFF</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format4'>GTF</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/wiggle.html'>WIG</A>\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bigWig.html'>bigWig</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format1.7'>BED detail</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format10'>Personal Genome SNP</A>,\n"
  " or <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format2'>PSL</A>\n"
  " formats. To configure the display, set\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html#TRACK'>track</A>\n"
  " and"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html#BROWSER'>browser</A>\n"
  " line attributes as described in the \n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html'>User's Guide</A>.\n"
  " URLs for data in the bigBed, bigWig, BAM and VCF formats must be embedded in a track\n"
  " line in the box below.\n"
  );
  }
else
  {
  puts(" Data must be formatted in\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format1'>BED</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bigBed.html'>bigBed</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bedgraph.html'>bedGraph</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format3'>GFF</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format4'>GTF</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/wiggle.html'>WIG</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bigWig.html'>bigWig</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format5'>MAF</A>,\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/bam.html'>BAM</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format1.7'>BED detail</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format10'>Personal Genome SNP,</A>\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/vcf.html'>VCF</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format13'>broadPeak</A>,\n"
  " <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format12'>narrowPeak</A>,\n"
  " or <A TARGET=_BLANK HREF='../FAQ/FAQformat.html#format2'>PSL</A>\n"
  " formats. To configure the display, set\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html#TRACK'>track</A>\n"
  " and"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html#BROWSER'>browser</A>\n"
  " line attributes as described in the \n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html'>User's Guide</A>.\n"
  " URLs for data in the bigBed, bigWig, BAM and VCF formats must be embedded in a track\n"
  " line in the box below.\n"
  " Publicly available custom tracks are listed\n"
  " <A HREF='../goldenPath/customTracks/custTracks.html'>here</A>.\n"
  " Examples are\n"
  " <A TARGET=_BLANK HREF='../goldenPath/help/customTrack.html#EXAMPLE1'>here</A>.\n"
  );
  }
}

void addCustomForm(struct customTrack *ct, char *err)
/* display UI for adding custom tracks by URL or pasting data */
{
char *dataUrl = NULL;
char buf[1024];

boolean gotClade = FALSE;
boolean isUpdateForm = FALSE;
if (ct)
    {
    isUpdateForm = TRUE;
    dataUrl = ctDataUrl(ct);
    }
else
    /* add form needs clade for assembly menu */
    gotClade = hGotClade();

jsIncludeFile("jquery.js", NULL);
jsIncludeFile("hgCustom.js", NULL);
jsIncludeFile("utils.js", NULL);
jsIncludeFile("ajax.js", NULL);

/* main form */
printf("<FORM ACTION=\"%s\" METHOD=\"%s\" "
    " ENCTYPE=\"multipart/form-data\" NAME=\"mainForm\" onsubmit=\"$('input[name=Submit]').attr('disabled', 'disabled');\" >\n",
    hgCustomName(), cartUsualString(cart, "formMethod", "POST"));
cartSaveSession(cart);

if (!isUpdateForm)
    {
    /* Print clade, genome and assembly  */
    /* NOTE: this uses an additional, hidden form (orgForm), below */
    char *onChangeDb = "onchange=\"document.orgForm.db.value = document.mainForm.db.options[document.mainForm.db.selectedIndex].value; document.orgForm.submit();\"";
    char *onChangeOrg = "onchange=\"document.orgForm.org.value = document.mainForm.org.options[document.mainForm.org.selectedIndex].value; document.orgForm.db.value = 0; document.orgForm.submit();\"";
    char *onChangeClade = "onchange=\"document.orgForm.clade.value = document.mainForm.clade.options[document.mainForm.clade.selectedIndex].value; document.orgForm.org.value = 0; document.orgForm.db.value = 0; document.orgForm.submit();\"";

    if (hIsGsidServer())
        {
        printf("<span style='color:red;'>The Custom Track function and its documentation is "
                "currently under development ...</span><BR><BR>\n");
        }

    puts("<TABLE BORDER=0>\n");
    if (gotClade)
        {
        puts("<TR><TD>clade\n");
        printCladeListHtml(hOrganism(database), onChangeClade);
        puts("&nbsp;&nbsp;&nbsp;");
        puts("genome\n");
        printGenomeListForCladeHtml(database, onChangeOrg);
        }
    else
        {
        puts("<TR><TD>genome\n");
        printGenomeListHtml(database, onChangeOrg);
        }
    puts("&nbsp;&nbsp;&nbsp;");
    puts("assembly\n");
    printAssemblyListHtml(database, onChangeDb);
    if (! stringIn(database, hFreezeFromDb(database)))
	{
	puts("&nbsp;&nbsp;&nbsp;");
	printf("[%s]", database);
	}
    puts("</TD></TR></TABLE>\n");
    }

/* intro text */
puts("<P>");
if (isUpdateForm)
    puts("Update your custom track configuration, data, and/or documentation.");
else
    puts("Display your own data as custom annotation tracks in the browser.");
addIntro();
puts("<P>");

/* row for error message */
if (err)
    printf("<P><B>&nbsp;&nbsp;&nbsp;&nbsp;<span style='color:RED; font-style:italic;'>"
           "Error</span>&nbsp;%s</B><P>", err);

cgiSimpleTableStart();

/* first rows for update form are for track and browser line entry */
if (isUpdateForm)
    {
    /* row for instructions */
    cgiSimpleTableRowStart();
    cgiSimpleTableFieldStart();
    if (dataUrl)
        puts("Configuration:");
    else
        {
        puts("Edit configuration:");
        }
    cgiTableFieldEnd();
    cgiTableField("&nbsp;");
    puts("<TD ALIGN='RIGHT'>");
    cgiMakeButtonWithOnClick("Submit", "Submit", NULL, "return submitClick(this);");
    printf("<img id='loadingImg' src='../images/loading.gif' />\n");
    cgiTableFieldEnd();
    cgiTableField("&nbsp;");
    cgiTableRowEnd();

    /* row for text entry box */
    cgiSimpleTableRowStart();
    puts("<TD COLSPAN=2>");
    if (dataUrl)
        {
        /* can't update via pasting if loaded from URL */
        cgiMakeTextAreaDisableable(hgCtConfigLines,
            cartUsualString(cart, hgCtConfigLines, customTrackUserConfig(ct)),
                            CONFIG_ENTRY_ROWS, TEXT_ENTRY_COLS, TRUE);
        }
    else
        {
        cgiMakeTextArea(hgCtConfigLines,
                cartUsualString(cart, hgCtConfigLines, customTrackUserConfig(ct)),
                            CONFIG_ENTRY_ROWS, TEXT_ENTRY_COLS);
        }
    cgiTableFieldEnd();

    cgiSimpleTableFieldStart();
    cgiSimpleTableStart();

    cgiSimpleTableRowStart();
    cgiSimpleTableFieldStart();
    cgiTableFieldEnd();
    cgiTableRowEnd();

    cgiTableEnd();
    cgiTableFieldEnd();
    cgiTableRowEnd();
    }

/* next row - label entry for file upload */
cgiSimpleTableRowStart();
if (isUpdateForm)
    {
    /* update existing */
    /* extra space */
    cgiSimpleTableRowStart();
    puts("<TD STYLE='padding-top:9';\"></TD>");
    cgiTableRowEnd();
    if (dataUrl)
        cgiTableField("Data:");
    else
        cgiTableField("Paste in replacement data:");
    }
else
    cgiTableField("Paste URLs or data:");

if (isUpdateForm && dataUrl)
    cgiTableField("&nbsp");
else
    {
    puts("<TD ALIGN='RIGHT'>");
    puts("Or upload: ");
    cgiMakeFileEntry(hgCtDataFile);
    cgiTableFieldEnd();
    }
if (!isUpdateForm)
    {
    cgiSimpleTableFieldStart();
    cgiMakeButtonWithOnClick("Submit", "Submit", NULL, "return submitClick(this);");
    printf("<img id='loadingImg' src='../images/loading.gif' />\n");
    cgiTableFieldEnd();
    }
cgiTableRowEnd();

/* next row - text entry box for  data, and clear button */
cgiSimpleTableRowStart();
puts("<TD COLSPAN=2>");
if (dataUrl)
    {
    /* can't update via pasting if loaded from URL */
    safef(buf, sizeof buf, "Replace data at URL: %s", ctDataUrl(ct));
    cgiMakeTextAreaDisableable(hgCtDataText, buf,
                                TEXT_ENTRY_ROWS, TEXT_ENTRY_COLS, TRUE);
    }
else
    {
    int rows = (isUpdateForm ? TEXT_ENTRY_ROWS - CONFIG_ENTRY_ROWS :
                                TEXT_ENTRY_ROWS);
    cgiMakeTextArea(hgCtDataText, cartUsualString(cart, hgCtDataText, ""),
                                    rows, TEXT_ENTRY_COLS);
    }

cgiTableFieldEnd();

cgiSimpleTableFieldStart();
cgiSimpleTableStart();

cgiSimpleTableRowStart();
cgiSimpleTableFieldStart();
if (!(isUpdateForm && dataUrl))
    {
    printf("<span id='loadingMsg'></span>\n");
    makeClearButton(hgCtDataText);
    }
cgiTableFieldEnd();
cgiTableRowEnd();

cgiTableEnd();
cgiTableFieldEnd();
cgiTableRowEnd();

/* extra space */
cgiSimpleTableRowStart();
cgiSimpleTableFieldStart();
cgiDown(0.7);
cgiTableFieldEnd();
cgiTableRowEnd();

/* next row - label for description text entry */
cgiSimpleTableRowStart();
cgiTableField("Optional track documentation: ");
if (isUpdateForm && ctHtmlUrl(ct))
    cgiTableField("&nbsp;");
else
    {
    puts("<TD ALIGN='RIGHT'>");
    puts("Or upload: ");
    cgiMakeFileEntry(hgCtDocFile);
    cgiTableFieldEnd();
    }
cgiTableRowEnd();

/* next row - text entry for description, and clear button(s) */
cgiSimpleTableRowStart();
puts("<TD COLSPAN=2>");

if (ct && ctHtmlUrl(ct))
    {
    safef(buf, sizeof buf, "Replace doc at URL: %s", dataUrl);
    cgiMakeTextAreaDisableable(hgCtDocText, buf,
                                    TEXT_ENTRY_ROWS, TEXT_ENTRY_COLS, TRUE);
    }
else
    {
    cgiMakeTextArea(hgCtDocText, cartUsualString(cart, hgCtDocText, ""),
                                    TEXT_ENTRY_ROWS, TEXT_ENTRY_COLS);
    cgiSimpleTableFieldStart();
    cgiSimpleTableStart();
    cgiSimpleTableRowStart();
    cgiSimpleTableFieldStart();
    makeClearButton(hgCtDocText);
    cgiTableFieldEnd();
    cgiTableRowEnd();
    cgiTableEnd();
    }
cgiTableFieldEnd();

cgiTableRowEnd();
cgiTableEnd();

/* help text at bottom of screen - link for HTML description template */
puts("Click <A HREF=\"../goldenPath/help/ct_description.txt\" TARGET=_blank>here</A> for an HTML document template that may be used for Genome Browser track descriptions.");

if (isUpdateForm)
    {
    /* hidden variables to identify track */
    cgiMakeHiddenVar(hgCtUpdatedTable, ct->tdb->track);
    char buf[512];
    char *shortLabel = htmlEncode(ct->tdb->shortLabel);
    char *longLabel = htmlEncode(ct->tdb->longLabel);
    safef(buf, sizeof buf, "track name='%s' description='%s'",
				shortLabel, longLabel);
    char *trackLine = htmlEncode(ctOrigTrackLine(ct));
    cgiMakeHiddenVar(hgCtUpdatedTrack, trackLine ? trackLine : buf);
    freeMem(trackLine);
    freeMem(shortLabel);
    freeMem(longLabel);
    }
else
    {
    /* hidden form to handle clade/genome/assembly dropdown.
     * This is at end of page for layout reasons (preserve vertical space) */
    puts("</FORM>");
    printf("<FORM STYLE=\"margin-bottom:0;\" ACTION=\"%s\" METHOD=\"GET\" NAME=\"orgForm\">", hgCustomName());
    cartSaveSession(cart);
    if (gotClade)
        printf("<INPUT TYPE=\"HIDDEN\" NAME=\"clade\" VALUE=\"\">\n");
    printf("<INPUT TYPE=\"HIDDEN\" NAME=\"org\" VALUE=\"%s\">\n", organism);
    printf("<INPUT TYPE=\"HIDDEN\" NAME=\"db\" VALUE=\"%s\">\n", database);
    printf("<INPUT TYPE=\"HIDDEN\" NAME=\"hgct_do_add\" VALUE=\"1\">\n");
    }
puts("</FORM>");
cgiDown(0.9);
}

void tableHeaderFieldStart(int columns)
{
/* print table column header with white text on black background */
printf("<TD COLSPAN=%d ALIGN='CENTER' BGCOLOR='#536ED3'>", columns);
}

void tableHeaderField(char *label, char *description)
{
/* print table column header with white text on black background */
puts("<TD ALIGN='CENTER' BGCOLOR='#536ED3' ");
if (description)
    printf("TITLE='%s'", description);
printf("><B>%s</B></TD> ", wrapWhiteFont(label));
}

void showCustomTrackList(struct customTrack *ctList, int numCts)
/* print table of custom tracks with controls */
{
struct customTrack *ct;
char buf[256];
char *pos = NULL;
char *dataUrl;
int colSpan = 4;

/* handle 'set all' and 'clr all' (won't be used if user has javascript enabled). */
boolean setAllDelete = FALSE;
boolean setAllUpdate = FALSE;
if (cartVarExists(cart, hgCtDoDeleteSet))
    setAllDelete = TRUE;
if (cartVarExists(cart, hgCtDoRefreshSet))
    setAllUpdate = TRUE;

/* determine which columns to display (avoid empty columns) */
int updateCt = 0, itemCt = 0, posCt = 0, errCt = 0;
for (ct = ctList; ct != NULL; ct = ct->next)
    {
    if (ctDataUrl(ct))
        updateCt++;
    if (ctItemCount(ct) > 0)
        itemCt++;
    if (ctInitialPosition(ct) || ctFirstItemPos(ct))
        posCt++;
    if (ct->networkErrMsg)
	errCt++;
    }
hTableStart();
cgiSimpleTableRowStart();
tableHeaderField("Name", "Short track identifier");
tableHeaderField("Description", "Long track identifier");
tableHeaderField("Type", "Data format of track");
tableHeaderField("Doc", "HTML track description");
if (itemCt)
    {
    tableHeaderField("Items", "Count of discrete items in track");
    colSpan++;
    }
if (posCt)
    {
    tableHeaderField("Pos"," Go to genome browser at default track position or first item");
    colSpan++;
    }
if (errCt)
    {
    tableHeaderField("Error"," Error in custom track");
    colSpan++;
    }

boolean showAllButtons = FALSE;
if (numCts > 3)
    showAllButtons = TRUE;

tableHeaderFieldStart(showAllButtons ? 2 : 1);
cgiMakeButtonWithMsg(hgCtDoDelete, "delete", "Remove custom track");
cgiTableFieldEnd();

/* add column with Update button if any custom tracks are updateable */
if (updateCt)
    {
    tableHeaderFieldStart(showAllButtons ? 2 : 1);
    cgiMakeButtonWithMsg(hgCtDoRefresh, "update", "Refresh from data URL");
    cgiTableFieldEnd();
    }

cgiTableRowEnd();

for (ct = ctList; ct != NULL; ct = ct->next)
    {
    /* Name  field */
    char *shortLabel = htmlEncode(ct->tdb->shortLabel);
    if ((ctDataUrl(ct) && ctHtmlUrl(ct)) ||
            sameString(ct->tdb->type, "chromGraph"))
        printf("<TR><TD>%s</A></TD>", shortLabel);
    else
	{
	char *cgiName = cgiEncode(ct->tdb->track);
        printf("<TR><TD><A TITLE='Update custom track: %s' HREF='%s?%s&%s=%s'>%s</A></TD>",
            shortLabel, hgCustomName(),cartSidUrlString(cart),
	    hgCtTable, cgiName, shortLabel);
	freeMem(cgiName);
	}
    freeMem(shortLabel);
    /* Description field */
    char *longLabel = htmlEncode(ct->tdb->longLabel);
    printf("<TD>%s</TD>", longLabel);
    freeMem(longLabel);
    /* Type field */
    printf("<TD>%s</TD>", ctInputType(ct));
    /* Doc field */
    printf("<TD ALIGN='CENTER'>%s</TD>",
                isNotEmpty(ct->tdb->html) ? "Y" : "&nbsp;");
    /* Items field */
    if (itemCt)
        {
        int count = ctItemCount(ct);
        if (count > 0)
            printf("<TD ALIGN='CENTER'>%d</TD>", count);
        else
            puts("<TD>&nbsp;</TD>");
        }
    /* Pos field; indicates initial position for the track,
     * or first element */
    if (posCt)
        {
        pos = ctInitialPosition(ct);
        if (!pos)
            pos = ctFirstItemPos(ct);
        if (pos)
            {
            char *chrom = cloneString(pos);
            chopSuffixAt(chrom, ':');
            if (hgOfficialChromName(database, chrom))
                printf("<TD><A HREF='%s?%s&position=%s&hgTracksConfigPage=notSet' TITLE=%s>%s:</A></TD>",
                    hgTracksName(), cartSidUrlString(cart),pos, pos, chrom);
            else
                puts("<TD>&nbsp;</TD>");
            }
        else
            puts("<TD>&nbsp;</TD>");
        }
    if (errCt)
	{
	if (ct->networkErrMsg)
	    {
	    printf("\n<TD><A href=\"javascript:void(0)\" onClick=\"alert('%s')\">Show</A></TD>\n",
		javaScriptLiteralEncode(ct->networkErrMsg));
	    }
	else
	    puts("<TD>&nbsp;</TD>");
	}
    /* Delete checkboxes */
    printf("<TD COLSPAN=%d ALIGN=CENTER>", showAllButtons ? 2 : 1);
    safef(buf, sizeof(buf), "%s_%s", hgCtDeletePrefix,
            ct->tdb->track);
    cgiMakeCheckBoxJS(buf, setAllDelete, "class='deleteCheckbox'");
    puts("</TD>");

    /* Update checkboxes */
    if (updateCt)
        {
        printf("<TD COLSPAN=%d ALIGN=CENTER>", showAllButtons ? 2 : 1);
        safef(buf, sizeof(buf), "%s_%s", hgCtRefreshPrefix,
                ct->tdb->track);
        if ((dataUrl = ctDataUrl(ct)) != NULL)
            {
            char js[1024];
            safef(js, sizeof(js), "class='updateCheckbox' title='refresh data from: %s'", dataUrl);
            cgiMakeCheckBoxJS(buf, setAllUpdate, js);
            }
        else
            puts("&nbsp;");
	puts("</TD>");
        }
    puts("</TR>\n");
    }
if (showAllButtons)
    {
    cgiSimpleTableRowStart();
    printf("<TD COLSPAN=%d ALIGN='RIGHT'>check all / clear all&nbsp;</TD>", colSpan);
    cgiSimpleTableFieldStart();
    cgiMakeButtonWithOnClick(hgCtDoDeleteSet, "+", "Select all for deletion", "$('.deleteCheckbox').attr('checked', true); return false;");
    cgiTableFieldEnd();
    cgiSimpleTableFieldStart();
    cgiMakeButtonWithOnClick(hgCtDoDeleteClr, "-", "Clear all for deletion", "$('.deleteCheckbox').attr('checked', false); return false;");
    cgiTableFieldEnd();
    if (updateCt)
        {
        cgiSimpleTableFieldStart();
        cgiMakeButtonWithOnClick(hgCtDoRefreshSet, "+", "Select all for update", "$('.updateCheckbox').attr('checked', true); return false;");
        cgiTableFieldEnd();
        cgiSimpleTableFieldStart();
        cgiMakeButtonWithOnClick(hgCtDoRefreshClr, "-", "Clear all for update", "$('.updateCheckbox').attr('checked', false); return false;");
        cgiTableFieldEnd();
        }
    cgiTableRowEnd();
    }
hTableEnd();
}

struct dbDb *getCustomTrackDatabases()
/* Get list of databases having custom tracks for this user.
 * Dispose of this with dbDbFreeList. */
{
struct dbDb *dbList = NULL, *dbDb;
char *db;

/* Get list of assemblies with custom tracks */
struct hashEl *hels = cartFindPrefix(cart, CT_FILE_VAR_PREFIX);
struct hashEl *hel = NULL;
for (hel = hels; hel != NULL; hel = hel->next)
    {
    /* TODO: chop actual prefix */
    db = chopPrefixAt(cloneString(hel->name), '_');
        /* TODO: check if file exists, if not remove ctfile_ var */
    dbDb = hDbDb(db);
    if (dbDb)
        slAddTail(&dbList, dbDb);
    }
return dbList;
}

static void manageCustomForm(char *warn)
/* list custom tracks and display checkboxes so user can select for delete */
{

struct dbDb *dbList = getCustomTrackDatabases();
struct dbDb *dbDb = NULL;
/* add this database to the list, as it may have no custom
 * tracks, but we still want to see it in the menu */
slAddTail(&dbList, hDbDb(database));
slReverse(&dbList);
/* remove duplicate entry for this database, if any */
for (dbDb = dbList->next; dbDb != NULL; dbDb = dbDb->next)
    if (sameString(dbDb->name, database))
        slRemoveEl(&dbList, dbDb);

boolean assemblyMenu = FALSE;
if (slCount(dbList) > 1)
    assemblyMenu = TRUE;

if (assemblyMenu)
    {
    /* hidden form to handle genome/assembly dropdown */
    printf("<FORM STYLE=\"margin-bottom:0;\" ACTION=\"%s\" METHOD=\"GET\" NAME=\"orgForm\">", hgCustomName());
    cartSaveSession(cart);
    printf("<INPUT TYPE=\"HIDDEN\" NAME=\"org\" VALUE=\"%s\">\n", organism);
    printf("<INPUT TYPE=\"HIDDEN\" NAME=\"db\" VALUE=\"%s\">\n", database);
    puts("</FORM>");
    }

/* the main form contains a table of all tracks, with checkboxes to delete */
printf("<FORM ACTION=\"%s\" METHOD=\"%s\" NAME=\"mainForm\">\n",
           hgCustomName(), cartUsualString(cart, "formMethod", "POST"));
cartSaveSession(cart);

if (assemblyMenu)
    {
    /* Print clade, genome and assembly  */
    char *onChangeDb = "onchange=\"document.orgForm.db.value = document.mainForm.db.options[document.mainForm.db.selectedIndex].value; document.orgForm.submit();\"";
    char *onChangeOrg = "onchange=\"document.orgForm.org.value = document.mainForm.org.options[document.mainForm.org.selectedIndex].value; document.orgForm.db.value = 0; document.orgForm.submit();\"";

    puts("<TABLE BORDER=0>\n");
    puts("<TR><TD>genome\n");
    printSomeGenomeListHtml(database, dbList, onChangeOrg);
    puts("&nbsp;&nbsp;&nbsp;");
    puts("assembly\n");
    printSomeAssemblyListHtml(database, dbList, onChangeDb);
    puts("&nbsp;&nbsp;&nbsp;");
    printf("[%s]", database);
    puts("</TD></TR></TABLE><P>\n");
    }
else
    printf("<B>genome:</B> %s &nbsp;&nbsp;&nbsp;<B>assembly:</B> %s &nbsp;&nbsp;&nbsp;[%s]\n",
            organism, hFreezeDate(database), database);

if (measureTiming && (loadTime > 0))
    printf("\n<BR>load time: %ld ms<BR>\n", loadTime);
/* place for warning messages to appear */
if (isNotEmpty(warn))
    {
    char *encoded = htmlEncode(warn);
    printf("<P><B>&nbsp;&nbsp;&nbsp;&nbsp;%s", encoded);
    freeMem(encoded);
    }

/* count up number of custom tracks for this genome */
int numCts = slCount(ctList);

cgiSimpleTableStart();
cgiSimpleTableRowStart();
if (numCts)
    {
    puts("<TD VALIGN=\"TOP\">");
    showCustomTrackList(ctList, numCts);
    }
else
    puts("<TD VALIGN=\"TOP\"><B><EM>No custom tracks for this genome:<B></EM>&nbsp;&nbsp;");
puts("</TD>");

/* navigation  buttons */
puts("<TD VALIGN=\"TOP\">");
puts("<TABLE BORDER=0>");

/* button to add custom tracks */
int buttonWidth = 13;
puts("<TR><TD>");
printf("<INPUT TYPE=SUBMIT NAME=\"%s\" VALUE=\"%s\" STYLE=\"width:%dem\">",
                hgCtDoAdd, "add custom tracks", buttonWidth);
puts("</TD></TR>");
puts("</FORM>");

/* determine if there's a navigation position for this screen */
char *pos = NULL;
if (ctList)
    {
    pos = ctInitialPosition(ctList);
    if (!pos)
        pos = ctFirstItemPos(ctList);
    }

/* button for GB navigation */
puts("<TR><TD>");
printf("<FORM STYLE=\"margin-bottom:0;\" ACTION=\"%s\" METHOD=\"GET\" NAME=\"tracksForm\">\n",
           hgTracksName());
cartSaveSession(cart);
printf("<INPUT TYPE=SUBMIT NAME=\"Submit\" VALUE=\"%s\" STYLE=\"width:%dem\">",
        "go to genome browser", buttonWidth);
if (pos)
    cgiMakeHiddenVar("position", pos);
puts("</FORM>");
puts("</TD></TR>");

/* button for TB navigation */
puts("<TR><TD>");
printf("<FORM STYLE=\"margin-bottom:0;\" ACTION=\"%s\" METHOD=\"GET\" NAME=\"tablesForm\">\n",
           hgTablesName());
cartSaveSession(cart);
printf("<INPUT TYPE=SUBMIT NAME=\"Submit\" VALUE=\"%s\" STYLE=\"width:%dem\">",
        "go to table browser", buttonWidth);
puts("</FORM>");
puts("</TD></TR>");

puts("</TABLE>");
puts("</TD>");

cgiTableRowEnd();
cgiTableEnd();
cartSetString(cart, "hgta_group", "user");
}

void helpCustom()
/* display documentation */
{
webNewSection("Loading Custom Tracks");
char *browserVersion;
if (btIE == cgiClientBrowser(&browserVersion, NULL, NULL) && *browserVersion < '8')
    puts("<span>");
else
    puts("<span style='position:relative; top:-1em;'>");
webIncludeHelpFile("customTrackLoad", FALSE);

puts("</span>");
}

void doBrowserLines(struct slName *browserLines, char **retErr)
/*  parse variables from browser lines into the cart */
{
char *err = NULL;
struct slName *bl;
for (bl = browserLines; bl != NULL; bl = bl->next)
    {
    char *words[96];
    int wordCount;

    wordCount = chopLine(bl->name, words);
    if (wordCount > 1)
        {
	char *command = words[1];
	if (sameString(command, "hide")
            || sameString(command, "dense")
            || sameString(command, "pack")
            || sameString(command, "squish")
            || sameString(command, "full"))
	    {
	    if (wordCount > 2)
	        {
		int i;
		for (i=2; i<wordCount; ++i)
		    {
		    char *s = words[i];
		    if (sameWord(s, "all"))
                        {
                        cartSetString(cart, "hgt.visAllFromCt", command);
                        }
                    else
                        {
                        char buf[256];
                        safef(buf, sizeof buf, "hgtct.%s", s);
                        cartSetString(cart, buf, command);
                        }
		    }
		}
	    }
	else if (sameString(command, "position"))
	    {
            char *chrom = NULL;
            int start = 0, end = 0;
	    if (wordCount < 3)
                {
	        err = "Expecting 3 words in browser position line";
                break;
                }
	    if (!hgParseChromRange(database, words[2], &chrom, &start, &end) ||
                start < 0 || end > hChromSize(database, chrom))
                {
	        err ="Invalid browser position (use chrN:123-456 format)";
                break;
                }
            cartSetString(cart, "position", words[2]);
	    }
	}
    }
if (retErr)
    *retErr = err;
}

#ifdef PROGRESS_METER
static void progressMeter()
{
printf("<FORM STYLE=\"margin-bottom:0;\" ACTION=\"%s\" METHOD=\"GET\" NAME=\"orgForm\">", hgCustomName());
cartSaveSession(cart);
printf("<INPUT TYPE=\"HIDDEN\" NAME=\"org\" VALUE=\"%s\">\n", organism);
printf("<INPUT TYPE=\"HIDDEN\" NAME=\"db\" VALUE=\"%s\">\n", database);
printf("<INPUT TYPE=\"HIDDEN\" NAME=\"hgct_do_add\" VALUE=\"1\">\n");
puts("</FORM>");
}
static void doProgress(char *err)
/* display progress meter to show loading process */
{
cartWebStart(cart, database, "Custom Track loading progress meter");
progressMeter();
// addCustomForm(NULL, err);
helpCustom();
cartWebEnd(cart);
}
#endif

void doAddCustom(char *err)
/* display form for adding custom tracks.
 * Include error message, if any */
{
cartWebStart(cart, database, "Add Custom Tracks");
addCustomForm(NULL, err);
helpCustom();
cartWebEnd(cart);
}

void doUpdateCustom(struct customTrack *ct, char *err)
/* display form for adding custom tracks.
 * Include error message, if any */
{
char *longLabel = htmlEncode(ct->tdb->longLabel);
cartWebStart(cart, database, "Update Custom Track: %s [%s]",
        longLabel, database);
freeMem(longLabel);
cartSetString(cart, hgCtDocText, ct->tdb->html);
addCustomForm(ct, err);
helpCustom();
cartWebEnd(cart);
}

static void doManageCustom(char *warn)
/* display form for deleting & updating custom tracks.
 * Include warning message, if any */
{
cartWebStart(cart, database, "Manage Custom Tracks");
jsIncludeFile("jquery.js", NULL);
manageCustomForm(warn);
webNewSection("Managing Custom Tracks");
webIncludeHelpFile("customTrackManage", FALSE);
cartWebEnd(cart);
}

char *fixNewData(struct cart *cart)
/* append a newline to incoming data, to keep custom preprocessor happy */
{
char *customText = cartUsualString(cart, hgCtDataText, "");
if (isNotEmpty(customText))
    {
    struct dyString *ds = dyStringNew(0);
    dyStringPrintf(ds, "%s\n", customText);
    customText = dyStringCannibalize(&ds);
    cartSetString(cart, hgCtDataText, customText);
    }
return customText;
}

char *replacedTracksMsg(struct customTrack *replacedCts)
/* make warning message listing replaced tracks */
{
struct customTrack *ct;

if (!slCount(replacedCts))
    return NULL;
struct dyString *dsWarn = dyStringNew(0);
dyStringAppend(dsWarn, "Replaced: ");
for (ct = replacedCts; ct != NULL; ct = ct->next)
    {
    if (ct != replacedCts)
	/* not the first */
	dyStringAppend(dsWarn, ", ");
    dyStringAppend(dsWarn, ct->tdb->shortLabel);
    }
return dyStringCannibalize(&dsWarn);
}

void doDeleteCustom()
/* remove custom tracks from list based on cart variables */
{
struct customTrack *ct;
for (ct = ctList; ct != NULL; ct = ct->next)
    {
    char var[256];
    safef(var, sizeof var, "%s_%s", hgCtDeletePrefix, ct->tdb->track);
    if (cartBoolean(cart, var))
	slRemoveEl(&ctList, ct);
    }
}

void doRefreshCustom(char **warn)
/* reparse custom tracks from URLs based on cart variables */
{
struct customTrack *ct;
struct customTrack *replacedCts = NULL;
struct customTrack *refreshCts = NULL;

for (ct = ctList; ct != NULL; ct = ct->next)
    {
    char var[256];
    safef(var, sizeof var, "%s_%s", hgCtRefreshPrefix, ct->tdb->track);
    if (cartUsualBoolean(cart, var, FALSE))
	{
	struct customTrack *nextCt = NULL, *urlCt = NULL;
	struct customTrack *urlCts =
	    customFactoryParse(database, ctDataUrl(ct), FALSE, NULL);
	for (urlCt = urlCts; urlCt != NULL; urlCt = nextCt)
	    {
	    nextCt = urlCt->next;
	    if (sameString(ct->tdb->track, urlCt->tdb->track))
		slAddHead(&refreshCts, urlCt);
	    }
	}
    }
ctList = customTrackAddToList(ctList, refreshCts, &replacedCts, FALSE);
if (warn)
    *warn = replacedTracksMsg(replacedCts);
customTrackHandleLift(database, ctList);
}

void addWarning(struct dyString *ds, char *msg)
/* build up a warning message from parts */
{
if (!msg)
    return;
if (isNotEmpty(ds->string))
    dyStringAppend(ds, ". ");
dyStringAppend(ds, msg);
}

char *saveLines(char *text, int max)
/* save lines from input, up to 'max'.
 * Prepend with comment, if truncated */
{
if (!text)
    return NULL;

char buf[128];
int count = 0;
char *line;
boolean truncated = FALSE;
struct dyString *ds = dyStringNew(0);

safef(buf, sizeof buf, "# Displaying first %d lines of data", max);
struct lineFile *lf = lineFileOnString("saved custom text", TRUE, text);
while (lineFileNext(lf, &line, NULL))
    {
    if (startsWith(buf, line))
        continue;
    if (++count > max)
        {
        truncated = TRUE;
        break;
        }
    dyStringAppend(ds, line);
    dyStringAppend(ds, "\n");
    }
if (truncated)
    {
    struct dyString *dsNew = dyStringNew(0);
    dyStringPrintf(dsNew, "%s\n%s", buf, dyStringCannibalize(&ds));
    return dyStringCannibalize(&dsNew);
    }
return (dyStringCannibalize(&ds));
}

struct customTrack *ctFromList(struct customTrack *ctList, char *track)
/* return custom track from list */
{
struct customTrack *ct = NULL;
for (ct = ctList; ct != NULL; ct = ct->next)
    if (sameString(track, ct->tdb->track))
        return ct;
return NULL;
}

boolean customTrackHasConfig(char *text)
/* determine if there are track or browser lines in text */
{
text = skipLeadingSpaces(text);
return startsWith("track ", text) || startsWith("browser ", text);
}

int timerCounter;
#define TIMER_INTERVAL 10

static void timer(int sig)
{
// Per HTML 4.01 spec (http://www.w3.org/TR/html401/struct/global.html#h-7.1):
//
//      White space (spaces, newlines, tabs, and comments) may appear before or after each section [including the DOCTYPE].
//
// So we print out comments periodically to keep this process from being killed by apache or the user's web browser.

printf("<!-- processing (%d seconds) -->\n", timerCounter++ * TIMER_INTERVAL);
fflush(stdout);
alarm(TIMER_INTERVAL);
}

void doMiddle(struct cart *theCart)
/* create web page */
{
char *ctFileName = NULL;
struct slName *browserLines = NULL;
struct customTrack *replacedCts = NULL;
char *err = NULL, *warn = NULL;
char *selectedTable = NULL;
struct customTrack *ct = NULL;
boolean ctUpdated = FALSE;
char *initialDb = NULL;

long thisTime = clock1000();

cart = theCart;
measureTiming = isNotEmpty(cartOptionalString(cart, "measureTiming"));
initialDb = cloneString(cartString(cart, "db"));
getDbAndGenome(cart, &database, &organism, oldVars);

setUdcCacheDir();
customFactoryEnableExtraChecking(TRUE);

#if ((defined USE_BAM || defined USE_TABIX) && defined KNETFILE_HOOKS)
knetUdcInstall();
if (udcCacheTimeout() < 300)
    udcSetCacheTimeout(300);
#endif//def (USE_BAM || USE_TABIX) && KNETFILE_HOOKS

if (sameString(initialDb, "0"))
    {
    /* when an organism is selected from the custom track management page,
     * set the database to be the default only if it has custom tracks.
     * Otherwise, pick an assembly for that organism that does have custom tracks. */
    struct dbDb *dbDb, *dbDbs = getCustomTrackDatabases();
    char *dbWithCts = NULL;
    for (dbDb = dbDbs; dbDb != NULL; dbDb = dbDb->next)
        {
        if (sameString(database, dbDb->name))
            break;
        if (sameString(organism, dbDb->organism))
            {
            if (!dbWithCts)
                dbWithCts = cloneString(dbDb->name);
            }
        }
    if (dbWithCts)  // set the database for the selected organism to an assembly that
        {           // has custom tracks
        database = dbWithCts;
        cartSetString(cart, "db", database);
        }
    }

if (cartVarExists(cart, hgCtDoAdd))
    doAddCustom(NULL);
#ifdef PROGRESS_METER
else if (cartVarExists(cart, hgCtDoProgress))
    {
    doProgress(NULL);
    }
#endif
else if (cartVarExists(cart, hgCtTable))
    {
    /* update track */
    /* need to clone the hgCtTable value, as the ParseCart will remove
       the variable */
    selectedTable = cloneString(cartString(cart, hgCtTable));
    if (isNotEmpty(selectedTable))
        {
        ctList = customTracksParseCart(database, cart, NULL, NULL);
        ct = ctFromList(ctList, selectedTable);
        }
    if (ct)
        doUpdateCustom(ct, NULL);
    else
        doAddCustom(NULL);
    }
else
    {
    /* get new and existing custom tracks from cart and decide what to do */

    // setup a timer to periodically print out something to stdout to make sure apache or the web browser doesn't time us out (see redmine #3002).
    // e.g. see http://stackoverflow.com/questions/5547166/how-to-avoid-cgi-timeout
    struct sigaction *act;
    AllocVar(act);
    act->sa_handler = timer;
    act->sa_flags = SA_RESTART;
    sigaction(SIGALRM, act, NULL);
    alarm(TIMER_INTERVAL);

    char *customText = fixNewData(cart);
    /* save input so we can display if there's an error */
    char *savedCustomText = saveLines(cloneString(customText),
                                SAVED_LINE_COUNT);
    char *trackConfig = cartOptionalString(cart, hgCtConfigLines);
    char *savedConfig = cloneString(trackConfig);

    struct dyString *dsWarn = dyStringNew(0);
    char *fileName = cartOptionalString(cart, hgCtDataFileName);
    boolean hasData = (isNotEmpty(customText) || isNotEmpty(fileName));
    if (cartVarExists(cart, hgCtUpdatedTrack) && hasData)
        {
        /* from 'update' screen */
        /* prepend config to data for parser */
        struct dyString *dsTrack = dyStringNew(0);
        if (!trackConfig)
            trackConfig = cartOptionalString(cart, hgCtUpdatedTrack);
        char *fileContents = NULL;
        if (isNotEmpty(fileName))
            {
            if (customTrackIsCompressed(fileName))
                fileContents = "Compressed files not supported for data update";
            else
                fileContents = cartOptionalString(cart, hgCtDataFile);
            customText = fileContents;
            }
        /* check for duplicate track config in config and data entry */
        if (customTrackHasConfig(trackConfig) &&
            customTrackHasConfig(customText))
            {
            if (startsWith(trackConfig, customText))
                trackConfig = "";
            else
                customText = "Duplicate track configuration found - remove track and browser lines from Configuration box or from Data";
            }
        dyStringPrintf(dsTrack, "%s\n%s\n", trackConfig, customText);
        customText = dyStringCannibalize(&dsTrack);
        cartSetString(cart, hgCtDataText, customText);
        if (isNotEmpty(fileContents))
            {
            /* already handled file */
            cartRemove(cart, hgCtDataFile);
            cartRemove(cart, hgCtDataFileName);
            }
        }
    boolean ctParseError = FALSE;
    struct errCatch *catch = errCatchNew();
    if (errCatchStart(catch))
	ctList = customTracksParseCartDetailed(database, cart, &browserLines, &ctFileName,
					       &replacedCts, NULL, &err);
    errCatchEnd(catch);
    if (catch->gotError)
	{
	addWarning(dsWarn, err);
	addWarning(dsWarn, catch->message->string);
	ctParseError = TRUE;
	}
    errCatchFree(&catch);

    /* exclude special setting used by table browser to indicate
     * db assembly for error-handling purposes only */
    char *db = NULL;
    if (trackConfig && (db = stringIn("db=", trackConfig)) != NULL)
        {
        db += 3;
        char *nextTok = skipToSpaces(db);
        if (!nextTok)
            nextTok = strchr(db, 0);
        db = cloneStringZ(db,nextTok-db);
        if (!sameString(db,database))
            err = "Invalid configuration found - remove db= or return it to it's original value";
        }

    if (cartVarExists(cart, hgCtUpdatedTrack) && !hasData)
        {
        /* update custom track config and doc, but not data*/
        selectedTable = cartOptionalString(cart, hgCtUpdatedTable);
        if (selectedTable)
            {
            ct = ctFromList(ctList, selectedTable);
            if (ct)
                {
                struct errCatch *catch = errCatchNew();
                if (errCatchStart(catch))
                    {
                    customTrackUpdateFromConfig(ct, database, trackConfig, &browserLines);
                    ctUpdated = TRUE;
                    }
                errCatchEnd(catch);
                if (catch->gotError)
                    addWarning(dsWarn, catch->message->string);
                errCatchFree(&catch);
                }
            }
        }
    addWarning(dsWarn, replacedTracksMsg(replacedCts));
    doBrowserLines(browserLines, &warn);
    addWarning(dsWarn, warn);
    if (err)
	{
        char *selectedTable = NULL;
        cartSetString(cart, hgCtDataText, savedCustomText);
        cartSetString(cart, hgCtConfigLines, savedConfig);
        if ((selectedTable= cartOptionalString(cart, hgCtUpdatedTable)) != NULL)
            {
            ct = ctFromList(ctList, selectedTable);
            doUpdateCustom(ct, err);
            }
        else
            doAddCustom(err);
       	cartRemovePrefix(cart, hgCt);
	return;
	}
    if (cartVarExists(cart, hgCtDoDelete))
        {
	doDeleteCustom();
        ctUpdated = TRUE;
        }
    if (cartVarExists(cart, hgCtDoRefresh))
	{
	doRefreshCustom(&warn);
	addWarning(dsWarn, warn);
        ctUpdated = TRUE;
	}
    if (ctUpdated || ctConfigUpdate(ctFileName))
	{
	customTracksSaveCart(database, cart, ctList);

	/* refresh ctList again to pickup remote resource error state */
	struct errCatch *catch = errCatchNew();
	if (errCatchStart(catch))
	    ctList = customTracksParseCartDetailed(database, cart, &browserLines, &ctFileName,
					       &replacedCts, NULL, &err);
	errCatchEnd(catch);
	if (catch->gotError)
	    {
	    addWarning(dsWarn, err);
	    addWarning(dsWarn, catch->message->string);
	    ctParseError = TRUE;
	    }
	errCatchFree(&catch);

	}
    warn = dyStringCannibalize(&dsWarn);
    if (measureTiming)
	{
	long lastTime = clock1000();
	loadTime = lastTime - thisTime;
	}
    if (!initialDb || ctList || cartVarExists(cart, hgCtDoDelete))
        doManageCustom(warn);
    else if (ctParseError)
	doAddCustom(warn);
    else
        doAddCustom(NULL);
    }
cartRemovePrefix(cart, hgCt);
cartRemove(cart, CT_CUSTOM_TEXT_VAR);
}


int main(int argc, char *argv[])
/* Process command line. */
{
htmlPushEarlyHandlers();
oldVars = hashNew(10);
cgiSpoof(&argc, argv);
cartEmptyShell(doMiddle, hUserCookie(), excludeVars, oldVars);
return 0;
}

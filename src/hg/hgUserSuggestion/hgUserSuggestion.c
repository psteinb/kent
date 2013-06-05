/* hgUserSuggestion - CGI-script to collect user's suggestion. */

#include "common.h"
#include "errabort.h"
#include "hCommon.h"
#include "jksql.h"
#include "portable.h"
#include "cheapcgi.h"
#include "htmshell.h"
#include "hdb.h"
#include "hui.h"
#include "cart.h"
#include "hPrint.h"
#include "dbDb.h"
#include "web.h"
#include "hash.h"
#include "hgConfig.h"
#include "hgUserSuggestion.h"
#include "mailViaPipe.h"

/* ---- Global variables. ---- */
struct cart *cart;	/* The user's ui state. */
struct hash *oldVars = NULL;

/* ---- Global helper functions ---- */
char *mailToAddr()
/* Return the address to send suggestion to  */
{
if isEmpty(cfgOption(CFG_SUGGEST_MAILTOADDR))
    return cloneString("NULL_suggest.mailToAddr");
else
    return cloneString(cfgOption(CFG_SUGGEST_MAILTOADDR));
}

char *mailFromAddr()
/* Return the bogus sender address to help filter out spam */
{
if isEmpty(cfgOption(CFG_SUGGEST_MAILFROMADDR))
    return cloneString("NULL_suggest.mailFromAddr");
else
    return cloneString(cfgOption(CFG_SUGGEST_MAILFROMADDR));
}

char *filterKeyword()
/* Return the keyword used to filter out spam  */
{
if isEmpty(cfgOption(CFG_FILTERKEYWORD))
    return cloneString("NULL_suggest.filterKeyword");
else
    return cloneString(cfgOption(CFG_FILTERKEYWORD));
}

char *mailSignature()
/* Return the signature to be used by outbound mail or NULL. Allocd
 * here. */
{
if isEmpty(cfgOption(CFG_SUGGEST_MAIL_SIGNATURE))
    return cloneString("NULL_mailSignature");
else
    return cloneString(cfgOption(CFG_SUGGEST_MAIL_SIGNATURE));
}

char *mailReturnAddr()
/* Return the return addr. to be used by outbound mail or NULL. Allocd
 * here. */
{
if isEmpty(cfgOption(CFG_SUGGEST_MAIL_RETURN_ADDR))
    return cloneString("NULL_mailReturnAddr");
else
    return cloneString(cfgOption(CFG_SUGGEST_MAIL_RETURN_ADDR));
}

char *browserName()
/* Return the browser name like 'UCSC Genome Browser' */
{
if isEmpty(cfgOption(CFG_SUGGEST_BROWSER_NAME))
    return cloneString("NULL_browserName");
else
    return cloneString(cfgOption(CFG_SUGGEST_BROWSER_NAME));
}

static char *now()
/* Return a mysql-formatted time like "2008-05-19 15:33:34". */
{
char nowBuf[256];
time_t curtime;
curtime = time (NULL); 
struct tm *theTime = localtime(&curtime);
strftime(nowBuf, sizeof nowBuf, "%Y-%m-%d %H:%M:%S", theTime);
return cloneString(nowBuf);
}

void resetAllSuggFields()
/* clear all suggestion fields */
{
cartRemove(cart, "suggestCfmEmail");
cartRemove(cart, "suggestDetails");
cartRemove(cart, "suggestEmail");
cartRemove(cart, "suggestName");
cartRemove(cart, "suggestSubject");
cartRemove(cart, "suggestSummary");
}

/* javascript functions */
void printMainForm()
/* javascript to print mainForm */
{
hPrintf(
    "     <FORM ACTION=\"../cgi-bin/hgUserSuggestion?do.suggestSendMail=1\" METHOD=\"POST\" ENCTYPE=\"multipart/form-data\" NAME=\"mainForm\" onLoad=\"document.forms.mainForm.name.focus()\">\n");
hPrintf(
    "     <H2>User Suggestion Form</H2>\n"
    "     <P> Please use this form to submit ... </P>\n"
);
hPrintf(
    "      <div id=\"suggest\">  \n"
    "       <label for=\"name\">Your Name:</label><input type=\"text\" name=\"suggestName\" id=\"name\" size=\"50\" /><BR>\n"
    "       <label for=\"email\">Your Email:</label><input type=\"text\" name=\"suggestEmail\" id=\"email\" size=\"50\" /><BR>   \n"
    "       <label for=\"confirmEmail\">Re-enter Your Email:</label><input type=\"text\" \n"
    "          name=\"suggestCfmEmail\" id=\"cfmemail\" size=\"50\" /><BR>   \n");
hPrintf(
    "       <label for=\"category\">Category:</label><select name=\"suggestCategory\" id=\"category\">\n"
    "         <option selected>New feature request</option> \n"
    "         <option>New utilities request</option>\n"
    "         <option>New/Update genome request</option>\n"
    "         <option>New track request</option>\n"
    "         <option>Others</option>\n"
    "         </select><BR>\n");
hPrintf(
    "       <label for=\"summary\">Summary:</label><input type=\"text\" name=\"suggestSummary\" id=\"summary\" size=\"50\" /><BR>\n"
    "       <label for=\"details\">Details:</label><BR><textarea name=\"suggestDetails\" id=\"details\" cols=\"100\" rows=\"10\"></textarea><BR>  \n"
    "     </div>\n");
hPrintf(
    "         <p>\n"
    "           <label for=\"code\">Write CAPTCHA security code below (disabled) > <span id=\"txtCaptchaDiv\" style=\"color:#F00\"></span><BR><!-- this is where the script will place the generated code --> \n"
    "           <input type=\"hidden\" id=\"txtCaptcha\" /></label><!-- this is where the script will place a copy of the code for validation: this is a hidden field -->\n"
    "           <input type=\"text\" name=\"txtInput\" id=\"txtInput\" size=\"30\" />\n"
    "         </p>\n");
hPrintf(
    "      <div class=\"formControls\">\n"
    "        <input id=\"sendButton\" type=\"button\" value=\"Send\" onclick=\"submitform()\"/> \n"
    "        <input type=\"reset\" name=\"suggestClear\" value=\"Clear\" class=\"largeButton\"> \n"
    "        <input type=\"cancel\" name=\"Cancel\" value=\"Cancel\" class=\"largeButton\">\n"
    "      </div>\n"
    "      \n"
    "     </FORM>\n\n");
}
void printValidateScript()
/* javascript to validate form inputs */
{
hPrintf(  
    "    <script type=\"text/javascript\">\n"
    "    function validateMainForm(theform)\n"
    "    {\n"
    "    var x=theform.suggestName.value;\n"
    "    if (x==null || x==\"\")\n"
    "      {\n"
    "      alert(\"Name field must be filled out\");\n"
    "      theform.suggestName.focus() ;\n"
    "      return false;\n"
    "      }\n");
hPrintf(
    "    if (!validateMailAddr(theform.suggestEmail.value))\n"
    "      {\n"
    "      alert(\"Not a valid e-mail address\");\n"
    "      theform.suggestEmail.focus() ;\n"
    "      return false;\n"
    "      }\n"
    "    var str1 = theform.suggestEmail.value;\n"
    "    var str2 = theform.suggestCfmEmail.value;\n"
    "    if (str2==null || str2==\"\")\n"
    "      {\n"
    "      alert(\"Please re-enter your email address.\");\n"
    "      theform.suggestCfmEmail.focus();\n"
    "      return false;\n"
    "      }\n"
    "    if (str1 != str2)\n"
    "      {\n"
    "      alert(\"Email addresses do not match, please re-enter.\");\n"
    "      theform.suggestCfmEmail.focus();\n"
    "      return false;\n"
    "      }\n");
hPrintf(
    "    var y=theform.suggestSummary.value;\n"
    "    if (y==null || y==\"\")\n"
    "      {\n"
    "      alert(\"Summary field must be filled out\");\n"
    "      theform.suggestSummary.focus() ;\n"
    "      return false;\n"
    "      }          \n"
    "    var z=theform.suggestDetails.value;\n"
    "    if (z==null || z==\"\")\n"
    "      {\n"
    "      alert(\"Details field must be filled out\");\n"
    "      theform.suggestDetails.focus() ;\n"
    "      return false;\n"
    "      }\n"
    "    return true; \n"
    "    }\n\n");
hPrintf(
    "    function validateMailAddr(x)\n"
    "    {\n"
    "    var atpos=x.indexOf(\"@\");\n"
    "    var dotpos=x.lastIndexOf(\".\");\n"
    "    if (atpos<1 || dotpos<atpos+2 || dotpos+2>=x.length)\n"
    "      {\n"
    "      return false;\n"
    "      } \n"
    "    return true;\n"
    "    }\n"
    "    </script><br />\n\n");
}

void printCheckCaptchaScript()
/* javascript to check CAPTCHA code */
{
hPrintf( 
    " <script type=\"text/javascript\">\n"
    "         function checkCaptcha(theform){\n"
    "                 var why = \"\";\n"
    "                  \n"
    "                 if(theform.txtInput.value == \"\"){\n"
    "                         why += \"- Security code should not be empty.\n\";\n"
    "                 }\n"
    "                 if(theform.txtInput.value != \"\"){\n"
    "                         if(ValidCaptcha(theform.txtInput.value) == false){\n"
    "                                 why += \"- Security code did not match.\n\";\n"
    "                         }\n"
    "                 }\n"
    "                 if(why != \"\"){\n"
    "                         alert(why);\n"
    "                         theform.txtInput.focus() ;\n"
    "                         return false;\n"
    "                 }\n"
    "            return true;\n"
    "         }\n\n");
hPrintf(
    "         var a = Math.ceil(Math.random() * 9)+ '';\n"
    "         var b = Math.ceil(Math.random() * 9)+ '';\n"
    "         var c = Math.ceil(Math.random() * 9)+ '';\n"
    "         var d = Math.ceil(Math.random() * 9)+ '';\n"
    "         var e = Math.ceil(Math.random() * 9)+ '';\n\n"
    "         var code = a + b + c + d + e;\n"
    "         document.getElementById(\"txtCaptcha\").value = code;\n"
    "         document.getElementById(\"txtCaptchaDiv\").innerHTML = code;\n\n");
hPrintf(
    " function ValidCaptcha(){\n"
    "         var str1 = removeSpaces(document.getElementById('txtCaptcha').value);\n"
    "         var str2 = removeSpaces(document.getElementById('txtInput').value);\n"
    "         if (str1 == str2){\n"
    "                 return true;\n"
    "         } else {\n"
    "                 return false;\n"
    "         }\n"
    " }\n\n");
hPrintf(
    " function removeSpaces(string){\n"
    "         return string.split(' ').join('');\n"
    " }\n"
    " </script><br />\n\n");
}

void printSubmitFormScript()
/* javascript to submit form */
{
hPrintf(
    "     <script type=\"text/javascript\">\n"
    "     function submitform()\n"
    "     {\n"
    "      if ( validateMainForm(document.forms[\"mainForm\"]) )\n"
//    "      if ( validateMainForm(document.forms[\"mainForm\"]) && checkCaptcha(document.forms[\"mainForm\"]))\n"
    "        {\n"
    "          document.forms[\"mainForm\"].submit();\n"
    "        }\n"
    "     }\n"
    "     </script>\n\n");
}

void printSuggestionConfirmed()
{
hPrintf(
    "<h2>Thank you for your suggestion!</h2>");
hPrintf(
    "<p>"
    "Thank you for your suggestion regarding the UCSC Genome Browser.<BR>"
    "A confirmation mail has send to you containing an unique suggestion ID,<BR>"
"Please use this ID for all future communications related to this suggestion.</p><BR>");
hPrintf(
    "<p><a href=\"hgUserSuggestion\">Click here for more suggestions</a><BR></p>");

} 

void sendSuggestionBack(char *sName, char *sEmail, char *sCategory, char *sSummary, char *sDetails, char *suggestID)
/* send back the suggestion */
{
/* parameters from hg.cong */
char *mailTo = mailToAddr();
char *mailFrom=mailFromAddr();
char *filter=filterKeyword();
char subject[256];
char msg[4096]; /* need to make larger */
safef(msg, sizeof(msg),
    "SuggestionID:: %s\nUserName:: %s\nUserEmail:: %s\nCategory:: %s\nSummary:: %s\n\n\nDetails::\n%s",
    suggestID, sName, sEmail, sCategory, sSummary, sDetails);

safef(subject, sizeof(subject),"%s %s", filter, suggestID);   
int result;
result = mailViaPipe(mailTo, subject, msg, mailFrom);
}

void sendConfirmMail(char *emailAddr, char *suggestID)
/* send user suggestion confirm mail */
{
char subject[256];
char msg[4096];
char *remoteAddr=getenv("REMOTE_ADDR");
char brwName[256];
char returnAddr[256];
char signature[256];
safecpy(brwName,sizeof(brwName), browserName());
safecpy(returnAddr,sizeof(returnAddr), mailReturnAddr());
safecpy(signature,sizeof(signature), mailSignature());

safef(subject, sizeof(subject),"Thank you for your suggestion to the %s", brwName);
safef(msg, sizeof(msg),
    "  Someone (probably you, from IP address %s) submitted a suggestion to the %s.\nThe suggestion has been assigned an ID \"%s\".\nPlease use this ID for all future communications related to ths suggestion.\n\nThanks!\n%s\n%s",
remoteAddr, brwName, suggestID, signature, returnAddr);
int result;
result = mailViaPipe(emailAddr, subject, msg, returnAddr);
}

void askForSuggest(char *organism, char *db)
/* Put up the suggestion form. */
{
printMainForm();
printValidateScript();
printCheckCaptchaScript();
printSubmitFormScript();
//cartSaveSession(cart);
}

void  submitSuggestion()
/* send the suggestion to ,.. */
{
/* parameters from hg.cong */
char *filter=filterKeyword();

/* values from cart */
char *sName=cartUsualString(cart,"suggestName","");
char *sEmail=cartUsualString(cart,"suggestEmail","");
char *sCategory=cartUsualString(cart,"suggestCategory","");
char *sSummary=cartUsualString(cart,"suggestSummary","");
char *sDetails=cartUsualString(cart,"suggestDetails","");

char suggestID[256];
safef(suggestID, sizeof(suggestID),"%s-%s", sEmail, now());
char subject[256];
safef(subject, sizeof(subject),"%s %s", filter, suggestID);
/* send back the suggestion */
sendSuggestionBack(sName, sEmail, sCategory, sSummary, sDetails, suggestID);
/* send confirmation mail to user */
sendConfirmMail(sEmail,suggestID);
/* display confirmation page */
printSuggestionConfirmed();
cartRemove(cart, "do.suggestSendMail");
}

void doMiddle(struct cart *theCart)
/* Write header and body of html page. */
{
char *db, *organism;

cart = theCart;
getDbAndGenome(cart, &db, &organism, oldVars);

    cartWebStart(theCart, db, "User Suggestion");

if (cartVarExists(cart, "do.suggestSendMail"))
{
    submitSuggestion();
    cartRemove(cart, "do.suggestSendMail");
    return;
}

    askForSuggest(organism,db);
    cartWebEnd();
}

/* Null terminated list of CGI Variables we don't want to save
 * permanently. */
char *excludeVars[] = {"Submit", "submit", "Clear", NULL};

int main(int argc, char *argv[])
/* Process command line. */
{
long enteredMainTime = clock1000();
oldVars = hashNew(10);
cgiSpoof(&argc, argv);

htmlSetStyleSheet("/style/userAccounts.css");
htmlSetFormClass("accountScreen");
cartHtmlShell("User Suggestion",doMiddle, hUserCookie(), excludeVars, oldVars);
//cartEmptyShell(doMiddle, hUserCookie(), excludeVars, oldVars);
cgiExitTime("hgUserSuggestion", enteredMainTime);
return 0;
}


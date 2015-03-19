/* wikiLink - interoperate with a wiki site (share user identities). */

/* Copyright (C) 2014 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#ifndef WIKILINK_H
#define WIKILINK_H

/* hg.conf wiki parameters -- wikiLink is disabled if any are undefined. */
#define CFG_WIKI_HOST "wiki.host"
#define CFG_WIKI_USER_NAME_COOKIE "wiki.userNameCookie"
#define CFG_WIKI_LOGGED_IN_COOKIE "wiki.loggedInCookie"
#define CFG_WIKI_SESSION_COOKIE "wiki.sessionCookie"

/* hg.conf login system parameter -- using non-wiki login system if defined */ 
#define CFG_LOGIN_SYSTEM_NAME "login.systemName"

char *loginSystemName();
/* Return the wiki host specified in hg.conf, or NULL.  Allocd here. */

boolean loginSystemEnabled();
/* Return TRUE if login.systemName  parameter is defined in hg.conf . */

char *wikiLinkHost();
/* Return the wiki host specified in hg.conf, or NULL.  Allocd here. */

boolean wikiLinkEnabled();
/* Return TRUE if all wiki.* parameters are defined in hg.conf . */

char *wikiLinkUserName();
/* Return the user name specified in cookies from the browser, or NULL if 
 * the user doesn't appear to be logged in. */

char *wikiLinkUserLoginUrl(char *hgsid);
/* Return the URL for the wiki user login page. */

char *wikiLinkUserLoginUrlReturning(char *hgsid, char *returnUrl);
/* Return the URL for the wiki user login page. */

char *wikiLinkUserLogoutUrl(char *hgsid);
/* Return the URL for the wiki user logout page. */

char *wikiLinkUserLogoutUrlReturning(char *hgsid, char *returnUrl);
/* Return the URL for the wiki user logout page. */

char *wikiLinkUserSignupUrl(char *hgsid);
/* Return the URL for the user signup  page. */

char *wikiLinkChangePasswordUrl(char *hgsid);
/* Return the URL for the user change password page. */

#endif /* WIKILINK_H */

/* botDelay - contact bottleneck server and sleep 
 * for a little bit if IP address looks like it is
 * being just too demanding. */

/* Copyright (C) 2004 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

int botDelayTime(char *host, int port, char *ip);
/* Figure out suggested delay time for ip address in
 * milliseconds. */

void botDelayCgi(char *host, int port, boolean noWarn);
/* Connect with bottleneck server and sleep the
 * amount it suggests for IP address calling CGI script. */

void botDelayMessage(char *ip, int millis);
/* Print out message saying why you are stalled. */

void hgBotDelay();
/* High level bot delay call - for use with regular webpage output */ 

void hgBotDelayNoWarn();
/* High level bot delay call without warning - for use with non-webpage output */

int hgBotDelayTime();
/* Get suggested delay time from cgi. */


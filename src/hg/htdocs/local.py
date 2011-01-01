#!/usr/bin/env python

# Get info about latest data updates and browser builds, formatted
# for printing in the genome browser

import cgi

import os, stat, time

debug = False

# Change to cgitb.enable(display=0, logdir="/tmp") for non-debugging
import cgitb; cgitb.enable();

from subprocess import Popen, PIPE

# Return a pipeline primed with some commands
def getPipeline(filePattern):
    p1 = Popen(["/bin/ls", "/scratch/build/log"], stdout=PIPE)
    p2 = Popen(["/bin/fgrep", filePattern], stdin=p1.stdout, stdout=PIPE)
    return Popen(["/bin/cut", "-d.", "-f", "1"], stdin=p2.stdout, stdout=PIPE)

# Return the most recent 'count' updates (as strings) matching the 'name'
def getUpdateTimestamps(name, count):
    p = getPipeline("ucsc" + name + ".log")
    p2 = Popen(["/usr/bin/tail", "-n", str(count)], stdin=p.stdout, stdout=PIPE)
    output = p2.communicate()[0]
    output = output.split("\n")
    return output[0:len(output) - 1]

# Get modified time in both machine and human-readable form from a file
def getTimestamp(name):
    statinfo = os.stat(name)
    mtime = statinfo[stat.ST_MTIME]
    readableMtime = time.strftime("%Y-%m-%d %T %Z", time.localtime(mtime))
    return readableMtime, mtime

# Print the last 'count' timestamps from 'aList'
def printTimestamps(aList, count):
    listEnd = max = len(aList)
    if max > count:
        max = 2
    slice = aList[listEnd - max: listEnd]
    for i in reversed(slice):
        timestamp = getTimestamp("/scratch/build/log/" + i + ".ucscDbUpdate.log")[0]
        print """<a href="updatelog/%s.ucscDbUpdate.log"><tt>%s</tt></a><br>""" % (i, timestamp)

# Get count of number of last updates
p = getPipeline("ucscDbUpdate.log")
p = Popen(["/usr/bin/wc", "-l"], stdin=p.stdout, stdout=PIPE)
count = p.communicate()[0].split("\n")[0]
if count > 3:
    count = 3

# Get timestamps of up to 'count' most recent database updates
dbUpdates = getUpdateTimestamps("DbUpdate", count)

# Get timestamp of most recent code import
codeUpdate = getUpdateTimestamps("Import", 1)[0]
codeUpdate, codeUpdateTime = getTimestamp("/scratch/build/log/" + codeUpdate + ".ucscImport.log")

# Get info for dev, personal browser build dates
devUpdate = getTimestamp("../cgi-bin/hgTracks")[0]
bindir= "../cgi-bin"
server = os.getenv("SERVER_NAME")
isUserBrowser = server.find('-') > 0
if isUserBrowser:
    serverOwner = server.split("-")[1]
    serverOwner = serverOwner.split(".")[0]
    bindir += '-' + serverOwner
oldBrowser = False
try:
    browserUpdate, browserUpdateTime = getTimestamp(bindir + "/hgTracks")
    if codeUpdateTime - browserUpdateTime > 0:
        browserUpdate = "<font color=\"red\">" + browserUpdate + "</font>"
        oldBrowser = True
except OSError:
    browserUpdate = "<font color=\"red\">No browser built</font>"

print """
<!--Local Content---------------------------------------------->  
        <TABLE WIDTH="100%%" BGCOLOR="#888888" BORDER="0" CELLSPACING="0" CELLPADDING="1">
            <TR><TD>        
            <TABLE BGCOLOR="#fffee8" WIDTH="100%%"  BORDER="0" CELLSPACING="0" CELLPADDING="0">
                <TR><TD>        
                <TABLE BGCOLOR="#D9E4F8" BACKGROUND="images/hr.gif" WIDTH="100%%"><TR><TD>
                    <FONT SIZE="4"><B>&nbsp; Bejerano Lab information</B></FONT>
                </TD></TR></TABLE>
        
                <TABLE BGCOLOR="#fffee8" WIDTH="100%%" CELLPADDING=0>
                    <TR><TH HEIGHT=3></TH></TR>
                    <TR><TD WIDTH=10></TD><TD>
                    <P>
                    Updates to the the Bejerano lab genome browser:
                    <p>
                    <table cellpadding="2" cellspacing="0">
                      <tr align="left">
                        <td valign="top">Tracks and related files from UCSC:</td><td width="5"/>
                        <td>
"""
printTimestamps(dbUpdates, count)
print """
                        </td>
                      </tr>
                      <tr>
                        <td>Browser code in the SVN repository:</td><td width="5"/>
                        <td><tt>%s</tt></td>
                      </tr>
                      <tr>
                      <td>Last build of the <i>dev.stanford.edu</i> browser code:</td><td width="5"/>
                        <td><tt>%s</tt></td>
                      </tr>
""" % (codeUpdate, devUpdate)
if isUserBrowser:
    print """
                      <tr>
                        <td>Last build of <i>%s</i> code:</td><td width="5"/>
                        <td><tt>%s</tt></td>
                      </tr>
    """ % (server, browserUpdate)
    if oldBrowser:
        print """
                      <tr>
                        <td colspan="3"><i><font color="red">%s is out-of-date compared to the SVN repository!</font></i></td>
                      </tr>
        """ % (server)
print """
                    </table>
                    </TD><TD WIDTH=15>
                </TD></TR></TABLE>
            <BR></TD></TR></TABLE>
        </TD></TR></TABLE>
"""

if debug:
    print "environment: "
    for k in os.environ.keys():
        print "%20s: %s" % (k, os.environ[k])

/* encode2CopyManifest - Copy files in encode2 manifest and in case of tar'd files rezip them 
 * independently. */

#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "md5.h"
#include "portable.h"
#include "obscure.h"

char *dataDir = "/scratch/kent/encValData";
char *tempDir = "/tmp";

void usage()
/* Explain usage and exit. */
{
errAbort(
  "encode2CopyManifest - Copy files in encode2 manifest and in case of tar'd files rezip them independently.\n"
  "usage:\n"
  "   encode2CopyManifest sourceDir sourceManifest destDir destManifest\n"
  "options:\n"
  "   -dataDir=/path/to/encode/asFilesAndChromSizesEtc\n"
  "   -tmpDir=/tmp\n"
  );
}

/* Command line validation table. */
static struct optionSpec options[] = {
   {"dataDir", OPTION_STRING},
   {NULL, 0},
};

#define FILEINFO_NUM_COLS 6

struct manifestInfo
/* Information on one file */
    {
    struct manifestInfo *next;  /* Next in singly linked list. */
    char *fileName;	/* Name of file with directory relative to manifest */
    char *format;	/* bam fastq etc */
    char *experiment;	/* wgEncodeXXXX */
    char *replicate;	/* 1 2 both n/a */
    char *enrichedIn;	/* promoter exon etc. */
    char *md5sum;	/* Hash of file contents or n/a */
    };

struct manifestInfo *manifestInfoLoad(char **row)
/* Load a manifestInfo from row fetched with select * from manifestInfo
 * from database.  Dispose of this with manifestInfoFree(). */
{
struct manifestInfo *ret;

AllocVar(ret);
ret->fileName = cloneString(row[0]);
ret->format = cloneString(row[1]);
ret->experiment = cloneString(row[2]);
ret->replicate = cloneString(row[3]);
ret->enrichedIn = cloneString(row[4]);
ret->md5sum = cloneString(row[5]);
return ret;
}

void manifestInfoTabOut(struct manifestInfo *mi, FILE *f)
/* Write tab-separated version of manifestInfo to f */
{
fprintf(f, "%s\t", mi->fileName);
fprintf(f, "%s\t", mi->format);
fprintf(f, "%s\t", mi->experiment);
fprintf(f, "%s\t", mi->replicate);
fprintf(f, "%s\t", mi->enrichedIn);
fprintf(f, "%s\n", mi->md5sum);
}

struct manifestInfo *manifestInfoLoadAll(char *fileName)
/* Load all manifestInfos from file. */
{
struct lineFile *lf = lineFileOpen(fileName, TRUE);
char *row[FILEINFO_NUM_COLS];
struct manifestInfo *list = NULL, *fi;
while (lineFileRow(lf, row))
   {
   fi = manifestInfoLoad(row);
   slAddHead(&list, fi);
   }
slReverse(&list);
return list;
}

void makeDirOnlyOnce(char *dir, struct hash *hash)
/* Check if dir is already in hash.  If so we're done.  If not make dir and add it to hash. */
{
if (!hashLookup(hash, dir))
    {
    verbose(2, "make dir %s\n", dir);
    hashAdd(hash, dir, NULL);
    makeDirs(dir);
    }
}

void systemCommand(char *s)
/* Do system() command on s,  and check error status, aborting with message if
 * any problem. */
{
verbose(1, "cmd> %s\n", s);
int err = system(s);
if (err != 0)
    errAbort("err %d\nCouldn't %s", err, s);
#ifdef SOON
#endif /* SOON */
}

void untgzIntoDir(char *tgzFile, char *dir)
/* Will do the equivalent of
 *    cd dir
 *    tar -zx tgzFile */
{
char *origDir = cloneString(getCurrentDir());
struct dyString *command = dyStringNew(0);
setCurrentDir(dir);
dyStringPrintf(command, "tar -zxf %s", tgzFile);
systemCommand(command->string);
dyStringFree(&command);
freez(&origDir);
}

void doGzippedBedToBigBed(char *bedFile, char *asType, char *bedType, char *bigBed)
/* Convert some bed file to an as file. */
{
struct dyString *cmd = dyStringNew(0);
char *tempName = rTempName(tempDir, "b2bb", ".bed");
if (tempName == NULL)
    errAbort("Can't find tempName");
dyStringPrintf(cmd, "zcat %s | sort -k1,1 -k2,2n > %s", bedFile, tempName);
systemCommand(cmd->string);
dyStringClear(cmd);
dyStringPrintf(cmd, "bedToBigBed -type=%s -as=%s/as/%s.as %s %s/hg19/chrom.sizes %s",
    bedType, dataDir, asType, tempName, dataDir, bigBed);
systemCommand(cmd->string);
dyStringClear(cmd);
dyStringPrintf(cmd, "rm %s", tempName);
systemCommand(cmd->string);
dyStringFree(&cmd);
}


void gzipFastqs(char *dir, struct manifestInfo *mi, FILE *f)
/* Check that all files in dir have fastq suffix, and gzip them. */
{
/* Get file list and check all are .fastq. */
struct fileInfo *fi, *fiList = listDirX(dir, "*", FALSE);
for (fi = fiList; fi != NULL; fi = fi->next)
    if (!endsWith(fi->name, ".fastq") && !endsWith(fi->name, ".fastq.gz"))
        errAbort("%s is not fastq inside %s", fi->name, dir);

/* Do system calls for gzip. */
struct dyString *cmd = dyStringNew(0);
for (fi = fiList; fi != NULL; fi = fi->next)
    {
    if (!endsWith(fi->name, ".gz"))
	{
	dyStringPrintf(cmd, "gzip %s/%s", dir, fi->name);
	systemCommand(cmd->string);
	}
    }

/* Write out revised manifest. */
char *oldFileName = mi->fileName;
char *oldFormat = mi->format;
mi->format = "fastq";
for (fi = fiList; fi != NULL; fi = fi->next)
    {
    char *suffix = (endsWith(fi->name, ".gz") ? "" : ".gz");
    char name[PATH_LEN];
    safef(name, sizeof(name), "%s.dir/%s%s", oldFileName, fi->name, suffix);
    mi->fileName = name;
    manifestInfoTabOut(mi, f);
    }
mi->format = oldFormat;
mi->fileName = oldFileName;
}

void doCopyFile(char *sourcePath, char *destPath)
/* Issue system command to copy file. */
{
struct dyString *cmd = dyStringNew(0);
dyStringPrintf(cmd, "cp -a %s %s", sourcePath, destPath);
systemCommand(cmd->string);
dyStringFree(&cmd);
}

#ifdef OLD
    processManifestItem(mi, sourceDir, destDir, destDirHash, f);
         sourcePath, destPath, destDirHash, destDir, f);
#endif /* OLD */

void processManifestItem(struct manifestInfo *mi, char *sourceRoot, 
    char *destRoot, struct hash *dirHash, FILE *f)
/* Process a line from the manifest.  Source path is the full path to the source file.
 * destPath is where to put the destination in the straightforward case where the destination
 * is just a single file.  In the more complex case dirHash and destDir are helpful.
 * The dirHash contains directories that are already known to exist, and helps keep the
 * program from making the same directory repeatedly.  The destRootDir parameter 
 * contains the top level destination dir.  The destPath is the same as destRootDir + mi->fileName
 * and typically mi->fileName will include a / or two.  Finally the f parameter is where
 * the program writes the revised manifest file,  after whatever transformation occurred in
 * the processing step.
 *    What occurs inside the function is:
 * o - Most files are just copied.  Optionally an md5-sum might be checked.
 * o - Files that are tgz's of multiple fastqs are split into individual fastq.gz's inside
 *     a directory named after the archive. */
{
/* Make up bunches of components for file names. */
char *fileName = mi->fileName;
char sourcePath[PATH_LEN];
safef(sourcePath, sizeof(sourcePath), "%s/%s", sourceRoot, fileName);
char destPath[PATH_LEN];
char destDir[PATH_LEN], destFileName[FILENAME_LEN], destExtension[FILEEXT_LEN];
safef(destPath, sizeof(destPath), "%s/%s", destRoot, fileName);
splitPath(destPath, destDir, destFileName, destExtension);

verbose(1, "# %s\t%s\n", fileName, mi->format);
if (endsWith(fileName, ".fastq.tgz"))
    {
    char outDir[PATH_LEN];
    safef(outDir, sizeof(outDir), "%s.dir", destPath);
    verbose(2, "Unpacking %s into %s\n", sourcePath, outDir);
    makeDir(outDir);
    untgzIntoDir(sourcePath, outDir);
    gzipFastqs(outDir, mi, f);
    }
else if (endsWith(fileName, ".narrowPeak.gz"))
    {
    char outFileName[FILENAME_LEN];
    safef(outFileName, sizeof(outFileName), "%s%s", destFileName, ".bigBed");
    char outPath[PATH_LEN];
    safef(outPath, sizeof(outPath), "%s%s%s", destDir, destFileName, ".bigBed");
    doGzippedBedToBigBed(sourcePath, "narrowPeak", "bed6+4", outPath);
    mi->fileName = cloneString(outFileName);
    manifestInfoTabOut(mi, f);
    }
else
    {
    verbose(2, "copyFile(%s,%s)\n", sourcePath, destPath);
    doCopyFile(sourcePath, destPath);
    manifestInfoTabOut(mi, f);
    }
}


void encode2CopyManifest(char *sourceDir, char *sourceManifest, char *destDir, char *destManifest)
/* encode2CopyManifest - Copy files in encode2 manifest and in case of tar'd files rezip them 
 * independently. */
{
struct manifestInfo *fileList = manifestInfoLoadAll(sourceManifest);
verbose(1, "Loaded information on %d files from %s\n", slCount(fileList), sourceManifest);
verboseTimeInit();
FILE *f = mustOpen(destManifest, "w");
struct manifestInfo *mi;
struct hash *destDirHash = hashNew(0);
makeDirOnlyOnce(destDir, destDirHash);
for (mi = fileList; mi != NULL; mi = mi->next)
    {
    /* Make path to source file. */
    char sourcePath[PATH_LEN];
    safef(sourcePath, sizeof(sourcePath), "%s/%s", sourceDir, mi->fileName);

    /* Make destination dir */
    char localDir[PATH_LEN];
    splitPath(mi->fileName, localDir, NULL, NULL);
    char destSubDir[PATH_LEN];
    safef(destSubDir, sizeof(destSubDir), "%s/%s", destDir, localDir);
    makeDirOnlyOnce(destSubDir, destDirHash);

    char destPath[PATH_LEN];
    safef(destPath, sizeof(destPath), "%s/%s", destDir, mi->fileName);

    processManifestItem(mi, sourceDir, destDir, destDirHash, f);

#ifdef OLD
    /* If md5sum available check it. */
    if (!sameString(mi->md5sum, "n/a"))
        {
	char *md5sum = md5HexForFile(sourcePath);
	verboseTime(1, "md5Summed %s (%lld bytes)", mi->fileName, (long long)fileSize(sourcePath));
	if (!sameString(md5sum, mi->md5sum))
	    {
	    warn("md5sum mismatch on %s, %s in metaDb vs %s in file", sourcePath, mi->md5sum, md5sum);
	    fprintf(f, "md5sum mismatch on %s, %s in metaDb vs %s in file\n", sourcePath, mi->md5sum, md5sum);
	    ++mismatch;
	    verbose(2, "%d md5sum matches, %d mismatches\n", match, mismatch);
	    }
	else 
	    ++match;
	}
#endif /* OLD */
    }
carefulClose(&f);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
dataDir = optionVal("dataDir", dataDir);
if (argc != 5)
    usage();
encode2CopyManifest(argv[1], argv[2], argv[3], argv[4]);
return 0;
}

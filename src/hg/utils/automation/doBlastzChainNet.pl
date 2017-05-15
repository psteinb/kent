#!/usr/bin/env perl

# DO NOT EDIT the /cluster/bin/scripts copy of this file --
# edit ~/kent/src/hg/utils/automation/doBlastzChainNet.pl instead.

# $Id: doBlastzChainNet.pl,v 1.33 2010/04/12 16:33:12 hiram Exp $

# to-do items:
# - lots of testing
# - better logging: right now it just passes stdout and stderr,
#   leaving redirection to a logfile up to the user
# - -swapBlastz, -loadBlastz
# - -tDb, -qDb
# - -tUnmasked, -qUnmasked
# - -axtBlastz
# - another Gill wish list item: save a lav header (involves run-blastz-ucsc)
# - 2bit / multi-sequence support when abridging?
# - reciprocal best?
# - hgLoadSeq of query instead of assuming there's a $qDb database?

use Getopt::Long;
use warnings;
use strict;
use diagnostics;
use FindBin qw($Bin);
use lib "$Bin";
use HgAutomate;
use HgRemoteScript;
use HgStepManager;
use File::Basename;

# Hardcoded paths/command sequences:

print "BIN: $Bin\n";
my $getFileServer = 'fileServer';
my $blastzRunUcsc = "blastz-run-ucsc";
my $partition = "partitionSequence.pl";
my $clusterLocal = '/scratch/tmp';
my $clusterSortaLocal = '/scratch/tmp';
my @clusterNAS = ('/scratch/tmp');
my $clusterNAS = join('/... or ', @clusterNAS) . '/...';
my @clusterNoNo = ('/afs');
my @fileServerNoNo = ('kkhome', 'kks00');
my @fileServerNoLogin = ('hoxa-mds-1');

# Option variable names, both common and peculiar to doBlastz:
use vars @HgAutomate::commonOptionVars;
use vars @HgStepManager::optionVars;
use vars qw/
    $opt_blastzOutRoot
    $opt_swap
    $opt_chainMinScore
    $opt_chainLinearGap
    $opt_tRepeats
    $opt_qRepeats
    $opt_readmeOnly
    $opt_ignoreSelf
    $opt_syntenicNet
    $opt_noDbNameCheck
    $opt_inclHap
    $opt_noLoadChainSplit
    $opt_loadChainSplit
	 $opt_clusterType
	 $opt_doNotRescoreSubNets
    /;
$opt_clusterType = "";

# Specify the steps supported with -continue / -stop:
my $stepper = new HgStepManager(
    [ { name => 'partition',  func => \&doPartition },
      { name => 'blastz',     func => \&doBlastzClusterRun },
      { name => 'cat',        func => \&doCatRun },
      { name => 'filterPsl',  func => \&doFilterPsl },
      { name => 'chainRun',   func => \&doChainRun },
      { name => 'chainMerge', func => \&doChainMerge },
      { name => 'patchChains', func => \&doPatchChains },
      { name => 'cleanChains', func => \&doCleanChains },
      { name => 'net',        func => \&netChains },
      { name => 'load',       func => \&loadUp },  # MH: modified to run only netClass; no chain/net loading anymore
#      { name => 'download',   func => \&doDownloads },
      { name => 'cleanup',    func => \&cleanup },
      { name => 'syntenicNet',func => \&doSyntenicNet }
    ]
			       );

# Option defaults:
my $bigClusterHub = 'genome';
my $smallClusterHub = 'genome';
my $dbHost = 'genome.pks.mpg.de';
my $workhorseGenome = 'genome';
my $fileServerGenome= 'genome';

my $bigClusterLSF = 'falcon';
my $smallClusterLSF = 'falcon';
my $workhorseLSF = 'falcon';
my $fileServerLSF = 'falcon';

my $bigClusterSlurm = 'falcon1';
my $smallClusterSlurm = 'falcon1';
my $workhorseSlurm = 'falcon1';
my $fileServerSlurm = 'falcon1';

my $defaultChainLinearGap = "loose";
my $defaultChainMinScore = "1000";	# from axtChain itself
my $defaultTRepeats = "";		# for netClass option tRepeats
my $defaultQRepeats = "";		# for netClass option qRepeats
my $defaultSeq1Limit = 30;
my $defaultSeq2Limit = 100;
my $filterPsl = 0;			# flag for filtering psls for seq identity and entropy
my $patchChains = 0;			# flag for patching chains
my $cleanChains = 0;			# flag for cleaning chains
my $workhorse = "";
my $fileServer = "";
my $chainingQueue = "long";	# queue for chaining jobs (long is default)
  
sub usage {
  # Usage / help / self-documentation:
  my ($status, $detailed) = @_;
  my $base = $0;
  $base =~ s/^(.*\/)?//;
  # Basic help (for incorrect usage):
  print STDERR "
usage: $base DEF
options:
";
  print STDERR $stepper->getOptionHelp();
print STDERR <<_EOF_
    -clusterType	  MANDATORY: Specify the clusterType as either genome or falcon or falcon1
                          NOTE: Do not use clusterTpye=genome for large genome-alignments. Run it on falcon or falcon1 or ask Michael first. 
    -blastzOutRoot dir    Directory path where outputs of the blastz cluster
                          run will be stored.  By default, they will be
                          stored in the $HgAutomate::clusterData build directory , but
                          this option can specify something more cluster-
                          friendly: $clusterNAS .
                          If dir does not already exist it will be created.
                          Blastz outputs are removed in the cleanup step.
    -swap                 DEF has already been used to create chains; swap
                          those chains (target for query), then net etc. in
                          a new directory:
                          $HgAutomate::clusterData/\$qDb/$HgAutomate::trackBuild/blastz.\$tDb.swap/
    -chainMinScore n      Add -minScore=n (default: $defaultChainMinScore) to the
                                  axtChain command.
    -chainLinearGap type  Add -linearGap=<loose|medium|filename> to the
                                  axtChain command.  (default: loose)
    -doNotRescoreSubNets  flag: if set, do not use chainNet -rescore to properly compute score of nested subnets
    -tRepeats table       Add -tRepeats=table to netClass (default: rmsk)
    -qRepeats table       Add -qRepeats=table to netClass (default: rmsk)
    -ignoreSelf           Do not assume self alignments even if tDb == qDb
    -syntenicNet          Perform optional syntenicNet step
    -noDbNameCheck        ignore Db name format
    -inclHap              include haplotypes *_hap* in chain/net, default not
    -loadChainSplit       load split chain tables, default is not split tables
_EOF_
  ;
print STDERR &HgAutomate::getCommonOptionHelp('dbHost' => $dbHost,
				      'workhorse' => $workhorse,
				      'fileServer' => 'genome',
				      'bigClusterHub' => $bigClusterHub,
				      'smallClusterHub' => $smallClusterHub);
print STDERR "
Automates UCSC's blastz/chain/net pipeline:
    1. Big cluster run of blastz.
    2. Small cluster consolidation of blastz result files.
    3. Small cluster chaining run.
    4. Sorting and netting of chains on the fileserver
       (no nets for self-alignments).
    5. Generation of liftOver-suitable chains from nets+chains on fileserver
       (not done for self-alignments).
    6. Generation of axtNet and mafNet files on the fileserver (not for self).
    7. Addition of gap/repeat info to nets on hgwdev (not for self).
    8. Loading of chain and net tables on hgwdev (no nets for self).
    9. Setup of download directory on hgwdev.
    10.Optional (-syntenicNet flag): Generation of syntenic mafNet files.
DEF is a Scott Schwartz-style bash script containing blastz parameters.
This script makes a lot of assumptions about conventional placements of
certain files, and what will be in the DEF vars.  Stick to the conventions
described in the -help output, pray to the cluster gods, and all will go
well.  :)

";
  # Detailed help (-help):
  print STDERR "
Assumptions:
1. $HgAutomate::clusterData/\$db/ is the main directory for database/assembly \$db.
   $HgAutomate::clusterData/\$tDb/$HgAutomate::trackBuild/blastz.\$qDb.\$date/ will be the directory
   created for this run, where \$tDb is the target/reference db and
   \$qDb is the query.  (Can be overridden, see #10 below.)
   $dbHost:$HgAutomate::goldenPath/\$tDb/vs\$QDb/ (or vsSelf)
   is the directory where downloadable files need to go.
   LiftOver chains (not applicable for self-alignments) go in this file:
   $HgAutomate::clusterData/\$tDb/$HgAutomate::trackBuild/liftOver/\$tDbTo\$QDb.over.chain.gz
   a copy is kept here (in case the liftOver/ copy is overwritten):
   $HgAutomate::clusterData/\$tDb/$HgAutomate::trackBuild/blastz.\$qDb.\$date/\$tDb.\$qDb.over.chain.gz
   and symbolic links to the liftOver/ file are put here:
   $dbHost:$HgAutomate::goldenPath/\$tDb/liftOver/\$tDbTo\$QDb.over.chain.gz
   $dbHost:$HgAutomate::gbdb/\$tDb/liftOver/\$tDbTo\$QDb.over.chain.gz
2. DEF's SEQ1* variables describe the target/reference assembly.
   DEF's SEQ2* variables describe the query assembly.
   If those are the same assembly, then we're doing self-alignments and
   will drop aligned blocks that cross the diagonal.
3. DEF's SEQ1_DIR is either a directory containing one nib file per
   target sequence (usually chromosome), OR a complete path to a
   single .2bit file containing all target sequences.  This directory
   should be in $clusterLocal or $clusterSortaLocal .
   SEQ2_DIR: ditto for query.
4. DEF's SEQ1_LEN is a tab-separated dump of the target database table
   chromInfo -- or at least a file that contains all sequence names
   in the first column, and corresponding sizes in the second column.
   Normally this will be $HgAutomate::clusterData/\$tDb/chrom.sizes, but for a
   scaffold-based assembly, it is a good idea to put it in $clusterSortaLocal
   or $clusterNAS
   because it will be a large file and it is read by blastz-run-ucsc
   (big cluster script).
   SEQ2_LEN: ditto for query.
5. DEF's SEQ1_CHUNK and SEQ1_LAP determine the step size and overlap size
   of chunks into which large target sequences are to be split before
   alignment.  SEQ2_CHUNK and SEQ2_LAP: ditto for query.
6. DEF's SEQ1_LIMIT and SEQ2_LIMIT decide what the maximum number of
   sequences should be for any partitioned file (the files created in the
   tParts and qParts directories).  This limit only effects SEQ1 or SEQ2
   when they are 2bit files.  Some 2bit files have too many contigs.  This
   reduces the number of blastz hippos (jobs taking forever compared to
   the other jobs).  SEQ1_LIMIT defaults to $defaultSeq1Limit and SEQ2_LIMIT defaults to $defaultSeq2Limit.
7. DEF's BLASTZ_ABRIDGE_REPEATS should be set to something nonzero if
   abridging of lineage-specific repeats is to be performed.  If so, the
   following additional constraints apply:
   a. Both target and query assemblies must be structured as one nib file
      per sequence in SEQ*_DIR (sorry, this rules out scaffold-based
      assemblies).
   b. SEQ1_SMSK must be set to a directory containing one file per target
      sequence, with the name pattern \$seq.out.spec.  This file must be
      a RepeatMasker .out file (usually filtered by DateRepeats).  The
      directory should be under $clusterLocal or $clusterSortaLocal .
      SEQ2_SMSK: ditto for query.
8. DEF's BLASTZ_[A-Z] variables will be translated into blastz command line
   options (e.g. BLASTZ_H=foo --> H=foo, BLASTZ_Q=foo --> Q=foo).
   For human-mouse evolutionary distance/sensitivity, none of these are
   necessary (blastz-run-ucsc defaults will be used).  Here's what we have
   used for human-fugu and other very-distant pairs:
BLASTZ_H=2000
BLASTZ_Y=3400
BLASTZ_L=6000
BLASTZ_K=2200
BLASTZ_Q=$HgAutomate::clusterData/blastz/HoxD55.q
   Blastz parameter tuning is somewhat of an art and is beyond the scope
   here.  Webb Miller and Jim can provide guidance on how to set these for
   a new pair of organisms.
9. DEF's PATH variable, if set, must specify a path that contains programs
   necessary for blastz to run: blastz, and if BLASTZ_ABRIDGE_REPEATS is set,
   then also fasta-subseq, strip_rpts, restore_rpts, and revcomp.
   If DEF does not contain a PATH, blastz-run-ucsc will use its own default.
10. DEF's BLASTZ variable can specify an alternate path for blastz.
11. DEF's BASE variable can specify the blastz/chain/net build directory
    (defaults to $HgAutomate::clusterData/\$tDb/$HgAutomate::trackBuild/blastz.\$qDb.\$date/).
12. SEQ?_CTGDIR specifies sequence source with the contents of full chrom
    sequences and the contig randoms and chrUn.  This keeps the contigs
    separate during the blastz and chaining so that chains won't go through
    across multiple contigs on the randoms.
13. SEQ?_CTGLEN specifies a length file to be used in conjunction with the
    special SEQ?_CTGDIR file specified above which contains the random contigs.
14. SEQ?_LIFT specifies a lift file to lift sequences in the SEQ?_CTGDIR
    to their random and chrUn positions.  This is useful for a 2bit file that
    has both full chrom sequences and the contigs for the randoms.
15. SEQ2_SELF=1 specifies the SEQ2 is already specially split for self
    alignments and to use SEQ2 sequence for self alignment, not just a
    copy of SEQ1
16. TMPDIR - specifies directory on cluster node to keep temporary files
    Typically TMPDIR=/scratch/tmp
17. All other variables in DEF will be ignored!

" if ($detailed);
  exit $status;
}


# Globals:
my %defVars = ();
my ($DEF, $tDb, $qDb, $QDb, $isSelf, $selfSplit, $buildDir,$hub,$clusterRun,$paraRun,$paraReChain,$paraNetChain);
my ($swapDir, $splitRef, $inclHap, $secondsStart, $secondsEnd);
my $clusterType = "";
my $rescoreSubNets = "";

sub isInDirList {
  # Return TRUE if $dir is under (begins with) something in dirList.
  my ($dir, @dirList) = @_;
  my $pat = '^(' . join('|', @dirList) . ')(/.*)?$';
  return ($dir =~ m@$pat@);
}

sub enforceClusterNoNo {
  # Die right away if user is trying to put cluster output somewhere
  # off-limits.
  my ($dir, $desc) = @_;
  if (&isInDirList($dir, @clusterNoNo)) {
    die "\ncluster outputs are forbidden to go to " .
      join (' or ', @clusterNoNo) . " so please choose a different " .
      "$desc instead of $dir .\n\n";
  }
  my $testFileServer = `$getFileServer $dir/`;
  if (scalar(grep /^$testFileServer$/, @fileServerNoNo)) {
    die "\ncluster outputs are forbidden to go to fileservers " .
      join (' or ', @fileServerNoNo) . " so please choose a different " .
      "$desc instead of $dir (which is hosted on $testFileServer).\n\n";
  }
}

sub checkOptions {
  # Make sure command line options are valid/supported.
  my $ok = GetOptions(@HgStepManager::optionSpec,
		      @HgAutomate::commonOptionSpec,
		      "blastzOutRoot=s",
		      "swap",
		      "chainMinScore=i",
		      "chainLinearGap=s",
		      "tRepeats=s",
		      "qRepeats=s",
		      "readmeOnly",
		      "ignoreSelf",
            "syntenicNet",
            "noDbNameCheck",
            "inclHap",
            "noLoadChainSplit",
            "loadChainSplit",
		      "clusterType=s",
		      "doNotRescoreSubNets"
		     );
  &usage(1) if (!$ok);
  &usage(0, 1) if ($opt_help);
  &HgAutomate::processCommonOptions();
  my $err = $stepper->processOptions();
  usage(1) if ($err);
  if ($opt_swap) {
    if ($opt_continue) {
      if ($stepper->stepPrecedes($opt_continue, 'net')) {
	warn "\nIf -swap is specified, then -continue must specify a step ".
	  "of \"net\" or later.\n";
	&usage(1);
      }
    } else {
      # If -swap is given but -continue is not, force -continue and tell
      # $stepper to reevaluate options:
      $opt_continue = 'chainMerge';
      $err = $stepper->processOptions();
      usage(1) if ($err);
    }
    if ($opt_stop) {
      if ($stepper->stepPrecedes($opt_stop, 'chainMerge')) {
	warn "\nIf -swap is specified, then -stop must specify a step ".
	"of \"chainMerge\" or later.\n";
	&usage(1);
      }
    }
  }
  if ($opt_blastzOutRoot) {
    if ($opt_blastzOutRoot !~ m@^/\S+/\S+@) {
      warn "\n-blastzOutRoot must specify a full path.\n";
      &usage(1);
    }
    &enforceClusterNoNo($opt_blastzOutRoot, '-blastzOutRoot');
    if (! &isInDirList($opt_blastzOutRoot, @clusterNAS)) {
      warn "\n-blastzOutRoot is intended to specify something on " .
	"$clusterNAS, but I'll trust your judgment " .
	"and use $opt_blastzOutRoot\n\n";
    }
  }
  $workhorse = $opt_workhorse if ($opt_workhorse);
  $bigClusterHub = $opt_bigClusterHub if ($opt_bigClusterHub);
  $smallClusterHub = $opt_smallClusterHub if ($opt_smallClusterHub);
}

#########################################################################
# The following routines were taken almost verbatim from blastz-run-ucsc,
# so may be good candidates for libification!  unless that would slow down
# blastz-run-ucsc...
# nfsNoodge() was removed from loadDef() and loadSeqSizes() -- since this
# script will not be run on the cluster, we should fully expect files to
# be immediately visible.

sub loadDef {
  # Read parameters from a bash script with Scott's param variable names:
  my ($def) = @_;
  my $fh = &HgAutomate::mustOpen("$def");
  while (<$fh>) {
    s/^\s*export\s+//;
    next if (/^\s*#/ || /^\s*$/);
    if (/(\w+)\s*=\s*(.*)/) {
      my ($var, $val) = ($1, $2);
      while ($val =~ /\$(\w+)/) {
	my $subst = $defVars{$1};
	if (defined $subst) {
	  $val =~ s/\$$1/$subst/;
	} else {
	  die "Can't find value to substitute for \$$1 in $DEF var $var.\n";
	}
      }
      $defVars{$var} = $val;
    }
  }
  close($fh);
  
  # test if TMPDIR in DEF exists; if not create it
  if (exists $defVars{TMPDIR} && ! -e $defVars{TMPDIR}) {
  		print STDERR "create $defVars{TMPDIR}\n";
		system "mkdir -p $defVars{TMPDIR}";
  }
  
}

sub loadSeqSizes {
  # Load up sequence -> size mapping from $sizeFile into $hashRef.
  my ($sizeFile, $hashRef) = @_;
  my $fh = &HgAutomate::mustOpen("$sizeFile");
  while (<$fh>) {
    chomp;
    my ($seq, $size) = split;
    $hashRef->{$seq} = $size;
  }
  close($fh);
}

# end shared stuff from blastz-run-ucsc
#########################################################################

sub requireVar {
  my ($var) = @_;
  die "Error: $DEF is missing variable $var\n" if (! defined $defVars{$var});
}

sub requirePath {
  my ($var) = @_;
  my $val = $defVars{$var};
  die "Error: $DEF $var=$val must specify a complete path\n"
    if ($val !~ m@^/\S+/\S+@);
  if ( -d $val ) {
    my $fileCount = `find $val -maxdepth 1 -type f | wc -l`;
    chomp $fileCount;
    if ($fileCount < 1) {
	die "Error: $DEF variable: $var=$val specifies an empty directory.\n";
    }
  } elsif ( ! -s $val ) {
    die "Error: $DEF variable: $var=$val is not a file or directory.\n";
  }
}

sub requireNum {
  my ($var) = @_;
  my $val = $defVars{$var};
  die "Error: $DEF variable $var=$val must specify a number.\n"
    if ($val !~ /^\d+$/);
}

my $oldDbFormat = '[a-z][a-z](\d+)?';
my $newDbFormat = '[a-z][a-z][a-z][A-Z][a-z][a-z0-9](\d+)?';
my $newnewDbFormat = '[a-z][a-z][a-z][A-Z][a-z][a-z][A-Z][a-z][a-z0-9](\d+)?';
my $HLDbFormat = 'HL[a-z][a-z][a-z][A-Z][a-z][a-z0-9](\d+)?';
sub getDbFromPath {
  # Require that $val is a full path that contains a recognizable db as
  # one of its elements (possibly the last one).
  my ($var) = @_;
  my $val = $defVars{$var};
  my $db;
  if ($opt_noDbNameCheck ||
	$val =~ m@^/\S+/($oldDbFormat|$newDbFormat|$newnewDbFormat|$HLDbFormat)((\.2bit)|(/(\S+)?))?$@) {
    $db = $1;
  } else {
    die "Error: $DEF variable $var=$val must be a full path with " .
      "a recognizable database as one of its elements.\n"
  }
  if (! defined($db)) {
    if ($val =~ m#^/hive/data/genomes/#) {
	$val =~ s#^/hive/data/genomes/##;
	$val =~ s#/.*##;
	$db = $val;
	warn "Warning: assuming database $db from /hive/data/genomes/<db>/ path\n";
    } elsif ($val =~ m#^/scratch/data/#) {
	$val =~ s#^/scratch/data/##;
	$val =~ s#/.*##;
	$db = $val;
	warn "Warning: assuming database $db from /scratch/data/<db>/ path\n";
    }
  }
return $db;
}

sub checkDef {
  # Make sure %defVars contains what we need and looks consistent with
  # our assumptions.
  foreach my $s ('SEQ1_', 'SEQ2_') {
    foreach my $req ('DIR', 'LEN', 'CHUNK', 'LAP') {
      &requireVar("$s$req");
    }
    &requirePath($s . 'DIR');
    &requirePath($s . 'LEN');
    &requireNum($s . 'CHUNK');
    &requireNum($s . 'LAP');
  }
  $tDb = &getDbFromPath('SEQ1_DIR');
  $qDb = &getDbFromPath('SEQ2_DIR');
  $isSelf = $opt_ignoreSelf ? 0 : ($tDb eq $qDb);
  # special split on SEQ2 for Self alignments
  $selfSplit = $defVars{'SEQ2_SELF'} || 0;
  $QDb = $isSelf ? 'Self' : ucfirst($qDb);
  if ($isSelf && $opt_swap) {
    die "-swap is not supported for self-alignments\n" .
        "($DEF has $tDb as both target and query).\n";
  }
  HgAutomate::verbose(1, "$DEF looks OK!\n" .
	  "\ttDb=$tDb\n\tqDb=$qDb\n\ts1d=$defVars{SEQ1_DIR}\n" .
	  "\tisSelf=$isSelf\n");
  if ($defVars{'SEQ1_SMSK'} || $defVars{'SEQ2_SMSK'} ||
      $defVars{'BLASTZ_ABRIDGE_REPEATS'}) {
    &requireVar('BLASTZ_ABRIDGE_REPEATS');
    foreach my $s ('SEQ1_', 'SEQ2_') {
      my $var = $s. 'SMSK';
      &requireVar($var);
      &requirePath($var);
    }
    HgAutomate::verbose(1, "Abridging repeats!\n");
  }
}


sub doPartition {
  # Partition the sequence up before blastz.
  my $runDir = "$buildDir/run.blastz";
  my $targetList = "$tDb.lst";
  my $queryList = $isSelf ? $targetList :
	($opt_ignoreSelf ? "$qDb.ignoreSelf.lst" : "$qDb.lst");
  if ($selfSplit) {
    $queryList = "$qDb.selfSplit.lst"
  }
  my $tPartDir = '-lstDir tParts';
  my $qPartDir = '-lstDir qParts';
  my $outRoot = $opt_blastzOutRoot ? "$opt_blastzOutRoot/psl" : '../psl';

  my $seq1Dir = $defVars{'SEQ1_CTGDIR'} || $defVars{'SEQ1_DIR'};
  my $seq2Dir = $defVars{'SEQ2_CTGDIR'} || $defVars{'SEQ2_DIR'};
  my $seq1Len = $defVars{'SEQ1_CTGLEN'} || $defVars{'SEQ1_LEN'};
  my $seq2Len = $defVars{'SEQ2_CTGLEN'} || $defVars{'SEQ2_LEN'};
  my $seq1Limit = (defined $defVars{'SEQ1_LIMIT'}) ? $defVars{'SEQ1_LIMIT'} :
    $defaultSeq1Limit;
  my $seq2Limit = (defined $defVars{'SEQ2_LIMIT'}) ? $defVars{'SEQ2_LIMIT'} :
    $defaultSeq2Limit;
  my $seq2MaxLength = `awk '{print \$2}' $seq2Len | sort -rn | head -1`;
  chomp $seq2MaxLength;
  my $bundleParts = 0;
  # OK to bundle parts list bits into 2bit files when not abridging
  $bundleParts = 1 if ( ! $defVars{'BLASTZ_ABRIDGE_REPEATS'} );

  my $partitionTargetCmd =
    ("$partition $defVars{SEQ1_CHUNK} $defVars{SEQ1_LAP} " .
     "$seq1Dir $seq1Len -xdir xdir.sh -rawDir $outRoot $seq1Limit " .
     "$tPartDir > $targetList");
  my $partitionQueryCmd =
    (($isSelf && (! $selfSplit)) ?
     '# Self-alignment ==> use target partition for both.' :
     "$partition $defVars{SEQ2_CHUNK} $defVars{SEQ2_LAP} " .
     "$seq2Dir $seq2Len $seq2Limit " .
     "$qPartDir > $queryList");

  &HgAutomate::mustMkdir($runDir);
  my $whatItDoes =
"It computes partitions of target and query sequences into chunks of the
specified size for the blastz cluster run.  The actual splitting of
sequence is not performed here, but later on by blastz cluster jobs.";
  my $bossScript = newBash HgRemoteScript("$runDir/doPartition.bash", $hub,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
$partitionTargetCmd
export L1=`wc -l < $targetList`
$partitionQueryCmd
export L2=`wc -l < $queryList`
export L=`echo \$L1 \$L2 | awk '{print \$1*\$2}'`
echo "cluster batch jobList size: \$L = \$L1 * \$L2"
_EOF_
    );
  if ($bundleParts) {
  $bossScript->add(<<_EOF_
if [ -d tParts ]; then
  echo 'constructing tParts/*.2bit files'
  ls tParts/*.lst | sed -e 's#tParts/##; s#.lst##;' | while read tPart
  do
    sed -e 's#.*.2bit:##;' tParts/\$tPart.lst \\
      | twoBitToFa -seqList=stdin $seq1Dir stdout \\
        | faToTwoBit stdin tParts/\$tPart.2bit
  done
fi
if [ -d qParts ]; then
  echo 'constructing qParts/*.2bit files'
  ls qParts/*.lst | sed -e 's#qParts/##; s#.lst##;' | while read qPart
  do
    sed -e 's#.*.2bit:##;' qParts/\$qPart.lst \\
      | twoBitToFa -seqList=stdin $seq2Dir stdout \\
        | faToTwoBit stdin qParts/\$qPart.2bit
  done
fi
_EOF_
    );
  }
  $bossScript->execute();

  my $noJobsT = `wc -l < $runDir/$targetList`;
  my $noJobsQ = `wc -l < $runDir/$queryList`;
  my $noJobs = $noJobsT * $noJobsQ;

  print ( "*** The number of jobs should not exceed 10,000" ); 
  if( $clusterType eq "falcon1" || $clusterType eq "falcon") { 
      print( ", and a runtime over 10 minutes" );
  }
  print( " ***\n" ); 
  if( $noJobs > 4000 ) {
      print( "Stopped $0. You have $noJobs right now. To achieve a good number of jobs in the range of 3000, you can adapt your DEF file. Run 'rm -rf run.blastz psl' before restarting the alignment.\n" );
      exit( 0 ); 
  } else {
      print( "Ok, continue with jobs.\n" ); 
  }

  my $mkOutRootHost = $opt_blastzOutRoot ? $hub : $fileServer;
  my $mkOutRoot =     $opt_blastzOutRoot ? "mkdir -p $opt_blastzOutRoot;" : "";
  &HgAutomate::run("$HgAutomate::runSSH $mkOutRootHost " .
		   "'(cd $runDir; $mkOutRoot csh -ef xdir.sh)'");
}

sub doBlastzClusterRun {
  # Set up and perform the big-cluster blastz run.
  my $runDir = "$buildDir/run.blastz";
  my $targetList = "$tDb.lst";
  my $outRoot = $opt_blastzOutRoot ? "$opt_blastzOutRoot/psl" : '../psl';
  my $queryList = $isSelf ? $targetList :
	($opt_ignoreSelf ? "$qDb.ignoreSelf.lst" : "$qDb.lst");
  if ($selfSplit) {
    $queryList = "$qDb.selfSplit.lst"
  }
  # First, make sure we're starting clean.
  if (-e "$runDir/run.time") {
    die "doBlastzClusterRun: looks like this was run successfully already " .
      "(run.time exists).  Either run with -continue cat or some later " .
	"stage, or move aside/remove $runDir/ and run again.\n";
  } elsif ((-e "$runDir/gsub" || -e "$runDir/jobList") && ! $opt_debug) {
    die "doBlastzClusterRun: looks like we are not starting with a clean " .
      "state.  Please move aside or remove $runDir/ and run again.\n";
  }
  # Second, make sure we got through the partitioning already
  if (! -e "$runDir/$targetList" && ! $opt_debug) {
    die "doBlastzClusterRun: there's no target list file " .
        "so start over without the -continue align.\n";
  }
  if (! -e "$runDir/$queryList" && ! $opt_debug) {
    die "doBlastzClusterRun: there's no query list file" .
        "so start over without the -continue align.\n";
  }
my $checkOutExists ="$outRoot" . '/$(file1)/$(file1)_$(file2).psl';
if($clusterType eq "genome"){
	$checkOutExists = "{check out exists "."$outRoot" . '/$(file1)/$(file1)_$(file2).psl }';
}
 
  my $templateCmd = ("$blastzRunUcsc -outFormat psl " .
		     ($isSelf ? '-dropSelf ' : '') .
		     '$(path1) $(path2) ../DEF ' .
		     $checkOutExists);
  &HgAutomate::makeGsub($runDir, $templateCmd);

  `touch "$runDir/para_hub_$hub"`;
 
 #customize the $myparaRun variable depending upon the clusterType: 
  my $myParaRun = $HgAutomate::paraRun;
  if ($clusterType eq "falcon1" || $clusterType eq "falcon") {
	$myParaRun = "
para make blastz_$tDb$qDb jobList -q medium\n
para check blastz_$tDb$qDb\n
para time blastz_$tDb$qDb > run.time\n
cat run.time\n";
}

  my $whatItDoes = "It sets up and performs the big cluster blastz run.";
  
  my $bossScript = new HgRemoteScript("$runDir/doClusterRun.csh", $hub,
				      $runDir, $whatItDoes, $DEF);
## Never indent the content of the add() function!
  $bossScript->add(<<_EOF_
$HgAutomate::gensub2 $targetList $queryList gsub jobList
$myParaRun
_EOF_
    );
  $bossScript->execute();
}	#	sub doBlastzClusterRun {}

sub doCatRun {
  # Do a small cluster run to concatenate the lowest level of chunk result
  # files from the big cluster blastz run.  This brings results up to the
  # next level: per-target-chunk results, which may still need to be
  # concatenated into per-target-sequence in the next step after this one --
  # chaining.
  my $runDir = "$buildDir/run.cat";
  # First, make sure we're starting clean.
  if (-e "$runDir/run.time") {
    die "doCatRun: looks like this was run successfully already " .
      "(run.time exists).  Either run with -continue chainRun or some later " .
	"stage, or move aside/remove $runDir/ and run again.\n";
  } elsif ((-e "$runDir/gsub" || -e "$runDir/jobList") && ! $opt_debug) {
    die "doCatRun: looks like we are not starting with a clean " .
      "state.  Please move aside or remove $runDir/ and run again.\n";
  }
  # Make sure previous stage was successful.
  my $successFile = "$buildDir/run.blastz/run.time";
  if (! -e $successFile && ! $opt_debug) {
    die "doCatRun: looks like previous stage was not successful (can't find " .
      "$successFile).\n";
  }
  
  my $checkOutExists =" ../pslParts/\$(file1).psl.gz";
if($clusterType eq "genome"){
	$checkOutExists = "{check out exists ../pslParts/\$(file1).psl.gz } ";
}
  &HgAutomate::mustMkdir($runDir);
  &HgAutomate::makeGsub($runDir,
      "./cat.csh \$(path1) $checkOutExists");
 
 `touch "$runDir/para_hub_$hub"`;

  my $outRoot = $opt_blastzOutRoot ? "$opt_blastzOutRoot/psl" : '../psl';

  my $fh = &HgAutomate::mustOpen(">$runDir/cat.csh");
  print $fh <<_EOF_
#!/bin/csh -ef
find $outRoot/\$1/ -name "*.psl" | xargs cat | grep "^#" -v | gzip -c > \$2
_EOF_
  ;
  close($fh); 

#customize the $myparaRun variable depending upon the clusterType: 
# Now the cat is executed as a single job in the medium queue (can take ~30 min total)
my $myParaRun = $HgAutomate::paraRun;
  if ($clusterType eq "falcon1" || $clusterType eq "falcon") {
	$myParaRun = "
echo \"jobList\" > jobListCombinedClusterJob\n
chmod +x jobList\n
para make catRun_$tDb$qDb jobListCombinedClusterJob -q medium\n
para check catRun_$tDb$qDb\n
para time catRun_$tDb$qDb > run.time\n
cat run.time\n";
}
  my $whatItDoes =
"It sets up and performs a small cluster run to concatenate all files in
each subdirectory of $outRoot into a per-target-chunk file.";
  my $bossScript = new HgRemoteScript("$runDir/doCatRun.csh", $hub,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
(cd $outRoot; find . -maxdepth 1 -type d | grep '^./') \\
        | sed -e 's#/\$##; s#^./##' > tParts.lst
chmod a+x cat.csh
$HgAutomate::gensub2 tParts.lst single gsub jobList
mkdir ../pslParts
$myParaRun
_EOF_
    );
  $bossScript->execute();
}	#	sub doCatRun {}


#######################################################################
# added by Michael Hiller
#######################################################################
sub doFilterPsl {
  if ($filterPsl == 0) {
  	print "skip filtering psls\n";
	return;
  }

  # Do a cluster to filter each psl file for seq identity and entropy.
  # The seq identity and entropy parameters are read from the DEF file. 
  my $runDir = "$buildDir/run.filterPsl";
  # First, make sure we're starting clean.
  if (-e "$runDir/run.time") {
    die "doFilterPsl: looks like this was run successfully already " .
      "(run.time exists).  Either run with -continue chainRun or some later " .
	"stage, or move aside/remove $runDir/ and run again.\n";
  } elsif ((-e "$runDir/gsub" || -e "$runDir/jobList") && ! $opt_debug) {
    die "doFilterPsl: looks like we are not starting with a clean " .
      "state.  Please move aside or remove $runDir/ and run again.\n";
  }
  # Make sure previous stage was successful.
  my $successFile = "$buildDir/run.blastz/run.time";
  if (! -e $successFile && ! $opt_debug) {
    die "doFilterPsl: looks like previous stage was not successful (can't find " .
      "$successFile).\n";
  }
  
  my $checkOutExists = " splitPSL/\$(file1).filtered";
if($clusterType eq "genome"){
$checkOutExists = "{check out exists" . " splitPSL/\$(file1).filtered}";
}
  &HgAutomate::mustMkdir($runDir);
  &HgAutomate::makeGsub($runDir, "./filterPsl.csh splitPSL/\$(file1) $checkOutExists");
  
  `touch "$runDir/para_hub_$hub"`;

  my $fh = &HgAutomate::mustOpen(">$runDir/filterPsl.csh");
  print $fh <<_EOF_
#!/bin/csh -ef
filterPslIdentityEntropy.py \$1 $defVars{'SEQ_IDENT'} $defVars{'MIN_ENTROPY'} $defVars{'WIN_SIZE'} $defVars{'SEQ1_DIR'} $defVars{'SEQ2_DIR'} \$2
_EOF_
  ;
  close($fh);
#customize the $myparaRun variable depending upon the clusterType: 
my $myParaRun = $HgAutomate::paraRun;
  if ($clusterType eq "falcon1" || $clusterType eq "falcon") {
	$myParaRun = "
para make filterPsl_$tDb$qDb jobList -q medium\n
para check filterPsl_$tDb$qDb\n
para time filterPsl_$tDb$qDb > run.time\n
cat run.time\n";
}
  
  my $whatItDoes =
"It sets up and performs a cluster run to filter all psl files in $buildDir/pslParts and outputs to $buildDir/pslPartsFiltered.";
  my $bossScript = new HgRemoteScript("$runDir/doFilterPsl.csh", $hub,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
# split all psl files
rm -rf splitPSL
mkdir splitPSL
(find $buildDir/pslParts/ -maxdepth 1 -type f -name "*.psl.gz" -printf "%f\\n" | sed 's/.psl.gz//g' ) | xargs -i sh -c "echo split file {}.psl.gz; pslPartition $buildDir/pslParts/{}.psl.gz splitPSL -namePrefix={}.PART -outLevels=0 -partSize=100000; echo DONE"

# get a list of split files
(find splitPSL -maxdepth 1 -type f -name "*.psl") > tParts.lst

# generate joblist
$HgAutomate::gensub2 tParts.lst single gsub jobList

# cluster run
chmod a+x filterPsl.csh
$myParaRun

# combine split output
mkdir ../pslPartsFiltered
find splitPSL -maxdepth 1 -type f -name "*.psl.filtered" -exec cat {} \\; | gzip > ../pslPartsFiltered/finalFiltered.psl.gz
_EOF_
    );
  $bossScript->execute();
}	#	sub doFilterPsl {}
######################################################################
######################################################################

############################################
# added by Michael Hiller
# ##########################################
sub bundlePslForChaining {
     my ($inputDir, $outputDir, $outputFileList, $maxBases, $gzipped) = @_; 
 
     print "bundlePslForChaining: $inputDir, $outputDir, $outputFileList, maxBases=$maxBases, gzipped=$gzipped\n";
     my $gzip = "";
     $gzip = ".gz" if ($gzipped == 1);

     # get filelist
     my $fileList = `find $inputDir -name "*psl$gzip" -printf "%p "`; 
     chomp($fileList);
     print "fileList: $fileList\n";

     # we need 2 tmpDirs. One for pslSortAcc internally. One for the chrom-split files
     my $splitPSL = `mktemp -d`; chomp($splitPSL);
     my $tmpDir = `mktemp -d`; chomp($tmpDir);
     my $call = "pslSortAcc nohead $splitPSL $tmpDir $fileList";
     print "$call\n";
     system("$call") == 0 || die("ERROR: $call failed\n");
     `rm -rf $tmpDir`;

     # bundle
     $call = "bundleChromSplitPslFiles.perl $splitPSL $defVars{'SEQ1_LEN'} $outputDir -maxBases $maxBases";
     print "$call\n";
     system("$call") == 0 || die("ERROR: $call failed\n");
     `rm -rf $splitPSL`;

     # get output filelist
     $call = "(cd $outputDir; find . -name \"*.psl\" -printf \"%f\\n\" > $outputFileList )";
     print "$call\n";
     system("$call") == 0 || die("ERROR: $call failed\n");
     my $numFiles = `cat $outputFileList | wc -l`; chomp($numFiles);
     print "outputFileList $outputFileList created with $numFiles files\n";
}
 

sub makePslPartsLst {
  return if ($opt_debug);

  if ($filterPsl == 1) {
     bundlePslForChaining("$buildDir/pslPartsFiltered", "$buildDir/axtChain/run/splitPSL", "$buildDir/axtChain/run/pslParts.lst", 50000000, 1);
  } else {
     bundlePslForChaining("$buildDir/pslParts", "$buildDir/axtChain/run/splitPSL", "$buildDir/axtChain/run/pslParts.lst", 50000000, 1);
  }
}


=pod
sub makePslPartsLst {
  # Create a pslParts.lst file the subdirectories of pslParts; if some
  # are for subsequences of the same sequence, make a single .lst line
  # for the sequence (single chaining job with subseqs' alignments
  # catted together).  Otherwise (i.e. subdirs that contain small
  # target seqs glommed together by partitionSequences) make one .lst
  # line per partition.
  return if ($opt_debug);
  # if we have filtered the psl files, then read from pslPartsFiltered
  if ($filterPsl == 1) {
  	opendir(P, "$buildDir/pslPartsFiltered")
	    || die "Couldn't open directory $buildDir/pslPartsFiltered for reading: $!\n";
  } else {
  	opendir(P, "$buildDir/pslParts")
	    || die "Couldn't open directory $buildDir/pslParts for reading: $!\n";
  }
  my @parts = readdir(P);
  closedir(P);
  my $partsLst = "$buildDir/axtChain/run/pslParts.lst";
  my $fh = &HgAutomate::mustOpen(">$partsLst");
  my %seqs = ();
  my $count = 0;

  foreach my $p (@parts) {
    $p =~ s@^/.*/@@;  $p =~ s@/$@@;
    $p =~ s/\.psl\.gz//;
    next if ($p eq '.' || $p eq '..');
    if ($p =~ m@^(\S+:\S+):\d+-\d+$@) {
      # Collapse subsequences (subranges of a sequence) down to one entry
      # per sequence:
      $seqs{$1} = 1;
    } else {
      print $fh "$p\n";
      $count++;
    }
  }
  foreach my $p (keys %seqs) {
    print $fh "$p:\n";
    $count++;
  }
  close($fh);
  if ($count < 1) {
    die "makePslPartsLst: didn't find any pslParts/ items." if ($filterPsl == 0);
    die "makePslPartsLst: didn't find any pslPartsFiltered/ items." if ($filterPsl == 1);
  }
}
=cut

sub doChainRun {
  # Do a small cluster run to chain alignments to each target sequence.
  my $runDir = "$buildDir/axtChain/run";
  # First, make sure we're starting clean.
  if (-e "$runDir/run.time") {
    die "doChainRun: looks like this was run successfully already " .
      "(run.time exists).  Either run with -continue chainMerge or some " .
	"later stage, or move aside/remove $runDir/ and run again.\n";
  } elsif ((-e "$runDir/gsub" || -e "$runDir/jobList") && ! $opt_debug) {
    die "doChainRun: looks like we are not starting with a clean " .
      "state.  Please move aside or remove $runDir/ and run again.\n";
  }
  # Make sure previous stage was successful.
  my $successFile = "$buildDir/run.cat/run.time";
  $successFile = "$buildDir/run.filterPsl/run.time" if ($filterPsl == 1);
  if (! -e $successFile && ! $opt_debug) {
    die "doChainRun: looks like previous stage was not successful (can't " .
      "find $successFile).\n";
  }
  &HgAutomate::mustMkdir($runDir);
 
  my $checkOutExists = " chain/\$(file1).chain";
if($clusterType eq "genome"){
	$checkOutExists = " {check out line+ chain/\$(file1).chain}";
}

  &HgAutomate::makeGsub($runDir,
	       "./chain.csh \$(file1) $checkOutExists");
  `touch "$runDir/para_hub_$hub"`;

  my $seq1Dir = $defVars{'SEQ1_CTGDIR'} || $defVars{'SEQ1_DIR'};
  my $seq2Dir = $defVars{'SEQ2_CTGDIR'} || $defVars{'SEQ2_DIR'};
  my $matrix = $defVars{'BLASTZ_Q'} ? "-scoreScheme=$defVars{BLASTZ_Q} " : "";
  my $minScore = $opt_chainMinScore ? "-minScore=$opt_chainMinScore" : "";
  my $linearGap = $opt_chainLinearGap ? "-linearGap=$opt_chainLinearGap" :
	"-linearGap=$defaultChainLinearGap";

  my $fh = &HgAutomate::mustOpen(">$runDir/chain.csh");
  print $fh  <<_EOF_
#!/bin/csh -ef
cat $buildDir/axtChain/run/splitPSL/\$1 \\
| axtChain -psl -verbose=0 $matrix $minScore $linearGap stdin \\
    $seq1Dir \\
    $seq2Dir \\
    stdout \\
| chainAntiRepeat $seq1Dir \\
    $seq2Dir \\
    stdin \$2
_EOF_
    ;
  if (exists($defVars{'SEQ1_LIFT'})) {
  print $fh <<_EOF_
set c=\$2:t:r
echo "lifting \$2 to \${c}.lifted.chain"
liftUp liftedChain/\${c}.lifted.chain \\
    $defVars{'SEQ1_LIFT'} carry \$2
rm \$2
mv liftedChain/\${c}.lifted.chain \$2
_EOF_
    ;
  }
  if (exists($defVars{'SEQ2_LIFT'})) {
  print $fh <<_EOF_
set c=\$2:t:r
echo "lifting \$2 to \${c}.lifted.chain"
liftUp -chainQ liftedChain/\${c}.lifted.chain \\
    $defVars{'SEQ2_LIFT'} carry \$2
rm \$2
mv liftedChain/\${c}.lifted.chain \$2
_EOF_
    ;
  }
  close($fh);

  # this splits the psls into chroms and bundles them and produces pslParts.lst 
  &makePslPartsLst();
#customize the $myparaRun variable depending upon the clusterType: 
my $myParaRun = $HgAutomate::paraRun;
  if ($clusterType eq "falcon") {
	$myParaRun = "
para make chainRun_$tDb$qDb jobList -q $chainingQueue\n
para check chainRun_$tDb$qDb\n
para time chainRun_$tDb$qDb > run.time\n
cat run.time\n";
}elsif ($clusterType eq "falcon1") {
# request 15 GB of mem for the chaining jobs. Some take more than 5 GB apparently
	$myParaRun = "
para make chainRun_$tDb$qDb jobList -q $chainingQueue -memoryMb 15000\n
para check chainRun_$tDb$qDb\n
para time chainRun_$tDb$qDb > run.time\n
cat run.time\n";
}


  my $whatItDoes =
"It sets up and performs a small cluster run to chain all alignments
to each target sequence.";
  my $bossScript = new HgRemoteScript("$runDir/doChainRun.csh", $hub,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
chmod a+x chain.csh
$HgAutomate::gensub2 pslParts.lst single gsub jobList
mkdir chain liftedChain
$myParaRun
rmdir liftedChain
_EOF_
  );
  $bossScript->execute();
}	#	sub doChainRun {}


sub postProcessChains {
  # chainMergeSort etc.
  my $runDir = "$buildDir/axtChain";
  my $chain = "$tDb.$qDb.all.chain.gz";
  # First, make sure we're starting clean.
  if (-e "$runDir/$chain") {
    die "postProcessChains: looks like this was run successfully already " .
      "($chain exists).  Either run with -continue net or some later " .
      "stage, or move aside/remove $runDir/$chain and run again.\n";
  } elsif (-e "$runDir/all.chain" || -e "$runDir/all.chain.gz") {
    die "postProcessChains: looks like this was run successfully already " .
      "(all.chain[.gz] exists).  Either run with -continue net or some later " .
      "stage, or move aside/remove $runDir/all.chain[.gz] and run again.\n";
  } elsif (-e "$runDir/chain" && ! $opt_debug) {
    die "postProcessChains: looks like we are not starting with a clean " .
      "state.  Please move aside or remove $runDir/chain and run again.\n";
  }
  # Make sure previous stage was successful.
  my $successFile = "$buildDir/axtChain/run/run.time";
  if (! -e $successFile && ! $opt_debug) {
    die "postProcessChains: looks like previous stage was not successful " .
      "(can't find $successFile).\n";
  }
 
  my $cmd="$HgAutomate::runSSH $workhorse ";
  $cmd .= "'cd $buildDir; nice find $runDir/run/chain -name \"*.chain\" ";
  $cmd .= "| chainMergeSort -inputList=stdin ";
  $cmd .= "| nice gzip -c > $runDir/$chain'";
  &HgAutomate::run($cmd);
  if ($splitRef) {
    &HgAutomate::run("$HgAutomate::runSSH $fileServer nice " .
	 "chainSplit $runDir/chain $runDir/$chain");
  }
  &HgAutomate::nfsNoodge("$runDir/$chain");
}	#	sub postProcessChains {}


sub getAllChain {
  # Find the most likely candidate for all.chain from a previous run/step.
  my ($runDir) = @_;
  my $chain;
  if (-e "$runDir/$tDb.$qDb.all.chain.gz") {
    $chain = "$tDb.$qDb.all.chain.gz";
  } elsif (-e "$runDir/$tDb.$qDb.all.chain") {
    $chain = "$tDb.$qDb.all.chain";
  } elsif (-e "$runDir/all.chain.gz") {
    $chain = "all.chain.gz";
  } elsif (-e "$runDir/all.chain") {
    $chain = "all.chain";
  } elsif ($opt_debug) {
    $chain = "$tDb.$qDb.all.chain.gz";
  }
  return $chain;
}


sub swapChains {
  # chainMerge step for -swap: chainSwap | chainSort.
  my $runDir = "$swapDir/axtChain";
  my $inChain = &getAllChain("$buildDir/axtChain");
  my $swappedChain = "$qDb.$tDb.all.chain.gz";
  # First, make sure we're starting clean.
  if (-e "$runDir/$swappedChain") {
    die "swapChains: looks like this was run successfully already " .
     "($runDir/$swappedChain exists).  Either run with -continue net or some " .
     "later stage, or move aside/remove $runDir/$swappedChain and run again.\n";
  } elsif (-e "$runDir/all.chain" || -e "$runDir/all.chain.gz") {
    die "swapChains: looks like this was run successfully already " .
     "($runDir/all.chain[.gz] exists).  Either run with -continue net or some " .
     "later stage, or move aside/remove $runDir/all.chain[.gz] and run again.\n";
  }
  # Main routine already made sure that $buildDir/axtChain/all.chain is there.
 
  &HgAutomate::run("$HgAutomate::runSSH $workhorse nice " .
       "'chainSwap $buildDir/axtChain/$inChain stdout " .
       "| nice chainSort stdin stdout " .
       "| nice gzip -c > $runDir/$swappedChain'");
  &HgAutomate::nfsNoodge("$runDir/$swappedChain");
  if ($splitRef) {
    &HgAutomate::run("$HgAutomate::runSSH $fileServer nice " .
	 "chainSplit $runDir/chain $runDir/$swappedChain");
  }
}	#	sub swapChains {}


sub swapGlobals {
  # Swap our global variables ($buildDir, $tDb, $qDb and %defVars SEQ1/SEQ2)
  # so that the remaining steps need no tweaks for -swap.
  $buildDir = $swapDir;
  my $tmp = $qDb;
  $qDb = $tDb;
  $tDb = $tmp;
  $QDb = $isSelf ? 'Self' : ucfirst($qDb);
  foreach my $var ('DIR', 'LEN', 'CHUNK', 'LAP', 'SMSK') {
    $tmp = $defVars{"SEQ1_$var"};
    $defVars{"SEQ1_$var"} = $defVars{"SEQ2_$var"};
    $defVars{"SEQ2_$var"} = $tmp;
  }
  $defVars{'BASE'} = $swapDir;
}


sub doChainMerge {
  # If -swap, swap chains from other org;  otherwise, merge the results
  # from the chainRun step.
  if ($opt_swap) {
    &swapChains();
    &swapGlobals();
  } else {
    &postProcessChains();
  }
}



#######################################################################
# added by Michael Hiller
#######################################################################
sub doPatchChains {
  if ($patchChains == 0) {
  	print "skip patchingChains\n";
	return;
  }

  # Do a cluster run to patch the all.chains and filter the new local alignments
  # Then rebuild the patchedChains from the all and newly-added psl entries.
  my $runDir = "$buildDir/run.patchChain";

  # First, make sure we're starting clean.
  if (-e "$runDir/run.time") {
    die "doPatchChain: looks like this was run successfully already " .
      "(run.time exists).  Either run with -continue net or some later " .
	"stage, or move aside/remove $runDir/ and run again.\n";
  } elsif ((-e "$runDir/gsub" || -e "$runDir/jobList") && ! $opt_debug) {
    die "doPatchChain: looks like we are not starting with a clean " .
      "state.  Please move aside or remove $runDir/ and run again.\n";
  }
  # Make sure previous stage was successful.
  my $chain = &getAllChain("$buildDir/axtChain");
  if (! defined $chain && ! $opt_debug) {
    die "patchChain: looks like previous stage was not successful " .
      "(can't find [$tDb.$qDb.]all.chain[.gz]).\n";
  }

  &HgAutomate::mustMkdir($runDir);
  `touch "$runDir/para_hub_$hub"`;

  # filtering parameters to be added if the flag FILTERPSL=1
  my $filterParameters = ""; 
  $filterParameters = "-minEntropy $defVars{'MIN_ENTROPY'} -windowSize $defVars{'WIN_SIZE'} -minIdentity $defVars{'SEQ_IDENT'}" if ($filterPsl == 1);
  my $filterFlag = "";
  $filterFlag = ".filtered" if ($filterPsl == 1);
  
  # output dir
  my $outputDir = "../pslPartsPatched";
  $outputDir = "../pslPartsPatchedFiltered" if ($filterPsl == 1);
  my $absOutputDir = "$buildDir/pslPartsPatched"; 
  $absOutputDir = "$buildDir/pslPartsPatchedFiltered" if ($filterPsl == 1);
 
  # dir where the pslParts are 
  my $pslPartsDir = "../pslParts"; 
  $pslPartsDir = "../pslPartsFiltered" if ($filterPsl == 1);

  if (-e "$absOutputDir") {
    die "doPatchChain: looks like this was run successfully already ($absOutputDir exists). Either run with -continue net or some later stage, or move aside/remove $absOutputDir and run again.\n";
  }

  my $cat = "cat";
  $cat = "zcat" if ($chain =~ /\.gz$/);	

  # create the script that chains the new and old psls
  my $seq1Dir = $defVars{'SEQ1_CTGDIR'} || $defVars{'SEQ1_DIR'};
  my $seq2Dir = $defVars{'SEQ2_CTGDIR'} || $defVars{'SEQ2_DIR'};
  my $matrix = $defVars{'BLASTZ_Q'} ? "-scoreScheme=$defVars{BLASTZ_Q} " : "";
  my $minScore = $opt_chainMinScore ? "-minScore=$opt_chainMinScore" : "";
  my $linearGap = $opt_chainLinearGap ? "-linearGap=$opt_chainLinearGap" : "-linearGap=$defaultChainLinearGap";
  my $fh = &HgAutomate::mustOpen(">$runDir/rechain.csh");
  print $fh  <<_EOF_
#!/bin/csh -ef
axtChain -psl -verbose=0 $matrix $minScore $linearGap \$1 \\
    $seq1Dir \\
    $seq2Dir \\
    stdout \\
| chainAntiRepeat $seq1Dir \\
    $seq2Dir \\
    stdin \$2
_EOF_
    ;
  close($fh);
#customize the $myparaRun variable depending upon the clusterType:
my $myParaRun = $HgAutomate::paraRun;
  if ($clusterType eq "falcon1" || $clusterType eq "falcon") {
	$myParaRun = "
para make patchChain_$tDb$qDb jobList -q medium\n
para check patchChain_$tDb$qDb\n
para time patchChain_$tDb$qDb > run.time\n
cat run.time\n";
}

  my $lastzParameters = "--format=axt K=$defVars{'PATCHBLASTZ_K'} L=$defVars{'PATCHBLASTZ_L'} M=0 T=0 W=$defVars{'PATCHBLASTZ_W'}";
  $lastzParameters = "--format=axt K=$defVars{'PATCHBLASTZ_K'} L=$defVars{'PATCHBLASTZ_L'} M=0 T=0 W=$defVars{'PATCHBLASTZ_W'}  Q=$defVars{'BLASTZ_Q'}" if (exists $defVars{'BLASTZ_Q'});
 
  my $whatItDoes =
"It sets up and performs a cluster run to patch chains and rebuild chains using the old and new alignments. ";
  my $bossScript = new HgRemoteScript("$runDir/doPatchChain.csh", $hub,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
# first run patchChain to create the cluster run joblist
$cat $buildDir/axtChain/$chain | \\
    patchChain.perl /dev/stdin $defVars{'SEQ1_DIR'} $defVars{'SEQ2_DIR'} $defVars{'SEQ1_LEN'} $defVars{'SEQ2_LEN'} \\
    -chainMinScore $defVars{'CHAIN_MINSCORE'} -gapMaxSizeT $defVars{'GAPMAXSIZE_T'} -gapMaxSizeQ $defVars{'GAPMAXSIZE_Q'} \\
        -gapMinSizeT $defVars{'GAPMINSIZE_T'} -gapMinSizeQ $defVars{'GAPMINSIZE_Q'} -numJobs $defVars{'NUM_JOBS'} \\
        -jobDir jobs -outputDir $outputDir $filterParameters \\
        -lastzParameters "$lastzParameters"

# create the output dir (we test above if that dir exist, so no mkdir -p is necessary)
mkdir $outputDir

$myParaRun

# concatenate all new results
find $outputDir -name \"*$filterFlag.psl\" | xargs -i cat {} > patched$filterFlag.psl

# delete the individual files
find $outputDir -name \"*$filterFlag.psl\" | xargs -i rm {}

# mv file 
mv patched$filterFlag.psl $outputDir/

# combine old and new psl files
find $pslPartsDir -name \"*.psl.gz\" | xargs -i zcat {} > $outputDir/old.psl
cat $outputDir/old.psl $outputDir/patched$filterFlag.psl | gzip -c > $outputDir/oldAndPatched.psl.gz
rm $outputDir/old.psl
_EOF_
    );

###########
# run per-chrom chaining jobs
my $numRefChroms = `wc -l < $defVars{SEQ1_LEN}`; chomp($numRefChroms);

my $paraReChain = "
para make jobListReChain\n
para check\n
para time > run.timeReChain\n
cat run.timeReChain\n";
   if ($clusterType eq "falcon1" || $clusterType eq "falcon") {
	$paraReChain = "
para make ReChain_$tDb$qDb jobListReChain -q $chainingQueue\n
para check ReChain_$tDb$qDb\n
para time ReChain_$tDb$qDb > run.timeReChain\n
cat run.time\n";
}


  print "--> run chaining on ref-chrom-split psl files (reference has $numRefChroms chromsomes)\n"; 
  $bossScript->add(<<_EOF_

# split by target chrom
setenv splitPSL `mktemp -d`
setenv tmpDir `mktemp -d`
pslSortAcc nohead \$splitPSL \$tmpDir $outputDir/oldAndPatched.psl.gz
rm -rf \$tmpDir

# and bundle into at least 50 Mb chunks
bundleChromSplitPslFiles.perl \$splitPSL $defVars{'SEQ1_LEN'} pslBundled -v -maxBases 50000000
rm -rf \$splitPSL

# create a new chain
# cluster job: run for every bundle
mkdir chain
chmod +x rechain.csh
(cd pslBundled; find . -maxdepth 1 -type f -name "*.psl" -printf "%f\\n") | sed 's/.psl\$//g' | awk '{print "./rechain.csh pslBundled/"\$1".psl chain/"\$1".chain"}' > jobListReChain

$paraReChain

# merge
find chain -name \"*.chain\" | chainMergeSort -inputList=stdin | gzip -c > $buildDir/axtChain/$tDb.$qDb.allpatched.chain.gz

# cleanup a bit and gzip
rm -rf chain pslBundled
_EOF_
    );

  $bossScript->execute();
}	#	sub doPatchChains {}
######################################################################
######################################################################



#######################################################################
# added by Michael Hiller
# remove random chain-breaking alignments using chainCleaner
#######################################################################
sub doCleanChains {
  if ($cleanChains == 0) {
     print "skip cleaning Chains\n";
     return;
  }

  # all or allpatched chains are in this runDir
  my $runDir = "$buildDir/axtChain";

  # First, make sure we're starting clean.
  if (-e "$runDir/log.chainCleaning") {
    die "doCleanChain: looks like this was run successfully already (log.chainCleaning exists). " . 
        "Either run with -continue net or some later stage, or move aside/remove $runDir/log.chainCleaning and run again.\n";
  }elsif (-e "$runDir/$tDb.$qDb.all.beforeCleaning.chain.gz" || -e "$runDir/$tDb.$qDb.allpatched.beforeCleaning.chain.gz") {
    die "doCleanChain: looks like this was run successfully already ($tDb.$qDb.all*.beforeCleaning.chain.gz exists)" .  
        "Either run with -continue net or some later stage, or move aside/remove $runDir/$tDb.$qDb.all*.beforeCleaning.chain.gz and run again.\n";
  }

  # Make sure previous stage was successful.
  my $chain = &getAllChain($runDir);
  if (! defined $chain && ! $opt_debug) {
    die "doCleanChains: looks like previous stage was not successful (can't find [$tDb.$qDb.]all.chain[.gz]).\n";
  }
  # Make sure that the previous stage patching was successful if we do patching.
  if ($patchChains == 1 && ! $opt_debug) {
     die "doCleanChains: looks like previous stage was not successful (can't find $buildDir/axtChain/$tDb.$qDb.allpatched.chain.gz)\n" if (! -e "$buildDir/axtChain/$tDb.$qDb.allpatched.chain.gz");
     die "doCleanChains: looks like previous stage was not successful (can't find $buildDir/run.patchChain/run.timeReChain)\n" if (! -e "$buildDir/run.patchChain/run.timeReChain");
     # use the allpatched.chain later
     $chain = "$tDb.$qDb.allpatched.chain.gz";
  }

  # initial output chain. 
  # NOTE: we rename the all[patched].chain.gz later as all[patched].beforeCleaning.chain.gz   and  let all[patched].chain.gz be the cleaned chain. Reason: No change in netChains necessary.
  my $outputChain = "$tDb.$qDb.all.cleaned.chain";
  $outputChain = "$tDb.$qDb.allpatched.cleaned.chain" if ($patchChains == 1);
  my $renamedChain = "$tDb.$qDb.all.beforeCleaning.chain.gz";
  $renamedChain = "$tDb.$qDb.allpatched.beforeCleaning.chain.gz" if ($patchChains == 1);

 
  # create the script that cleans the chains
  my $seq1Dir = $defVars{'SEQ1_CTGDIR'} || $defVars{'SEQ1_DIR'};
  my $seq2Dir = $defVars{'SEQ2_CTGDIR'} || $defVars{'SEQ2_DIR'};
  my $matrix = $defVars{'BLASTZ_Q'} ? "-scoreScheme=$defVars{BLASTZ_Q} " : "";
#  my $minScore = $opt_chainMinScore ? "-minScore=$opt_chainMinScore" : "";
  my $linearGap = $opt_chainLinearGap ? "-linearGap=$opt_chainLinearGap" : "-linearGap=$defaultChainLinearGap";

  #customise the $paraCleanChain depending upon the clusterType and create joblist file to run chainClean step on falcon cluster ONLY
  my $paraCleanChain='./cleanChains.csh';
  if ($clusterType eq "falcon"){
	# request 20 GB
  	$paraCleanChain = "
para make cleanChain_$tDb$qDb jobListChainCleaner -q short  -p '-R \"rusage[mem=20000]\"'\n
para check cleanChain_$tDb$qDb\n
para time cleanChain_$tDb$qDb > run.time.chainClean\n
cat run.time.chainClean\n";
   open FILE, ">$runDir/jobListChainCleaner" or die $!; 
   print FILE "./cleanChains.csh\n";
   close FILE;
 }elsif ($clusterType eq "falcon1"){
	# request 60 GB
  	$paraCleanChain = "
para make cleanChain_$tDb$qDb jobListChainCleaner -q short  -memoryMb 60000\n
para check cleanChain_$tDb$qDb\n
para time cleanChain_$tDb$qDb > run.time.chainClean\n
cat run.time.chainClean\n";
   open FILE, ">$runDir/jobListChainCleaner" or die $!; 
   print FILE "./cleanChains.csh\n";
   close FILE;
 }
 

  # cleanChains.csh runs the actual chainCleaner command
  my $fh = &HgAutomate::mustOpen(">$runDir/cleanChains.csh");
  print $fh <<_EOF_
#!/bin/csh -ef
time chainCleaner $buildDir/axtChain/$chain $seq1Dir $seq2Dir $outputChain removedSuspects.bed $linearGap $matrix -tSizes=$defVars{SEQ1_LEN} -qSizes=$defVars{SEQ2_LEN} $defVars{'CLEANCHAIN_PARAMETERS'} >& log.chainCleaner
_EOF_
  ;
  close($fh);

 my $whatItDoes = "It performs a chainCleaner run on the cluster.";

  # script that we execute. This pushes the chainCleaner cluster job.
  my $bossScript = new HgRemoteScript("$runDir/doCleanChain.csh", $hub, $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
# clean chain
chmod a+x cleanChains.csh
$paraCleanChain

gzip $outputChain
  
# now rename the all[patched].chain.gz later as all[patched].beforeCleaning.chain.gz
mv $buildDir/axtChain/$chain $buildDir/axtChain/$renamedChain
  
# now rename the all[patched].cleaned.chain.gz later as all[patched].chain.gz
mv $buildDir/axtChain/$outputChain.gz $buildDir/axtChain/$chain
_EOF_
    );

  $bossScript->execute();
}  # do clean chains




sub netChains {
  # Turn chains into nets (,axt,maf,.over.chain).
  # Don't do this for self alignments.
  return if ($isSelf);
  my $runDir = "$buildDir/axtChain";
  
  # First, make sure we're starting clean.
  if (-d "$buildDir/mafNet") {
    die "netChains: looks like this was run successfully already " .
      "(mafNet exists).  Either run with -continue load or some later " .
	"stage, or move aside/remove $buildDir/mafNet " .
	  "and $runDir/noClass.net and run again.\n";
  } elsif (-e "$runDir/noClass.net") {
    die "netChains: looks like we are not starting with a " .
      "clean state.  Please move aside or remove $runDir/noClass.net " .
	"and run again.\n";
  }
  # Make sure previous stage was successful.
  my $chain = &getAllChain($runDir);
  if (! defined $chain && ! $opt_debug) {
    die "netChains: looks like previous stage was not successful " .
      "(can't find [$tDb.$qDb.]all.chain[.gz]).\n";
  }
  # Make sure previous stage was successful. Special check if patching was done
  if ($patchChains == 1 && ! $opt_debug) {
  	die "netChains: looks like previous stage was not successful (can't find $buildDir/axtChain/$tDb.$qDb.allpatched.chain.gz)\n" if (! -e "$buildDir/axtChain/$tDb.$qDb.allpatched.chain.gz");
  	die "netChains: looks like previous stage was not successful (can't find $buildDir/run.patchChain/run.timeReChain)\n" if (! -e "$buildDir/run.patchChain/run.timeReChain");
	# use the allpatched.chain later
	$chain = "$buildDir/axtChain/$tDb.$qDb.allpatched.chain.gz";
  }

  #customise the $paraNetChain depending upon the clusterType and create joblist file to run net step on  falcon cluster ONLY
  my $paraNetChain='./netChains.csh';
  if ($clusterType eq "falcon"){
  	$paraNetChain = "
para make netChain_$tDb$qDb jobList -q medium  -p '-R \"rusage[mem=20000]\"'\n
para check netChain_$tDb$qDb\n
para time netChain_$tDb$qDb > run.time\n
cat run.time\n";
   open FILE, ">$runDir/jobList" or die $!; 
   print FILE './netChains.csh'."\n";
   close FILE;
 }elsif ($clusterType eq "falcon1"){
  	$paraNetChain = "
para make netChain_$tDb$qDb jobList -q medium -memoryMb 25000\n
para check netChain_$tDb$qDb\n
para time netChain_$tDb$qDb > run.time\n
cat run.time\n";
   open FILE, ">$runDir/jobList" or die $!; 
   print FILE './netChains.csh'."\n";
   close FILE;
 }

  #Creating a netChains.csh script that will actually exceute the netting step
  my $fh = &HgAutomate::mustOpen(">$runDir/netChains.csh");
  print $fh <<_EOF_
#!/bin/csh -ef
# Make nets ("noClass", i.e. without rmsk/class stats which are added later):
chainPreNet $inclHap $chain $defVars{SEQ1_LEN} $defVars{SEQ2_LEN} stdout \\
| chainNet $inclHap stdin -minSpace=1 $defVars{SEQ1_LEN} $defVars{SEQ2_LEN} stdout /dev/null $rescoreSubNets \\
| netSyntenic stdin noClass.net

# Make liftOver chains:
netChainSubset -verbose=0 noClass.net $chain stdout \\
| chainStitchId stdin stdout | gzip -c > $tDb.$qDb.over.chain.gz
_EOF_
  ;
  my $seq1Dir = $defVars{'SEQ1_DIR'};
  my $seq2Dir = $defVars{'SEQ2_DIR'};
  if ($splitRef) {
  	# if we have patched chains, we need to remove the axtChain/chain dir and split the allpatched chains
	if ($patchChains == 1) {
  		print $fh <<_EOF_
#rm -rf $runDir/chain
#chainSplit $runDir/chain $chain
_EOF_
 ;
 	}  
  	print $fh <<_EOF_ 
# Make axtNet for download: one .axt per $tDb seq.
#netSplit noClass.net net
#cd ..
#mkdir axtNet
#foreach f (axtChain/net/*.net)
#netToAxt \$f axtChain/chain/\$f:t:r.chain \\
#  $seq1Dir $seq2Dir stdout \\
#  | axtSort stdin stdout \\
#  | gzip -c > axtNet/\$f:t:r.$tDb.$qDb.net.axt.gz
#end
# Make mafNet for multiz: one .maf per $tDb seq.
mkdir ../mafNet
#foreach f (axtNet/*.$tDb.$qDb.net.axt.gz)
#  axtToMaf -tPrefix=$tDb. -qPrefix=$qDb. \$f \\
#        $defVars{SEQ1_LEN} $defVars{SEQ2_LEN} \\
#        stdout \\
#  | gzip -c > mafNet/\$f:t:r:r:r:r:r.maf.gz
#end    
_EOF_
;
  } 
  else {
    print $fh <<_EOF_ 
# Make axtNet for download: one .axt for all of $tDb.
#mkdir ../axtNet
#netToAxt -verbose=0 noClass.net $chain \\
#  $seq1Dir $seq2Dir stdout \\
#| axtSort stdin stdout \\
#| gzip -c > ../axtNet/$tDb.$qDb.net.axt.gz

# Make mafNet for multiz: one .maf for all of $tDb.
mkdir ../mafNet
#axtToMaf -tPrefix=$tDb. -qPrefix=$qDb. ../axtNet/$tDb.$qDb.net.axt.gz \\
#  $defVars{SEQ1_LEN} $defVars{SEQ2_LEN} \\
#  stdout \\
#| gzip -c > ../mafNet/$tDb.$qDb.net.maf.gz
_EOF_
    ;      
  }  
  close($fh);
  
  my $whatItDoes =
"It generates nets (without repeat/gap stats -- those are added later on
$dbHost) from chains, and generates axt, maf and .over.chain from the nets.";
  my $bossScript = new HgRemoteScript("$runDir/doNetChains.csh", $workhorse,
				      $runDir, $whatItDoes, $DEF);
  
  $bossScript->add(<<_EOF_
chmod a+x netChains.csh
$paraNetChain
_EOF_
     ); 
 
  $bossScript->execute();
}	#	sub netChains {}


sub loadUp {
	# MH: modified to run only netClass; no chain/net loading anymore
  # Load chains; add repeat/gap stats to net; load nets.
  my $runDir = "$buildDir/axtChain";
  my $QDbLink = "chain$QDb" . "Link";
  # First, make sure we're starting clean.
  if (-e "$runDir/$tDb.$qDb.net" || -e "$runDir/$tDb.$qDb.net.gz") {
    die "loadUp: looks like this was run successfully already " .
      "($tDb.$qDb.net[.gz] exists).  Either run with -continue download, " .
	"or move aside/remove $runDir/$tDb.$qDb.net[.gz] and run again.\n";
  }
  # Make sure previous stage was successful.
  my $successDir = $isSelf ? "$runDir/$tDb.$qDb.all.chain.gz" :
                             "$buildDir/mafNet/";
  if (! -e $successDir && ! $opt_debug) {
    die "loadUp: looks like previous stage was not successful " .
      "(can't find $successDir).\n";
  }
  my $whatItDoes =
"It loads the chain tables into $tDb, adds gap/repeat stats to the .net file,
and loads the net table.";
  my $bossScript = new HgRemoteScript("$runDir/loadUp.csh", $workhorse,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
# Load chains:
_EOF_
    );
  if ($opt_loadChainSplit && $splitRef) {
    $bossScript->add(<<_EOF_
#cd $runDir/chain
#foreach c (`awk '{print \$1;}' $defVars{SEQ1_LEN}`)
#    set f = \$c.chain
#    if (! -e \$f) then
#      echo no chains for \$c
#      set f = /dev/null
#    endif
#    hgLoadChain $tDb \${c}_chain$QDb \$f
#end
_EOF_
      );
  } else {
    $bossScript->add(<<_EOF_
#cd $runDir
#hgLoadChain -tIndex $tDb chain$QDb $tDb.$qDb.all.chain.gz
_EOF_
      );
  }
  if (! $isSelf) {
  	my $tRepeats = $opt_tRepeats ? "-tRepeats=$opt_tRepeats" : $defaultTRepeats;
  	my $qRepeats = $opt_qRepeats ? "-qRepeats=$opt_qRepeats" : $defaultQRepeats;
  	if ($opt_swap) {
    		$tRepeats = $opt_qRepeats ? "-tRepeats=$opt_qRepeats" : $defaultQRepeats;
    	$qRepeats = $opt_tRepeats ? "-qRepeats=$opt_tRepeats" : $defaultTRepeats;
  	}

 	if ($clusterType eq "falcon1" || $clusterType eq "falcon") {
   		$bossScript->add(<<_EOF_

# Add gap/repeat stats to the net file using database tables:
# create a tmp dir on genome first
# in that dir we will scp the noClass.net and run netClass
# then we copy the resulting net file back and delete that tmp dir		
set remoteTempDir=`ssh -x -o 'StrictHostKeyChecking = no' -o 'BatchMode = yes' $dbHost nice mktemp -d`
echo "created remote temp dir \$remoteTempDir on $dbHost" 
cd $runDir
scp noClass.net $dbHost:\$remoteTempDir
ssh -x -o 'StrictHostKeyChecking = no' -o 'BatchMode = yes' $dbHost nice netClass -verbose=0 -noAr \$remoteTempDir/noClass.net $tDb $qDb \$remoteTempDir/$tDb.$qDb.net
scp $dbHost:\$remoteTempDir/$tDb.$qDb.net .
ssh -x -o 'StrictHostKeyChecking = no' -o 'BatchMode = yes' $dbHost nice rm -rf \$remoteTempDir
gzip $tDb.$qDb.net
# Add gap/repeat stats to the net file using database tables:
#cd $runDir
#netClass -verbose=0 $tRepeats $qRepeats -noAr noClass.net $tDb $qDb $tDb.$qDb.net

# Load nets:
#netFilter -minGap=10 $tDb.$qDb.net \\
#| hgLoadNet -verbose=0 $tDb net$QDb stdin

#cd $buildDir
#featureBits $tDb $QDbLink >&fb.$tDb.$QDbLink.txt
#cat fb.$tDb.$QDbLink.txt
_EOF_
      );
  	# on genome: just execute netClass    
  	}else{
	    $bossScript->add(<<_EOF_
# Add gap/repeat stats to the net file using database tables:
cd $runDir
netClass -verbose=0 $tRepeats $qRepeats -noAr noClass.net $tDb $qDb $tDb.$qDb.net
gzip $tDb.$qDb.net
_EOF_
); 
  	}
  }
  
  $bossScript->execute();
# maybe also peek in trackDb and see if entries need to be added for chain/net
}	#	sub loadUp {}


sub makeDownloads {
  # Compress the netClassed .net for download (other files should have been
  # compressed already).
  my $runDir = "$buildDir/axtChain";
  
  if (-e "$runDir/$tDb.$qDb.net") {
    &HgAutomate::run("$HgAutomate::runSSH $fileServer nice " .
	 "gzip $runDir/$tDb.$qDb.net");
  }
  # Make an md5sum.txt file.
  my $net = $isSelf ? "" : "$tDb.$qDb.net.gz";
  my $whatItDoes =
"It makes an md5sum.txt file for downloadable files, with relative paths
matching what the user will see on the download server, and installs the
over.chain file in the liftOver dir.";
  my $bossScript = new HgRemoteScript("$runDir/makeMd5sum.csh", $workhorse,
				      $runDir, $whatItDoes, $DEF);
  my $over = $tDb . "To$QDb.over.chain.gz";
  my $altOver = "$tDb.$qDb.over.chain.gz";
  my $liftOverDir = "$HgAutomate::clusterData/$tDb/$HgAutomate::trackBuild/liftOver";
  $bossScript->add(<<_EOF_
mkdir -p $liftOverDir
md5sum $tDb.$qDb.all.chain.gz $net > md5sum.txt
_EOF_
  );
  if (! $isSelf) {
    my $axt = ($splitRef ?
	       "md5sum axtNet/*.gz >> axtChain/md5sum.txt" :
	       "cd axtNet\nmd5sum *.gz >> ../axtChain/md5sum.txt");
    $bossScript->add(<<_EOF_
rm -f $liftOverDir/$over
cp -p $altOver $liftOverDir/$over
cd ..
$axt
_EOF_
    );
  }
  $bossScript->execute();
}

sub getBlastzParams {
  my %vars;
  # Return parameters in BLASTZ_Q file, or defaults, for README.txt.
  my $matrix =
"           A    C    G    T
      A   91 -114  -31 -123
      C -114  100 -125  -31
      G  -31 -125  100 -114
      T -123  -31 -114   91";
  if ($defVars{'BLASTZ_Q'}) {
    my $readLineLimit = 100;  # safety valve to get out if reading nonsense
    my $linesRead = 0;
    my $fh = &HgAutomate::mustOpen($defVars{'BLASTZ_Q'});
    my $line;
    my $matrixFound = 0;
    while (!$matrixFound && ($linesRead < $readLineLimit) && ($line = <$fh>)) {
      ++$linesRead;
      next if (($line =~ m/^#/) || ($line =~ m/^$/));
      if ($line =~ m/^\s*A\s+C\s+G\s+T\s*$/) {
        $matrixFound = 1;
      } else {
         chomp $line;
         $line =~ s/\s+//g;
         $line =~ s/#.*//;
         die "can not find tag=value in $defVars{BLASTZ_Q}" if ($line !~ /=/);
         my ($tag, $value) = split('=',$line);
         # ignore O E gap_open_penalty gap_extend_penalty
         next if ($tag eq "O" || $tag eq "E"
               || $tag eq "gap_open_penalty" || $tag eq "gap_extend_penalty");
         $vars{$tag} = $value;
      }
    }
    die "can not find score matrix in $defVars{BLASTZ_Q}" if (!$matrixFound);
    $line =~ s/^   // if (length($line) > 22);
    $matrix = '        ' . $line;
    foreach my $base ('A', 'C', 'G', 'T') {
      $line = <$fh>;
      die "Too few lines of $defVars{BLASTZ_Q}" if (! $line);
      if ($line !~ /^[ACGT]?\s*-?\d+\s+-?\d+\s+-?\d+\s+-?\d+\s*$/) {
	die "Can't parse this line of $defVars{BLASTZ_Q}:\n$line";
      }
      $line =~ s/^[ACGT] //;
      $matrix .= "      $base " . $line;
    }
    chomp $matrix;
    $line = <$fh>;
    if ($line && $line =~ /\S/) {
      warn "\nWarning: BLASTZ_Q matrix file $defVars{BLASTZ_Q} has " .
           "additional contents after the matrix -- those are ignored " .
	   "by blastz.\n\n";
    }
    close($fh);
  }
  my $o = $defVars{'BLASTZ_O'} || 400;
  my $e = $defVars{'BLASTZ_E'} || 30;
  my $k = $defVars{'BLASTZ_K'} || 3000;
  my $l = $defVars{'BLASTZ_L'} || 3000;
  my $h = $defVars{'BLASTZ_H'} || 2000;
  my $blastzOther = '';
  foreach my $var (sort keys %defVars) {
    if ($var =~ /^BLASTZ_(\w)$/) {
      my $p = $1;
      if ($p ne 'K' && $p ne 'L' && $p ne 'H' && $p ne 'Q') {
	if ($blastzOther eq '') {
	  $blastzOther = 'Other blastz
parameters specifically set for this species pair:';
	}
	$blastzOther .= "\n    $p=$defVars{$var}";
      }
    }
  }
  return ($matrix, $o, $e, $k, $l, $h, $blastzOther);
}

sub commafy {
  # Assuming $num is a number, add commas where appropriate.
  my ($num) = @_;
  $num =~ s/(\d)(\d\d\d)$/$1,$2/;
  $num =~ s/(\d)(\d\d\d),/$1,$2,/g;
  return($num);
}

sub describeOverlapping {
  # Return some text describing how large sequences were split.
  my $lap;
  my $chunkPlusLap1 = $defVars{'SEQ1_CHUNK'} + $defVars{'SEQ1_LAP'};
  my $chunkPlusLap2 = $defVars{'SEQ2_CHUNK'} + $defVars{'SEQ2_LAP'};
  if ($chunkPlusLap1 == $chunkPlusLap2) {
    $lap .= "Any sequences larger\n" .
"than " . &commafy($chunkPlusLap1) . " bases were split into chunks of " .
&commafy($chunkPlusLap1) . " bases
overlapping by " . &commafy($defVars{SEQ1_LAP}) . " bases for alignment.";
  } else {
    $lap .= "Any $tDb sequences larger\n" .
"than " . &commafy($chunkPlusLap1) . " bases were split into chunks of " .
&commafy($chunkPlusLap1) . " bases overlapping
by " . &commafy($defVars{SEQ1_LAP}) . " bases for alignment.  " .
"A similar process was followed for $qDb,
with chunks of " . &commafy($chunkPlusLap2) . " overlapping by " .
&commafy($defVars{SEQ2_LAP}) . ".";
  }
  $lap .= "  Following alignment, the
coordinates of the chunk alignments were corrected by the
blastz-normalizeLav script written by Scott Schwartz of Penn State.";
  return $lap;
}


sub dumpDownloadReadme {
  # Write a file (README.txt) describing the download files.
  my ($file) = @_;
  my $fh = &HgAutomate::mustOpen(">$file");
  my ($tGenome, $tDate, $tSource) = &HgAutomate::getAssemblyInfo($dbHost, $tDb);
  my ($qGenome, $qDate, $qSource) = &HgAutomate::getAssemblyInfo($dbHost, $qDb);
  my $dir = $splitRef ? 'axtNet/*.' : '';
  my $synNet = $splitRef ?
  "mafSynNet/*.maf.gz - filtered net files for syntenic alignments
               only, in MAF format, see also, description of MAF format:
               http://genome.ucsc.edu/FAQ/FAQformat.html#format5" :
  "$tDb.$qDb.synNet.maf.gz - filtered net file for syntenic alignments
               only, in MAF format, see also, description of MAF format:
               http://genome.ucsc.edu/FAQ/FAQformat.html#format5

  - $tDb.$qDb.syn.net.gz - filtered net file for syntenic alignments only";

  my ($matrix, $o, $e, $k, $l, $h, $blastzOther) = &getBlastzParams();
  my $defaultMatrix = $defVars{'BLASTZ_Q'} ? '' : ' the default matrix';
  my $lap = &describeOverlapping();
  my $abridging = "";
  if ($defVars{'BLASTZ_ABRIDGE_REPEATS'}) {
    if ($isSelf) {
      $abridging = "
All repetitive sequences identified by RepeatMasker were removed from the
assembly before alignment using the fasta-subseq and strip_rpts programs
from Penn State.  The abbreviated genome was aligned with lastz, and the
transposons were then added back in (i.e. the alignment coordinates were
adjusted) using the restore_rpts program from Penn State.";
    } else {
      $abridging = "
Transposons that have been inserted since the $qGenome/$tGenome split were
removed from the assemblies before alignment using the fasta-subseq and
strip_rpts programs from Penn State.  The abbreviated genomes were aligned
with lastz, and the transposons were then added back in (i.e. the
alignment coordinates were adjusted) using the restore_rpts program from
Penn State.";
    }
  }
  my $desc = $isSelf ?
"This directory contains alignments of
    $tGenome ($tDb, $tDate,
    $tSource) to itself." :
"This directory contains alignments of the following assemblies:

  - target/reference: $tGenome
    ($tDb, $tDate,
    $tSource)

  - query: $qGenome
    ($qDb, $qDate,
    $qSource)";

  print $fh "$desc

Files included in this directory:

  - md5sum.txt: md5sum checksums for the files in this directory

  - $tDb.$qDb.all.chain.gz: chained lastz alignments. The chain format is
    described in http://genome.ucsc.edu/goldenPath/help/chain.html .

";
  if (! $isSelf) {
    print $fh
"  - $tDb.$qDb.net.gz: \"net\" file that describes rearrangements between
    the species and the best $qGenome match to any part of the
    $tGenome genome.  The net format is described in
    http://genome.ucsc.edu/goldenPath/help/net.html .

  - $dir$tDb.$qDb.net.axt.gz: chained and netted alignments,
    i.e. the best chains in the $tGenome genome, with gaps in the best
    chains filled in by next-best chains where possible.  The axt format is
    described in http://genome.ucsc.edu/goldenPath/help/axt.html .

  - $synNet

  - reciprocalBest/ directory, contains reciprocal-best netted chains
    for $tDb-$qDb

";
  }
  if ($opt_swap) {
    my $TDb = ucfirst($tDb);
    print $fh
"The chainSwap program was used to translate $qDb-referenced chained lastz
alignments to $tDb into $tDb-referenced chains aligned to $qDb.  See
the download directory goldenPath/$qDb/vs$TDb/README.txt for more
information about the $qDb-referenced lastz and chaining process.
";
  } else {
    print $fh ($isSelf ?
"The $tDb assembly was aligned to itself" :
"The $tDb and $qDb assemblies were aligned");
  my $chainMinScore = $opt_chainMinScore ? "$opt_chainMinScore" :
	$defaultChainMinScore;
  my $chainLinearGap = $opt_chainLinearGap ? "$opt_chainLinearGap" :
	$defaultChainLinearGap;
    print $fh " by the lastz alignment
program, which is available from Webb Miller's lab at Penn State
University (http://www.bx.psu.edu/miller_lab/).  $lap $abridging

The lastz scoring matrix (Q parameter) used was$defaultMatrix:

$matrix

with a gap open penalty of O=$o and a gap extension penalty of E=$e.
The minimum score for an alignment to be kept was K=$k for the first pass
and L=$l for the second pass, which restricted the search space to the
regions between two alignments found in the first pass.  The minimum
score for alignments to be interpolated between was H=$h.  $blastzOther

The .lav format lastz output was translated to the .psl format with
lavToPsl, then chained by the axtChain program.\n
Chain minimum score: $chainMinScore, and linearGap matrix of ";
    if ($chainLinearGap =~ m/loose/) {
	print $fh "(loose):
tablesize   11
smallSize   111
position  1   2   3   11  111 2111  12111 32111 72111 152111  252111
qGap    325 360 400  450  600 1100   3600  7600 15600  31600   56600
tGap    325 360 400  450  600 1100   3600  7600 15600  31600   56600
bothGap 625 660 700  750  900 1400   4000  8000 16000  32000   57000
";
    } elsif ($chainLinearGap =~ m/medium/) {
	print $fh "(medium):
tableSize   11
smallSize  111
position  1   2   3   11  111 2111  12111 32111  72111 152111  252111
qGap    350 425 450  600  900 2900  22900 57900 117900 217900  317900
tGap    350 425 450  600  900 2900  22900 57900 117900 217900  317900
bothGap 750 825 850 1000 1300 3300  23300 58300 118300 218300  318300
";
    } else {
	print $fh "(specified):\n", `cat $chainLinearGap`, "\n";
    }
  }
  if (! $isSelf) {
    print $fh "
Chained alignments were processed into nets by the chainNet, netSyntenic,
and netClass programs.
Best-chain alignments in axt format were extracted by the netToAxt program.";
  }
  print $fh "
All programs run after lastz were written by Jim Kent at UCSC.

----------------------------------------------------------------
If you plan to download a large file or multiple files from this directory,
we recommend you use ftp rather than downloading the files via our website.
To do so, ftp to hgdownload.soe.ucsc.edu, then go to the directory
goldenPath/$tDb/vs$QDb/. To download multiple files, use the \"mget\"
command:

    mget <filename1> <filename2> ...
    - or -
    mget -a (to download all files in the current directory)

All files in this directory are freely available for public use.

--------------------------------------------------------------------
References

Chiaromonte F, Yap VB, Miller W. Scoring pairwise genomic sequence
alignments. Pac Symp Biocomput.  2002:115-26.

Kent WJ, Baertsch R, Hinrichs A, Miller W, Haussler D.
Evolution's cauldron: Duplication, deletion, and rearrangement in the
mouse and human genomes. Proc Natl Acad Sci U S A. 2003 Sep
30;100(20):11484-9.

Schwartz S, Kent WJ, Smit A, Zhang Z, Baertsch R, Hardison RC,
Haussler D, Miller W. Human-Mouse Alignments with BLASTZ. Genome
Res. 2003 Jan;13(1):103-7.

";
  close($fh);
}


sub installDownloads {
  # Load chains; add repeat/gap stats to net; load nets.
  my $runDir = "$buildDir/axtChain";
  # Make sure previous stage was successful.
  my $successFile = $isSelf ? "$runDir/$tDb.$qDb.all.chain.gz" :
                              "$runDir/$tDb.$qDb.net.gz";
  if (! -e $successFile && ! $opt_debug) {
    die "installDownloads: looks like previous stage was not successful " .
      "(can't find $successFile).\n";
  }
  &dumpDownloadReadme("$runDir/README.txt");
  my $over = $tDb . "To$QDb.over.chain.gz";
  my $liftOverDir = "$HgAutomate::clusterData/$tDb/$HgAutomate::trackBuild/liftOver";
  my $gpLiftOverDir = "$HgAutomate::goldenPath/$tDb/liftOver";
  my $gbdbLiftOverDir = "$HgAutomate::gbdb/$tDb/liftOver";
  my $andNets = $isSelf ? "." :
    ", nets and axtNet,\n" .
    "# and copies the liftOver chains to the liftOver download dir.";
  my $whatItDoes = "It creates the download directory for chains$andNets";
  my $bossScript = new HgRemoteScript("$runDir/installDownloads.csh", $dbHost,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
mkdir -p $HgAutomate::goldenPath/$tDb
rm -rf $HgAutomate::goldenPath/$tDb/vs$QDb
mkdir $HgAutomate::goldenPath/$tDb/vs$QDb
cd $HgAutomate::goldenPath/$tDb/vs$QDb
ln -s $runDir/$tDb.$qDb.all.chain.gz .
ln -s $runDir/README.txt .
ln -s $runDir/md5sum.txt .

_EOF_
    );
  if (! $isSelf) {
    my $axt = ($splitRef ?
	       "mkdir axtNet\n" . "ln -s $buildDir/axtNet/*.axt.gz axtNet/" :
	       "ln -s $buildDir/axtNet/$tDb.$qDb.net.axt.gz .");
    $bossScript->add(<<_EOF_
ln -s $runDir/$tDb.$qDb.net.gz .
$axt

mkdir -p $gpLiftOverDir
rm -f $gpLiftOverDir/$over
ln -s $liftOverDir/$over $gpLiftOverDir/$over
mkdir -p $gbdbLiftOverDir
rm -f $gbdbLiftOverDir/$over
ln -s $liftOverDir/$over $gbdbLiftOverDir/$over
hgAddLiftOverChain -minMatch=0.1 -multiple -path=$gbdbLiftOverDir/$over \\
  $tDb $qDb

# Update (or create) liftOver/md5sum.txt with the new .over.chain.gz.
if (-e $gpLiftOverDir/md5sum.txt) then
  set tmpFile = `mktemp -t tmpMd5.XXXXXX`
  csh -c "grep -v $over $gpLiftOverDir/md5sum.txt || true" > \$tmpFile
  md5sum $gpLiftOverDir/$over \\
  | sed -e 's\@$gpLiftOverDir/\@\@' >> \$tmpFile
  sort \$tmpFile > $gpLiftOverDir/md5sum.txt
  rm \$tmpFile
else
  md5sum $gpLiftOverDir/$over | sed -e 's\@$gpLiftOverDir/\@\@' \\
	> $gpLiftOverDir/md5sum.txt
endif
_EOF_
      );
  }
  $bossScript->execute();
# maybe also peek in trackDb and see if entries need to be added for chain/net
}

sub doDownloads {
	return 1;
  # Create compressed files for download and make links from test server's
  # goldenPath/ area.
  &makeDownloads();
  &installDownloads();
}

sub cleanup {
  # Remove intermediate files.
  my $runDir = $buildDir;
  my $outRoot = $opt_blastzOutRoot ? "$opt_blastzOutRoot/psl" : "$buildDir/psl";
  my $rootCanal = ($opt_blastzOutRoot ?
		   "rmdir --ignore-fail-on-non-empty $opt_blastzOutRoot" :
		   '');
  my $doSymLink = 0;
  my $baseName = basename($buildDir);
  my $dirName = dirname($buildDir);
  $doSymLink = 1 if ($dirName =~ m#.*/$tDb/bed$#);
  my $whatItDoes =
"It cleans up files after a successful blastz/chain/net/install series.
It uses rm -f so failures should be ignored (e.g. if a partial cleanup has
already been performed).";
  my $bossScript = new HgRemoteScript("$buildDir/cleanUp.csh", $fileServer,
				      $runDir, $whatItDoes, $DEF);
  $bossScript->add(<<_EOF_
rm -fr $outRoot/
$rootCanal
rm -fr $buildDir/axtChain/run/chain/
rm -fr $buildDir/axtChain/run/err/
rm -fr $buildDir/run.blastz/err/
# avoid no-match error exit when *.2bit does not exist
/bin/csh -c "rm -fr $buildDir/run.blastz/tParts/*.2bit || true"
/bin/csh -c "rm -fr $buildDir/run.blastz/qParts/*.2bit || true"
rm -fr $buildDir/run.cat/err/
rm -f  $buildDir/axtChain/noClass.net
rm -f  $buildDir/run.blastz/batch.bak
rm -f  $buildDir/run.cat/batch.bak
rm -f  $buildDir/axtChain/run/batch.bak
rm -rf $buildDir/run.filterPsl/err/
rm -rf $buildDir/run.patchChain/err/
rm -f $buildDir/run.filterPsl/batch.bak
rm -f $buildDir/run.patchChain/batch.bak
_EOF_
    );
  if ($splitRef) {
    $bossScript->add(<<_EOF_
rm -fr $buildDir/axtChain/net/
rm -fr $buildDir/axtChain/chain/
_EOF_
      );
  }
  if ($doSymLink) {
    $bossScript->add(<<_EOF_
cd $dirName
rm -f lastz.$qDb
ln -s $baseName lastz.$qDb
_EOF_
      );
  }
  $bossScript->execute();
}

sub doSyntenicNet {
  # MH: changes are (i) we always call doSyntenicNet unless you -stop before, (ii) we test for $tDb.$qDb.syntenic.net.gz (not mafSynNet dir)
  # and (iii) we don't run netToAxt anymore

  # Create syntenic net mafs for multiz
  my $whatItDoes =
"It filters the net for synteny and creates syntenic net MAF files for
multiz. Use this option when the query genome is high-coverage and not
too distant from the reference.  Suppressed unless -syntenicNet is included.";
#  if (not $opt_syntenicNet) {
#    return;
#  }
  my $runDir = "$buildDir/axtChain";
  my $successNetFile = "$buildDir/axtChain/$tDb.$qDb.syntenic.net.gz";
  if (-e $successNetFile) {
      die "doSyntenicNet: looks like this was run successfully already " .
          "($successNetFile exists).  To re-run, " .
          "move aside/remove $successNetFile and run again.\n";
  }
  # remove a potential unzipped syntenic net file in case it exists
  `rm -f $buildDir/axtChain/$tDb.$qDb.syntenic.net`;

  # First, make sure we're starting clean.
#  my $successDir = "$buildDir/mafSynNet";
#  if (-e $successDir) {
#      die "doSyntenicNet: looks like this was run successfully already " .
#          "($successDir).  To re-run, " .
#          "move aside/remove $successDir and run again.\n";
#  }
  # Make sure previous stage was successful.
  my $successFile = "$runDir/$tDb.$qDb.net";
  my $successFileWithoutPath = "$tDb.$qDb.net";
	if (! -e "$runDir/$tDb.$qDb.net" && -e "$runDir/$tDb.$qDb.net.gz") {
   	$successFile = "$runDir/$tDb.$qDb.net.gz";
		$successFileWithoutPath = "$tDb.$qDb.net.gz";
 	}
  
  if (! -e "$successFile" && ! $opt_debug) {
      die "doSyntenicNet: looks like previous stage was not successful " .
          "(can't find $successFile).\n";
  }
  
  my $bossScript = new HgRemoteScript("$runDir/netSynteny.csh", $workhorse,
                                    $runDir, $whatItDoes, $DEF);
=pod
  if ($splitRef) {
    $bossScript->add(<<_EOF_
# filter net for synteny and create syntenic net mafs
netFilter -syn $successFileWithoutPath  \\
    | netSplit stdin synNet
chainSplit chain $tDb.$qDb.all.chain.gz
cd ..
mkdir $successDir
foreach f (axtChain/synNet/*.net)
netToAxt \$f axtChain/chain/\$f:t:r.chain \\
    $defVars{'SEQ1_DIR'} $defVars{'SEQ2_DIR'} stdout \\
  | axtSort stdin stdout \\
  | axtToMaf -tPrefix=$tDb. -qPrefix=$qDb. stdin \\
    $defVars{SEQ1_LEN} $defVars{SEQ2_LEN} \\
    stdout \\
| gzip -c > mafSynNet/\$f:t:r:r:r:r:r.maf.gz
end
rm -fr $runDir/synNet
rm -fr $runDir/chain
cd mafSynNet
md5sum *.maf.gz > md5sum.txt
mkdir -p $HgAutomate::goldenPath/$tDb/vs$QDb/mafSynNet
cd $HgAutomate::goldenPath/$tDb/vs$QDb/mafSynNet
ln -s $buildDir/mafSynNet/* .
_EOF_
      );
  } else {
=cut
# scaffold-based assembly
# filter net for synteny and create syntenic net mafs
    $bossScript->add(<<_EOF_
netFilter -syn $successFileWithoutPath | gzip -c > $tDb.$qDb.syntenic.net.gz
#netToAxt $tDb.$qDb.syn.net.gz $tDb.$qDb.all.chain.gz \\
#    $defVars{'SEQ1_DIR'} $defVars{'SEQ2_DIR'} stdout \\
#  | axtSort stdin stdout \\
#  | axtToMaf -tPrefix=$tDb. -qPrefix=$qDb. stdin \\
#    $defVars{SEQ1_LEN} $defVars{SEQ2_LEN} \\
#    stdout \\
#| gzip -c > $tDb.$qDb.synNet.maf.gz
_EOF_
      );
#  }
  $bossScript->execute();
}

#########################################################################
#
# -- main --

# Prevent "Suspended (tty input)" hanging:
&HgAutomate::closeStdin();

#$opt_debug = 1;

&checkOptions();

# initializations depending on the cluster type
$clusterType = $opt_clusterType;
# first, check if clusterType is specified
die "ERROR: you have to specify -clusterType parameter. Should be either genome or falcon or falcon1\n" if ($clusterType eq "");
# second check if clusterType is either genome or falcon
die "ERROR: -clusterType must be either genome or falcon or falcon1. You specified $clusterType\n" if ( ! (($clusterType eq "genome") || ($clusterType eq "falcon1") || ($clusterType eq "falcon") ) );
# third check if the script is executed at the given $clusterType
die "ERROR: you gave clusterType $clusterType but you execute the code on $ENV{'HOSTNAME'}\n" if ($clusterType ne $ENV{'HOSTNAME'});

#customize the $fileServer and $workhorse and $hub variable depending upon the clusterType:
if ($clusterType eq 'genome'){
    $fileServer = $fileServerGenome;
    $workhorse = $workhorseGenome;
	 $hub = $bigClusterHub;
}elsif($clusterType eq 'falcon1'){
    $workhorse = $workhorseSlurm;
    $fileServer = $fileServerSlurm;
    $hub = $bigClusterSlurm;
}elsif($clusterType eq 'falcon'){
    $workhorse = $workhorseLSF;
    $fileServer = $fileServerLSF;
    $hub = $bigClusterLSF;
}
print "FILESERVER: $fileServer  WORKHORSE $workhorse  HUB $hub\n";


&usage(1) if (scalar(@ARGV) != 1);
$secondsStart = `date "+%s"`;
chomp $secondsStart;
($DEF) = @ARGV;

$inclHap = "";
$inclHap = "-inclHap" if ($opt_inclHap);
&loadDef($DEF);
&checkDef();

$filterPsl = 1 if (exists $defVars{'FILTERPSL'} && $defVars{'FILTERPSL'} == 1);
if ($filterPsl == 1) {
	print "Will Filter PSL for sequence identity and entropy. \n";
	# check if the seq id, entropy and winsize is given
	die "ERROR: $DEF doesn't specify WIN_SIZE\n" if ( ! exists $defVars{'WIN_SIZE'});
	die "ERROR: $DEF doesn't specify SEQ_IDENT\n" if ( ! exists $defVars{'SEQ_IDENT'});
	die "ERROR: $DEF doesn't specify MIN_ENTROPY\n" if ( ! exists $defVars{'MIN_ENTROPY'});
}else{
	print "NO PSL filtering. \n";
}

$patchChains = 1 if (exists $defVars{'PATCHCHAIN'} && $defVars{'PATCHCHAIN'} == 1);
if ($patchChains == 1) {
	print "Will patch Chains. \n";
	# check if the parameters are given
	die "ERROR: $DEF doesn't specify CHAIN_MINSCORE\n" if ( ! exists $defVars{'CHAIN_MINSCORE'});
	die "ERROR: $DEF doesn't specify GAPMAXSIZE_T\n" if ( ! exists $defVars{'GAPMAXSIZE_T'});
	die "ERROR: $DEF doesn't specify GAPMAXSIZE_Q\n" if ( ! exists $defVars{'GAPMAXSIZE_Q'});
	die "ERROR: $DEF doesn't specify GAPMINSIZE_T\n" if ( ! exists $defVars{'GAPMINSIZE_T'});
	die "ERROR: $DEF doesn't specify GAPMINSIZE_Q\n" if ( ! exists $defVars{'GAPMINSIZE_Q'});
	die "ERROR: $DEF doesn't specify NUM_JOBS\n" if ( ! exists $defVars{'NUM_JOBS'});
	die "ERROR: $DEF doesn't specify PATCHBLASTZ_K\n" if ( ! exists $defVars{'PATCHBLASTZ_K'});
	die "ERROR: $DEF doesn't specify PATCHBLASTZ_L\n" if ( ! exists $defVars{'PATCHBLASTZ_L'});
	die "ERROR: $DEF doesn't specify PATCHBLASTZ_W\n" if ( ! exists $defVars{'PATCHBLASTZ_W'});
}else{
	print "NO chain patching. \n";
}

$cleanChains = 1 if (exists $defVars{'CLEANCHAIN'} && $defVars{'CLEANCHAIN'} == 1);
if ($cleanChains == 1) {
	print "Will clean the Chains ";
	if (exists $defVars{'CLEANCHAIN_PARAMETERS'}) {
		print "with parameters: $defVars{'CLEANCHAIN_PARAMETERS'}\n";
	}else{
		print "\n";
	}
}else{
	print "NO chain cleaning. \n";
}


if ($opt_doNotRescoreSubNets) {
	$rescoreSubNets = "";
	print "chainNet will NOT compute real subnet scores but will approximate them. \n";
}else{
	my $matrix = $defVars{'BLASTZ_Q'} ? "-scoreScheme=$defVars{BLASTZ_Q} " : "";
	my $linearGap = $opt_chainLinearGap ? "-linearGap=$opt_chainLinearGap" : "-linearGap=$defaultChainLinearGap";
	my $seq1Dir = $defVars{'SEQ1_CTGDIR'} || $defVars{'SEQ1_DIR'};
	my $seq2Dir = $defVars{'SEQ2_CTGDIR'} || $defVars{'SEQ2_DIR'};
	$rescoreSubNets = " -rescore $matrix $linearGap -tNibDir=$seq1Dir -qNibDir=$seq2Dir ";
	print "chainNet will compute real subnet scores (parameters: $rescoreSubNets \n";
}

$chainingQueue = $defVars{'CHAININGQUEUE'} if (exists $defVars{'CHAININGQUEUE'});
die "ERROR: variable CHAININGQUEUE in DEF $chainingQueue is neither long/medium/short\n" if (! ($chainingQueue eq "long" || $chainingQueue eq "medium" || $chainingQueue eq "short"));

my $seq1IsSplit = (`wc -l < $defVars{SEQ1_LEN}` <=
		   $HgAutomate::splitThreshold);
my $seq2IsSplit = (`wc -l < $defVars{SEQ2_LEN}` <=
		   $HgAutomate::splitThreshold);

# Undocumented option for quickly generating a README from DEF:
if ($opt_readmeOnly) {
  $splitRef = $opt_swap ? $seq2IsSplit : $seq1IsSplit;
  &swapGlobals() if $opt_swap;
  &dumpDownloadReadme("/tmp/README.txt");
  exit 0;
}

my $date = `date +%Y-%m-%d`;
chomp $date;
$buildDir = $defVars{'BASE'} ||
  "$HgAutomate::clusterData/$tDb/$HgAutomate::trackBuild/blastz.$qDb.$date";

if ($opt_swap) {
  my $inChain = &getAllChain("$buildDir/axtChain");
  if (! defined $inChain) {
    die "-swap: Can't find $buildDir/axtChain/[$tDb.$qDb.]all.chain[.gz]\n" .
        "which is required for -swap.\n";
  }
  $swapDir = "$HgAutomate::clusterData/$qDb/$HgAutomate::trackBuild/blastz.$tDb.swap";
  &HgAutomate::mustMkdir("$swapDir/axtChain");
  $splitRef = $seq2IsSplit;
  &HgAutomate::verbose(1, "Swapping from $buildDir/axtChain/$inChain\n" .
	      "to $swapDir/axtChain/$qDb.$tDb.all.chain.gz .\n");
} else {
  if (! -d $buildDir) {
    &HgAutomate::mustMkdir($buildDir);
  }
  if (! $opt_blastzOutRoot && $stepper->stepPrecedes($stepper->getStartStep(), 'chainRun')) {
    &enforceClusterNoNo($buildDir, 'blastz/chain/net build directory (or use -blastzOutRoot)');
  }
  $splitRef = $seq1IsSplit;
  &HgAutomate::verbose(1, "Building in $buildDir\n");
}

if (! -e "$buildDir/DEF") {
  &HgAutomate::run("cp $DEF $buildDir/DEF");
}

 
# When running -swap, swapGlobals() happens at the end of the chainMerge step.
# However, if we also use -continue with some step later than chainMerge, we
# need to call swapGlobals before executing the remaining steps.
if ($opt_swap &&
    $stepper->stepPrecedes('chainMerge', $stepper->getStartStep())) {
  &swapGlobals();
}

$stepper->execute();

$secondsEnd = `date "+%s"`;
chomp $secondsEnd;
my $elapsedSeconds = $secondsEnd - $secondsStart;
my $elapsedMinutes = int($elapsedSeconds/60);
$elapsedSeconds -= $elapsedMinutes * 60;

HgAutomate::verbose(1,
	"\n *** All done !  Elapsed time: ${elapsedMinutes}m${elapsedSeconds}s\n");
HgAutomate::verbose(1,
	" *** Make sure that goldenPath/$tDb/vs$QDb/README.txt is accurate.\n")
  if ($stepper->stepPrecedes('load', $stepper->getStopStep()));
HgAutomate::verbose(1,
	" *** Add {chain,net}$QDb tracks to trackDb.ra if necessary.\n")
  if ($stepper->stepPrecedes('net', $stepper->getStopStep()));
HgAutomate::verbose(1,
	"\n\n");

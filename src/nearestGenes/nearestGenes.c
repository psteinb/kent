// MH: added flag -NameFile to output symbols and names for genes;


// adapted from nearest_gene.c of 'dark' ('dark' version is probably OBSOLETE)

/* version 0.13

-------------------------------------------------------------------------------
tag each bed file interval with the name and distance to the closest gene.

you can do this with single genes (transcripts):

select concat(name,".",alignID),chrom,txStart,txEnd,strand from knownGene order by chrom,txStart,txEnd into outfile ...;
sort -k 2,2 -k 3,3n -k 4,4n /tmp/... >! hg16.knownGene.loci

or (much better) with gene loci - see hg16.b200i200.nl.README or bdgpJill.README

!!! if correcting output by hand to resolve multi-locus overlap 
!!! remember to update both inLocus anand inStrand !!!

-------------------------------------------------------------------------------

to do:
- actually introducing the binsearch can be EASY. think about it...
  just find the vertex range within the chrom to paste!
- in-code to do comments marked with @@@

notes:
- assuming given bed intervals are half open
- ended up not using the fact that the input bed files are
   sorted per chromosome entries. Can be made more efficient!
- gene list entries outside of CHROM_NAME are ignored.
  bed  file entries outside of CHROM_NAME cause program to abort.
*/


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"

void usage()
/* Explain usage and exit. */
{
errAbort(
  "nearestGenes - Print closest upstream, intersecting, and downstream genes as well as distance away\n"
  "usage:\n"
  "    nearestGenes geneList bedInterval -NameFile=file\n"
  "options:\n"
  "  -TSS - Calculate distance away relative to TSS\n"
  "  -hw2 - Report only the single closest gene as defined in hw2\n"
  "  -NameFile=file - output symbols and names for genes; requires a filename containing input like this: uc009vjk.1      DQ575955        Homo sapiens cDNA FLJ45445 fis, clone BRSSN2013696"
  );
}

struct optionSpec options[] = {
   {"TSS",OPTION_BOOLEAN},
   {"hw2",OPTION_BOOLEAN},
	{"NameFile",OPTION_STRING},
   {NULL, 0},
};

bool TSS = FALSE;
bool hw2 = FALSE;
char* NameFile;
struct hash *ID2SymName;

// ----------------------------------------------------------------------------

// we will demand bed/psl fields to EXACTLY match these 

char *CHROM_NAME[] = { "unused", // less confusing
					   "chr1", "chr2", "chr3", "chr4" , "chr5", "chr6", "chr7",
					   "chr8", "chr9", "chr10", "chr11", "chr12", "chr13", 
					   "chr14", "chr15", "chr16", "chr17", "chr18", "chr19", 
					   "chr20", "chr21", "chr22", "chrX", "chrY", "chrM" };
#define CHROM_ITEMS 25  // indexed from 1

/* drosophila melanogaster centered 

char *CHROM_NAME[] = { "unused", // less confusing
			   "chr2L", "chr2R", "chr2h", "chr3L" , "chr3R", "chr3h", "chr4",
			   "chrU", "chrX", "chrXh", "chrYh" }; // includes chrU
#define CHROM_ITEMS 11  // indexed from 1
*/

struct chromnode {
  int size;    // no. of segments
  int bases;   // total length in bases
  int *from;   // from[size]
  int *to;     // to[size]
  char *feature;// feature[size] = strand
  char **name;  // name[size] = knownGene.name
};

#define PLUSSTRAND  1
#define MINUSSTRAND 0

#define min(a,b) ( (a) < (b) ? (a) : (b) )

#define max(a,b) ( (a) > (b) ? (a) : (b) )

// ----------------------------------------------------------------------------

void exiterr(char *str)
{ 
  fprintf(stderr,"Error: %s\n",str);
  exit(1);
}

void *callocordie(int elt_count, int elt_size)
{
  void *ptr;

  if ((ptr = calloc(elt_count,elt_size))==NULL)
    exiterr("failed to allocate memory.");
  
  return(ptr);
}

// instead of original from kent
char *cloneStringGill(char *s)
{
  int len;
  char *t;

  if (s==NULL)
    return(NULL);
  len = strlen(s);
  t = callocordie(len+1,sizeof(char));
  strcpy(t,s);
  return(t);
}


int chrom_no(char *chrom_name)
// we match against CHROM_NAME
// anything else, including, e.g. chrM, chr3_random, etc. returns -1
{
  int i;

  for (i=1; i<= CHROM_ITEMS; i++)
    if (strcmp(chrom_name,CHROM_NAME[i])==0)
      return(i);

  return(-1);
}


// ----------------------------------------------------------------------------

void init_chrom(struct chromnode *chrom)
{
  int i;

  for (i=0; i<= CHROM_ITEMS; i++) // [0] nulled but not used
    {
      chrom[i].size = 0;
      chrom[i].bases = 0;
      chrom[i].from = NULL;
      chrom[i].to = NULL;
      chrom[i].feature = NULL;
      chrom[i].name = NULL;
    }
}

void count_sizes(struct chromnode *chrom, char *genefilename)
{
  FILE *genefile;
  char buf[80];
  int from,to,chrmi;

  if ((genefile = fopen(genefilename,"r")) == NULL)
    exiterr("open gene file for 1st read failed.");
  
  for (;;)
    {

      if(fscanf(genefile,"%s %d %d %*c\n",buf,&from,&to)==EOF)
//      if(fscanf(genefile,"%*s %s %d %d %*c\n",buf,&from,&to)==EOF)
		break;  // we dont care about the interval names now

      if((chrmi = chrom_no(buf))>0)
		{
		  chrom[chrmi].bases += (to-from); // half open intervals!
		  chrom[chrmi].size++;
// 		  printf("%s %d %d %d\n",buf,chrmi,from,to);
		}

    }

  fclose(genefile);
}

void allocate_sizes(struct chromnode *chrom)
{
  int i, tot_base, tot_vert;

  fprintf(stderr,"allocating...\nchrom\tgenes\t\tbases\t    bases/gene\n");
  tot_base = tot_vert = 0;
  for (i=1; i<= CHROM_ITEMS; i++)
    {
      fprintf(stderr,"%s\t%8d\t%9d\t%4d\n", CHROM_NAME[i], chrom[i].size,chrom[i].bases,
			  (chrom[i].size==0 ? 0 : chrom[i].bases/chrom[i].size));
      if (chrom[i].size>0)
		{
		  chrom[i].from = (int *)   callocordie(chrom[i].size,sizeof(int));
		  chrom[i].to   = (int *)   callocordie(chrom[i].size,sizeof(int));
		  chrom[i].feature = (char *)   callocordie(chrom[i].size,sizeof(char));
		  chrom[i].name = (char **) callocordie(chrom[i].size,sizeof(char *));
		  tot_vert += chrom[i].size;
		  tot_base += chrom[i].bases;
		  chrom[i].size = 0; // incremented back in next stage
		}
    }
  fprintf(stderr,"total:\t%8d\t%9d\n",tot_vert,tot_base);
  fprintf(stderr,"\n");
}


void load_vectors(struct chromnode *chrom, char *genefilename)
{
  FILE *genefile;
  char chrmbuf[80],namebuf[80],strand;
  int i,from,to,chrmi;

  if ((genefile = fopen(genefilename,"r")) == NULL)
    exiterr("open gene file for 2nd read failed.");
  
  for (;;)
    {

//      if(fscanf(genefile,"%s %s %d %d %c\n",namebuf,chrmbuf,&from,&to,&strand)==EOF)
      if(fscanf(genefile,"%s %d %d %s %c\n",chrmbuf,&from,&to,namebuf,&strand)==EOF)
		break;

      chrmi = chrom_no(chrmbuf);
      
      if (chrmi>0)
		{
		  i = chrom[chrmi].size++;
//		  printf("%s  %s %d %d %c\n",chrmbuf,namebuf,from,to,strand);
		  chrom[chrmi].from[i] = from;
		  chrom[chrmi].to[i]   = to;
		  chrom[chrmi].feature[i] = strand;
		  chrom[chrmi].name[i] = cloneStringGill(namebuf);

		}
    }

  fclose(genefile);
}

// ----------------------------------------------------------------------------

void print_track(struct chromnode *chrom, char *bedfilename)
// we give three answers (and \N,0 when none is found)
// 1 gene with max intersecting bases with interval
// 2 gene closest not-overlapping downstream of interval
// 3 gene closest not-overlapping upstream of interval
{
  FILE *bedfile;
  char chrmbuf[80],ivalbuf[80];
  char *name;
  int i,from,to,j;
  int j1, bp1, j2, bp2, j3, bp3, bp;
  int len1, len2, len3;
  char *g1, *g2, *g3;
  g1 = g2 = g3 = NULL;
  if ((bedfile = fopen(bedfilename,"r")) == NULL)
    exiterr("open bed file failed.");
  
  for (;;)
    {
      
      if(fscanf(bedfile,"%s %d %d %s\n",chrmbuf,&from,&to,ivalbuf)==EOF)
		break;
      i = chrom_no(chrmbuf);
//      printf("%s  %d %d     %d\n",chrmbuf,from,to,chrom[i].size);
      if (i <0)
		errAbort("bed file contains unrecognized chromosome name: %s", chrmbuf);
      if (chrom[i].size==0)
		exiterr("bed interval in chromosome devoid of gene data.");
      
	  j1 = j2 = j3 = -1;
	  bp1 = bp2 = bp3 = 0;
	  len1 = len2 = len3 = 99999999;
      // find closest gene - (inefficient!!)
      // from, to are bed's start, end
      // chrom[i] is array of all genes on chrom i
      // chrom[i].from[j] is the start of the jth gene on chrom i
      for (j=0; j<chrom[i].size; j++)
		{
		  bp = min(chrom[i].to[j],to)-max(chrom[i].from[j],from); //intersection?
		  if (bp>0)
			{
			  if(!hw2)
			    {
			    if (bp1>0)
				fprintf(stderr,"%s intersects more than one gene locus (%s,%s).\n",
						ivalbuf,chrom[i].name[j1],chrom[i].name[j]);
			    if (bp>bp1)
				{
				  bp1 = bp;
				  j1 = j;
				}
			    }
			  else
			    {
			    // we can intersect more than one gene entry (isoform)
			    if(bp1 == 0)
			      {
			      // No other has intersected yet
			      bp1 = bp;
			      j1 = j;
			      len1 = chrom[i].to[j]-chrom[i].from[j];
			      g1 = chrom[i].name[j]; 
			      }
			    else
			      {
			      // already have intersection
			      if(((chrom[i].to[j]-chrom[i].from[j]) < len1) || (((chrom[i].to[j]-chrom[i].from[j]) == len1) && (strcmp(chrom[i].name[j], g1) < 0)))
				{
			        bp1 = bp;
			        j1 = j;
			        len1 = chrom[i].to[j]-chrom[i].from[j];
			        g1 = chrom[i].name[j]; 
					
				}

			      }
			    }
			}
		  else
			if ((bp=chrom[i].from[j]-to)>=0) // gene downstream of interval?
			  {
				if (!TSS)
				  { 
				  if (j2<0 || bp<bp2)
				    { 
					bp2 = bp;
					j2 = j;
					len2 = chrom[i].to[j]-chrom[i].from[j];
					g2 = chrom[i].name[j];
				    }
				  else if ((bp == bp2) && hw2)
				    {
				    if (((chrom[i].to[j]-chrom[i].from[j]) < len2) || (((chrom[i].to[j]-chrom[i].from[j]) == len2) && (strcmp(chrom[i].name[j], g2) < 0)))
				      {
					bp2 = bp;
					j2 = j;
					len2 = chrom[i].to[j]-chrom[i].from[j];
					g2 = chrom[i].name[j];

				      } 
				    } 
				  }
				else
				  {
				  int tss;
					if(chrom[i].feature[j] == '+')
					  tss = chrom[i].from[j];
					else
					  tss = chrom[i].to[j];
				    if(j2<0 || tss-to<bp2) {
				      bp2 = tss-to;
				      j2 = j;
				    }
				  }
// in principle we could also search for ties in ups/dow
			  }
			else
			  {
				bp = from - chrom[i].to[j]; // gene must be upstream of interval
				if(!TSS)
				  {
				    if (j3<0 || bp<bp3)
				      {
					bp3 = bp;
					j3 = j;
					len3 = chrom[i].to[j]-chrom[i].from[j];
					g3 = chrom[i].name[j];
				      }
				    else if ((bp == bp3) && hw2)
				      {
				      if (((chrom[i].to[j]-chrom[i].from[j]) < len3) || (((chrom[i].to[j]-chrom[i].from[j]) == len3) && (strcmp(chrom[i].name[j], g3) < 0)))
				        {
					bp3 = bp;
					j3 = j;
					len3 = chrom[i].to[j]-chrom[i].from[j];
					g3 = chrom[i].name[j];
				        } 
				      } 
				  }
				else
				  {
				  int tss;
 				  if(chrom[i].feature[j] == '+')
                                     tss = chrom[i].from[j];
                                  else
                                     tss = chrom[i].to[j];
				  if(j3<0 || from-tss<bp3) {
				    bp3 = from-tss;
				    j3 = j;
				    }
				  }
			  }

		}


// print order is 3,1,2 or : gene upstream, gene intersecting, gene downstream
    if(!hw2) {
      printf("%s\t%d\t%d\t%s", chrmbuf, from, to, ivalbuf);
		
	  	if (j3<0) {
			if (strlen(NameFile) > 0) {
				printf("\t\\N\t\\N");
			}
			printf("\t%s\t%s\t%d", "\\N", "\\N", bp3);
		} else {
			// get symbol and name		
			if (strlen(NameFile) > 0) {
//				printf("\nID ( %s )\n",chrom[i].name[j3]);
				name = hashFindVal(ID2SymName, chrom[i].name[j3]);
				if (name == NULL) {
					printf("\t\\N\t\\N");
				}else{
					printf("\t%s", name);			// name consists of two \tab separated fields (symbol, name)
//					printf("GET \t(%s)\n", (char*)hashFindVal(ID2SymName, chrom[i].name[j3]));
				}
			}
			printf("\t%s\t%c\t%d", chrom[i].name[j3], chrom[i].feature[j3], bp3);
		}

	  	if (j1<0) {
			if (strlen(NameFile) > 0) {
				printf("\t\\N\t\\N");
			}
			printf("\t%s\t%s\t%d", "\\N", "\\N", bp1);
	  	}else{
			// get symbol and name		
			if (strlen(NameFile) > 0) {
				name = hashFindVal(ID2SymName, chrom[i].name[j1]);
				if (name == NULL) {
					printf("\t\\N\t\\N");
				}else{
					printf("\t%s", name);
				}
			}
			printf("\t%s\t%c\t%d", chrom[i].name[j1], chrom[i].feature[j1], bp1);
		}

		if (j2<0) {
			if (strlen(NameFile) > 0) {
				printf("\t\\N\t\\N");
			}
			printf("\t%s\t%s\t%d", "\\N", "\\N", bp2);
	  	}else{
			// get symbol and name		
			if (strlen(NameFile) > 0) {
				name = hashFindVal(ID2SymName, chrom[i].name[j2]);
				if (name == NULL) {
					printf("\t\\N\t\\N");
				}else{
					printf("\t%s", name);
				}
			}
			printf("\t%s\t%c\t%d", chrom[i].name[j2], chrom[i].feature[j2], bp2);
		}
	  	printf("\n");
    } else {
      printf("%s\t%d\t%d\t%s", chrmbuf, from, to, ivalbuf);
      if(j1 >= 0)
	printf("\t%s", chrom[i].name[j1]);
      else if((j2 >= 0) && (bp2 > bp3))
	printf("\t%s", chrom[i].name[j2]);
      else if((j3 >= 0) && (bp3 > bp2))
	printf("\t%s", chrom[i].name[j3]);
      else
	errAbort("Are up and downstream really the same? %d %d", bp2, bp3);
      printf("\n");
      }
    }

  fclose(bedfile);

// print results

//  for (i=1; i<=CHROM_ITEMS; i++)
//    for (j=0; j<chrom[i].size; j++)
//      if (chrom[i].feature[j]>0)
//	printf("%s\t%d\n",chrom[i].name[j],chrom[i].feature[j]);

}

// ----------------------------------------------------------------------------



// ----------------------------------------------------------------------------

void readNameFile(char *filename) {
  FILE *file;
  ID2SymName = hashNew(0);
  char ID[80],name[1000];
  if ((file = fopen(filename,"r")) == NULL)
    exiterr("open namefile file failed.");
  
  for (;;)  {
		// read something like "uc001aal.1      OR4F5   olfactory receptor, family 4, subfamily F,"
		if(fscanf(file,"%s\t",ID)==EOF)
			break;
		if(fgets(name,1000,file)==NULL)
			break;

		// remove \n at end
		name[strlen(name)-1] = '\0';
		hashAdd(ID2SymName, ID, cloneStringZ(name,strlen(name)));
//		printf("add %s -->   %s  ( %s ) \n",ID,(char*)hashFindVal(ID2SymName, ID),ID);
	}
	return;
}





int main(int argc, char *argv[])
// add documentation

{
  struct chromnode chrom[CHROM_ITEMS+1];
  // 0 unused; 1-22 chrom; 23=X; 24=Y; 25=X

  optionInit(&argc, argv, options);
  TSS = optionExists("TSS");
  hw2 = optionExists("hw2");
  NameFile = optionVal("NameFile", "");			// requires a filename containing input like this: uc009vjk.1      DQ575955        Homo sapiens cDNA FLJ45445 fis, clone BRSSN2013696.

  if (argc!=3)
    usage();

 	// read the name file if -NameFile=** given
	if (strlen(NameFile) > 0) {
  		fprintf(stderr,"reading NAMEFILE %s .... ",NameFile);	
		readNameFile(NameFile);
		fprintf(stderr,"done\n");
	}

// initializing
  init_chrom(chrom);
 
// counting sizes
  count_sizes(chrom,argv[1]);

// allocating vectors
  allocate_sizes(chrom);

// load vectors
  load_vectors(chrom,argv[1]);

// core BED file laoded, we continue to intersect with est BED file

  print_track(chrom, argv[2]);

// we don't bother freeing chrom

  return(0);
}

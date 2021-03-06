# alleleFreqs.sql was originally generated by the autoSql program, which also 
# generated alleleFreqs.c and alleleFreqs.h.  This creates the database representation of
# an object which can be loaded and saved from RAM in a fairly 
# automatic way.

#Allele Frequencies from HapMap
CREATE TABLE alleleFreqs (
    rsId varchar(255) not null,	#  rs			rs2104604
    chrom varchar(255) not null,	#  chrom			Chr1
    chromStart int not null,	#  pos			101809619
    strand char(1) not null,	#  strand		+
    assembly varchar(255) not null,	#  build			ncbi_b34
    center varchar(255) not null,	#  center		bcm
    protLSID varchar(255) not null,	# protLSID
    assayLSID varchar(255) not null,	# assayLSID
    panelLSID varchar(255) not null,	# panelLSID
    majAllele char(1) not null,	#  major_allele		G
    majCount int not null,	#  major_allele_count	120
    majFreq float not null,	#  major_allele_freq	1
    minAllele char(1) not null,	#  minor_allele		T
    minCount int not null,	#  minor_allele_count	0
    minFreq float not null,	#  minor_allele_freq	0
    total int not null,	#  total			120
              #Indices
    INDEX(rsId)
);

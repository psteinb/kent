/* gtexUi - GTEx (Genotype Tissue Expression) tracks */

/* Copyright (C) 2015 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */
#ifndef GTEXUI_H
#define GTEXUI_H

/* Color scheme */
#define GTEX_COLORS                     "colorScheme"
#define GTEX_COLORS_RAINBOW             "rainbow"

/* Color scheme from GTEX papers and portal */
#define GTEX_COLORS_GTEX                "gtex"
#define GTEX_COLORS_DEFAULT             GTEX_COLORS_GTEX

/* Data transform */
    // WARNING: this also appears in JS
#define GTEX_LOG_TRANSFORM              "logTransform"
#define GTEX_LOG_TRANSFORM_DEFAULT      TRUE

/* Viewing limits */
    // WARNING: this also appears in JS
#define GTEX_MAX_LIMIT                  "maxLimit"
#define GTEX_MAX_LIMIT_DEFAULT          300
/* TODO: Get default from gtexInfo table */

/* Sample selection and comparison */
    // WARNING: this also appears in JS
#define GTEX_SAMPLES                    "samples"
#define GTEX_SAMPLES_ALL                "all"
#define GTEX_SAMPLES_COMPARE_SEX        "sex"
#define GTEX_SAMPLES_DEFAULT            GTEX_SAMPLES_ALL

#define GTEX_SAMPLES_COMPARE_AGE        "age"
#define GTEX_COMPARE_AGE_YEARS          "years"
#define GTEX_COMPARE_AGE_DEFAULT        50

    // WARNING: this also appears in JS
#define GTEX_COMPARISON_DISPLAY         "comparison"
#define GTEX_COMPARISON_MIRROR          "mirror"
#define GTEX_COMPARISON_DIFF            "difference"
#define GTEX_COMPARISON_DEFAULT         GTEX_COMPARISON_DIFF

/* Graph type */
#define GTEX_GRAPH              "graphType"
#define GTEX_GRAPH_RAW          "raw"
#define GTEX_GRAPH_NORMAL       "normalized"
#define GTEX_GRAPH_DEFAULT      GTEX_GRAPH_RAW

/* Tissue filter */
#define GTEX_TISSUE_SELECT      "tissues"

/* Gene filter */
#define GTEX_CODING_GENE_FILTER                 "codingOnly"
#define GTEX_CODING_GENE_FILTER_DEFAULT         FALSE

/* Hide exons */
#define GTEX_SHOW_EXONS         "showExons"
#define GTEX_SHOW_EXONS_DEFAULT FALSE

/* Suppress whiteout behind graph (to show highlight and blue lines) */
#define GTEX_NO_WHITEOUT         "noWhiteout"
#define GTEX_NO_WHITEOUT_DEFAULT        FALSE

/* Item name is gene symbol, accession, or both */
#define GTEX_LABEL                "label"
#define GTEX_LABEL_SYMBOL         "name"
#define GTEX_LABEL_ACCESSION      "accession"
#define GTEX_LABEL_BOTH           "both"
#define GTEX_LABEL_DEFAULT  GTEX_LABEL_SYMBOL

/* Identify GTEx gene track as it uses special trackUI. 
 * NOTE: trackDb must follow this naming convention unless/until there is
 * a new trackType.
 */ 
#define GTEX_GENE_TRACK_BASENAME        "gtexGene"

boolean gtexIsGeneTrack(char *trackName);
/* Identify GTEx gene track so custom trackUi CGI can be launched */

char *gtexGeneTrackUiName();
/* Refer to Body Map CGI if suitable */

void gtexPortalLink(char *geneId);
/* print URL to GTEX portal gene expression page using Ensembl Gene Id*/

boolean gtexGeneBoxplot(char *geneId, char *geneName, char *version, 
                                boolean doLogTransform, struct tempName *pngTn);
/* Create a png temp file with boxplot of GTEx expression values for this gene. 
 * GeneId is the Ensembl gene ID.  GeneName is the HUGO name, used for graph title;
 * If NULL, label with the Ensembl gene ID */

#endif /* GTEXUI_H */

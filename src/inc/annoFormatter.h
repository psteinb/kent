/* annoFormatter -- aggregates, formats and writes output from multiple sources */

#ifndef ANNOFORMATTER_H
#define ANNOFORMATTER_H

#include "annoRow.h"
#include "annoStreamer.h"
#include "options.h"

// The real work of aggregating and formatting data is left to
// subclass implementations.  The purpose of this module is to provide
// an interface for communication with other components of the
// annoGratorQuery framework, and simple methods shared by all
// subclasses.

struct annoFormatterOption
/* A named and typed option and its value. */
    {
    struct annoFormatterOption *next;
    struct optionSpec spec;
    void *value;
    };

struct annoFormatter
/* Generic interface to aggregate data fields from multiple sources and write
 * output. */
    {
    struct annoFormatter *next;

    // Public methods
    struct annoFormatterOption *(*getOptions)(struct annoFormatter *self);
    void (*setOptions)(struct annoFormatter *self, struct annoFormatterOption *options);
    /* Get and set output options */

    void (*initialize)(struct annoFormatter *self, struct annoGratorQuery *query);
    /* Initialize output (header, etc) and set query pointer */

    void (*collect)(struct annoFormatter *self, struct annoStreamer *source, struct annoRow *rows);
    /* Collect data from one source */

    void (*discard)(struct annoFormatter *self);
    /* Discard data collected so far (filter failure) */

    void (*formatOne)(struct annoFormatter *self);
    /* Aggregate all sources' data for a single primarySource item into output: */

    void (*close)(struct annoFormatter **pSelf);
    /* End of input; finish output, close connection/handle and free self. */

    // Private members -- callers are on the honor system to access these using only methods above.
    struct annoFormatterOption *options;
    struct annoGratorQuery *query;
    };

// ---------------------- annoFormatter default methods -----------------------

struct annoFormatterOption *annoFormatterGetOptions(struct annoFormatter *self);
/* Return supported options and current settings.  Callers can modify and free when done. */

void annoFormatterSetOptions(struct annoFormatter *self, struct annoFormatterOption *newOptions);
/* Free old options and use clone of newOptions. */

void annoFormatterFree(struct annoFormatter **pSelf);
/* Free self. This should be called at the end of subclass close methods, after
 * subclass-specific connections are closed and resources are freed. */

#endif//ndef ANNOFORMATTER_H

/* annoStreamDb -- subclass of annoStreamer for database tables */

/* Copyright (C) 2013 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#ifndef ANNOSTREAMDB_H
#define ANNOSTREAMDB_H

#include "annoStreamer.h"

struct annoStreamer *annoStreamDbNew(char *db, char *table, struct annoAssembly *aa,
				     struct asObject *asObj, int maxOutRows);
/* Create an annoStreamer (subclass) object from a database table described by asObj. */

#endif//ndef ANNOSTREAMDB_H

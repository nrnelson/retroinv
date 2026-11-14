/*
 * TEWIPE.H - removing the engine's files from a data directory, for the
 * unit tests (TEWIPE.C).
 *
 * Kept apart from the rest of the engine scaffolding (TENGH.H) so that
 * the client tests, whose near data is nearly full, link only this.
 */
#ifndef TEWIPE_H
#define TEWIPE_H

#include "COMPAT.H"

/* Remove, under the prefix dir (as eng_open takes one), every table and
   memo file, every index, the scratch names of their packs, reindexes,
   creates and copies, the marker ENG_EDITMARK, the commit mark
   ENG_COPYMARK and the CSV import's scratch index; then every name of
   the NULL-ended list extra (NULL for none), each with its export
   scratch names (its extension replaced by ENG_CSVSCRATCH and the digit
   of each tag).  A missing file is no
   error; a name too long for a DOS path is skipped. */
void te_remove(const char *dir, const char *const *extra);

#endif

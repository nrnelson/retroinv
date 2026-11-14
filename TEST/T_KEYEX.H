/*
 * T_KEYEX.H - what the two source files of the T_KEYEX test share: the
 * fixture table of T_KEYEX2.C.
 *
 * The test is split in two compilation units, T_KEYEX.C (the checks,
 * main) and T_KEYEX2.C (the fixture), because Watcom C 6.5 runs out of
 * compiler memory on it as one file.
 */
#ifndef T_KEYEX_H
#define T_KEYEX_H

#include "COMPAT.H"
#include "DBF.H"

/* Create KXFIX.DBF in the scratch directory and leave it open in *out
   (NULL on failure). */
int make_fixture(dbf **out);

/* Store number text s in N field f of t's buffer as it stands,
   right-justified; the text must have the field's decimals and fit its
   width.  The text goes straight into the record, so values wider than
   an i32 can be stored. */
int put_num_text(dbf *t, u16 f, const char *s);

#endif

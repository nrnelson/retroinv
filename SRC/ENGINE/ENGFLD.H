/*
 * ENGFLD.H - field numbers and widths of the schema as compile-time
 * constants.
 *
 * ENG_FIELD(ITEMS, QTY) is the 0-based number of QTY in ITEMS.DBF,
 * ENG_NFIELDS(ITEMS) the table's field count and ENG_MAXNFIELDS the
 * largest count; table names in ENG_FIELD and ENG_NFIELDS are the ENG_T_
 * names without the prefix.  All three come from the rows of SCHTAB.H
 * through the enumeration below, so they cost no data.
 *
 * A file that defines ENGFLD_WIDTHS before it includes this header also
 * gets ENG_FW_t_name and ENG_FD_t_name, the width and the decimals of
 * each field of the tables listed below, and ENG_KL_x, the key length
 * of each index (x is the ENG_X_ name without the prefix), as
 * enumeration constants for compile-time checks.
 *
 * Only the files that use the macros include this header, after
 * ENGINE.H, and only those that need the widths ask for them: Watcom C
 * 6.5 runs out of memory compiling the largest programs when every
 * includer of ENGINE.H also reads the enumerations.
 */
#ifndef ENGFLD_H
#define ENGFLD_H

/* ENG_FB_t before the fields of table t, ENG_FN_t_name for each field,
   ENG_FE_t after them. */
#define SCH_TABLE(t, memo, seq, firstx, nindex) ENG_FB_##t,
#define SCH_FIELD(t, name, type, len, dec, flags) ENG_FN_##t##_##name,
#define SCH_END(t) ENG_FE_##t,
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
enum eng_fnum {
#include "SCHTAB.H"
    ENG_FN_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

#define ENG_FIELD(t, name)  ((u16)(ENG_FN_##t##_##name - ENG_FB_##t - 1))
#define ENG_NFIELDS(t)      ((u16)(ENG_FE_##t - ENG_FB_##t - 1))

/* Fields of the widest table; SCHEMA.C checks at compile time that no
   table has more. */
#define ENG_MAXNFIELDS      ENG_NFIELDS(ITEMS)

/* ENG_FW_t_name and ENG_FD_t_name for each field of the tables whose
   widths some file reads (ENG_FWD_t not empty: all but MAINT, AUDIT and
   SEQ), ENG_KL_x for each index, when ENGFLD_WIDTHS is defined.  Leaving
   the others out keeps the enumeration within Watcom C 6.5's compiler
   memory in the files that read it.  A file that defines ENGFLD_ITEMS
   instead gets those of ITEMS alone, and no ENG_KL_x: ITEMS.C, which
   needs no more, compiles near the end of that memory. */
#ifdef ENGFLD_ITEMS
#define ENGFLD_WIDTHS
#endif
#ifdef ENGFLD_WIDTHS
#define ENG_FWD(fw, fd, len, dec)       fw = (len), fd = (dec),
#define ENG_FWD_ITEMS                   ENG_FWD
#ifdef ENGFLD_ITEMS
#define ENG_FWD_LOCS(fw, fd, len, dec)
#define ENG_FWD_CATS(fw, fd, len, dec)
#define ENG_FWD_LTYPES(fw, fd, len, dec)
#define ENG_FWD_LOANS(fw, fd, len, dec)
#define ENG_FWD_USERS(fw, fd, len, dec)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#else
#define ENG_FWD_LOCS                    ENG_FWD
#define ENG_FWD_CATS                    ENG_FWD
#define ENG_FWD_LTYPES                  ENG_FWD
#define ENG_FWD_LOANS                   ENG_FWD
#define ENG_FWD_USERS                   ENG_FWD
#define SCH_INDEX(x, t, expr, keylen, unique, dup) ENG_KL_##x = (keylen),
#endif
#define ENG_FWD_MAINT(fw, fd, len, dec)
#define ENG_FWD_AUDIT(fw, fd, len, dec)
#define ENG_FWD_SEQ(fw, fd, len, dec)
#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags) \
    ENG_FWD_##t(ENG_FW_##t##_##name, ENG_FD_##t##_##name, len, dec)
#define SCH_END(t)
#define SCH_SEED(t, code, name)
enum eng_fwidth {
#include "SCHTAB.H"
    ENG_FW_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED
#endif

#endif

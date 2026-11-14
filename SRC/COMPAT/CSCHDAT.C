/*
 * CSCHDAT.C - the descriptions of the schema that only an open of the
 * data directory reads, defined outside DGROUP: every field's name,
 * dBASE type, width and decimals in file order (table after table, in
 * ENG_T_ order), the key expression of every index, and the rows a new
 * code table (CATS.DBF, LTYPES.DBF) is seeded with.  The rows come
 * from SRC/ENGINE/SCHTAB.H.  With them lie the numbered texts of
 * SRC/ENGINE/TXTAB.H (ENG_TX_), which a call copies out only when it
 * shows one.
 *
 * This module holds nothing but these FARD definitions; CSCHEMA.C reads
 * them through FARX declarations and copies them out.  Under Turbo C
 * 2.01, which cannot define far data, the module is compiled with
 * -zRCSCH_FAR -zSCSCH_GRP -zTCODE: its data segment becomes CSCH_FAR in
 * group CSCH_GRP and class CODE, so TLINK places it among the code
 * segments, before DGROUP.  Class FAR_DATA would not do: TLINK places
 * that after _STACK, where the near heap and the stack of the small and
 * medium models grow.  Any other data defined here would land in the same
 * segment and could not be read as near data.
 *
 * A Windows program cannot keep far data (COMPAT.H, FARD) and binds this
 * data, all but csch_stbl, as a resource instead, which the host tool
 * CSCHBIN.C writes from this module (CSCHDAT.H gives its layout).
 * Compiled for Windows, the module defines only csch_stbl, in DGROUP,
 * one byte a seed row naming its table: the walk that numbers a row
 * among its table's reads it without locking the resource, and
 * eng_seed_row gives the caller a table to index by even when the
 * resource cannot be read.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENGTX_STORE
#include "ENGTX.H"

/* The YYYYMMDD text of D field d, built from functions dBASE III Plus
   has (it has no DTOS); used by the MAINT1 and LOANS2 keys. */
#define SCH_YMD(d) "STR(YEAR(" d "),4)+RIGHT(STR(MONTH(" d ")+100,3),2)" \
                   "+RIGHT(STR(DAY(" d ")+100,3),2)"

#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)

#ifndef COMPAT_WIN16
#define SCH_FIELD(t, name, type, len, dec, flags) #name,
const char FARD csch_fname[][DBF_NAMELEN + 1] = {
#include "SCHTAB.H"
};
#undef SCH_FIELD

#define SCH_FIELD(t, name, type, len, dec, flags) type,
const char FARD csch_ftype[] = {
#include "SCHTAB.H"
};
#undef SCH_FIELD

#define SCH_FIELD(t, name, type, len, dec, flags) (u8)(len),
const u8 FARD csch_flen[] = {
#include "SCHTAB.H"
};
#undef SCH_FIELD

#define SCH_FIELD(t, name, type, len, dec, flags) (u8)(dec),
const u8 FARD csch_fdec[] = {
#include "SCHTAB.H"
};
#undef SCH_FIELD

/* One array per key expression: csch_x_ITEMS1 and so on. */
#define SCH_FIELD(t, name, type, len, dec, flags)
#undef SCH_INDEX
#define SCH_INDEX(x, t, expr, keylen, unique, dup) \
    const char FARD csch_x_##x[] = expr;
#include "SCHTAB.H"
#undef SCH_INDEX
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#else
#define SCH_FIELD(t, name, type, len, dec, flags)
#endif

#undef SCH_SEED
#define SCH_SEED(t, code, name) (u8)ENG_T_##t,
const u8 FARD csch_stbl[] = {
#include "SCHTAB.H"
};
#undef SCH_SEED

#ifndef COMPAT_WIN16
#define SCH_SEED(t, code, name) code,
const char FARD csch_scode[][ENG_LTYPELEN + 1] = {
#include "SCHTAB.H"
};
#undef SCH_SEED

#define SCH_SEED(t, code, name) name,
const char FARD csch_sname[][ENG_CODENAMELEN + 1] = {
#include "SCHTAB.H"
};

/* The texts, and the size of each with its NUL, in number order. */
#define TX(name, text) text,
const struct eng_txs FARD csch_tx = {
#include "TXTAB.H"
};
#undef TX
#define TX(name, text) (u8)sizeof(text),
const u8 FARD csch_txlen[] = {
#include "TXTAB.H"
};
#undef TX
#endif

#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

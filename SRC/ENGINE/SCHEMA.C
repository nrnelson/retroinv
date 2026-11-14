/*
 * SCHEMA.C - the parts of the schema every request uses: the tables and
 * indexes with their names and numbers, and the ENG_F_ flags of every
 * field, which tell the protocol and the audit how to treat it.
 *
 * The rows come from SCHTAB.H; the descriptions only an open needs
 * (field names, types and widths, key expressions, seed rows) are kept
 * outside DGROUP by SRC/COMPAT/CSCHDAT.C.  Everything here is constant
 * data indexed by the ENG_T_ and ENG_X_ numbers of ENGINE.H.
 *
 * The limits of ENGINE.H and ENGFLD.H that the rows must keep (record,
 * key and name lengths, field counts, the default field lists, the memo
 * count, the AUDIT and ITEM FIND widths) are checked here at compile
 * time, by enumerations and declarations that cost no data.  Each block
 * of rows defines the five row macros it reads SCHTAB.H with and
 * undefines them after it.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENGFLD_WIDTHS
#include "ENGFLD.H"

/* Shorter names for the flag combinations of SCHTAB.H. */
#define RO                      ENG_F_RO
#define UP                      ENG_F_UPPER
#define HID                     ENG_F_HIDDEN
#define LST                     ENG_F_LIST
#define NOA                     ENG_F_NOAUDIT
#define MOV                     ENG_F_AUDMOVE
#define PAR                     ENG_F_AUDPAR
#define REQ                     ENG_F_REQ
#define NNG                     ENG_F_NONNEG
#define LON                     ENG_F_AUDLOAN

/* Table names, each written once: an ID table's SEQ_NAME is its name. */
#define SCH_TABLE(t, memo, seq, firstx, nindex) \
    static const char tn_##t[] = #t;
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
#include "SCHTAB.H"
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* The seq argument of a row, 1 or 0, as the SEQ_NAME pointer. */
#define SEQ_1(t)                tn_##t
#define SEQ_0(t)                NULL

#define SCH_TABLE(t, memo, seq, firstx, nindex) \
    { tn_##t, SEQ_##seq(t), ENG_NFIELDS(t), firstx, memo, nindex },
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
static const eng_tdef tables[ENG_NTABLES] = {
#include "SCHTAB.H"
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* Number of the first field of each table in fflags.  ENG_FB_t is the
   count of the enumerators before it: the fields of the tables before
   t, and their ENG_FB_ and ENG_FE_, two for each of the ENG_T_t tables
   before it. */
#define SCH_TABLE(t, memo, seq, firstx, nindex) \
    (u8)(ENG_FB_##t - 2 * ENG_T_##t),
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
static const u8 ffirst[ENG_NTABLES] = {
#include "SCHTAB.H"
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags) flags,
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
static const u16 fflags[] = {
#include "SCHTAB.H"
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup) \
    { #x, keylen, ENG_T_##t, (dup) + 1 },
#define SCH_SEED(t, code, name)
static const eng_xdef indexes[ENG_NINDEX] = {
#include "SCHTAB.H"
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* ---------------------------------------------------------------------
 * The schema limits, checked at compile time.
 * --------------------------------------------------------------------- */

/* For each table t: RL_t, its record length with the deletion flag,
   and LW_t and LC_t, the summed width and the count of its ENG_F_LIST
   fields (the default field list of eng_fieldlist). */
#define SCH_TABLE(t, memo, seq, firstx, nindex) RL_##t = 1
#define SCH_FIELD(t, name, type, len, dec, flags) + (len)
#define SCH_END(t) ,
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
enum sch_reclen {
#include "SCHTAB.H"
    RL_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

#define SCH_TABLE(t, memo, seq, firstx, nindex) LW_##t = 0
#define SCH_FIELD(t, name, type, len, dec, flags) \
    + (((flags) & LST) ? (len) : 0)
#define SCH_END(t) ,
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
enum sch_listw {
#include "SCHTAB.H"
    LW_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

#define SCH_TABLE(t, memo, seq, firstx, nindex) LC_##t = 0
#define SCH_FIELD(t, name, type, len, dec, flags) + (((flags) & LST) ? 1 : 0)
#define SCH_END(t) ,
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
enum sch_listn {
#include "SCHTAB.H"
    LC_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* TF_t, the first index of table t, or -1. */
#define SCH_TABLE(t, memo, seq, firstx, nindex) TF_##t = (firstx),
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
enum sch_firstx {
#include "SCHTAB.H"
    TF_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* KS_x, the key bytes of index x and of the indexes before it in its
   table.  KC_x follows the KS_ of the index before x, so it is that sum
   plus one; the sum starts again at the first index of each table. */
#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup) \
    KC_##x, KS_##x = (ENG_X_##x == TF_##t ? 0 : KC_##x - 1) + (keylen),
#define SCH_SEED(t, code, name)
enum sch_keysum {
    KS_START = 0,
#include "SCHTAB.H"
    KS_END
};
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* Each table's record, its name, its field count and its default field
   list fit their limits. */
#define SCH_TABLE(t, memo, seq, firstx, nindex) \
    && RL_##t <= ENG_MAXREC && sizeof #t - 1 <= ENG_TBLNAMELEN && \
    ENG_NFIELDS(t) <= ENG_MAXNFIELDS && LW_##t <= ENG_MAXRWIDTH && \
    LC_##t <= ENG_MAXFLIST
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
extern char sch_tables_ok[1];
extern char sch_tables_ok[(1
#include "SCHTAB.H"
                           ) ? 1 : 2];
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* Each index's key and name, and the keys of each table, fit their
   limits. */
#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup) \
    && (keylen) <= ENG_MAXKEYLEN && KS_##x <= ENG_MAXKEYS && \
    sizeof #x - 1 <= ENG_IXNAMELEN && !(unique) == !((dup) + 1)
#define SCH_SEED(t, code, name)
extern char sch_indexes_ok[1];
extern char sch_indexes_ok[(1
#include "SCHTAB.H"
                            ) ? 1 : 2];
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* ENG_NMEMO counts the tables with a memo file. */
#define SCH_TABLE(t, memo, seq, firstx, nindex) + (memo)
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
extern char sch_memo_ok[1];
extern char sch_memo_ok[(0
#include "SCHTAB.H"
                         == ENG_NMEMO) ? 1 : 2];
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* Every AUDIT value fits ENG_AUDVAL. */
#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags) \
    && (ENG_T_##t != ENG_T_AUDIT || (len) <= ENG_AUDVAL)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup)
#define SCH_SEED(t, code, name)
extern char sch_audit_ok[1];
extern char sch_audit_ok[(1
#include "SCHTAB.H"
                          ) ? 1 : 2];
#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED

/* The fields ITEM FIND searches fit ENG_FINDFLD. */
extern char sch_find_ok[1];
extern char sch_find_ok[(ENG_FW_ITEMS_MFR <= ENG_FINDFLD &&
                         ENG_FW_ITEMS_MODEL <= ENG_FINDFLD &&
                         ENG_FW_ITEMS_DESCR <= ENG_FINDFLD &&
                         ENG_FW_ITEMS_TAGS <= ENG_FINDFLD &&
                         ENG_FW_ITEMS_SERIALNO <= ENG_FINDFLD &&
                         ENG_FW_ITEMS_ITEMTYPE <= ENG_FINDFLD) ? 1 : 2];

const eng_tdef *eng_table_def(int t)
{
    if (t < 0 || t >= ENG_NTABLES)
        return NULL;
    return &tables[t];
}

const eng_xdef *eng_index_def(int x)
{
    if (x < 0 || x >= ENG_NINDEX)
        return NULL;
    return &indexes[x];
}

u16 eng_field_flags(int t, u16 f)
{
    if (t < 0 || t >= ENG_NTABLES || f >= tables[t].nfields)
        return 0;
    return fflags[ffirst[t] + f];
}

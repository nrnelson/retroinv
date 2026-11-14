/*
 * DBFINT.H - definitions shared by the modules of SRC/DBF only.
 *
 * The DBF_ open-mode call that every SRC/DBF module opens files with,
 * the memo file constants of DBT.C and PACK.C's memo copy, and the .NDX
 * page layout that NDX.C reads and changes and PACK.C's REINDEX writes,
 * with the stored expression length and unique flag that REINDEX copies.
 * Code outside SRC/DBF uses DBF.H alone; make lint refuses this header
 * anywhere but SRC/DBF and the unit tests in TEST.
 */
#ifndef DBFINT_H
#define DBFINT_H

#include "COMPAT.H"
#include "DBF.H"

/* ---------------------------------------------------------------------
 * Opening files.
 * --------------------------------------------------------------------- */

/* Open path with the cf_ access and sharing of the DBF_ open mode
   (DBF_RDWR: read and write, deny read and write; DBF_RDONLY: read,
   deny write) into *h.  DBF_EARG for any other mode, else the cf_open
   result through dbf_map_cf. */
int dbf_cf_open(const char *path, int mode, int *h);

/* ---------------------------------------------------------------------
 * Memo files.
 * --------------------------------------------------------------------- */
#define DBT_TERM        0x1A    /* memo terminator, written twice       */
#define DBT_FILL        0x00    /* header and gap bytes                 */

/* Highest block number whose byte offset still fits in a u32. */
#define DBT_MAXBLOCK    ((u32)0xFFFFFFFFUL / (u32)DBT_BLOCK)

/* ---------------------------------------------------------------------
 * Index files.
 *
 * NDX.C's header comment describes the format; these are its offsets
 * and the sizes derived from them.
 * --------------------------------------------------------------------- */

/* Header offsets. */
#define NDX_H_ROOT      0
#define NDX_H_PAGES     4
#define NDX_H_COUNTS    8       /* bytes 0..7: rewritten at flush       */
#define NDX_H_KEYLEN    12
#define NDX_H_KPP       14
#define NDX_H_TYPE      16
#define NDX_H_ENTSZ     18
#define NDX_H_UNIQUE    23
#define NDX_H_EXPR      24

/* Page layout: the count, then entries of lower page, record, key. */
#define NDX_P_ENTRIES   4
#define NDX_E_LOWER     0
#define NDX_E_RECNO     4
#define NDX_E_KEY       8
#define NDX_PTRSIZE     4       /* the rightmost-child slot's used part */

/* Entry size for a key of kl bytes: the key after the two u32 fields,
   rounded up to a multiple of 4; and the keys per page for an entry
   size of es bytes. */
#define NDX_ENTSZ(kl)   (((kl) + NDX_E_KEY + 3u) / 4u * 4u)
#define NDX_KPP(es)     ((NDX_PAGE - NDX_P_ENTRIES) / (es))

#define NDX_MAXENT      NDX_ENTSZ(NDX_MAXKEY)   /* largest entry        */
#define NDX_MAXDEPTH    16      /* levels on a path, root and leaf      */

/* Highest page number whose byte offset still fits in a u32. */
#define NDX_MAXPAGE     ((u32)0xFFFFFFFFUL / (u32)NDX_PAGE)

/* Address of entry i of the page at b, where x points at a struct whose
   entsz member is the entry size (NDX.C's index handle, PACK.C's index
   builder), and of entry 0 (written separately, as Turbo C warns about a
   multiplication by a constant 0).  Macros rather than functions
   because they sit in the binary searches and page edits run for every
   key, where a call would cost more than the addressing. */
#define NDX_ENT(x, b, i) \
    ((b) + NDX_P_ENTRIES + (size_t)(i) * (size_t)(x)->entsz)
#define NDX_ENT0(b)     ((b) + NDX_P_ENTRIES)

/* Length of x's key expression as its header stores it: ndx_expr's
   text followed by the trailing blanks ndx_expr leaves out. */
u16 ndx_exprlen(ndx *x);

/* 1 when the header's unique flag is set, as dBASE's SET UNIQUE ON
   writes it; ndx_create writes 0.  The flag is reported only: ndx_insert
   does not enforce it. */
int ndx_unique(ndx *x);

#endif

/*
 * ENGCODE.H - the code tables of the engine (CODES.C): the categories
 * and the location types.
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGCODE_H
#define ENGCODE_H

/* ---------------------------------------------------------------------
 * Code tables (CODES.C): the categories (CATS.DBF, codes of ENG_CATLEN)
 * and the location types (LTYPES.DBF, codes of ENG_LTYPELEN), named by
 * t, ENG_T_CATS or ENG_T_LTYPES (any other is ENG_EINTERNAL).
 *
 * Both hold a code, a name of ENG_CODENAMELEN and a sort order, in that
 * order.  Codes are 1..width characters A-Z and 0-9, upper-cased on
 * input and unique among the live rows; a code that is not well formed
 * names no row: a lookup gives ENG_ENOTFOUND, an ADD ENG_EARG naming the
 * code field.  SORTORD is never blank: a blank value on ADD or SET is
 * ENG_EARG naming SORTORD.  A table never holds more than ENG_MAXCODES
 * records, deleted ones included, and has no index: a lookup reads its
 * records in order.  DEL is refused while a live item has the category
 * (CATEGORY) or a live location the type (LTYPE); neither field has an
 * index, so DEL reads the whole of ITEMS.DBF or LOCS.DBF, an accepted
 * exception to the bounded-work rule for a rare command.  AUDIT.REC_ID
 * of both is the record number.
 * --------------------------------------------------------------------- */
#define ENG_CODESORTDEF 50      /* SORTORD of an ADD without one        */

/* Copy code upper-cased into out (width + 1 bytes).  1 if it is
   1..width characters A-Z and 0-9 (a code table's code), with loc set
   the first one A-Z and '-' allowed after it (a location's code), else
   0. */
int eng_code_norm(const char *code, u16 width, int loc, char *out);

/* Load the live row code of table t into its buffer; ENG_ENOTFOUND. */
int eng_code_get(eng *e, int t, const char *code, u32 *recno);

/* CAT LIST, LTYPE LIST: the record numbers of the live rows ordered by
   SORTORD then code; recnos holds ENG_MAXCODES.  Load each with
   eng_rec_read. */
int eng_code_list(eng *e, int t, u32 *recnos, u16 *n);

/* CAT ADD code CAT_NAME= [SORTORD=], LTYPE ADD code LT_NAME= [SORTORD=]
   (default ENG_CODESORTDEF; ENG_EDUP for a live code).  When the table
   already holds ENG_MAXCODES records, the first deleted one is
   overwritten and made live (audited as an I row with its record
   number); ENG_ECONSTR when all are live.  SET code [name=][SORTORD=],
   and DEL code (ENG_ECONSTR while the code is in use). */
int eng_code_add(eng *e, int t, const char *code, const eng_pair *p,
                 u16 n);
int eng_code_set(eng *e, int t, const char *code, const eng_pair *p,
                 u16 n);
int eng_code_del(eng *e, int t, const char *code);

#endif

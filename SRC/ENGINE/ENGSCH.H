/*
 * ENGSCH.H - the far part of the schema copied out (SRC/COMPAT/CSCHEMA.C):
 * field layouts, key expressions and seed rows, which only the open and
 * its tests read.
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only those files pay the compiler memory for it (Watcom C 6.5
 * runs out of memory on the largest test programs otherwise).  Include
 * it after ENGINE.H.
 */
#ifndef ENGSCH_H
#define ENGSCH_H

/* Each call below copies out of far data, which a Windows program reads
   from its resource (SRC/COMPAT/CSCHDAT.H); a copy fails when out is too
   small, when eng_schema_fault fails it, or when the resource cannot be
   read.

   Field f of table t as the schema describes it (name, type, len, dec;
   offset 0), for creating and checking tables; ENG_EINTERNAL out of
   range or when the copy fails. */
int eng_schema_field(int t, u16 f, dbf_field *out);

/* The key expression of index x into out (size bytes, at
   least NDX_MAXEXPR + 1); ENG_EINTERNAL out of range or when the copy
   fails. */
int eng_index_expr(int x, char *out, u16 size);

/* Fault injection, for tests only; programs leave eng_schema_fault at
   its initial null value.  When it is set, every copy out of the far
   schema data first calls it, and a nonzero result makes the copy fail
   as one that does not fit. */
typedef int (*eng_schema_fault_fn)(void);
extern eng_schema_fault_fn eng_schema_fault;

/* Seed rows of the code tables, CATS.DBF then LTYPES.DBF:
   seed i, 0 <= i < eng_seed_rows(), gives its table (*t, ENG_T_), code
   (ENG_LTYPELEN + 1 bytes), name (ENG_CODENAMELEN + 1) and sort order,
   its place among its table's seeds from 1; out of range *t is -1, the
   texts empty and the sort order 0.  A text whose copy fails is empty,
   which the caller takes as a failure of that row (no seed text is
   empty); the rows it wrote before stay. */
u16 eng_seed_rows(void);
void eng_seed_row(u16 i, int *t, char *code, char *name, u16 *sortord);

#endif

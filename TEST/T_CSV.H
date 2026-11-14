/*
 * T_CSV.H - what the source files of the T_CSV test share: the engine
 * under test, the file writers and export runners of T_CSV.C, the lines
 * of the export fixture, which the import phases of T_CSV2.C read back
 * in, and the import runners of T_CSV2.C, which T_CSV3.C uses.
 *
 * The test is split in three compilation units, T_CSV.C (the export
 * phases, main), T_CSV2.C (the concurrent exports and the import
 * phases) and T_CSV3.C (LOCS lines in any order, the round trip of a
 * tree), because Watcom C 6.5 runs out of compiler memory on it as one
 * file.
 */
#ifndef T_CSV_H
#define T_CSV_H

#include "COMPAT.H"
#include "ENGINE.H"

#define LINEMAX     800         /* an expected line                     */

extern eng *g_e;
extern char g_line[LINEMAX];

/* The exported lines of items 1, 2 and 4 of the fixture. */
extern const char row1[];
extern const char row2[];
extern const char row4[];

/* 1 when file name is in the data directory. */
int exists(const char *name);

/* Write the n bytes of s (n 0: strlen(s)) as file name of the data
   directory, replacing it; one check. */
void put_file(const char *name, const char *s, u16 n);

/* Append the n bytes of s (n 0: strlen(s)) to the existing file name of
   the data directory; one check. */
void add_file(const char *name, const char *s, u16 n);

/* 1 when file name holds every live record of table t, in record order,
   as the export must write them, then the Ctrl-Z and nothing more. */
int export_ok(int t, const char *name);

/* Every request of an export of table t to name with tag tag, from
   cursor after on ("" for a new export), a bounded number of requests;
   the last code, the requests made and the rows written. */
int export_all(int t, const char *name, u8 flags, u8 tag,
               const char *after, int *calls, u32 *rows);

/* The counts of the last import. */
extern u16 g_errors;
extern u32 g_added;

/* Import IN.CSV into t; the code, with the counts in *errors and *added
   and the refusals kept for import_err. */
int import(int t, u8 flags, u16 *errors, u32 *added);

/* Check that refusal i of the last import was on line, naming field,
   with text msg (NULL: any). */
void import_err(u16 i, u32 line, const char *field, const char *msg);

/* Import text s (len bytes, 0: strlen(s)) into t, expecting n records
   added. */
void accepted(int t, u8 flags, const char *s, u16 len, u32 n);

/* The path of the live location coded code into g_line, "?" when there
   is none. */
const char *lpath(const char *code);

/* Append to IN.CSV head, spaces and value, len characters in all, then
   end. */
void pad_out(const char *head, u16 len, const char *value,
             const char *end);

/* The phases of T_CSV2.C and T_CSV3.C. */
void test_concurrent(void);
void test_parse(void);
void test_header(void);
void test_import(void);
void test_tree(void);
void test_order(void);
void test_cost(void);
void test_room(void);
void test_fail(void);
void test_trip(const eng_time *now);

#endif

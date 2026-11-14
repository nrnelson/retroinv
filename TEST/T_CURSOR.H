/*
 * T_CURSOR.H - what the two source files of the T_CURSOR test share: the
 * engine, the record helpers of T_CURSOR.C, and the tests of T_CURSO2.C
 * that main runs.  The data directory is the scratch directory of
 * T_FILES.H.
 *
 * The test is split in two compilation units, T_CURSOR.C (round trips,
 * hostile cursors, changes between pages, equal runs, the read limit,
 * reverse record order, bounds, main) and T_CURSO2.C (empty last pages,
 * bad records, a damaged index, walk cursors, IDs by binary search),
 * because Watcom C 6.5 runs out of compiler memory on it as one file.
 */
#ifndef T_CURSOR_H
#define T_CURSOR_H

#include "COMPAT.H"
#include "ENGINE.H"

#define MAXLIST         320     /* recnos one walk or paging collects   */

extern eng *g_e;                /* the engine of fresh                  */
extern u32 g_got[MAXLIST];      /* recnos the pages returned            */
extern const eng_time g_now;    /* the engine's fixed clock             */

/* A fresh, empty data directory; 0 (the check failed) when it cannot be
   opened. */
int fresh(void);

/* Append a record to table t: kv lists field names and values, ending
   with NULL; N values are integers.  Its keys are inserted into every
   index of the table; its record number. */
u32 raw_add(int t, const char *const *kv);

/* Append item id with those fields, as raw_add. */
u32 item_add(u32 id, const char *loc, const char *mfr, const char *model,
             const char *serial);

/* Append user id with the given name, as raw_add (USERS records are
   small, so the long runs fit a 360 KB floppy). */
u32 user_add(u32 id, const char *name);

/* Mark record recno of table t deleted (its index entries stay). */
void raw_delete(int t, u32 recno);

/* One request: open the scan after cursor, take at most page records
   (appended to g_got at *ngot), and leave the cursor in cur
   (ENG_CURSORMAX bytes) when entries are left (else ""). */
int request(eng_scan *s, int t, int ix, const u8 *prefix, u16 plen,
            eng_match_fn match, const char *after, u16 page, u16 *ngot,
            char *cur);

/* The cursor text a test expects for an entry, into out (ENG_CURSORMAX
   bytes). */
void make_cursor(const char *name, const u8 *key, u16 kl, u32 recno,
                 char *out);

/* T_CURSO2.C. */
void test_empty_last(void);
void test_before_and_bad_record(void);
void test_damaged(void);
void test_walk_cursor(void);
void test_id_find(void);

#endif

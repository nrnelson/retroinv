/*
 * T_TABLES.H - what the three source files of the T_TABLES test share:
 * the buffers, the file and open helpers of T_TABLES.C, set_seq of
 * T_TABLE3.C, and the tests of T_TABLE2.C and T_TABLE3.C that main
 * runs.  The data directory is the scratch directory of T_FILES.H.
 *
 * The test is split in three compilation units, T_TABLES.C (the schema,
 * creation, seed and reopen tests, main), T_TABLE2.C (mismatched files,
 * failed schema copies, the edit marker, failed creates, torn appends,
 * lone memo files, files in use, handles, kept and long markers, STATUS
 * D, SEQ rows that move) and T_TABLE3.C (the rebuild of missing indexes
 * and SEQ, code tables that stay empty), because Watcom C 6.5 runs out
 * of compiler memory on larger ones.
 */
#ifndef T_TABLES_H
#define T_TABLES_H

#include "COMPAT.H"
#include "ENGINE.H"
#include "T_FILES.H"

extern char g_why[TFS_WHYSZ];          /* eng_open's failure text      */
extern u8 *g_buf;                      /* HDRMAX bytes for file reads  */

/* The helpers of T_TABLES.C, each described where it is defined. */
void check_seq(eng *e, const char *name, const char *want);
int exists(const char *stem, const char *ext);
void remove_all(void);
void expect_close(eng *e);
void expect_closed(void);
int expect_open(eng **e, int want);
void expect_seek(eng *e, int x, const char *key, u32 want);
void expect_text(eng *e, int t, const char *field, const char *want);
void expect_why(const char *name);
u32 read_file(const char *stem, const char *ext, u16 *got);
void remove_file(const char *stem, const char *ext);
void write_bytes(const char *stem, const char *ext, const char *p,
                 u16 n);
void write_file(const char *stem, const char *ext, const char *text);

/* T_TABLE3.C: set_seq, described where it is defined, and the tests. */
void set_seq(const char *name, i32 v);
void test_rebuild_and_heal(void);
void test_codes_emptied(void);

/* T_TABLE2.C. */
void test_mismatch(void);
void test_edit_marker(void);
void test_marker_kept(void);
void test_index_then_fail(void);
void test_create_fails(void);
void test_torn_append(void);
void test_lone_memo(void);
void test_in_use(void);
void test_handles(void);
void test_kept_repairs(void);
void test_long_markers(void);
void test_items_status(void);
void test_seq_moved(void);

#endif

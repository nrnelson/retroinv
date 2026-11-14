/*
 * T_EDIT.H - what the three source files of the T_EDIT test share: the
 * engine and the helpers of T_EDIT.C, and the tests of T_EDIT2.C and
 * T_EDIT3.C that main runs.  The data directory is the scratch
 * directory of T_FILES.H.
 *
 * The test is split in three compilation units, T_EDIT.C (SEQ rows,
 * audit rows, edits, values, memos, index upkeep, copy and reuse, puts
 * by field number, quiet commits, main), T_EDIT2.C (the ID keys, note
 * lines, the old memo of a commit, the edit marker, batches) and
 * T_EDIT3.C (the calls that write the marker, DBF codes), because
 * Watcom C 6.5 runs out of compiler memory on it as one file.
 */
#ifndef T_EDIT_H
#define T_EDIT_H

#include "COMPAT.H"
#include "ENGINE.H"

extern eng *g_e;                /* the engine of fresh and reopen       */
extern u16 g_f_mfr;             /* the field number of ITEMS MFR        */

/* A fresh data directory with its SEQ rows; the actor is STA1/TESTER.
   0 (the check failed) when it cannot be opened. */
int fresh(void);

/* Close and reopen the same files.  A marker the open kept for the
   repairs of eng_init_data is closed as eng_init_data closes it. */
void reopen(void);

/* Commit a new item with the given ID through the edit layer; its
   record number. */
u32 new_item(u32 id, const char *loc, const char *mfr);

/* Checks that report the call on failure, so each kind costs one
   message text: eng_edit_put, eng_edit_begin and eng_edit_commit give
   ENG_OK, and field name of record recno of table t reads as want. */
void put_ok(const char *field, const char *val);
void begin_ok(int t, u32 recno);
void commit_ok(u32 recid);
void fld_is(int t, u32 recno, const char *name, const char *want);

/* Every index of table t holds exactly one entry per record, whose key
   is the record's key: one check per index, naming the index and
   te_index_rec when it fails. */
void check_indexes(int t);

/* T_EDIT2.C: eng_id_key and eng_key_id; note lines; the old memo of a
   commit; the ENG_EDITMARK marker of changes stopped or failed at every
   disk write; batches. */
void t_idkey(void);
void t_note(void);
void t_oldmemo(void);
void t_marker(void);
void t_batch(void);

/* T_EDIT3.C: the marker of a new change, and the one an open keeps and
   writes afresh, written out to its sector's end with zeros, then cut
   back to its line, as the file layer sees it; the ENG_E code of each
   DBF_E code (eng_dbferr). */
void t_markpad(void);
void t_dbferr(void);

/* T_EDIT3.C: memos saved in parts, staged and written whole. */
void t_stage(void);

#endif

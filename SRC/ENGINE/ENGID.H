/*
 * ENGID.H - ID numbers (IDGEN.C): SEQ.DBF, the search by ID of the
 * tables without an ID index, and the STR(id,8) ID keys.
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 *
 * SEQ.DBF holds one row per ID-bearing table (SEQ_NAME = eng_tdef.seq):
 * ITEMS, LOCS, MAINT, LOANS, AUDIT, USERS; NEXT_VAL is the next ID to
 * issue.
 * Issuing reads the row, increments it, writes and flushes SEQ.DBF
 * before the caller appends the record, so a crash may leave a gap but
 * never a reused ID.  Modules check for duplicates before reserving.
 */
#ifndef ENGID_H
#define ENGID_H

/* Reserve count consecutive IDs of table t; *first receives the first.
   A count of 0 reserves nothing and gives the next ID, as
   eng_seq_peek. */
int eng_seq_next(eng *e, int t, u16 count, u32 *first);

/* The next ID of table t without reserving it. */
int eng_seq_peek(eng *e, int t, u32 *next);

/* Append a missing row (NEXT_VAL 1) for every ID-bearing table. */
int eng_seq_rows(eng *e);

/* For every ID-bearing table, raise NEXT_VAL to one above the largest ID
   present if it is behind.  IDs are appended in increasing order, so the
   largest is the ID of the table's last record (deleted or not).  A last
   record whose ID is blank, not a number, zero or negative is passed
   over for the one before it, stepping down at most ENG_IDSTEP records;
   if none of them has an ID, the records give 0.  For a table whose
   first index is a unique index on its ID (ITEMS1, LOCS1) the last key
   of that index is taken as well. */
int eng_seq_heal(eng *e);

/* Find the record of ID-bearing table t (MAINT, LOANS) whose ID (its
   first field) is id, by a binary search over the record numbers: IDs
   are appended in increasing order and PACK keeps that order, so no
   index on the ID is needed (at most about log2 of the record count
   reads).  The record is left in t's buffer; *recno is its number, or
   0 when no record has the ID or that record is deleted.  A probed
   record whose ID is blank, not a number, zero or negative stands for
   the nearest record below it whose ID can be read, found by stepping
   down at most ENG_IDSTEP records; when there is none the search gives
   ENG_EIO.
   This relies on the operator rule that MAINT.DBF and LOANS.DBF are
   only appended to with ascending IDs (records are never reused or
   renumbered by hand), which TEST/CHKDBF.PY checks. */
#define ENG_IDSTEP      16
int eng_id_find(eng *e, int t, u32 id, u32 *recno);

/* The key STR(id,8) of an ID: 1 when id is 1..ENG_MAXID and key
   (ENG_IDLEN + 1 bytes) holds its ENG_IDLEN characters, right-aligned
   with spaces, and a NUL; else 0 with key empty. */
int eng_id_key(u32 id, char *key);

/* The ID in the len key bytes at key: spaces, then digits, at most
   ENG_MAXID; 0 when they hold anything else. */
u32 eng_key_id(const u8 *key, u16 len);

#endif

/*
 * T_ADMIN.H - what the two source files of the T_ADMIN test share: the
 * engine under test and its directories, the record counts of the data
 * before RESTORE and of the backup, and the helpers of T_ADMIN.C that
 * the fault sweeps and the handle check of T_ADMIN2.C use.
 *
 * The test is split in two compilation units, T_ADMIN.C (PACK, REINDEX,
 * BACKUP, RESTORE, main) and T_ADMIN2.C (the fault sweeps of RESTORE and
 * BACKUP, RESTORE of bad backup sets, the open's FILES= check, a damaged
 * N field), because Watcom C 6.5 runs out of compiler memory on it as
 * one file.
 */
#ifndef T_ADMIN_H
#define T_ADMIN_H

#include "COMPAT.H"
#include "ENGINE.H"
#include "T_FILES.H"

extern const eng_time g_now;

/* The scratch extensions a copy is written under, in the order of the
   kinds .DBF, .DBT and .NDX; no step may leave one behind. */
extern const char *const k_scr[];

extern eng *g_e;
extern char g_bak[TFS_MAXDIR + 1];      /* the backup directory prefix */
extern u32 g_old[ENG_NTABLES];          /* record counts before RESTORE */
extern u32 g_new[ENG_NTABLES];          /* and in the backup            */
extern u32 g_step;                      /* the sweeps' step             */

/* The backup directory's file stem + ext, in one static buffer. */
const char *bpath(const char *stem, const char *ext);

/* Text n, copied out into one static buffer. */
const char *tx(u16 n);

/* eng_admin of op on dir with g_e, its removed count into *removed when
   not NULL. */
int admin(int op, const char *dir, u32 *removed);

/* The data directory spelt another way into p (DOS_PATHMAX bytes): a
   drive's root as the drive alone, any other directory with "." after
   it. */
void data_spelt(char *p);

/* 1 when no scratch file of a copy is left in the directory of prefix
   pre, and no marker in the data directory. */
int no_scratch(const char *pre);

/* 1 when every index of g_e is consistent with its table. */
int indexes_ok(void);

/* The record count of every table of g_e into n. */
void counts(u32 *n);

/* Change the data after the backup: a new item, a new location and a
   loan of item 2. */
void change_data(void);

/* The phases of T_ADMIN2.C; test_looks runs op, BACKUP or RESTORE,
   with each look of its check that the two directories differ
   failing; test_data_gone runs RESTORE with the data directory gone;
   test_put_looks runs eng_put_copies in the backup directory with a
   look failing; test_bad_sets runs RESTORE from a backup set missing a
   file, holding a damaged table or cut short, which each refuse;
   test_damaged runs the open, REINDEX, PACK and ITEM SET over an N
   field damaged on disk. */
void test_damaged(void);
void test_looks(int op);
void test_data_gone(void);
void test_put_looks(void);
void test_bad_sets(void);
void test_restore_faults(void);
void test_backup_faults(void);
void test_handles(void);
void test_markers(void);
void test_pack_faults(void);
#ifdef COMPAT_HOST
void test_close_faults(void);
#endif
void test_init_fail(void);

#endif

/*
 * T_ADMIN.C - unit test for the administration of the data directory
 * (ADMIN.C: PACK, REINDEX, BACKUP, RESTORE) and the open's check of the
 * file handles (TABLES.C), run through a complete engine.
 *
 * usage: T_ADMIN dir bakdir [step]
 *
 * dir and bakdir are two existing directories, or prefixes ending in
 * '\', '/' or ':': the scratch data directory (tfs_set_dir of T_FILES.H)
 * and the backup directory.  Every engine file in both is removed at the
 * start, and those of dir and the saved copies in both at the end;
 * bakdir is left holding a backup of the packed and reindexed data, for
 * TEST/CHKDBF.PY to check.  step (default 1) makes the fault sweeps of
 * RESTORE and BACKUP fail only every step-th disk-changing call.  The
 * clock is fixed.  Checks:
 *   - PACK: the deleted records of every table dropped and counted, the
 *     memo file shrunk with its texts kept, every index rebuilt, items,
 *     locations and maintenance records found again, ON_LOAN kept, one K
 *     row by the request's actor; a second PACK removes nothing;
 *   - REINDEX: an index left stale by a hand edit rebuilt, one K row;
 *   - an N field of an item damaged on disk, "  12  12" (a torn write),
 *     then "********" (dBASE's overflow): the open's repair, REINDEX,
 *     PACK and ITEM SET over it succeed, every index consistent, ITEMS2
 *     ordering it as dBASE does (as 12, then its asterisks after the
 *     other item's spaces);
 *   - the refusals of BACKUP and RESTORE, each before anything changes:
 *     a bad or too long name, the data directory under two spellings, a
 *     directory that does not exist; for RESTORE a table or a memo file
 *     missing, the commit mark of a BACKUP cut short, a damaged table,
 *     a table of another layout and one shorter than its record count;
 *   - BACKUP and RESTORE with the first, then the second, look of the
 *     probe that tells the two directories apart failing (CF_EACCES),
 *     through the data directory spelt another way and through another
 *     directory holding a file of the probe's name: the command fails
 *     as when the probe cannot be made, ENG_EINUSE naming DIR, with no
 *     table or audit row changed, no probe, scratch copy or mark left,
 *     and the file of the probe's name kept;
 *   - RESTORE with the probe's create in the data directory failing as
 *     when that directory is gone: ENG_EIO naming nothing, with the
 *     text of a missing directory;
 *   - the renames into place of eng_put_copies with the look of the
 *     commit mark failing beside a cut-off copy, then with the mark
 *     there and the look of a copy already renamed failing: the error
 *     comes back and the table is kept;
 *   - BACKUP: every file copied byte for byte (AUDIT.DBF one record
 *     short of the K row written afterwards, and SEQ.DBF and the
 *     indexes of AUDIT with it), no scratch file left in either
 *     directory, the K row with the directory; a second BACKUP over the
 *     first; a third whose copy
 *     fails, leaving no partial copy;
 *   - RESTORE whose first rename fails: an error with the engine shut,
 *     the next open finishing the renames from the commit mark;
 *   - RESTORE: the backup's tables back, every index rebuilt from them,
 *     ON_LOAN consistent, the K row;
 *   - RESTORE with each disk-changing call failing, once and from there
 *     on as when the machine stops, and with each close failing once: a
 *     RESTORE that succeeds gives the backup, and a single failure that
 *     leaves the engine open leaves every table as it was, or as in the
 *     backup when it was in the K row; the next open succeeds, with
 *     every table as it was before or every table as in the backup,
 *     every index consistent and no scratch file or marker left, and
 *     RESTORE run again gives the backup;
 *   - BACKUP with the close of its first copy failing, and with its
 *     commit mark's create failing: BACKUP fails, leaving no copy;
 *   - BACKUP with each disk-changing call failing so: the data opens
 *     unchanged, a single failure while copying leaves no copy in the
 *     backup directory, and RESTORE from it gives the new backup when
 *     BACKUP succeeded, else either refuses it as incomplete or gives
 *     the old backup or the new one whole;
 *   - PACK, RESTORE and BACKUP beside the marker of a failed change:
 *     PACK keeps its count line, so the rows it covers are flagged;
 *     RESTORE first makes its repairs, then drops it, flagging none of
 *     the restored rows, and when its commit mark fails the rows stay
 *     flagged; BACKUP makes the repairs before it copies, so the backup
 *     holds the X row, and when they fail it gives the error and copies
 *     nothing, the open after it making them;
 *   - PACK with each disk-changing call failing so: the next open has
 *     every table packed or as it was and every index consistent;
 *   - on the host, REINDEX and PACK with each close failing once: the
 *     engine once closed holds no near block they took;
 *   - REINDEX with each disk-changing call failing once, the repairs of
 *     the reopen among them: a failure leaves the engine either set up
 *     or with no file;
 *   - the open's FILES= check: with handles for one file fewer than the
 *     engine's files and its transient ones, the open fails with
 *     ENG_EHANDLES naming SEQ.DBF, before a table is opened; with
 *     exactly enough it succeeds.
 * Prints each failure and a summary line; exit status 0 when every
 * check passes, 1 when one fails, 2 for a usage error or a directory
 * too long.  The fault sweeps, the refusals of a bad backup set and the
 * FILES= check are in T_ADMIN2.C, as Watcom C 6.5 runs out of compiler
 * memory on one file (T_ADMIN.H).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGADMIN.H"
#include "ENGITEM.H"
#include "ENGLOCS.H"
#include "ENGMAINT.H"
#include "ENGLOAN.H"
#include "ENGTX.H"
/* Host only: the DOS builds count no blocks, and it puts Watcom C 6.5
   out of compiler memory (E084). */
#ifdef COMPAT_HOST
#define TF_MALLOC
#endif
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"
#include "T_ADMIN.H"

const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };
const char *const k_scr[] = { ENG_SCRDBF, ENG_SCRDBT, ENG_SCRNDX };

/* The fields of the last audit row as last_row gives them. */
static const char k_row[] = "ACTION TBL REC_ID FLD NEWVAL STATION USERNAME";

eng *g_e;
char g_bak[TFS_MAXDIR + 1];
u32 g_old[ENG_NTABLES];
u32 g_new[ENG_NTABLES];
u32 g_step = 1;
static char g_path[DOS_PATHMAX];        /* bpath's result              */
static char g_tx[ENG_TXMAX + 1];

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

const char *bpath(const char *stem, const char *ext)
{
    strcpy(g_path, g_bak);
    strcat(g_path, stem);
    strcat(g_path, ext);
    return g_path;
}

const char *tx(u16 n)
{
    eng_tx_copy(n, g_tx);
    return g_tx;
}

/* The last audit row as its k_row fields. */
static const char *last_row(void)
{
    return te_aud(te_naud(), k_row);
}

int admin(int op, const char *dir, u32 *removed)
{
    u32 n;
    int rc;

    rc = eng_admin(g_e, op, dir, &n);
    if (removed != NULL)
        *removed = n;
    return rc;
}

int no_scratch(const char *pre)
{
    char p[DOS_PATHMAX];
    u16 i;
    int t;

    if (tfs_exists(tfs_path(ENG_EDITMARK, "")))
        return 0;
    for (t = 0; t < ENG_NTABLES; t++)
        for (i = 0; i < 2; i++) {
            sprintf(p, "%s%s%s", pre, eng_table_def(t)->name, k_scr[i]);
            if (tfs_exists(p))
                return 0;
        }
    for (t = 0; t < ENG_NINDEX; t++) {
        sprintf(p, "%s%s%s", pre, eng_index_def(t)->name, k_scr[2]);
        if (tfs_exists(p))
            return 0;
    }
    return 1;
}

int indexes_ok(void)
{
    int x;

    for (x = 0; x < ENG_NINDEX; x++)
        if (!te_index_ok(g_e, x))
            return 0;
    return 1;
}

void counts(u32 *n)
{
    int t;

    for (t = 0; t < ENG_NTABLES; t++)
        n[t] = eng_reccount(g_e, t);
}

/* Add an item from spec; its ID, 0 on failure. */
static u32 add_item(const char *spec)
{
    u32 id;
    u16 n;

    n = te_kv(spec);
    if (!TF_EQ(eng_item_add(g_e, te_p, n, &id), ENG_OK))
        return 0;
    return id;
}

/* The memo of item id as text, "?" when it cannot be read. */
static const char *notes(u32 id)
{
    u16 len;

    if (eng_item_notes(g_e, id, &len) != ENG_OK)
        return "?";
    return eng_memobuf(g_e);
}

/* ---------------------------------------------------------------------
 * PACK and REINDEX.
 * --------------------------------------------------------------------- */

/* Locations A1 and A2 (A2 deleted); items 1 and 2 live with notes, item
   1 lent out once, items 3 and 4 deleted, item 4 with notes; two
   maintenance records of item 1, the first deleted.  Item 1's notes are
   rewritten twice, so its old texts take memo blocks no record points
   at. */
static void make_data(void)
{
    u32 m;
    u32 l;
    u16 n;

    te_loc("A1", 0);
    te_loc("A2", 0);
    TF_EQ(add_item("CATEGORY=SYS|MFR=IBM|MODEL=5150|LOC_ID=A1|"
                   "NOTES=first"), 1);
    TF_EQ(add_item("CATEGORY=CPU|MFR=Intel|MODEL=8088|LOC_ID=A1|"
                   "NOTES=two lines\nof notes"), 2);
    TF_EQ(add_item("CATEGORY=DRV|MFR=Tandon|MODEL=TM100"), 3);
    TF_EQ(add_item("CATEGORY=CARD|MFR=Hercules|NOTES=gone"), 4);
    n = te_kv("NOTES=second version of the notes");
    TF_EQ(eng_item_set(g_e, 1, te_p, n), ENG_OK);
    n = te_kv("NOTES=kept notes");
    TF_EQ(eng_item_set(g_e, 1, te_p, n), ENG_OK);
    TF_EQ(eng_item_del(g_e, 3), ENG_OK);
    TF_EQ(eng_item_del(g_e, 4), ENG_OK);
    TF_EQ(eng_loc_del(g_e, "A2"), ENG_OK);
    n = te_kv("MTYPE=CL|SUMMARY=Cleaned");
    TF_EQ(eng_maint_add(g_e, 1, te_p, n, &m), ENG_OK);
    TF_EQ(eng_maint_del(g_e, m), ENG_OK);
    n = te_kv("MTYPE=RP|SUMMARY=Recapped");
    TF_EQ(eng_maint_add(g_e, 1, te_p, n, &m), ENG_OK);
    TF_EQ(m, 2);
    n = te_kv("HOLDER=Jane|DUE_DATE=20261031");
    TF_EQ(eng_loan_out(g_e, 1, ENG_K_LOAN, te_p, n, &l), ENG_OK);
}

static void test_pack(void)
{
    u32 before[ENG_NTABLES];
    u32 dbt;
    u32 removed;
    u32 r;

    make_data();
    counts(before);
    dbt = tfs_size(tfs_path("ITEMS", ".DBT"));
    removed = 0;
    TF_EQ(admin(ENG_ADM_PACK, NULL, &removed), ENG_OK);
    TF_EQ(removed, 4);
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), before[ENG_T_ITEMS] - 2);
    TF_EQ(eng_reccount(g_e, ENG_T_LOCS), before[ENG_T_LOCS] - 1);
    TF_EQ(eng_reccount(g_e, ENG_T_MAINT), before[ENG_T_MAINT] - 1);
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), before[ENG_T_AUDIT] + 1);
    TF_CHECK(tfs_size(tfs_path("ITEMS", ".DBT")) < dbt);
    TF_CHECK(eng_is_open(g_e));
    TF_CHECK(indexes_ok());
    TF_CHECK(no_scratch(tfs_dir()));
    TF_EQ(eng_item_get(g_e, 1, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "ON_LOAN"), "1");
    TF_EQ(eng_item_get(g_e, 2, &r), ENG_OK);
    TF_EQ(r, 2);
    TF_EQ(eng_item_get(g_e, 3, &r), ENG_ENOTFOUND);
    TF_STREQ(notes(1), "kept notes");
    TF_STREQ(notes(2), "two lines\r\nof notes");
    TF_EQ(eng_loc_get(g_e, "A1", &r), ENG_OK);
    TF_EQ(eng_loc_get(g_e, "A2", &r), ENG_ENOTFOUND);
    TF_EQ(eng_maint_get(g_e, 2, &r), ENG_OK);
    TF_EQ(r, 1);
    TF_STREQ(last_row(), "K|AUDIT|0|PACK||STA1|TESTER");

    TF_EQ(admin(ENG_ADM_PACK, NULL, &removed), ENG_OK);
    TF_EQ(removed, 0);
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), before[ENG_T_ITEMS] - 2);
    TF_CHECK(indexes_ok());
}

static void test_reindex(void)
{
    u32 r;

    TF_EQ(te_raw_put(ENG_T_ITEMS, 2, -1, "MFR", "NEC", 0), 0);
    TF_CHECK(!te_index_ok(g_e, ENG_X_ITEMS4));
    TF_EQ(admin(ENG_ADM_REINDEX, NULL, NULL), ENG_OK);
    TF_CHECK(indexes_ok());
    TF_CHECK(no_scratch(tfs_dir()));
    TF_EQ(eng_item_get(g_e, 2, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "MFR"), "NEC");
    TF_STREQ(last_row(), "K|AUDIT|0|REINDEX||STA1|TESTER");
}

/* ---------------------------------------------------------------------
 * BACKUP and RESTORE.
 * --------------------------------------------------------------------- */

void data_spelt(char *p)
{
    size_t n;

    strcpy(p, tfs_dir());
    n = strlen(p);
    if (n >= 2 && p[n - 2] == ':')
        p[n - 1] = '\0';
    else
        strcat(p, ".");
}

/* The refusals both commands share, op being one of them: nothing
   changes. */
static void refusals(int op)
{
    char p[DOS_PATHMAX];

    TF_CHECK(TE_FAIL(admin(op, "", NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DIRNAME)));
    TF_CHECK(TE_FAIL(admin(op, "A B", NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DIRNAME)));
    TF_CHECK(TE_FAIL(admin(op, "A*", NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DIRNAME)));
    TF_CHECK(TE_FAIL(admin(op, "A?", NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DIRNAME)));
    TF_CHECK(TE_FAIL(admin(op, "A\x7F", NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DIRNAME)));
    memset(p, 'D', ENG_MAXDIR);
    p[ENG_MAXDIR] = '\0';
    TF_CHECK(TE_FAIL(admin(op, p, NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DIRNAME)));
    TF_CHECK(TE_FAIL(admin(op, tfs_dir(), NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DATADIR)));
    data_spelt(p);
    TF_CHECK(TE_FAIL(admin(op, p, NULL), ENG_EARG, "DIR",
                     tx(ENG_TX_DATADIR)));
    sprintf(p, "%sNOSUCH", g_bak);
    TF_CHECK(TE_FAIL(admin(op, p, NULL), ENG_ENOTFOUND, "DIR",
                     tx(ENG_TX_NODIR)));
    TF_CHECK(eng_is_open(g_e));
}

/* The engine's file stem + ext of the data directory and of the backup
   are the same bytes. */
static int same(const char *stem, const char *ext)
{
    char p[DOS_PATHMAX];

    strcpy(p, bpath(stem, ext));
    return tfs_same(tfs_path(stem, ext), p);
}

static void test_backup(void)
{
    const eng_tdef *td;
    dbf *d;
    int t;

    refusals(ENG_ADM_BACKUP);
    test_looks(ENG_ADM_BACKUP);
    TF_EQ(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_OK);
    TF_EQ(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_OK);
    /* A copy that fails leaves no partial copy in the backup. */
    tfs_fault(TFS_OP(CF_OP_WRITE), 3, 1, 0);
    TF_CHECK(TE_FAIL(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_EFULL, NULL,
                     tx(ENG_TX_DISKFULL)));
    tfs_fault_off();
    TF_CHECK(no_scratch(tfs_dir()));
    TF_CHECK(no_scratch(g_bak));
    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        if (t != ENG_T_AUDIT && t != ENG_T_SEQ)
            TF_CHECK(same(td->name, ".DBF"));
        if (td->memo)
            TF_CHECK(same(td->name, ".DBT"));
    }
    for (t = 0; t < ENG_NINDEX; t++)
        if (eng_index_def(t)->table != ENG_T_AUDIT)
            TF_CHECK(same(eng_index_def(t)->name, ".NDX"));
    if (TF_EQ(dbf_open(&d, bpath("AUDIT", ".DBF"), DBF_RDONLY), DBF_OK)) {
        TF_EQ(dbf_reccount(d) + 1, eng_reccount(g_e, ENG_T_AUDIT));
        dbf_close(d);
    }
    sprintf(g_path, "K|AUDIT|0|BACKUP|%s|STA1|TESTER", g_bak);
    TF_STREQ(last_row(), g_path);
}

void change_data(void)
{
    u32 l;
    u16 n;

    TF_EQ(add_item("CATEGORY=SW|MFR=Microsoft|MODEL=DOS 3.10"), 5);
    te_loc("B1", 0);
    n = te_kv("HOLDER=Joe|DUE_DATE=20261101");
    TF_EQ(eng_loan_out(g_e, 2, ENG_K_LOAN, te_p, n, &l), ENG_OK);
}

static void test_restore(void)
{
    u32 r;

    change_data();
    counts(g_old);
    refusals(ENG_ADM_RESTORE);
    test_bad_sets();
    TF_EQ(eng_item_get(g_e, 5, &r), ENG_OK);
    /* A failed rename is the reopen's error, so the engine is shut,
       never open with the data restored under an error; the next open
       finishes the renames from the commit mark. */
    tfs_fault(TFS_OP(CF_OP_RENAME), 1, 1, 0);
    TF_CHECK(admin(ENG_ADM_RESTORE, g_bak, NULL) != ENG_OK);
    tfs_fault_off();
    TF_CHECK(!eng_is_open(g_e));
    eng_close(g_e);
    g_e = NULL;
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    TF_EQ(eng_item_get(g_e, 5, &r), ENG_ENOTFOUND);

    TF_EQ(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_OK);
    counts(g_new);
    TF_EQ(g_new[ENG_T_ITEMS], g_old[ENG_T_ITEMS] - 1);
    TF_EQ(g_new[ENG_T_LOCS], g_old[ENG_T_LOCS] - 1);
    TF_EQ(g_new[ENG_T_LOANS], g_old[ENG_T_LOANS] - 1);
    TF_EQ(eng_item_get(g_e, 5, &r), ENG_ENOTFOUND);
    TF_EQ(eng_loc_get(g_e, "B1", &r), ENG_ENOTFOUND);
    TF_EQ(eng_item_get(g_e, 2, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "ON_LOAN"), "0");
    TF_STREQ(notes(1), "kept notes");
    TF_CHECK(same("ITEMS", ".DBF"));
    TF_CHECK(same("ITEMS", ".DBT"));
    TF_CHECK(same("LOANS", ".DBF"));
    TF_CHECK(indexes_ok());
    TF_CHECK(no_scratch(tfs_dir()));
    sprintf(g_path, "K|AUDIT|0|RESTORE|%s|STA1|TESTER", g_bak);
    TF_STREQ(last_row(), g_path);
    test_looks(ENG_ADM_RESTORE);
    test_data_gone();
    test_put_looks();
}

int main(int argc, char **argv)
{
    tf_begin("T_ADMIN");
    if (argc < 3 || argc > 4) {
        printf("usage: T_ADMIN dir bakdir [step]\n");
        return 2;
    }
    if (argc > 3)
        g_step = (u32)atol(argv[3]);
    if (g_step == 0)
        g_step = 1;
    tfs_set_dir(argv[2]);
    strcpy(g_bak, tfs_dir());
    tfs_set_dir(argv[1]);
    te_remove(g_bak, NULL);
    te_remove(tfs_dir(), NULL);
    TF_EQ(te_save(1), 0);
    if (TF_EQ(te_open(&g_e, &g_now), ENG_OK)) {
        test_pack();
        test_reindex();
        test_damaged();
        if (g_e != NULL)
            test_backup();
        TF_EQ(eng_close(g_e), ENG_OK);
        g_e = NULL;
        TF_EQ(te_save(1), 0);
        if (TF_EQ(te_open(&g_e, &g_now), ENG_OK)) {
            test_restore();
            test_restore_faults();
            test_backup_faults();
            test_handles();
            test_markers();
            test_pack_faults();
#ifdef COMPAT_HOST
            test_close_faults();
#endif
            test_init_fail();
        }
    }
    if (g_e != NULL)
        TF_EQ(eng_close(g_e), ENG_OK);
    te_remove(tfs_dir(), NULL);
    TF_EQ(te_save(1), 0);
    return tf_done();
}

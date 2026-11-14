/*
 * T_ADMIN2.C - the second half of the T_ADMIN test (T_ADMIN.H): RESTORE,
 * BACKUP and PACK stopped at each disk-changing call, the looks of
 * BACKUP's and RESTORE's directory check and of the renames into place
 * failing, RESTORE with the data directory gone, REINDEX and PACK with
 * each close failing, the marker runs, REINDEX's repairs failing,
 * RESTORE's refusal of a bad backup set, the open's FILES= check, and an
 * N field damaged on disk.
 * The header comment of T_ADMIN.C lists the checks.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGADMIN.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
#include "ENGMAINT.H"
#include "ENGTX.H"
#define TF_PART
#define TF_FILE "T_ADMIN2"
/* Host only: the DOS builds count no blocks, and it puts Watcom C 6.5
   out of compiler memory (E084). */
#ifdef COMPAT_HOST
#define TF_MALLOC
#endif
#include "TESTFW.H"
#include "T_FILES.H"
#include "TENGH.H"
#include "TEWIPE.H"
#include "T_ADMIN.H"

/* The open-hook state of test_handles: the length of the run of opens
   with no other file call between them, and the run that fails. */
static u16 g_run;
static u16 g_runmax;

static u16 g_bkcreates;         /* BACKUP's creates up to its mark      */

/* The state of copy_close: 0 before the first write, 1 after it, 2 once
   the close after it has failed. */
static int g_ccstate;

/* Every op, closes included, so that the closes are counted apart. */
#define AD_EVERY        (TFS_ALLOPS | TFS_OP(CF_OP_CLOSE))

/* bak_files: save the backup directory's files under the extension .SV
   and the kind's letter, put them back from those (removing the commit
   mark and every scratch copy there), or remove the saved copies. */
#define BK_SAVE         0
#define BK_PUT          1
#define BK_DROP         2

/* Put back the saved state of the data directory (te_save), with no
   leftover of a stopped pack, reindex or copy beside it. */
static void reset_state(void)
{
    te_remove(tfs_dir(), NULL);
    TF_EQ(te_save(0), 0);
}

/* 1 when the tables of g_e but AUDIT and SEQ hold as many records as
   those of a, or all as many as those of b. */
static int whole(const u32 *a, const u32 *b)
{
    u32 got[ENG_NTABLES];
    int ina;
    int inb;
    int t;

    counts(got);
    ina = 1;
    inb = 1;
    for (t = 0; t < ENG_NTABLES; t++) {
        if (t == ENG_T_AUDIT || t == ENG_T_SEQ)
            continue;
        if (got[t] != a[t])
            ina = 0;
        if (got[t] != b[t])
            inb = 0;
    }
    return ina || inb;
}

/* RESTORE with call n of the ops of mask ops failing, once (crash 0)
   or with every later call too (crash 1), from the saved state before
   RESTORE; then the checks that T_ADMIN.C's header comment lists.
   Calls from kcall on are those of the K row.  The engine is closed at
   the end. */
static void fault_case(int ops, u32 n, int crash, u32 kcall)
{
    u32 got[ENG_NTABLES];
    int bak;
    int rc;

    reset_state();
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    tfs_fault(ops, n, crash ? 0 : 1, 0);
    rc = admin(ENG_ADM_RESTORE, g_bak, NULL);
    tfs_fault_off();
    /* Success gives the backup; a single failure that leaves the engine
       open gives the data as it was, or the backup's in the K row. */
    bak = rc == ENG_OK || n >= kcall;
    if ((rc == ENG_OK || !crash) && eng_is_open(g_e) &&
        !TF_CHECK(bak ? whole(g_new, g_new) : whole(g_old, g_old)))
        printf("T_ADMIN: call %lu gives %d with the data %s\n",
               (unsigned long)n, rc, bak ? "as it was" : "restored");
    eng_close(g_e);
    g_e = NULL;
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK)) {
        printf("T_ADMIN: call %lu (%s) leaves no open\n", (unsigned long)n,
               crash ? "crash" : "single");
        return;
    }
    if (!TF_CHECK(whole(g_old, g_new)))
        printf("T_ADMIN: call %lu (%s) leaves a mixed set\n",
               (unsigned long)n, crash ? "crash" : "single");
    TF_CHECK(indexes_ok());
    TF_CHECK(no_scratch(tfs_dir()));
    TF_EQ(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_OK);
    counts(got);
    TF_EQ(got[ENG_T_ITEMS], g_new[ENG_T_ITEMS]);
    TF_EQ(got[ENG_T_LOANS], g_new[ENG_T_LOANS]);
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
}

void test_restore_faults(void)
{
    eng_audrow row;
    u32 calls;
    u32 closes;
    u32 kcall;
    u32 kclose;
    u32 n;

    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    reset_state();
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    change_data();
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    TF_EQ(te_save(1), 0);
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    counts(g_old);
    tfs_fault(AD_EVERY, 0, 0, 0);
    TF_EQ(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_OK);
    closes = tfs_count(CF_OP_CLOSE);
    calls = tfs_calls() - closes;
    tfs_fault_off();
    TF_EQ(eng_close(g_e), ENG_OK);
    /* The number of the first call and of the first close of the K row,
       measured on AUDIT indexes just rebuilt as RESTORE's own. */
    TF_EQ(te_marker_put("AUDIT"), 0);
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    row.action = ENG_A_ADMIN;
    row.fld = "RESTORE";
    row.oldval = NULL;
    row.newval = g_bak;
    tfs_fault(AD_EVERY, 0, 0, 0);
    TF_EQ(eng_audit(g_e, ENG_T_AUDIT, 0, &row), ENG_OK);
    kclose = closes - tfs_count(CF_OP_CLOSE) + 1;
    kcall = calls - (tfs_calls() - tfs_count(CF_OP_CLOSE)) + 1;
    tfs_fault_off();
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    printf("T_ADMIN: RESTORE: %lu disk-changing calls\n",
           (unsigned long)calls);
    for (n = 1; n <= calls; n += g_step) {
        fault_case(TFS_ALLOPS, n, 0, kcall);
        fault_case(TFS_ALLOPS, n, 1, kcall);
    }
    /* Each close failing once, the commit mark's among them. */
    printf("T_ADMIN: RESTORE: %lu closes\n", (unsigned long)closes);
    for (n = 1; n <= closes; n += g_step)
        fault_case(TFS_OP(CF_OP_CLOSE), n, 0, kclose);
    reset_state();
    TF_EQ(te_open(&g_e, &g_now), ENG_OK);
}

/* bak_files of how; returns 0 or the first cf_ code. */
static int bak_files(int how)
{
    static const char *const ext[] = { ".DBF", ".DBT", ".NDX" };
    static const char *const sv[] = { ".SVF", ".SVT", ".SVX" };
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    const char *stem;
    int first;
    int rc;
    int i;
    int k;

    first = 0;
    for (i = 0; i < ENG_NTABLES + ENG_NINDEX; i++)
        for (k = 0; k < 3; k++) {
            if ((i < ENG_NTABLES) == (k == 2))
                continue;
            stem = i < ENG_NTABLES ? eng_table_def(i)->name :
                   eng_index_def(i - ENG_NTABLES)->name;
            sprintf(a, "%s%s%s", g_bak, stem, ext[k]);
            sprintf(b, "%s%s%s", g_bak, stem, sv[k]);
            if (how == BK_SAVE)
                rc = tfs_copy(a, b);
            else if (how == BK_PUT)
                rc = tfs_copy(b, a);
            else
                rc = cf_remove(b);
            if (rc == CF_ENOENT)
                rc = 0;
            if (first == 0)
                first = rc;
            if (how == BK_PUT) {
                sprintf(a, "%s%s%s", g_bak, stem, k_scr[k]);
                cf_remove(a);
            }
        }
    if (how == BK_PUT)
        cf_remove(bpath(ENG_COPYMARK, ""));
    return first;
}

/* BACKUP with call n failing, once (crash 0) or from there on (crash
   1), over the saved backup: the data directory opens unchanged, a
   single failure while copying or of the commit mark's create leaves no
   copy in the backup directory, and RESTORE from it then gives the
   new backup when BACKUP succeeded, else either refuses it as
   incomplete (the commit mark is there) or gives the old backup or the
   new one whole. */
static void backup_case(u32 n, int crash)
{
    u32 data[ENG_NTABLES];
    int before;
    int done;
    int rc;

    reset_state();
    TF_EQ(bak_files(BK_PUT), 0);
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    counts(data);
    tfs_fault(TFS_ALLOPS, n, crash ? 0 : 1, 0);
    done = admin(ENG_ADM_BACKUP, g_bak, NULL) == ENG_OK;
    tfs_fault_off();
    before = !crash && (tfs_count(CF_OP_CREATE) < g_bkcreates ||
                        (tfs_count(CF_OP_CREATE) == g_bkcreates &&
                         tfs_failop() == CF_OP_CREATE));
    eng_close(g_e);
    g_e = NULL;
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    TF_CHECK(whole(data, data));
    TF_CHECK(no_scratch(tfs_dir()));
    if (before && !TF_CHECK(no_scratch(g_bak)))
        printf("T_ADMIN: BACKUP call %lu leaves a copy\n", (unsigned long)n);
    te_snap();
    rc = admin(ENG_ADM_RESTORE, g_bak, NULL);
    if (rc != ENG_OK && !TF_CHECK(!done)) {
        printf("T_ADMIN: BACKUP call %lu (%s) leaves no backup\n",
               (unsigned long)n, crash ? "crash" : "single");
    } else if (rc != ENG_OK) {
        TF_CHECK(te_fail(rc, ENG_ENOTFOUND, "DIR", tx(ENG_TX_SETMISS),
                         TF_FILE, __LINE__));
    } else if (!TF_CHECK(done ? whole(data, data) : whole(data, g_new))) {
        printf("T_ADMIN: BACKUP call %lu (%s) leaves a mixed set\n",
               (unsigned long)n, crash ? "crash" : "single");
    }
    TF_CHECK(indexes_ok());
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
}

/* cf_fault: fail the first close after the first write, once.  In
   BACKUP that is the close of its first copy, whose bytes the host keeps
   all the same, so only the code returned tells the failure. */
static int copy_close(int op)
{
    if (op == CF_OP_WRITE && g_ccstate == 0)
        g_ccstate = 1;
    else if (op == CF_OP_CLOSE && g_ccstate == 1) {
        g_ccstate = 2;
        return CF_EFULL;
    }
    return 0;
}

void test_backup_faults(void)
{
    u32 calls;
    u32 n;

    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    TF_EQ(bak_files(BK_SAVE), 0);
    reset_state();
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_OK);
    calls = tfs_calls();
    tfs_fault_off();
    /* The creates up to the commit mark: those of a BACKUP whose renames
       all fail. */
    tfs_fault(TFS_OP(CF_OP_RENAME), 1, 0, 0);
    TF_CHECK(admin(ENG_ADM_BACKUP, g_bak, NULL) != ENG_OK);
    g_bkcreates = tfs_count(CF_OP_CREATE);
    tfs_fault_off();
    /* A failed close of a copy, and a failed create of the commit mark,
       fail BACKUP and drop its copies. */
    g_ccstate = 0;
    cf_fault = copy_close;
    TF_CHECK(TE_FAIL(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_EFULL, NULL,
                     tx(ENG_TX_DISKFULL)));
    cf_fault = NULL;
    TF_EQ(g_ccstate, 2);
    TF_CHECK(no_scratch(g_bak));
    tfs_fault(TFS_OP(CF_OP_CREATE), g_bkcreates, 1, 0);
    TF_CHECK(TE_FAIL(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_EFULL, NULL,
                     tx(ENG_TX_DISKFULL)));
    tfs_fault_off();
    TF_CHECK(no_scratch(g_bak));
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    printf("T_ADMIN: BACKUP: %lu disk-changing calls\n",
           (unsigned long)calls);
    for (n = 1; n <= calls; n += g_step) {
        backup_case(n, 0);
        backup_case(n, 1);
    }
    TF_EQ(bak_files(BK_PUT), 0);
    TF_EQ(bak_files(BK_DROP), 0);
    reset_state();
    TF_EQ(te_open(&g_e, &g_now), ENG_OK);
}

/* ---------------------------------------------------------------------
 * The looks of the check that the two directories differ, failing.
 * --------------------------------------------------------------------- */

static u16 g_looks;             /* the cf_exists calls so far           */
static u16 g_look;              /* the one fail_look fails              */

/* cf_fault_rd: fail cf_exists number g_look with CF_EACCES, as when
   DOS will not search the directory. */
static int fail_look(int op)
{
    if (op != CF_OP_EXISTS)
        return 0;
    return ++g_looks == g_look ? CF_EACCES : 0;
}

void test_looks(int op)
{
    char spelt[DOS_PATHMAX];
    char plant[DOS_PATHMAX];
    const char *seq;
    int other;
    int rc;

    seq = eng_table_def(ENG_T_SEQ)->name;
    data_spelt(spelt);
    for (other = 0; other < 2; other++)
        for (g_look = 1; g_look <= 2; g_look++) {
            /* Another directory: a file of the probe's name where the
               probe is looked for, so that it is seen and looked for
               again. */
            if (other) {
                strcpy(plant, op == ENG_ADM_BACKUP ?
                       tfs_path(seq, ENG_SCRDBF) : bpath(seq, ENG_SCRDBF));
                TF_EQ(tfs_write_all(plant, "", 0), 0);
            }
            te_snap();
            g_looks = 0;
            cf_fault_rd = fail_look;
            rc = admin(op, other ? g_bak : spelt, NULL);
            cf_fault_rd = NULL;
            if (!te_fail(rc, ENG_EINUSE, "DIR", tx(ENG_TX_INUSE), TF_FILE,
                         __LINE__))
                printf("T_ADMIN: %s look %u through %s\n",
                       op == ENG_ADM_BACKUP ? "BACKUP" : "RESTORE",
                       (unsigned)g_look,
                       other ? "another directory" : "a second spelling");
            TF_CHECK(eng_is_open(g_e));
            if (other) {
                TF_CHECK(tfs_exists(plant));
                cf_remove(plant);
            }
            TF_CHECK(no_scratch(tfs_dir()));
            TF_CHECK(no_scratch(g_bak));
            TF_CHECK(!tfs_exists(tfs_path(ENG_COPYMARK, "")));
            TF_CHECK(!tfs_exists(bpath(ENG_COPYMARK, "")));
        }
}

/* RESTORE with the probe's create failing CF_ENOPATH, as when the data
   directory is gone: ENG_EIO naming nothing, the engine still open. */
void test_data_gone(void)
{
    tfs_fault(TFS_OP(CF_OP_CREATE), 1, 1, CF_ENOPATH);
    TF_CHECK(TE_FAIL(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_EIO, NULL,
                     tx(ENG_TX_NODIR)));
    tfs_fault_off();
    TF_CHECK(eng_is_open(g_e));
}

/* ---------------------------------------------------------------------
 * The looks of the renames into place, failing.
 * --------------------------------------------------------------------- */

/* eng_put_copies in the backup directory with its look number look
   failing (fail_look), the commit mark there when mark and a cut-off
   copy of ITEMS.DBF when cut: the error comes back with ITEMS.DBF
   kept, and the copies and the mark are removed afterwards. */
static void put_look(u16 look, int mark, int cut)
{
    char items[DOS_PATHMAX];
    char mk[DOS_PATHMAX];
    u32 size;

    strcpy(items, bpath("ITEMS", ".DBF"));
    strcpy(mk, bpath(ENG_COPYMARK, ""));
    size = tfs_size(items);
    if (mark)
        TF_EQ(tfs_write_all(mk, "", 0), 0);
    if (cut)
        TF_EQ(tfs_write_all(bpath("ITEMS", ENG_SCRDBF), "", 0), 0);
    g_looks = 0;
    g_look = look;
    cf_fault_rd = fail_look;
    TF_EQ(eng_put_copies(g_bak), CF_EACCES);
    cf_fault_rd = NULL;
    TF_EQ(tfs_size(items), size);
    TF_EQ(eng_drop_copies(g_bak), 0);
    cf_remove(mk);
}

void test_put_looks(void)
{
    /* The look of the mark: the copy must not replace the table. */
    put_look(1, 0, 1);
    /* With the mark, the look of ITEMS's copy, which a stopped
       eng_put_copies has renamed already: the table must stay.  Each
       table's copies are looked for after the mark, .DBT then .DBF. */
    put_look(3 + 2 * ENG_T_ITEMS, 1, 0);
}

/* ---------------------------------------------------------------------
 * The FILES= check of the open.
 * --------------------------------------------------------------------- */

/* cf_fault_rd and cf_fault: count the run of opens, which any other
   file call ends, and fail the g_runmax-th open of a run with
   CF_EMFILE, as DOS does when its file table is full. */
static int run_open(int op)
{
    if (op != CF_OP_OPEN) {
        g_run = 0;
        return 0;
    }
    return ++g_run == g_runmax ? CF_EMFILE : 0;
}

void test_handles(void)
{
    char why[TFS_WHYSZ];
    char want[TFS_WHYSZ];
    eng *e;

    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    g_run = 0;
    g_runmax = ENG_NHANDLES + ENG_TMPHANDLES;
    cf_fault_rd = run_open;
    cf_fault = run_open;
    why[0] = '\0';
    TF_EQ(eng_open(&e, tfs_dir(), &g_now, why, (u16)sizeof why),
          ENG_EHANDLES);
    sprintf(want, "%sSEQ.DBF: %s", tfs_dir(), tx(ENG_TX_NOHANDLE));
    TF_STREQ(why, want);
    TF_CHECK(e == NULL);
    g_runmax++;
    TF_EQ(eng_open(&e, tfs_dir(), &g_now, why, (u16)sizeof why), ENG_OK);
    cf_fault_rd = NULL;
    cf_fault = NULL;
    if (e != NULL)
        TF_EQ(eng_close(e), ENG_OK);
    TF_EQ(te_open(&g_e, &g_now), ENG_OK);
}

/* ---------------------------------------------------------------------
 * A marker left by a failed change, PACK stopped, eng_init_data
 * failing.
 * --------------------------------------------------------------------- */

/* Write the marker of a change that failed: its count line, count, and
   the line of ITEMS. */
static void stale_marker(u32 count)
{
    char text[TXT_U32LEN + 16];

    sprintf(text, "%lu\r\nITEMS\r\n", (unsigned long)count);
    TF_EQ(tfs_write_all(tfs_path(ENG_EDITMARK, ""), text,
                        (u16)strlen(text)), 0);
}

/* The X rows (ENG_A_CUT) among the audit records after record from. */
static u16 cut_rows(u32 from)
{
    u32 i;
    u16 n;

    n = 0;
    for (i = from + 1; i <= te_naud(); i++)
        if (strcmp(te_aud(i, "ACTION"), "X") == 0)
            n++;
    return n;
}

static u16 g_marked;            /* creates since RESTORE's last copy   */

/* cf_fault: fail RESTORE's commit mark, the second create once the copy
   it makes last is there (the first is its new marker's). */
static int fail_commit(int op)
{
    const eng_tdef *td;

    td = eng_table_def(ENG_NTABLES - 1);
    if (op != CF_OP_CREATE ||
        !tfs_exists(tfs_path(td->name, td->memo ? ENG_SCRDBT : ENG_SCRDBF)))
        return 0;
    return ++g_marked == 2 ? CF_EFULL : 0;
}

static int g_rpstate;           /* 1 once fail_repair has failed a call */

/* cf_fault: fail the first call but a close of the repairs that
   eng_init_data makes over a kept marker (in its batch), once. */
static int fail_repair(int op)
{
    if (op == CF_OP_CLOSE || g_rpstate || !eng_batch_on(g_e))
        return 0;
    g_rpstate = 1;
    return CF_EFULL;
}

/* The records of the backup's AUDIT.DBF. */
static u32 bak_audits(void)
{
    dbf *d;
    u32 n;

    n = 0;
    if (TF_EQ(dbf_open(&d, bpath("AUDIT", ".DBF"), DBF_RDONLY), DBF_OK)) {
        n = dbf_reccount(d);
        dbf_close(d);
    }
    return n;
}

void test_markers(void)
{
    u32 kept;
    u32 n;

    /* PACK keeps the count line of a marker a failed change left, so
       the rows it covers are still flagged... */
    n = te_naud();
    stale_marker(n - 1);
    TF_EQ(admin(ENG_ADM_PACK, NULL, NULL), ENG_OK);
    TF_EQ(cut_rows(n), 1);
    /* ...while RESTORE first makes the marker's repairs, then starts a
       marker of its own: the rows of the restored table are not the
       ones the old count covered, so the audit holds the backup's rows
       and the K row alone... */
    stale_marker(1);
    TF_EQ(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_OK);
    TF_EQ(cut_rows(0), 0);
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), g_new[ENG_T_AUDIT]);
    TF_CHECK(no_scratch(tfs_dir()));
    /* ...and one whose commit mark fails leaves the rows flagged. */
    n = te_naud();
    stale_marker(n - 1);
    g_marked = 0;
    cf_fault = fail_commit;
    TF_EQ(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_EFULL);
    cf_fault = NULL;
    TF_EQ(g_marked, 2);
    TF_CHECK(eng_is_open(g_e));
    TF_EQ(cut_rows(n), 1);
    TF_CHECK(no_scratch(tfs_dir()));
    /* BACKUP makes them before it copies: the backup holds the X row,
       every row but BACKUP's own K row. */
    n = te_naud();
    stale_marker(n - 1);
    TF_EQ(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_OK);
    TF_EQ(cut_rows(n), 1);
    TF_EQ(bak_audits() + 1, te_naud());
    /* One whose repairs fail copies nothing and gives the error; the
       open after it makes them. */
    kept = bak_audits();
    n = te_naud();
    stale_marker(n - 1);
    g_rpstate = 0;
    cf_fault = fail_repair;
    TF_EQ(admin(ENG_ADM_BACKUP, g_bak, NULL), ENG_EFULL);
    cf_fault = NULL;
    TF_EQ(g_rpstate, 1);
    TF_EQ(bak_audits(), kept);
    TF_CHECK(no_scratch(g_bak));
    TF_EQ(cut_rows(n), 1);
    TF_CHECK(!tfs_exists(tfs_path(ENG_EDITMARK, "")));
}

/* 1 when every table of g_e but AUDIT and SEQ holds as many records as
   in a or as in b, each on its own. */
static int each(const u32 *a, const u32 *b)
{
    u32 got[ENG_NTABLES];
    int t;

    counts(got);
    for (t = 0; t < ENG_NTABLES; t++)
        if (t != ENG_T_AUDIT && t != ENG_T_SEQ && got[t] != a[t] &&
            got[t] != b[t])
            return 0;
    return 1;
}

void test_pack_faults(void)
{
    u32 before[ENG_NTABLES];
    u32 packed[ENG_NTABLES];
    u32 calls;
    u32 n;
    int crash;

    TF_EQ(eng_item_del(g_e, 2), ENG_OK);
    te_loc("C1", 0);
    TF_EQ(eng_loc_del(g_e, "C1"), ENG_OK);
    TF_EQ(eng_maint_del(g_e, 2), ENG_OK);
    counts(before);
    TF_EQ(eng_close(g_e), ENG_OK);
    TF_EQ(te_save(1), 0);
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(admin(ENG_ADM_PACK, NULL, NULL), ENG_OK);
    calls = tfs_calls();
    tfs_fault_off();
    counts(packed);
    TF_EQ(eng_close(g_e), ENG_OK);
    printf("T_ADMIN: PACK: %lu disk-changing calls\n", (unsigned long)calls);
    for (n = 1; n <= calls; n += g_step)
        for (crash = 0; crash < 2; crash++) {
            reset_state();
            if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
                continue;
            tfs_fault(TFS_ALLOPS, n, crash ? 0 : 1, 0);
            admin(ENG_ADM_PACK, NULL, NULL);
            tfs_fault_off();
            eng_close(g_e);
            if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
                continue;
            if (!TF_CHECK(each(before, packed) && indexes_ok() &&
                          no_scratch(tfs_dir())))
                printf("T_ADMIN: PACK call %lu (%s)\n", (unsigned long)n,
                       crash ? "crash" : "single");
            TF_EQ(eng_close(g_e), ENG_OK);
        }
    g_e = NULL;
    reset_state();
    TF_EQ(te_open(&g_e, &g_now), ENG_OK);
}

#ifdef COMPAT_HOST
/* REINDEX, then PACK, with each close failing once, from the saved
   state; the engine closed after each holds no near block. */
void test_close_faults(void)
{
    static const int ops[] = { ENG_ADM_REINDEX, ENG_ADM_PACK };
    static const char *const names[] = { "REINDEX", "PACK" };
    u16 closes;
    u16 held;
    u16 n;
    int i;

    for (i = 0; i < 2; i++) {
        TF_EQ(eng_close(g_e), ENG_OK);
        g_e = NULL;
        reset_state();
        held = tm_held();
        if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
            return;
        tfs_fault(TFS_OP(CF_OP_CLOSE), 0, 0, 0);
        TF_EQ(admin(ops[i], NULL, NULL), ENG_OK);
        closes = tfs_count(CF_OP_CLOSE);
        tfs_fault_off();
        TF_EQ(eng_close(g_e), ENG_OK);
        g_e = NULL;
        TM_HELD(held);
        printf("T_ADMIN: %s: %u closes\n", names[i], closes);
        for (n = 1; n <= closes; n++) {
            reset_state();
            if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
                return;
            tfs_fault(TFS_OP(CF_OP_CLOSE), n, 1, 0);
            admin(ops[i], NULL, NULL);
            tfs_fault_off();
            eng_close(g_e);
            g_e = NULL;
            if (!TM_HELD(held))
                printf("T_ADMIN: %s close %u leaves near blocks held\n",
                       names[i], n);
        }
        reset_state();
        if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
            return;
    }
}
#endif

void test_init_fail(void)
{
    u32 calls;
    u32 n;
    u16 shut;

    /* An ON_LOAN that the reopen's repairs must correct, so that
       eng_init_data writes. */
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(1), -1, "ON_LOAN", NULL, 3), 0);
    TF_EQ(eng_close(g_e), ENG_OK);
    TF_EQ(te_save(1), 0);
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(admin(ENG_ADM_REINDEX, NULL, NULL), ENG_OK);
    calls = tfs_calls();
    tfs_fault_off();
    TF_EQ(eng_close(g_e), ENG_OK);
    shut = 0;
    for (n = 1; n <= calls; n += g_step) {
        reset_state();
        if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
            continue;
        tfs_fault(TFS_ALLOPS, n, 1, 0);
        /* A failure in the repairs leaves the engine with no file; any
           other leaves it open and set up. */
        if (admin(ENG_ADM_REINDEX, NULL, NULL) != ENG_OK) {
            if (!eng_is_open(g_e))
                shut++;
            TF_CHECK(!eng_is_open(g_e) || !eng_marker_kept(g_e));
        }
        tfs_fault_off();
        eng_close(g_e);
    }
    if (g_step == 1)
        TF_CHECK(shut > 0);
    reset_state();
    TF_EQ(te_open(&g_e, &g_now), ENG_OK);
}

/* RESTORE of a set made bad by a change to its file stem + ext, undone
   afterwards from the copy save + ext; code and tx are the refusal. */
static void bad_set(const char *stem, const char *ext, int how, int code,
                    u16 txn)
{
    char keep[DOS_PATHMAX];
    u8 b;

    sprintf(keep, "%s%s.KEP", g_bak, stem);
    TF_EQ(tfs_copy(bpath(stem, ext), keep), 0);
    switch (how) {
    case 0:                     /* missing */
        TF_EQ(cf_remove(bpath(stem, ext)), 0);
        break;
    case 1:                     /* not a dBASE III table */
        b = 0x30;
        TF_EQ(tfs_poke(bpath(stem, ext), 0, &b, 1), 0);
        break;
    case 2:                     /* another layout: the LOCS table */
        sprintf(keep, "%sLOCS.DBF", g_bak);
        TF_EQ(tfs_copy(keep, bpath(stem, ext)), 0);
        sprintf(keep, "%s%s.KEP", g_bak, stem);
        break;
    default:                    /* fewer records than its count */
        b = 0x7F;
        TF_EQ(tfs_poke(bpath(stem, ext), 4, &b, 1), 0);
        break;
    }
    TF_CHECK(TE_FAIL(admin(ENG_ADM_RESTORE, g_bak, NULL), code, "DIR",
                     tx(txn)));
    TF_CHECK(eng_is_open(g_e));
    TF_CHECK(no_scratch(tfs_dir()));
    TF_EQ(tfs_copy(keep, bpath(stem, ext)), 0);
    TF_EQ(cf_remove(keep), 0);
}

void test_bad_sets(void)
{
    bad_set("LOANS", ".DBF", 0, ENG_ENOTFOUND, ENG_TX_SETMISS);
    bad_set("MAINT", ".DBT", 0, ENG_ENOTFOUND, ENG_TX_SETMISS);
    bad_set("ITEMS", ".DBF", 1, ENG_EIO, ENG_TX_TBLBAD);
    bad_set("CATS", ".DBF", 2, ENG_EIO, ENG_TX_TBLBAD);
    bad_set("USERS", ".DBF", 3, ENG_EIO, ENG_TX_TBLBAD);
    TF_EQ(tfs_write_all(bpath(ENG_COPYMARK, ""), "", 0), 0);
    TF_CHECK(TE_FAIL(admin(ENG_ADM_RESTORE, g_bak, NULL), ENG_ENOTFOUND,
                     "DIR", tx(ENG_TX_SETMISS)));
    TF_EQ(cf_remove(bpath(ENG_COPYMARK, "")), 0);
}

/* ---------------------------------------------------------------------
 * A damaged N field.
 * --------------------------------------------------------------------- */

/* Write text over the start of field name of item 1 (record 1 since
   PACK) straight into ITEMS.DBF, as a torn write or a hand edit leaves
   it, with the engine closed; then open it again. */
static int damage(const char *name, const char *text)
{
    dbf *d;

    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    if (TF_EQ(dbf_open(&d, tfs_path("ITEMS", ".DBF"), DBF_RDWR), DBF_OK)) {
        TF_EQ(dbf_read(d, 1), DBF_OK);
        memcpy(dbf_recbuf(d) +
               dbf_field_at(d, (u16)dbf_field_index(d, name))->offset,
               text, strlen(text));
        TF_EQ(dbf_write(d), DBF_OK);
        TF_EQ(dbf_close(d), DBF_OK);
    }
    return TF_EQ(te_open(&g_e, &g_now), ENG_OK);
}

/* An N field that holds no clean number is read as dBASE reads it, so
   the open's repair, REINDEX, PACK and ITEM SET all work over it, and
   ITEMS2 (STR(LOC_ID,8)+STR(ITEM_ID,8)) orders it as dBASE does. */
void test_damaged(void)
{
    u32 r;
    int exact;
    u16 n;

    /* A change cut off after its marker and part of its record: the
       open rebuilds ITEMS's indexes over "  12  12", which is 12. */
    TF_EQ(te_marker_put("ITEMS"), 0);
    if (!damage("LOC_ID", "  12  12"))
        return;
    TF_CHECK(indexes_ok());
    TF_EQ(eng_item_get(g_e, 1, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "LOC_ID"), "12  12");
    TF_EQ(ndx_seek(eng_ndx(g_e, ENG_X_ITEMS2),
                   (const u8 *)"      12       1", 16, &r, &exact), DBF_OK);
    TF_EQ(r, 1);
    TF_EQ(exact, 1);
    TF_EQ(admin(ENG_ADM_REINDEX, NULL, NULL), ENG_OK);
    TF_CHECK(indexes_ok());
    /* dBASE's overflow: STR writes asterisks, which sort after the
       other item's leading spaces. */
    if (!damage("LOC_ID", "********"))
        return;
    TF_EQ(admin(ENG_ADM_PACK, NULL, NULL), ENG_OK);
    TF_CHECK(indexes_ok());
    TF_EQ(ndx_last(eng_ndx(g_e, ENG_X_ITEMS2), &r), DBF_OK);
    TF_EQ(r, 1);
    /* ITEM SET writes a number over it. */
    n = te_kv("LOC_ID=A1");
    TF_EQ(eng_item_set(g_e, 1, te_p, n), ENG_OK);
    TF_CHECK(indexes_ok());
    TF_EQ(eng_item_get(g_e, 1, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "LOC_ID"), "1");
}

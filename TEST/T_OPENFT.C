/*
 * T_OPENFT.C - opening a data directory when the machine stops at any
 * point of the open.
 *
 * usage: T_OPENFT dir [step]
 *
 * dir is a scratch directory used only by this test (a scratch drive
 * under DOS, such as A:\ on a boot floppy).  Six starting states are
 * built in it with the engine itself and saved under other extensions:
 *   0  an empty directory (the first open creates and seeds everything);
 *   1  a directory with locations and items, two indexes deleted and an
 *      edit marker naming ITEMS (indexes to create and to rebuild);
 *   2  the same with ITEMS.DBF, ITEMS.DBT and CATS.DBF deleted (tables to
 *      create beside stale indexes, categories to seed);
 *   3  state 1 with an uncounted record after the last one of ITEMS.DBF,
 *      a table the marker names (a tail to cut);
 *   4  the directory of state 1 before its changes, with no marker and
 *      an uncounted record after the last one of SEQ.DBF;
 *   5  state 1 with a marker whose count line leaves out its last
 *      NCUT audit rows (rows a cut change left, to be flagged).
 * For each state the disk-changing calls of an undisturbed open are
 * counted; then for every call number n (every step-th one, default 1)
 * the state is restored and opened with the fault recorder of T_FILES.H
 * making call n fail, once ("single") or with every later call failing
 * too, as when the machine stops ("crash").  Afterwards the first open
 * without faults must succeed and leave a consistent directory: the
 * ADMIN user found through USERS1; every index holding exactly one entry
 * per record of its table, each with the key its expression gives for
 * that record; every table file exactly its header, records and end byte
 * long; the items of the state kept; after a crash of state 0's open,
 * every seed category; every seed record with its I row; the audit rows
 * a stopped open left behind a marker flagged by an X row (te_cut of
 * TENGH.H), and those of state 5 always, a second X row taking in the
 * first one's range and more; and no scratch, leftover or marker file.
 *
 * Then a change of two items rewritten in place, a password change and
 * a location appended is stopped at each of its calls, run as four
 * changes and as one batch: no record is counted on the disk before its
 * row, also by a close stopped after a failed flush at the batch's end,
 * and after the next open every change that reached the disk has its
 * audit row, and rows whose change did not are flagged.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGSCH.H"
#include "ENGEDIT.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
#include "ENGUSER.H"
#include "TESTFW.H"
#include "T_FILES.H"
#define TE_AUDCUT
#include "TENGH.H"
#include "TEWIPE.H"

#define NSTATES         6
#define NCUT            2       /* rows state 5's marker leaves out     */
#define MAXSEED         31      /* CATS records whose I rows are kept
                                   in a u32 mask                       */

/* The .DBF header fields read: the record count (le32 at 4), the header
   length (le16 at 8) and the record length (le16 at 10). */
#define HDR_COUNT       4
#define HDR_HDRLEN      8
#define HDR_RECLEN      10
#define HDR_READ        12

static int g_failed;            /* cases that failed, for the summary   */
static u32 g_from;              /* audit rows the state's marker covers
                                   none of                             */
static int g_twice;             /* opens that found two X rows          */
static char g_row[TE_TEXTMAX];  /* a marker's or a row's text          */

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

/* Names that must not remain after an open: scratch files of the open,
   of a pack and of a reindex. */
static const char *const stray_ext[] = {
    ".&DB", ".&DT", ".&NX", ".$DB", ".%DB", ".$DT", ".%DT", ".$NX",
    ".%NX", ".#NX", NULL
};

/* ---------------------------------------------------------------------
 * The starting states.
 * --------------------------------------------------------------------- */
static const char *const loc_codes[] = { "A1", "B2", "C3" };

static void add_items(eng *e)
{
    eng_pair p[3];
    u32 id;
    int i;

    eng_set_actor(e, "T_OPENFT", "TEST");
    for (i = 0; i < 3; i++)
        TF_EQ(te_loc(loc_codes[i], 0), (u32)i + 1u);
    p[0].key = "CATEGORY";
    p[0].val = "SYS";
    p[1].key = "MFR";
    p[1].val = "IBM";
    p[2].key = "LOC_ID";
    for (i = 0; i < 3; i++) {
        p[2].val = loc_codes[i];
        TF_EQ(eng_item_add(e, p, 3, &id), ENG_OK);
    }
    TF_EQ(eng_item_del(e, id), ENG_OK);
}

/* Build state s in the directory and save it. */
static void make_state(int s)
{
    eng *e;

    tfs_fault_off();
    te_remove(tfs_dir(), NULL);
    if (s > 0 && TF_EQ(te_open(&e, &g_now), ENG_OK)) {
        add_items(e);
        TF_EQ(eng_close(e), ENG_OK);
    }
    if (s > 0 && s != 4) {
        cf_remove(tfs_path("LOCS1", ".NDX"));
        cf_remove(tfs_path("ITEMS4", ".NDX"));
        TF_EQ(te_marker_put("ITEMS"), 0);
    }
    if (s == 2) {
        cf_remove(tfs_path("ITEMS", ".DBF"));
        cf_remove(tfs_path("ITEMS", ".DBT"));
        cf_remove(tfs_path("CATS", ".DBF"));
    }
    if (s == 3)
        TF_EQ(tfs_add_tail("ITEMS"), 0);
    if (s == 4)
        TF_EQ(tfs_add_tail("SEQ"), 0);
    g_from = te_disk_count(ENG_T_AUDIT);
    if (s == 5) {
        g_from -= NCUT;
        sprintf(g_row, "%lu\r\nITEMS\r\n", (unsigned long)g_from);
        TF_EQ(tfs_write_all(tfs_path(ENG_EDITMARK, ""), g_row,
                            (u16)strlen(g_row)), 0);
    }
    TF_EQ(te_save(1), 0);
}

/* Empty the directory and restore the saved state. */
static void restore(void)
{
    te_remove(tfs_dir(), NULL);
    TF_EQ(te_save(0), 0);
}

/* ---------------------------------------------------------------------
 * The check after a disturbed open.
 * --------------------------------------------------------------------- */

/* 1 when table t's file is its header, records and end byte, exactly. */
static int table_size_ok(int t)
{
    const char *path;
    u8 hdr[HDR_READ];

    path = tfs_path(eng_table_def(t)->name, ".DBF");
    return tfs_peek(path, 0, hdr, HDR_READ) == 0 &&
           tfs_size(path) == get_le16(hdr + HDR_HDRLEN) +
                             get_le32(hdr + HDR_COUNT) *
                             get_le16(hdr + HDR_RECLEN) + 1u;
}

/* 1 when a scratch or leftover name of an engine file is in the
   directory. */
static int strays(void)
{
    const char *stem;
    int i;
    int j;

    for (i = 0; i < ENG_NTABLES + ENG_NINDEX; i++) {
        stem = i < ENG_NTABLES ? eng_table_def(i)->name
                               : eng_index_def(i - ENG_NTABLES)->name;
        for (j = 0; stray_ext[j] != NULL; j++)
            if (tfs_exists(tfs_path(stem, stray_ext[j])))
                return 1;
    }
    return 0;
}

/* The count line's value of the marker a stopped run left, or dflt when
   there is no marker or it has no count line. */
static u32 mark_count(u32 dflt)
{
    u32 v;
    u16 got;
    u16 i;

    if (tfs_read_all(tfs_path(ENG_EDITMARK, ""), g_row, ENG_MARKMAX,
                     &got) != 0)
        return dflt;
    v = 0;
    for (i = 0; i < got && TXT_ISDIGIT(g_row[i]); i++)
        v = v * 10 + (u32)(g_row[i] - '0');
    return i > 0 && i + 1u < got && g_row[i] == '\r' &&
           g_row[i + 1] == '\n' ? v : dflt;
}

/* 0 when the audit rows of e after the state's own are as verify
   describes, after a stopped open that left a marker covering the AUDIT
   records after from and left records counted; else the check's
   number. */
static int audit_check(eng *e, int s, u32 from, u32 left)
{
    u32 seen;
    u32 first;
    u32 upto;
    u32 last;
    u32 id;
    u32 r;
    int act;
    int nx;

    nx = te_cut(from < g_from ? from : g_from);
    if (nx < 0 || (nx == 0 && (s == 5 || left > from)))
        return 8;
    seen = 0;
    first = 0;
    upto = 0;
    nx = 0;
    for (r = g_from + 1; r <= te_naud(); r++) {
        act = te_rtext(ENG_T_AUDIT, r, "ACTION")[0];
        id = (u32)te_rnum(ENG_T_AUDIT, r, "REC_ID");
        if (act == ENG_A_INSERT && id >= 1 && id <= MAXSEED &&
            strcmp(te_rtext(ENG_T_AUDIT, r, "TBL"), "CATS") == 0)
            seen |= (u32)1 << (id - 1);
        if (act != ENG_A_CUT)
            continue;
        last = (u32)strtoul(te_rtext(ENG_T_AUDIT, r, "NEWVAL"), NULL, 10);
        if (nx++ > 0 && (id != first || last <= upto))
            return 9;
        first = id;
        upto = last;
    }
    if (nx > 1)
        g_twice++;
    if ((s == 0 || s == 2) && (eng_reccount(e, ENG_T_CATS) > MAXSEED ||
        (~seen & (((u32)1 << eng_reccount(e, ENG_T_CATS)) - 1u)) != 0))
        return 10;
    return 0;
}

/* 0 when the directory reopens clean, else 1 after printing why. */
static int verify(int s, const char *mode, i32 n, int rc0)
{
    eng *e;
    u32 left;
    u32 from;
    u32 r;
    int bad;
    int rc;
    int i;

    bad = 0;
    tfs_fault_off();
    left = te_disk_count(ENG_T_AUDIT);
    from = mark_count(left);
    rc = te_open(&e, &g_now);
    if (rc != ENG_OK) {
        printf("T_OPENFT: state %d %s %ld (open %d): reopen %d\n", s,
               mode, (long)n, rc0, rc);
        return 1;
    }
    if (eng_seek_live(e, ENG_X_USERS1, "ADMIN", &r) != ENG_OK || r == 0)
        bad = 1;
    for (i = 0; i < ENG_NINDEX; i++)
        if (!te_index_ok(e, i))
            bad = 2;
    if (s != 0 && s != 2 && eng_reccount(e, ENG_T_ITEMS) != 3)
        bad = 3;
    if (eng_reccount(e, ENG_T_CATS) == 0 ||
        eng_reccount(e, ENG_T_LTYPES) == 0 ||
        (s == 0 && strcmp(mode, "crash") == 0 &&
         eng_reccount(e, ENG_T_CATS) + eng_reccount(e, ENG_T_LTYPES) !=
         eng_seed_rows()))
        bad = 4;
    rc = audit_check(e, s, from, left);
    if (rc != 0)
        bad = rc;
    eng_close(e);
    for (i = 0; i < ENG_NTABLES; i++)
        if (!table_size_ok(i))
            bad = 5;
    if (tfs_exists(tfs_path(ENG_EDITMARK, "")))
        bad = 6;
    if (strays())
        bad = 7;
    if (bad)
        printf("T_OPENFT: state %d %s %ld (open %d): check %d\n", s, mode,
               (long)n, rc0, bad);
    return bad != 0;
}

/* Every disturbed open of state s. */
static void sweep(int s, i32 step)
{
    eng *e;
    i32 total;
    i32 n;
    int rc;
    int crash;

    make_state(s);
    restore();
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    rc = te_open(&e, &g_now);
    tfs_fault_off();
    total = (i32)tfs_calls();
    if (!TF_EQ(rc, ENG_OK))
        return;
    eng_close(e);
    printf("T_OPENFT: state %d: %ld disk-changing calls\n", s, (long)total);
    for (crash = 0; crash < 2; crash++)
        for (n = 1; n <= total; n += step) {
            restore();
            tfs_fault(TFS_ALLOPS, (u32)n, crash ? 0 : 1, 0);
            rc = te_open(&e, &g_now);
            tfs_fault_off();
            if (rc == ENG_OK)
                eng_close(e);
            if (!TF_EQ(verify(s, crash ? "crash" : "single", n, rc), 0))
                g_failed++;
        }
}

/* ---------------------------------------------------------------------
 * The stopped change.
 * --------------------------------------------------------------------- */

/* The audit rows of the change, as te_aud gives the fields k_cfields. */
static const char k_cfields[] = "TBL REC_ID FLD NEWVAL";
static const char *const k_crows[] = {
    "ITEMS|1|MFR|ZENITH", "ITEMS|2|MFR|ZENITH", "LOCS|4|LOC_ID|4",
    "USERS|1|PWHASH|"
};
#define NCROWS          4

#define NLOCS           3       /* locations before the change          */
#define LOCROW          4       /* D4's row among the change's rows     */

static char g_hash[ENG_MAXVAL]; /* ADMIN's PWHASH before the change     */

/* The change: the MFR of items 1 and 2 rewritten in place, ADMIN's
   password changed (its W row in the same bracket as the record), then
   the location D4 appended, as four changes or as one batch begun on
   ITEMS; its result. */
static int change(eng *e, int batch)
{
    static const eng_pair d4[] = {
        { "LOC_NAME", "D4" }, { "LOC_CODE", "D4" }, { "LTYPE", "BIN" }
    };
    u32 r;
    int rc;

    rc = batch ? eng_batch_begin(e, ENG_T_ITEMS) : ENG_OK;
    for (r = 1; r <= 2 && rc == ENG_OK; r++) {
        rc = eng_edit_begin(e, ENG_T_ITEMS, r);
        if (rc == ENG_OK)
            rc = eng_edit_finish(e, eng_edit_put(e, "MFR", "ZENITH"), r);
    }
    if (rc == ENG_OK)
        rc = eng_user_passwd(e, "ADMIN", "admin", "SECRET1");
    if (rc == ENG_OK)
        rc = eng_loc_add(e, d4, sizeof d4 / sizeof d4[0], &r);
    return batch ? eng_batch_end(e, rc) : rc;
}

/* 0 when the directory reopens with every part of the change that
   reached the disk audited and the rows of any other part flagged, the
   indexes and table files whole and no marker left; else 1 after
   printing why. */
static int verify_change(int batch, const char *mode, i32 n, int rc0)
{
    eng *e;
    u32 left;
    u32 r;
    u32 from;
    int landed[NCROWS];
    int has[NCROWS];
    int orphan;
    int bad;
    int rc;
    int i;

    bad = 0;
    tfs_fault_off();
    left = te_disk_count(ENG_T_AUDIT);
    from = mark_count(left);
    rc = te_open(&e, &g_now);
    if (rc != ENG_OK) {
        printf("T_OPENFT: change %d %s %ld (%d): reopen %d\n", batch, mode,
               (long)n, rc0, rc);
        return 1;
    }
    for (i = 0; i < 2; i++)
        landed[i] = strcmp(te_rtext(ENG_T_ITEMS, (u32)i + 1u, "MFR"),
                           "ZENITH") == 0;
    landed[2] = eng_loc_get(e, "D4", &r) == ENG_OK;
    landed[3] = strcmp(te_rtext(ENG_T_USERS, 1, "PWHASH"), g_hash) != 0;
    for (i = 0; i < NCROWS; i++)
        has[i] = 0;
    for (r = g_from + 1; r <= te_naud(); r++)
        for (i = 0; i < NCROWS; i++)
            if (strcmp(te_aud(r, k_cfields), k_crows[i]) == 0)
                has[i] = 1;
    orphan = 0;
    for (i = 0; i < NCROWS; i++) {
        if (landed[i] && !has[i])
            bad = 1;
        if (has[i] && !landed[i])
            orphan = 1;
    }
    i = te_cut(from);
    if (i < 0 || (i == 0 && (orphan || left > from)))
        bad = 2;
    for (i = 0; i < ENG_NINDEX; i++)
        if (!te_index_ok(e, i))
            bad = 3;
    eng_close(e);
    for (i = 0; i < ENG_NTABLES; i++)
        if (!table_size_ok(i))
            bad = 4;
    if (tfs_exists(tfs_path(ENG_EDITMARK, "")))
        bad = 5;
    if (bad)
        printf("T_OPENFT: change %d %s %ld (%d): check %d\n", batch, mode,
               (long)n, rc0, bad);
    return bad != 0;
}

/* The change run with call n failing once and, when that leaves D4
   appended but not counted (a failed flush at the batch's end), the
   close after it with each of its calls in turn failing once and, as
   when the machine stops, with every later call failing too: the close
   too must count no record before its row. */
static void close_sweep(int batch, i32 n)
{
    eng *e;
    i32 m;
    int rc;
    int k;

    for (m = 1; ; m++)
        for (k = 0; k < 2; k++) {
            restore();
            if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
                return;
            tfs_fault(TFS_ALLOPS, (u32)n, 1, 0);
            rc = change(e, batch);
            if (rc == ENG_OK || te_disk_count(ENG_T_LOCS) != NLOCS ||
                eng_reccount(e, ENG_T_LOCS) == NLOCS) {
                eng_close(e);
                return;
            }
            tfs_fault(TFS_ALLOPS, (u32)m, (u32)k, 0);
            eng_close(e);
            if (!TF_EQ(verify_change(batch, k ? "close once" : "close",
                                     n * 1000 + m, rc), 0))
                g_failed++;
            if (tfs_calls() < (u32)m)
                return;
        }
}

/* The change stopped at each of its disk-changing calls in turn, once
   and as when the machine stops, on the directory of state 4 without
   its SEQ tail.  Before the engine is closed (whose close flushes
   again), the disk must not count D4 without its row. */
static void sweep_change(int batch, i32 step)
{
    eng *e;
    i32 total;
    i32 n;
    int rc;
    int crash;

    tfs_fault_off();
    te_remove(tfs_dir(), NULL);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    add_items(e);
    g_from = eng_reccount(e, ENG_T_AUDIT);
    strcpy(g_hash, te_rtext(ENG_T_USERS, 1, "PWHASH"));
    TF_EQ(eng_close(e), ENG_OK);
    TF_EQ(te_save(1), 0);
    restore();
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(change(e, batch), ENG_OK);
    tfs_fault_off();
    total = (i32)tfs_calls();
    eng_close(e);
    TF_EQ(verify_change(batch, "none", 0, ENG_OK), 0);
    printf("T_OPENFT: change %d: %ld disk-changing calls\n", batch,
           (long)total);
    for (crash = 0; crash < 2; crash++)
        for (n = 1; n <= total; n += step) {
            restore();
            if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
                return;
            tfs_fault(TFS_ALLOPS, (u32)n, crash ? 0 : 1, 0);
            rc = change(e, batch);
            if (!TF_CHECK(te_disk_count(ENG_T_LOCS) == NLOCS ||
                          te_disk_count(ENG_T_AUDIT) >= g_from + LOCROW))
                printf("T_OPENFT: change %d %ld: D4 counted alone\n", batch,
                       (long)n);
            eng_close(e);
            if (!TF_EQ(verify_change(batch, crash ? "crash" : "single", n,
                                     rc), 0))
                g_failed++;
            if (!crash)
                close_sweep(batch, n);
        }
}

int main(int argc, char **argv)
{
    i32 step;
    int s;

    tf_begin("T_OPENFT");
    if (argc < 2) {
        printf("usage: T_OPENFT dir [step]\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    step = argc > 2 ? (i32)atol(argv[2]) : 1;
    if (step < 1)
        step = 1;
    for (s = 0; s < NSTATES; s++)
        sweep(s, step);
    if (step == 1)
        TF_CHECK(g_twice > 0);
    for (s = 0; s < 2; s++)
        sweep_change(s, step);
    if (g_failed > 0)
        printf("T_OPENFT: %d cases failed\n", g_failed);
    tfs_fault_off();
    te_remove(tfs_dir(), NULL);
    te_save(1);
    return tf_done();
}

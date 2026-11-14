/*
 * T_CSVF.C - CSV import and export when the machine stops at any point.
 *
 * usage: T_CSVF dir [step]
 *
 * dir is a scratch directory used only by this test (tfs_set_dir of
 * T_FILES.H).  A starting state is built in it with the engine (the
 * location A1 and NITEMS items) and saved under other extensions
 * (te_save of TENGH.H).  Three sweeps follow, each first run once
 * undisturbed to count its disk-changing calls; then for every call
 * number n (every step-th one, default 1) the state is restored, the
 * engine opened, and the operation run with the fault recorder of
 * T_FILES.H making call n and every later one fail, as when the machine
 * stops; the engine is closed with the calls still failing, so nothing
 * more reaches the disk.  Checks:
 *   - import of NADD ITEMS lines and of NLOCS LOCS lines: afterwards an
 *     undisturbed open must succeed with either none or all of the
 *     lines' records live (for LOCS all their codes or none), every
 *     index holding exactly one entry per record with the key its
 *     expression gives, every table file exactly its header, records
 *     and end byte long, SEQ above the largest ITEM_ID and AUD_ID, and
 *     the I audit rows of the import none or one per line, never fewer
 *     than its records (the final flush writes AUDIT before the table),
 *     and the import's rows left on disk without their records flagged
 *     by an X row of the reopen (te_cut of TENGH.H);
 *     after the undisturbed import, its K row and then one I row per
 *     line, each with the record's own ID; the LOCS lines are a chain,
 *     each in the location of the line before, named by its code;
 *   - export of ITEMS in two requests, to a new file and over an
 *     existing one with REPLACE: the target is afterwards absent, the
 *     old file, or the complete export, byte for byte.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when dir is missing or too long.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#include "ENGITEM.H"
#include "ENGLOCS.H"
#include "ENGCSV.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#define TE_AUDCUT
#include "TENGH.H"

#define NITEMS      55          /* items of the starting state          */
#define NADD        4           /* ITEMS lines imported                 */
#define NLOCS       3           /* LOCS lines imported                  */
#define MAXCALLS    100         /* requests of one export               */
#define DBFHEAD     12          /* .DBF header bytes holding the sizes  */

/* The files of the test besides the engine's, and of them the export
   target a run starts without; te_remove also removes their export
   scratch names. */
static const char *const k_files[] = {
    "IN.CSV", "IN2.CSV", "X.CSV", "GOOD.CSV", NULL
};
static const char *const k_target[] = { "X.CSV", NULL };

/* The text of the old target that an export with REPLACE overwrites. */
static const char k_old[] = "OLD";

/* The codes of the LOCS lines of IN2.CSV, as the import stores them. */
static const char *const k_codes[NLOCS] = { "S1", "S2", "S3" };

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

static eng *g_e;
static u32 g_base[ENG_NTABLES]; /* the counts of the starting state     */
static int g_failed;

/* ---------------------------------------------------------------------
 * Files.
 * --------------------------------------------------------------------- */

/* The engine's files back as te_save left them, with no marker, no
   scratch file and no export target. */
static void restore(void)
{
    te_remove(tfs_dir(), k_target);
    TF_EQ(te_save(0), 0);
}

/* Remove every file of the test, saved copies included. */
static void remove_all(void)
{
    te_remove(tfs_dir(), k_files);
    te_save(1);
}

static void put_text(const char *name, const char *s)
{
    TF_EQ(tfs_write_all(tfs_path(name, ""), s, (u16)strlen(s)), 0);
}

/* ---------------------------------------------------------------------
 * The checks.
 * --------------------------------------------------------------------- */

/* 1 when table t's file is its header, records and end byte, exactly. */
static int table_size_ok(int t)
{
    u8 hdr[DBFHEAD];
    const char *path;
    u32 size;

    path = tfs_path(eng_table_def(t)->name, ".DBF");
    size = tfs_size(path);
    if (size == TFS_NOFILE || tfs_peek(path, 0, hdr, DBFHEAD) != 0)
        return 0;
    return size == get_le16(hdr + 8) +
                   get_le32(hdr + 4) * get_le16(hdr + 10) + 1u;
}

/* 1 when SEQ's next ID of table t is above the ID of its last record. */
static int seq_ok(int t, const char *idname)
{
    u32 next;
    u32 n;

    n = eng_reccount(g_e, t);
    if (eng_seq_peek(g_e, t, &next) != ENG_OK)
        return 0;
    return n == 0 || (i32)next > te_rnum(t, n, idname);
}

/* Live records of table t. */
static u32 live(int t)
{
    u32 n;
    u32 r;
    u32 k;

    n = eng_reccount(g_e, t);
    k = 0;
    for (r = 1; r <= n; r++)
        if (eng_rec_read(g_e, t, r) == ENG_OK &&
            !dbf_is_deleted(eng_dbf(g_e, t)))
            k++;
    return k;
}

/* I rows of table t's name among the audit records after base. */
static u32 irows(int t, u32 base)
{
    char want[ENG_TBLNAMELEN + 3];
    u32 n;
    u32 r;
    u32 k;

    strcpy(want, eng_table_def(t)->name);
    strcat(want, "|I");
    n = te_naud();
    k = 0;
    for (r = base + 1; r <= n; r++)
        if (strcmp(te_aud(r, "TBL ACTION"), want) == 0)
            k++;
    return k;
}

/* 0 when the directory reopens consistent after an import into t of
   nadd lines, else 1 after printing why. */
static int verify(int t, u32 nadd, i32 n, int rc0)
{
    u32 left;
    u32 got;
    u32 ir;
    u32 r;
    int bad;
    int cut;
    int rc;
    int i;
    int k;

    bad = 0;
    left = te_disk_count(ENG_T_AUDIT);
    rc = te_open(&g_e, &g_now);
    if (rc != ENG_OK) {
        printf("T_CSVF: %s %ld (import %d): reopen %d\n",
               eng_table_def(t)->name, (long)n, rc0, rc);
        return 1;
    }
    got = live(t) - g_base[t];
    if (got != 0 && got != nadd)
        bad = 1;
    for (i = 0; i < ENG_NINDEX; i++)
        if (!te_index_ok(g_e, i))
            bad = 2;
    if (!seq_ok(ENG_T_ITEMS, "ITEM_ID") || !seq_ok(ENG_T_AUDIT, "AUD_ID"))
        bad = 3;
    ir = irows(t, g_base[ENG_T_AUDIT]);
    cut = te_cut(g_base[ENG_T_AUDIT]);
    if ((ir != 0 && ir != nadd) || ir < got || cut < 0 ||
        (left > g_base[ENG_T_AUDIT] && got == 0 && cut == 0))
        bad = 4;
    if (t == ENG_T_LOCS) {
        k = 0;
        for (i = 0; i < NLOCS; i++)
            if (eng_loc_get(g_e, k_codes[i], &r) == ENG_OK)
                k++;
        if ((u32)k != got)
            bad = 5;
    }
    eng_close(g_e);
    g_e = NULL;
    for (i = 0; i < ENG_NTABLES; i++)
        if (!table_size_ok(i))
            bad = 6;
    if (bad)
        printf("T_CSVF: %s %ld (import %d): check %d (%lu added, %lu I)\n",
               eng_table_def(t)->name, (long)n, rc0, bad, (unsigned long)got,
               (unsigned long)ir);
    return bad != 0;
}

/* ---------------------------------------------------------------------
 * The sweeps.
 * --------------------------------------------------------------------- */

static void make_state(void)
{
    eng_pair p[2];
    u32 id;
    int i;

    remove_all();
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    TF_EQ(te_loc("A1", 0), 1);
    p[0].key = "CATEGORY";
    p[0].val = "SYS";
    p[1].key = "LOC_ID";
    p[1].val = "A1";
    for (i = 0; i < NITEMS; i++)
        TF_EQ(eng_item_add(g_e, p, 2, &id), ENG_OK);
    for (i = 0; i < ENG_NTABLES; i++)
        g_base[i] = eng_reccount(g_e, i);
    g_base[ENG_T_ITEMS] = live(ENG_T_ITEMS);
    g_base[ENG_T_LOCS] = live(ENG_T_LOCS);
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    TF_EQ(te_save(1), 0);
    put_text("IN.CSV", ",SYS,,One\r\n,SYS,,Two,,,,,,,A1\r\n"
                       ",sys,,Three\r\n,SYS,,Four\r\n\032");
    put_text("IN2.CSV", ",,First,S1,BIN\r\n,S1,Second,\"s2\",BOX\r\n"
                        ",s2,Third,S3,BIN\r\n");
}

/* One import of the file into t with the fault at call n (0: none);
   the import's code. */
static int run_import(int t, const char *file, i32 n)
{
    u32 added;
    u16 errors;
    int rc;

    restore();
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return -1;
    tfs_fault(TFS_ALLOPS, (u32)n, 0, CF_EFULL);
    rc = eng_import(g_e, t, tfs_path(file, ""), 0, NULL, NULL, &errors,
                    &added);
    if (n == 0) {
        TF_EQ(rc, ENG_OK);
        TF_EQ(errors, 0);
    }
    eng_close(g_e);
    g_e = NULL;
    tfs_fault_off();
    return rc;
}

/* Check the audit rows that the undisturbed import into t of nadd lines
   left after those of the starting state: its K row, then one I row per
   record, in line order, naming the record's new ID. */
static void check_rows(int t, u32 nadd)
{
    char want[TE_TEXTMAX];
    const char *name;
    u32 id;
    u32 a;
    u16 k;

    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    name = eng_table_def(t)->name;
    a = g_base[ENG_T_AUDIT];
    sprintf(want, "%s|0|K|IMPORT", name);
    TF_STREQ(te_aud(a + 1, "TBL REC_ID ACTION FLD"), want);
    for (k = 1; k <= nadd; k++) {
        id = g_base[t] + k;
        sprintf(want, "%s|%lu|I|%s||%lu", name, (unsigned long)id,
                eng_field_name(g_e, t, 0), (unsigned long)id);
        TF_STREQ(te_aud(a + 1 + k, "TBL REC_ID ACTION FLD OLDVAL NEWVAL"),
                 want);
    }
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
}

static void sweep_import(int t, const char *file, u32 nadd, i32 step)
{
    i32 total;
    i32 n;
    int rc;

    run_import(t, file, 0);
    total = (i32)tfs_calls();
    if (!TF_EQ(verify(t, nadd, 0, 0), 0))
        g_failed++;
    check_rows(t, nadd);
    printf("T_CSVF: %s import: %ld disk-changing calls\n",
           eng_table_def(t)->name, (long)total);
    for (n = 1; n <= total + 1; n += step) {
        rc = run_import(t, file, n);
        if (!TF_EQ(verify(t, nadd, n, rc), 0))
            g_failed++;
    }
}

/* One export of ITEMS to X.CSV with the fault at call n (0: none),
   REPLACE over an old X.CSV when replace is 1; the last code. */
static int run_export(int replace, i32 n)
{
    char cursor[ENG_CURSORMAX];
    char after[ENG_CURSORMAX];
    u16 rows;
    int rc;
    int calls;

    restore();
    if (replace)
        put_text("X.CSV", k_old);
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return -1;
    tfs_fault(TFS_ALLOPS, (u32)n, 0, CF_EFULL);
    after[0] = '\0';
    calls = 0;
    do {
        rc = eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""),
                        (u8)(replace ? ENG_CSV_REPLACE : 0), 0, after,
                        &rows, cursor);
        strcpy(after, cursor);
        calls++;
    } while (rc == ENG_OK && cursor[0] != '\0' && calls < MAXCALLS);
    eng_close(g_e);
    g_e = NULL;
    tfs_fault_off();
    if (n == 0) {
        TF_EQ(rc, ENG_OK);
        TF_EQ(calls, 2);
    }
    return rc;
}

/* 1 when X.CSV holds the text k_old. */
static int old_file(void)
{
    char buf[sizeof k_old];
    u16 got;

    return tfs_read_all(tfs_path("X.CSV", ""), buf, (u16)sizeof buf,
                        &got) == 0 &&
           got == sizeof k_old - 1 && memcmp(buf, k_old, got) == 0;
}

static void sweep_export(int replace, i32 step)
{
    i32 total;
    i32 n;
    int ok;

    run_export(replace, 0);
    total = (i32)tfs_calls();
    printf("T_CSVF: export%s: %ld disk-changing calls\n",
           replace ? " with REPLACE" : "", (long)total);
    if (!replace)
        TF_EQ(tfs_copy(tfs_path("X.CSV", ""), tfs_path("GOOD.CSV", "")), 0);
    TF_CHECK(tfs_same(tfs_path("X.CSV", ""), tfs_path("GOOD.CSV", "")));
    for (n = 1; n <= total + 1; n += step) {
        run_export(replace, n);
        ok = !tfs_exists(tfs_path("X.CSV", "")) ||
             tfs_same(tfs_path("X.CSV", ""), tfs_path("GOOD.CSV", "")) ||
             (replace && old_file());
        if (!TF_CHECK(ok)) {
            printf("T_CSVF: export%s %ld: partial target\n",
                   replace ? " with REPLACE" : "", (long)n);
            g_failed++;
        }
    }
}

int main(int argc, char **argv)
{
    i32 step;

    tf_begin("T_CSVF");
    if (argc < 2) {
        printf("usage: T_CSVF dir [step]\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    step = argc > 2 ? (i32)atol(argv[2]) : 1;
    if (step < 1)
        step = 1;
    make_state();
    sweep_import(ENG_T_ITEMS, "IN.CSV", NADD, step);
    sweep_import(ENG_T_LOCS, "IN2.CSV", NLOCS, step);
    sweep_export(0, step);
    sweep_export(1, step);
    if (g_failed > 0)
        printf("T_CSVF: %d cases failed\n", g_failed);
    remove_all();
    return tf_done();
}

/*
 * T_HEAL.C - the repairs an open makes after an interrupted change of
 * several items, when the machine stops or a call fails during them.
 *
 * usage: T_HEAL dir [step]
 *
 * dir is a scratch directory used only by this test (a scratch drive
 * under DOS).  One starting state is built in it with the engine and
 * saved under other extensions: an ASSY MOVE and an ASSY REMOVE cut off
 * after the commits of their first parts, a LOAN OUT and a LOAN RETURN
 * whose item's commit failed and a LOAN USE whose host's commit failed
 * (the records written straight into the tables, as such changes leave
 * them), and an edit marker naming ITEMS and LOANS.
 *
 * The state is opened undisturbed twice, counting the disk-changing
 * calls of eng_open_files alone and then those of the whole eng_open,
 * whose eng_init_data makes the repairs; the result (every part back at
 * its top-level item's location, every ON_LOAN the QTY of its item's
 * open loans, the host's ATTACHED its open row) is checked, and
 * ITEMS.DBF, LOANS.DBF and the repair's audit rows are kept as the
 * reference.  Then for every call number k of
 * the repairs (every step-th one, default 1) the state is restored and
 * opened with eng_open, the fault recorder of T_FILES.H making the k-th
 * call of the repairs fail, once ("single"), or with every later call
 * failing too, the close included ("crash", as when the machine stops).
 * An open that fails must say in its why text that setting up the data
 * directory failed.  Afterwards the marker must still be there (after a
 * crash, or after a single failure that failed the open), and the first
 * open without faults must succeed and remove it, leave ITEMS.DBF and
 * LOANS.DBF byte for byte as the reference, every index holding exactly
 * one entry per record with the key its expression gives for that
 * record, and the audit rows after the state's own exactly the
 * undisturbed repair's rows (by SYSTEM, one per correction): each at
 * least once, as a correction cut short is made again with the same row,
 * and no other but the X rows of the reopen, which flag the rows a stop
 * left behind the marker (te_cut of TENGH.H).  The marker a stop leaves
 * keeps the count of the state's audit rows.
 *
 * Then two opens in a row in one process whose repairs fail each keep
 * the marker, still naming ITEMS and LOANS, and the open after them
 * removes it and gives the reference.  Then one correction committed
 * with eng_edit_heal inside a batch has its audit row in AUDIT.DBF and
 * the indexes of AUDIT on the disk when the commit returns: the batch's
 * end changes none of them, nor does the close after a row written
 * outside a batch.  Then an ITEM SPLIT whose new item reached the disk
 * but not its item's lower QTY is completed by the open, with one U row
 * by SYSTEM, unless a later change of a QTY is among the rows the
 * marker covers; a whole split is left as it is.  Then an ITEM SPLIT
 * with each of its disk-changing calls failing in turn, once or with
 * every later one ("single", "crash"), leaves after the next open the
 * units it split neither doubled nor lost, and every index whole, also
 * when after a single failure the session added an item.  Last, cuts
 * of a split that the rules of the open leave alone or complete: its
 * SYSTEM rows skipped, the previous row of the last user QTY row an
 * insert (a row of another table between them counting as that row),
 * only ITEMS rows, the items live and holding what the row says, and
 * the new QTY no less than the units the item's open rows hold.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGEDIT.H"
#include "ENGITEM.H"
#include "ENGASSY.H"
#include "ENGLOAN.H"
#include "TESTFW.H"
#include "T_FILES.H"
#define TE_AUDCUT
#include "TENGH.H"
#include "TEWIPE.H"

#define NREF            6       /* rows of the undisturbed repair       */

static u32 g_naud;              /* audit rows of the starting state     */
static int g_failed;            /* cases that failed, for the summary   */

/* The rows of the undisturbed repair, one per correction, as te_aud
   gives the fields k_row. */
static char g_ref[NREF][TE_TEXTMAX];
static const char k_row[] = "STATION USERNAME REC_ID ACTION FLD OLDVAL "
                            "NEWVAL";

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

/* ---------------------------------------------------------------------
 * The starting state.
 * --------------------------------------------------------------------- */

/* A new item at location loc (a code) with quantity qty. */
static u32 item(eng *e, const char *loc, const char *qty)
{
    char spec[TE_TEXTMAX];
    u32 id;

    id = 0;
    sprintf(spec, "CATEGORY=SYS|LOC_ID=%s|QTY=%s", loc, qty);
    TF_EQ(eng_item_add(e, te_p, te_kv(spec), &id), ENG_OK);
    return id;
}

/* A loan of qty of item id; its LOAN_ID. */
static u32 lend(eng *e, u32 id, const char *qty)
{
    char spec[TE_TEXTMAX];
    u32 loan;

    loan = 0;
    sprintf(spec, "QTY=%s|HOLDER=PAT|DUE=20261231", qty);
    TF_EQ(eng_loan_out(e, id, ENG_K_LOAN, te_p, te_kv(spec), &loan), ENG_OK);
    return loan;
}

/* Field name of item id written straight into ITEMS.DBF (a number, or
   text when text is not NULL), its index entries left as they were. */
static void raw_put(u32 id, const char *name, const char *text, i32 num)
{
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), -1, name, text, num), 0);
}

/* The items, in ID order: an assembly 1 (parts 2 and 4, 3 under 2) and
   an assembly 5 (part 6, 7 under 6), then the lent items 8 and 9, a
   unit of 8 in use with 9. */
static void make_state(void)
{
    eng *e;
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 t;
    u32 r;
    u32 s;
    u32 la;
    u32 lb;
    u32 u;

    tfs_fault_off();
    te_remove(tfs_dir(), NULL);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    eng_set_actor(e, "T_HEAL", "TEST");
    TF_EQ(te_loc("A1", 0), 1);
    TF_EQ(te_loc("B1", 0), 2);
    TF_EQ(te_loc("C1", 0), 3);
    a = item(e, "A1", "1");
    b = item(e, "A1", "1");
    c = item(e, "A1", "1");
    d = item(e, "A1", "1");
    t = item(e, "B1", "1");
    r = item(e, "A1", "1");
    s = item(e, "A1", "1");
    TF_EQ(eng_assy_install(e, b, a), ENG_OK);
    TF_EQ(eng_assy_install(e, c, b), ENG_OK);
    TF_EQ(eng_assy_install(e, d, a), ENG_OK);
    TF_EQ(eng_assy_install(e, s, r), ENG_OK);
    TF_EQ(eng_assy_install(e, r, t), ENG_OK);
    la = item(e, "C1", "5");
    lb = item(e, "C1", "3");
    lend(e, la, "2");
    lend(e, la, "1");
    TF_EQ(eng_loan_return(e, lend(e, lb, "3"), NULL), ENG_OK);
    TF_EQ(eng_loan_out(e, la, ENG_K_USE, te_p, te_kv("HOST=9"), &u), ENG_OK);
    /* ASSY MOVE 1 LOC=C1 after the commits of parts 2 and 3; ASSY
       REMOVE 6 LOC=C1 after that of part 7. */
    raw_put(b, "LOC_ID", NULL, 3);
    raw_put(c, "LOC_ID", NULL, 3);
    raw_put(s, "LOC_ID", NULL, 3);
    /* The second LOAN OUT of item 8 and the LOAN RETURN of item 9, their
       item's commit failed. */
    raw_put(la, "ON_LOAN", NULL, 2);
    raw_put(lb, "ON_LOAN", NULL, 3);
    raw_put(lb, "ATTACHED", NULL, 0);
    g_naud = te_naud();
    TF_EQ(eng_close(e), ENG_OK);
    TF_EQ(te_marker_put("ITEMS|LOANS"), 0);
    TF_EQ(te_save(1), 0);
    TF_EQ(a, 1);
    TF_EQ(lb, 9);
}

/* ---------------------------------------------------------------------
 * The checks.
 * --------------------------------------------------------------------- */

/* 1 when the marker exists, covers the audit rows after the state's
   own and names ITEMS and LOANS. */
static int marker_names(void)
{
    char text[ENG_MARKMAX + 1];
    char head[ENG_MARKHEAD + 1];
    u16 got;

    if (tfs_read_all(tfs_path(ENG_EDITMARK, ""), text, ENG_MARKMAX,
                     &got) != 0)
        return 0;
    text[got] = '\0';
    sprintf(head, "%lu\r\n", (unsigned long)g_naud);
    return strncmp(text, head, strlen(head)) == 0 &&
           strstr(text, "ITEMS\r\n") != NULL &&
           strstr(text, "LOANS\r\n") != NULL;
}

/* 1 when stem.DBF and its reference stem.RDF hold the same bytes. */
static int same_as_ref(const char *stem)
{
    return tfs_same(tfs_path(stem, ".DBF"), tfs_path(stem, ".RDF"));
}

/* The repaired items, checked once on the undisturbed repair. */
static void check_repair(void)
{
    static const char *const want[] = {
        "1", "1", "1", "1", "2", "2", "2", "3", "3"
    };
    u16 i;

    for (i = 0; i < (u16)(sizeof want / sizeof want[0]); i++)
        TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec((u32)i + 1u), "LOC_ID"),
                 want[i]);
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(6), "PARENT_ID"), "5");
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(8), "ON_LOAN"), "3");
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(9), "ON_LOAN"), "0");
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(8), "IN_USE"), "1");
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(9), "ATTACHED"), "1");
    TF_EQ(te_naud(), g_naud + NREF);
}

/* Keep the rows of the undisturbed repair, each by SYSTEM. */
static void keep_rows(void)
{
    u16 i;

    if (!TF_EQ(te_naud() - g_naud, NREF))
        return;
    for (i = 0; i < NREF; i++) {
        strcpy(g_ref[i], te_aud(g_naud + 1u + i, k_row));
        TF_EQ(strncmp(g_ref[i], "SYSTEM|SYSTEM|", 14), 0);
    }
}

/* 1 when every audit row after the state's own is one of the reference
   rows (by SYSTEM, so a repeated correction's row may come twice) or an
   X row, and every reference row is among them (no correction without
   its row). */
static int audit_ok(void)
{
    const char *row;
    int seen[NREF];
    u32 r;
    u16 i;

    for (i = 0; i < NREF; i++)
        seen[i] = 0;
    for (r = g_naud + 1; r <= te_naud(); r++) {
        if (strcmp(te_rtext(ENG_T_AUDIT, r, "ACTION"), "X") == 0)
            continue;
        row = te_aud(r, k_row);
        i = 0;
        while (i < NREF && strcmp(row, g_ref[i]) != 0)
            i++;
        if (i == NREF) {
            printf("T_HEAL: stray audit row %s\n", row);
            return 0;
        }
        seen[i] = 1;
    }
    for (i = 0; i < NREF; i++)
        if (!seen[i]) {
            printf("T_HEAL: audit row missing: %s\n", g_ref[i]);
            return 0;
        }
    return 1;
}

/* 0 when the directory reopens as the reference, with the rows a stop
   left after the state's own flagged by an X row when the marker stayed,
   else 1 after printing why. */
static int verify(const char *mode, i32 k, int rc0)
{
    eng *e;
    u32 left;
    int marked;
    int bad;
    int cut;
    int rc;
    int i;

    bad = 0;
    tfs_fault_off();
    left = te_disk_count(ENG_T_AUDIT);
    marked = tfs_exists(tfs_path(ENG_EDITMARK, ""));
    rc = te_open(&e, &g_now);
    if (rc != ENG_OK) {
        printf("T_HEAL: %s %ld (open %d): reopen %d\n", mode, (long)k, rc0,
               rc);
        return 1;
    }
    if (tfs_exists(tfs_path(ENG_EDITMARK, "")))
        bad = 1;
    for (i = 0; i < ENG_NINDEX; i++)
        if (!te_index_ok(e, i))
            bad = 2;
    cut = te_cut(g_naud);
    if (!audit_ok() || cut < 0 || (marked && left > g_naud && cut == 0))
        bad = 3;
    eng_close(e);
    if (!same_as_ref("ITEMS") || !same_as_ref("LOANS"))
        bad = 4;
    if (bad)
        printf("T_HEAL: %s %ld (open %d, op %d): check %d\n", mode,
               (long)k, rc0, tfs_failop(), bad);
    return bad != 0;
}

/* ---------------------------------------------------------------------
 * The runs.
 * --------------------------------------------------------------------- */

/* The undisturbed repair, its calls counted: *files is the count of
   eng_open_files' calls, *total that of the repairs'.  0 when an open
   failed. */
static int undisturbed(i32 *files, i32 *total)
{
    char why[TFS_WHYSZ];
    eng *e;
    int rc;

    TF_EQ(te_save(0), 0);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    rc = eng_open_files(&e, tfs_dir(), &g_now, why, (u16)sizeof why);
    tfs_fault_off();
    *files = (i32)tfs_calls();
    if (!TF_EQ(rc, ENG_OK))
        return 0;
    TF_EQ(eng_marker_kept(e), 1);
    eng_close(e);

    TF_EQ(te_save(0), 0);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    rc = te_open(&e, &g_now);
    tfs_fault_off();
    *total = (i32)tfs_calls() - *files;
    if (!TF_EQ(rc, ENG_OK))
        return 0;
    TF_CHECK(!tfs_exists(tfs_path(ENG_EDITMARK, "")));
    check_repair();
    keep_rows();
    TF_CHECK(audit_ok());
    TF_EQ(eng_close(e), ENG_OK);
    TF_EQ(tfs_copy(tfs_path("ITEMS", ".DBF"), tfs_path("ITEMS", ".RDF")),
          0);
    TF_EQ(tfs_copy(tfs_path("LOANS", ".DBF"), tfs_path("LOANS", ".RDF")),
          0);
    return 1;
}

/* Restore the state and open it with the recorder failing call failat
   (one of the repairs'), and every later one too when crash; the open's
   result.  An open that fails names the repairs in its why text. */
static int disturbed_open(i32 failat, int crash)
{
    static const char k_setup[] = "setting up the data directory";
    char why[TFS_WHYSZ];
    eng *e;
    int rc;

    TF_EQ(te_save(0), 0);
    tfs_fault(TFS_ALLOPS, (u32)failat, crash ? 0 : 1, 0);
    rc = eng_open(&e, tfs_dir(), &g_now, why, (u16)sizeof why);
    if (rc == ENG_OK)
        eng_close(e);
    else
        TF_CHECK(strncmp(why, k_setup, sizeof k_setup - 1) == 0);
    tfs_fault_off();
    return rc;
}

/* Every disturbed repair, after the undisturbed one; files is the count
   of eng_open_files' calls, total that of the repairs'. */
static void sweep(i32 files, i32 total, i32 step)
{
    i32 k;
    int rc;
    int crash;

    for (crash = 0; crash < 2; crash++)
        for (k = 1; k <= total; k += step) {
            rc = disturbed_open(files + k, crash);
            if ((crash || rc != ENG_OK) && !marker_names()) {
                printf("T_HEAL: %s %ld (open %d): marker gone\n",
                       crash ? "crash" : "single", (long)k, rc);
                g_failed++;
                TF_CHECK(0);
            }
            if (!TF_EQ(verify(crash ? "crash" : "single", k, rc), 0))
                g_failed++;
        }
}

/* Two opens of the state in a row whose repairs fail (the files opened
   undisturbed, then eng_init_data with a call failing, then the close,
   as eng_open does): the first part-way through, at call k, the second
   at its first call, then a clean open. */
static void twice(i32 k)
{
    char why[TFS_WHYSZ];
    eng *e;
    int i;

    TF_EQ(te_save(0), 0);
    for (i = 0; i < 2; i++) {
        tfs_fault_off();
        if (!TF_EQ(eng_open_files(&e, tfs_dir(), &g_now, why,
                                  (u16)sizeof why), ENG_OK))
            return;
        TF_EQ(eng_marker_kept(e), 1);
        tfs_fault(TFS_ALLOPS, i == 0 ? (u32)k : 1u, 1, 0);
        TF_CHECK(eng_init_data(e) != ENG_OK);
        tfs_fault_off();
        eng_close(e);
        TF_CHECK(marker_names());
    }
    TF_EQ(verify("twice", k, 0), 0);
}

/* Copy AUDIT.DBF and its indexes as they are on the disk to their
   references (.RDF, .RNX), for audit_kept. */
static void audit_keep(void)
{
    const char *name;
    int x;

    TF_EQ(tfs_copy(tfs_path("AUDIT", ".DBF"), tfs_path("AUDIT", ".RDF")),
          0);
    for (x = ENG_X_AUDIT1; x <= ENG_X_AUDIT2; x++) {
        name = eng_index_def(x)->name;
        TF_EQ(tfs_copy(tfs_path(name, ".NDX"), tfs_path(name, ".RNX")), 0);
    }
}

/* Check that AUDIT.DBF and its indexes are still as audit_keep copied
   them. */
static void audit_kept(void)
{
    const char *name;
    int x;

    TF_CHECK(same_as_ref("AUDIT"));
    for (x = ENG_X_AUDIT1; x <= ENG_X_AUDIT2; x++) {
        name = eng_index_def(x)->name;
        TF_CHECK(tfs_same(tfs_path(name, ".NDX"), tfs_path(name, ".RNX")));
    }
}

/* One correction through eng_edit_heal on the repaired directory, item
   1 moved to B1 inside a batch, as the repairs run: its row is written,
   and AUDIT.DBF and its indexes are on the disk when the commit
   returns, so the batch's end, which flushes every file, changes none
   of them.  A row written outside a batch is on the disk at its end
   likewise, so the close changes none of them. */
static void heal_flushes(void)
{
    char was[ENG_MAXVAL];
    eng_audrow row;
    eng *e;
    u32 r;
    u32 n;
    int f;
    int x;

    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    r = te_irec(1);
    n = te_naud();
    strcpy(was, te_rtext(ENG_T_ITEMS, r, "LOC_ID"));
    f = eng_field_num(e, ENG_T_ITEMS, "LOC_ID");
    if (!TF_CHECK(f >= 0)) {
        eng_close(e);
        return;
    }
    TF_EQ(eng_batch_begin(e, ENG_T_ITEMS), ENG_OK);
    TF_EQ(eng_edit_begin(e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_edit_putf(e, (u16)f, "2"), ENG_OK);
    TF_EQ(eng_edit_heal(e, (u16)f, was, ENG_A_MOVE, 1), ENG_OK);
    audit_keep();
    TF_EQ(eng_batch_end(e, ENG_OK), ENG_OK);
    audit_kept();
    for (x = ENG_X_AUDIT1; x <= ENG_X_AUDIT2; x++)
        TF_CHECK(te_index_ok(e, x));
    TF_EQ(te_naud(), n + 1);
    TF_STREQ(te_aud(n + 1, "TBL REC_ID ACTION FLD OLDVAL NEWVAL"),
             "ITEMS|1|M|LOC_ID|1|2");
    row.action = ENG_A_UPDATE;
    row.fld = "QTY";
    row.oldval = "1";
    row.newval = "2";
    TF_EQ(eng_audit(e, ENG_T_ITEMS, 1, &row), ENG_OK);
    audit_keep();
    TF_EQ(eng_close(e), ENG_OK);
    audit_kept();
}

/* ITEM SPLIT 2 of item 1's 5 into item 2, three ways: cut off before
   item 1's rewrite, cut off then followed by a change of item 2's QTY,
   and whole; each time the marker a stop leaves (its count before the
   split, naming ITEMS) and the open's result. */
static void split_heal(void)
{
    static const char *const want[] = { "3", "5", "3" };
    char mark[TE_TEXTMAX];
    eng *e;
    u32 m;
    u32 n;
    int i;

    for (i = 0; i < 3; i++) {
        tfs_fault_off();
        te_remove(tfs_dir(), NULL);
        if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
            return;
        TF_EQ(te_loc("A1", 0), 1);
        TF_EQ(item(e, "A1", "5"), 1);
        n = te_naud();
        TF_EQ(eng_item_split(e, 1, "2", &m), ENG_OK);
        TF_EQ(m, 2);
        if (i < 2)
            raw_put(1, "QTY", NULL, 5);
        if (i == 1)
            TF_EQ(eng_item_set(e, 2, te_p, te_kv("QTY=4")), ENG_OK);
        TF_EQ(eng_close(e), ENG_OK);
        sprintf(mark, "%lu|ITEMS", (unsigned long)n);
        TF_EQ(te_marker_put(mark), 0);
        if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
            return;
        TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(1), "QTY"), want[i]);
        TF_STREQ(te_aud(te_naud(), "USERNAME ACTION FLD OLDVAL"),
                 i == 0 ? "SYSTEM|U|QTY|5" : "SYSTEM|X|AUD_ID|");
        TF_EQ(eng_close(e), ENG_OK);
    }
}

/* 1 when the directory opens with its live items 1 to 3 holding want
   units between them and every index whole, else 0 after printing
   why. */
static int split_whole(const char *mode, u32 k, i32 want)
{
    eng *e;
    u32 id;
    u32 r;
    int bad;
    int x;

    if (te_open(&e, &g_now) != ENG_OK) {
        printf("T_HEAL: split %s %lu: reopen\n", mode, (unsigned long)k);
        return 0;
    }
    for (id = 1; id <= 3; id++)
        if (eng_item_get(e, id, &r) == ENG_OK)
            want -= te_rnum(ENG_T_ITEMS, r, "QTY");
    bad = want != 0;
    for (x = 0; x < ENG_NINDEX; x++)
        if (!te_index_ok(e, x))
            bad = 1;
    eng_close(e);
    if (bad)
        printf("T_HEAL: split %s %lu: units or index\n", mode,
               (unsigned long)k);
    return !bad;
}

/* ITEM SPLIT 1 QTY=2 of a QTY of 5 with each of its disk-changing calls
   failing in turn, once or with every later one too (a stop): the open
   after it finds the 5 units neither doubled nor lost.  After a single
   failure the session goes on with an ITEM ADD of 3 units, whose I row
   may follow rows the split left: the open finds 8. */
static void split_cut(void)
{
    eng *e;
    u32 total;
    u32 k;
    u32 m;
    i32 want;
    int crash;

    tfs_fault_off();
    te_remove(tfs_dir(), NULL);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    TF_EQ(te_loc("A1", 0), 1);
    TF_EQ(item(e, "A1", "5"), 1);
    TF_EQ(eng_close(e), ENG_OK);
    TF_EQ(te_save(1), 0);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(eng_item_split(e, 1, "2", &m), ENG_OK);
    tfs_fault_off();
    total = tfs_calls();
    TF_EQ(eng_close(e), ENG_OK);
    for (crash = 0; crash < 2; crash++)
        for (k = 1; k <= total; k++) {
            TF_EQ(te_save(0), 0);
            if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
                return;
            tfs_fault(TFS_ALLOPS, k, crash ? 0 : 1, 0);
            eng_item_split(e, 1, "2", &m);
            want = 5;
            if (!crash) {
                tfs_fault_off();
                if (eng_item_add(e, te_p, te_kv("CATEGORY=SYS|QTY=3"),
                                 &m) == ENG_OK)
                    want += 3;
            }
            eng_close(e);
            tfs_fault_off();
            if (!TF_CHECK(split_whole(crash ? "crash" : "single", k,
                                      want)))
                g_failed++;
        }
}

/* ---------------------------------------------------------------------
 * The rules of the split repair, each case failing when its rule is
 * dropped.
 * --------------------------------------------------------------------- */

#define NRULE           14

static const char *const k_rule[NRULE] = {
    "previous row is another item's change", "previous row is a loan's",
    "later QTY row", "QTY row of another table", "NEWVAL not a number",
    "NEWVAL with a leading zero", "new item's QTY changed",
    "item's QTY changed", "new item deleted", "item deleted",
    "previous row is a location's", "item lent meanwhile",
    "item lent its new QTY", "whole cut"
};

/* A fresh directory with location A1 and item 1 of 5 units; the open
   engine, or NULL when that failed. */
static eng *rule_start(void)
{
    eng *e;

    tfs_fault_off();
    te_remove(tfs_dir(), NULL);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return NULL;
    TF_EQ(te_loc("A1", 0), 1);
    TF_EQ(item(e, "A1", "5"), 1);
    return e;
}

/* ITEM SPLIT 1 QTY=2 whose rewrite of item 1 never reached the disk. */
static void rule_cut(eng *e)
{
    u32 m;

    TF_EQ(eng_item_split(e, 1, "2", &m), ENG_OK);
    TF_EQ(m, 2);
    raw_put(1, "QTY", NULL, 5);
}

/* An ITEM SET of item 1 (spec), its record write lost. */
static void rule_set(eng *e, const char *spec)
{
    TF_EQ(eng_item_set(e, 1, te_p, te_kv(spec)), ENG_OK);
    raw_put(1, "QTY", NULL, 5);
}

/* The marker a stop leaves, with the audit count n; then an open, which
   must remove it and leave item 1 with q1 units, item 2 with q2 and
   every index whole, and have written rows SYSTEM U QTY rows after n. */
static void rule_open(const char *name, u32 n, const char *q1,
                      const char *q2, u32 rows)
{
    char mark[TE_TEXTMAX];
    eng *e;
    u32 r;
    u32 sys;
    int bad;
    int x;

    sprintf(mark, "%lu|ITEMS", (unsigned long)n);
    TF_EQ(te_marker_put(mark), 0);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    bad = tfs_exists(tfs_path(ENG_EDITMARK, ""));
    bad |= strcmp(te_rtext(ENG_T_ITEMS, te_irec(1), "QTY"), q1) != 0;
    bad |= strcmp(te_rtext(ENG_T_ITEMS, te_irec(2), "QTY"), q2) != 0;
    sys = 0;
    for (r = n + 1; r <= te_naud(); r++)
        if (strcmp(te_aud(r, "USERNAME TBL ACTION FLD"),
                   "SYSTEM|ITEMS|U|QTY") == 0)
            sys++;
    bad |= sys != rows;
    for (x = 0; x < ENG_NINDEX; x++)
        if (!te_index_ok(e, x))
            bad = 1;
    TF_EQ(eng_close(e), ENG_OK);
    if (bad)
        printf("T_HEAL: split rule \"%s\": item 1 %s, item 2 %s, %lu "
               "SYSTEM rows\n", name, te_rtext(ENG_T_ITEMS, te_irec(1),
               "QTY"), te_rtext(ENG_T_ITEMS, te_irec(2), "QTY"),
               (unsigned long)sys);
    if (!TF_CHECK(!bad))
        g_failed++;
}

/* A repair already cut after its SYSTEM row: the row is skipped, so the
   open completes the split once more, and one after that finds item 1
   as it is. */
static void split_sysskip(void)
{
    eng *e;
    u32 n;

    if ((e = rule_start()) == NULL)
        return;
    n = te_naud();
    rule_cut(e);
    TF_EQ(eng_close(e), ENG_OK);
    rule_open("first repair", n, "3", "2", 1);
    if (!TF_EQ(te_open(&e, &g_now), ENG_OK))
        return;
    raw_put(1, "QTY", NULL, 5);
    TF_EQ(eng_close(e), ENG_OK);
    rule_open("SYSTEM row skipped", n, "3", "2", 2);
    rule_open("repeated", n, "3", "2", 2);
}

/* Cuts that the open leaves alone or completes by its rules: the row
   before the last user QTY row must be an insert, only ITEMS rows
   count, NEWVAL must be the difference as plain decimal text, both
   items must be live and hold what the rows say, and the new QTY must
   cover the units the item's open rows hold. */
static void split_rules(void)
{
    eng *e;
    u32 n;
    u32 loan;
    const char *q1;
    const char *q2;
    u32 rows;
    int c;

    for (c = 0; c < NRULE; c++) {
        if ((e = rule_start()) == NULL)
            return;
        n = te_naud();
        q1 = "5";
        q2 = "2";
        rows = 0;
        switch (c) {
        case 0:
            TF_EQ(item(e, "A1", "2"), 2);
            TF_EQ(eng_item_set(e, 2, te_p, te_kv("MFR=X")), ENG_OK);
            rule_set(e, "QTY=3");
            break;
        case 1:
            TF_EQ(item(e, "A1", "2"), 2);
            loan = lend(e, 2, "1");
            TF_EQ(eng_loan_set(e, loan, te_p, te_kv("CONTACT=Bob")),
                  ENG_OK);
            rule_set(e, "QTY=3");
            break;
        case 2:
            rule_cut(e);
            rule_set(e, "QTY=4");
            break;
        case 3:
            rule_cut(e);
            rule_set(e, "QTY=4");
            TF_STREQ(te_aud(te_naud(), "TBL ACTION FLD OLDVAL NEWVAL"),
                     "ITEMS|U|QTY|5|4");
            TF_EQ(te_raw_put(ENG_T_AUDIT, te_naud(), ENG_X_AUDIT1, "TBL",
                             "LOANS", 0), 0);
            q1 = "3";
            rows = 1;
            break;
        case 4:
        case 5:
            rule_cut(e);
            TF_STREQ(te_aud(n + 2, "TBL ACTION FLD OLDVAL NEWVAL"),
                     "ITEMS|U|QTY|5|3");
            TF_EQ(te_raw_put(ENG_T_AUDIT, n + 2, -1, "NEWVAL",
                             c == 4 ? "x" : "03", 0), 0);
            break;
        case 6:
            rule_cut(e);
            raw_put(2, "QTY", NULL, 1);
            q2 = "1";
            break;
        case 7:
            rule_cut(e);
            raw_put(1, "QTY", NULL, 4);
            q1 = "4";
            break;
        case 8:
            rule_cut(e);
            TF_EQ(eng_item_del(e, 2), ENG_OK);
            break;
        case 9:
            rule_cut(e);
            TF_EQ(eng_item_del(e, 1), ENG_OK);
            break;
        case 10:
            TF_EQ(item(e, "A1", "2"), 2);
            TF_EQ(te_loc("A2", 0), 2);
            rule_set(e, "QTY=3");
            break;
        case 11:
            rule_cut(e);
            lend(e, 1, "4");
            break;
        case 12:
            rule_cut(e);
            lend(e, 1, "3");
            q1 = "3";
            rows = 1;
            break;
        default:
            rule_cut(e);
            q1 = "3";
            rows = 1;
        }
        TF_EQ(eng_close(e), ENG_OK);
        rule_open(k_rule[c], n, q1, q2, rows);
    }
}

int main(int argc, char **argv)
{
    static const char *const k_refs[] = {
        "ITEMS.RDF", "LOANS.RDF", "AUDIT.RDF", "AUDIT1.RNX", "AUDIT2.RNX",
        NULL
    };
    i32 step;
    i32 files;
    i32 total;

    tf_begin("T_HEAL");
    if (argc < 2) {
        printf("usage: T_HEAL dir [step]\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    step = argc > 2 ? (i32)atol(argv[2]) : 1;
    if (step < 1)
        step = 1;
    make_state();
    if (undisturbed(&files, &total)) {
        printf("T_HEAL: %ld disk-changing calls, %ld of them repairs\n",
               (long)(files + total), (long)total);
        sweep(files, total, step);
        twice(total / 2);
        heal_flushes();
        split_heal();
        split_cut();
        split_sysskip();
        split_rules();
    }
    if (g_failed > 0)
        printf("T_HEAL: %d cases failed\n", g_failed);
    tfs_fault_off();
    te_remove(tfs_dir(), k_refs);
    te_save(1);
    return tf_done();
}

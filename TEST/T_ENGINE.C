/*
 * T_ENGINE.C - unit test for the item, location and category rules of
 * the engine (ITEMS.C, LOCS.C), run on a real data directory, and for
 * its numbered texts (CSCHEMA.C).
 *
 * usage: T_ENGINE [dir]
 *
 * dir is a scratch directory (default the current one); every engine
 * file in it is removed before each phase and at the end.  The clock is
 * fixed, so dates are known.
 *
 * Phases:
 *   items      ADD defaults and checks, GET, SET with the UPDATED stamp,
 *              DEL, UNDEL with its checks, COPY, notes and their limit,
 *              the field rules of COND, WORKS and TAGS,
 *              the location rule of assemblies and the DEL rules for
 *              children and loans (PARENT_ID and ON_LOAN, which no item
 *              command sets, are written straight into the table and its
 *              index here), and the audit rows of each change;
 *   lists      every ITEM LIST order paged two entries at a time, with
 *              runs of equal keys across page ends, the filters, ITEM
 *              SERIAL, ITEM FIND, the cursor text, bad cursors, and a
 *              cursor that stays valid while the entries around it
 *              change;
 *   locations  LOC ADD/SET/DEL/GET and notes, codes, names, the tree
 *              (moves, cycles, depth, paths), occupancy;
 *   walks      LOC LIST and LOC CONTENTS in tree order, paged, with
 *              their cursors, the read limit and a damaged tree;
 *   categories CAT and LTYPE ADD/SET/DEL, LIST order, the ENG_MAXCODES
 *              limit with reuse of deleted records;
 *   bounded    ITEM FIND across pages stopped by ENG_MAXREADS, and LOC
 *              DEL and ITEM DEL (children) with exactly ENG_MAXREADS
 *              deleted entries to read (accepted) and with one more
 *              (refused, unsettled).  Its filler items are copies
 *              appended straight to ITEMS.DBF and its indexes, deleted
 *              and moved there too, so they write no audit rows and the
 *              data stays small;
 *   texts      the numbered texts copied out of far data, and the text
 *              and number a failure records;
 *   tags       ITEM TAGS: repeats, first spellings, values another
 *              program left, deleted records, pages of ENG_MAXREADS
 *              records and pages a full set ends early; the TAG= and
 *              WORKS= filters on such values.
 * Every failing call is checked for its code and the field it names
 * (some for their message too), and for leaving the record count of
 * every table and the edit marker unchanged (TE_FAIL of TENGH.H).
 *
 * This file holds the helpers, the items phase and main; the other
 * phases are in T_ENGIN2.C, T_ENGIN3.C and T_ENGIN4.C, and T_ENGINE.H
 * declares what they share.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGNOTE.H"
#include "ENGID.H"
#include "ENGCODE.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
/* Checks keep no file name or expression text: Turbo C stores a
   separate copy of every string literal, which would cost this test
   kilobytes of near data. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"
#include "T_ENGINE.H"

eng *g_e;
char *g_big;
static const eng_time t0 = { 2026, 10, 8, 12, 0, 0 };
static const eng_time t1 = { 2026, 10, 9, 9, 30, 0 };

/* ---------------------------------------------------------------------
 * The engine, expected successes and reading back.
 * --------------------------------------------------------------------- */

/* Remove every engine file of the scratch directory and open a fresh
   engine there (te_open: the clock t0, the actor STA1/TESTER); 0, with
   a failed check, when the open fails. */
int phase_start(void)
{
    te_remove(tfs_dir(), NULL);
    return TF_EQ(te_open(&g_e, &t0), ENG_OK);
}

/* Close the engine of phase_start and remove its files. */
void phase_end(void)
{
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    te_remove(tfs_dir(), NULL);
}

/* Check that rc (of a call at file and line, file as TF_FILE gives it)
   is ENG_OK, printing the engine's error when it is not; the error is
   cleared either way. */
int expect_ok(int rc, const char *file, int line)
{
    if (rc != ENG_OK)
        printf("line %d: got %d (%s %s)\n", line, rc, eng_errfield(g_e),
               eng_errmsg(g_e));
    eng_clear_error(g_e);
    return tf_check(rc == ENG_OK, file, line, "OK");
}

/* ITEM ADD of the pairs of spec (as te_kv splits it), checked to
   succeed; the new item's ID, 0 when the add failed. */
u32 add_item(const char *spec)
{
    u32 id;

    id = 0;
    if (!OK(eng_item_add(g_e, te_p, te_kv(spec), &id)))
        printf("  ITEM ADD %s\n", spec);
    return id;
}

/* Field name of live item id, or "?" when GET fails. */
static const char *ifield(u32 id, const char *name)
{
    u32 r;

    if (eng_item_get(g_e, id, &r) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_ITEMS, TE_BUF, name);
}

/* The k-th audit row from the end (1: the last) as te_aud joins its
   fields TBL REC_ID ACTION FLD OLDVAL NEWVAL, "?" when there is none. */
const char *aud(u32 k)
{
    return te_aud(te_naud() + 1 - k, "TBL REC_ID ACTION FLD OLDVAL NEWVAL");
}

/* The last two audit rows are x and y, in either order. */
static int two_rows(const char *x, const char *y)
{
    char last[TE_TEXTMAX];

    strcpy(last, aud(1));
    if (strcmp(last, x) == 0)
        return strcmp(aud(2), y) == 0;
    return strcmp(last, y) == 0 && strcmp(aud(2), x) == 0;
}

/* ---------------------------------------------------------------------
 * Fields no item command sets, written straight into the table.
 * --------------------------------------------------------------------- */

/* PARENT_ID of item child, with its ITEMS3 entry moved. */
void set_parent(u32 child, u32 parent)
{
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(child), ENG_X_ITEMS3, "PARENT_ID",
                     NULL, (i32)parent), ENG_OK);
}

/* ON_LOAN of item id (no index holds it). */
static void set_onloan(u32 id, i32 qty)
{
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), -1, "ON_LOAN", NULL, qty),
          ENG_OK);
}

/* count copies of item src appended straight to ITEMS.DBF with the next
   IDs and entered in every ITEMS index; SEQ is raised afterwards. */
void raw_copies(u32 src, u16 count)
{
    const eng_tdef *td;
    dbf *d;
    u8 key[ENG_MAXKEYLEN];
    u32 r;
    u32 id;
    u16 fid;
    u16 i;
    int x;
    int ok;

    td = eng_table_def(ENG_T_ITEMS);
    d = eng_dbf(g_e, ENG_T_ITEMS);
    fid = (u16)eng_field_num(g_e, ENG_T_ITEMS, "ITEM_ID");
    if (!TF_EQ(eng_item_get(g_e, src, &r), ENG_OK))
        return;
    id = eng_reccount(g_e, ENG_T_ITEMS);
    ok = 1;
    for (i = 0; i < count && ok; i++) {
        id++;
        ok = dbf_put_num(d, fid, (i32)id) == DBF_OK &&
             dbf_append(d) == DBF_OK;
        for (x = td->firstx; ok && x < td->firstx + td->nindex; x++)
            ok = kx_eval(eng_kx(g_e, x), d, key) == DBF_OK &&
                 ndx_insert(eng_ndx(g_e, x), key, dbf_recno(d)) == DBF_OK;
    }
    TF_CHECK(ok);
    TF_EQ(eng_flush(g_e), ENG_OK);
    TF_EQ(eng_seq_heal(g_e), ENG_OK);
}

/* Mark live item id deleted in ITEMS.DBF, without an audit row. */
void raw_delete(u32 id)
{
    dbf *d;
    u32 r;

    d = eng_dbf(g_e, ENG_T_ITEMS);
    if (TF_EQ(eng_item_get(g_e, id, &r), ENG_OK)) {
        dbf_set_deleted(d, 1);
        TF_EQ(dbf_write(d), DBF_OK);
    }
}

/* ---------------------------------------------------------------------
 * Items.
 * --------------------------------------------------------------------- */
/* The items of the items phase, shared by its parts. */
static u32 g_it_a;
static u32 g_it_b;
static u32 g_it_c;
static u32 g_it_d;

/* ITEM ADD: defaults, checks and its audit row; GET. */
static void items_add(void)
{
    u32 id;
    u32 r;
    u32 a0;
    u16 len;

    TF_EQ(te_loc("A1", 0), 1);
    TF_EQ(te_loc("B1", 0), 2);

    /* ADD with defaults, and its one I row; a location by its code. */
    a0 = te_naud();
    g_it_a = add_item("CATEGORY=card|MFR=IBM|MODEL=CGA|LOC_ID=a1");
    TF_EQ(g_it_a, 1);
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "ITEMS|1|I|ITEM_ID||1");
    TF_STREQ(te_aud(te_naud(), "STATION USERNAME ADATE"),
             "STA1|TESTER|20261008");
    TF_STREQ(ifield(g_it_a, "CATEGORY"), "CARD");
    TF_STREQ(ifield(g_it_a, "LOC_ID"), "1");
    TF_STREQ(ifield(g_it_a, "QTY"), "1");
    TF_STREQ(ifield(g_it_a, "COND"), "");
    TF_STREQ(ifield(g_it_a, "WORKS"), "?");
    TF_STREQ(ifield(g_it_a, "STATUS"), "A");
    TF_STREQ(ifield(g_it_a, "PARENT_ID"), "0");
    TF_STREQ(ifield(g_it_a, "ON_LOAN"), "0");
    TF_STREQ(ifield(g_it_a, "CREATED"), "20261008");
    TF_STREQ(ifield(g_it_a, "UPDATED"), "20261008");
    g_it_b = add_item("CATEGORY=SYS|MFR=Compaq|MODEL=Portable|QTY=2|COND=gd|"
            "STATUS=d|ACQ_PRICE=1999.50|NOTES=line1\nline2");
    TF_EQ(g_it_b, 2);
    TF_STREQ(ifield(g_it_b, "COND"), "GD");
    TF_STREQ(ifield(g_it_b, "STATUS"), "D");
    TF_STREQ(ifield(g_it_b, "ACQ_PRICE"), "1999.50");
    TF_STREQ(ifield(g_it_b, "LOC_ID"), "0");
    if (OK(eng_item_notes(g_e, g_it_b, &len))) {
        TF_EQ(len, 12);
        TF_STREQ(eng_memobuf(g_e), "line1\r\nline2");
    }

    /* ADD failures: 102 with the field, 202 for unknown keys. */
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("MFR=X"), &id), ENG_EARG, "CATEGORY",
            NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=ZZZ"), &id), ENG_ECONSTR,
            "CATEGORY", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|LOC_ID=Q9"), &id),
            ENG_ECONSTR, "LOC_ID", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|LOC_ID=3"), &id),
            ENG_ECONSTR, "LOC_ID", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|LOC_ID=-1"), &id),
            ENG_ECONSTR, "LOC_ID", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|COND=XX"), &id),
            ENG_EARG, "COND", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|COND=WEX"), &id),
            ENG_EARG, "COND", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|COND=EX"), &id),
            ENG_EARG, "COND", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|COND=UT"), &id),
            ENG_EARG, "COND", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|WORKS="), &id),
            ENG_EARG, "WORKS", "works must be Y, N, I or ?");
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|WORKS=X"), &id),
            ENG_EARG, "WORKS", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|TAGS=a,b*"), &id),
            ENG_EARG, "TAGS", "each tag 1-12 of A-Z a-z 0-9 - _ . / +");
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|STATUS=X"), &id),
            ENG_EARG, "STATUS", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|QTY=-1"), &id),
            ENG_EARG, "QTY", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|CUR_VALUE=-0.01"),
                         &id), ENG_EARG, "CUR_VALUE", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|ITEM_ID=7"), &id),
            ENG_EARG, "ITEM_ID", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|ON_LOAN=1"), &id),
            ENG_EARG, "ON_LOAN", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|PARENT_ID=1"), &id),
            ENG_EARG, "PARENT_ID", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|CREATED=20200101"),
                         &id), ENG_EARG, "CREATED", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|BOGUS=1"), &id),
            ENG_EARG, "BOGUS", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CARD|NOTES=a\rb"), &id),
            ENG_EARG, "NOTES", NULL);
    /* No ID was used by the failures. */
    g_it_c = add_item("CATEGORY=CARD|MFR=ibm|MODEL=EGA|LOC_ID=1|"
                      "SERIALNO=s1");
    TF_EQ(g_it_c, 3);

    /* GET. */
    TF_EQ(eng_item_get(g_e, g_it_a, &r), ENG_OK);
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "MFR"), "IBM");
    TE_FAIL(eng_item_get(g_e, 99, &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_item_get(g_e, 0, &r), ENG_ENOTFOUND, "", NULL);
}

/* ITEM SET, the UPDATED stamp and the loan rules. */
static void items_set(void)
{
    u32 a0;

    /* SET stamps UPDATED, audits each changed field once; a LOC_ID
       change of a top-level item without children is an M row. */
    eng_set_now(g_e, &t1);
    a0 = te_naud();
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("QTY=3|MFR=IBM|MODEL=CGA Card")));
    TF_EQ(te_naud(), a0 + 2);
    TF_STREQ(aud(2), "ITEMS|1|U|MODEL|CGA|CGA Card");
    TF_STREQ(aud(1), "ITEMS|1|U|QTY|1|3");
    TF_STREQ(ifield(g_it_a, "UPDATED"), "20261009");
    TF_STREQ(ifield(g_it_a, "CREATED"), "20261008");
    a0 = te_naud();
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=b1")));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "ITEMS|1|M|LOC_ID|1|2");
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=")));
    TF_STREQ(ifield(g_it_a, "LOC_ID"), "0");
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=2")));
    TF_STREQ(ifield(g_it_a, "LOC_ID"), "2");
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=00")));
    TF_STREQ(ifield(g_it_a, "LOC_ID"), "0");
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=A1")));
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("QTY=-2")), ENG_EARG, "QTY",
            NULL);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("CATEGORY=")), ENG_EARG,
            "CATEGORY", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("CATEGORY=NONE")),
            ENG_ECONSTR, "CATEGORY", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=ZZ")), ENG_ECONSTR,
            "LOC_ID", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("STATUS=")), ENG_EARG,
            "STATUS", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("UPDATED=20260101")),
            ENG_EARG, "UPDATED", NULL);
    TE_FAIL(eng_item_set(g_e, 77, te_p, te_kv("QTY=1")), ENG_ENOTFOUND, "",
            NULL);
    TF_STREQ(ifield(g_it_a, "QTY"), "3");

    /* Loans: QTY not below ON_LOAN, no DEL while on loan. */
    set_onloan(g_it_a, 2);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("QTY=1")), ENG_ECONSTR,
            "QTY", NULL);
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("QTY=2")));
    TE_FAIL(eng_item_del(g_e, g_it_a), ENG_ECONSTR, "ON_LOAN", NULL);
    set_onloan(g_it_a, 0);
}

/* Assemblies and ITEM COPY. */
static void items_assy(void)
{
    u32 id;
    u32 a0;
    u16 len;

    /* Assemblies: g_it_c is installed in g_it_a.  Neither may change
       location; the same location, by ID or code, is no change. */
    set_parent(g_it_c, g_it_a);
    TE_FAIL(eng_item_set(g_e, g_it_c, te_p, te_kv("LOC_ID=B1")), ENG_ECONSTR,
            "LOC_ID", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=B1")), ENG_ECONSTR,
            "LOC_ID", NULL);
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=a1|DESCR=Card set")));
    OK(eng_item_set(g_e, g_it_c, te_p, te_kv("LOC_ID=1")));
    TE_FAIL(eng_item_del(g_e, g_it_a), ENG_ECONSTR, "", NULL);

    /* COPY of the child: a top-level item, no serial, new dates, the
       notes copied. */
    OK(eng_item_addnote(g_e, g_it_c, "installed", ENG_NOTE_ADD));
    g_it_d = 0;
    a0 = te_naud();
    OK(eng_item_copy(g_e, g_it_c, &g_it_d));
    TF_EQ(g_it_d, 4);
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "ITEMS|4|I|ITEM_ID||4");
    TF_STREQ(ifield(g_it_d, "SERIALNO"), "");
    TF_STREQ(ifield(g_it_d, "PARENT_ID"), "0");
    TF_STREQ(ifield(g_it_d, "ON_LOAN"), "0");
    TF_STREQ(ifield(g_it_d, "MODEL"), "EGA");
    TF_STREQ(ifield(g_it_d, "LOC_ID"), "1");
    TF_STREQ(ifield(g_it_d, "CREATED"), "20261009");
    if (OK(eng_item_notes(g_e, g_it_d, &len)))
        TF_STREQ(eng_memobuf(g_e), "installed");
    TF_STREQ(ifield(g_it_c, "SERIALNO"), "s1");
    TE_FAIL(eng_item_copy(g_e, 55, &id), ENG_ENOTFOUND, "", NULL);
}

/* ITEM DEL and UNDEL with their checks and audit rows. */
static void items_undel(void)
{
    u32 id;
    u32 r;
    u32 a0;
    u16 len;

    /* DEL and UNDEL with their audit rows; the parent rule. */
    a0 = te_naud();
    OK(eng_item_del(g_e, g_it_c));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "ITEMS|3|D|||DELETED");
    TE_FAIL(eng_item_get(g_e, g_it_c, &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_item_del(g_e, g_it_c), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_c, te_p, te_kv("QTY=1")), ENG_ENOTFOUND, "",
            NULL);
    TE_FAIL(eng_item_notes(g_e, g_it_c, &len), ENG_ENOTFOUND, "", NULL);
    OK(eng_item_del(g_e, g_it_a));
    TE_FAIL(eng_item_undel(g_e, g_it_c), ENG_ECONSTR, "PARENT_ID", NULL);
    TE_FAIL(eng_item_undel(g_e, g_it_b), ENG_ECONSTR, "", NULL);
    TE_FAIL(eng_item_undel(g_e, 99), ENG_ENOTFOUND, "", NULL);
    eng_set_now(g_e, &t0);
    OK(eng_item_undel(g_e, g_it_a));
    TF_STREQ(aud(1), "ITEMS|1|D||DELETED|");
    TF_STREQ(ifield(g_it_a, "UPDATED"), "20261008");
    OK(eng_item_undel(g_e, g_it_c));
    TF_STREQ(ifield(g_it_c, "PARENT_ID"), "1");

    /* An installed item undeleted after its parent moved takes the
       parent's location, audited as a move; without a move only the D
       row is written. */
    OK(eng_item_del(g_e, g_it_c));
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=B1")));
    a0 = te_naud();
    OK(eng_item_undel(g_e, g_it_c));
    TF_EQ(te_naud(), a0 + 2);
    TF_STREQ(ifield(g_it_c, "LOC_ID"), "2");
    TF_CHECK(two_rows("ITEMS|3|D||DELETED|", "ITEMS|3|M|LOC_ID|1|2"));
    OK(eng_item_del(g_e, g_it_c));
    OK(eng_item_set(g_e, g_it_a, te_p, te_kv("LOC_ID=A1")));
    OK(eng_item_undel(g_e, g_it_c));
    TF_STREQ(ifield(g_it_c, "LOC_ID"), "1");
    TF_CHECK(two_rows("ITEMS|3|D||DELETED|", "ITEMS|3|M|LOC_ID|2|1"));
    OK(eng_item_del(g_e, g_it_c));
    a0 = te_naud();
    OK(eng_item_undel(g_e, g_it_c));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "ITEMS|3|D||DELETED|");

    /* UNDEL refused while the category or the location is deleted; a
       location deleted never comes back, so the item is pointed at the
       new one of the same code by hand. */
    id = add_item("CATEGORY=PART|LOC_ID=B1");
    OK(eng_item_del(g_e, id));
    OK(eng_code_del(g_e, ENG_T_CATS, "PART"));
    TE_FAIL(eng_item_undel(g_e, id), ENG_ECONSTR, "CATEGORY", NULL);
    OK(eng_code_add(g_e, ENG_T_CATS, "PART", te_p, te_kv("CAT_NAME=Parts")));
    OK(eng_loc_del(g_e, "B1"));
    TE_FAIL(eng_item_undel(g_e, id), ENG_ECONSTR, "LOC_ID", NULL);
    TF_EQ(te_loc("B1", 0), 3);
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), ENG_X_ITEMS2, "LOC_ID", NULL,
                     3), ENG_OK);
    OK(eng_item_undel(g_e, id));
    TF_STREQ(ifield(id, "LOC_ID"), "3");
}

/* Item notes: appended lines, the input rules, the limit. */
static void items_notes(void)
{
    u32 a0;
    u16 len;
    u16 was;

    /* Notes: an appended line, UPDATED, the M input rules and the limit. */
    eng_set_now(g_e, &t1);
    a0 = te_naud();
    OK(eng_item_addnote(g_e, g_it_b, "line3\nline4", ENG_NOTE_ADD));
    TF_EQ(te_naud(), a0 + 1);
    TF_CHECK(strncmp(aud(1), "ITEMS|2|U|NOTES|", 16) == 0);
    was = 0;
    if (OK(eng_item_notes(g_e, g_it_b, &was)))
        TF_STREQ(eng_memobuf(g_e), "line1\r\nline2\r\nline3\r\nline4");
    TF_STREQ(ifield(g_it_b, "UPDATED"), "20261009");
    TE_FAIL(eng_item_addnote(g_e, g_it_b, "bad\rcr", ENG_NOTE_ADD),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_item_addnote(g_e, g_it_b, "bell\007", ENG_NOTE_ADD),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_item_addnote(g_e, 99, "x", ENG_NOTE_ADD),
            ENG_ENOTFOUND, "", NULL);
    /* A line of half the limit fits once; the line break counts. */
    memset(g_big, 'x', ENG_MAXMEMO / 2);
    g_big[ENG_MAXMEMO / 2] = '\0';
    OK(eng_item_addnote(g_e, g_it_b, g_big, ENG_NOTE_ADD));
    TE_FAIL(eng_item_addnote(g_e, g_it_b, g_big, ENG_NOTE_ADD),
            ENG_ECONSTR, "NOTES", NULL);
    len = 0;
    if (OK(eng_item_notes(g_e, g_it_b, &len)))
        TF_EQ(len, was + 2 + ENG_MAXMEMO / 2);
    /* Exactly ENG_MAXMEMO is accepted. */
    memset(g_big, 'y', (size_t)(ENG_MAXMEMO - len - 2));
    g_big[ENG_MAXMEMO - len - 2] = '\0';
    OK(eng_item_addnote(g_e, g_it_b, g_big, ENG_NOTE_ADD));
    if (OK(eng_item_notes(g_e, g_it_b, &len)))
        TF_EQ(len, ENG_MAXMEMO);
    TE_FAIL(eng_item_addnote(g_e, g_it_b, "", ENG_NOTE_ADD),
            ENG_ECONSTR, "NOTES", NULL);
    TE_FAIL(eng_item_set(g_e, g_it_b, te_p, te_kv("NOTES=a\rb")), ENG_EARG,
            "NOTES", NULL);
    OK(eng_item_set(g_e, g_it_b, te_p, te_kv("NOTES=short")));
    if (OK(eng_item_notes(g_e, g_it_b, &len)))
        TF_STREQ(eng_memobuf(g_e), "short");
    eng_set_now(g_e, &t0);
}

static void test_items(void)
{
    if (!phase_start())
        return;
    items_add();
    items_set();
    items_assy();
    items_undel();
    items_notes();
    phase_end();
}

int main(int argc, char **argv)
{
    tf_begin("T_ENGINE");
    if (argc > 1)
        tfs_set_dir(argv[1]);
    g_big = (char *)malloc(ENG_MAXMEMO + 2);
    if (!TF_CHECK(g_big != NULL))
        return tf_done();
    test_items();
    test_lists();
    test_locs();
    test_walks();
    test_cats();
    test_bounded();
    test_texts();
    test_tags();
    free(g_big);
    return tf_done();
}

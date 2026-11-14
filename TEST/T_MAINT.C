/*
 * T_MAINT.C - unit test for maintenance records (MAINT.C), run through a
 * complete engine.
 *
 * usage: T_MAINT [dir]
 *
 * dir (default the current directory) is a directory, or a prefix ending
 * in '\', '/' or ':', that holds the scratch data directory (tfs_set_dir
 * of T_FILES.H); every engine file there is removed before the open and
 * at the end.  The clock is fixed, so dates and audit rows are
 * reproducible.  Checks:
 *   - MAINT ADD: the defaults (MDATE today, ITEM_ID, MAINT_ID from SEQ),
 *     every MTYPE code, lower-case codes stored upper-case, the I audit
 *     row, NOTES stored as the DETAIL memo;
 *   - refusals that write nothing: an item that is not live (ENG_ENOTFOUND
 *     naming ITEM), blank or missing MTYPE and SUMMARY, an unknown MTYPE
 *     (one that spans two codes included), the read-only IDs (refused
 *     before a bad value in an earlier pair, with or without NOTES), NOTES
 *     with DETAIL, a bad NOTES value (named NOTES), a negative COST;
 *   - GET of unknown, zero and deleted IDs (ENG_ENOTFOUND);
 *   - SET with its U rows (DETAIL as MEMO), the required fields refused
 *     when made blank, unchanged values writing no row;
 *   - DEL with its D row, and every command on the deleted record;
 *   - NOTES and NOTES ADD= (lines joined by CR LF, a U row, the length
 *     limit as ENG_ECONSTR naming NOTES);
 *   - MAINT LIST: one item's records, oldest MDATE first, deleted ones
 *     skipped, paged with cursors; an item that is not live; a cursor of
 *     another index;
 *   - MAINT RECENT: the most recently added first whatever the MDATE,
 *     deleted records skipped, paged with cursors, also from a cursor
 *     naming a deleted record; cursors of another table;
 *   - the records after a reopen;
 *   - the records of an item deleted afterwards (GET, SET, NOTES ADD=,
 *     DEL and RECENT reach them; ADD and LIST refuse the item), and an
 *     empty MDATE= stored blank and listed first;
 *   - no edit marker left after any change or refusal.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when dir is too long.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGNOTE.H"
#include "ENGITEM.H"
#include "ENGMAINT.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"

#define LISTMAX     200         /* a list of MAINT_IDs, NUL included    */
#define SPECMAX     40          /* the pairs of one add, NUL included   */

/* The fields of an audit row as aud gives them. */
static const char k_aud[] = "TBL REC_ID ACTION FLD OLDVAL NEWVAL USERNAME";

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

static eng *g_e;
static char g_memo[ENG_MAXMEMO + 1];
static u32 g_i1;                /* items: two live, one deleted         */
static u32 g_i2;
static u32 g_i3;

/* ---------------------------------------------------------------------
 * Fields, audit rows, changes.
 * --------------------------------------------------------------------- */

/* Audit record recno as its k_aud fields. */
static const char *aud(u32 recno)
{
    return te_aud(recno, k_aud);
}

/* 1 when the edit marker ENG_EDITMARK is in the data directory. */
static int marked(void)
{
    return tfs_exists(tfs_path(ENG_EDITMARK, ""));
}

/* MAINT ADD for item, the pairs of spec; the new ID, 0 on failure.
   The edit marker is gone afterwards. */
static u32 add(u32 item, const char *spec)
{
    u32 id;

    id = 0;
    if (!TF_EQ(eng_maint_add(g_e, item, te_p, te_kv(spec), &id), ENG_OK))
        printf("  add %s: %s\n", spec, eng_errfield(g_e));
    TF_CHECK(!marked());
    return id;
}

/* The text of field name of live MAINT record id, "?" when there is
   none. */
static const char *mf(u32 id, const char *name)
{
    u32 r;

    if (eng_maint_get(g_e, id, &r) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_MAINT, TE_BUF, name);
}

static u32 item(const char *spec)
{
    u32 id;

    id = 0;
    TF_EQ(eng_item_add(g_e, te_p, te_kv(spec), &id), ENG_OK);
    return id;
}

/* The MAINT_IDs of one page of at most max records of scan s as
   "a b c", with the cursor of the page into cur and *more. */
static const char *page(eng_scan *s, u16 max, char *cur, int *more)
{
    static char ids[LISTMAX];
    u32 r;
    u16 n;

    ids[0] = '\0';
    for (n = 0; n < max; n++) {
        if (!TF_EQ(eng_scan_next(g_e, s, &r), ENG_OK) || r == 0)
            break;
        if (n > 0)
            strcat(ids, " ");
        strcat(ids, te_rtext(ENG_T_MAINT, r, "MAINT_ID"));
    }
    eng_scan_cursor(s, cur);
    TF_EQ(eng_scan_more(g_e, s, more), ENG_OK);
    return ids;
}

/* ---------------------------------------------------------------------
 * The phases.
 * --------------------------------------------------------------------- */

static void test_add(void)
{
    static const char *const codes[] = { "CL", "RC", "TS", "MD", "IN" };
    char spec[SPECMAX];
    u32 id;
    u32 n;
    u16 len;
    int i;

    g_i1 = item("CATEGORY=SYS|MFR=IBM|MODEL=5150");
    g_i2 = item("CATEGORY=CARD|MFR=HERCULES");
    g_i3 = item("CATEGORY=DRV|MFR=TANDON");
    TF_EQ(eng_item_del(g_e, g_i3), ENG_OK);

    n = te_naud();
    id = add(g_i1, "MTYPE=RP|SUMMARY=Recapped PSU|COST=12.50");
    TF_EQ(id, 1);
    TF_EQ(te_naud(), n + 1);
    TF_STREQ(aud(te_naud()), "MAINT|1|I|MAINT_ID||1|TESTER");
    TF_STREQ(mf(id, "ITEM_ID"), "1");
    TF_STREQ(mf(id, "MDATE"), "20261008");
    TF_STREQ(mf(id, "MTYPE"), "RP");
    TF_STREQ(mf(id, "SUMMARY"), "Recapped PSU");
    TF_STREQ(mf(id, "COST"), "12.50");
    TF_STREQ(mf(id, "DONE_BY"), "");
    TF_EQ(eng_maint_notes(g_e, id, &len), ENG_OK);
    TF_EQ(len, 0);

    /* Every code; lower case is stored upper-cased; NOTES is DETAIL. */
    for (i = 0; i < (int)(sizeof codes / sizeof codes[0]); i++) {
        sprintf(spec, "MTYPE=%s|SUMMARY=s%d|MDATE=2025010%d", codes[i], i,
                i + 1);
        TF_EQ(add(g_i2, spec), (u32)(i + 2));
    }
    id = add(g_i1, "MTYPE=ts|SUMMARY=Tested|DONE_BY=Norman|"
                   "NOTES=line one\nline two");
    TF_EQ(id, 7);
    TF_STREQ(mf(id, "MTYPE"), "TS");
    TF_STREQ(mf(id, "DONE_BY"), "Norman");
    TF_STREQ(aud(te_naud()), "MAINT|7|I|MAINT_ID||7|TESTER");
    TF_EQ(eng_maint_notes(g_e, id, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "line one\r\nline two");
    TF_EQ(len, 18);
    id = add(g_i1, "MTYPE=IN|SUMMARY=By DETAIL|DETAIL=d");
    TF_EQ(eng_maint_notes(g_e, id, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "d");

    /* Refusals. */
    TE_FAIL(eng_maint_add(g_e, g_i3, te_p, te_kv("MTYPE=RP|SUMMARY=x"), &id),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TF_EQ(id, 0);
    TE_FAIL(eng_maint_add(g_e, 999, te_p, te_kv("MTYPE=RP|SUMMARY=x"), &id),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TE_FAIL(eng_maint_add(g_e, 0, te_p, te_kv("MTYPE=RP|SUMMARY=x"), &id),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=|SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=RP"), &id),
            ENG_EARG, "SUMMARY", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=RP|SUMMARY=   "), &id),
            ENG_EARG, "SUMMARY", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=XX|SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=R|SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=PR|SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=PC|SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=RPX|SUMMARY=x"), &id),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|MAINT_ID=50"), &id),
            ENG_EARG, "MAINT_ID", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|ITEM_ID=2"), &id),
            ENG_EARG, "ITEM_ID", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|COST=x|MAINT_ID=3"), &id),
            ENG_EARG, "MAINT_ID", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|NOTES=a\rb|ITEM_ID=2"),
                          &id), ENG_EARG, "ITEM_ID", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p, te_kv("MTYPE=RP|SUMMARY=x|COST=-1"),
                          &id), ENG_EARG, "COST", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|MDATE=20261332"), &id),
            ENG_EARG, "MDATE", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|COLOR=red"), &id),
            ENG_EARG, "COLOR", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|NOTES=a|DETAIL=b"), &id),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|NOTES=a\rb"), &id),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_maint_add(g_e, g_i1, te_p,
                          te_kv("MTYPE=RP|SUMMARY=x|DETAIL=a\rb"), &id),
            ENG_EARG, "DETAIL", NULL);
    TF_EQ(add(g_i2, "MTYPE=CL|SUMMARY=after refusals"), 9);
}

static void test_get(void)
{
    u32 r;

    TF_EQ(eng_maint_get(g_e, 9, &r), ENG_OK);
    TF_EQ(r, 9);
    TF_STREQ(te_rtext(ENG_T_MAINT, r, "SUMMARY"), "after refusals");
    TF_EQ(eng_maint_get(g_e, 1, &r), ENG_OK);
    TF_EQ(r, 1);
    TF_EQ(eng_maint_get(g_e, 0, &r), ENG_ENOTFOUND);
    TF_EQ(r, 0);
    TF_EQ(eng_maint_get(g_e, 10, &r), ENG_ENOTFOUND);
    TF_STREQ(eng_errfield(g_e), "ID");
    TF_EQ(eng_maint_get(g_e, 99999999L, &r), ENG_ENOTFOUND);
}

static void test_set(void)
{
    u32 n;
    u16 n16;

    n = te_naud();
    TF_EQ(eng_maint_set(g_e, 1, te_p,
                        te_kv("SUMMARY=Recapped PSU C12|COST=13|DONE_BY=Bob")),
          ENG_OK);
    TF_EQ(te_naud(), n + 3);
    TF_STREQ(aud(n + 1),
             "MAINT|1|U|SUMMARY|Recapped PSU|Recapped PSU C12|TESTER");
    TF_STREQ(aud(n + 2), "MAINT|1|U|COST|12.50|13.00|TESTER");
    TF_STREQ(aud(n + 3), "MAINT|1|U|DONE_BY||Bob|TESTER");
    TF_CHECK(!marked());
    TF_STREQ(mf(1, "SUMMARY"), "Recapped PSU C12");
    TF_STREQ(mf(1, "ITEM_ID"), "1");

    /* An unchanged value writes no row. */
    n = te_naud();
    TF_EQ(eng_maint_set(g_e, 1, te_p, te_kv("COST=13.00|MTYPE=RP")), ENG_OK);
    TF_EQ(te_naud(), n);

    TF_EQ(eng_maint_set(g_e, 1, te_p, te_kv("MTYPE=md|MDATE=20261231")),
          ENG_OK);
    TF_STREQ(aud(te_naud() - 1), "MAINT|1|U|MDATE|20261008|20261231|TESTER");
    TF_STREQ(aud(te_naud()), "MAINT|1|U|MTYPE|RP|MD|TESTER");
    TF_EQ(eng_maint_set(g_e, 8, te_p, te_kv("NOTES=new text")), ENG_OK);
    TF_STREQ(aud(te_naud()), "MAINT|8|U|DETAIL|MEMO|MEMO|TESTER");
    TF_EQ(eng_maint_notes(g_e, 8, &n16), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "new text");

    /* The required fields cannot be made blank. */
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("SUMMARY=")),
            ENG_EARG, "SUMMARY", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("COST=1|SUMMARY= ")), ENG_EARG,
            "SUMMARY", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("MTYPE=")),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("MTYPE=ZZ")),
            ENG_EARG, "MTYPE", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("MAINT_ID=3")), ENG_EARG,
            "MAINT_ID", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("ITEM_ID=2")),
            ENG_EARG, "ITEM_ID", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("DETAIL=a|NOTES=b")), ENG_EARG,
            "NOTES", NULL);
    TE_FAIL(eng_maint_set(g_e, 1, te_p, te_kv("NOTES=\001")),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_maint_set(g_e, 50, te_p, te_kv("COST=1")),
            ENG_ENOTFOUND, "ID", NULL);
    TF_STREQ(mf(1, "SUMMARY"), "Recapped PSU C12");
    TF_STREQ(mf(1, "COST"), "13.00");
}

static void test_del(void)
{
    u32 r;
    u16 len;

    TF_EQ(eng_maint_del(g_e, 3), ENG_OK);
    TF_STREQ(aud(te_naud()), "MAINT|3|D|||DELETED|TESTER");
    TF_CHECK(!marked());
    TF_EQ(eng_reccount(g_e, ENG_T_MAINT), 9);
    TF_EQ(eng_maint_get(g_e, 3, &r), ENG_ENOTFOUND);
    TF_EQ(r, 0);
    TE_FAIL(eng_maint_del(g_e, 3), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_maint_set(g_e, 3, te_p, te_kv("COST=1")),
            ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_maint_notes(g_e, 3, &len), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_maint_addnote(g_e, 3, "x", ENG_NOTE_ADD),
            ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_maint_del(g_e, 77), ENG_ENOTFOUND, "ID", NULL);
}

static void test_notes(void)
{
    u16 len;
    u16 i;

    /* The first line of an empty memo: no old value. */
    TF_EQ(eng_maint_addnote(g_e, 1, "first", ENG_NOTE_ADD), ENG_OK);
    TF_STREQ(aud(te_naud()), "MAINT|1|U|DETAIL||MEMO|TESTER");
    TF_CHECK(!marked());
    TF_EQ(eng_maint_addnote(g_e, 1, "second", ENG_NOTE_ADD), ENG_OK);
    TF_EQ(eng_maint_notes(g_e, 1, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "first\r\nsecond");
    TF_EQ(len, 13);
    TF_EQ(eng_maint_addnote(g_e, 7, "x\ny", ENG_NOTE_ADD), ENG_OK);
    TF_EQ(eng_maint_notes(g_e, 7, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "line one\r\nline two\r\nx\r\ny");
    TF_STREQ(mf(7, "SUMMARY"), "Tested");
    TE_FAIL(eng_maint_addnote(g_e, 7, "a\rb", ENG_NOTE_ADD),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_maint_addnote(g_e, 50, "x", ENG_NOTE_ADD),
            ENG_ENOTFOUND, "ID", NULL);

    /* The length limit. */
    for (i = 0; i < ENG_MAXMEMO - 5; i++)
        g_memo[i] = (char)('a' + i % 26);
    g_memo[i] = '\0';
    te_p[0].key = "NOTES";
    te_p[0].val = g_memo;
    TF_EQ(eng_maint_set(g_e, 9, te_p, 1), ENG_OK);
    TE_FAIL(eng_maint_addnote(g_e, 9, "1234", ENG_NOTE_ADD),
            ENG_ECONSTR, "NOTES", NULL);
    TF_EQ(eng_maint_addnote(g_e, 9, "123", ENG_NOTE_ADD), ENG_OK);
    TF_EQ(eng_maint_notes(g_e, 9, &len), ENG_OK);
    TF_EQ(len, ENG_MAXMEMO);
    TF_CHECK(strcmp(eng_memobuf(g_e) + ENG_MAXMEMO - 5, "\r\n123") == 0);
    TE_FAIL(eng_maint_addnote(g_e, 9, "", ENG_NOTE_ADD),
            ENG_ECONSTR, "NOTES", NULL);
}

static void test_list(void)
{
    char cur[ENG_CURSORMAX];
    char c1[ENG_CURSORMAX];
    eng_scan s;
    int more;

    TF_EQ(add(g_i2, "MTYPE=RP|SUMMARY=old|MDATE=20240101"), 10);

    TF_EQ(eng_maint_scan(g_e, &s, g_i2, NULL), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "10 2 4 5 6 9");
    TF_EQ(more, 0);
    TF_EQ(eng_maint_scan(g_e, &s, g_i2, NULL), ENG_OK);
    TF_STREQ(page(&s, 2, cur, &more), "10 2");
    TF_EQ(more, 1);
    strcpy(c1, cur);
    TF_EQ(eng_maint_scan(g_e, &s, g_i2, cur), ENG_OK);
    TF_STREQ(page(&s, 2, cur, &more), "4 5");
    TF_EQ(more, 1);
    TF_EQ(eng_maint_scan(g_e, &s, g_i2, cur), ENG_OK);
    TF_STREQ(page(&s, 2, cur, &more), "6 9");
    TF_EQ(more, 0);
    TF_EQ(eng_maint_scan(g_e, &s, g_i2, c1), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "4 5 6 9");

    /* Equal dates in the order the records were added. */
    TF_EQ(eng_maint_scan(g_e, &s, g_i1, NULL), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "7 8 1");
    TF_EQ(eng_maint_scan(g_e, &s, g_i1, c1), ENG_ECURSOR);
    TF_EQ(eng_maint_scan(g_e, &s, g_i2, "ITEMS1::1"), ENG_ECURSOR);
    TF_EQ(eng_maint_scan(g_e, &s, g_i2, "MAINT::1"), ENG_ECURSOR);

    TE_FAIL(eng_maint_scan(g_e, &s, g_i3, NULL),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TE_FAIL(eng_maint_scan(g_e, &s, 999, NULL),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TE_FAIL(eng_maint_scan(g_e, &s, 0, NULL),
            ENG_ENOTFOUND, "ITEM", "no such item");
}

static void test_recent(void)
{
    char cur[ENG_CURSORMAX];
    eng_scan s;
    int more;

    TF_EQ(eng_maint_recent(g_e, &s, NULL), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "10 9 8 7 6 5 4 2 1");
    TF_EQ(more, 0);
    TF_EQ(eng_maint_recent(g_e, &s, NULL), ENG_OK);
    TF_STREQ(page(&s, 4, cur, &more), "10 9 8 7");
    TF_STREQ(cur, "MAINT::7");
    TF_EQ(more, 1);
    TF_EQ(eng_maint_recent(g_e, &s, cur), ENG_OK);
    TF_STREQ(page(&s, 4, cur, &more), "6 5 4 2");
    TF_STREQ(cur, "MAINT::2");
    TF_EQ(more, 1);
    TF_EQ(eng_maint_recent(g_e, &s, cur), ENG_OK);
    TF_STREQ(page(&s, 4, cur, &more), "1");
    TF_EQ(more, 0);

    /* From a cursor naming a deleted record, and one deleted between
       pages. */
    TF_EQ(eng_maint_recent(g_e, &s, "maint::3"), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "2 1");
    TF_EQ(eng_maint_del(g_e, 6), ENG_OK);
    TF_EQ(eng_maint_recent(g_e, &s, "MAINT::7"), ENG_OK);
    TF_STREQ(page(&s, 2, cur, &more), "5 4");
    TF_EQ(eng_maint_del(g_e, 10), ENG_OK);
    TF_EQ(eng_maint_recent(g_e, &s, NULL), ENG_OK);
    TF_STREQ(page(&s, 1, cur, &more), "9");
    TF_STREQ(cur, "MAINT::9");

    TF_EQ(eng_maint_recent(g_e, &s, "ITEMS::3"), ENG_ECURSOR);
    TF_EQ(eng_maint_recent(g_e, &s, "MAINT1::3"), ENG_ECURSOR);
    TF_EQ(eng_maint_recent(g_e, &s, "MAINT::0"), ENG_ECURSOR);
}

static void test_reopen(void)
{
    char cur[ENG_CURSORMAX];
    eng_scan s;
    int more;

    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return;
    TF_STREQ(mf(7, "SUMMARY"), "Tested");
    TF_STREQ(mf(1, "MDATE"), "20261231");
    TF_EQ(eng_maint_recent(g_e, &s, NULL), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "9 8 7 5 4 2 1");
    TF_EQ(add(g_i1, "MTYPE=CL|SUMMARY=after reopen"), 11);
}

/* A record of an item deleted afterwards is still reached by GET, SET,
   NOTES, DEL and RECENT; an explicit empty MDATE stores a blank, which
   sorts first in MAINT LIST.  No change leaves the edit marker. */
static void test_orphan(void)
{
    char cur[ENG_CURSORMAX];
    eng_scan s;
    u32 i4;
    u32 r;
    u16 len;
    int more;

    TF_EQ(add(g_i1, "MTYPE=CL|SUMMARY=undated|MDATE="), 12);
    TF_STREQ(mf(12, "MDATE"), "");
    TF_EQ(eng_maint_scan(g_e, &s, g_i1, NULL), ENG_OK);
    TF_STREQ(page(&s, 25, cur, &more), "12 7 8 11 1");

    i4 = item("CATEGORY=PERI|MFR=EPSON|MODEL=FX-80");
    TF_EQ(add(i4, "MTYPE=RP|SUMMARY=New ribbon"), 13);
    TF_EQ(eng_item_del(g_e, i4), ENG_OK);
    TF_EQ(eng_maint_get(g_e, 13, &r), ENG_OK);
    TF_EQ(r, 13);
    sprintf(cur, "%lu", (unsigned long)i4);
    TF_STREQ(te_rtext(ENG_T_MAINT, r, "ITEM_ID"), cur);
    TF_EQ(eng_maint_set(g_e, 13, te_p, te_kv("SUMMARY=New ribbon, inked")),
          ENG_OK);
    TF_CHECK(!marked());
    TF_STREQ(aud(te_naud()),
             "MAINT|13|U|SUMMARY|New ribbon|New ribbon, inked|TESTER");
    TF_EQ(eng_maint_addnote(g_e, 13, "ribbon 7753", ENG_NOTE_ADD), ENG_OK);
    TF_CHECK(!marked());
    TF_EQ(eng_maint_notes(g_e, 13, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "ribbon 7753");
    TF_EQ(eng_maint_recent(g_e, &s, NULL), ENG_OK);
    TF_STREQ(page(&s, 2, cur, &more), "13 12");
    TE_FAIL(eng_maint_scan(g_e, &s, i4, NULL),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TE_FAIL(eng_maint_add(g_e, i4, te_p, te_kv("MTYPE=RP|SUMMARY=x"), &r),
            ENG_ENOTFOUND, "ITEM", "no such item");
    TF_EQ(eng_maint_del(g_e, 13), ENG_OK);
    TF_CHECK(!marked());
    TF_STREQ(aud(te_naud()), "MAINT|13|D|||DELETED|TESTER");
    TF_EQ(eng_maint_recent(g_e, &s, NULL), ENG_OK);
    TF_STREQ(page(&s, 1, cur, &more), "12");
}

int main(int argc, char **argv)
{
    tf_begin("T_MAINT");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    if (TF_EQ(te_open(&g_e, &g_now), ENG_OK)) {
        test_add();
        test_get();
        test_set();
        test_del();
        test_notes();
        test_list();
        test_recent();
        test_reopen();
        test_orphan();
    }
    if (g_e != NULL)
        TF_EQ(eng_close(g_e), ENG_OK);
    te_remove(tfs_dir(), NULL);
    return tf_done();
}

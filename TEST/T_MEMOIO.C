/*
 * T_MEMOIO.C - the notes of a record as a client holds them
 * (SRC/CLIENT/MEMOIO.H).
 *
 * usage: T_MEMOIO dir
 *
 * dir is a scratch data directory prefix holding nothing but this
 * test's files; its engine files are removed first and last.  The test
 * links the recording link of TRLINK.H, so the requests are checked
 * exactly and a chosen one can be made to fail or answered with lines
 * of the test's own.
 *
 * Covered:
 *   - mio_open: an empty memo, and RQ_EINTERNAL with no block when
 *     memory has run out;
 *   - loading: the exact request for ITEM and LOC; no line, one line,
 *     MEMO_MAXLINES lines and one more (MM_TOOMANY, n cut; before the
 *     cut, mio_text and mio_save take only the lines held); a line of
 *     exactly MEMO_COLS characters, one longer split, TABs expanded,
 *     a CR, an LF and 7Fh as '?' (MM_CHANGED); bytes with CR LF between
 *     lines; K TRUNCATED=1 (MM_TRUNC), but not =0; a memo past
 *     LINK_MAXMEMO once expanded (MM_TOOLONG); mio_check's order; a
 *     failed load; mio_count, its failures keeping the response and
 *     counting no line, also after some came (K ERROR=);
 *   - the lines: mio_put, mio_insert and mio_delete keeping bytes, and
 *     mio_get's NUL after a shorter line;
 *   - saving: a SET of exactly LINK_MAXLINE bytes, and one byte more
 *     making it a NOTES NEW=, as long, and its last line a NOTES END=;
 *     every request of the longest memo within LINK_MAXLINE, NEW=, then
 *     PART=, then END=, and read back whole; a PART= one line shorter
 *     than a NEW= of the same lines; trailing spaces and empty
 *     lines dropped; a clear (NOTES=""); a save failing at its first,
 *     second and last request, with done, total, res and the memo the
 *     server keeps, and the session ending under one; the link lost at a
 *     PART=, after the server wrote an END=, and before a SET's answer,
 *     and NEW= refused by an older server, with mio_failmsg's message;
 *   - text (MIOTEXT.C): mio_text's CR LF breaks, its cut, a memo of
 *     LINK_MAXMEMO bytes; mio_set_text's line ends (CR LF, lone CR,
 *     lone LF), the empty text, a last break, the load's conversions
 *     and limits, and a text saved and read back.
 *
 * Exit status 0 when every check passed, else 1.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#define TL_ENDS
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_LINK
#include "TESTFW.H"
#include "MEMOIO.H"

#define PATHMAX         96
/* The text buffer: a memo of LINK_MAXMEMO bytes, or a text built to
   pass that by some lines. */
#define TEXTSZ          (LINK_MAXMEMO + 2 * MEMO_COLS + 1)
/* "ITEM SET 1 NOTES=" with a quoted value, its quotes counted. */
#define SETHEAD         (sizeof "ITEM SET 1 NOTES=\"\"" - 1)
/* The memo that fills one SET: SPLIT_N - 1 lines of SPLIT_A characters,
   then one of SPLIT_B, joined by "\n" escaped as two bytes, make a SET
   of LINK_MAXLINE.  SPLIT_N is the fewest lines that leave SPLIT_B below
   MEMO_COLS, so that a line one longer still fits a line of the memo. */
#define SPLIT_A         56
#define SPLIT_N         ((u16)(1 + (LINK_MAXLINE - SETHEAD - MEMO_COLS + \
                                    SPLIT_A + 2) / (SPLIT_A + 2)))
#define SPLIT_B         ((u16)(LINK_MAXLINE - SETHEAD - 2 * (SPLIT_N - 1) - \
                               (SPLIT_N - 1) * SPLIT_A))
extern char split_fits[1];
extern char split_fits[(SPLIT_B > 0 && SPLIT_B < MEMO_COLS) ? 1 : 2];

/* The lines of test_save_fail, FAIL_W characters each, FAIL_N to a
   request (NEW=, PART= or END=, whose heads are no longer than a SET's
   but by PART='s byte, which the lines leave room for), FAIL_LINES
   lines in three requests. */
#define FAIL_W          50
#define FAIL_N          ((u16)((LINK_MAXLINE - SETHEAD + 2) / (FAIL_W + 2)))
#define FAIL_LINES      12
extern char fail_three[1];
extern char fail_three[((FAIL_LINES + FAIL_N - 1) / FAIL_N == 3 &&
                         (LINK_MAXLINE - SETHEAD + 2) % (FAIL_W + 2) > 0) ?
                        1 : 2];

/* The lines of test_save_part, PART_W characters each: PART_N of them
   and their escaped newlines would pass a PART= request by one byte, and
   take a NEW= request exactly. */
#define PARTHEAD        (sizeof "ITEM NOTES 1 PART=\"\"" - 1)
#define PART_N          5
#define PART_W          ((u16)((LINK_MAXLINE + 1 - PARTHEAD + 2) / PART_N - 2))
extern char part_fits[1];
extern char part_fits[((LINK_MAXLINE + 1 - PARTHEAD + 2) % PART_N == 0 &&
                       PART_W <= MEMO_COLS) ? 1 : 2];

/* The bytes a line of test_save_max takes inside quotes: L, three
   digits, a space, an escaped quote and backslash, a space, a letter. */
#define LINE_COST       11

static char g_ddir[PATHMAX];
static char g_text[TEXTSZ];
static mio g_m;

static int no_mem(u16 size)
{
    return size > 0;
}

/* Fill b with n letters starting at the one of seed, and a NUL. */
static void letters(char *b, u16 n, u16 seed)
{
    u16 i;

    for (i = 0; i < n; i++)
        b[i] = (char)('a' + (seed + i) % 26);
    b[n] = '\0';
}

/* Send "ITEM SET 1 NOTES=" with the n bytes of v, which may hold LF. */
static int set_notes(const char *v, u16 n)
{
    rq q;
    rq_res res;

    rq_begin(&q, "ITEM SET 1");
    rq_pairn(&q, "NOTES", v, n);
    return rq_send(tc_lk, &q, NULL, NULL, &res);
}

/* Load item 1 into g_m; the rq_send code. */
static int load1(void)
{
    rq_res res;

    return mio_load(&g_m, tc_lk, "ITEM", "1", &res);
}

/* 1 when line i of g_m is s. */
static int line_is(u16 i, const char *s)
{
    char lb[MEMO_SLOT];

    mio_get(&g_m, i, lb);
    return (size_t)g_m.len[i] == strlen(s) && strcmp(lb, s) == 0;
}

/* Make g_m the n lines of s, through mio_insert and mio_put. */
static void make(const char *const *s, u16 n)
{
    u16 i;

    while (g_m.n > 0)
        mio_delete(&g_m, 0);
    for (i = 0; i < n; i++) {
        mio_insert(&g_m, i);
        mio_put(&g_m, i, s[i], (u16)strlen(s[i]));
    }
}

/* Save g_m as item 1's memo, checking total; the code, *done set. */
static int save1(u16 total, u16 *done, rq_res *res)
{
    u16 t;
    int rc;

    t = 999;
    *done = 999;
    rc = mio_save(&g_m, tc_lk, "ITEM", "1", done, &t, res);
    TF_EQ(t, total);
    return rc;
}

/* ---------------------------------------------------------------------
 * Opening and loading.
 * --------------------------------------------------------------------- */
static void test_open(void)
{
    mio m;

    cmem_fault = no_mem;
    m.b = (cmem_blk *)&m;
    TF_EQ(mio_open(&m), RQ_EINTERNAL);
    TF_CHECK(m.b == NULL);
    cmem_fault = NULL;
    memset(&g_m, 0xAA, sizeof g_m);
    TF_EQ(mio_open(&g_m), RQ_OK);
    TF_CHECK(g_m.b != NULL);
    TF_EQ(g_m.n, 0);
    TF_EQ(g_m.bytes, 0);
    TF_EQ(mio_check(&g_m), 0);
}

static void test_load_small(void)
{
    rq_res res;
    u16 lines;

    tl_clear();
    TF_EQ(load1(), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM NOTES 1");
    TF_EQ(g_m.n, 0);
    TF_EQ(g_m.bytes, 0);
    TF_EQ(mio_check(&g_m), 0);
    lines = 9;
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &lines, &res), RQ_OK);
    TF_EQ(lines, 0);

    TF_EQ(tc_send("ITEM SET 1 NOTES=hello"), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 1);
    TF_CHECK(line_is(0, "hello"));
    TF_EQ(g_m.bytes, 5);
    TF_EQ(mio_check(&g_m), 0);
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &lines, &res), RQ_OK);
    TF_EQ(lines, 1);

    /* An empty line between two: CR LF counted between them. */
    TF_EQ(set_notes("ab\n\ncde", 7), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 3);
    TF_CHECK(line_is(0, "ab"));
    TF_CHECK(line_is(1, ""));
    TF_CHECK(line_is(2, "cde"));
    TF_EQ(g_m.bytes, 2 + 2 + 2 + 3);

    tl_clear();
    TF_EQ(tc_send("LOC SET A1 NOTES=shelf"), RQ_OK);
    TF_EQ(mio_load(&g_m, tc_lk, "LOC", "A1", &res), RQ_OK);
    TF_STREQ(tl_req(1), "LOC NOTES A1");
    TF_EQ(g_m.n, 1);
    TF_CHECK(line_is(0, "shelf"));

    /* A failed load and count. */
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    TF_EQ(load1(), RQ_EFULL);
    tl_fail(2, "-ERR 200 no such item FIELD=ID");
    lines = 9;
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &lines, &res), RQ_ENOTFOUND);
    TF_EQ(res.code, RQ_ENOTFOUND);
    TF_STREQ(res.text, "no such item");
    TF_STREQ(res.field, "ID");
    TF_EQ(lines, 0);

    /* A failure after some lines came counts none of them. */
    tl_clear();
    tl_fail(1, "+DATA\nL a\nL b\nK ERROR=302\n.");
    lines = 9;
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &lines, &res), RQ_EFULL);
    TF_EQ(lines, 0);
}

/* MEMO_MAXLINES lines, then one more. */
static void test_load_max(void)
{
    char v[3 * MEMO_MAXLINES];
    rq q;
    rq_res res;
    u16 done;
    u16 n;
    u16 i;
    u16 half;

    half = MEMO_MAXLINES / 2;
    for (i = 0; i < 2 * half; i += 2) {
        v[i] = (char)('A' + i / 2 % 26);
        v[i + 1] = '\n';
    }
    TF_EQ(set_notes(v, (u16)(2 * half - 1)), RQ_OK);
    n = 0;
    for (i = half; i < MEMO_MAXLINES; i++) {
        v[n++] = '\n';
        v[n++] = (char)('a' + i % 26);
    }
    rq_begin(&q, "ITEM NOTES 1");
    rq_pairn(&q, "ADD", v + 1, (u16)(n - 1));
    TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, MEMO_MAXLINES);
    TF_EQ(g_m.bytes, MEMO_MAXLINES + 2 * (MEMO_MAXLINES - 1));
    TF_EQ(mio_check(&g_m), 0);
    TF_EQ(g_m.n, MEMO_MAXLINES);
    TF_CHECK(line_is(0, "A"));
    TF_CHECK(line_is(half, "y"));
    TF_CHECK(line_is(MEMO_MAXLINES - 1, "v"));

    /* One more, changed on loading. */
    TF_EQ(tc_send("ITEM NOTES 1 ADD=\"\\x09xyz\""), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, MEMO_MAXLINES + 1);
    TF_EQ(g_m.bytes, MEMO_MAXLINES + MEMO_TAB + 3 + 2 * MEMO_MAXLINES);
    /* Before mio_check cuts n, the text and a save take the lines held
       alone. */
    mio_text(&g_m, g_text, TEXTSZ);
    TF_EQ(strlen(g_text), MEMO_MAXLINES + 2 * (MEMO_MAXLINES - 1));
    TF_STREQ(g_text + strlen(g_text) - 3, "\r\nv");
    TF_EQ(mio_check(&g_m), MM_TOOMANY);
    TF_EQ(g_m.n, MEMO_MAXLINES);
    TF_CHECK(line_is(MEMO_MAXLINES - 1, "v"));
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &n, &res), RQ_OK);
    TF_EQ(n, MEMO_MAXLINES + 1);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(save1(MEMO_MAXLINES, &done, &res), RQ_OK);
    TF_EQ(done, MEMO_MAXLINES);
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &n, &res), RQ_OK);
    TF_EQ(n, MEMO_MAXLINES);
}

/* Long lines, TABs and control bytes. */
static void test_load_convert(void)
{
    char v[2 * MEMO_COLS + 4];
    char w[MEMO_COLS + 1];

    letters(v, MEMO_COLS, 0);
    TF_EQ(set_notes(v, MEMO_COLS), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 1);
    TF_CHECK(line_is(0, v));
    TF_EQ(g_m.bytes, MEMO_COLS);
    TF_EQ(mio_check(&g_m), 0);

    /* One more is a line of its own; twice MEMO_COLS is two lines. */
    letters(v, MEMO_COLS + 1, 0);
    TF_EQ(set_notes(v, MEMO_COLS + 1), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 2);
    TF_EQ(g_m.len[0], MEMO_COLS);
    TF_CHECK(line_is(1, v + MEMO_COLS));
    TF_EQ(g_m.bytes, MEMO_COLS + 2 + 1);
    TF_EQ(mio_check(&g_m), MM_CHANGED);
    letters(v, 2 * MEMO_COLS, 0);
    TF_EQ(set_notes(v, 2 * MEMO_COLS), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 2);
    TF_CHECK(line_is(1, v + MEMO_COLS));

    /* A TAB goes to the next multiple of MEMO_TAB, and at the end of a
       line no further than MEMO_COLS. */
    TF_EQ(set_notes("a\tb", 3), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    memset(w, ' ', MEMO_TAB);
    w[0] = 'a';
    strcpy(w + MEMO_TAB, "b");
    TF_CHECK(line_is(0, w));
    TF_EQ(mio_check(&g_m), MM_CHANGED);
    letters(v, MEMO_COLS - 2, 0);
    strcpy(v + MEMO_COLS - 2, "\tz");
    TF_EQ(set_notes(v, MEMO_COLS), RQ_OK);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 2);
    TF_EQ(g_m.len[0], MEMO_COLS);
    TF_CHECK(line_is(1, "z"));

    /* A CR, an LF and 7Fh inside a line, as answers only another server
       could send, become '?'. */
    tl_clear();
    tl_fail(1, "+DATA\nL a\\x0Db\\x0Ac\nL \\x7F\n.");
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 2);
    TF_CHECK(line_is(0, "a?b?c"));
    TF_CHECK(line_is(1, "?"));
    TF_EQ(mio_check(&g_m), MM_CHANGED);
}

/* Memos that a save would cut short. */
static void test_load_limits(void)
{
    char tabs[50];
    rq q;
    rq_res res;
    u16 i;

    tl_clear();
    tl_fail(1, "+DATA\nL x\nK TRUNCATED=1\n.");
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.n, 1);
    TF_EQ(g_m.trunc, 1);
    TF_EQ(mio_check(&g_m), MM_TRUNC);
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.trunc, 0);
    /* Only the value 1 says so. */
    tl_clear();
    tl_fail(1, "+DATA\nL x\nK TRUNCATED=0\n.");
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.trunc, 0);

    /* Each line of 50 TABs is 400 spaces once expanded. */
    memset(tabs, '\t', sizeof tabs);
    TF_EQ(tc_send("ITEM SET 1 NOTES=\"\""), RQ_OK);
    for (i = 0; i < 6; i++) {
        rq_begin(&q, "ITEM NOTES 1");
        rq_pairn(&q, "ADD", tabs, (u16)sizeof tabs);
        TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
    }
    TF_EQ(load1(), RQ_OK);
    TF_CHECK(g_m.n <= MEMO_MAXLINES);
    TF_CHECK(g_m.bytes > LINK_MAXMEMO);
    TF_EQ(mio_check(&g_m), MM_TOOLONG);

    /* The order: too many lines, then too long, then truncated, then
       changed. */
    for (i = 0; i <= MEMO_MAXLINES; i++)
        strcpy(g_text + 8 * i, "\t\t\t\t\t\t\t\n");
    mio_set_text(&g_m, g_text);
    TF_EQ(g_m.n, MEMO_MAXLINES + 2);
    TF_CHECK(g_m.bytes > LINK_MAXMEMO);
    g_m.trunc = 1;
    TF_EQ(mio_check(&g_m), MM_TOOMANY);
    TF_EQ(mio_check(&g_m), MM_TOOLONG);
    tl_clear();
    tl_fail(1, "+DATA\nL \\x09\nK TRUNCATED=1\n.");
    TF_EQ(load1(), RQ_OK);
    TF_EQ(g_m.changed, 1);
    TF_EQ(mio_check(&g_m), MM_TRUNC);
}

/* ---------------------------------------------------------------------
 * The lines.
 * --------------------------------------------------------------------- */
static void test_lines(void)
{
    while (g_m.n > 0)
        mio_delete(&g_m, 0);
    TF_EQ(g_m.bytes, 0);
    mio_insert(&g_m, 0);
    TF_EQ(g_m.n, 1);
    TF_EQ(g_m.bytes, 0);
    TF_CHECK(line_is(0, ""));
    mio_put(&g_m, 0, "abcdef", 6);
    TF_EQ(g_m.bytes, 6);
    mio_put(&g_m, 0, "abcxyz", 3);
    TF_CHECK(line_is(0, "abc"));
    TF_EQ(g_m.bytes, 3);
    mio_insert(&g_m, 1);
    mio_put(&g_m, 1, "de", 2);
    TF_EQ(g_m.bytes, 3 + 2 + 2);
    mio_insert(&g_m, 1);
    TF_EQ(g_m.n, 3);
    TF_EQ(g_m.bytes, 3 + 2 + 2 + 2);
    TF_CHECK(line_is(0, "abc"));
    TF_CHECK(line_is(1, ""));
    TF_CHECK(line_is(2, "de"));
    mio_insert(&g_m, 0);
    mio_put(&g_m, 0, "z", 1);
    TF_CHECK(line_is(1, "abc"));
    TF_CHECK(line_is(3, "de"));
    TF_EQ(g_m.bytes, 1 + 2 + 3 + 2 + 2 + 2);
    mio_delete(&g_m, 1);
    TF_EQ(g_m.n, 3);
    TF_CHECK(line_is(0, "z"));
    TF_CHECK(line_is(1, ""));
    TF_CHECK(line_is(2, "de"));
    TF_EQ(g_m.bytes, 1 + 2 + 2 + 2);
    mio_delete(&g_m, 2);
    mio_delete(&g_m, 0);
    TF_EQ(g_m.n, 1);
    TF_EQ(g_m.bytes, 0);
    mio_delete(&g_m, 0);
    TF_EQ(g_m.n, 0);
    TF_EQ(g_m.bytes, 0);
}

/* ---------------------------------------------------------------------
 * Saving.
 * --------------------------------------------------------------------- */

/* 1 when item 1's memo, loaded into a mio of its own, is the first n
   lines of g_m, trailing spaces dropped. */
static int held(u16 n)
{
    char a[MEMO_SLOT];
    char b[MEMO_SLOT];
    mio m;
    rq_res res;
    u16 i;
    u16 l;
    int ok;

    if (!TF_EQ(mio_open(&m), RQ_OK))
        return 0;
    ok = TF_EQ(mio_load(&m, tc_lk, "ITEM", "1", &res), RQ_OK) &&
         TF_EQ(m.n, n);
    for (i = 0; ok && i < n; i++) {
        mio_get(&g_m, i, a);
        mio_get(&m, i, b);
        l = (u16)strlen(a);
        while (l > 0 && a[l - 1] == ' ')
            a[--l] = '\0';
        ok = TF_STREQ(b, a);
    }
    mio_close(&m);
    return ok;
}

/* 1 when item 1's memo holds n lines, the first being FAIL_W letters
   from the one of seed. */
static int kept(u16 n, u16 seed)
{
    char a[MEMO_SLOT];
    char b[MEMO_SLOT];
    mio m;
    rq_res res;
    int ok;

    if (!TF_EQ(mio_open(&m), RQ_OK))
        return 0;
    ok = TF_EQ(mio_load(&m, tc_lk, "ITEM", "1", &res), RQ_OK) &&
         TF_EQ(m.n, n);
    if (ok) {
        letters(a, FAIL_W, seed);
        mio_get(&m, 0, b);
        ok = TF_STREQ(b, a);
    }
    mio_close(&m);
    return ok;
}

static void test_save_split(void)
{
    char s[SPLIT_N][MEMO_SLOT];
    const char *p[SPLIT_N];
    char want[LINK_MAXLINE + 1];
    rq_res res;
    u16 done;
    u16 i;

    for (i = 0; i < SPLIT_N; i++) {
        letters(s[i], (u16)(i + 1 < SPLIT_N ? SPLIT_A : SPLIT_B), i);
        p[i] = s[i];
    }
    make(p, SPLIT_N);
    tl_clear();
    TF_EQ(save1(SPLIT_N, &done, &res), RQ_OK);
    TF_EQ(done, SPLIT_N);
    TF_EQ(tl_count(), 1);
    strcpy(want, "ITEM SET 1 NOTES=\"");
    for (i = 0; i < SPLIT_N; i++) {
        if (i > 0)
            strcat(want, "\\n");
        strcat(want, s[i]);
    }
    strcat(want, "\"");
    TF_EQ(strlen(want), LINK_MAXLINE);
    TF_STREQ(tl_req(0), want);
    TF_CHECK(held(SPLIT_N));

    /* One byte more: the memo is staged, its last line in the END= that
       writes it. */
    letters(s[SPLIT_N - 1], (u16)(SPLIT_B + 1), 7);
    mio_put(&g_m, SPLIT_N - 1, s[SPLIT_N - 1], (u16)(SPLIT_B + 1));
    tl_clear();
    TF_EQ(save1(SPLIT_N, &done, &res), RQ_OK);
    TF_EQ(done, SPLIT_N);
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(0), "ITEM NOTES 1 NEW=\""));
    TF_EQ(strlen(tl_req(0)), LINK_MAXLINE - 2 - SPLIT_B);
    strcpy(want, "ITEM NOTES 1 END=");
    strcat(want, s[SPLIT_N - 1]);
    TF_STREQ(tl_req(1), want);
    TF_CHECK(held(SPLIT_N));
}

/* MEMO_MAXLINES lines of quotes, backslashes and spaces. */
static void test_save_max(void)
{
    char lb[MEMO_SLOT];
    rq_res res;
    u16 done;
    u16 i;
    u16 n;

    while (g_m.n > 0)
        mio_delete(&g_m, 0);
    for (i = 0; i < MEMO_MAXLINES; i++) {
        sprintf(lb, "L%03u \"\\ %c", (unsigned)i, 'a' + i % 26);
        mio_insert(&g_m, i);
        mio_put(&g_m, i, lb, (u16)strlen(lb));
    }
    tl_clear();
    TF_EQ(save1(MEMO_MAXLINES, &done, &res), RQ_OK);
    TF_EQ(done, MEMO_MAXLINES);
    TF_CHECK(!tl_full());
    n = tl_count();
    TF_CHECK(n > 2);
    TF_CHECK(tf_starts(tl_req(0), "ITEM NOTES 1 NEW=\"L000 "));
    for (i = 0; i < n; i++) {
        TF_CHECK(strlen(tl_req(i)) <= LINK_MAXLINE);
        if (i > 0)
            TF_CHECK(tf_starts(tl_req(i), i + 1 < n ?
                               "ITEM NOTES 1 PART=\"" :
                               "ITEM NOTES 1 END=\""));
    }
    /* The NEW is full: the next line would pass, its LINE_COST bytes
       and its escaped newline. */
    TF_CHECK(strlen(tl_req(0)) + LINE_COST + 2 > LINK_MAXLINE);
    TF_CHECK(held(MEMO_MAXLINES));
}

/* A PART= keeps room for its key, one byte longer than NEW='s: of three
   requests' worth of lines that would each fill a NEW=, the second
   request takes one fewer. */
static void test_save_part(void)
{
    char lb[MEMO_SLOT];
    rq_res res;
    u16 done;
    u16 i;

    while (g_m.n > 0)
        mio_delete(&g_m, 0);
    for (i = 0; i < 3 * PART_N; i++) {
        letters(lb, PART_W, i);
        mio_insert(&g_m, i);
        mio_put(&g_m, i, lb, PART_W);
    }
    tl_clear();
    TF_EQ(save1(3 * PART_N, &done, &res), RQ_OK);
    TF_EQ(tl_count(), 4);
    TF_EQ(strlen(tl_req(0)), LINK_MAXLINE);
    TF_EQ(strlen(tl_req(1)), LINK_MAXLINE - PART_W - 1);
    TF_CHECK(held(3 * PART_N));
}

/* Trailing spaces and empty lines, the clear, and failures. */
static void test_save_trim(void)
{
    static const char *const trail[] = { "ab  ", " ", "", "" };
    static const char *const inner[] = { "", "x  y", "", "z" };
    static const char *const blank[] = { "   ", "" };
    rq_res res;
    u16 done;

    make(trail, 4);
    tl_clear();
    TF_EQ(save1(1, &done, &res), RQ_OK);
    TF_EQ(done, 1);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM SET 1 NOTES=ab");
    TF_CHECK(held(1));

    make(inner, 4);
    tl_clear();
    TF_EQ(save1(4, &done, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM SET 1 NOTES=\"\\nx  y\\n\\nz\"");
    TF_CHECK(held(4));

    /* The clear: no line, or only blank ones. */
    make(blank, 2);
    tl_clear();
    TF_EQ(save1(0, &done, &res), RQ_OK);
    TF_EQ(done, 0);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM SET 1 NOTES=\"\"");
    TF_CHECK(held(0));
    make(blank, 0);
    TF_EQ(tc_send("ITEM SET 1 NOTES=x"), RQ_OK);
    tl_clear();
    TF_EQ(save1(0, &done, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM SET 1 NOTES=\"\"");
    TF_CHECK(held(0));
}

static void test_save_fail(void)
{
    char lb[MEMO_SLOT];
    rq_res res;
    u16 done;
    u16 i;

    while (g_m.n > 0)
        mio_delete(&g_m, 0);
    for (i = 0; i < FAIL_LINES; i++) {
        letters(lb, FAIL_W, i);
        mio_insert(&g_m, i);
        mio_put(&g_m, i, lb, FAIL_W);
    }
    tl_clear();
    TF_EQ(save1(FAIL_LINES, &done, &res), RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_CHECK(held(FAIL_LINES));

    /* A memo of other lines fails at each request in turn: done counts
       the lines of the requests before, and the server keeps the memo
       it had, all FAIL_LINES lines of it. */
    for (i = 0; i < FAIL_LINES; i++) {
        letters(lb, FAIL_W, (u16)(i + 1));
        mio_put(&g_m, i, lb, FAIL_W);
    }
    for (i = 1; i <= 3; i++) {
        tl_clear();
        tl_fail(i, "-ERR 302 disk full");
        TF_EQ(save1(FAIL_LINES, &done, &res), RQ_EFULL);
        TF_EQ(done, (i - 1) * FAIL_N);
        TF_EQ(res.code, RQ_EFULL);
        TF_EQ(tl_count(), i);
        TF_CHECK(kept(FAIL_LINES, 0));
    }
    TF_CHECK(tf_starts(tl_req(2), "ITEM NOTES 1 END=\""));
}

/* The session ending under a save, which ends the test's session. */
static void test_save_closed(void)
{
    rq_res res;
    u16 done;

    tl_clear();
    tl_fail(2, NULL);
    TF_EQ(save1(FAIL_LINES, &done, &res), RQ_ECLOSED);
    TF_EQ(done, FAIL_N);
    TF_EQ(tl_count(), 2);
}

/* Open the link again on the test's data and log in, as a client that
   connects again after losing its link; 1 on success. */
static int relink(void)
{
    link_clock clk;
    char why[LINK_WHYSZ];

    link_close(tc_lk);
    tc_clock(&clk);
    return TF_EQ(link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why),
                 0) &&
           TF_EQ(tc_send("HELLO TEST"), RQ_OK) &&
           TF_EQ(tc_send("LOGIN ADMIN secret1"), RQ_OK);
}

/* Saves that fail, and what mio_failmsg makes of them.  The link lost
   at a PART=: the stored memo kept, MM_PARTIAL.  The link lost once the
   server has written the memo of an END=, or before the answer to a
   SET: done is total, MM_MAYBE, and the END= did write the memo.  A
   NEW= an older server refuses: MM_OLDSRV, nothing written.  A plain
   refusal of the first request: no message. */
static void test_save_lost(void)
{
    static const char *const one[] = { "only" };
    rq_res res;
    u16 done;
    int rc;

    if (!relink())
        return;
    tl_clear();
    tl_fail(2, tl_lost);
    rc = save1(FAIL_LINES, &done, &res);
    TF_EQ(rc, RQ_ELOST);
    TF_EQ(done, FAIL_N);
    TF_EQ(mio_failmsg(rc, done, FAIL_LINES, &res), MM_PARTIAL);
    if (!relink())
        return;
    TF_CHECK(kept(FAIL_LINES, 0));

    tl_clear();
    tl_fail(3, tl_gone);
    rc = save1(FAIL_LINES, &done, &res);
    TF_EQ(rc, RQ_ELOST);
    TF_EQ(done, FAIL_LINES);
    TF_EQ(mio_failmsg(rc, done, FAIL_LINES, &res), MM_MAYBE);
    if (!relink())
        return;
    TF_CHECK(kept(FAIL_LINES, 1));

    tl_clear();
    tl_fail(1, "-ERR 102 unexpected key FIELD=NEW");
    for (done = 0; done < FAIL_LINES; done++)
        mio_put(&g_m, done, "changed", 7);
    rc = save1(FAIL_LINES, &done, &res);
    TF_EQ(rc, RQ_EARG);
    TF_EQ(mio_failmsg(rc, done, FAIL_LINES, &res), MM_OLDSRV);
    TF_CHECK(kept(FAIL_LINES, 1));
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    rc = save1(FAIL_LINES, &done, &res);
    TF_EQ(mio_failmsg(rc, done, FAIL_LINES, &res), 0);
    make(one, 0);
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    rc = save1(0, &done, &res);
    TF_EQ(mio_failmsg(rc, done, 0, &res), 0);

    make(one, 1);
    tl_clear();
    tl_fail(1, tl_lost);
    rc = save1(1, &done, &res);
    TF_EQ(rc, RQ_ELOST);
    TF_EQ(done, 1);
    TF_EQ(mio_failmsg(rc, done, 1, &res), MM_MAYBE);
    relink();
}

/* ---------------------------------------------------------------------
 * Text.
 * --------------------------------------------------------------------- */
static void test_text(void)
{
    static const char *const three[] = { "ab", "", "cd" };
    u16 i;

    make(three, 3);
    mio_text(&g_m, g_text, TEXTSZ);
    TF_STREQ(g_text, "ab\r\n\r\ncd");
    TF_EQ(strlen(g_text), g_m.bytes);
    mio_text(&g_m, g_text, 8);
    TF_STREQ(g_text, "ab\r\n\r\nc");
    mio_text(&g_m, g_text, 4);
    TF_STREQ(g_text, "ab");
    mio_text(&g_m, g_text, 2);
    TF_STREQ(g_text, "a");
    mio_text(&g_m, g_text, 1);
    TF_STREQ(g_text, "");
    make(three, 0);
    memset(g_text, 'x', 4);
    mio_text(&g_m, g_text, TEXTSZ);
    TF_STREQ(g_text, "");

    /* A memo of LINK_MAXMEMO bytes fills LINK_MAXMEMO + 1. */
    for (i = 0; g_m.bytes + 2 + MEMO_COLS <= LINK_MAXMEMO; i++) {
        letters(g_text, MEMO_COLS, i);
        mio_insert(&g_m, i);
        mio_put(&g_m, i, g_text, MEMO_COLS);
    }
    letters(g_text, (u16)(LINK_MAXMEMO - g_m.bytes - 2), 0);
    mio_insert(&g_m, i);
    mio_put(&g_m, i, g_text, (u16)strlen(g_text));
    TF_EQ(g_m.bytes, LINK_MAXMEMO);
    memset(g_text, 'x', TEXTSZ);
    mio_text(&g_m, g_text, LINK_MAXMEMO + 1);
    TF_EQ(strlen(g_text), LINK_MAXMEMO);
    TF_EQ(g_text[MEMO_COLS], '\r');
    TF_EQ(g_text[MEMO_COLS + 1], '\n');
}

static void test_set_text(void)
{
    rq_res res;
    u16 done;
    u16 i;
    u16 n;

    mio_set_text(&g_m, "");
    TF_EQ(g_m.n, 0);
    TF_EQ(g_m.bytes, 0);
    mio_set_text(&g_m, "a");
    TF_EQ(g_m.n, 1);
    TF_CHECK(line_is(0, "a"));
    mio_set_text(&g_m, "a\r\nb\rc\nd\r\n\r\ne");
    TF_EQ(g_m.n, 6);
    TF_CHECK(line_is(0, "a"));
    TF_CHECK(line_is(1, "b"));
    TF_CHECK(line_is(2, "c"));
    TF_CHECK(line_is(3, "d"));
    TF_CHECK(line_is(4, ""));
    TF_CHECK(line_is(5, "e"));
    TF_EQ(g_m.bytes, 5 + 2 * 5);
    TF_EQ(mio_check(&g_m), 0);
    mio_set_text(&g_m, "a\r\n");
    TF_EQ(g_m.n, 2);
    TF_CHECK(line_is(1, ""));
    mio_set_text(&g_m, "\n\r");
    TF_EQ(g_m.n, 3);
    TF_EQ(g_m.bytes, 4);
    mio_set_text(&g_m, "\r\r\n");
    TF_EQ(g_m.n, 3);

    /* The text round trip. */
    mio_set_text(&g_m, "one\r\n\r\n  two  \r\nthree");
    mio_text(&g_m, g_text, TEXTSZ);
    TF_STREQ(g_text, "one\r\n\r\n  two  \r\nthree");
    TF_EQ(strlen(g_text), g_m.bytes);

    /* A load's conversions, the trunc of a load cleared. */
    tl_clear();
    tl_fail(1, "+DATA\nL x\nK TRUNCATED=1\n.");
    TF_EQ(load1(), RQ_OK);
    mio_set_text(&g_m, "\tx\001y");
    TF_EQ(g_m.trunc, 0);
    TF_EQ(g_m.n, 1);
    TF_CHECK(line_is(0, "        x?y"));
    TF_EQ(mio_check(&g_m), MM_CHANGED);
    letters(g_text, MEMO_COLS + 1, 0);
    mio_set_text(&g_m, g_text);
    TF_EQ(g_m.n, 2);
    TF_EQ(mio_check(&g_m), MM_CHANGED);

    /* Past the limits. */
    n = 0;
    for (i = 0; i < MEMO_MAXLINES; i++) {
        g_text[n++] = 'x';
        g_text[n++] = '\r';
        g_text[n++] = '\n';
    }
    g_text[n++] = 'y';
    g_text[n] = '\0';
    mio_set_text(&g_m, g_text);
    TF_EQ(g_m.n, MEMO_MAXLINES + 1);
    TF_EQ(mio_check(&g_m), MM_TOOMANY);
    TF_EQ(g_m.n, MEMO_MAXLINES);
    g_text[n - 3] = '\0';
    mio_set_text(&g_m, g_text);
    TF_EQ(g_m.n, MEMO_MAXLINES);
    TF_EQ(mio_check(&g_m), 0);
    n = 0;
    while (n + MEMO_COLS + 2 < TEXTSZ) {
        letters(g_text + n, MEMO_COLS, n);
        n = (u16)(n + MEMO_COLS);
        g_text[n++] = '\r';
        g_text[n++] = '\n';
    }
    g_text[n] = '\0';
    mio_set_text(&g_m, g_text);
    TF_CHECK(g_m.bytes > LINK_MAXMEMO);
    TF_EQ(mio_check(&g_m), MM_TOOLONG);

    /* A text saved and read back. */
    mio_set_text(&g_m, "first\r\nsecond\rthird\n");
    tl_clear();
    TF_EQ(save1(3, &done, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM SET 1 NOTES=\"first\\nsecond\\nthird\"");
    TF_CHECK(held(3));
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    int rc;

    tf_begin("T_MEMOIO");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_MEMOIO dir\n");
        return 2;
    }
    cmem_init();
    test_open();
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_MEMOIO: %s\n", why);
        return tf_done();
    }
    if (tc_admin() &&
        TF_EQ(tc_send("ITEM ADD CATEGORY=SYS MFR=X MODEL=Y LOC_ID=A1"),
              RQ_OK)) {
        test_load_small();
        test_load_max();
        test_load_convert();
        test_load_limits();
        test_lines();
        test_save_split();
        test_save_max();
        test_save_part();
        test_save_trim();
        test_text();
        test_set_text();
        test_save_fail();
        test_save_closed();
        test_save_lost();
    }
    mio_close(&g_m);
    TF_EQ(link_close(tc_lk), 0);
    te_remove(g_ddir, NULL);
    return tf_done();
}

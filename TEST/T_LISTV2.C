/*
 * T_LISTV2.C - the second half of the T_LISTV test (see T_LISTV.C):
 * the format functions' columns, and the pager through windows taller
 * than one protocol page, over the engine's list and over answers of the
 * test's own.  It is a file of its own only to stay within the compiler
 * memory of Watcom C 6.5; T_LISTV.H holds what both share.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "LISTV.H"
#include "TRLINK.H"
/* Checks keep no expression text, as in T_LISTV.C. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_PART
#define TF_FILE "T_LISTV2"
#define TF_KEYSRC
#define TF_LINK
#include "TESTFW.H"
#include "T_LISTV.H"

/* ---------------------------------------------------------------------
 * Columns.
 * --------------------------------------------------------------------- */
#define COL_GUARD       '#'

/* Columns: cut to the width, left- or right-aligned, a separating space
   while there is room, nothing past LV_ROWTEXT. */
void test_columns(void)
{
    char t[LV_ROWTEXT + 2];
    char want[LV_ROWTEXT + 2];
    char blank[MSG_DATEW + 2];
    static const char *const nodate[] = {
        "", "2026100", "202610091", "2026-10-", "2026100X", " 2026100",
        NULL
    };
    u16 at;
    u16 i;

    memset(t, COL_GUARD, sizeof t);
    at = 0;
    lv_col(t, &at, "AB", 5, 0);
    TF_EQ(at, 6);
    TF_STREQ(t, "AB    ");
    lv_col(t, &at, "7", 4, 1);
    TF_EQ(at, 11);
    TF_STREQ(t, "AB       7 ");
    /* Longer than the column: the first w characters, either way. */
    lv_col(t, &at, "ABCDEFG", 3, 1);
    TF_EQ(at, 15);
    TF_STREQ(t, "AB       7 ABC ");
    lv_col(t, &at, "WXYZ", 3, 0);
    TF_EQ(at, 19);
    TF_STREQ(t, "AB       7 ABC WXY ");
    /* Leading spaces of the value are not shown. */
    lv_col(t, &at, "  x", 3, 0);
    TF_EQ(at, 23);
    TF_STREQ(t, "AB       7 ABC WXY x   ");
    lv_col(t, &at, "", 2, 1);
    TF_EQ(at, 26);
    TF_STREQ(t, "AB       7 ABC WXY x      ");
    TF_EQ(t[LV_ROWTEXT + 1], COL_GUARD);

    /* A column ending at LV_ROWTEXT: no separator after it. */
    memset(t, COL_GUARD, sizeof t);
    memset(t, ' ', LV_ROWTEXT - 4);
    t[LV_ROWTEXT - 4] = '\0';
    at = LV_ROWTEXT - 4;
    lv_col(t, &at, "WXYZ", 4, 0);
    TF_EQ(at, LV_ROWTEXT);
    TF_EQ(strlen(t), LV_ROWTEXT);
    TF_STREQ(t + LV_ROWTEXT - 4, "WXYZ");
    TF_EQ(t[LV_ROWTEXT + 1], COL_GUARD);
    /* At LV_ROWTEXT already: nothing more is written. */
    lv_col(t, &at, "Q", 1, 0);
    TF_EQ(at, LV_ROWTEXT);
    TF_EQ(strlen(t), LV_ROWTEXT);
    TF_STREQ(t + LV_ROWTEXT - 4, "WXYZ");
    TF_EQ(t[LV_ROWTEXT + 1], COL_GUARD);

    /* A column that would pass LV_ROWTEXT is left out: the row and *at
       stay as they were. */
    memset(t, COL_GUARD, sizeof t);
    memset(t, ' ', LV_ROWTEXT - 2);
    t[LV_ROWTEXT - 2] = '\0';
    at = LV_ROWTEXT - 2;
    lv_col(t, &at, "WXYZ", 4, 0);
    TF_EQ(at, LV_ROWTEXT - 2);
    TF_EQ(strlen(t), LV_ROWTEXT - 2);
    TF_EQ(t[LV_ROWTEXT + 1], COL_GUARD);
    for (i = 0; i < LV_ROWTEXT - 2; i++)
        if (t[i] != ' ')
            break;
    TF_EQ(i, LV_ROWTEXT - 2);

    /* Date columns: YYYY-MM-DD in MSG_DATEW cells, blank on a value that
       is not exactly MSG_DATELEN digits. */
    memset(t, COL_GUARD, sizeof t);
    at = 0;
    lv_datecol(t, &at, "20261009");
    TF_EQ(at, MSG_DATEW + 1);
    TF_STREQ(t, "2026-10-09 ");
    memset(blank, ' ', MSG_DATEW + 1);
    blank[MSG_DATEW + 1] = '\0';
    for (i = 0; nodate[i] != NULL; i++) {
        memset(t, COL_GUARD, sizeof t);
        at = 0;
        lv_datecol(t, &at, nodate[i]);
        TF_EQ(at, MSG_DATEW + 1);
        TF_STREQ(t, blank);
    }
    /* After another column, at the place it leaves. */
    memset(t, COL_GUARD, sizeof t);
    at = 0;
    lv_col(t, &at, "AB", 2, 0);
    lv_datecol(t, &at, "x");
    TF_EQ(at, 3 + MSG_DATEW + 1);
    strcpy(want, "AB ");
    strcat(want, blank);
    TF_STREQ(t, want);
    lv_datecol(t, &at, "19991231");
    TF_EQ(at, 3 + 2 * (MSG_DATEW + 1));
    strcat(want, "1999-12-31 ");
    TF_STREQ(t, want);
}

/* ---------------------------------------------------------------------
 * Windows taller than one protocol page.
 * --------------------------------------------------------------------- */
#define TALL_SOME       40      /* more than LINK_PAGEMAX, under
                                   LV_MAXPAGE                           */

static const char k_page[] = " PAGE=";

/* Every request logged since tl_clear is a page request for 1 to
   LINK_PAGEMAX rows; clears the log. */
static void page_caps(void)
{
    u16 i;
    const char *p;
    int n;

    TF_CHECK(!tl_full());
    for (i = 0; i < tl_count(); i++) {
        p = strstr(tl_req(i), k_page);
        if (!TF_CHECK(p != NULL))
            continue;
        n = atoi(p + sizeof k_page - 1);
        TF_CHECK(n >= 1 && n <= LINK_PAGEMAX);
    }
    tl_clear();
}

/* One answer of a scripted list: n rows with the next ids and the
   cursor cur after them, or none when the list ends there. */
typedef struct {
    u8 n;
    const char *cur;
} tall_step;

/* The longest answer of a script: the F line, LINK_PAGEMAX rows with ids
   under 1000, a C line of a cursor of at most FK_CURLEN characters, and
   the end line. */
#define FK_HEAD         "+DATA\nF ITEM_ID\n"
#define FK_ROW          "R 999\n"
#define FK_CURLEN       8
#define FK_MAX          (sizeof FK_HEAD + LINK_PAGEMAX * (sizeof FK_ROW - 1) \
                         + FK_CURLEN + sizeof "C \n.")

static char g_fk[FK_MAX];
static u16 g_fk_id;
static const tall_step *g_fk_steps;
static u16 g_fk_n;

/* tl_hook: answer the s-th ITEM LIST request with the s-th step, while
   there are steps. */
static void fk_answer(const char *line, u16 len)
{
    const tall_step *st;
    u16 k;

    if (!is_list(line, len) || tl_count() > g_fk_n)
        return;
    st = &g_fk_steps[tl_count() - 1];
    TF_CHECK(st->n <= LINK_PAGEMAX && g_fk_id + st->n < 1000);
    TF_CHECK(st->cur == NULL || strlen(st->cur) <= FK_CURLEN);
    strcpy(g_fk, FK_HEAD);
    for (k = 0; k < st->n; k++) {
        strcat(g_fk, "R ");
        txt_u32(g_fk + strlen(g_fk), g_fk_id++);
        strcat(g_fk, "\n");
    }
    if (st->cur != NULL) {
        strcat(g_fk, "C ");
        strcat(g_fk, st->cur);
        strcat(g_fk, "\n");
    }
    strcat(g_fk, ".");
    tl_fail(tl_count(), g_fk);
}

/* Script the next requests with n steps; the rows count from id 1. */
static void fk_script(const tall_step *steps, u16 n)
{
    g_fk_steps = steps;
    g_fk_n = n;
    g_fk_id = 1;
    tl_clear();
    tl_hook = fk_answer;
}

/* A window of LV_MAXPAGE rows is opened (test_vals refuses more). */
static void test_tall_open(void)
{
    lv *v;

    TF_EQ(LV_MAXPAGE, 64);
    v = lv_open(tc_lk, &items_src, 2, 1, LV_MAXPAGE + 1, 60);
    if (TF_CHECK(v != NULL))
        lv_close(v);
}

/* The first page of a tall window takes one request per protocol page,
   each asking for the rows still missing, the second from the cursor
   after the first page's rows. */
static void test_tall_first(void)
{
    lv *v;
    char c25[LINK_MAXCURSOR + 1];
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, LINK_PAGEMAX + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    for (i = 0; i < LINK_PAGEMAX; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    strcpy(c25, after_of(tl_req(0)));
    TF_CHECK(c25[0] != '\0');
    lv_close(v);

    v = lv_open(tc_lk, &items_src, 2, 1, TALL_SOME + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=ID PAGE=25");
    TF_CHECK(tf_starts(tl_req(1), "ITEM LIST ORDER=ID PAGE=15 AFTER="));
    TF_STREQ(after_of(tl_req(1)), c25);
    TF_EQ(lv_rows(v), TALL_SOME);
    TF_EQ(sel_id(v), 1);
    lv_close(v);

    v = lv_open(tc_lk, &items_src, 2, 1, LV_MAXPAGE + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=ID PAGE=25");
    TF_CHECK(tf_starts(tl_req(1), "ITEM LIST ORDER=ID PAGE=25 AFTER="));
    TF_STREQ(after_of(tl_req(1)), c25);
    TF_CHECK(tf_starts(tl_req(2), "ITEM LIST ORDER=ID PAGE=14 AFTER="));
    TF_CHECK(strcmp(after_of(tl_req(2)), c25) != 0);
    TF_EQ(lv_rows(v), LV_MAXPAGE);
    lv_close(v);
}

/* A tall window over the whole list: Down to the last row and Up to
   the first land on every row in turn, End and the page moves on the
   right rows, and no request asks for more than a protocol page. */
static void test_tall_walk(u8 ps)
{
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, (u8)(ps + 1), 60);
    if (!TF_CHECK(v != NULL))
        return;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 2; i <= g_nitems; i++) {
        TF_EQ(lv_down(v), RQ_OK);
        TF_EQ(sel_id(v), i);
    }
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    page_caps();
    for (i = g_nitems - 1; i >= 1; i--) {
        TF_EQ(lv_up(v), RQ_OK);
        TF_EQ(sel_id(v), i);
    }
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    page_caps();

    /* PgDn through the pages, stopping on the last row; PgUp back. */
    for (i = 1; i + ps <= g_nitems; i = (u16)(i + ps)) {
        TF_EQ(sel_id(v), i);
        TF_EQ(lv_pgdn(v), RQ_OK);
    }
    TF_EQ(sel_id(v), i);
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(g_nitems - ps));
    page_caps();

    /* End from the top ends on the last row, the rows above it held. */
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    TF_CHECK(lv_rows(v) >= ps);
    page_caps();
    for (i = 1; i < ps; i++) {
        TF_EQ(lv_up(v), RQ_OK);
        TF_EQ(sel_id(v), (u32)(g_nitems - i));
    }
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(g_nitems - ps - ps + 1));
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    for (i = 0; i < 2 * ps; i++)
        TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(g_nitems - 2 * ps));
    page_caps();
    lv_close(v);
}

/* A request that returns fewer rows than asked for, or none, and has a
   cursor, is followed by one asking for the rows still missing from it,
   at most a protocol page; the cursor after the page is the last
   answer's, which the page after it goes on from. */
static void test_tall_short(void)
{
    static const tall_step s_a[] = {
        { 10, "K1" }, { 20, "K2" }, { 10, "K3" }, { 25, "K4" }, { 15, "K5" }
    };
    static const tall_step s_b[] = { { 0, "K1" }, { 25, "K2" }, { 5, "K3" } };
    static const tall_step s_c[] = { { 25, "K1" }, { 7, NULL } };
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, TALL_SOME + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    fk_script(s_a, 5);
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=ID PAGE=25");
    TF_STREQ(tl_req(1), "ITEM LIST ORDER=ID PAGE=25 AFTER=K1");
    TF_STREQ(tl_req(2), "ITEM LIST ORDER=ID PAGE=10 AFTER=K2");
    TF_EQ(lv_rows(v), TALL_SOME);
    for (i = 1; i < TALL_SOME; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), TALL_SOME);
    TF_EQ(tl_count(), 3);
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(tl_count(), 5);
    TF_STREQ(tl_req(3), "ITEM LIST ORDER=ID PAGE=25 AFTER=K3");
    TF_STREQ(tl_req(4), "ITEM LIST ORDER=ID PAGE=15 AFTER=K4");
    TF_EQ(sel_id(v), TALL_SOME + 1);
    TF_EQ(lv_rows(v), 2 * TALL_SOME);
    tl_hook = NULL;
    lv_close(v);

    v = lv_open(tc_lk, &items_src, 2, 1, 31, 60);
    if (!TF_CHECK(v != NULL))
        return;
    fk_script(s_b, 3);
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=ID PAGE=25");
    TF_STREQ(tl_req(1), "ITEM LIST ORDER=ID PAGE=25 AFTER=K1");
    TF_STREQ(tl_req(2), "ITEM LIST ORDER=ID PAGE=5 AFTER=K2");
    TF_EQ(lv_rows(v), 30);
    tl_hook = NULL;
    lv_close(v);

    /* The list ends inside the second request: no third. */
    v = lv_open(tc_lk, &items_src, 2, 1, TALL_SOME + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    fk_script(s_c, 2);
    TF_EQ(lv_home(v), RQ_OK);
    tl_hook = NULL;
    TF_EQ(tl_count(), 2);
    TF_EQ(lv_rows(v), 32);
    for (i = 1; i < 40; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), 32);
    TF_EQ(tl_count(), 2);
    lv_close(v);
}

/* A request failing in the middle of the page after A keeps the rows
   of those before it; the next move asks for the rows still missing
   from the cursor after them. */
static void test_tall_fail(void)
{
    lv *v;
    char c65[LINK_MAXCURSOR + 1];
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, TALL_SOME + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 1; i < TALL_SOME; i++)
        TF_EQ(lv_down(v), RQ_OK);
    tl_clear();
    tl_fail(2, "-ERR 402 busy");
    TF_EQ(lv_down(v), RQ_EBUSY);
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=25 AFTER="));
    TF_CHECK(tf_starts(tl_req(1), "ITEM LIST ORDER=ID PAGE=15 AFTER="));
    strcpy(c65, after_of(tl_req(1)));
    TF_EQ(lv_rows(v), TALL_SOME + LINK_PAGEMAX);
    TF_EQ(sel_id(v), TALL_SOME);
    /* The rows held are reached without a request; the one after them
       asks for the rows still missing, from the cursor after the held. */
    for (i = 0; i < LINK_PAGEMAX; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), TALL_SOME + LINK_PAGEMAX);
    tl_clear();
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=15 AFTER="));
    TF_STREQ(after_of(tl_req(0)), c65);
    TF_EQ(sel_id(v), TALL_SOME + LINK_PAGEMAX + 1);
    TF_EQ(lv_rows(v), 2 * TALL_SOME);
    for (i = 1; i < TALL_SOME - LINK_PAGEMAX; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), 2 * TALL_SOME);

    /* The first page failing in its second request: Home recovers. */
    tl_clear();
    tl_fail(2, "-ERR 402 busy");
    TF_EQ(lv_home(v), RQ_EBUSY);
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_rows(v), TALL_SOME);
    TF_EQ(sel_id(v), 1);
    for (i = 1; i < TALL_SOME + 5; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), TALL_SOME + 5);

    /* A bad cursor in the middle of a page starts again at the top. */
    tl_clear();
    tl_fail(2, "-ERR 204 bad cursor");
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_CHECK(tl_count() >= 3);
    TF_STREQ(tl_req((u16)(tl_count() - 2)), "ITEM LIST ORDER=ID PAGE=25");
    TF_CHECK(tf_starts(tl_req((u16)(tl_count() - 1)),
                       "ITEM LIST ORDER=ID PAGE=15 AFTER="));
    TF_EQ(sel_id(v), 1);
    TF_EQ(lv_rows(v), TALL_SOME);
    lv_close(v);
}

/* Esc typed during the fill of a tall page, between its requests:
   the rows of the first request stay, with the cursor after them. */
static void test_tall_esc(void)
{
    static const u16 k_esc[] = { KEY_ESC, KEY_NONE };
    static const u16 k_none[] = { KEY_NONE };
    char c25[LINK_MAXCURSOR + 1];
    char num[TXT_U32LEN + 1];
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, TALL_SOME + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 2);
    strcpy(c25, after_of(tl_req(1)));
    tui_info("");
    ahead(k_esc, 1);
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(lv_rows(v), LINK_PAGEMAX);
    txt_u32(num, LINK_PAGEMAX);
    TF_STREQ(g_seen, msg_sub(MT_WORKING, num, NULL));
    ahead(k_none, 1);
    tl_clear();
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_CHECK(tl_count() >= 1);
    TF_STREQ(after_of(tl_req(0)), c25);
    page_caps();
    lv_close(v);

    /* A short page after A is filled on after a stop: the progress of
       the next wait counts the rows already in it and the new ones. */
    v = lv_open(tc_lk, &items_src, 2, 1, LV_MAXPAGE + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 1; i < LV_MAXPAGE; i++)
        TF_EQ(lv_down(v), RQ_OK);
    ahead(k_esc, 1);
    tl_clear();
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(lv_rows(v), LV_MAXPAGE + LINK_PAGEMAX);
    tui_info("");
    ahead(k_esc, 1);
    tl_clear();
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=25 AFTER="));
    TF_EQ(lv_rows(v), LV_MAXPAGE + 2 * LINK_PAGEMAX);
    txt_u32(num, 2 * LINK_PAGEMAX);
    TF_STREQ(g_seen, msg_sub(MT_WORKING, num, NULL));
    key_set_source(NULL, NULL);
    lv_close(v);
}

void test_tall(void)
{
    test_tall_open();
    test_tall_first();
    test_tall_walk(TALL_SOME);
    test_tall_walk(LV_MAXPAGE);
    test_tall_short();
    test_tall_fail();
    test_tall_esc();
}

/*
 * T_ASSY.C - unit test for the assemblies of the engine (ASSY.C), run
 * on a real data directory.
 *
 * usage: T_ASSY [dir]
 *
 * dir (default the current directory) is a directory, or a prefix ending
 * in '\', '/' or ':', that holds the scratch data directory (tfs_set_dir
 * of T_FILES.H); every engine file there is removed before each phase
 * and at the end.  The clock is fixed, so dates are known.  Checks:
 *   - INSTALL (nested parts, a child with parts, a move to another
 *     parent, the no-op), REMOVE and MOVE with their location upkeep,
 *     audit rows and UPDATED stamps; their errors (missing items,
 *     cycles, a child on loan, LOC); ITEM SET of LOC_ID or STATUS=D on
 *     assembly items;
 *   - depth: a chain of ENG_MAXDEPTH levels and one more refused, a
 *     subtree of height 3 under the last level that holds it, its
 *     deleted leaf counted, then undeleted; PATH;
 *   - size: an assembly of ENG_MAXASSY entries, one more refused,
 *     deleted parts counted, a two-item subtree refused, a move inside a
 *     full assembly allowed, UNDEL in a full one, an installed child
 *     moved from another assembly into a nearly full one;
 *   - walks: BOM order and levels, paged two at a time with cursors,
 *     cursors outside the BOM, parts deleted between pages; LIST and
 *     LIST TOP (whole and one per page), CHILDREN.  Then eng_assy_heal on
 *     part locations damaged by hand: live parts mirrored again with M
 *     rows by the actor in effect and UPDATED stamped, a deleted part and
 *     a part with a broken chain left alone, a second pass changing
 *     nothing;
 *   - the read bound: a LIST that spends ENG_MAXREADS - 1 reads and stops
 *     on a deleted part, resumed under the undecided item;
 *   - damaged data: a PARENT_ID cycle (BOM fails instead of looping,
 *     PATH, INSTALL, MOVE, heal) and a negative PARENT_ID (LIST passes it
 *     over, PATH, INSTALL, heal, REMOVE);
 *   - the repair at open: an ASSY MOVE and an ASSY REMOVE cut off after
 *     their first parts, with the marker naming ITEMS they leave:
 *     eng_open puts the parts back at their top-level item's location,
 *     with M rows by SYSTEM and UPDATED stamped, rebuilds ITEMS2 and
 *     removes the marker.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when dir is too long.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGITEM.H"
#include "ENGASSY.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"

/* NASSY assemblies, each a top-level item with a deleted part before a
   live one: three reads per assembly listed.  With ENG_MAXREADS - 2 a
   multiple of three, the first LIST request of test_listmax stops on
   the deleted part of the next assembly, still undecided. */
#define NASSY       70
#define MAXPAGES    100         /* requests of one walk                 */

extern char listmax_fits[1];
extern char listmax_fits[((ENG_MAXREADS - 2) % 3 == 0 &&
                          NASSY * 3 > ENG_MAXREADS) ? 1 : 2];

/* The fields of an audit row as aud gives them. */
static const char k_aud[] = "REC_ID ACTION FLD OLDVAL NEWVAL";

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };
static const eng_time g_next = { 2026, 10, 9, 9, 30, 0 };

static eng *g_e;
/* A walk listed as " id" or " id:level" per item. */
static char g_out[NASSY * (ENG_IDLEN + 1) + 1];

/* ---------------------------------------------------------------------
 * The engine and its files.
 * --------------------------------------------------------------------- */

static void stop(void)
{
    if (g_e != NULL)
        TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    te_remove(tfs_dir(), NULL);
}

/* A fresh data directory with locations A1, B1 and C1. */
static int start(void)
{
    stop();
    if (!TF_EQ(te_open(&g_e, &g_now), ENG_OK))
        return 0;
    TF_EQ(te_loc("A1", 0), 1);
    TF_EQ(te_loc("B1", 0), 2);
    TF_EQ(te_loc("C1", 0), 3);
    return 1;
}

/* ---------------------------------------------------------------------
 * Items, their fields and audit rows.
 * --------------------------------------------------------------------- */

/* A new item at location loc, given by its code. */
static u32 add(const char *loc)
{
    eng_pair p[2];
    u32 id;

    p[0].key = "CATEGORY";
    p[0].val = "CARD";
    p[1].key = "LOC_ID";
    p[1].val = loc;
    id = 0;
    TF_EQ(eng_item_add(g_e, p, 2, &id), ENG_OK);
    return id;
}

/* The text of field name of item id, live or deleted ("?" when there is
   none). */
static const char *fld(u32 id, const char *name)
{
    return te_rtext(ENG_T_ITEMS, te_irec(id), name);
}

/* The code of the location of item id, live or deleted: "" for none,
   "?" for a LOC_ID that is not one of start's. */
static const char *loc(u32 id)
{
    static const char *const codes[] = { "", "A1", "B1", "C1" };
    i32 v;

    if (fld(id, "LOC_ID")[0] == '?')
        return "?";
    v = te_rnum(ENG_T_ITEMS, TE_BUF, "LOC_ID");
    return v >= 0 && v <= 3 ? codes[(int)v] : "?";
}

/* The PARENT_ID of item id, live or deleted; -1 when there is none or
   it is blank. */
static i32 par(u32 id)
{
    const char *v;

    v = fld(id, "PARENT_ID");
    if (v[0] == '\0' || v[0] == '?')
        return -1;
    return te_rnum(ENG_T_ITEMS, TE_BUF, "PARENT_ID");
}

/* Store text (NULL: num) in field name of item id, live or deleted,
   straight into the table, moving its entry in index x (none when x is
   negative, as a change stopped before its index writes leaves it). */
static void raw_put(u32 id, int x, const char *name, const char *text,
                    i32 num)
{
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), x, name, text, num), 0);
}

/* Audit record recno as its k_aud fields. */
static const char *aud(u32 recno)
{
    return te_aud(recno, k_aud);
}

/* 1 when the last two audit rows are x and y, in either order. */
static int two(const char *x, const char *y)
{
    char last[TE_TEXTMAX];

    strcpy(last, aud(te_naud()));
    if (strcmp(last, x) == 0)
        return strcmp(aud(te_naud() - 1), y) == 0;
    return strcmp(last, y) == 0 && strcmp(aud(te_naud() - 1), x) == 0;
}

/* ITEM SET of one pair. */
static int set1(u32 id, const char *key, const char *val)
{
    eng_pair p;

    p.key = key;
    p.val = val;
    return eng_item_set(g_e, id, &p, 1);
}

/* ---------------------------------------------------------------------
 * Changes.
 * --------------------------------------------------------------------- */

static void test_changes(void)
{
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 x;
    u32 y;
    u32 n0;

    if (!start())
        return;
    a = add("A1");
    b = add("B1");
    c = add("C1");
    d = add("C1");
    x = add("C1");
    y = add("A1");
    eng_set_now(g_e, &g_next);

    /* INSTALL: P and M rows, the parent's location, UPDATED. */
    n0 = te_naud();
    TF_EQ(eng_assy_install(g_e, b, a), ENG_OK);
    TF_EQ(par(b), a);
    TF_STREQ(loc(b), "A1");
    TF_STREQ(fld(b, "UPDATED"), "20261009");
    TF_EQ(te_naud(), n0 + 2);
    TF_CHECK(two("2|P|PARENT_ID|0|1", "2|M|LOC_ID|2|1"));
    TF_EQ(eng_assy_install(g_e, c, b), ENG_OK);
    TF_STREQ(loc(c), "A1");
    /* Same location: the P row alone. */
    n0 = te_naud();
    TF_EQ(eng_assy_install(g_e, x, d), ENG_OK);
    TF_EQ(te_naud(), n0 + 1);
    TF_STREQ(aud(te_naud()), "5|P|PARENT_ID|0|4");
    /* A child with a part: the part first, the child last. */
    n0 = te_naud();
    TF_EQ(eng_assy_install(g_e, d, c), ENG_OK);
    TF_EQ(te_naud(), n0 + 3);
    TF_STREQ(aud(n0 + 1), "5|M|LOC_ID|3|1");
    TF_CHECK(two("4|P|PARENT_ID|0|3", "4|M|LOC_ID|3|1"));
    TF_STREQ(loc(x), "A1");
    TF_STREQ(fld(x, "UPDATED"), "20261009");
    /* Again into its own parent: nothing changes. */
    n0 = te_naud();
    TF_EQ(eng_assy_install(g_e, d, c), ENG_OK);
    TF_EQ(te_naud(), n0);
    /* Installed elsewhere in the same assembly: moved, one P row. */
    TF_EQ(eng_assy_install(g_e, x, b), ENG_OK);
    TF_EQ(te_naud(), n0 + 1);
    TF_STREQ(aud(te_naud()), "5|P|PARENT_ID|4|2");

    /* INSTALL errors. */
    TE_FAIL(eng_assy_install(g_e, 999, a), ENG_ENOTFOUND, "CHILD", NULL);
    TE_FAIL(eng_assy_install(g_e, 0, a), ENG_ENOTFOUND, "CHILD", NULL);
    TE_FAIL(eng_assy_install(g_e, y, 999), ENG_ENOTFOUND, "PARENT", NULL);
    TE_FAIL(eng_assy_install(g_e, a, a), ENG_ECONSTR, "PARENT",
            "cannot be put inside itself or its contents");
    TE_FAIL(eng_assy_install(g_e, a, b), ENG_ECONSTR, "PARENT",
            "cannot be put inside itself or its contents");
    TE_FAIL(eng_assy_install(g_e, a, d), ENG_ECONSTR, "PARENT",
            "cannot be put inside itself or its contents");
    TE_FAIL(eng_assy_install(g_e, b, x), ENG_ECONSTR, "PARENT",
            "cannot be put inside itself or its contents");
    raw_put(y, ENG_X_ITEMS1, "ON_LOAN", NULL, 1);
    TE_FAIL(eng_assy_install(g_e, y, a), ENG_ECONSTR, "CHILD",
            "item is on loan");
    raw_put(y, ENG_X_ITEMS1, "ON_LOAN", NULL, 0);

    /* ITEM SET on assembly items. */
    TE_FAIL(set1(b, "STATUS", "D"), ENG_ECONSTR, "STATUS",
            "installed in an assembly");
    TE_FAIL(set1(b, "LOC_ID", "C1"), ENG_ECONSTR, "LOC_ID", NULL);
    TE_FAIL(set1(a, "LOC_ID", "3"), ENG_ECONSTR, "LOC_ID", NULL);

    /* REMOVE: the child and its part. */
    TE_FAIL(eng_assy_remove(g_e, a, "B1"), ENG_ECONSTR, "CHILD",
            "not part of an assembly");
    TE_FAIL(eng_assy_remove(g_e, 999, "B1"), ENG_ENOTFOUND, "CHILD", NULL);
    TE_FAIL(eng_assy_remove(g_e, c, ""), ENG_EARG, "LOC", NULL);
    TE_FAIL(eng_assy_remove(g_e, c, NULL), ENG_EARG, "LOC", NULL);
    TE_FAIL(eng_assy_remove(g_e, c, "  "), ENG_EARG, "LOC", NULL);
    TE_FAIL(eng_assy_remove(g_e, c, "ZZ"), ENG_ECONSTR, "LOC",
            "unknown location");
    TE_FAIL(eng_assy_remove(g_e, c, "9"), ENG_ECONSTR, "LOC", NULL);
    n0 = te_naud();
    TF_EQ(eng_assy_remove(g_e, c, "b1"), ENG_OK);
    TF_EQ(par(c), 0);
    TF_STREQ(loc(c), "B1");
    TF_STREQ(loc(d), "B1");
    TF_EQ(te_naud(), n0 + 3);
    TF_STREQ(aud(n0 + 1), "4|M|LOC_ID|1|2");
    TF_CHECK(two("3|P|PARENT_ID|2|0", "3|M|LOC_ID|1|2"));

    /* MOVE: every item of the assembly, the item named last. */
    TE_FAIL(eng_assy_move(g_e, b, "C1"), ENG_ECONSTR, "ID",
            "installed in an assembly");
    TE_FAIL(eng_assy_move(g_e, 999, "C1"), ENG_ENOTFOUND, "ID", NULL);
    TE_FAIL(eng_assy_move(g_e, a, "ZZ"), ENG_ECONSTR, "LOC",
            "unknown location");
    TE_FAIL(eng_assy_move(g_e, a, ""), ENG_EARG, "LOC", NULL);
    TE_FAIL(eng_assy_move(g_e, a, "  "), ENG_EARG, "LOC", NULL);
    n0 = te_naud();
    TF_EQ(eng_assy_move(g_e, a, "C1"), ENG_OK);
    TF_EQ(te_naud(), n0 + 3);
    TF_STREQ(aud(te_naud()), "1|M|LOC_ID|1|3");
    TF_STREQ(loc(b), "C1");
    TF_STREQ(loc(x), "C1");
    n0 = te_naud();
    TF_EQ(eng_assy_move(g_e, a, "C1"), ENG_OK);
    TF_EQ(te_naud(), n0);
    TF_EQ(eng_assy_move(g_e, y, "B1"), ENG_OK);
    TF_EQ(te_naud(), n0 + 1);
    TF_STREQ(loc(y), "B1");
    /* A deleted part stays; undeleted, it takes its parent's place. */
    TF_EQ(eng_item_del(g_e, x), ENG_OK);
    n0 = te_naud();
    TF_EQ(eng_assy_move(g_e, a, "A1"), ENG_OK);
    TF_EQ(te_naud(), n0 + 2);
    TF_STREQ(loc(x), "C1");
    TF_EQ(eng_item_undel(g_e, x), ENG_OK);
    TF_STREQ(loc(x), "A1");
}

/* ---------------------------------------------------------------------
 * Depth.
 * --------------------------------------------------------------------- */

static void test_depth(void)
{
    u32 l[ENG_MAXDEPTH + 1];
    u32 recs[ENG_MAXDEPTH];
    u32 t;
    u32 u;
    u32 v;
    u16 i;
    u16 n;

    if (!start())
        return;
    for (i = 0; i <= ENG_MAXDEPTH; i++)
        l[i] = add("B1");
    for (i = 1; i < ENG_MAXDEPTH; i++)
        TF_EQ(eng_assy_install(g_e, l[i], l[i - 1]), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, l[ENG_MAXDEPTH], l[ENG_MAXDEPTH - 1]),
            ENG_ECONSTR, "PARENT", "parts nested more than 16 levels deep");

    /* A subtree of height 3 fits only under level ENG_MAXDEPTH - 3; its
       deleted leaf counts. */
    t = add("A1");
    u = add("A1");
    v = add("A1");
    TF_EQ(eng_assy_install(g_e, u, t), ENG_OK);
    TF_EQ(eng_assy_install(g_e, v, u), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, t, l[ENG_MAXDEPTH - 3]), ENG_ECONSTR,
            "PARENT", "parts nested more than 16 levels deep");
    TF_EQ(eng_item_del(g_e, v), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, t, l[ENG_MAXDEPTH - 3]), ENG_ECONSTR,
            "PARENT", "parts nested more than 16 levels deep");
    TF_EQ(eng_assy_install(g_e, t, l[ENG_MAXDEPTH - 4]), ENG_OK);
    TF_STREQ(loc(u), "B1");
    TF_STREQ(loc(v), "A1");
    TF_EQ(eng_item_undel(g_e, v), ENG_OK);
    TF_STREQ(loc(v), "B1");

    /* PATH: the ancestors, top-level first. */
    TF_EQ(eng_assy_path(g_e, v, recs, &n), ENG_OK);
    if (TF_EQ(n, ENG_MAXDEPTH - 1)) {
        for (i = 0; i < ENG_MAXDEPTH - 3; i++)
            TF_EQ(te_rnum(ENG_T_ITEMS, recs[i], "ITEM_ID"), l[i]);
        TF_EQ(te_rnum(ENG_T_ITEMS, recs[n - 2], "ITEM_ID"), t);
        TF_EQ(te_rnum(ENG_T_ITEMS, recs[n - 1], "ITEM_ID"), u);
    }
    TF_EQ(eng_assy_path(g_e, l[0], recs, &n), ENG_OK);
    TF_EQ(n, 0);
    TE_FAIL(eng_assy_path(g_e, 999, recs, &n), ENG_ENOTFOUND, "ID", NULL);
    /* A chain broken by hand. */
    raw_put(l[1], ENG_X_ITEMS3, "PARENT_ID", NULL, 999);
    TE_FAIL(eng_assy_path(g_e, l[3], recs, &n), ENG_EIO, "", NULL);
}

/* ---------------------------------------------------------------------
 * Size.
 * --------------------------------------------------------------------- */

static void test_size(void)
{
    char want[TE_TEXTMAX];
    char want2[TE_TEXTMAX];
    u32 s;
    u32 p1;
    u32 p2;
    u32 p3;
    u32 t2;
    u32 t3;
    u32 extra;
    u32 a2;
    u32 c2;
    u32 c3;
    u32 n0;
    u16 i;

    if (!start())
        return;
    s = add("A1");
    p1 = 0;
    for (i = 1; i < ENG_MAXASSY; i++) {
        p3 = add("B1");
        if (p1 == 0)
            p1 = p3;
        TF_EQ(eng_assy_install(g_e, p3, s), ENG_OK);
    }
    p2 = p1 + 1;
    p3 = p1 + 2;
    extra = add("B1");
    TE_FAIL(eng_assy_install(g_e, extra, s), ENG_ECONSTR, "PARENT",
            "too many parts");
    /* Inside a full assembly a part may change its parent. */
    TF_EQ(eng_assy_install(g_e, p3, p2), ENG_OK);
    /* A deleted part still counts. */
    TF_EQ(eng_assy_remove(g_e, p1, "B1"), ENG_OK);
    TF_EQ(eng_item_del(g_e, p3), ENG_OK);
    t2 = add("C1");
    t3 = add("C1");
    TF_EQ(eng_assy_install(g_e, t3, t2), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, t2, s), ENG_ECONSTR, "PARENT",
            "too many parts");
    TF_EQ(eng_assy_install(g_e, extra, s), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, p1, s), ENG_ECONSTR, "PARENT",
            "too many parts");
    TE_FAIL(eng_assy_install(g_e, p1, p2), ENG_ECONSTR, "PARENT",
            "too many parts");
    TF_EQ(eng_item_undel(g_e, p3), ENG_OK);
    TF_STREQ(loc(p3), "A1");
    n0 = te_naud();
    TF_EQ(eng_assy_move(g_e, s, "C1"), ENG_OK);
    TF_EQ(te_naud(), n0 + ENG_MAXASSY);

    /* An installed child with a part moves into another assembly: refused
       while the sum passes ENG_MAXASSY, accepted at exactly that. */
    a2 = add("B1");
    c2 = add("B1");
    c3 = add("A1");
    TF_EQ(eng_assy_install(g_e, c3, c2), ENG_OK);
    TF_EQ(eng_assy_install(g_e, c2, a2), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, c2, s), ENG_ECONSTR, "PARENT",
            "too many parts");
    TF_EQ(eng_assy_remove(g_e, p1 + 3, "A1"), ENG_OK);
    TF_EQ(eng_assy_remove(g_e, p1 + 4, "A1"), ENG_OK);
    n0 = te_naud();
    TF_EQ(eng_assy_install(g_e, c2, s), ENG_OK);
    TF_EQ(par(c2), s);
    TF_STREQ(loc(c2), "C1");
    TF_STREQ(loc(c3), "C1");
    TF_EQ(te_naud(), n0 + 3);
    sprintf(want, "%lu|M|LOC_ID|2|3", (unsigned long)c3);
    TF_STREQ(aud(n0 + 1), want);
    sprintf(want, "%lu|P|PARENT_ID|%lu|%lu", (unsigned long)c2,
            (unsigned long)a2, (unsigned long)s);
    sprintf(want2, "%lu|M|LOC_ID|2|3", (unsigned long)c2);
    TF_CHECK(two(want, want2));
}

/* ---------------------------------------------------------------------
 * Walks.
 * --------------------------------------------------------------------- */

/* Up to per items of walk w appended to g_out as " id:level" (" id" for
   a list walk); *more and the cursor into after. */
static int page(eng_walk *w, int per, int *more, char *after)
{
    char t[24];
    const char *id;
    u32 r;
    u16 lv;
    int i;
    int rc;

    *more = 0;
    rc = ENG_OK;
    for (i = 0; i < per; i++) {
        rc = eng_walk_next(g_e, w, &r, &lv);
        if (rc != ENG_OK || r == 0)
            break;
        id = te_rtext(ENG_T_ITEMS, TE_BUF, "ITEM_ID");
        if (w->kind == ENG_W_BOM)
            sprintf(t, " %s:%u", id, (unsigned)lv);
        else
            sprintf(t, " %s", id);
        if (strlen(g_out) + strlen(t) >= sizeof g_out) {
            printf("page: g_out is full\n");
            TF_CHECK(0);
            break;
        }
        strcat(g_out, t);
    }
    if (rc == ENG_OK)
        rc = eng_walk_more(g_e, w, more);
    eng_walk_cursor(w, after);
    return rc;
}

/* The whole walk of id (0: LIST, top as eng_assy_list) in pages of per,
   into g_out; *pages counts the pages that said more followed. */
static void walk(u32 id, int top, int per, const char *after, int *pages)
{
    eng_walk w;
    char cur[ENG_CURSORMAX];
    int more;
    int rc;

    g_out[0] = '\0';
    *pages = 0;
    strcpy(cur, after);
    more = 1;
    while (more && *pages < MAXPAGES) {
        if (id != 0)
            rc = eng_assy_bom(g_e, &w, id, cur);
        else
            rc = eng_assy_list(g_e, &w, top, cur);
        if (rc == ENG_OK)
            rc = page(&w, per, &more, cur);
        if (!TF_EQ(rc, ENG_OK))
            return;
        if (more)
            (*pages)++;
    }
}

/* The cursor after the first n items of the BOM of id. */
static void bom_after(u32 id, int n, char *cur)
{
    eng_walk w;
    int more;

    g_out[0] = '\0';
    cur[0] = '\0';
    TF_EQ(eng_assy_bom(g_e, &w, id, NULL), ENG_OK);
    TF_EQ(page(&w, n, &more, cur), ENG_OK);
}

static void test_walks(void)
{
    char cur[ENG_CURSORMAX];
    char wcur[ENG_CURSORMAX];
    eng_walk w;
    eng_scan s;
    u32 i;
    u32 r;
    u32 n0;
    int pages;

    if (!start())
        return;
    /* 1 R: 2 P1 (3 Q1, 4 Q2 (5 Z)), 6 P2, 7 P3 deleted; 8 W (9 W1);
       10 G with 11 G1 deleted. */
    for (i = 1; i <= 11; i++)
        add("A1");
    TF_EQ(eng_assy_install(g_e, 2, 1), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 3, 2), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 4, 2), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 5, 4), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 6, 1), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 7, 1), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 9, 8), ENG_OK);
    TF_EQ(eng_assy_install(g_e, 11, 10), ENG_OK);
    TF_EQ(eng_item_del(g_e, 7), ENG_OK);
    TF_EQ(eng_item_del(g_e, 11), ENG_OK);

    /* BOM whole, and two at a time. */
    walk(1, 0, 100, "", &pages);
    TF_STREQ(g_out, " 2:1 3:2 4:2 5:3 6:1");
    TF_EQ(pages, 0);
    walk(1, 0, 2, "", &pages);
    TF_STREQ(g_out, " 2:1 3:2 4:2 5:3 6:1");
    TF_EQ(pages, 2);
    walk(4, 0, 1, "", &pages);
    TF_STREQ(g_out, " 5:1");
    walk(6, 0, 1, "", &pages);
    TF_STREQ(g_out, "");
    TE_FAIL(eng_assy_bom(g_e, &w, 999, NULL), ENG_ENOTFOUND, "ID", NULL);

    /* Cursors outside the BOM. */
    bom_after(8, 1, wcur);
    TE_FAIL(eng_assy_bom(g_e, &w, 1, wcur), ENG_ECURSOR, "", NULL);
    bom_after(1, 2, cur);
    TE_FAIL(eng_assy_bom(g_e, &w, 8, cur), ENG_ECURSOR, "", NULL);
    TE_FAIL(eng_assy_bom(g_e, &w, 3, cur), ENG_ECURSOR, "", NULL);
    TE_FAIL(eng_assy_bom(g_e, &w, 1, "ITEMS3:20:1"), ENG_ECURSOR, "", NULL);
    TE_FAIL(eng_assy_bom(g_e, &w, 1, "ITEMS1:20:1"), ENG_ECURSOR, "", NULL);
    /* The BOM of a part continues a cursor below it. */
    walk(2, 0, 9, cur, &pages);
    TF_STREQ(g_out, " 4:1 5:2");

    /* LIST, whole and one per page. */
    walk(0, 0, 100, "", &pages);
    TF_STREQ(g_out, " 1 2 4 8");
    walk(0, 0, 1, "", &pages);
    TF_STREQ(g_out, " 1 2 4 8");
    walk(0, 1, 100, "", &pages);
    TF_STREQ(g_out, " 1 8");
    walk(0, 1, 1, "", &pages);
    TF_STREQ(g_out, " 1 8");
    /* A list cursor above every key ends the walk. */
    walk(0, 0, 100, "ITEMS3:3020:1", &pages);
    TF_STREQ(g_out, "");

    /* CHILDREN: the live direct parts. */
    g_out[0] = '\0';
    TF_EQ(eng_assy_children(g_e, &s, 1, NULL), ENG_OK);
    while (eng_scan_next(g_e, &s, &r) == ENG_OK && r != 0) {
        strcat(g_out, " ");
        strcat(g_out, te_rtext(ENG_T_ITEMS, TE_BUF, "ITEM_ID"));
    }
    TF_STREQ(g_out, " 2 6");
    TE_FAIL(eng_assy_children(g_e, &s, 999, NULL), ENG_ENOTFOUND, "ID", NULL);

    /* Parts deleted between pages: the cursor's own item, and the part
       the next page would have started with. */
    bom_after(1, 2, cur);
    TF_EQ(eng_item_del(g_e, 3), ENG_OK);
    walk(1, 0, 100, cur, &pages);
    TF_STREQ(g_out, " 4:2 5:3 6:1");
    bom_after(1, 2, cur);
    TF_EQ(eng_item_del(g_e, 5), ENG_OK);
    walk(1, 0, 100, cur, &pages);
    TF_STREQ(g_out, " 6:1");
    /* Q2 now has deleted parts only: LIST passes it over. */
    walk(0, 0, 100, "", &pages);
    TF_STREQ(g_out, " 1 2 8");
    TF_EQ(eng_item_undel(g_e, 3), ENG_OK);
    TF_EQ(eng_item_undel(g_e, 5), ENG_OK);

    /* The repair, after locations changed by hand: the live parts go
       back to their top-level item's location, a deleted part and one
       whose chain is broken stay. */
    raw_put(2, ENG_X_ITEMS2, "LOC_ID", NULL, 2);
    raw_put(5, ENG_X_ITEMS2, "LOC_ID", NULL, 3);
    raw_put(7, ENG_X_ITEMS2, "LOC_ID", NULL, 3);
    raw_put(9, ENG_X_ITEMS2, "LOC_ID", NULL, 2);
    raw_put(9, ENG_X_ITEMS3, "PARENT_ID", NULL, 999);
    eng_set_actor(g_e, "", ENG_SYSTEM);
    eng_set_now(g_e, &g_next);
    n0 = te_naud();
    TF_EQ(eng_assy_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), n0 + 2);
    TF_STREQ(aud(n0 + 1), "2|M|LOC_ID|2|1");
    TF_STREQ(aud(n0 + 2), "5|M|LOC_ID|3|1");
    TF_STREQ(te_aud(te_naud(), "USERNAME"), ENG_SYSTEM);
    TF_STREQ(loc(2), "A1");
    TF_STREQ(loc(5), "A1");
    TF_STREQ(loc(7), "C1");
    TF_STREQ(loc(9), "B1");
    TF_STREQ(fld(2, "UPDATED"), "20261009");
    TF_STREQ(fld(6, "UPDATED"), "20261008");
    n0 = te_naud();
    TF_EQ(eng_assy_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), n0);
}

/* ---------------------------------------------------------------------
 * The read bound of LIST, and data damaged by another program.
 * --------------------------------------------------------------------- */

/* Items listed in g_out. */
static int words(void)
{
    const char *c;
    int n;

    n = 0;
    for (c = g_out; *c != '\0'; c++)
        if (*c == ' ')
            n++;
    return n;
}

static void test_listmax(void)
{
    char cur[ENG_CURSORMAX];
    char want[ENG_CURSORMAX];
    u8 k[24];
    eng_walk w;
    u32 t;
    u16 i;
    int more;
    int pages;

    if (!start())
        return;
    for (i = 0; i < NASSY; i++) {
        t = add("A1");
        add("A1");
        add("A1");
        TF_EQ(eng_assy_install(g_e, t + 1, t), ENG_OK);
        TF_EQ(eng_assy_install(g_e, t + 2, t), ENG_OK);
        TF_EQ(eng_item_del(g_e, t + 1), ENG_OK);
    }
    g_out[0] = '\0';
    TF_EQ(eng_assy_list(g_e, &w, 0, NULL), ENG_OK);
    TF_EQ(page(&w, NASSY, &more, cur), ENG_OK);
    TF_EQ(w.reads, ENG_MAXREADS - 1);
    TF_EQ(more, 1);
    t = (u32)(ENG_MAXREADS - 2) / 3 * 3 + 1;
    sprintf((char *)k, "%8lu%8lu", (unsigned long)t, (unsigned long)t + 1);
    eng_cursor_text(ENG_X_ITEMS3, k, t + 1, want);
    TF_STREQ(cur, want);
    TF_EQ(words(), (ENG_MAXREADS - 2) / 3);
    walk(0, 0, NASSY, cur, &pages);
    sprintf(want, " %lu", (unsigned long)t);
    TF_EQ(strncmp(g_out, want, strlen(want)), 0);
    TF_EQ(words(), NASSY - (ENG_MAXREADS - 2) / 3);
}

static void test_damaged(void)
{
    eng_walk w;
    u32 recs[ENG_MAXDEPTH];
    u32 a;
    u32 b;
    u32 top;
    u32 q;
    u32 r;
    u32 s2;
    u32 n0;
    u16 n;
    u16 lv;
    int pages;

    if (!start())
        return;
    a = add("A1");
    b = add("A1");
    top = add("B1");
    TF_EQ(eng_assy_install(g_e, a, b), ENG_OK);
    /* A cycle: b's parent set to a by another program. */
    raw_put(b, ENG_X_ITEMS3, "PARENT_ID", NULL, (i32)a);
    TF_EQ(eng_assy_bom(g_e, &w, a, NULL), ENG_OK);
    TF_EQ(eng_walk_next(g_e, &w, &r, &lv), ENG_OK);
    TF_EQ(te_rnum(ENG_T_ITEMS, r, "ITEM_ID"), b);
    TF_EQ(eng_walk_next(g_e, &w, &r, &lv), ENG_EIO);
    TF_EQ(r, 0);
    TF_EQ(eng_walk_next(g_e, &w, &r, &lv), ENG_EIO);
    TF_EQ(eng_walk_more(g_e, &w, &pages), ENG_EIO);
    eng_clear_error(g_e);
    TE_FAIL(eng_assy_path(g_e, a, recs, &n), ENG_EIO, "", NULL);
    TE_FAIL(eng_assy_install(g_e, top, a), ENG_ECONSTR, "PARENT",
            "parts nested more than 16 levels deep");
    TE_FAIL(eng_assy_install(g_e, a, top), ENG_EIO, "CHILD", NULL);
    TE_FAIL(eng_assy_move(g_e, a, "C1"), ENG_ECONSTR, "ID", NULL);
    TE_FAIL(eng_assy_remove(g_e, a, "C1"), ENG_EIO, "", NULL);

    /* A negative PARENT_ID. */
    q = add("C1");
    s2 = add("A1");
    r = add("C1");
    TF_EQ(eng_assy_install(g_e, r, s2), ENG_OK);
    raw_put(q, ENG_X_ITEMS3, "PARENT_ID", NULL, -5);
    raw_put(q, ENG_X_ITEMS2, "LOC_ID", NULL, 2);
    walk(0, 0, 100, "", &pages);
    TF_STREQ(g_out, " 1 2 5");
    walk(0, 0, 1, "", &pages);
    TF_STREQ(g_out, " 1 2 5");
    walk(0, 1, 100, "", &pages);
    TF_STREQ(g_out, " 5");
    TE_FAIL(eng_assy_path(g_e, q, recs, &n), ENG_EIO, "", NULL);
    TE_FAIL(eng_assy_install(g_e, q, s2), ENG_EIO, "CHILD", NULL);
    TE_FAIL(eng_assy_move(g_e, q, "C1"), ENG_ECONSTR, "ID",
            "installed in an assembly");

    /* The repair passes both by and changes nothing. */
    n0 = te_naud();
    TF_EQ(eng_assy_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), n0);
    TF_STREQ(loc(q), "B1");
    TF_STREQ(loc(a), "A1");
    /* REMOVE makes the item with the negative parent top-level again. */
    TF_EQ(eng_assy_remove(g_e, q, "C1"), ENG_OK);
    TF_EQ(par(q), 0);

    /* Top-level locations written by another program: a negative one
       breaks its group (heal leaves its part, INSTALL refuses), another
       is mirrored once. */
    a = add("A1");
    b = add("A1");
    TF_EQ(eng_assy_install(g_e, b, a), ENG_OK);
    raw_put(a, ENG_X_ITEMS2, "LOC_ID", NULL, -1);
    raw_put(b, ENG_X_ITEMS2, "LOC_ID", NULL, 2);
    top = add("A1");
    q = add("A1");
    r = add("C1");
    TF_EQ(eng_assy_install(g_e, q, top), ENG_OK);
    TF_EQ(eng_assy_install(g_e, r, top), ENG_OK);
    raw_put(top, ENG_X_ITEMS2, "LOC_ID", NULL, 3);
    raw_put(r, ENG_X_ITEMS2, "LOC_ID", NULL, 3);
    n0 = te_naud();
    TF_EQ(eng_assy_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), n0 + 1);
    TF_STREQ(loc(b), "B1");
    TF_STREQ(loc(q), "C1");
    TF_STREQ(loc(r), "C1");
    n0 = te_naud();
    TF_EQ(eng_assy_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), n0);
    q = add("B1");
    TE_FAIL(eng_assy_install(g_e, q, b), ENG_EIO, "PARENT", NULL);
}

/* ---------------------------------------------------------------------
 * The repair at open after an interrupted change.
 * --------------------------------------------------------------------- */

/* An ASSY MOVE and an ASSY REMOVE cut off after the commits of their
   first parts, and the marker naming ITEMS they leave: the next open
   puts every part back at its top-level item's location. */
static void test_reopen(void)
{
    char key[ENG_MAXKEYLEN];
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 t;
    u32 r;
    u32 s;
    u32 n0;
    u32 k;
    int exact;

    if (!start())
        return;
    a = add("A1");
    b = add("A1");
    c = add("A1");
    d = add("A1");
    t = add("B1");
    r = add("A1");
    s = add("A1");
    TF_EQ(eng_assy_install(g_e, b, a), ENG_OK);
    TF_EQ(eng_assy_install(g_e, c, b), ENG_OK);
    TF_EQ(eng_assy_install(g_e, d, a), ENG_OK);
    TF_EQ(eng_assy_install(g_e, s, r), ENG_OK);
    TF_EQ(eng_assy_install(g_e, r, t), ENG_OK);
    /* MOVE a LOC=C1 after the commits of b and c; REMOVE r LOC=C1 after
       that of s.  The records are written, their ITEMS2 entries not. */
    raw_put(b, -1, "LOC_ID", NULL, 3);
    raw_put(c, -1, "LOC_ID", NULL, 3);
    raw_put(s, -1, "LOC_ID", NULL, 3);
    n0 = te_naud();
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    TF_EQ(te_marker_put("ITEMS"), 0);
    if (!TF_EQ(te_open(&g_e, &g_next), ENG_OK))
        return;
    TF_CHECK(!tfs_exists(tfs_path(ENG_EDITMARK, "")));
    TF_EQ(eng_marker_kept(g_e), 0);
    TF_STREQ(loc(a), "A1");
    TF_STREQ(loc(b), "A1");
    TF_STREQ(loc(c), "A1");
    TF_STREQ(loc(d), "A1");
    TF_STREQ(loc(r), "B1");
    TF_STREQ(loc(s), "B1");
    TF_EQ(par(r), t);
    TF_STREQ(fld(b, "UPDATED"), "20261009");
    TF_STREQ(fld(s, "UPDATED"), "20261009");
    TF_STREQ(fld(d, "UPDATED"), "20261008");
    /* In ITEMS3 order: the parts of a, of b, then of r. */
    TF_EQ(te_naud(), n0 + 3);
    TF_STREQ(aud(n0 + 1), "2|M|LOC_ID|3|1");
    TF_STREQ(aud(n0 + 2), "3|M|LOC_ID|3|1");
    TF_STREQ(aud(n0 + 3), "7|M|LOC_ID|3|2");
    for (k = n0 + 1; k <= n0 + 3; k++)
        TF_STREQ(te_aud(k, "STATION USERNAME"), "SYSTEM|SYSTEM");
    /* ITEMS2 was rebuilt: b's entry names A1 again. */
    sprintf(key, "%8lu%8lu", 1UL, (unsigned long)b);
    TF_EQ(ndx_seek(eng_ndx(g_e, ENG_X_ITEMS2), (const u8 *)key,
                   16, &r, &exact), DBF_OK);
    TF_EQ(exact, 1);
}

int main(int argc, char **argv)
{
    tf_begin("T_ASSY");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    test_changes();
    test_depth();
    test_size();
    test_walks();
    test_listmax();
    test_damaged();
    test_reopen();
    stop();
    return tf_done();
}

/*
 * T_CURSO2.C - the second half of the T_CURSOR test (see T_CURSOR.C):
 * last pages that hold no record, cursors before the first entry and
 * entries whose record cannot be read, a damaged index page, walk
 * cursors, and IDs found by binary search.  It is a file of its own only
 * to stay within the compiler memory of Watcom C 6.5; T_CURSOR.H holds
 * what both share.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#define TF_PART
#define TF_FILE "T_CURSO2"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_CURSOR.H"

/* ---------------------------------------------------------------------
 * Empty last pages and unreadable records.
 * --------------------------------------------------------------------- */

/* Keeps items 1..5. */
static int first_five(eng *e, eng_scan *s)
{
    char v[ENG_MAXVAL];

    eng_rec_text(e, s->table, 0, v);
    return atol(v) <= 5;
}

void test_empty_last(void)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char c2[ENG_CURSORMAX];
    u16 ngot;
    u32 i;

    if (!fresh())
        return;
    for (i = 1; i <= 10; i++)
        item_add(i, "33", "Tandy", "1000", "");
    ngot = 0;
    TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0, first_five, NULL,
                  5, &ngot, cur), ENG_OK);
    TF_EQ(ngot, 5);
    TF_STREQ(cur, "ITEMS1:2020202020202035:5");
    strcpy(c2, cur);
    TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0, first_five, c2,
                  5, &ngot, cur), ENG_OK);
    TF_EQ(ngot, 5);
    TF_EQ(s.reads, 5);
    TF_STREQ(cur, "");

    /* A prefix scan whose remaining entries all fail the filter. */
    {
        u8 pre[ENG_IDLEN];

        memcpy(pre, "      33", ENG_IDLEN);
        ngot = 0;
        TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
                      first_five, NULL, 5, &ngot, cur), ENG_OK);
        TF_EQ(ngot, 5);
        TF_CHECK(cur[0] != '\0');
        strcpy(c2, cur);
        TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
                      first_five, c2, 5, &ngot, cur), ENG_OK);
        TF_EQ(ngot, 5);
        TF_STREQ(cur, "");
    }
    /* After the last entry of the range there is no C line. */
    ngot = 0;
    TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0, NULL, NULL, 10,
                  &ngot, cur), ENG_OK);
    TF_EQ(ngot, 10);
    TF_STREQ(cur, "");
}

/* A cursor that sorts before the first entry returns every entry; an
   index entry whose record cannot be read fails the scan without moving
   its cursor past the entry before it. */
void test_before_and_bad_record(void)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    u8 key[ENG_MAXKEYLEN];
    u16 ngot;
    u16 i;
    u32 r;

    if (!fresh())
        return;
    user_add(1, "BOB");
    user_add(2, "CAROL");
    user_add(3, "BOB");
    ngot = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL,
                  "USERS1:41:FFFFFFFF", 25, &ngot, cur), ENG_OK);
    TF_EQ(ngot, 3);
    TF_EQ(g_got[0], 1);
    TF_EQ(g_got[1], 3);
    TF_EQ(g_got[2], 2);
    TF_STREQ(cur, "");
    ngot = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL,
                  "USERS1::1", 2, &ngot, cur), ENG_OK);
    TF_EQ(ngot, 2);
    TF_EQ(g_got[0], 1);
    TF_EQ(g_got[1], 3);
    TF_STREQ(cur, "USERS1:424F42:3");

    /* An entry for record 9 of three, between BOB and CAROL. */
    memset(key, ' ', sizeof key);
    memcpy(key, "BOBBY", 5);
    TF_EQ(ndx_insert(eng_ndx(g_e, ENG_X_USERS1), key, 9), DBF_OK);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL),
          ENG_OK);
    for (i = 0; i < 2; i++)
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 3);
    TF_CHECK(eng_scan_next(g_e, &s, &r) >= ENG_EIO);
    TF_EQ(r, 0);
    eng_scan_cursor(&s, cur);
    TF_STREQ(cur, "USERS1:424F42:3");
    TF_EQ(s.reads, 2);
    TF_CHECK(eng_scan_next(g_e, &s, &r) >= ENG_EIO);
    /* Repeating the request meets the same record again. */
    ngot = 0;
    TF_CHECK(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL,
                     "USERS1:424F42:3", 25, &ngot, cur) >= ENG_EIO);
    TF_EQ(ngot, 0);
    TF_EQ(ndx_delete(eng_ndx(g_e, ENG_X_USERS1), key, 9), DBF_OK);
}

/* ---------------------------------------------------------------------
 * A failing index read.
 * --------------------------------------------------------------------- */

/* USERS1 with a root over two leaves, the second one damaged: stepping
   from the last entry of the first leaf into the second fails, and both
   eng_scan_more and eng_scan_next report it instead of an end. */
void test_damaged(void)
{
    static u8 page[NDX_PAGE];
    eng_scan s;
    char name[ENG_USERLEN + 1];
    char cur[ENG_CURSORMAX];
    char why[TFS_WHYSZ];
    const char *path;
    u32 root;
    u32 leaf2;
    u16 n1;
    u16 i;
    u32 r;
    int more;

    if (!fresh())
        return;
    for (i = 1; i <= 40; i++) {
        sprintf(name, "U%03u", i);
        user_add(i, name);
    }
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    path = tfs_path("USERS1", ".NDX");
    TF_EQ(tfs_peek(path, 0, page, NDX_PAGE), 0);
    root = get_le32(page);
    TF_EQ(tfs_peek(path, root * NDX_PAGE, page, NDX_PAGE), 0);
    TF_EQ(get_le16(page), 1);
    leaf2 = get_le32(page + 4 + 16);
    TF_EQ(tfs_peek(path, get_le32(page + 4) * NDX_PAGE, page, NDX_PAGE),
          0);
    n1 = get_le16(page);
    TF_CHECK(n1 > 0 && n1 < 40);
    TF_EQ(tfs_peek(path, leaf2 * NDX_PAGE, page, NDX_PAGE), 0);
    put_le16(page, 0xFFFFu);
    TF_EQ(tfs_poke(path, leaf2 * NDX_PAGE, page, NDX_PAGE), 0);
    if (!TF_EQ(eng_open_files(&g_e, tfs_dir(), &g_now, why,
                              (u16)sizeof why), ENG_OK)) {
        g_e = NULL;
        return;
    }

    TF_EQ(eng_scan_open(g_e, &s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL),
          ENG_OK);
    for (i = 0; i < n1; i++)
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, n1);
    more = 1;
    TF_EQ(eng_scan_more(g_e, &s, &more), ENG_EINDEX);
    TF_EQ(more, 0);
    strcpy(why, eng_errmsg(g_e));
    TF_CHECK(why[0] != '\0');
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_EINDEX);
    TF_EQ(r, 0);
    /* A failed scan keeps reporting the original failure's text. */
    TF_STREQ(eng_errmsg(g_e), why);
    eng_clear_error(g_e);
    TF_EQ(eng_scan_more(g_e, &s, &more), ENG_EINDEX);
    TF_CHECK(eng_errmsg(g_e)[0] != '\0');
    /* A scan never opened is a caller's error. */
    memset(&s, 0, sizeof s);
    TF_EQ(eng_scan_more(g_e, &s, &more), ENG_EINTERNAL);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_EINTERNAL);
    /* Paging reports the failure rather than a last page. */
    n1 = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL, NULL, 25,
                  &n1, cur), ENG_EINDEX);
}

/* ---------------------------------------------------------------------
 * Walk cursors and IDs by binary search.
 * --------------------------------------------------------------------- */

/* Walk cursors written and read back; the prefix rule is the walk's.
   Continues test_bound on the same files. */
void test_walk_cursor(void)
{
    static const char *const bad[] = {
        "", "ITEMS1::7", "ITEMS3", "ITEMS3:20:0", "ITEMS3:2:7",
        "ITEMS3:2020202020202033202020202020203920:7", "ITEMS3::123456789",
        "ITEMS::7", NULL
    };
    u8 key[ENG_ITEMS3LEN];
    u8 got[ENG_ITEMS3LEN];
    char out[ENG_CURSORMAX];
    char want[ENG_CURSORMAX];
    u32 r;
    u16 i;

    memcpy(key, "       3       9", ENG_ITEMS3LEN);
    eng_cursor_text(ENG_X_ITEMS3, key, 7, out);
    make_cursor("ITEMS3", key, ENG_ITEMS3LEN, 7, want);
    TF_STREQ(out, want);
    TF_EQ(eng_cursor_parse(g_e, ENG_X_ITEMS3, out, got, &r), ENG_OK);
    TF_CHECK(memcmp(got, key, ENG_ITEMS3LEN) == 0);
    TF_EQ(r, 7);
    memcpy(key, "      12        ", ENG_ITEMS3LEN);
    eng_cursor_text(ENG_X_ITEMS3, key, 0x1234, out);
    TF_STREQ(out, "ITEMS3:2020202020203132:1234");
    TF_EQ(eng_cursor_parse(g_e, ENG_X_ITEMS3,
                           "items3:2020202020203132:0001234", got, &r),
          ENG_OK);
    TF_CHECK(memcmp(got, key, ENG_ITEMS3LEN) == 0);
    TF_EQ(r, 0x1234);
    eng_cursor_text(ENG_X_ITEMS3, key, 0, out);
    TF_STREQ(out, "");
    for (i = 0; bad[i] != NULL; i++)
        if (!TF_EQ(eng_cursor_parse(g_e, ENG_X_ITEMS3, bad[i], got, &r),
                   ENG_ECURSOR))
            printf("  cursor \"%s\"\n", bad[i]);
    TF_EQ(eng_cursor_parse(g_e, ENG_X_ITEMS3, NULL, got, &r), ENG_ECURSOR);
    TF_EQ(eng_cursor_parse(g_e, ENG_NINDEX, "ITEMS3::1", got, &r),
          ENG_EINTERNAL);
}

/* Append MAINT records 1..n whose MAINT_ID is 3 times the record number,
   blank for those in [blank0, blank1]. */
static void maint_ids(u32 n, u32 blank0, u32 blank1)
{
    char a[12];
    const char *kv[3];
    u32 i;

    for (i = 1; i <= n; i++) {
        sprintf(a, "%lu", (unsigned long)(i * 3));
        kv[0] = i >= blank0 && i <= blank1 ? "DONE_BY" : "MAINT_ID";
        kv[1] = a;
        kv[2] = NULL;
        raw_add(ENG_T_MAINT, kv);
    }
}

static void id_is(u32 id, u32 want)
{
    u32 r;

    TF_EQ(eng_id_find(g_e, ENG_T_MAINT, id, &r), ENG_OK);
    if (!TF_EQ(r, want))
        printf("  ID %lu\n", (unsigned long)id);
}

void test_id_find(void)
{
    u32 r;
    u32 i;

    if (!fresh())
        return;
    id_is(3, 0);
    maint_ids(1, 0, 0);
    id_is(3, 1);
    id_is(4, 0);
    if (!fresh())
        return;
    maint_ids(70, 10, 12);
    raw_delete(ENG_T_MAINT, 40);
    for (i = 1; i <= 70; i++)
        id_is(i * 3, (i >= 10 && i <= 12) || i == 40 ? 0 : i);
    id_is(0, 0);
    id_is(1, 0);
    id_is(100, 0);
    id_is(211, 0);
    id_is(99999999UL, 0);
    TF_EQ(eng_id_find(g_e, ENG_T_CATS, 1, &r), ENG_EINTERNAL);
    /* More unreadable IDs in a row than ENG_IDSTEP below a probe. */
    if (!fresh())
        return;
    maint_ids(40, 1, 40);
    TF_EQ(eng_id_find(g_e, ENG_T_MAINT, 30, &r), ENG_EIO);
    TF_EQ(r, 0);
}

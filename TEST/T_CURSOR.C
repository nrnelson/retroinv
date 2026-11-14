/*
 * T_CURSOR.C - unit test for paging cursors and bounded scans (CURSOR.C).
 *
 * usage: T_CURSOR [dir]
 *
 * Works on a fresh data directory in dir (a prefix such as "OBJ/HOST/CDATA/"
 * or "D:\"; a missing separator is added), removing every engine file
 * first and at the end.  Records are written straight into the tables and
 * their indexes, without audit rows, so the scans see exactly the entries
 * the test made.  Checks:
 *   - paging every index and the record order with several page sizes
 *     gives the order of the index, and each cursor is the exact text of
 *     the last entry examined, which reopens the scan at the same place;
 *   - every malformed cursor, a cursor of another index or table, and a
 *     cursor outside the scan's prefix give ENG_ECURSOR;
 *   - records inserted, deleted and re-keyed between pages neither repeat
 *     nor hide the others;
 *   - a run of more than ENG_MAXREADS equal keys pages through to its end,
 *     each request moving forward, also when a filter rejects them all;
 *   - a cursor anywhere in that run, or before or past it, reopens the
 *     scan at the next entry within OPENREADS page reads, the page pool
 *     warm or emptied;
 *   - the read limit: positioning counts nothing, every entry examined
 *     counts one, and a request stops before examining an entry once
 *     ENG_MAXREADS have been counted;
 *   - a last page that holds no record after a page that ended with more
 *     entries left;
 *   - a damaged index page met while stepping is reported as ENG_EINDEX
 *     by eng_scan_more and eng_scan_next, not taken as the end;
 *   - reverse record order, past deleted records and from cursors;
 *   - a bound ends a scan before the first entry not below it, with no
 *     entries left after it, and pages across it with cursors;
 *   - walk cursors written and parsed, and malformed ones refused;
 *   - IDs found by binary search, past deleted records and blank IDs,
 *     and too many blank IDs in a row reported as damage.
 * Empty last pages, unreadable records, the damaged index page, walk
 * cursors and IDs are tested in T_CURSO2.C; T_CURSOR.H declares what the
 * two files share.  Prints each failure and a summary line; exit status 0
 * when every check passes, else 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "T_CURSOR.H"

#define NRUN        230         /* equal USERS1 keys: above ENG_MAXREADS */
#define OPENREADS   6           /* page reads of an open in the run: the
                                   descent of the two-level USERS1 with a
                                   binary search over the run's leaves,
                                   which stepping through would read one
                                   by one                               */
#define COLDEVERY   46          /* opens in the run per engine reopen   */

eng *g_e;

static u16 g_nread;             /* reads count_read has seen            */

static u32 g_want[MAXLIST];     /* expected recnos                      */
u32 g_got[MAXLIST];

const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

/* Open the engine on the scratch directory, closing it first (which
   empties the index page pool) and removing its files when wipe is 1;
   0 (the check failed) when it cannot be opened. */
static int reopen(int wipe)
{
    char why[TFS_WHYSZ];
    int rc;

    if (g_e != NULL)
        eng_close(g_e);
    g_e = NULL;
    if (wipe)
        te_remove(tfs_dir(), NULL);
    rc = eng_open_files(&g_e, tfs_dir(), &g_now, why, (u16)sizeof why);
    if (!TF_EQ(rc, ENG_OK)) {
        printf("open: %s\n", why);
        g_e = NULL;
        return 0;
    }
    return 1;
}

int fresh(void)
{
    return reopen(1);
}

/* Insert the keys of the record in table t's buffer, numbered recno, into
   every index of the table. */
static void index_rec(int t, u32 recno)
{
    const eng_tdef *td;
    u8 key[ENG_MAXKEYLEN];
    int x;

    td = eng_table_def(t);
    for (x = td->firstx; x >= 0 && x < td->firstx + td->nindex; x++) {
        TF_EQ(kx_eval(eng_kx(g_e, x), eng_dbf(g_e, t), key), DBF_OK);
        TF_EQ(ndx_insert(eng_ndx(g_e, x), key, recno), DBF_OK);
    }
}

u32 raw_add(int t, const char *const *kv)
{
    dbf *d;
    int f;

    d = eng_dbf(g_e, t);
    dbf_blank(d);
    for (; kv[0] != NULL; kv += 2) {
        f = eng_field_num(g_e, t, kv[0]);
        if (!TF_CHECK(f >= 0))
            continue;
        if (eng_field(g_e, t, (u16)f)->type == 'N')
            TF_EQ(dbf_put_num(d, (u16)f, (i32)atol(kv[1])), DBF_OK);
        else
            TF_EQ(dbf_put_str(d, (u16)f, kv[1]), DBF_OK);
    }
    TF_EQ(dbf_append(d), DBF_OK);
    index_rec(t, dbf_recno(d));
    return dbf_recno(d);
}

u32 item_add(u32 id, const char *loc, const char *mfr,
                    const char *model, const char *serial)
{
    char ids[12];
    const char *kv[13];

    sprintf(ids, "%lu", (unsigned long)id);
    kv[0] = "ITEM_ID";  kv[1] = ids;
    kv[2] = "CATEGORY"; kv[3] = "SYS";
    kv[4] = "LOC_ID";   kv[5] = loc;
    kv[6] = "MFR";      kv[7] = mfr;
    kv[8] = "MODEL";    kv[9] = model;
    kv[10] = "SERIALNO"; kv[11] = serial;
    kv[12] = NULL;
    return raw_add(ENG_T_ITEMS, kv);
}

void raw_delete(int t, u32 recno)
{
    dbf *d;

    d = eng_dbf(g_e, t);
    TF_EQ(dbf_read(d, recno), DBF_OK);
    dbf_set_deleted(d, 1);
    TF_EQ(dbf_write(d), DBF_OK);
}

/* Give item recno a new MFR, moving its ITEMS4 entry. */
static void raw_set_mfr(u32 recno, const char *mfr)
{
    u8 key[ENG_MAXKEYLEN];
    dbf *d;
    kx *k;
    ndx *x;

    d = eng_dbf(g_e, ENG_T_ITEMS);
    k = eng_kx(g_e, ENG_X_ITEMS4);
    x = eng_ndx(g_e, ENG_X_ITEMS4);
    TF_EQ(dbf_read(d, recno), DBF_OK);
    TF_EQ(kx_eval(k, d, key), DBF_OK);
    TF_EQ(ndx_delete(x, key, recno), DBF_OK);
    TF_EQ(dbf_put_str(d, (u16)eng_field_num(g_e, ENG_T_ITEMS, "MFR"), mfr),
          DBF_OK);
    TF_EQ(dbf_write(d), DBF_OK);
    TF_EQ(kx_eval(k, d, key), DBF_OK);
    TF_EQ(ndx_insert(x, key, recno), DBF_OK);
}

/* The live records of index x within the prefix, in index order, and
   strictly after (akey, arec) when arec is not 0, into g_want. */
static u16 walk(int x, const u8 *prefix, u16 plen, const u8 *akey,
                u32 arec)
{
    u8 key[ENG_MAXKEYLEN];
    ndx *nx;
    dbf *d;
    u32 r;
    u16 n;
    u16 kl;
    int c;

    nx = eng_ndx(g_e, x);
    d = eng_dbf(g_e, eng_index_def(x)->table);
    kl = ndx_keylen(nx);
    n = 0;
    r = 0;
    TF_EQ(ndx_first(nx, &r), DBF_OK);
    for (; r != 0; ndx_next(nx, &r)) {
        TF_EQ(ndx_key(nx, key), DBF_OK);
        if (plen > 0 && memcmp(key, prefix, plen) != 0)
            continue;
        if (arec != 0) {
            c = memcmp(key, akey, kl);
            if (c < 0 || (c == 0 && r <= arec))
                continue;
        }
        TF_EQ(dbf_read(d, r), DBF_OK);
        if (dbf_is_deleted(d) || n >= MAXLIST)
            continue;
        g_want[n++] = r;
    }
    return n;
}

/* eng_scan_more succeeds and gives want. */
static void more_is(eng_scan *s, int want)
{
    int more;

    TF_EQ(eng_scan_more(g_e, s, &more), ENG_OK);
    TF_EQ(more, want);
}

int request(eng_scan *s, int t, int ix, const u8 *prefix, u16 plen,
            eng_match_fn match, const char *after, u16 page, u16 *ngot,
            char *cur)
{
    u32 r;
    u16 i;
    int more;
    int rc;

    cur[0] = '\0';
    rc = eng_scan_open(g_e, s, t, ix, prefix, plen, after);
    if (rc != ENG_OK)
        return rc;
    s->match = match;
    for (i = 0; i < page; i++) {
        rc = eng_scan_next(g_e, s, &r);
        if (rc != ENG_OK)
            return rc;
        if (r == 0)
            break;
        if (*ngot < MAXLIST)
            g_got[(*ngot)++] = r;
    }
    rc = eng_scan_more(g_e, s, &more);
    if (rc != ENG_OK)
        return rc;
    if (more)
        eng_scan_cursor(s, cur);
    return ENG_OK;
}

/* Page through a whole scan; returns the number of requests, and the
   records in g_got (*ngot).  Every request must move the cursor. */
static u16 page_all(int t, int ix, const u8 *prefix, u16 plen,
                    eng_match_fn match, u16 page, u16 *ngot)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char prev[ENG_CURSORMAX];
    u16 req;

    *ngot = 0;
    cur[0] = '\0';
    for (req = 0; req < 400; req++) {
        strcpy(prev, cur);
        if (!TF_EQ(request(&s, t, ix, prefix, plen, match, prev, page,
                           ngot, cur), ENG_OK))
            break;
        if (cur[0] == '\0')
            return (u16)(req + 1);
        if (!TF_CHECK(strcmp(cur, prev) != 0))
            break;
    }
    TF_CHECK(0);
    return req;
}

static int same_list(u16 ngot, u16 nwant)
{
    u16 i;

    if (!TF_EQ(ngot, nwant))
        return 0;
    for (i = 0; i < ngot; i++)
        if (!TF_EQ(g_got[i], g_want[i]))
            return 0;
    return 1;
}

void make_cursor(const char *name, const u8 *key, u16 kl, u32 recno,
                 char *out)
{
    u16 n;
    u16 i;

    strcpy(out, name);
    strcat(out, ":");
    n = kl;
    while (n > 0 && key[n - 1] == ' ')
        n--;
    for (i = 0; i < n; i++)
        sprintf(out + strlen(out), "%02X", key[i]);
    sprintf(out + strlen(out), ":%lX", (unsigned long)recno);
}

/* ---------------------------------------------------------------------
 * Round trips.
 * --------------------------------------------------------------------- */

static const char *const mfrs[] = { "IBM", "Compaq", "ibm", "Zenith",
                                    "AST", "Compaq", "" };

static void fill_tables(void)
{
    char a[12];
    char b[12];
    char c[12];
    const char *kv[13];
    u32 i;

    for (i = 1; i <= 21; i++) {
        sprintf(a, "%lu", (unsigned long)((i % 3 + 1) * 10 + i % 4));
        sprintf(b, "M%lu", (unsigned long)(i % 5));
        sprintf(c, i % 4 == 0 ? "" : "SN%lu", (unsigned long)(i % 6));
        item_add(i * 3, i % 7 == 0 ? "" : a, mfrs[(int)(i % 7)], b, c);
    }
    for (i = 1; i <= 9; i++) {
        sprintf(a, "%lu", (unsigned long)i);
        sprintf(b, "%lu", (unsigned long)(i % 3));
        sprintf(c, "L%lu", (unsigned long)(10 - i));
        kv[0] = "LOC_ID"; kv[1] = a;
        kv[2] = "PARENT_ID"; kv[3] = b;
        kv[4] = "LOC_NAME"; kv[5] = c;
        kv[6] = "LOC_CODE"; kv[7] = c;
        kv[8] = "LTYPE"; kv[9] = "BIN";
        kv[10] = NULL;
        raw_add(ENG_T_LOCS, kv);
        sprintf(b, "%lu", (unsigned long)(i % 4 + 1));
        sprintf(c, "2026%02lu%02lu", (unsigned long)(i % 12 + 1),
                (unsigned long)(i + 3));
        kv[0] = "MAINT_ID"; kv[1] = a;
        kv[2] = "ITEM_ID"; kv[3] = b;
        kv[4] = "MDATE"; kv[5] = i % 3 == 0 ? "" : c;
        kv[6] = NULL;
        raw_add(ENG_T_MAINT, kv);
        kv[0] = "LOAN_ID"; kv[1] = a;
        kv[2] = "ITEM_ID"; kv[3] = b;
        kv[4] = "DUE_DATE"; kv[5] = c;
        kv[6] = NULL;
        raw_add(ENG_T_LOANS, kv);
        sprintf(c, "U%lu", (unsigned long)(i * 7 % 10));
        kv[0] = "USER_ID"; kv[1] = a;
        kv[2] = "USERNAME"; kv[3] = c;
        kv[4] = NULL;
        raw_add(ENG_T_USERS, kv);
        kv[0] = "AUD_ID"; kv[1] = a;
        kv[2] = "TBL"; kv[3] = i % 2 ? "ITEMS" : "LOCS";
        kv[4] = "REC_ID"; kv[5] = b;
        kv[6] = NULL;
        raw_add(ENG_T_AUDIT, kv);
    }
    raw_delete(ENG_T_ITEMS, 5);
    raw_delete(ENG_T_LOCS, 2);
}

static void test_roundtrip(void)
{
    static const u16 pages[] = { 1, 3, 5, 25 };
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char want[ENG_CURSORMAX];
    char again[ENG_CURSORMAX];
    u16 ngot;
    u16 nwant;
    u16 p;
    u32 r;
    int x;

    if (!fresh())
        return;
    fill_tables();
    for (x = 0; x < ENG_NINDEX; x++) {
        nwant = walk(x, NULL, 0, NULL, 0);
        TF_CHECK(nwant > 0);
        for (p = 0; p < sizeof pages / sizeof pages[0]; p++) {
            page_all(eng_index_def(x)->table, x, NULL, 0, NULL, pages[p],
                     &ngot);
            if (!same_list(ngot, nwant))
                printf("  index %s, page %u\n", eng_index_def(x)->name,
                       pages[p]);
        }
        /* The cursor after the first two entries is their exact text, and
           reopening on it gives it back unchanged. */
        TF_EQ(eng_scan_open(g_e, &s, eng_index_def(x)->table, x, NULL, 0,
                            NULL), ENG_OK);
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
        TF_EQ(r, g_want[1]);
        eng_scan_cursor(&s, cur);
        make_cursor(eng_index_def(x)->name, s.key,
                    eng_index_def(x)->keylen, r, want);
        TF_STREQ(cur, want);
        TF_EQ(eng_scan_open(g_e, &s, eng_index_def(x)->table, x, NULL, 0,
                            cur), ENG_OK);
        TF_EQ(s.reads, 0);
        eng_scan_cursor(&s, again);
        TF_STREQ(again, cur);
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
        TF_EQ(r, g_want[2]);
        TF_EQ(s.reads, 1);
    }

    /* Record order: the live records 1..n, cursor TABLE::recno. */
    for (p = 0; p < sizeof pages / sizeof pages[0]; p++) {
        page_all(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, NULL, pages[p],
                 &ngot);
        TF_EQ(ngot, 20);
        TF_EQ(g_got[3], 4);
        TF_EQ(g_got[4], 6);
    }
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0,
                        "items::0B"), ENG_OK);
    eng_scan_cursor(&s, cur);
    TF_STREQ(cur, "ITEMS::B");
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 12);
    more_is(&s, 1);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0,
                        "ITEMS::15"), ENG_OK);
    more_is(&s, 0);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0,
                        "ITEMS::FFFFFFFF"), ENG_OK);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);

    /* A prefix scan of ITEMS2 gives exactly the location's items. */
    {
        u8 pre[ENG_IDLEN];

        memcpy(pre, "      21", ENG_IDLEN);
        nwant = walk(ENG_X_ITEMS2, pre, ENG_IDLEN, NULL, 0);
        TF_CHECK(nwant >= 2);
        page_all(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN, NULL, 1, &ngot);
        same_list(ngot, nwant);
        memcpy(pre, "      99", ENG_IDLEN);
        page_all(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN, NULL, 3, &ngot);
        TF_EQ(ngot, 0);
    }

    /* A scan that has examined nothing has the empty cursor. */
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0, ""),
          ENG_OK);
    eng_scan_cursor(&s, cur);
    TF_STREQ(cur, "");
    more_is(&s, 1);
}

/* eng_scan_more between steps does not move the scan.  Continues
   test_roundtrip on the same files. */
static void test_more(void)
{
    eng_scan s;
    u32 r;
    u16 n;
    u16 i;

    n = walk(ENG_X_ITEMS4, NULL, 0, NULL, 0);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, NULL),
          ENG_OK);
    more_is(&s, 1);
    for (i = 0; i < n; i++) {
        more_is(&s, 1);
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
        TF_EQ(r, g_want[i]);
    }
    more_is(&s, 0);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);
    more_is(&s, 0);
}

/* ---------------------------------------------------------------------
 * Hostile cursors.
 * --------------------------------------------------------------------- */

static void hostile(int t, int ix, const u8 *prefix, u16 plen,
                    const char *c, int want)
{
    eng_scan s;
    int rc;

    rc = eng_scan_open(g_e, &s, t, ix, prefix, plen, c);
    if (!TF_EQ(rc, want))
        printf("  cursor \"%s\"\n", c);
}

static void test_hostile(void)
{
    static const char *const bad4[] = {
        "ITEMS4", "ITEMS4:", "ITEMS4:41", "ITEMS4::", "ITEMS4:41:",
        "ITEMS4::0", "ITEMS4::00000000", "ITEMS4::123456789",
        "ITEMS4::000000001", "ITEMS4::G", "ITEMS4:4:1", "ITEMS4:414:1",
        "ITEMS4:4G:1", "ITEMS4:G4:1", "ITEMS4:41:1:", "ITEMS4:41:1x",
        "ITEMS4 :41:1", " ITEMS4::1", "ITEMS4:41:-1", "ITEMS4:41:+1",
        "ITEMS4: 41:1", "ITEMS4:41 :1", "ITEMS4:41: 1", "ITEMS5::1",
        "ITEMS::1", "ITEMS44::1", "ITEMS:41:1", "ITEM4::1", ":41:1",
        "::1", ":", "X", "ITEMS4;41;1", "LOCS1::1", "ITEMS4:41:1 ",
        NULL
    };
    char c[ENG_CURSORMAX + 8];
    u8 pre[ENG_IDLEN];
    int i;

    if (!fresh())
        return;
    item_add(1, "11", "IBM", "5150", "");
    item_add(2, "12", "IBM", "5160", "");
    for (i = 0; bad4[i] != NULL; i++)
        hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, bad4[i], ENG_ECURSOR);

    /* Bytes of 80h and above are not hex digits or names; as key bytes
       they are fine. */
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4:\xC4\xE9:1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4\xC4:41:1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4:41:\xB1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4:C4E9FF80:1", ENG_OK);

    /* Accepted forms: lower case, leading zeros, the longest key. */
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "items4:4a4b:1", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4::0000000A", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4::FFFFFFFF", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, "ITEMS4:00ff:1", ENG_OK);
    strcpy(c, "ITEMS4:");
    for (i = 0; i < ENG_MAXKEYLEN; i++)
        strcat(c, "5A");
    strcat(c, ":1");
    TF_EQ((long)strlen(c), ENG_CURSORLEN - 7);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, c, ENG_OK);
    /* One byte more than the key, and an odd digit. */
    strcpy(c, "ITEMS4:");
    for (i = 0; i <= ENG_MAXKEYLEN; i++)
        strcat(c, "5A");
    strcat(c, ":1");
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, c, ENG_ECURSOR);
    strcpy(c, "ITEMS4:");
    for (i = 0; i < ENG_MAXKEYLEN; i++)
        strcat(c, "5A");
    strcat(c, "5:1");
    hostile(ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, c, ENG_ECURSOR);
    /* ITEMS1 keys are 8 bytes. */
    hostile(ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0,
            "ITEMS1:2020202020202031:1", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0,
            "ITEMS1:202020202020203131:1", ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS1, NULL, 0, "ITEMS4::1", ENG_ECURSOR);

    /* Prefix scans: the key must start with the prefix. */
    memcpy(pre, "      11", ENG_IDLEN);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
            "ITEMS2:2020202020203131:1", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
            "ITEMS2:20202020202031312020202020202031:1", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
            "ITEMS2:2020202020203132:1", ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
            "ITEMS2:20202020202031:1", ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN,
            "ITEMS2:2020202020313131:1", ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, ENG_IDLEN, "ITEMS2::1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, pre, 2, "ITEMS2:2020202020313131:1",
            ENG_OK);
    hostile(ENG_T_ITEMS, ENG_X_ITEMS2, NULL, 0, "ITEMS2::1", ENG_OK);

    /* Record order: the table's name and an empty key only. */
    hostile(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, "ITEMS::1", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, "Items::1", ENG_OK);
    hostile(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, "ITEMS:20:1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, "ITEMS1::1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, "LOCS::1", ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_SCAN_RECORDS, NULL, 0, "ITEMS::0",
            ENG_ECURSOR);
    hostile(ENG_T_LOCS, ENG_X_LOCS1, NULL, 0, "LOCS::1", ENG_ECURSOR);
    hostile(ENG_T_LOCS, ENG_X_LOCS1, NULL, 0, "LOCS1::1", ENG_OK);
    hostile(ENG_T_USERS, ENG_X_USERS1, NULL, 0, "USERS1:41444D494E:1",
            ENG_OK);
    hostile(ENG_T_USERS, ENG_X_USERS1, NULL, 0,
            "USERS1:41444D494E41444D49:1", ENG_ECURSOR);
}

/* ---------------------------------------------------------------------
 * Changes between pages.
 * --------------------------------------------------------------------- */

/* 1 if recno r is among the n recnos of g_got from index from. */
static int got_has(u16 from, u16 n, u32 r)
{
    u16 i;

    for (i = from; i < n; i++)
        if (g_got[i] == r)
            return 1;
    return 0;
}

static void test_changes(void)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char after[ENG_CURSORMAX];
    char mfr[8];
    u8 ckey[ENG_MAXKEYLEN];
    u32 crec;
    u32 moved;
    u32 gone;
    u32 back;
    u32 i;
    u16 ngot;
    u16 nwant;
    u16 first;
    u16 k;

    if (!fresh())
        return;
    for (i = 1; i <= 30; i++) {
        sprintf(mfr, "M%02lu", (unsigned long)(i % 10));
        item_add(i, "11", mfr, "X", "");
    }
    ngot = 0;
    TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, NULL, NULL, 7,
                  &ngot, cur), ENG_OK);
    TF_EQ(ngot, 7);
    first = ngot;
    memcpy(ckey, s.key, sizeof ckey);
    crec = s.recno;
    TF_EQ(crec, g_got[6]);
    TF_EQ(crec, 2);

    /* Between the pages: the cursor's own record moves to the end, the
       next one is deleted, the one after it moves before the cursor, and
       new records arrive before the cursor, after it, and on its key. */
    moved = crec;
    raw_set_mfr(moved, "ZZZ");
    walk(ENG_X_ITEMS4, NULL, 0, ckey, crec);
    gone = g_want[0];
    back = g_want[1];
    raw_delete(ENG_T_ITEMS, gone);
    raw_set_mfr(back, "A00");
    item_add(31, "11", "M00", "X", "");
    item_add(32, "11", "M09", "X", "");
    item_add(33, "11", "M02", "X", "");
    nwant = walk(ENG_X_ITEMS4, NULL, 0, ckey, crec);

    for (k = 0; cur[0] != '\0' && k < 20; k++) {
        strcpy(after, cur);
        TF_EQ(request(&s, ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, NULL,
                      after, 7, &ngot, cur), ENG_OK);
    }
    TF_CHECK(!got_has(first, ngot, gone));
    TF_CHECK(!got_has(first, ngot, back));
    TF_CHECK(!got_has(first, ngot, 31));
    TF_CHECK(got_has(first, ngot, 32));
    TF_CHECK(got_has(first, ngot, 33));
    TF_EQ(g_got[ngot - 1], moved);
    for (k = 0; k < first; k++)
        TF_CHECK(!got_has(first, ngot, g_got[k]) || g_got[k] == moved);
    memmove(g_got, g_got + first, (size_t)(ngot - first) * sizeof g_got[0]);
    TF_CHECK(same_list((u16)(ngot - first), nwant));
}

/* ---------------------------------------------------------------------
 * Equal keys and the read limit.
 * --------------------------------------------------------------------- */

static int reject_all(eng *e, eng_scan *s)
{
    return e == NULL && s == NULL;
}

/* Keeps every tenth record by its ID (the first field). */
static int keep_tenth(eng *e, eng_scan *s)
{
    char v[ENG_MAXVAL];

    eng_rec_text(e, s->table, 0, v);
    return atol(v) % 10 == 0;
}

/* cf_fault_rd: count the reads, failing none. */
static int count_read(int op)
{
    if (op == CF_OP_READ)
        g_nread++;
    return 0;
}

u32 user_add(u32 id, const char *name)
{
    char ids[12];
    const char *kv[5];

    sprintf(ids, "%lu", (unsigned long)id);
    kv[0] = "USER_ID";  kv[1] = ids;
    kv[2] = "USERNAME"; kv[3] = name;
    kv[4] = NULL;
    return raw_add(ENG_T_USERS, kv);
}

static void test_equal_run(void)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char want[ENG_CURSORMAX];
    u16 ngot;
    u16 nreq;
    u16 i;
    u32 r;
    u32 r2;
    int ok;

    /* The cursor of record 12 on ITEMS4, as ENGINE.H's example writes it. */
    if (!fresh())
        return;
    item_add(1, "11", "Apple", "II", "");
    for (i = 0; i < 12; i++)
        item_add((u32)(i + 2), "11", "IBM", "5150", "");
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_X_ITEMS4, NULL, 0, NULL),
          ENG_OK);
    for (i = 0; i < 12; i++)
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 12);
    eng_scan_cursor(&s, cur);
    strcpy(want, "ITEMS4:49424D");
    for (i = 0; i < 27; i++)
        strcat(want, "20");
    strcat(want, "35313530:C");
    TF_STREQ(cur, want);

    /* A run of NRUN equal USERS1 keys between two others. */
    if (!fresh())
        return;
    user_add(1, "AAA");
    for (i = 0; i < NRUN; i++)
        user_add((u32)(i + 2), "SAME");
    user_add(NRUN + 2, "ZZZ");

    /* Paging through the run, 25 at a time. */
    nreq = page_all(ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL, 25, &ngot);
    TF_EQ(ngot, NRUN + 2);
    TF_EQ(nreq, (NRUN + 2 + 24) / 25);
    for (i = 0; i < ngot; i++)
        TF_EQ(g_got[i], i + 1);

    /* Reopening after every entry of the run, and after record numbers
       before and past it, takes a few page reads and gives the next
       entry; every COLDEVERY-th open starts with an empty page pool. */
    ok = 1;
    for (r = 1; r <= NRUN + 3 && ok; r++) {
        if (r % COLDEVERY == 0 && !reopen(0))
            return;
        make_cursor("USERS1", (const u8 *)"SAME", 4,
                    r <= NRUN + 2 ? r : 0xFFFFFFFFUL, want);
        g_nread = 0;
        cf_fault_rd = count_read;
        ok = TF_EQ(eng_scan_open(g_e, &s, ENG_T_USERS, ENG_X_USERS1, NULL,
                                 0, want), ENG_OK);
        cf_fault_rd = NULL;
        ok = ok && TF_CHECK(g_nread <= OPENREADS);
        ok = ok && TF_EQ(eng_scan_next(g_e, &s, &r2), ENG_OK);
        ok = ok && TF_EQ(r2, r <= NRUN ? r + 1 : NRUN + 2);
    }

    /* Reopening deep inside the run: positioning counts nothing. */
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_USERS, ENG_X_USERS1, NULL, 0,
                        "USERS1:53414D45:D2"), ENG_OK);
    TF_EQ(s.reads, 0);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0xD3);
    TF_EQ(s.reads, 1);

    /* A filter that rejects the whole run: each request examines exactly
       ENG_MAXREADS entries and returns nothing, yet the paging ends. */
    nreq = page_all(ENG_T_USERS, ENG_X_USERS1, NULL, 0, reject_all, 25,
                    &ngot);
    TF_EQ(ngot, 0);
    TF_EQ(nreq, (NRUN + 2 + ENG_MAXREADS - 1) / ENG_MAXREADS);
    ngot = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, reject_all, NULL,
                  25, &ngot, cur), ENG_OK);
    TF_EQ(s.reads, ENG_MAXREADS);
    TF_EQ(s.recno, ENG_MAXREADS);
    make_cursor("USERS1", (const u8 *)"SAME", 4, ENG_MAXREADS, want);
    TF_STREQ(cur, want);
    strcpy(want, cur);
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, reject_all, want,
                  25, &ngot, cur), ENG_OK);
    TF_EQ(ngot, 0);
    TF_EQ(s.reads, NRUN + 2 - ENG_MAXREADS);
    TF_STREQ(cur, "");
}

static void test_read_limit(void)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char c2[ENG_CURSORMAX];
    char want[ENG_CURSORMAX];
    char name[12];
    dbf *d;
    u16 ngot;
    u16 i;
    u32 r;

    if (!fresh())
        return;
    /* 250 deleted users, then 10 live ones. */
    d = eng_dbf(g_e, ENG_T_USERS);
    for (i = 1; i <= 260; i++) {
        sprintf(name, "U%03u", i);
        user_add(i, name);
        if (i <= 250)
            raw_delete(ENG_T_USERS, i);
    }
    ngot = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL, NULL, 25,
                  &ngot, cur), ENG_OK);
    TF_EQ(ngot, 0);
    TF_EQ(s.reads, ENG_MAXREADS);
    TF_EQ(s.recno, ENG_MAXREADS);
    /* The last entry examined: the user of record ENG_MAXREADS. */
    sprintf(name, "U%03u", (unsigned)ENG_MAXREADS);
    make_cursor("USERS1", (const u8 *)name, (u16)strlen(name), ENG_MAXREADS,
                want);
    TF_STREQ(cur, want);
    /* The limit is reached: no further entry is examined. */
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);
    TF_EQ(s.reads, ENG_MAXREADS);
    TF_EQ(s.recno, ENG_MAXREADS);
    strcpy(c2, cur);
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, NULL, c2, 1,
                  &ngot, cur), ENG_OK);
    TF_EQ(ngot, 1);
    TF_EQ(g_got[0], 251);
    TF_EQ(s.reads, 51);

    /* Every tenth kept: one request returns the matches among the first
       ENG_MAXREADS entries examined. */
    for (i = 1; i <= 250; i++) {
        TF_EQ(dbf_read(d, i), DBF_OK);
        dbf_set_deleted(d, 0);
        TF_EQ(dbf_write(d), DBF_OK);
    }
    ngot = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_X_USERS1, NULL, 0, keep_tenth, NULL,
                  25, &ngot, cur), ENG_OK);
    TF_EQ(ngot, ENG_MAXREADS / 10);
    TF_EQ(s.reads, ENG_MAXREADS);
    /* The 200th entry examined is returned; the next call reads nothing. */
    TF_EQ(g_got[ngot - 1], ENG_MAXREADS);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);
    TF_EQ(s.reads, ENG_MAXREADS);
    TF_STREQ(cur, want);

    /* The same in record order. */
    ngot = 0;
    TF_EQ(request(&s, ENG_T_USERS, ENG_SCAN_RECORDS, NULL, 0, reject_all,
                  NULL, 25, &ngot, cur), ENG_OK);
    TF_EQ(ngot, 0);
    TF_EQ(s.reads, ENG_MAXREADS);
    make_cursor("USERS", (const u8 *)"", 0, ENG_MAXREADS, want);
    TF_STREQ(cur, want);
    strcpy(c2, cur);
    TF_EQ(request(&s, ENG_T_USERS, ENG_SCAN_RECORDS, NULL, 0, reject_all,
                  c2, 25, &ngot, cur), ENG_OK);
    TF_EQ(s.reads, 60);
    TF_STREQ(cur, "");
}

/* ---------------------------------------------------------------------
 * Reverse record order and bounds.
 * --------------------------------------------------------------------- */

/* Last record first, deleted ones skipped, cursors as in record order. */
static void test_reverse(void)
{
    eng_scan s;
    u16 ngot;
    u16 i;
    u32 r;

    if (!fresh())
        return;
    fill_tables();
    for (i = 0; i < 4; i++) {
        page_all(ENG_T_ITEMS, ENG_SCAN_RRECORDS, NULL, 0, NULL,
                 (u16)(i * 6 + 1), &ngot);
        TF_EQ(ngot, 20);
        TF_EQ(g_got[0], 21);
        TF_EQ(g_got[15], 6);
        TF_EQ(g_got[16], 4);
        TF_EQ(g_got[19], 1);
    }
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_SCAN_RRECORDS, NULL, 0,
                        "items::6"), ENG_OK);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 4);
    TF_EQ(s.reads, 2);
    more_is(&s, 1);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_SCAN_RRECORDS, NULL, 0,
                        "ITEMS::1"), ENG_OK);
    more_is(&s, 0);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 0);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS, ENG_SCAN_RRECORDS, NULL, 0,
                        "ITEMS::FFFFFFFF"), ENG_OK);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 21);
    hostile(ENG_T_ITEMS, ENG_SCAN_RRECORDS, NULL, 0, "ITEMS1::1",
            ENG_ECURSOR);
    hostile(ENG_T_ITEMS, ENG_SCAN_RRECORDS, (const u8 *)"1", 1, "",
            ENG_EINTERNAL);
}

/* Open loans (LOANS2 prefix "   0") due before 20260601: the loans
   numbered 1 to 4 of fill_tables.  The entry at the bound ends the scan
   uncounted and leaves no more entries. */
static void test_bound(void)
{
    static const u8 bound[] = "   020260601";
    const char *kv[7];
    int more;
    eng_scan s;
    char cur[ENG_CURSORMAX];
    char c2[ENG_CURSORMAX];
    u16 ngot;
    u16 i;
    u32 r;

    if (!fresh())
        return;
    fill_tables();
    kv[0] = "LOAN_ID"; kv[1] = "10";
    kv[2] = "DUE_DATE"; kv[3] = "20260101";
    kv[4] = "RET_DATE"; kv[5] = "20260102";
    kv[6] = NULL;
    raw_add(ENG_T_LOANS, kv);
    for (i = 3; i <= 5; i++) {
        ngot = 0;
        TF_EQ(eng_scan_open(g_e, &s, ENG_T_LOANS, ENG_X_LOANS2, bound, 4,
                            NULL), ENG_OK);
        s.blen = i == 5 ? 0 : ENG_BOUNDLEN;
        memcpy(s.bound, bound, ENG_BOUNDLEN);
        while (ngot < i && eng_scan_next(g_e, &s, &r) == ENG_OK && r != 0)
            g_got[ngot++] = r;
        TF_EQ(ngot, i);
        TF_EQ(g_got[0], 1);
        TF_EQ(g_got[2], 3);
        more_is(&s, i == 3 || i == 5);
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
        TF_EQ(r, i == 3 ? 4 : i == 4 ? 0 : 6);
        TF_EQ(s.reads, i == 5 ? 6 : 4);
    }
    /* Paged: three, then the fourth with no cursor after it. */
    ngot = 0;
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_LOANS, ENG_X_LOANS2, bound, 4,
                        NULL), ENG_OK);
    s.blen = ENG_BOUNDLEN;
    memcpy(s.bound, bound, ENG_BOUNDLEN);
    for (i = 0; i < 3; i++)
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    more_is(&s, 1);
    eng_scan_cursor(&s, cur);
    strcpy(c2, cur);
    TF_EQ(eng_scan_open(g_e, &s, ENG_T_LOANS, ENG_X_LOANS2, bound, 4, c2),
          ENG_OK);
    s.blen = ENG_BOUNDLEN;
    memcpy(s.bound, bound, ENG_BOUNDLEN);
    TF_EQ(eng_scan_next(g_e, &s, &r), ENG_OK);
    TF_EQ(r, 4);
    more_is(&s, 0);
    /* A bound longer than the key (ITEMS1, 8 bytes) or in record
       order is refused. */
    for (i = 0; i < 2; i++) {
        TF_EQ(eng_scan_open(g_e, &s, ENG_T_ITEMS,
                            i == 0 ? ENG_X_ITEMS1 : ENG_SCAN_RECORDS, NULL,
                            0, NULL), ENG_OK);
        s.blen = (u8)(i == 0 ? 9 : 1);
        TF_EQ(eng_scan_next(g_e, &s, &r), ENG_EINTERNAL);
        TF_EQ(eng_scan_more(g_e, &s, &more), ENG_EINTERNAL);
    }
}

int main(int argc, char **argv)
{
    tf_begin("T_CURSOR");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    test_roundtrip();
    test_more();
    test_hostile();
    test_changes();
    test_equal_run();
    test_read_limit();
    test_empty_last();
    test_before_and_bad_record();
    test_damaged();
    test_reverse();
    test_bound();
    test_walk_cursor();
    test_id_find();
    if (g_e != NULL)
        eng_close(g_e);
    te_remove(tfs_dir(), NULL);
    return tf_done();
}

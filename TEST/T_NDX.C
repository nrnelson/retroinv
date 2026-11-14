/*
 * T_NDX.C - unit test for the dBASE III index files of NDX.C.
 *
 * usage: T_NDX [dir [golden]]
 *
 * Creates scratch indexes in dir (default the current directory) and
 * checks the header written by ndx_create byte for byte; inserts NKEYS
 * keys built from a fixed pseudo-random sequence, with duplicate keys and
 * one long run of equal keys, in shuffled record order, and compares
 * forward and backward walks, seeks of every key, of absent keys and of
 * prefixes, and seeks after every entry, live or deleted, and after record
 * numbers 0 and 0xFFFFFFFF of a key, with a sorted copy; reads the file
 * back page by page to check the tree's shape and the dBASE page
 * conventions, including inner keys equal to the highest key below them;
 * searches every key by key alone, as dBASE III Plus does; deletes about
 * half the keys, deletes and re-inserts others and checks again, before
 * and after a reopen; builds a deep tree with the longest keys, empties it
 * and unlinks emptied leaves at both ends of their parents; checks a
 * read-only open, a header left behind its pages by a stop before
 * ndx_flush, damaged files, an insert below an inner key raised above its
 * child's keys, headers with each field the open checks out of range or on
 * the edge of its range (on the host also a root of 0 with an allocation
 * failing), and argument checks.
 * golden is a directory holding tables and indexes written by dBASE
 * III Plus 1.1 (default TEST/GOLDEN/DB3, which works from the
 * repository root): each index in goldens[] is checked page by page,
 * walked against its table's keys and searched for every record, then
 * a copy of it is changed here and checked again; the numeric BIG3.NDX
 * must be refused.  When the golden files are absent and no golden
 * argument was given, as on a DOS test drive, those checks are skipped.
 * The scratch files are removed at the start and at the end.  Prints
 * each failure and a summary line; exit status 0 when every check
 * passes, else 1 (2 when dir or golden is too long).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "T_FILES.H"

#ifndef NKEYS
#define NKEYS       5000    /* keys in the main test, at most 32767     */
#endif
#define KEYLEN      4       /* a u16 in right-justified hex: 12-byte
                               entries keep the file small on a floppy */
#define TENTSZ      12      /* entry size of a KEYLEN key               */
#define TKPP        42      /* keys per page of a KEYLEN key            */
#define VALMAX      30000u  /* key values are even numbers below 2 * VALMAX */
#define RUNVAL      50000u  /* the value of the long run of equal keys  */
#define RUNEVERY    10      /* every RUNEVERY-th key has RUNVAL         */
#define DUPEVERY    7       /* every DUPEVERY-th key repeats an earlier one */
#define NPREFIX     300     /* prefix seeks                             */
#define NDEEP       300     /* keys in the deep tree, sized for a floppy */
#define DEEPLEN     NDX_MAXKEY
#define DEEPBLOCK   8       /* deep-tree entries deleted high to low    */
#define DEEPENT     108     /* entry size of a DEEPLEN key              */
#define NUNLINK     100     /* keys in the unlink test                  */
#define NUNLINK_ROUNDS 6
#define MAXLEVELS   16      /* levels the page checker follows          */
#define WALKMAX     20000L  /* steps before a walk counts as endless    */
#define STALEKEY    8       /* key length of test_stale_inner           */

/* The scratch files, removed before and after the run. */
static const char *const k_scratch[] = {
    "T_NDX_A.NDX", "T_NDX_B.NDX", NULL
};

/* The golden directory as a prefix, separator included, made by
   tfs_set_dir's rule. */
static char g_gold[TFS_MAXDIR + 1];
static char g_gpath[DOS_PATHMAX];

/* Test data, from malloc so that they stay out of the near data.  Entry
   i has the key make_key(g_vals[i]) and record number i + 1. */
static u16 *g_vals;     /* NKEYS key values                             */
static u16 *g_perm;     /* NKEYS entry numbers: insertion order, then
                           sorted by key and record number              */
static u8 *g_gone;      /* NKEYS + 1 flags: entry i deleted, or in a
                           golden walk, record number i seen            */
static u8 *g_page;      /* MAXLEVELS pages for the page checker         */

static u32 g_lcg;

static u16 lcg(void)
{
    g_lcg = (u32)(g_lcg * (u32)1103515245UL + (u32)12345U);
    return (u16)((g_lcg >> 16) & 0x7FFFu);
}

/* The golden file name: the golden prefix and name, in g_gpath. */
static const char *golden_path(const char *name)
{
    strcpy(g_gpath, g_gold);
    strcat(g_gpath, name);
    return g_gpath;
}

static void make_key(u16 v, u8 *out)
{
    char buf[16];

    sprintf(buf, "%4X", (unsigned)v);
    memcpy(out, buf, KEYLEN);
}

/* ---------------------------------------------------------------------
 * Raw file access.
 * --------------------------------------------------------------------- */

static int poke32(const char *path, u32 pos, u32 v)
{
    u8 b[4];

    put_le32(b, v);
    return tfs_poke(path, pos, b, 4);
}

static int poke16(const char *path, u32 pos, u16 v)
{
    u8 b[2];

    put_le16(b, v);
    return tfs_poke(path, pos, b, 2);
}

/* Copy src to dst cut to its first size bytes; 0 or a cf_ code. */
static int copy_cut(const char *src, const char *dst, u32 size)
{
    int h;
    int rc;
    int rc2;

    rc = tfs_copy(src, dst);
    if (rc != 0)
        return rc;
    rc = cf_open(dst, (u8)(CF_RDWR | CF_DENYNONE), &h);
    if (rc != 0)
        return rc;
    rc = cf_truncate(h, size);
    rc2 = cf_close(h);
    return rc != 0 ? rc : rc2;
}

/* ---------------------------------------------------------------------
 * The page checker: reads the file without NDX.C and checks the tree.
 * --------------------------------------------------------------------- */

/* Read page pg of the file open on h into buf; returns 0 on success. */
static int read_page(int h, u32 pg, u8 *buf)
{
    u16 got;

    if (cf_seek(h, pg * (u32)NDX_PAGE) != 0 ||
        cf_read(h, buf, NDX_PAGE, &got) != 0 || got != NDX_PAGE)
        return -1;
    return 0;
}

typedef struct {
    int h;
    u32 npages;
    u16 keylen;
    u16 kpp;
    u16 entsz;
    int strict;         /* written here: no junk, leaves at one depth   */
    int height;         /* deepest leaf level + 1                       */
    int lowest;         /* shallowest leaf level + 1                    */
    i32 entries;
    int bad;
} pcheck;

/* Check the subtree under pg at level d: every inner key equals the
   highest key of its child, the keys after it are not lower, and no
   page but a root leaf is empty.  *maxkey and *minkey receive the
   subtree's highest and lowest key (keylen bytes); *empty is 1 when it
   has no entries. */
static void check_page(pcheck *c, u32 pg, int d, u8 *maxkey, u8 *minkey,
                       int *empty)
{
    u8 *b;
    u8 *e;
    u8 sub[NDX_MAXKEY];
    u8 submin[NDX_MAXKEY];
    u16 n;
    u16 i;
    u16 j;
    u32 child;
    int subempty;

    *empty = 1;
    if (d >= MAXLEVELS || pg == 0 || pg >= c->npages) {
        c->bad = 1;
        return;
    }
    b = g_page + (size_t)d * NDX_PAGE;
    if (read_page(c->h, pg, b) != 0) {
        c->bad = 1;
        return;
    }
    n = get_le16(b);
    if (n > c->kpp || (c->strict && (b[2] != 0 || b[3] != 0))) {
        c->bad = 1;
        return;
    }
    if (get_le32(b + 4) == 0) {
        /* A leaf; only the root may be empty. */
        if (c->height < d + 1)
            c->height = d + 1;
        if (c->lowest == 0 || c->lowest > d + 1)
            c->lowest = d + 1;
        if (n == 0) {
            if (d > 0)
                c->bad = 1;
            return;
        }
        c->entries += n;
        for (i = 0; i < n; i++) {
            e = b + 4 + (size_t)i * c->entsz;
            if (get_le32(e) != 0 || get_le32(e + 4) == 0)
                c->bad = 1;
            if (c->strict)
                for (j = (u16)(8 + c->keylen); j < c->entsz; j++)
                    if (e[j] != 0)
                        c->bad = 1;
        }
        if (c->strict)
            for (j = (u16)(4 + n * c->entsz); j < NDX_PAGE; j++)
                if (b[j] != 0)
                    c->bad = 1;
        memcpy(minkey, b + 4 + 8, c->keylen);
        memcpy(maxkey, b + 4 + (size_t)(n - 1) * c->entsz + 8, c->keylen);
        *empty = 0;
        return;
    }
    /* An inner page: n keyed children and the rightmost child. */
    if (c->strict)
        for (j = (u16)(4 + n * c->entsz + 4); j < NDX_PAGE; j++)
            if (b[j] != 0)
                c->bad = 1;
    for (i = 0; i <= n && !c->bad; i++) {
        e = b + 4 + (size_t)i * c->entsz;
        child = get_le32(e);
        if (child == 0 || child == pg || (i < n && get_le32(e + 4) != 0)) {
            c->bad = 1;
            return;
        }
        check_page(c, child, d + 1, sub, submin, &subempty);
        if (c->bad)
            return;
        if (subempty ||
            (i < n && memcmp(sub, e + 8, c->keylen) != 0) ||
            (i > 0 && memcmp(submin, e + 8 - c->entsz, c->keylen) < 0)) {
            c->bad = 1;
            return;
        }
        if (i == 0)
            memcpy(minkey, submin, c->keylen);
        if (i == n)
            memcpy(maxkey, sub, c->keylen);
    }
    *empty = 0;
}

/* Check the whole file; strict for files written here (no junk, every
   leaf at the same depth).  Returns the height, or 0 when the tree is
   bad; *entries receives the number of leaf entries. */
static int check_tree(const char *path, int strict, i32 *entries)
{
    pcheck c;
    u8 hdr[24];
    u8 mx[NDX_MAXKEY];
    u8 mn[NDX_MAXKEY];
    int empty;

    *entries = -1;
    if (!TF_EQ(tfs_peek(path, 0, hdr, sizeof hdr), 0))
        return 0;
    c.npages = get_le32(hdr + 4);
    c.keylen = get_le16(hdr + 12);
    c.kpp = get_le16(hdr + 14);
    c.entsz = get_le16(hdr + 18);
    c.strict = strict;
    c.height = 0;
    c.lowest = 0;
    c.entries = 0;
    c.bad = 0;
    if (!TF_CHECK(tfs_size(path) == c.npages * (u32)NDX_PAGE))
        return 0;
    if (!TF_EQ(cf_open(path, (u8)(CF_READ | CF_DENYNONE), &c.h), 0))
        return 0;
    check_page(&c, get_le32(hdr), 0, mx, mn, &empty);
    cf_close(c.h);
    if (strict && c.height != c.lowest)
        c.bad = 1;
    if (!TF_CHECK(!c.bad))
        return 0;
    *entries = c.entries;
    return c.height;
}

/* Search for key the way dBASE III Plus does, by key alone: descend
   into the first child whose key is >= key, then look in that leaf
   only.  Returns the record of the first entry there if its key is key,
   else 0.  h is the open index file. */
static u32 dbase_find(int h, const u8 *key)
{
    u8 *b;
    u8 *e;
    u32 pg;
    u16 keylen;
    u16 entsz;
    u16 n;
    u16 i;
    int d;

    b = g_page;
    if (read_page(h, 0, b) != 0)
        return 0;
    pg = get_le32(b);
    keylen = get_le16(b + 12);
    entsz = get_le16(b + 18);
    for (d = 0; d < MAXLEVELS; d++) {
        if (read_page(h, pg, b) != 0)
            return 0;
        n = get_le16(b);
        for (i = 0; i < n; i++)
            if (memcmp(b + 4 + (size_t)i * entsz + 8, key, keylen) >= 0)
                break;
        e = b + 4 + (size_t)i * entsz;
        if (get_le32(b + 4) == 0) {
            if (i < n && memcmp(e + 8, key, keylen) == 0)
                return get_le32(e + 4);
            return 0;
        }
        pg = get_le32(e);
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * The sorted copy.
 * --------------------------------------------------------------------- */

static int cmp_entry(const void *a, const void *b)
{
    u16 i;
    u16 j;

    i = *(const u16 *)a;
    j = *(const u16 *)b;
    if (g_vals[i] != g_vals[j])
        return g_vals[i] < g_vals[j] ? -1 : 1;
    if (i != j)
        return i < j ? -1 : 1;
    return 0;
}

/* Position in g_perm (sorted) of the first live entry whose key's first
   len bytes are >= prefix, or NKEYS. */
static u16 expect_lb(const u8 *prefix, u16 len)
{
    u8 k[KEYLEN];
    u16 lo;
    u16 hi;
    u16 mid;

    lo = 0;
    hi = NKEYS;
    while (lo < hi) {
        mid = (u16)(lo + (hi - lo) / 2u);
        make_key(g_vals[g_perm[mid]], k);
        if (memcmp(k, prefix, len) < 0)
            lo = (u16)(mid + 1u);
        else
            hi = mid;
    }
    while (lo < NKEYS && g_gone[g_perm[lo]])
        lo++;
    return lo;
}

/* Record number of the entry at position e in g_perm (sorted), or 0
   past the end. */
static u32 sorted_rec(u16 e)
{
    return e < NKEYS ? g_perm[e] + 1u : 0u;
}

static void make_data(void)
{
    u16 i;
    u16 j;
    u16 t;

    g_lcg = 1986UL;
    for (i = 0; i < NKEYS; i++) {
        if (i % RUNEVERY == RUNEVERY - 1)
            g_vals[i] = (u16)RUNVAL;
        else if (i > 0 && i % DUPEVERY == 0)
            g_vals[i] = g_vals[lcg() % i];
        else
            g_vals[i] = (u16)(2u * ((u16)(lcg() % VALMAX)));
        g_perm[i] = i;
        g_gone[i] = 0;
    }
    /* Insertion order: a shuffle, so equal keys arrive out of record
       order. */
    for (i = NKEYS - 1; i > 0; i--) {
        j = (u16)(lcg() % (i + 1u));
        t = g_perm[i];
        g_perm[i] = g_perm[j];
        g_perm[j] = t;
    }
}

/* Walk forward and backward and compare with the live sorted entries. */
static void check_walks(ndx *x)
{
    u8 k[KEYLEN];
    u8 want[KEYLEN];
    u32 r;
    i32 pos;
    i32 seen;
    int ok;

    pos = 0;
    seen = 0;
    ok = 1;
    TF_EQ(ndx_first(x, &r), DBF_OK);
    while (r != 0 && ok) {
        while (pos < NKEYS && g_gone[g_perm[(u16)pos]])
            pos++;
        if (!TF_CHECK(pos < NKEYS))
            return;
        ok = TF_EQ(r, g_perm[(u16)pos] + 1u);
        TF_EQ(ndx_key(x, k), DBF_OK);
        make_key(g_vals[g_perm[(u16)pos]], want);
        ok = ok && TF_CHECK(memcmp(k, want, KEYLEN) == 0);
        pos++;
        seen++;
        ok = ok && TF_EQ(ndx_next(x, &r), DBF_OK);
    }
    while (pos < NKEYS && g_gone[g_perm[(u16)pos]])
        pos++;
    TF_EQ(pos, NKEYS);
    TF_EQ(ndx_key(x, k), DBF_ERANGE);
    TF_EQ(ndx_next(x, &r), DBF_OK);
    TF_EQ(r, 0);

    /* Backward from past the end. */
    pos = NKEYS - 1;
    ok = 1;
    TF_EQ(ndx_prev(x, &r), DBF_OK);
    while (r != 0 && ok) {
        while (pos >= 0 && g_gone[g_perm[(u16)pos]])
            pos--;
        if (!TF_CHECK(pos >= 0))
            return;
        ok = TF_EQ(r, g_perm[(u16)pos] + 1u);
        pos--;
        seen--;
        ok = ok && TF_EQ(ndx_prev(x, &r), DBF_OK);
    }
    TF_EQ(seen, 0);
    TF_EQ(ndx_prev(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_next(x, &r), DBF_OK);
    TF_CHECK(r != 0);
}

/* Seek every live key, absent keys and prefixes. */
static void check_seeks(ndx *x)
{
    u8 k[KEYLEN];
    u8 got[KEYLEN];
    u32 r;
    u32 r2;
    u16 i;
    u16 e;
    u16 len;
    int exact;
    int ok;

    ok = 1;
    for (i = 0; i < NKEYS && ok; i++) {
        if (g_gone[g_perm[i]])
            continue;
        make_key(g_vals[g_perm[i]], k);
        e = expect_lb(k, KEYLEN);
        ok = TF_EQ(ndx_seek(x, k, KEYLEN, &r, &exact), DBF_OK);
        ok = ok && TF_EQ(r, g_perm[e] + 1u) && TF_EQ(exact, 1);
    }
    /* Absent keys: odd values sort between the even ones. */
    ok = 1;
    for (i = 0; i < NKEYS && ok; i += 3) {
        make_key((u16)(g_vals[i] | 1u), k);
        e = expect_lb(k, KEYLEN);
        ok = TF_EQ(ndx_seek(x, k, KEYLEN, &r, &exact), DBF_OK);
        ok = ok && TF_EQ(exact, 0);
        ok = ok && TF_EQ(r, sorted_rec(e));
        if (ok && e < NKEYS) {
            /* Stepping from a seek continues in order. */
            TF_EQ(ndx_key(x, got), DBF_OK);
            TF_CHECK(memcmp(got, k, KEYLEN) > 0);
            TF_EQ(ndx_prev(x, &r2), DBF_OK);
            if (r2 != 0) {
                TF_EQ(ndx_key(x, got), DBF_OK);
                TF_CHECK(memcmp(got, k, KEYLEN) < 0);
            }
        }
    }
    /* Prefixes of existing keys and of digit strings. */
    ok = 1;
    for (i = 0; i < NPREFIX && ok; i++) {
        make_key(g_vals[lcg() % NKEYS], k);
        len = (u16)(1u + lcg() % KEYLEN);
        if (i % 4 == 3)
            k[len - 1] = (u8)('0' + lcg() % 10u);
        e = expect_lb(k, len);
        ok = TF_EQ(ndx_seek(x, k, len, &r, &exact), DBF_OK);
        ok = ok && TF_EQ(r, sorted_rec(e));
        if (ok && e < NKEYS) {
            make_key(g_vals[g_perm[e]], got);
            TF_EQ(exact, memcmp(got, k, len) == 0);
        }
    }
    /* Past every key, and the empty prefix. */
    memset(k, 'Z', KEYLEN);
    TF_EQ(ndx_seek(x, k, KEYLEN, &r, &exact), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(exact, 0);
    TF_EQ(ndx_key(x, got), DBF_ERANGE);
    TF_EQ(ndx_prev(x, &r), DBF_OK);
    e = NKEYS;
    while (e > 0 && g_gone[g_perm[e - 1]])
        e--;
    TF_EQ(r, e > 0 ? g_perm[e - 1] + 1u : 0u);
    e = expect_lb(k, 0);
    TF_EQ(ndx_seek(x, k, 0, &r, &exact), DBF_OK);
    TF_EQ(r, sorted_rec(e));
    TF_EQ(exact, e < NKEYS);
}

/* Seek after every entry, live or deleted, after record numbers 0 and
   0xFFFFFFFF of present and absent keys, and past every key; a seek
   leaves the position between the live entry before and the one found. */
static void check_after(ndx *x)
{
    u8 k[KEYLEN];
    u8 odd[KEYLEN];
    u32 r;
    u16 i;
    u16 j;
    u16 e;
    int ok;

    ok = 1;
    for (i = 0; i < NKEYS && ok; i++) {
        make_key(g_vals[g_perm[i]], k);
        e = (u16)(i + 1u);
        while (e < NKEYS && g_gone[g_perm[e]])
            e++;
        j = (u16)(i + 1u);
        while (j > 0 && g_gone[g_perm[j - 1u]])
            j--;
        ok = TF_EQ(ndx_seek_after(x, k, g_perm[i] + 1u, &r), DBF_OK);
        ok = ok && TF_EQ(r, sorted_rec(e));
        ok = ok && TF_EQ(ndx_prev(x, &r), DBF_OK);
        ok = ok && TF_EQ(r, j > 0 ? g_perm[j - 1u] + 1u : 0u);
        ok = ok && TF_EQ(ndx_next(x, &r), DBF_OK);
        ok = ok && TF_EQ(r, sorted_rec(e));
    }
    /* Before a key's first record, past its last, and in an absent key:
       odd values sort between the even ones. */
    ok = 1;
    for (i = 0; i < NKEYS && ok; i += 3) {
        make_key(g_vals[i], k);
        make_key((u16)(g_vals[i] | 1u), odd);
        ok = TF_EQ(ndx_seek_after(x, k, 0, &r), DBF_OK);
        ok = ok && TF_EQ(r, sorted_rec(expect_lb(k, KEYLEN)));
        e = expect_lb(odd, KEYLEN);
        ok = ok && TF_EQ(ndx_seek_after(x, k, 0xFFFFFFFFUL, &r), DBF_OK);
        ok = ok && TF_EQ(r, sorted_rec(e));
        ok = ok && TF_EQ(ndx_seek_after(x, odd, i + 1u, &r), DBF_OK);
        ok = ok && TF_EQ(r, sorted_rec(e));
    }
    /* Past every key. */
    memset(k, 'Z', KEYLEN);
    TF_EQ(ndx_seek_after(x, k, 0, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_key(x, k), DBF_ERANGE);
}

/* ---------------------------------------------------------------------
 * Tests.
 * --------------------------------------------------------------------- */

static void test_create(const char *path)
{
    u8 want[NDX_PAGE];
    u8 gold[24];
    ndx *x;
    u16 i;
    int same;

    TF_EQ(ndx_create(path, "STR(ITEM_ID,8)", 8), DBF_OK);
    TF_EQ(tfs_size(path), 2 * NDX_PAGE);
    memset(want, 0, sizeof want);
    want[0] = 1;
    want[4] = 2;
    want[12] = 8;
    want[14] = 31;
    want[18] = 16;
    memcpy(want + 24, "STR(ITEM_ID,8)", 14);
    TF_EQ(tfs_peek(path, 0, g_page, NDX_PAGE), 0);
    TF_CHECK(memcmp(g_page, want, NDX_PAGE) == 0);
    TF_EQ(tfs_peek(path, NDX_PAGE, g_page, NDX_PAGE), 0);
    same = 1;
    for (i = 0; i < NDX_PAGE; i++)
        if (g_page[i] != 0)
            same = 0;
    TF_CHECK(same);

    /* The first 24 bytes match the index dBASE built for the same key. */
    if (tfs_peek(golden_path("ITEMS1.NDX"), 0, gold, sizeof gold) == 0)
        TF_CHECK(memcmp(gold, want, sizeof gold) == 0);

    TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK);
    if (x == NULL)
        return;
    TF_EQ(ndx_keylen(x), 8);
    TF_STREQ(ndx_expr(x), "STR(ITEM_ID,8)");
    TF_EQ(ndx_unique(x), 0);
    TF_EQ(ndx_close(x), DBF_OK);

    /* Entry sizes and keys per page at the edges. */
    TF_EQ(ndx_create(path, "C ", 1), DBF_OK);
    TF_EQ(tfs_peek(path, 0, g_page, 24), 0);
    TF_EQ(get_le16(g_page + 14), 42);
    TF_EQ(get_le16(g_page + 18), 12);
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x != NULL) {
        TF_STREQ(ndx_expr(x), "C");
        TF_EQ(ndx_close(x), DBF_OK);
    }
    TF_EQ(ndx_create(path, "NAME", 10), DBF_OK);
    TF_EQ(tfs_peek(path, 0, g_page, 24), 0);
    TF_EQ(get_le16(g_page + 14), 25);
    TF_EQ(get_le16(g_page + 18), 20);
    TF_EQ(ndx_create(path, "K", NDX_MAXKEY), DBF_OK);
    TF_EQ(tfs_peek(path, 0, g_page, 24), 0);
    TF_EQ(get_le16(g_page + 14), 4);
    TF_EQ(get_le16(g_page + 18), 108);
}

static void test_args(const char *path)
{
    char expr[NDX_MAXEXPR + 2];
    u8 k[KEYLEN];
    ndx *x;
    u32 r;
    int exact;

    TF_EQ(ndx_create(path, "K", 0), DBF_EARG);
    TF_EQ(ndx_create(path, "K", NDX_MAXKEY + 1), DBF_EARG);
    TF_EQ(ndx_create(path, "", 8), DBF_EARG);
    TF_EQ(ndx_create(path, NULL, 8), DBF_EARG);
    TF_EQ(ndx_create(NULL, "K", 8), DBF_EARG);
    memset(expr, 'A', sizeof expr);
    expr[NDX_MAXEXPR + 1] = '\0';
    TF_EQ(ndx_create(path, expr, 8), DBF_EARG);
    expr[NDX_MAXEXPR] = '\0';
    TF_EQ(ndx_create(path, expr, 8), DBF_OK);
    TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK);
    if (x != NULL) {
        TF_EQ(strlen(ndx_expr(x)), NDX_MAXEXPR);
        TF_EQ(ndx_close(x), DBF_OK);
    }

    TF_EQ(ndx_create(path, "HEX(N)", KEYLEN), DBF_OK);
    TF_EQ(ndx_open(&x, path, 5), DBF_EARG);
    TF_EQ(ndx_open(NULL, path, DBF_RDWR), DBF_EARG);
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    make_key(7, k);
    TF_EQ(ndx_insert(x, k, 0), DBF_EARG);
    TF_EQ(ndx_insert(x, NULL, 1), DBF_EARG);
    TF_EQ(ndx_seek(x, k, KEYLEN + 1, &r, &exact), DBF_EARG);
    TF_EQ(ndx_seek(x, NULL, 1, &r, &exact), DBF_EARG);
    TF_EQ(ndx_seek(x, k, 1, NULL, &exact), DBF_EARG);
    TF_EQ(ndx_seek_after(x, NULL, 1, &r), DBF_EARG);
    TF_EQ(ndx_seek_after(x, k, 1, NULL), DBF_EARG);

    /* An empty index. */
    TF_EQ(ndx_key(x, k), DBF_ERANGE);
    TF_EQ(ndx_prev(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_next(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_first(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_last(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_seek(x, k, KEYLEN, &r, &exact), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(exact, 0);
    TF_EQ(ndx_seek_after(x, k, 0, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_delete(x, k, 1), DBF_ERANGE);

    /* One entry; positions after insert and delete. */
    TF_EQ(ndx_insert(x, k, 3), DBF_OK);
    memset(k, 0, sizeof k);
    TF_EQ(ndx_key(x, k), DBF_OK);
    TF_CHECK(memcmp(k, "   7", KEYLEN) == 0);
    TF_EQ(ndx_insert(x, k, 3), DBF_EARG);
    TF_EQ(ndx_insert(x, k, 2), DBF_OK);
    TF_EQ(ndx_next(x, &r), DBF_OK);
    TF_EQ(r, 3);
    TF_EQ(ndx_seek_after(x, k, 2, &r), DBF_OK);
    TF_EQ(r, 3);
    TF_EQ(ndx_seek_after(x, k, 3, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_key(x, k), DBF_ERANGE);
    TF_EQ(ndx_prev(x, &r), DBF_OK);
    TF_EQ(r, 3);
    TF_EQ(ndx_delete(x, k, 2), DBF_OK);
    TF_EQ(ndx_key(x, k), DBF_OK);
    TF_EQ(ndx_next(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_delete(x, k, 2), DBF_ERANGE);
    TF_EQ(ndx_delete(x, k, 3), DBF_OK);
    TF_EQ(ndx_key(x, k), DBF_ERANGE);
    TF_EQ(ndx_first(x, &r), DBF_OK);
    TF_EQ(r, 0);

    /* A prefix seek finds a key with a byte below a blank after the
       prefix; past the end it is not exact, even when the key just
       deleted starts with the prefix. */
    TF_EQ(ndx_insert(x, (const u8 *)"A   ", 4), DBF_OK);
    TF_EQ(ndx_insert(x, (const u8 *)"A\001  ", 5), DBF_OK);
    TF_EQ(ndx_seek(x, (const u8 *)"A", 1, &r, &exact), DBF_OK);
    TF_EQ(r, 5);
    TF_EQ(exact, 1);
    TF_EQ(ndx_delete(x, (const u8 *)"A   ", 4), DBF_OK);
    TF_EQ(ndx_seek(x, (const u8 *)"A  ", 3, &r, &exact), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(exact, 0);
    TF_EQ(ndx_close(x), DBF_OK);
}

/* Every live key must be found by a dBASE-style search (dbase_find),
   at its first live record in key order.  x is flushed first. */
static void check_dbase(ndx *x, const char *path)
{
    u8 k[KEYLEN];
    u16 i;
    u16 last;
    int first;
    int h;
    int ok;

    TF_EQ(ndx_flush(x), DBF_OK);
    if (!TF_EQ(cf_open(path, (u8)(CF_READ | CF_DENYNONE), &h), 0))
        return;
    ok = 1;
    first = 1;
    last = 0;
    for (i = 0; i < NKEYS && ok; i++) {
        if (g_gone[g_perm[i]] || (!first && g_vals[g_perm[i]] == last))
            continue;
        first = 0;
        last = g_vals[g_perm[i]];
        make_key(g_vals[g_perm[i]], k);
        ok = TF_EQ(dbase_find(h, k), g_perm[i] + 1u);
    }
    cf_close(h);
}

/* Delete the entries of every other position in insertion order. */
static void delete_half(ndx *x)
{
    u8 k[KEYLEN];
    u16 i;
    u16 j;
    int ok;

    ok = 1;
    g_lcg = 1989UL;
    for (i = 0; i < NKEYS && ok; i++) {
        j = (u16)(lcg() % NKEYS);
        if (g_gone[j])
            continue;
        make_key(g_vals[j], k);
        ok = TF_EQ(ndx_delete(x, k, j + 1u), DBF_OK);
        g_gone[j] = 1;
        ok = ok && TF_EQ(ndx_delete(x, k, j + 1u), DBF_ERANGE);
    }
    /* Every key of the long run, so that whole leaves become empty. */
    make_key((u16)RUNVAL, k);
    for (j = 0; j < NKEYS && ok; j++) {
        if (g_gone[j] || g_vals[j] != RUNVAL || j % (2 * RUNEVERY) == 9)
            continue;
        ok = TF_EQ(ndx_delete(x, k, j + 1u), DBF_OK);
        g_gone[j] = 1;
    }
}

static void test_main(const char *path)
{
    u8 k[KEYLEN];
    u8 k2[KEYLEN];
    ndx *x;
    u32 r;
    u16 i;
    u16 j;
    i32 n;
    int ok;
    int exact;

    make_data();
    TF_EQ(ndx_create(path, "HEX(N) ", KEYLEN), DBF_OK);
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    ok = 1;
    for (i = 0; i < NKEYS && ok; i++) {
        j = g_perm[i];
        make_key(g_vals[j], k);
        ok = TF_EQ(ndx_insert(x, k, j + 1u), DBF_OK);
        ok = ok && TF_EQ(ndx_key(x, k2), DBF_OK);
        ok = ok && TF_CHECK(memcmp(k, k2, KEYLEN) == 0);
        if (i % 500 == 0)
            ok = ok && TF_EQ(ndx_insert(x, k, j + 1u), DBF_EARG);
    }
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_EQ(check_tree(path, 1, &n), 3);
    TF_EQ(n, NKEYS);

    qsort(g_perm, NKEYS, sizeof g_perm[0], cmp_entry);
    check_walks(x);
    check_seeks(x);
    check_after(x);
    check_dbase(x, path);

    delete_half(x);
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_CHECK(check_tree(path, 1, &n) >= 3);
    for (i = 0, j = 0; i < NKEYS; i++)
        if (!g_gone[i])
            j++;
    TF_EQ(n, j);
    check_walks(x);
    check_seeks(x);
    check_after(x);
    check_dbase(x, path);

    /* Delete and insert again the same entries. */
    ok = 1;
    for (i = 3; i < NKEYS && ok; i += 7) {
        if (g_gone[i])
            continue;
        make_key(g_vals[i], k);
        ok = TF_EQ(ndx_delete(x, k, i + 1u), DBF_OK);
        ok = ok && TF_EQ(ndx_insert(x, k, i + 1u), DBF_OK);
    }
    check_dbase(x, path);
    TF_CHECK(check_tree(path, 1, &n) >= 3);

    /* Insert some of the deleted keys again. */
    ok = 1;
    for (i = 0; i < NKEYS && ok; i += 5) {
        if (!g_gone[i])
            continue;
        make_key(g_vals[i], k);
        ok = TF_EQ(ndx_insert(x, k, i + 1u), DBF_OK);
        g_gone[i] = 0;
    }
    check_walks(x);
    check_dbase(x, path);
    TF_EQ(ndx_close(x), DBF_OK);
    TF_CHECK(check_tree(path, 1, &n) >= 3);

    /* Persistence, and a read-only open refusing writes. */
    TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK);
    if (x == NULL)
        return;
    TF_STREQ(ndx_expr(x), "HEX(N)");
    check_walks(x);
    check_seeks(x);
    check_after(x);
    make_key(g_vals[0], k);
    TF_EQ(ndx_insert(x, k, 99999UL), DBF_ERDONLY);
    TF_EQ(ndx_delete(x, k, 1), DBF_ERDONLY);
    TF_EQ(ndx_flush(x), DBF_ERDONLY);
    TF_EQ(ndx_seek(x, k, KEYLEN, &r, &exact), DBF_OK);
    TF_EQ(exact, 1);
    TF_EQ(ndx_close(x), DBF_OK);
}

/* Key of deep-tree entry i (record i + 1): 'Z's ending in i / 2. */
static void deep_key(u8 *k, u16 i)
{
    char buf[8];

    memset(k, 'Z', DEEPLEN);
    sprintf(buf, "%05u", (unsigned)(i / 2u));
    memcpy(k + DEEPLEN - 5, buf, 5);
}

/* The longest keys give 4 per page and a deep tree. */
static void test_deep(const char *path)
{
    u8 k[DEEPLEN];
    u8 got[DEEPLEN];
    ndx *x;
    u32 r;
    u32 prev;
    u16 i;
    u16 v;
    u16 j;
    i32 n;
    int ok;

    TF_EQ(ndx_create(path, "UPPER(DESCR)", DEEPLEN), DBF_OK);
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    g_lcg = 77UL;
    ok = 1;
    for (i = 0; i < NDEEP && ok; i++) {
        /* Keys that differ only at the end, inserted in shuffled order:
           entry v, two records per key. */
        v = (u16)((u32)i * 337u % NDEEP);
        deep_key(k, v);
        ok = TF_EQ(ndx_insert(x, k, v + 1u), DBF_OK);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    TF_CHECK(check_tree(path, 1, &n) >= 5);
    TF_EQ(n, NDEEP);

    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    n = 0;
    prev = 0;
    ok = TF_EQ(ndx_first(x, &r), DBF_OK);
    while (ok && r != 0) {
        ok = TF_EQ(r, prev + 1u);
        prev = r;
        n++;
        ok = ok && TF_EQ(ndx_next(x, &r), DBF_OK);
    }
    TF_EQ(n, NDEEP);
    /* Delete all but the last: every other entry from the front, then
       the rest in blocks of DEEPBLOCK, each from its high end, so that
       leaves empty at both ends of their parents.  Then walk backward. */
    ok = 1;
    for (i = 0; i + 1u < NDEEP && ok; i += 2) {
        deep_key(k, i);
        ok = TF_EQ(ndx_delete(x, k, i + 1u), DBF_OK);
    }
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_CHECK(check_tree(path, 1, &n) >= 5);
    TF_EQ(n, NDEEP / 2);
    for (i = 1; i + 1u < NDEEP && ok; i += 2 * DEEPBLOCK) {
        for (j = (u16)(i + 2 * DEEPBLOCK); j > i && ok; j -= 2) {
            if (j - 2u + 1u >= NDEEP)
                continue;
            deep_key(k, (u16)(j - 2u));
            ok = TF_EQ(ndx_delete(x, k, j - 1u), DBF_OK);
        }
        ok = ok && TF_EQ(ndx_flush(x), DBF_OK) &&
             TF_CHECK(check_tree(path, 1, &n) >= 1);
    }
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_CHECK(check_tree(path, 1, &n) >= 1);
    TF_EQ(n, 1);
    TF_EQ(ndx_last(x, &r), DBF_OK);
    TF_EQ(r, NDEEP);
    TF_EQ(ndx_prev(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_first(x, &r), DBF_OK);
    TF_EQ(r, NDEEP);
    memset(got, 'Z', DEEPLEN);
    TF_EQ(ndx_seek(x, got, 10, &r, &ok), DBF_OK);
    TF_EQ(r, NDEEP);
    TF_EQ(ok, 1);
    /* The last one too, leaving an empty root leaf; then one again. */
    deep_key(k, NDEEP - 1u);
    TF_EQ(ndx_delete(x, k, NDEEP), DBF_OK);
    TF_EQ(ndx_first(x, &r), DBF_OK);
    TF_EQ(r, 0);
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_EQ(check_tree(path, 1, &n), 1);
    TF_EQ(n, 0);
    TF_EQ(ndx_insert(x, k, 5), DBF_OK);
    TF_EQ(ndx_last(x, &r), DBF_OK);
    TF_EQ(r, 5);
    TF_EQ(ndx_close(x), DBF_OK);
}

/* Empty the rightmost leaf below the root's first child, NUNLINK_ROUNDS
   times: the leaf is unlinked, and the root's key for that child must
   fall to the child's new highest key. */
static void test_unlink(const char *path)
{
    u8 *k;
    u8 *b;
    ndx *x;
    u32 pg;
    u32 rec[NDX_PAGE / DEEPENT + 1];
    u16 i;
    u16 n;
    u16 round;
    i32 cnt;
    i32 left;
    int ok;

    k = (u8 *)malloc((size_t)DEEPLEN * 5u);
    if (!TF_CHECK(k != NULL))
        return;
    b = g_page;
    TF_EQ(ndx_create(path, "NAME", DEEPLEN), DBF_OK);
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL) {
        free(k);
        return;
    }
    ok = 1;
    for (i = 0; i < NUNLINK && ok; i++) {
        deep_key(k, (u16)(2u * i));
        ok = TF_EQ(ndx_insert(x, k, i + 1u), DBF_OK);
    }
    left = NUNLINK;
    for (round = 0; round < NUNLINK_ROUNDS && ok; round++) {
        TF_EQ(ndx_flush(x), DBF_OK);
        ok = TF_EQ(tfs_peek(path, 0, b, 8), 0);
        pg = get_le32(b);
        ok = ok && TF_EQ(tfs_peek(path, pg * NDX_PAGE, b, NDX_PAGE), 0);
        ok = ok && TF_CHECK(get_le16(b) >= 1 && get_le32(b + 4) != 0);
        if (!ok)
            break;
        pg = get_le32(b + 4);
        for (;;) {
            ok = TF_EQ(tfs_peek(path, pg * NDX_PAGE, b, NDX_PAGE), 0);
            if (!ok || get_le32(b + 4) == 0)
                break;
            pg = get_le32(b + 4 + (size_t)get_le16(b) * DEEPENT);
        }
        n = get_le16(b);
        ok = ok && TF_CHECK(n >= 1 && n <= 4);
        for (i = 0; i < n && ok; i++) {
            memcpy(k + (size_t)i * DEEPLEN, b + 4 + (size_t)i * DEEPENT + 8,
                   DEEPLEN);
            rec[i] = get_le32(b + 4 + (size_t)i * DEEPENT + 4);
        }
        for (i = 0; i < n && ok; i++)
            ok = TF_EQ(ndx_delete(x, k + (size_t)i * DEEPLEN, rec[i]),
                       DBF_OK);
        left -= n;
        TF_EQ(ndx_flush(x), DBF_OK);
        ok = ok && TF_CHECK(check_tree(path, 1, &cnt) >= 2);
        ok = ok && TF_EQ(cnt, left);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    free(k);
}

/* A header that names fewer pages than the file holds, as after a stop
   before ndx_flush: new pages must go after the existing ones. */
static void test_crash(const char *path)
{
    u8 k[KEYLEN];
    ndx *x;
    u32 r;
    u16 i;
    u32 size;
    i32 n;
    int ok;

    TF_EQ(ndx_create(path, "HEX(N)", KEYLEN), DBF_OK);
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    for (i = 1; i <= 200; i++) {
        make_key(i, k);
        TF_EQ(ndx_insert(x, k, i), DBF_OK);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    size = tfs_size(path);
    TF_CHECK(size != TFS_NOFILE && size > 4UL * NDX_PAGE);
    TF_EQ(tfs_peek(path, 0, g_page, 8), 0);
    TF_EQ(get_le32(g_page + 4), size / NDX_PAGE);
    /* A lagging page count; the root stays valid. */
    TF_EQ(poke32(path, 4, get_le32(g_page) + 1u), 0);

    TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK);
    if (x != NULL)
        TF_EQ(ndx_close(x), DBF_OK);
    TF_EQ(tfs_peek(path, 0, g_page, 8), 0);
    TF_EQ(get_le32(g_page + 4), get_le32(g_page) + 1u);

    /* A read-write open repairs it: closed with no change, the header
       counts every page of the file again and the tree walks whole. */
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x != NULL)
        TF_EQ(ndx_close(x), DBF_OK);
    TF_EQ(tfs_peek(path, 0, g_page, 8), 0);
    TF_EQ(get_le32(g_page + 4), size / NDX_PAGE);
    TF_CHECK(check_tree(path, 1, &n) >= 2);
    TF_EQ(n, 200);
    TF_EQ(poke32(path, 4, get_le32(g_page) + 1u), 0);

    /* A repair whose commit finds no free handle is committed by the
       next flush. */
    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x != NULL) {
        tfs_fault(TFS_OP(CF_OP_COMMIT), 1, 1, CF_EMFILE);
        TF_EQ(ndx_flush(x), DBF_EHANDLES);
        tfs_fault(0, 0, 0, 0);
        TF_EQ(ndx_flush(x), DBF_OK);
        tfs_fault_off();
        TF_EQ(tfs_count(CF_OP_COMMIT), 1);
        TF_EQ(ndx_close(x), DBF_OK);
    }
    TF_EQ(poke32(path, 4, get_le32(g_page) + 1u), 0);

    TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    ok = 1;
    for (i = 201; i <= 400 && ok; i++) {
        make_key(i, k);
        ok = TF_EQ(ndx_insert(x, k, i), DBF_OK);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    TF_CHECK(check_tree(path, 1, &n) >= 2);
    TF_EQ(n, 400);
    TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK);
    if (x == NULL)
        return;
    ok = TF_EQ(ndx_first(x, &r), DBF_OK);
    for (i = 1; i <= 400 && ok; i++) {
        ok = TF_EQ(r, i);
        ok = ok && TF_EQ(ndx_next(x, &r), DBF_OK);
    }
    TF_EQ(r, 0);
    TF_EQ(ndx_close(x), DBF_OK);
}

/* Walk forward through x; returns the first error, or DBF_OK, with
   *count entries seen.  DBF_EIO stands for a walk that does not end. */
static int walk_all(ndx *x, i32 *count)
{
    u32 r;
    int rc;

    *count = 0;
    rc = ndx_first(x, &r);
    while (rc == DBF_OK && r != 0) {
        if (++*count > WALKMAX)
            return DBF_EIO;
        rc = ndx_next(x, &r);
    }
    return rc;
}

/* Open a damaged copy: DBF_EFORMAT at open or during a walk. */
static int damaged_rc(const char *path)
{
    ndx *x;
    i32 n;
    u32 r;
    u8 k[KEYLEN];
    int exact;
    int rc;
    int rc2;

    rc = ndx_open(&x, path, DBF_RDWR);
    if (rc != DBF_OK)
        return rc;
    rc = walk_all(x, &n);
    if (rc == DBF_OK) {
        make_key(150, k);
        rc = ndx_seek(x, k, KEYLEN, &r, &exact);
        if (rc == DBF_OK)
            rc = ndx_insert(x, k, 9999);
    }
    rc2 = ndx_close(x);
    return rc != DBF_OK ? rc : rc2;
}

/* ndx_seek_after on the damaged index path after (n, n) gives
   DBF_EFORMAT and leaves no current entry. */
static void after_bad(const char *path, u16 n)
{
    u8 k[KEYLEN];
    ndx *x;
    u32 r;

    if (!TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK))
        return;
    make_key(n, k);
    TF_EQ(ndx_seek_after(x, k, n, &r), DBF_EFORMAT);
    TF_EQ(r, 0);
    TF_EQ(ndx_key(x, k), DBF_ERANGE);
    TF_EQ(ndx_close(x), DBF_OK);
}

static void test_damaged(const char *good, const char *bad)
{
    u8 k[KEYLEN];
    u8 buf[NDX_PAGE];
    ndx *x;
    u32 root;
    u32 leaf;
    u16 i;
    u32 size;

    /* A valid two-level index of 200 keys. */
    TF_EQ(ndx_create(good, "HEX(N)", KEYLEN), DBF_OK);
    TF_EQ(ndx_open(&x, good, DBF_RDWR), DBF_OK);
    if (x == NULL)
        return;
    for (i = 1; i <= 200; i++) {
        make_key(i, k);
        TF_EQ(ndx_insert(x, k, i), DBF_OK);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    size = tfs_size(good);
    TF_EQ(tfs_peek(good, 0, buf, 8), 0);
    root = get_le32(buf);
    TF_EQ(tfs_peek(good, root * NDX_PAGE, buf, NDX_PAGE), 0);
    TF_CHECK(get_le16(buf) >= 2);
    leaf = get_le32(buf + 4);

    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(damaged_rc(bad), DBF_OK);

    TF_EQ(copy_cut(good, bad, 100UL), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    TF_EQ(copy_cut(good, bad, 0UL), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);

    /* An expression with no end within the header. */
    TF_EQ(tfs_copy(good, bad), 0);
    memset(buf, 'A', sizeof buf);
    TF_EQ(tfs_poke(bad, 24, buf, NDX_PAGE - 24), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);

    /* A leaf count over the page capacity. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke16(bad, leaf * NDX_PAGE, TKPP + 1), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    after_bad(bad, 1);
    /* The same count on the root. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke16(bad, root * NDX_PAGE, 0xFFFF), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* A child outside the file. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke32(bad, root * NDX_PAGE + 4 + TENTSZ, 9999), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* A child pointing back at the root: a loop. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke32(bad, root * NDX_PAGE + 4 + TENTSZ, root), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* The root as its own first child: a loop on the way down. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke32(bad, root * NDX_PAGE + 4, root), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* A root with no key whose one child is itself, or outside the
       file. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke16(bad, root * NDX_PAGE, 0), 0);
    TF_EQ(poke32(bad, root * NDX_PAGE + 4, root), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    TF_EQ(poke32(bad, root * NDX_PAGE + 4, 9999), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* The same with a valid child: one leaf under a key-less root. */
    TF_EQ(poke32(bad, root * NDX_PAGE + 4, leaf), 0);
    TF_EQ(damaged_rc(bad), DBF_OK);
    /* Every child the same leaf: entries repeat. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(tfs_peek(bad, root * NDX_PAGE, buf, NDX_PAGE), 0);
    for (i = 0; i <= get_le16(buf); i++)
        put_le32(buf + 4 + i * TENTSZ, leaf);
    TF_EQ(tfs_poke(bad, root * NDX_PAGE, buf, NDX_PAGE), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* Leaf entries out of order. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(tfs_peek(bad, leaf * NDX_PAGE, buf, NDX_PAGE), 0);
    memcpy(buf + 4 + TENTSZ + 8, "ZZZZ", KEYLEN);
    TF_EQ(tfs_poke(bad, leaf * NDX_PAGE, buf, NDX_PAGE), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);
    /* An entry repeated: the step past it meets it again. */
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(tfs_peek(bad, leaf * NDX_PAGE, buf, NDX_PAGE), 0);
    memcpy(buf + 4 + 2 * TENTSZ, buf + 4 + TENTSZ, TENTSZ);
    TF_EQ(tfs_poke(bad, leaf * NDX_PAGE, buf, NDX_PAGE), 0);
    after_bad(bad, 2);
    /* The last page cut off. */
    TF_EQ(copy_cut(good, bad, size - NDX_PAGE), 0);
    TF_EQ(poke32(bad, 4, size / NDX_PAGE), 0);
    TF_EQ(damaged_rc(bad), DBF_EFORMAT);

    cf_remove(bad);
    TF_EQ(ndx_open(&x, bad, DBF_RDONLY), DBF_ENOENT);
    TF_CHECK(x == NULL);
}

/* An inner key above the highest key of its child, as a dBASE index
   that breaks I2 has it: an insert there descends to a leaf whose
   entries all lie below the new key, and locate must step on into the
   next leaf to find the position.  40 keys of STALEKEY bytes fill two
   leaves, the first ending at 00000160; the root's key for it is raised
   to 00000167, and 00000163 is inserted, deleted and inserted again. */
static void test_stale_inner(const char *path)
{
    char k[STALEKEY + 1];
    char prev[STALEKEY + 1];
    u8 buf[4];
    ndx *x;
    u32 root;
    u32 r;
    int n;
    int i;

    TF_EQ(ndx_create(path, "K", STALEKEY), DBF_OK);
    if (!TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK))
        return;
    for (i = 1; i <= 40; i++) {
        sprintf(k, "%08lu", (unsigned long)i * 10ul);
        TF_EQ(ndx_insert(x, (const u8 *)k, (u32)i), DBF_OK);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    TF_EQ(tfs_peek(path, 0, buf, sizeof buf), 0);
    root = get_le32(buf);
    TF_EQ(tfs_peek(path, root * NDX_PAGE + 12, k, STALEKEY), 0);
    TF_CHECK(memcmp(k, "00000160", STALEKEY) == 0);
    TF_EQ(tfs_poke(path, root * NDX_PAGE + 12, "00000167", STALEKEY), 0);

    if (!TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK))
        return;
    TF_EQ(ndx_insert(x, (const u8 *)"00000163", 99), DBF_OK);
    TF_EQ(ndx_delete(x, (const u8 *)"00000163", 99), DBF_OK);
    TF_EQ(ndx_insert(x, (const u8 *)"00000163", 99), DBF_OK);
    n = 0;
    prev[0] = '\0';
    k[STALEKEY] = '\0';
    TF_EQ(ndx_first(x, &r), DBF_OK);
    while (r != 0 && n <= 41) {
        n++;
        TF_EQ(ndx_key(x, (u8 *)k), DBF_OK);
        TF_CHECK(strcmp(prev, k) < 0);
        strcpy(prev, k);
        TF_EQ(ndx_next(x, &r), DBF_OK);
    }
    TF_EQ(n, 41);
    TF_EQ(ndx_close(x), DBF_OK);
}

/* A header for test_header: every field the open checks, and the
   open's result. */
typedef struct {
    u32 root;
    u32 npages;
    u16 keylen;
    u16 kpp;
    u16 type;
    u16 entsz;
    int rc;
} hdr_case;

/* The header of an empty index rewritten whole by each row of a table:
   a row is consistent except in the one field it puts out of range, or
   on the edge of its range, so that only that field's check decides the
   open.  An empty root page accepts every key length and keys per
   page.  The root at the page count shows only that a root at or above
   it is DBF_EFORMAT: page npages lies past the file's end, so a short
   read would refuse it too. */
static void test_header(const char *good, const char *bad)
{
    static const hdr_case cases[] = {
        { 1, 2, KEYLEN, TKPP, 0, TENTSZ, DBF_OK },
        { 0, 2, KEYLEN, TKPP, 0, TENTSZ, DBF_EFORMAT },
        { 2, 2, KEYLEN, TKPP, 0, TENTSZ, DBF_EFORMAT },
        { 1, 1, KEYLEN, TKPP, 0, TENTSZ, DBF_EFORMAT },
        { 1, NDX_MAXPAGE, KEYLEN, TKPP, 0, TENTSZ, DBF_OK },
        { 1, NDX_MAXPAGE + 1, KEYLEN, TKPP, 0, TENTSZ, DBF_EFORMAT },
        { 1, 2, 0, TKPP, 0, NDX_ENTSZ(0), DBF_EFORMAT },
        { 1, 2, NDX_MAXKEY, NDX_KPP(DEEPENT), 0, DEEPENT, DBF_OK },
        { 1, 2, NDX_MAXKEY + 1, NDX_KPP(NDX_ENTSZ(NDX_MAXKEY + 1)), 0,
          NDX_ENTSZ(NDX_MAXKEY + 1), DBF_EFORMAT },
        { 1, 2, KEYLEN, TKPP, 1, TENTSZ, DBF_EFORMAT },
        { 1, 2, KEYLEN, TKPP, 0, TENTSZ - 4, DBF_EFORMAT },
        { 1, 2, KEYLEN, TKPP + 1, 0, TENTSZ, DBF_EFORMAT },
        { 1, 2, KEYLEN, 2, 0, TENTSZ, DBF_OK },
        { 1, 2, KEYLEN, 1, 0, TENTSZ, DBF_EFORMAT }
    };
    u8 buf[NDX_H_EXPR];
    const hdr_case *c;
    ndx *x;

    TF_EQ(ndx_create(good, "HEX(N)", KEYLEN), DBF_OK);
    for (c = cases; c < cases + sizeof cases / sizeof cases[0]; c++) {
        TF_EQ(tfs_copy(good, bad), 0);
        TF_EQ(tfs_peek(bad, 0, buf, sizeof buf), 0);
        put_le32(buf + NDX_H_ROOT, c->root);
        put_le32(buf + NDX_H_PAGES, c->npages);
        put_le16(buf + NDX_H_KEYLEN, c->keylen);
        put_le16(buf + NDX_H_KPP, c->kpp);
        put_le16(buf + NDX_H_TYPE, c->type);
        put_le16(buf + NDX_H_ENTSZ, c->entsz);
        TF_EQ(tfs_poke(bad, 0, buf, sizeof buf), 0);
        if (!TF_EQ(ndx_open(&x, bad, DBF_RDONLY), c->rc))
            printf("  header case %d\n", (int)(c - cases));
        if (x != NULL)
            TF_EQ(ndx_close(x), DBF_OK);
    }
}

#ifdef COMPAT_HOST
/* A root of 0 is refused before the expression is allocated, so an
   allocation failing there too still gives DBF_EFORMAT.  n is the
   number of the expression's allocation in an open of the fresh index
   good, its last one. */
static void test_header_nomem(const char *good, const char *bad)
{
    ndx *x;
    u16 n;
    u16 held;

    held = tm_held();
    tm_fail(0);
    TF_EQ(ndx_open(&x, good, DBF_RDONLY), DBF_OK);
    n = tm_calls();
    if (x != NULL)
        TF_EQ(ndx_close(x), DBF_OK);
    tm_fail(n);
    TF_EQ(ndx_open(&x, good, DBF_RDONLY), DBF_ENOMEM);
    TF_EQ(tfs_copy(good, bad), 0);
    TF_EQ(poke32(bad, NDX_H_ROOT, 0), 0);
    tm_fail(n);
    TF_EQ(ndx_open(&x, bad, DBF_RDONLY), DBF_EFORMAT);
    tm_fail(0);
    TF_CHECK(x == NULL);
    TM_HELD(held);
}
#endif

/* A dBASE-written index and the table it was built on: the key is the
   raw text of one field, at byte off of each record. */
typedef struct {
    const char *ndx;
    const char *dbf;
    const char *expr;
    u16 off;
    u16 keylen;
    int height;
} golden;

static const golden goldens[] = {
    { "ITEMS1.NDX", "ITEMS.DBF", "STR(ITEM_ID,8)", 1, 8, 1 },
    { "BIG1.NDX", "BIG.DBF", "STR(ID,8)", 1, 8, 2 },
    { "BIG2.NDX", "BIG.DBF", "NAME", 9, 10, 2 },
    { "NUM993.NDX", "NUM993.DBF", "STR(ID,8)", 1, 8, 3 },
    { "NUM2000.NDX", "NUM2000.DBF", "STR(ID,8)", 1, 8, 3 },
    { "WIDE1.NDX", "WIDE.DBF", "NAME", 1, NDX_MAXKEY, 0 },
    { "WIDED1.NDX", "WIDED.DBF", "NAME", 1, NDX_MAXKEY, 0 }
};
#define NGOLDEN (sizeof goldens / sizeof goldens[0])

/* The key of record r of the table open on h (header length hl, record
   length rl) into out; returns 0 on success. */
static int table_key(int h, u16 hl, u16 rl, const golden *g, u32 r,
                     u8 *out)
{
    u16 got;

    if (cf_seek(h, (u32)hl + (r - 1u) * rl + g->off) != 0 ||
        cf_read(h, out, g->keylen, &got) != 0 || got != g->keylen)
        return -1;
    return 0;
}

/* Walk a golden index: every record once, with the key the table gives
   it, in key order; seek every record's key, which must land on the
   first entry with that key; and find it the dBASE way too. */
static void check_golden(const golden *g, const char *copy)
{
    char path[DOS_PATHMAX];
    u8 hdr[12];
    u8 k[NDX_MAXKEY];
    u8 want[NDX_MAXKEY];
    ndx *x;
    u32 count;
    u32 r;
    u32 r2;
    u32 i;
    u16 hl;
    u16 rl;
    i32 n;
    int h;
    int hn;
    int exact;
    int ok;

    strcpy(path, golden_path(g->dbf));
    if (!TF_EQ(tfs_peek(path, 0, hdr, sizeof hdr), 0))
        return;
    count = get_le32(hdr + 4);
    hl = get_le16(hdr + 8);
    rl = get_le16(hdr + 10);
    if (!TF_CHECK(count >= 1 && count <= NKEYS))
        return;
    if (!TF_EQ(cf_open(path, (u8)(CF_READ | CF_DENYNONE), &h), 0))
        return;
    strcpy(path, golden_path(g->ndx));
    n = -1;
    if (g->height != 0)
        TF_EQ(check_tree(path, 0, &n), g->height);
    else
        TF_CHECK(check_tree(path, 0, &n) >= 2);
    TF_EQ(n, count);

    TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK);
    if (x == NULL) {
        cf_close(h);
        return;
    }
    TF_STREQ(ndx_expr(x), g->expr);
    TF_EQ(ndx_keylen(x), g->keylen);
    TF_EQ(ndx_unique(x), 0);
    memset(g_gone, 0, (size_t)count + 1u);
    n = 0;
    ok = TF_EQ(ndx_first(x, &r), DBF_OK);
    while (ok && r != 0) {
        ok = TF_CHECK(r >= 1 && r <= count && !g_gone[(u16)r]);
        if (!ok)
            break;
        g_gone[(u16)r] = 1;
        TF_EQ(ndx_key(x, k), DBF_OK);
        ok = TF_EQ(table_key(h, hl, rl, g, r, want), 0);
        ok = ok && TF_CHECK(memcmp(k, want, g->keylen) == 0);
        n++;
        ok = ok && TF_EQ(ndx_next(x, &r), DBF_OK);
    }
    TF_EQ(n, count);

    if (!TF_EQ(cf_open(path, (u8)(CF_READ | CF_DENYNONE), &hn), 0))
        hn = -1;
    ok = 1;
    for (i = 1; i <= count && ok; i++) {
        ok = TF_EQ(table_key(h, hl, rl, g, i, want), 0);
        ok = ok && TF_EQ(ndx_seek(x, want, g->keylen, &r, &exact), DBF_OK);
        ok = ok && TF_EQ(exact, 1);
        ok = ok && TF_EQ(table_key(h, hl, rl, g, r, k), 0);
        ok = ok && TF_CHECK(memcmp(k, want, g->keylen) == 0);
        if (hn >= 0)
            ok = ok && TF_EQ(dbase_find(hn, want), r);
        /* The entry before is a lower key: r is the first of its key. */
        ok = ok && TF_EQ(ndx_prev(x, &r2), DBF_OK);
        if (ok && r2 != 0) {
            ok = TF_EQ(ndx_key(x, k), DBF_OK);
            ok = ok && TF_CHECK(memcmp(k, want, g->keylen) < 0);
        }
    }
    if (hn >= 0)
        cf_close(hn);
    TF_EQ(ndx_close(x), DBF_OK);

    /* A copy changed here: every third record's key removed and put
       back, which empties and refills leaves, then all checked again
       including the dBASE-style search. */
    TF_EQ(tfs_copy(path, copy), 0);
    TF_EQ(ndx_open(&x, copy, DBF_RDWR), DBF_OK);
    if (x == NULL) {
        cf_close(h);
        return;
    }
    ok = 1;
    for (i = 1; i <= count && ok; i += 3) {
        ok = TF_EQ(table_key(h, hl, rl, g, i, want), 0);
        ok = ok && TF_EQ(ndx_delete(x, want, i), DBF_OK);
    }
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_CHECK(check_tree(copy, 0, &n) >= 1);
    TF_EQ(n, count - (count + 2u) / 3u);
    for (i = 1; i <= count && ok; i += 3) {
        ok = TF_EQ(table_key(h, hl, rl, g, i, want), 0);
        ok = ok && TF_EQ(ndx_insert(x, want, i), DBF_OK);
    }
    TF_EQ(ndx_close(x), DBF_OK);
    TF_CHECK(check_tree(copy, 0, &n) >= 1);
    TF_EQ(n, count);
    if (TF_EQ(cf_open(copy, (u8)(CF_READ | CF_DENYNONE), &hn), 0)) {
        ok = 1;
        for (i = 1; i <= count && ok; i++) {
            ok = TF_EQ(table_key(h, hl, rl, g, i, want), 0);
            r = dbase_find(hn, want);
            ok = ok && TF_CHECK(r != 0);
            ok = ok && TF_EQ(table_key(h, hl, rl, g, r, k), 0);
            ok = ok && TF_CHECK(memcmp(k, want, g->keylen) == 0);
        }
        cf_close(hn);
    }
    cf_remove(copy);
    cf_close(h);
}

static void test_golden(const char *scratch)
{
    ndx *x;
    u16 i;

    for (i = 0; i < NGOLDEN; i++)
        check_golden(&goldens[i], scratch);

    /* BIG3: a date key, stored as numbers, is refused. */
    TF_EQ(ndx_open(&x, golden_path("BIG3.NDX"), DBF_RDONLY), DBF_EFORMAT);
    TF_CHECK(x == NULL);
}

/* ---------------------------------------------------------------------
 * Flush order: pages, then a commit when pages were added, then the
 * header, then a commit.
 * --------------------------------------------------------------------- */
static void test_flush_order(const char *path)
{
    u8 k[KEYLEN];
    ndx *x;
    u8 hdr[8];
    u32 before;
    u32 n;
    u16 i;

    TF_EQ(ndx_create(path, "KEY", KEYLEN), DBF_OK);
    if (!TF_EQ(ndx_open(&x, path, DBF_RDWR), DBF_OK))
        return;
    /* More keys than a leaf holds: the split adds pages. */
    for (i = 1; i <= TKPP + 5; i++) {
        make_key(i, k);
        TF_EQ(ndx_insert(x, k, i), DBF_OK);
    }
    TF_EQ(tfs_peek(path, 0, hdr, 8), 0);
    before = get_le32(hdr + 4);

    /* A failed size commit writes no header; one that finds no free
       handle is DBF_EHANDLES. */
    tfs_fault(TFS_OP(CF_OP_COMMIT), 1, 1, CF_EMFILE);
    TF_EQ(ndx_flush(x), DBF_EHANDLES);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_COMMIT), 1);
    TF_EQ(tfs_peek(path, 0, hdr, 8), 0);
    TF_EQ(get_le32(hdr + 4), before);

    tfs_fault(0, 0, 0, 0);
    TF_EQ(ndx_flush(x), DBF_OK);
    tfs_fault_off();
    n = tfs_calls();
    TF_CHECK(n >= 3 && n <= TFS_MAXOPS);
    TF_EQ(tfs_count(CF_OP_COMMIT), 2);
    if (n >= 3 && n <= TFS_MAXOPS) {
        TF_EQ(tfs_ops[(u16)n - 3], CF_OP_COMMIT);
        TF_EQ(tfs_ops[(u16)n - 2], CF_OP_WRITE);
        TF_EQ(tfs_ops[(u16)n - 1], CF_OP_COMMIT);
    }
    TF_EQ(tfs_peek(path, 0, hdr, 8), 0);
    TF_CHECK(get_le32(hdr + 4) > before);

    /* An insert that adds no page: its writes, then a single commit. */
    make_key(TKPP + 6, k);
    TF_EQ(ndx_insert(x, k, TKPP + 6), DBF_OK);
    tfs_fault(0, 0, 0, 0);
    TF_EQ(ndx_flush(x), DBF_OK);
    tfs_fault_off();
    n = tfs_calls();
    TF_CHECK(n >= 2 && n <= TFS_MAXOPS);
    TF_EQ(tfs_count(CF_OP_COMMIT), 1);
    if (n >= 2 && n <= TFS_MAXOPS)
        TF_EQ(tfs_ops[(u16)n - 1], CF_OP_COMMIT);

    /* The last commit maps its code the same way. */
    make_key(TKPP + 7, k);
    TF_EQ(ndx_insert(x, k, TKPP + 7), DBF_OK);
    tfs_fault(TFS_OP(CF_OP_COMMIT), 1, 1, CF_EMFILE);
    TF_EQ(ndx_flush(x), DBF_EHANDLES);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_COMMIT), 1);

    /* The next flush makes the failed commit again; one after it, with
       nothing written since, makes no disk call. */
    tfs_fault(0, 0, 0, 0);
    TF_EQ(ndx_flush(x), DBF_OK);
    TF_EQ(ndx_flush(x), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 1);
    TF_EQ(tfs_ops[0], CF_OP_COMMIT);
    TF_EQ(ndx_close(x), DBF_OK);
}

int main(int argc, char **argv)
{
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    ndx *x;
    int have_golden;

    tf_begin("T_NDX");
    g_vals = (u16 *)malloc(sizeof(u16) * NKEYS);
    g_perm = (u16 *)malloc(sizeof(u16) * NKEYS);
    g_gone = (u8 *)malloc(NKEYS + 1);
    g_page = (u8 *)malloc((size_t)MAXLEVELS * NDX_PAGE);
    if (!TF_CHECK(g_vals != NULL && g_perm != NULL && g_gone != NULL &&
                  g_page != NULL))
        return tf_done();
    tfs_set_dir(argc > 2 ? argv[2] : "TEST/GOLDEN/DB3");
    strcpy(g_gold, tfs_dir());
    tfs_set_dir(argc > 1 ? argv[1] : "");
    tfs_wipe(k_scratch);
    strcpy(a, tfs_path(k_scratch[0], ""));
    strcpy(b, tfs_path(k_scratch[1], ""));

    have_golden = argc > 2 ||
                  ndx_open(&x, golden_path("ITEMS1.NDX"), DBF_RDONLY) !=
                  DBF_ENOENT;
    if (have_golden && argc <= 2 && x != NULL)
        ndx_close(x);

    test_create(a);
    test_args(a);
    test_main(a);
    test_deep(a);
    test_unlink(a);
    test_crash(a);
    test_damaged(a, b);
    test_stale_inner(b);
    test_header(a, b);
#ifdef COMPAT_HOST
    test_header_nomem(a, b);
#endif
    test_flush_order(a);
    if (have_golden)
        test_golden(b);
    else
        printf("T_NDX: no golden files in %s, skipped\n", g_gold);

    tfs_wipe(k_scratch);
    free(g_vals);
    free(g_perm);
    free(g_gone);
    free(g_page);
    return tf_done();
}

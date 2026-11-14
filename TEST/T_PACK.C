/*
 * T_PACK.C - unit test for PACK and REINDEX (PACK.C).
 *
 * usage: T_PACK [dir [golden]]
 *
 * Works in dir (default the current directory).  Packs tables without
 * memos whose deleted records are at the start, in the middle, at the
 * end, everywhere or nowhere, and checks the remaining records, their
 * order and the counts; packs a table with two memo fields, edited memos
 * whose old blocks no record uses, memos across block boundaries and
 * memos of deleted records, and checks the rewritten block numbers, the
 * memo text, the exact size of the new memo file and its header; stops a
 * pack (with and without memos) and a reindex at every file call that
 * changes the disk, stops the recovery from each such state at each of
 * its own steps (two levels deep for a pack), and checks that the final
 * recovery leaves either the old or the new files complete, the right one
 * for where the stop came, and no temporary name; checks that
 * combinations no stop leaves are refused with every file untouched;
 * reindexes a TBIG-record table with duplicate keys and deleted records
 * and checks the file page by page and by walking it with ndx_*; checks
 * the unique flag, the stored expression and an interrupted index
 * replacement; compares our reindex of tables indexed by dBASE III Plus's
 * INDEX ON with dBASE's own index page by page, ignoring the bytes dBASE
 * leaves as stale memory, and walks each result checking every key
 * against its record, as it does for indexes dBASE kept up to date key by
 * key; and checks the error cases.  golden is the directory of the dBASE
 * files (default TEST/GOLDEN/DB3, or the program's own directory when
 * that holds BIG.DBF, as on a DOS test drive); without them those checks
 * are skipped.  Scratch files are removed at the start and at the end.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, else 1 (2 when dir or golden is too long).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"
#include "TESTFW.H"
#include "T_FILES.H"

/* Stops of the machine are simulated through the fault recorder of
   T_FILES.H (see "Interrupted packs"): from the stop-th disk-changing
   file call of any module on (counted from 0), every such call fails
   without effect, a write with CF_EFULL and any other with CF_EACCES. */
static cf_fault_fn g_record;    /* the recorder's own hook              */

/* The cf_fault hook while a stop is armed: the recorder's, with a
   failing write's code made CF_EFULL. */
static int stop_hook(int op)
{
    int rc;

    rc = g_record(op);
    return rc != 0 && op == CF_OP_WRITE ? CF_EFULL : rc;
}

/* Arm the stop at call stop, or with stop -1 only count the calls.
   tfs_fault_off disarms it; tfs_failop is then nonzero when the stop
   was reached. */
static void stop_at(i32 stop)
{
    tfs_fault(TFS_ALLOPS, stop < 0 ? 0 : (u32)stop + 1u, 0, CF_EACCES);
    g_record = cf_fault;
    cf_fault = stop_hook;
}

#define NSMALL      20      /* records of the tables without memos      */
#define TBIG        1200    /* records of the reindex test: at least
                               BIGVALS, more than the two levels of
                               STR(n,8) keys hold, and small enough for
                               the scratch files on a 360 KB floppy     */
#define BIGVALS     700     /* key values of the reindex test           */
#define KEYLEN      8
#define MEMOMAX     1600    /* longest memo text in the memo test       */
#define NMEMO       12      /* records of the memo test                 */
#define WALKMAX     10000L
#define LONGPATH    150     /* a path longer than PACK.C accepts        */

/* The golden directory as a prefix, separator included. */
static char g_gold[TFS_MAXDIR + 1];
static char g_gpath[DOS_PATHMAX];

/* Work buffers from malloc, out of the near data. */
static char *g_text;    /* MEMOMAX + 1                                  */
static char *g_rbuf;    /* MEMOMAX + 16                                 */
static u8 *g_pga;       /* two pages                                    */
static u16 *g_order;    /* TBIG entries: record numbers in key order    */
static u16 *g_count;    /* BIGVALS + 1 counters for the sort            */

/* The golden file name: the golden prefix and name, in g_gpath. */
static const char *golden_path(const char *name)
{
    strcpy(g_gpath, g_gold);
    strcat(g_gpath, name);
    return g_gpath;
}

/* The scratch file base + ext (ext with its dot) in out. */
static void ext_name(char *out, const char *base, const char *ext)
{
    strcpy(out, tfs_path(base, ext));
}

/* 1 when no temporary name of set base is left: T.$DB T.$DT T.%DB
   T.%DT of a table T.DBF, T.$NX T.%NX T.#NX of an index T.NDX. */
static int no_temps(const char *base)
{
    static const char *const ext[] = {
        ".$DB", ".$DT", ".%DB", ".%DT", ".$NX", ".%NX", ".#NX"
    };
    const char *p;
    int i;
    int ok;

    ok = 1;
    for (i = 0; i < (int)(sizeof ext / sizeof ext[0]); i++) {
        p = tfs_path(base, ext[i]);
        if (tfs_exists(p)) {
            printf("T_PACK: %s left behind\n", p);
            ok = 0;
        }
    }
    return ok;
}

/* ---------------------------------------------------------------------
 * Tables without memos.
 * --------------------------------------------------------------------- */

static dbf_field g_plain_fields[2] = {
    { "ID", 'N', 6, 0, 0 },
    { "NAME", 'C', 10, 0, 0 }
};

/* Create path with n records, record i having ID i and NAME "R<i>";
   record i is deleted when del(i) is nonzero. */
static int make_plain(const char *path, u16 n, int (*del)(u16))
{
    dbf *t;
    char s[12];
    u16 i;
    int rc;

    rc = dbf_create(path, g_plain_fields, 2);
    if (rc != DBF_OK)
        return rc;
    rc = dbf_open(&t, path, DBF_RDWR);
    if (rc != DBF_OK)
        return rc;
    for (i = 1; rc == DBF_OK && i <= n; i++) {
        dbf_blank(t);
        sprintf(s, "R%u", (unsigned)i);
        rc = dbf_put_num(t, 0, (i32)i);
        if (rc == DBF_OK)
            rc = dbf_put_str(t, 1, s);
        dbf_set_deleted(t, del(i));
        if (rc == DBF_OK)
            rc = dbf_append(t);
    }
    if (rc != DBF_OK) {
        dbf_close(t);
        return rc;
    }
    return dbf_close(t);
}

static int del_none(u16 i) { return i == 0; }
static int del_first(u16 i) { return i <= 3; }
static int del_middle(u16 i) { return i >= 9 && i <= 11; }
static int del_last(u16 i) { return i >= NSMALL - 1; }
static int del_all(u16 i) { return i > 0; }
static int del_mixed(u16 i) { return i == 1 || i % 3 == 0 || i == NSMALL; }

/* Pack a plain table and check its records. */
static void pack_plain(int (*del)(u16), const char *what)
{
    char p[DOS_PATHMAX];
    char s[12];
    dbf_packinfo info;
    dbf *t;
    i32 v;
    int blank;
    u16 i;
    u32 r;
    u32 kept;
    int ok;

    ext_name(p, "TP_PL", ".DBF");
    if (!TF_EQ(make_plain(p, NSMALL, del), DBF_OK))
        return;
    kept = 0;
    for (i = 1; i <= NSMALL; i++)
        if (!del(i))
            kept++;
    if (!TF_EQ(dbf_pack(p, &info), DBF_OK)) {
        printf("T_PACK: plain pack %s\n", what);
        return;
    }
    TF_EQ(info.kept, kept);
    TF_EQ(info.removed, NSMALL - kept);
    TF_EQ(info.memo_blocks_before, 0);
    TF_EQ(info.memo_blocks_after, 0);
    TF_CHECK(no_temps("TP_PL"));
    if (!TF_EQ(dbf_open(&t, p, DBF_RDONLY), DBF_OK))
        return;
    TF_EQ(dbf_reccount(t), kept);
    TF_EQ(dbf_version(t), 0x03);
    TF_EQ(tfs_size(p), (u32)dbf_hdrlen(t) + kept * dbf_reclen(t) + 1);
    r = 0;
    ok = 1;
    for (i = 1; i <= NSMALL && ok; i++) {
        if (del(i))
            continue;
        r++;
        ok = TF_EQ(dbf_read(t, r), DBF_OK);
        ok = ok && TF_CHECK(!dbf_is_deleted(t));
        ok = ok && TF_EQ(dbf_get_num(t, 0, &v, &blank), DBF_OK);
        ok = ok && TF_EQ(v, i);
        sprintf(s, "R%u", (unsigned)i);
        if (ok) {
            dbf_get_str(t, 1, g_rbuf, 16);
            ok = TF_STREQ(g_rbuf, s);
        }
    }
    if (!ok)
        printf("T_PACK: plain pack %s, record %lu\n", what,
               (unsigned long)r);
    dbf_close(t);
}

static void test_plain(void)
{
    char p[DOS_PATHMAX];

    pack_plain(del_none, "none deleted");
    pack_plain(del_first, "first deleted");
    pack_plain(del_middle, "middle deleted");
    pack_plain(del_last, "last deleted");
    pack_plain(del_all, "all deleted");
    pack_plain(del_mixed, "mixed");
    ext_name(p, "TP_PL", ".DBF");
    cf_remove(p);
}

/* ---------------------------------------------------------------------
 * Tables with memos.
 * --------------------------------------------------------------------- */

static dbf_field g_memo_fields[3] = {
    { "ID", 'N', 6, 0, 0 },
    { "NOTES", 'M', 10, 0, 0 },
    { "MORE", 'M', 10, 0, 0 }
};

/* Length of memo f of record i, 0 meaning a blank field (no memo). */
static u16 memo_len(u16 i, u16 f)
{
    static const u16 lens[NMEMO] = {
        5, 0, 510, 511, 600, 1, 1100, 0, 1600, 40, 300, 512
    };

    if (f == 1)
        return lens[i - 1];
    return (u16)(i % 4 == 0 ? 0 : 20 + i * 70);
}

/* Records deleted in the memo test. */
static int memo_del(u16 i)
{
    return i == 1 || i == 4 || i == 7 || i == NMEMO;
}

/* Text of memo f of record i, len bytes. */
static void memo_text(u16 i, u16 f, u16 len)
{
    u16 k;
    u8 c;

    for (k = 0; k < len; k++) {
        c = (u8)('A' + (i * 7u + f * 3u + k) % 26u);
        if (k % 64 == 63)
            c = '\n';
        g_text[k] = (char)c;
    }
    g_text[len] = '\0';
}

static u32 blocks_for(u16 len)
{
    return ((u32)len + 2u + DBT_BLOCK - 1u) / DBT_BLOCK;
}

/* Make the memo table.  Every memo is first written with other text,
   then rewritten, so the first copy's blocks are left unused, as a
   dBASE edit leaves them. */
static int make_memo(const char *dpath, const char *mpath)
{
    dbf *t;
    dbt *m;
    u32 blk;
    u16 i;
    u16 f;
    u16 len;
    int rc;

    rc = dbf_create(dpath, g_memo_fields, 3);
    if (rc == DBF_OK)
        rc = dbt_create(mpath);
    if (rc != DBF_OK)
        return rc;
    rc = dbf_open(&t, dpath, DBF_RDWR);
    if (rc != DBF_OK)
        return rc;
    rc = dbt_open(&m, mpath, DBF_RDWR);
    if (rc != DBF_OK) {
        dbf_close(t);
        return rc;
    }
    for (i = 1; rc == DBF_OK && i <= NMEMO; i++) {
        dbf_blank(t);
        rc = dbf_put_num(t, 0, (i32)i);
        for (f = 1; rc == DBF_OK && f <= 2; f++) {
            len = memo_len(i, f);
            if (len == 0)
                continue;
            rc = dbt_append(m, "old text", 8, &blk);
            memo_text(i, f, len);
            if (rc == DBF_OK)
                rc = dbt_append(m, g_text, len, &blk);
            if (rc == DBF_OK)
                rc = dbf_put_memo(t, f, blk);
        }
        dbf_set_deleted(t, memo_del(i));
        if (rc == DBF_OK)
            rc = dbt_flush(m);
        if (rc == DBF_OK)
            rc = dbf_append(t);
    }
    if (rc == DBF_OK)
        rc = dbt_close(m);
    else
        dbt_close(m);
    if (rc == DBF_OK)
        rc = dbf_close(t);
    else
        dbf_close(t);
    return rc;
}

static void test_memo(void)
{
    char d[DOS_PATHMAX];
    char mp[DOS_PATHMAX];
    dbf_packinfo info;
    dbf *t;
    dbt *m;
    u32 before;
    u32 next;
    u32 blk;
    u32 r;
    u32 kept;
    u32 lastend;
    i32 v;
    int blank;
    u16 i;
    u16 f;
    u16 len;
    u16 got;
    int ok;

    ext_name(d, "TP_ME", ".DBF");
    ext_name(mp, "TP_ME", ".DBT");
    if (!TF_EQ(make_memo(d, mp), DBF_OK))
        return;
    before = tfs_size(mp);
    if (!TF_EQ(dbf_pack(d, &info), DBF_OK))
        return;
    kept = 0;
    next = 1;
    lastend = DBT_BLOCK;
    for (i = 1; i <= NMEMO; i++) {
        if (memo_del(i))
            continue;
        kept++;
        for (f = 1; f <= 2; f++) {
            len = memo_len(i, f);
            if (len == 0)
                continue;
            lastend = next * DBT_BLOCK + len + 2u;
            next += blocks_for(len);
        }
    }
    TF_EQ(info.kept, kept);
    TF_EQ(info.removed, NMEMO - kept);
    TF_EQ(info.memo_blocks_before, (before + DBT_BLOCK - 1) / DBT_BLOCK);
    TF_EQ(info.memo_blocks_after, next);
    TF_CHECK(info.memo_blocks_after < info.memo_blocks_before);
    TF_EQ(tfs_size(mp), lastend);
    TF_CHECK(tfs_size(mp) < before);
    TF_CHECK(no_temps("TP_ME"));

    /* The header holds the next free block; the rest is zero. */
    if (TF_EQ(tfs_peek(mp, 0, g_pga, DBT_BLOCK), 0)) {
        TF_EQ(get_le32(g_pga), next);
        for (len = 4; len < DBT_BLOCK && g_pga[len] == 0; len++)
            ;
        TF_EQ(len, DBT_BLOCK);
    }

    if (!TF_EQ(dbf_open(&t, d, DBF_RDONLY), DBF_OK))
        return;
    if (!TF_EQ(dbt_open(&m, mp, DBF_RDONLY), DBF_OK)) {
        dbf_close(t);
        return;
    }
    TF_EQ(dbf_version(t), 0x83);
    TF_EQ(dbf_reccount(t), kept);
    r = 0;
    next = 1;
    ok = 1;
    for (i = 1; i <= NMEMO && ok; i++) {
        if (memo_del(i))
            continue;
        r++;
        ok = TF_EQ(dbf_read(t, r), DBF_OK);
        ok = ok && TF_EQ(dbf_get_num(t, 0, &v, &blank), DBF_OK);
        ok = ok && TF_EQ(v, i);
        for (f = 1; ok && f <= 2; f++) {
            len = memo_len(i, f);
            ok = TF_EQ(dbf_get_memo(t, f, &blk), DBF_OK);
            if (len == 0) {
                ok = ok && TF_EQ(blk, 0);
                continue;
            }
            /* Memos are laid out one after another in record order. */
            ok = ok && TF_EQ(blk, next);
            next += blocks_for(len);
            ok = ok && TF_EQ(dbt_read(m, blk, g_rbuf, MEMOMAX + 16, &got),
                             DBF_OK);
            memo_text(i, f, len);
            ok = ok && TF_EQ(got, len);
            ok = ok && TF_CHECK(memcmp(g_rbuf, g_text, len) == 0);
        }
    }
    if (!ok)
        printf("T_PACK: memo pack, record %lu\n", (unsigned long)r);
    dbt_close(m);
    dbf_close(t);

    /* An all-deleted memo table leaves only the header block. */
    if (TF_EQ(dbf_open(&t, d, DBF_RDWR), DBF_OK)) {
        for (r = 1; r <= dbf_reccount(t); r++) {
            dbf_read(t, r);
            dbf_set_deleted(t, 1);
            dbf_write(t);
        }
        TF_EQ(dbf_close(t), DBF_OK);
        TF_EQ(dbf_pack(d, &info), DBF_OK);
        TF_EQ(info.kept, 0);
        TF_EQ(info.removed, kept);
        TF_EQ(info.memo_blocks_after, 1);
        TF_EQ(tfs_size(mp), DBT_BLOCK);
    }
    cf_remove(d);
    cf_remove(mp);
}

/* ---------------------------------------------------------------------
 * Interrupted packs and reindexes.
 *
 * A stop is simulated through COMPAT.H's cf_fault hook: the file calls
 * that change the disk are counted, and from the chosen one on every
 * create, write, truncate, rename, removal, commit and close made by any
 * module fails without touching the disk, so the files are left exactly
 * as a machine that stopped at that instant leaves them.  Each such
 * state is saved, recovery itself is stopped at each of its own steps
 * (two levels deep), and a final recovery must then leave the old files
 * in place when the stop came before the commit point (no % file) and
 * the new ones after it, with no temporary name left.
 * --------------------------------------------------------------------- */

#define CR_DEPTH    2       /* levels of stops inside recovery          */
#define CR_MAXOPS   400     /* file calls of one run, an upper bound    */

/* The six names of a table: current, $ and % of table and memo file;
   and the four of an index: current, $, % and scratch. */
static const char *const cr_ext[] = {
    ".DBF", ".DBT", ".$DB", ".$DT", ".%DB", ".%DT"
};
static const char *const cr_xext[] = {
    ".NDX", ".$NX", ".%NX", ".#NX"
};

/* The snapshot set of cr_sweep at depth d, "TP_S<d>", in out. */
static void snap_name(char *out, int d)
{
    sprintf(out, "TP_S%d", d);
}

/* Remove every name of every set the tests make, the snapshots of
   every depth included. */
static void wipe_all(void)
{
    static const char *const sets[] = {
        "TP_PL", "TP_ME", "TP_OLD", "TP_NEW", "TP_CR", "TP_XC", "TP_XS",
        "TP_BIG", "TP_GOLD", "TP_ERR", "TP_NONE"
    };
    char snap[16];
    const char *base;
    int n;
    int i;
    int k;

    n = (int)(sizeof sets / sizeof sets[0]);
    for (i = 0; i < n + CR_DEPTH + 1; i++) {
        base = snap;
        if (i < n)
            base = sets[i];
        else
            snap_name(snap, i - n);
        for (k = 0; k < (int)(sizeof cr_ext / sizeof cr_ext[0]); k++)
            cf_remove(tfs_path(base, cr_ext[k]));
        for (k = 0; k < (int)(sizeof cr_xext / sizeof cr_xext[0]); k++)
            cf_remove(tfs_path(base, cr_xext[k]));
    }
}

static void cr_name(char *out, const char *base, int k)
{
    ext_name(out, base, cr_ext[k]);
}

/* Copy the n names ext[] of set from to set to; names missing in from
   are removed in to.  0 on success. */
static int copy_names(const char *from, const char *to,
                      const char *const *ext, int n)
{
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    int k;

    for (k = 0; k < n; k++) {
        ext_name(a, from, ext[k]);
        ext_name(b, to, ext[k]);
        cf_remove(b);
        if (tfs_exists(a) && tfs_copy(a, b) != 0)
            return 1;
    }
    return 0;
}

/* copy_names for the six names of a table set (cr_ext). */
static int cr_copy(const char *from, const char *to)
{
    return copy_names(from, to, cr_ext,
                      (int)(sizeof cr_ext / sizeof cr_ext[0]));
}

/* A small memo table for the stop tests, which keep several copies of
   it: six records, two deleted, memos of one to three blocks, one
   record without a memo and one memo edited (its first text left
   unused).  The memo file is a few blocks long. */
static int make_small_memo(const char *dpath, const char *mpath)
{
    static const u16 lens[6] = { 10, 600, 0, 30, 5, 1100 };
    dbf *t;
    dbt *m;
    u32 blk;
    u16 i;
    int rc;

    rc = dbf_create(dpath, g_memo_fields, 3);
    if (rc == DBF_OK)
        rc = dbt_create(mpath);
    if (rc == DBF_OK)
        rc = dbf_open(&t, dpath, DBF_RDWR);
    if (rc != DBF_OK)
        return rc;
    rc = dbt_open(&m, mpath, DBF_RDWR);
    if (rc != DBF_OK) {
        dbf_close(t);
        return rc;
    }
    for (i = 1; rc == DBF_OK && i <= 6; i++) {
        dbf_blank(t);
        rc = dbf_put_num(t, 0, (i32)i);
        if (rc == DBF_OK && i == 4)
            rc = dbt_append(m, "first", 5, &blk);
        if (rc == DBF_OK && lens[i - 1] > 0) {
            memo_text(i, 1, lens[i - 1]);
            rc = dbt_append(m, g_text, lens[i - 1], &blk);
            if (rc == DBF_OK)
                rc = dbf_put_memo(t, 1, blk);
        }
        dbf_set_deleted(t, i == 2 || i == 5);
        if (rc == DBF_OK)
            rc = dbt_flush(m);
        if (rc == DBF_OK)
            rc = dbf_append(t);
    }
    if (rc == DBF_OK)
        rc = dbt_close(m);
    else
        dbt_close(m);
    if (rc == DBF_OK)
        rc = dbf_close(t);
    else
        dbf_close(t);
    return rc;
}

/* What is in place in set base: 'O' the files of set "TP_OLD", 'N' those
   of "TP_NEW", '?' anything else.  memo: the table has a memo file. */
static char cr_inplace(const char *base, int memo)
{
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    char c[DOS_PATHMAX];
    char d[DOS_PATHMAX];

    cr_name(a, base, 0);
    cr_name(b, base, 1);
    cr_name(c, "TP_OLD", 0);
    cr_name(d, "TP_OLD", 1);
    if (tfs_same(a, c) && (!memo || tfs_same(b, d)))
        return 'O';
    cr_name(c, "TP_NEW", 0);
    cr_name(d, "TP_NEW", 1);
    if (tfs_same(a, c) && (!memo || tfs_same(b, d)))
        return 'N';
    return '?';
}

/* 1 if a % name of set base exists. */
static int cr_committed(const char *base)
{
    char p[DOS_PATHMAX];

    cr_name(p, base, 4);
    if (tfs_exists(p))
        return 1;
    cr_name(p, base, 5);
    return tfs_exists(p);
}

static int g_states;    /* stopped states examined                      */

/* From the state saved as set "TP_S<depth>", stop recovery at each of
   its steps (recursing to depth - 1), then check a recovery that runs
   to the end; want is 'O' or 'N'. */
static void cr_sweep(int depth, char want, int memo, const char *what)
{
    char snap[16];
    char p[DOS_PATHMAX];
    i32 j;
    int rc;
    int ok;

    snap_name(snap, depth);
    for (j = 0; j < CR_MAXOPS; j++) {
        if (!TF_EQ(cr_copy(snap, "TP_CR"), 0))
            return;
        g_states++;
        stop_at(depth > 0 ? j : -1);
        cr_name(p, "TP_CR", 0);
        rc = dbf_pack_recover(p);
        tfs_fault_off();
        if (tfs_failop() != 0) {
            snap_name(snap, depth - 1);
            if (!TF_EQ(cr_copy("TP_CR", snap), 0))
                return;
            cr_sweep(depth - 1, want, memo, what);
            snap_name(snap, depth);
            continue;
        }
        ok = TF_EQ(rc, DBF_OK);
        ok = ok && TF_EQ(cr_inplace("TP_CR", memo), want);
        ok = ok && TF_CHECK(no_temps("TP_CR"));
        if (!ok)
            printf("T_PACK: %s, recovery stopped at %ld (depth %d)\n",
                   what, (long)j, depth);
        return;
    }
    TF_CHECK(j < CR_MAXOPS);
}

/* Stop dbf_pack of a small table, with memos or without, at every step
   and check every recovery from there. */
static void test_pack_stops(int memo)
{
    char p[DOS_PATHMAX];
    char q[DOS_PATHMAX];
    char what[48];
    dbf_packinfo info;
    i32 c;
    int rc;
    char want;

    cr_name(p, "TP_OLD", 0);
    cr_name(q, "TP_OLD", 1);
    if (memo) {
        if (!TF_EQ(make_small_memo(p, q), DBF_OK))
            return;
    } else if (!TF_EQ(make_plain(p, NSMALL, del_mixed), DBF_OK)) {
        return;
    }
    /* The packed version, from a pack that is not stopped. */
    TF_EQ(cr_copy("TP_OLD", "TP_NEW"), 0);
    cr_name(p, "TP_NEW", 0);
    TF_EQ(dbf_pack(p, &info), DBF_OK);
    for (c = 0; c < CR_MAXOPS; c++) {
        TF_EQ(cr_copy("TP_OLD", "TP_CR"), 0);
        stop_at(c);
        cr_name(p, "TP_CR", 0);
        rc = dbf_pack(p, &info);
        tfs_fault_off();
        if (tfs_failop() == 0) {
            TF_EQ(rc, DBF_OK);
            TF_EQ(cr_inplace("TP_CR", memo), 'N');
            break;
        }
        want = (char)(cr_committed("TP_CR") ? 'N' : 'O');
        sprintf(what, "%s pack stopped at %ld", memo ? "memo" : "plain",
                (long)c);
        snap_name(q, CR_DEPTH);
        TF_EQ(cr_copy("TP_CR", q), 0);
        cr_sweep(CR_DEPTH, want, memo, what);
    }
    TF_CHECK(c > 2 && c < CR_MAXOPS);
    wipe_all();
}

/* Combinations no interrupted pack leaves: refused, nothing changed.
   Each letter is one of the six names: - absent, O the old file, N the
   new one, P the new one cut in half. */
#define NBADSTATE 16
static const char *const bad_states[NBADSTATE] = {
    "--N---",       /* a new table alone                                */
    "-ONN--",       /* new files but no table, before the commit point  */
    "O-NN--",       /* the old memo file missing                        */
    "OON-O-",       /* table, $ and % at once                           */
    "-O--O-",       /* a % table with nothing to put in place           */
    "-ONNOO",       /* memo renamed before the table                    */
    "OONNO-",       /* % table beside the old table                     */
    "NO--O-",       /* new table in place but no new memo file          */
    "-OPNO-",       /* the new table incomplete                         */
    "OON--O",       /* % memo file before the table was renamed         */
    "N----O",       /* % memo file only, table without its memo         */
    "-ONPO-",       /* incomplete new memo file at the commit point     */
    "NN-PO-",       /* incomplete $ memo file beside the new ones       */
    "OO-PO-",       /* % table copy with the old one in place           */
    "N--POO",       /* incomplete new memo file after its rename        */
    "NO-PO-"        /* new table in place, its memo file incomplete     */
};

/* Lay out state st in set "TP_CR" from sets "TP_OLD" and "TP_NEW". */
static int lay_state(const char *st)
{
    char p[DOS_PATHMAX];
    char a[DOS_PATHMAX];
    int k;
    int h;
    int rc;

    rc = 0;
    for (k = 0; rc == 0 && k < 6; k++) {
        cr_name(p, "TP_CR", k);
        cf_remove(p);
        if (st[k] == 'O')
            cr_name(a, "TP_OLD", k & 1);
        else if (st[k] == 'N' || st[k] == 'P')
            cr_name(a, "TP_NEW", k & 1);
        else
            continue;
        rc = tfs_copy(a, p);
        if (rc == 0 && st[k] == 'P' && cf_open(p, CF_RDWR, &h) == 0) {
            /* A new file cut in half, as a stop while it is being
               written leaves it (a memo file keeps its header). */
            rc = cf_truncate(h, tfs_size(a) / 2);
            cf_close(h);
        }
    }
    return rc;
}

static void test_bad_states(void)
{
    char p[DOS_PATHMAX];
    char a[DOS_PATHMAX];
    u32 size[6];
    dbf_packinfo info;
    int i;
    int k;
    int ok;

    cr_name(p, "TP_OLD", 0);
    cr_name(a, "TP_OLD", 1);
    if (!TF_EQ(make_small_memo(p, a), DBF_OK))
        return;
    TF_EQ(cr_copy("TP_OLD", "TP_NEW"), 0);
    cr_name(p, "TP_NEW", 0);
    TF_EQ(dbf_pack(p, &info), DBF_OK);
    for (i = 0; i < NBADSTATE; i++) {
        if (!TF_EQ(lay_state(bad_states[i]), 0))
            continue;
        for (k = 0; k < 6; k++) {
            cr_name(p, "TP_CR", k);
            size[k] = tfs_size(p);
        }
        cr_name(p, "TP_CR", 0);
        ok = TF_EQ(dbf_pack_recover(p), DBF_EFORMAT);
        ok = ok && TF_EQ(dbf_pack(p, &info), DBF_EFORMAT);
        for (k = 0; ok && k < 6; k++) {
            cr_name(p, "TP_CR", k);
            ok = TF_EQ(tfs_size(p), size[k]);
        }
        if (!ok)
            printf("T_PACK: state %s not refused untouched\n",
                   bad_states[i]);
    }
    /* No files at all: nothing to recover. */
    TF_EQ(lay_state("------"), 0);
    cr_name(p, "TP_CR", 0);
    TF_EQ(dbf_pack_recover(p), DBF_OK);
    wipe_all();
}

/* After a stopped reindex and a stopped recovery: a recovery run to
   the end leaves index p holding want entries and no temporary name. */
static int reindex_ok(const char *p, u32 want)
{
    ndx *x;
    u32 r;
    u32 n;
    int rc;
    int ok;

    ok = TF_EQ(ndx_reindex_recover(p), DBF_OK);
    n = 0;
    if (ok && TF_EQ(ndx_open(&x, p, DBF_RDONLY), DBF_OK)) {
        rc = ndx_first(x, &r);
        while (rc == DBF_OK && r != 0 && n < WALKMAX) {
            n++;
            rc = ndx_next(x, &r);
        }
        ndx_close(x);
    }
    ok = ok && TF_EQ(n, want);
    return ok && TF_CHECK(no_temps("TP_XC"));
}

/* Stop ndx_reindex at every step; the index must end as the old one
   (one stale entry) before the commit point and as the new one after,
   also when ndx_reindex_recover is itself stopped at any step. */
static void test_reindex_stops(void)
{
    char d[DOS_PATHMAX];
    char p[DOS_PATHMAX];
    char bak[DOS_PATHMAX];
    dbf *t;
    ndx *x;
    int nx;
    i32 c;
    i32 j;
    u32 want;
    int rc;

    nx = (int)(sizeof cr_xext / sizeof cr_xext[0]);
    ext_name(d, "TP_XC", ".DBF");
    ext_name(p, "TP_XC", ".NDX");
    ext_name(bak, "TP_XC", ".%NX");
    if (!TF_EQ(make_plain(d, NSMALL, del_mixed), DBF_OK))
        return;
    if (!TF_EQ(dbf_open(&t, d, DBF_RDONLY), DBF_OK))
        return;
    for (c = 0; c < CR_MAXOPS; c++) {
        TF_EQ(ndx_create(p, "NAME", 10), DBF_OK);
        if (TF_EQ(ndx_open(&x, p, DBF_RDWR), DBF_OK)) {
            TF_EQ(ndx_insert(x, (const u8 *)"stale     ", 1), DBF_OK);
            ndx_close(x);
        }
        stop_at(c);
        rc = ndx_reindex(t, p, NULL);
        tfs_fault_off();
        if (tfs_failop() == 0) {
            TF_EQ(rc, DBF_OK);
            TF_CHECK(reindex_ok(p, NSMALL));
            break;
        }
        want = tfs_exists(bak) ? NSMALL : 1;
        TF_EQ(copy_names("TP_XC", "TP_XS", cr_xext, nx), 0);
        for (j = 0; j < CR_MAXOPS; j++) {
            g_states++;
            TF_EQ(copy_names("TP_XS", "TP_XC", cr_xext, nx), 0);
            stop_at(j);
            rc = ndx_reindex_recover(p);
            tfs_fault_off();
            if (!reindex_ok(p, want))
                printf("T_PACK: reindex stopped at %ld, recovery at %ld\n",
                       (long)c, (long)j);
            if (tfs_failop() == 0)
                break;
        }
    }
    TF_CHECK(c > 2 && c < CR_MAXOPS);
    dbf_close(t);
    wipe_all();
}

/* ---------------------------------------------------------------------
 * Reindex.
 * --------------------------------------------------------------------- */

static dbf_field g_num_fields[1] = {
    { "ID", 'N', 8, 0, 0 }
};

static u16 big_val(u16 i)
{
    return (u16)(((u32)i * 389UL + 17UL) % (u32)BIGVALS);
}

static int big_del(u16 i)
{
    return i % 13 == 0;
}

/* Table of TBIG records with ID = big_val(i), some deleted. */
static int make_big(const char *path)
{
    dbf *t;
    u16 i;
    int rc;

    rc = dbf_create(path, g_num_fields, 1);
    if (rc == DBF_OK)
        rc = dbf_open(&t, path, DBF_RDWR);
    if (rc != DBF_OK)
        return rc;
    for (i = 1; rc == DBF_OK && i <= TBIG; i++) {
        dbf_blank(t);
        rc = dbf_put_num(t, 0, (i32)big_val(i));
        dbf_set_deleted(t, big_del(i));
        if (rc == DBF_OK)
            rc = dbf_append(t);
    }
    if (rc != DBF_OK) {
        dbf_close(t);
        return rc;
    }
    return dbf_close(t);
}

/* Sort record numbers 1..TBIG by (value, record) into g_order[], with a
   counting sort over the values; with unique, keep only the first record
   of each value.  A 0 follows the last entry. */
static void sort_order(int unique)
{
    u16 v;
    u16 i;
    u16 n;
    u16 c;

    memset(g_count, 0, sizeof(u16) * (BIGVALS + 1));
    for (i = 1; i <= TBIG; i++)
        g_count[big_val(i) + 1]++;
    n = 0;
    for (v = 1; v <= BIGVALS; v++) {
        c = g_count[v];
        g_count[v] = n;
        n = (u16)(n + c);
    }
    /* g_count[v + 1] is where the records of value v go. */
    for (i = 1; i <= TBIG; i++)
        g_order[g_count[big_val(i) + 1]++] = i;
    if (unique) {
        n = 1;
        for (i = 1; i < TBIG; i++)
            if (big_val(g_order[i]) != big_val(g_order[n - 1]))
                g_order[n++] = g_order[i];
    }
    g_order[n] = 0;
}

/* Check the pages of a reindexed STR(ID,8) file: leaves in file order
   hold the entries of g_order[] in sequence, all full but the last; the
   root is the last page; inner entries carry record 0. */
static void check_layout(const char *path, u16 nent)
{
    u32 root;
    u32 npages;
    u32 pg;
    u16 n;
    u16 k;
    u16 at;
    u16 kpp;
    u16 entsz;
    u8 *e;
    char want[KEYLEN + 1];
    int ok;
    int lastleaf;

    if (!TF_EQ(tfs_peek(path, 0, g_pga, NDX_PAGE), 0))
        return;
    root = get_le32(g_pga);
    npages = get_le32(g_pga + 4);
    kpp = get_le16(g_pga + 14);
    entsz = get_le16(g_pga + 18);
    ok = TF_EQ(get_le16(g_pga + 12), KEYLEN);
    ok = ok && TF_EQ(kpp, 31);
    ok = ok && TF_EQ(entsz, 16);
    ok = ok && TF_EQ(npages * NDX_PAGE, tfs_size(path));
    ok = ok && TF_EQ(root, npages - 1);
    at = 0;
    lastleaf = 0;
    for (pg = 1; ok && pg < npages; pg++) {
        ok = TF_EQ(tfs_peek(path, pg * NDX_PAGE, g_pga, NDX_PAGE), 0);
        n = get_le16(g_pga);
        e = g_pga + 4;
        if (n > 0 && get_le32(e) != 0) {
            for (k = 0; ok && k < n; k++)
                ok = TF_EQ(get_le32(g_pga + 4 + k * entsz + 4), 0);
            continue;
        }
        ok = TF_CHECK(!lastleaf);
        if (n < kpp)
            lastleaf = 1;
        for (k = 0; ok && k < n; k++, at++) {
            e = g_pga + 4 + k * entsz;
            ok = TF_CHECK(at < nent);
            ok = ok && TF_EQ(get_le32(e + 4), g_order[at]);
            sprintf(want, "%8u", (unsigned)big_val(g_order[at]));
            ok = ok && TF_CHECK(memcmp(e + 8, want, KEYLEN) == 0);
        }
    }
    TF_EQ(at, nent);
}

/* Walk the index with ndx_* and compare with g_order[]. */
static void check_walk(const char *path, u16 nent)
{
    ndx *x;
    u32 r;
    u16 at;
    u16 j;
    int rc;
    int ex;
    char want[KEYLEN + 1];

    if (!TF_EQ(ndx_open(&x, path, DBF_RDONLY), DBF_OK))
        return;
    at = 0;
    rc = ndx_first(x, &r);
    while (rc == DBF_OK && r != 0 && at < nent && r == g_order[at]) {
        at++;
        rc = ndx_next(x, &r);
    }
    TF_EQ(rc, DBF_OK);
    TF_EQ(at, nent);
    TF_EQ(r, 0);
    at = nent;
    rc = ndx_last(x, &r);
    while (rc == DBF_OK && r != 0 && at > 0 && r == g_order[at - 1]) {
        at--;
        rc = ndx_prev(x, &r);
    }
    TF_EQ(rc, DBF_OK);
    TF_EQ(at, 0);
    for (at = 0; at < nent; at += 97) {
        sprintf(want, "%8u", (unsigned)big_val(g_order[at]));
        rc = ndx_seek(x, (const u8 *)want, KEYLEN, &r, &ex);
        TF_EQ(rc, DBF_OK);
        TF_CHECK(ex);
        /* The first entry of a run of equal keys. */
        j = at;
        while (j > 0 && big_val(g_order[j - 1]) == big_val(g_order[at]))
            j--;
        TF_EQ(r, g_order[j]);
    }
    ndx_close(x);
}

static void test_reindex(void)
{
    char d[DOS_PATHMAX];
    char p[DOS_PATHMAX];
    char q[DOS_PATHMAX];
    dbf *t;
    ndx *x;
    u32 k;
    u16 n;
    u8 one;

    ext_name(d, "TP_BIG", ".DBF");
    ext_name(p, "TP_BIG", ".NDX");
    if (!TF_EQ(make_big(d), DBF_OK))
        return;
    if (!TF_EQ(dbf_open(&t, d, DBF_RDONLY), DBF_OK))
        return;
    /* An index holding stale entries is rebuilt from scratch; the
       expression is kept as stored, trailing space included. */
    TF_EQ(ndx_create(p, "STR(ID,8) ", KEYLEN), DBF_OK);
    if (TF_EQ(ndx_open(&x, p, DBF_RDWR), DBF_OK)) {
        TF_EQ(ndx_insert(x, (const u8 *)"stale!!!", 9999), DBF_OK);
        ndx_close(x);
    }
    TF_EQ(ndx_reindex(t, p, &k), DBF_OK);
    TF_EQ(k, TBIG);
    TF_CHECK(no_temps("TP_BIG"));
    sort_order(0);
    check_layout(p, TBIG);
    if (TF_EQ(tfs_peek(p, 0, g_pga, NDX_PAGE), 0)) {
        TF_CHECK(memcmp(g_pga + 24, "STR(ID,8) ", 11) == 0);
        TF_EQ(g_pga[23], 0);
    }
    check_walk(p, TBIG);

    /* Unique flag: only the first record of each key, flag kept. */
    one = 1;
    TF_EQ(tfs_poke(p, 23, &one, 1), 0);
    TF_EQ(ndx_reindex(t, p, NULL), DBF_OK);
    sort_order(1);
    for (n = 0; g_order[n] != 0; n++)
        ;
    TF_EQ(n, BIGVALS);
    check_layout(p, n);
    if (TF_EQ(ndx_open(&x, p, DBF_RDONLY), DBF_OK)) {
        TF_EQ(ndx_unique(x), 1);
        ndx_close(x);
    }
    check_walk(p, n);

    /* An interrupted replacement: old index renamed to %NX, the new one
       complete as $NX.  The next reindex finishes it first. */
    ext_name(q, "TP_BIG", ".%NX");
    TF_EQ(tfs_copy(p, q), 0);
    ext_name(q, "TP_BIG", ".$NX");
    TF_EQ(cf_rename(p, q), 0);
    ext_name(q, "TP_BIG", ".#NX");
    TF_EQ(tfs_copy(d, q), 0);
    TF_EQ(ndx_reindex(t, p, NULL), DBF_OK);
    TF_CHECK(no_temps("TP_BIG"));
    check_walk(p, n);

    dbf_close(t);
    cf_remove(p);
    cf_remove(d);
}

/* A table packed, then reindexed: the index follows the new numbers. */
static void test_pack_reindex(void)
{
    char d[DOS_PATHMAX];
    char p[DOS_PATHMAX];
    dbf_packinfo info;
    dbf *t;
    ndx *x;
    u32 r;
    u32 n;
    u32 k;
    int rc;
    int ex;

    ext_name(d, "TP_PL", ".DBF");
    ext_name(p, "TP_PL", ".NDX");
    if (!TF_EQ(make_plain(d, NSMALL, del_mixed), DBF_OK))
        return;
    TF_EQ(ndx_create(p, "NAME", 10), DBF_OK);
    TF_EQ(dbf_pack(d, &info), DBF_OK);
    if (TF_EQ(dbf_open(&t, d, DBF_RDONLY), DBF_OK)) {
        TF_EQ(ndx_reindex(t, p, &k), DBF_OK);
        TF_EQ(k, info.kept);
        if (TF_EQ(ndx_open(&x, p, DBF_RDONLY), DBF_OK)) {
            n = 0;
            rc = ndx_first(x, &r);
            TF_EQ(rc, DBF_OK);
            while (rc == DBF_OK && r != 0 && n < WALKMAX) {
                n++;
                rc = ndx_next(x, &r);
            }
            TF_EQ(n, info.kept);
            TF_EQ(ndx_seek(x, (const u8 *)"R2        ", 10, &r, &ex),
                  DBF_OK);
            TF_CHECK(ex);
            TF_EQ(dbf_read(t, r), DBF_OK);
            dbf_get_str(t, 1, g_rbuf, 16);
            TF_STREQ(g_rbuf, "R2");
            ndx_close(x);
        }
        dbf_close(t);
    }
    cf_remove(p);
    cf_remove(d);
}

/* ---------------------------------------------------------------------
 * dBASE INDEX ON results.
 * --------------------------------------------------------------------- */

/* Compare two index files page by page, looking only at the bytes
   dBASE defines: the header up to the expression's NUL, and in each
   page the count, its entries' page, record and key bytes, and the
   rightmost child's page number of an inner page. */
static int same_structure(const char *ours, const char *theirs)
{
    u8 *a;
    u8 *b;
    u32 npages;
    u32 pg;
    u16 n;
    u16 k;
    u16 keylen;
    u16 kpp;
    u16 entsz;
    u16 used;
    int ok;

    a = g_pga;
    b = g_pga + NDX_PAGE;
    if (tfs_size(ours) != tfs_size(theirs)) {
        printf("T_PACK: %s: size %lu, dBASE %lu\n", ours,
               (unsigned long)tfs_size(ours),
               (unsigned long)tfs_size(theirs));
        return 0;
    }
    ok = tfs_peek(ours, 0, a, NDX_PAGE) == 0 &&
         tfs_peek(theirs, 0, b, NDX_PAGE) == 0;
    for (used = 24; ok && used < NDX_PAGE && b[used] != 0; used++)
        ;
    ok = ok && memcmp(a, b, (size_t)used + 1) == 0;
    if (!ok)
        printf("T_PACK: %s: header differs from dBASE\n", ours);
    npages = get_le32(b + 4);
    keylen = get_le16(b + 12);
    kpp = get_le16(b + 14);
    entsz = get_le16(b + 18);
    for (pg = 1; ok && pg < npages; pg++) {
        ok = tfs_peek(ours, pg * NDX_PAGE, a, NDX_PAGE) == 0 &&
             tfs_peek(theirs, pg * NDX_PAGE, b, NDX_PAGE) == 0;
        n = get_le16(b);
        ok = ok && get_le16(a) == n && n <= kpp;
        for (k = 0; ok && k < n; k++)
            ok = memcmp(a + 4 + k * entsz, b + 4 + k * entsz,
                        (size_t)(8 + keylen)) == 0;
        if (ok && n > 0 && get_le32(b + 4) != 0)
            ok = memcmp(a + 4 + n * entsz, b + 4 + n * entsz, 4) == 0;
        if (!ok)
            printf("T_PACK: %s: page %lu differs from dBASE\n", ours,
                   (unsigned long)pg);
    }
    return ok;
}

/* Walk index p of table t with ndx_*: every record once, each entry's
   key equal to the expression evaluated on its record, in strictly
   increasing (key, record) order. */
static int walk_eval(const char *p, dbf *t)
{
    ndx *x;
    kx *k;
    u8 *key;
    u8 *ev;
    u8 *prev;
    u32 r;
    u32 last;
    u32 n;
    u16 len;
    int ok;
    int c;

    if (!TF_EQ(ndx_open(&x, p, DBF_RDONLY), DBF_OK))
        return 0;
    ok = TF_EQ(kx_compile(&k, t, ndx_expr(x)), DBF_OK);
    if (!ok) {
        ndx_close(x);
        return 0;
    }
    len = ndx_keylen(x);
    key = (u8 *)g_rbuf;
    ev = key + NDX_MAXKEY;
    prev = ev + NDX_MAXKEY;
    n = 0;
    last = 0;
    ok = TF_EQ(ndx_first(x, &r), DBF_OK);
    while (ok && r != 0 && n <= dbf_reccount(t)) {
        n++;
        ok = TF_EQ(ndx_key(x, key), DBF_OK);
        ok = ok && TF_EQ(dbf_read(t, r), DBF_OK);
        ok = ok && TF_EQ(kx_eval(k, t, ev), DBF_OK);
        ok = ok && TF_CHECK(memcmp(key, ev, len) == 0);
        if (ok && n > 1) {
            c = memcmp(prev, key, len);
            ok = TF_CHECK(c < 0 || (c == 0 && last < r));
        }
        memcpy(prev, key, len);
        last = r;
        if (ok)
            ok = TF_EQ(ndx_next(x, &r), DBF_OK);
    }
    ok = ok && TF_EQ(n, dbf_reccount(t));
    kx_free(k);
    ndx_close(x);
    return ok;
}

/* Reindex a scratch copy of dBASE's table and index and walk it; with
   onepass (dBASE built the index by INDEX ON, which lays pages out as
   ndx_reindex does) also compare it with dBASE's index.  0 when the
   golden pair is absent. */
static int golden_case(const char *table, const char *index, int onepass)
{
    char gd[DOS_PATHMAX];
    char gx[DOS_PATHMAX];
    char d[DOS_PATHMAX];
    char p[DOS_PATHMAX];
    dbf *t;
    int ok;

    strcpy(gd, golden_path(table));
    strcpy(gx, golden_path(index));
    if (!tfs_exists(gd) || !tfs_exists(gx))
        return 0;
    ext_name(d, "TP_GOLD", ".DBF");
    ext_name(p, "TP_GOLD", ".NDX");
    ok = TF_EQ(tfs_copy(gd, d), 0) && TF_EQ(tfs_copy(gx, p), 0);
    ok = ok && TF_EQ(dbf_open(&t, d, DBF_RDONLY), DBF_OK);
    if (ok) {
        ok = TF_EQ(ndx_reindex(t, p, NULL), DBF_OK);
        if (ok && onepass && !TF_CHECK(same_structure(p, gx)))
            printf("T_PACK: reindex of %s differs from dBASE's %s\n",
                   table, index);
        if (ok && !walk_eval(p, t))
            printf("T_PACK: walk of the reindexed %s failed\n", index);
        dbf_close(t);
    }
    cf_remove(d);
    cf_remove(p);
    return 1;
}

static void test_golden(void)
{
    int n;

    if (!golden_case("BIG.DBF", "BIG1.NDX", 1)) {
        printf("T_PACK: no golden files in %s, skipped\n", g_gold);
        return;
    }
    n = 1;
    n += golden_case("NUM993.DBF", "NUM993.NDX", 1);
    n += golden_case("NUM2000.DBF", "NUM2000.NDX", 1);
    n += golden_case("WIDE.DBF", "WIDE1.NDX", 1);
    n += golden_case("ITEMS.DBF", "ITEMS1.NDX", 1);
    printf("T_PACK: %d dBASE indexes compared\n", n);
    /* Indexes dBASE kept up to date key by key (a character key over a
       two-level table; one with emptied pages left outside the tree):
       their pages differ from a one-pass build, so they are only
       reindexed and walked. */
    golden_case("BIG.DBF", "BIG2.NDX", 0);
    golden_case("WIDED.DBF", "WIDED1.NDX", 0);
}

/* ---------------------------------------------------------------------
 * Errors.
 * --------------------------------------------------------------------- */

static void test_errors(void)
{
    char d[DOS_PATHMAX];
    char mp[DOS_PATHMAX];
    char p[DOS_PATHMAX];
    char lng[LONGPATH + 1];
    dbf_packinfo info;
    dbf *t;
    u32 size;
    u32 k;
    int h;

    ext_name(d, "TP_NONE", ".DBF");
    TF_EQ(dbf_pack(d, &info), DBF_ENOENT);
    TF_EQ(dbf_pack(NULL, &info), DBF_EARG);
    TF_EQ(dbf_pack_recover(NULL), DBF_EARG);
    memset(lng, 'A', LONGPATH);
    lng[LONGPATH] = '\0';
    TF_EQ(dbf_pack(lng, &info), DBF_EARG);
    TF_EQ(dbf_pack_recover(lng), DBF_EARG);
    TF_CHECK(no_temps("TP_NONE"));

    /* A memo table whose memo file is missing is left alone. */
    ext_name(d, "TP_ME", ".DBF");
    ext_name(mp, "TP_ME", ".DBT");
    TF_EQ(make_memo(d, mp), DBF_OK);
    TF_EQ(cf_remove(mp), 0);
    size = tfs_size(d);
    TF_EQ(dbf_pack(d, &info), DBF_ENOENT);
    TF_EQ(tfs_size(d), size);
    TF_CHECK(no_temps("TP_ME"));

    /* A memo pointer past the end of the memo file. */
    TF_EQ(make_memo(d, mp), DBF_OK);
    if (TF_EQ(dbf_open(&t, d, DBF_RDWR), DBF_OK)) {
        TF_EQ(dbf_read(t, 2), DBF_OK);
        TF_EQ(dbf_put_memo(t, 1, 5000), DBF_OK);
        TF_EQ(dbf_write(t), DBF_OK);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    size = tfs_size(mp);
    TF_EQ(dbf_pack(d, &info), DBF_EFORMAT);
    TF_EQ(tfs_size(mp), size);
    TF_CHECK(no_temps("TP_ME"));

    /* A memo without its terminator at the end of the file: the last
       memo written belongs to the last record, undeleted here. */
    TF_EQ(make_memo(d, mp), DBF_OK);
    if (TF_EQ(dbf_open(&t, d, DBF_RDWR), DBF_OK)) {
        TF_EQ(dbf_read(t, NMEMO), DBF_OK);
        dbf_set_deleted(t, 0);
        TF_EQ(dbf_write(t), DBF_OK);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    if (TF_EQ(cf_open(mp, CF_RDWR, &h), 0)) {
        TF_EQ(cf_size(h, &size), 0);
        TF_EQ(cf_truncate(h, size - 2), 0);
        cf_close(h);
    }
    TF_EQ(dbf_pack(d, &info), DBF_EFORMAT);
    TF_CHECK(no_temps("TP_ME"));
    cf_remove(d);
    cf_remove(mp);

    /* Reindex errors. */
    ext_name(d, "TP_PL", ".DBF");
    ext_name(p, "TP_ERR", ".NDX");
    TF_EQ(make_plain(d, 5, del_none), DBF_OK);
    if (TF_EQ(dbf_open(&t, d, DBF_RDONLY), DBF_OK)) {
        k = 1;
        TF_EQ(ndx_reindex(t, p, &k), DBF_ENOENT);
        TF_EQ(k, 0);
        TF_EQ(ndx_reindex(t, NULL, NULL), DBF_EARG);
        TF_EQ(ndx_reindex(NULL, p, NULL), DBF_EARG);
        /* Key length differs from the expression's. */
        TF_EQ(ndx_create(p, "STR(ID,8)", 9), DBF_OK);
        size = tfs_size(p);
        TF_EQ(ndx_reindex(t, p, NULL), DBF_EFORMAT);
        TF_EQ(tfs_size(p), size);
        /* An expression naming a field the table lacks. */
        TF_EQ(ndx_create(p, "NOSUCH", 6), DBF_OK);
        TF_EQ(ndx_reindex(t, p, NULL), DBF_EARG);
        TF_CHECK(tfs_exists(p));
        /* Not an index. */
        TF_EQ(tfs_copy(d, p), 0);
        TF_CHECK(ndx_reindex(t, p, NULL) != DBF_OK);
        TF_CHECK(no_temps("TP_ERR"));
        dbf_close(t);
    }
    cf_remove(p);
    cf_remove(d);
}

int main(int argc, char **argv)
{
    size_t n;
    const char *self;

    tf_begin("T_PACK");
    g_text = (char *)malloc(MEMOMAX + 1);
    g_rbuf = (char *)malloc(MEMOMAX + 16);
    g_pga = (u8 *)malloc(2 * NDX_PAGE);
    g_order = (u16 *)malloc(sizeof(u16) * (TBIG + 1));
    g_count = (u16 *)malloc(sizeof(u16) * (BIGVALS + 1));
    if (!TF_CHECK(g_text != NULL && g_rbuf != NULL && g_pga != NULL &&
                  g_order != NULL && g_count != NULL))
        return tf_done();
    dbf_set_today(2026, 10, 8);
    tfs_set_dir(argc > 2 ? argv[2] : "TEST/GOLDEN/DB3");
    strcpy(g_gold, tfs_dir());
    if (argc <= 2) {
        self = argc > 0 ? argv[0] : "";
        n = strlen(self);
        while (n > 0 && self[n - 1] != '\\' && self[n - 1] != '/' &&
               self[n - 1] != ':')
            n--;
        if (n > 0 && n <= TFS_MAXDIR) {
            memcpy(g_gold, self, n);
            g_gold[n] = '\0';
            if (!tfs_exists(golden_path("BIG.DBF")))
                strcpy(g_gold, tfs_dir());
        }
    }
    tfs_set_dir(argc > 1 ? argv[1] : "");
    wipe_all();

    test_plain();
    test_memo();
    test_pack_stops(1);
    test_pack_stops(0);
    test_bad_states();
    test_reindex_stops();
    printf("T_PACK: %d stopped states recovered\n", g_states);
    test_reindex();
    test_pack_reindex();
    test_golden();
    test_errors();
    wipe_all();

    free(g_text);
    free(g_rbuf);
    free(g_pga);
    free(g_order);
    free(g_count);
    return tf_done();
}

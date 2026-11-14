/*
 * T_DBT.C - unit test for the dBASE III memo files of DBT.C.
 *
 * usage: T_DBT [dir]
 *
 * Creates scratch memo files in dir (default the current directory) and
 * checks the header written by dbt_create byte for byte, appends and
 * reads memos whose length puts the two-byte terminator at, across and
 * past a block boundary, checks the next-free arithmetic and the on-disk
 * terminator and gap bytes, persistence across a reopen, truncating
 * reads, invalid block numbers, text containing the terminator, a
 * read-only open, damaged files, the exact bytes of one small memo, a
 * header left behind its data by a stop before dbt_flush, one ahead of
 * it (on the host, its gap filled before the memo's text is written),
 * a flush with nothing to write, the limits of the next-free number and
 * of the memo length, an empty memo and dbt_create over a larger file.
 * The scratch files are removed at the start and at the end.  Prints
 * each failure and a summary line; exit status 0 when every check
 * passes, else 1 (2 when dir is too long).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "TESTFW.H"
#include "T_FILES.H"

#define BIGMEMO 4000
#define NSIZES  7
#define RAWMAX  (24 * DBT_BLOCK)

/* Memo lengths under test and the blocks each one needs. */
static const u16 sizes[NSIZES] = { 0, 1, 510, 511, 512, 513, BIGMEMO };
static const u16 nblocks[NSIZES] = { 1, 1, 1, 2, 2, 2, 8 };

/* The scratch files, removed before and after the run. */
static const char *const k_scratch[] = {
    "T_DBT_A.DBT", "T_DBT_B.DBT", "T_DBT_C.DBT", NULL
};
#define RBUFSZ  (BIGMEMO + 16)

/* Work buffers, from malloc so that they stay out of the near data. */
static char *g_text;    /* BIGMEMO + 1 bytes                            */
static char *g_rbuf;    /* RBUFSZ bytes                                 */
static u8 *g_raw;       /* RAWMAX bytes                                 */

/* Memo text: a byte pattern that avoids the 0x1A terminator and starts
   with CR LF and the dBASE soft carriage return 0x8D. */
static void fill_text(void)
{
    u16 i;
    u8 c;

    for (i = 0; i < BIGMEMO; i++) {
        c = (u8)((i * 7u + 3u) & 0xFFu);
        if (c == 0x1A || c == 0)
            c = 'x';
        g_text[i] = (char)c;
    }
    g_text[0] = '\r';
    g_text[1] = '\n';
    g_text[2] = (char)0x8D;
    g_text[BIGMEMO] = '\0';
}

/* Read the whole file path into g_raw; returns its size, or -1 on error
   or when it is longer than RAWMAX. */
static i32 read_raw(const char *path)
{
    u16 got;

    if (tfs_read_all(path, g_raw, RAWMAX, &got) != 0 ||
        tfs_size(path) != got)
        return -1;
    return (i32)got;
}

/* Write a file of size bytes: a header block whose next-free number is
   next and whose other bytes are 0, then fill bytes.  Returns 0 on
   success. */
static int make_file(const char *path, u32 next, u8 fill, u32 size)
{
    int h;
    int rc;
    u32 done;
    u16 n;

    if (cf_create(path, &h) != 0)
        return -1;
    memset(g_raw, 0, DBT_BLOCK);
    put_le32(g_raw, next);
    rc = 0;
    done = 0;
    while (rc == 0 && done < size) {
        n = (size - done < (u32)DBT_BLOCK) ? (u16)(size - done)
                                           : (u16)DBT_BLOCK;
        rc = cf_write(h, g_raw, n);
        done += n;
        if (done == (u32)DBT_BLOCK)
            memset(g_raw, fill, DBT_BLOCK);
    }
    if (cf_close(h) != 0)
        rc = -1;
    return rc;
}

#ifdef COMPAT_HOST
/* The size of the file g_wpath at each of the last two writes, noted by
   cf_fault as each write begins; the host shows an open file's size as
   it grows, where DOS updates it on disk only at the close. */
static const char *g_wpath;
static u32 g_wsize[2];

static int note_size(int op)
{
    if (op == CF_OP_WRITE) {
        cf_fault = NULL;
        g_wsize[0] = g_wsize[1];
        g_wsize[1] = tfs_size(g_wpath);
        cf_fault = note_size;
    }
    return 0;
}
#endif

/* 1 if g_raw[from..to) are all equal to v. */
static int all_bytes(u32 from, u32 to, u8 v)
{
    u32 i;

    for (i = from; i < to; i++)
        if (g_raw[(u16)i] != v)
            return 0;
    return 1;
}

static void test_create(const char *path)
{
    TF_EQ(dbt_create(path), DBF_OK);
    TF_EQ(read_raw(path), DBT_BLOCK);
    TF_EQ(get_le32(g_raw), 1);
    TF_CHECK(all_bytes(4, DBT_BLOCK, 0));
}

/* Append every test size to a fresh file; first[] receives the blocks. */
static void test_append(const char *path, u32 *first)
{
    dbt *m;
    u32 expect;
    u16 i;

    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    expect = 1;
    for (i = 0; i < NSIZES; i++) {
        first[i] = 0;
        TF_EQ(dbt_append(m, g_text, sizes[i], &first[i]), DBF_OK);
        TF_EQ(first[i], expect);
        expect += nblocks[i];
    }
    for (i = 0; i < NSIZES; i++) {
        u16 len;

        len = 0xFFFFu;
        TF_EQ(dbt_read(m, first[i], g_rbuf, RBUFSZ, &len), DBF_OK);
        TF_EQ(len, sizes[i]);
        TF_EQ(strlen(g_rbuf), sizes[i]);
        TF_CHECK(memcmp(g_rbuf, g_text, sizes[i]) == 0);
    }
    TF_EQ(dbt_close(m), DBF_OK);
}

/* The file after test_append: header, terminators, and zeros in the
   unused end of each memo's last block up to the next memo; the file
   ends right after the last terminator. */
static void test_layout(const char *path, const u32 *first)
{
    u32 next;
    u32 off;
    u32 end;
    u16 i;

    next = first[NSIZES - 1] + nblocks[NSIZES - 1];
    TF_EQ(next, 18);
    TF_EQ(read_raw(path), 10L * DBT_BLOCK + BIGMEMO + 2);
    TF_EQ(get_le32(g_raw), next);
    TF_CHECK(all_bytes(4, DBT_BLOCK, 0));
    for (i = 0; i < NSIZES; i++) {
        off = first[i] * DBT_BLOCK;
        end = (i + 1 < NSIZES) ? first[i + 1] * DBT_BLOCK
                               : off + sizes[i] + 2u;
        TF_CHECK(memcmp(g_raw + (u16)off, g_text, sizes[i]) == 0);
        off += sizes[i];
        TF_EQ(g_raw[(u16)off], 0x1A);
        TF_EQ(g_raw[(u16)(off + 1)], 0x1A);
        TF_CHECK(all_bytes(off + 2, end, 0));
    }
    /* 510 bytes: both terminator bytes end the block, and the 511-byte
       memo follows at once. */
    TF_EQ(g_raw[(u16)(3 * DBT_BLOCK + 510)], 0x1A);
    TF_EQ(g_raw[(u16)(3 * DBT_BLOCK + 511)], 0x1A);
    TF_CHECK(memcmp(g_raw + 4 * DBT_BLOCK, g_text, 16) == 0);
    /* 511 bytes: the second terminator byte opens a second block, whose
       remaining 511 bytes are the gap fill before the 512-byte memo. */
    TF_EQ(g_raw[(u16)(4 * DBT_BLOCK + 511)], 0x1A);
    TF_EQ(g_raw[(u16)(5 * DBT_BLOCK)], 0x1A);
    TF_CHECK(all_bytes(5 * DBT_BLOCK + 1, 6 * DBT_BLOCK, 0));
    /* 512 bytes: both terminator bytes in a second block. */
    TF_EQ(g_raw[(u16)(6 * DBT_BLOCK + 511)], (u8)g_text[511]);
    TF_EQ(g_raw[(u16)(7 * DBT_BLOCK)], 0x1A);
    TF_EQ(g_raw[(u16)(7 * DBT_BLOCK + 1)], 0x1A);
    TF_CHECK(all_bytes(7 * DBT_BLOCK + 2, 8 * DBT_BLOCK, 0));
    /* 513 bytes: one text byte, then the terminator, in a second block. */
    TF_EQ(g_raw[(u16)(9 * DBT_BLOCK)], (u8)g_text[512]);
    TF_EQ(g_raw[(u16)(9 * DBT_BLOCK + 1)], 0x1A);
    TF_EQ(g_raw[(u16)(9 * DBT_BLOCK + 2)], 0x1A);
}

/* Reopen read-only: memos persist, appends and flushes are refused,
   reads truncate. */
static void test_readonly(const char *path, const u32 *first)
{
    dbt *m;
    u32 blk;
    u16 len;
    u16 i;

    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_OK);
    if (m == NULL)
        return;
    for (i = 0; i < NSIZES; i++) {
        TF_EQ(dbt_read(m, first[i], g_rbuf, RBUFSZ, &len), DBF_OK);
        TF_EQ(len, sizes[i]);
        TF_CHECK(memcmp(g_rbuf, g_text, sizes[i]) == 0);
    }
    blk = 99;
    TF_EQ(dbt_append(m, "A", 1, &blk), DBF_ERDONLY);
    TF_EQ(blk, 0);

    /* Truncating reads. */
    TF_EQ(dbt_read(m, first[6], g_rbuf, 100, &len), DBF_ERANGE);
    TF_EQ(len, 99);
    TF_EQ(strlen(g_rbuf), 99);
    TF_CHECK(memcmp(g_rbuf, g_text, 99) == 0);
    TF_EQ(dbt_read(m, first[1], g_rbuf, 1, &len), DBF_ERANGE);
    TF_EQ(len, 0);
    TF_STREQ(g_rbuf, "");
    TF_EQ(dbt_read(m, first[2], g_rbuf, 511, &len), DBF_OK);
    TF_EQ(len, 510);
    TF_EQ(dbt_read(m, first[3], g_rbuf, 511, &len), DBF_ERANGE);
    TF_EQ(len, 510);
    TF_EQ(strlen(g_rbuf), 510);
    TF_EQ(dbt_read(m, first[0], g_rbuf, 1, &len), DBF_OK);
    TF_EQ(len, 0);

    /* Invalid block numbers. */
    TF_EQ(dbt_read(m, 0, g_rbuf, RBUFSZ, &len), DBF_ERANGE);
    TF_EQ(dbt_read(m, 18, g_rbuf, RBUFSZ, &len), DBF_ERANGE);
    TF_EQ(dbt_read(m, 70000UL, g_rbuf, RBUFSZ, &len), DBF_ERANGE);
    TF_EQ(dbt_read(m, 0xFFFFFFFFUL, g_rbuf, RBUFSZ, &len), DBF_ERANGE);

    TF_EQ(dbt_flush(m), DBF_ERDONLY);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 10L * DBT_BLOCK + BIGMEMO + 2);
}

/* Reopen read-write: appends continue at the stored next free block.
   The flush after an append writes the header and commits; the next
   one, with nothing to write, makes no disk call. */
static void test_reopen(const char *path)
{
    dbt *m;
    u32 blk;
    u16 len;

    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    blk = 99;
    TF_EQ(dbt_append(m, "AB\032CD", 5, &blk), DBF_EARG);
    TF_EQ(blk, 0);
    TF_EQ(dbt_append(m, "\032", 1, &blk), DBF_EARG);
    TF_EQ(dbt_append(m, "HELLO", 5, &blk), DBF_OK);
    TF_EQ(blk, 18);
    tfs_fault(0, 0, 0, 0);
    TF_EQ(dbt_flush(m), DBF_OK);
    TF_EQ(dbt_flush(m), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 2);
    TF_EQ(tfs_ops[0], CF_OP_WRITE);
    TF_EQ(tfs_ops[1], CF_OP_COMMIT);
    TF_EQ(dbt_append(m, g_text, 1023, &blk), DBF_OK);
    TF_EQ(blk, 19);
    TF_EQ(dbt_append(m, g_text, 1022, &blk), DBF_OK);
    TF_EQ(blk, 22);
    TF_EQ(dbt_read(m, 18, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_STREQ(g_rbuf, "HELLO");
    TF_EQ(dbt_read(m, 22, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_EQ(len, 1022);
    TF_EQ(dbt_read(m, 24, g_rbuf, RBUFSZ, &len), DBF_ERANGE);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 22L * DBT_BLOCK + 1022 + 2);
    TF_EQ(get_le32(g_raw), 24);
}

/* One small memo, byte for byte. */
static void test_exact(const char *path)
{
    static const u8 want[8] = { 'A', 'B', 0x0D, 0x0A, 0x8D, 'C', 0x1A, 0x1A };
    dbt *m;
    u32 blk;
    u16 len;

    TF_EQ(dbt_create(path), DBF_OK);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_append(m, "AB\r\n\215C", 6, &blk), DBF_OK);
    TF_EQ(blk, 1);
    TF_EQ(dbt_read(m, 1, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_EQ(len, 6);
    TF_STREQ(g_rbuf, "AB\r\n\215C");
    TF_EQ(dbt_close(m), DBF_OK);

    TF_EQ(read_raw(path), DBT_BLOCK + sizeof want);
    TF_EQ(g_raw[0], 2);
    TF_CHECK(all_bytes(1, DBT_BLOCK, 0));
    TF_CHECK(memcmp(g_raw + DBT_BLOCK, want, sizeof want) == 0);
}

/* Opening missing or damaged files, and bad arguments. */
static void test_damaged(const char *path, const char *missing)
{
    dbt *m;
    u32 blk;
    u16 len;

    m = (dbt *)g_rbuf;
    TF_EQ(dbt_open(&m, missing, DBF_RDONLY), DBF_ENOENT);
    TF_CHECK(m == NULL);
    TF_EQ(dbt_open(&m, path, 7), DBF_EARG);

    /* Shorter than a header block. */
    memset(g_raw, 0, DBT_BLOCK);
    g_raw[0] = 1;
    TF_EQ(tfs_write_all(path, g_raw, 100), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_EFORMAT);

    /* Next free block 0. */
    g_raw[0] = 0;
    TF_EQ(tfs_write_all(path, g_raw, DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_EFORMAT);

    /* A memo running into the end of the file without a terminator; the
       header keeps garbage after the next-free number, which the flush
       after an append must leave alone. */
    memset(g_raw, 'Q', 2 * DBT_BLOCK);
    put_le32(g_raw, 2);
    TF_EQ(tfs_write_all(path, g_raw, 2 * DBT_BLOCK - 10), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_read(m, 1, g_rbuf, RBUFSZ, &len), DBF_EFORMAT);
    TF_EQ(dbt_read(m, 1, g_rbuf, 0, &len), DBF_EARG);
    TF_EQ(dbt_append(m, "A", 1, &blk), DBF_OK);
    TF_EQ(blk, 2);
    TF_EQ(dbt_flush(m), DBF_OK);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 2L * DBT_BLOCK + 3);
    TF_EQ(get_le32(g_raw), 3);
    TF_CHECK(all_bytes(4, 2 * DBT_BLOCK - 10, 'Q'));
    TF_CHECK(all_bytes(2 * DBT_BLOCK - 10, 2 * DBT_BLOCK, 0));
    TF_CHECK(memcmp(g_raw + 2 * DBT_BLOCK, "A\032\032", 3) == 0);
}

/* A memo without a terminator that runs to the end of the file, read
   with a buffer whose last text byte is the file's last byte: the text
   fits, but the file ends before the terminator, so the read fails with
   DBF_EFORMAT rather than DBF_OK or DBF_ERANGE.  One byte less of buffer
   is a truncation (DBF_ERANGE); more buffer is still DBF_EFORMAT.  The
   end of the file falls inside a block, on a block boundary, and after
   the memo has crossed one. */
static void test_eof_full(const char *path)
{
    static const u16 ends[3] = {
        2 * DBT_BLOCK - 10, 2 * DBT_BLOCK, 3 * DBT_BLOCK
    };
    dbt *m;
    u16 len;
    u16 n;
    int i;

    for (i = 0; i < 3; i++) {
        n = (u16)(ends[i] - DBT_BLOCK);
        memset(g_raw, 0, DBT_BLOCK);
        put_le32(g_raw, (u32)(ends[i] + DBT_BLOCK - 1) / DBT_BLOCK);
        memcpy(g_raw + DBT_BLOCK, g_text, n);
        TF_EQ(tfs_write_all(path, g_raw, ends[i]), 0);
        m = NULL;
        TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_OK);
        if (m == NULL)
            return;

        memset(g_rbuf, 'Z', RBUFSZ);
        TF_EQ(dbt_read(m, 1, g_rbuf, (u16)(n + 1), &len), DBF_EFORMAT);
        TF_EQ(len, n);
        TF_CHECK(memcmp(g_rbuf, g_text, n) == 0);
        TF_EQ(g_rbuf[n], '\0');

        TF_EQ(dbt_read(m, 1, g_rbuf, n, &len), DBF_ERANGE);
        TF_EQ(len, n - 1);
        TF_CHECK(memcmp(g_rbuf, g_text, n - 1) == 0);
        TF_EQ(g_rbuf[n - 1], '\0');

        TF_EQ(dbt_read(m, 1, g_rbuf, (u16)(n + 2), &len), DBF_EFORMAT);
        TF_EQ(len, n);
        TF_EQ(dbt_close(m), DBF_OK);
    }
}

/* A header that lags behind the data, as after a stop between
   dbt_append and dbt_flush: appends go past the end of the file. */
static void test_crash(const char *path)
{
    dbt *m;
    u32 blk;
    u16 len;

    /* Header says block 1 is free, but 2 blocks and 100 bytes exist and
       block 1 holds a memo. */
    TF_EQ(make_file(path, 1, 'Q', 2L * DBT_BLOCK + 100), 0);
    TF_EQ(tfs_poke(path, DBT_BLOCK, "OLD\032\032", 5), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_read(m, 1, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_STREQ(g_rbuf, "OLD");
    blk = 0;
    TF_EQ(dbt_append(m, "NEW", 3, &blk), DBF_OK);
    TF_EQ(blk, 3);
    TF_EQ(dbt_read(m, 3, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_STREQ(g_rbuf, "NEW");
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 3L * DBT_BLOCK + 5);
    TF_EQ(get_le32(g_raw), 4);
    TF_CHECK(memcmp(g_raw + DBT_BLOCK, "OLD\032\032", 5) == 0);
    TF_CHECK(all_bytes(DBT_BLOCK + 5, 2L * DBT_BLOCK + 100, 'Q'));
    TF_CHECK(all_bytes(2L * DBT_BLOCK + 100, 3L * DBT_BLOCK, 0));
    TF_CHECK(memcmp(g_raw + 3 * DBT_BLOCK, "NEW\032\032", 5) == 0);

    /* A reader reaches memos past a lagging header, which it leaves as
       it is. */
    TF_EQ(make_file(path, 1, 'Q', 2L * DBT_BLOCK + 3), 0);
    TF_EQ(tfs_poke(path, 2L * DBT_BLOCK, "OLD", 3), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_read(m, 2, g_rbuf, RBUFSZ, &len), DBF_EFORMAT);
    TF_STREQ(g_rbuf, "OLD");
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 2L * DBT_BLOCK + 3);
    TF_EQ(get_le32(g_raw), 1);
    TF_EQ(tfs_poke(path, 2L * DBT_BLOCK + 3, "\032\032", 2), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_read(m, 2, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_STREQ(g_rbuf, "OLD");
    TF_EQ(dbt_read(m, 3, g_rbuf, RBUFSZ, &len), DBF_ERANGE);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 2L * DBT_BLOCK + 5);
    TF_EQ(get_le32(g_raw), 1);

    /* Opening alone raises a lagging header at close. */
    TF_EQ(make_file(path, 1, 'Q', 3L * DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 3L * DBT_BLOCK);
    TF_EQ(get_le32(g_raw), 3);

    /* A header ahead of the data is kept, and the gap up to the memo
       filled before its text is written (seen on the host only). */
    TF_EQ(make_file(path, 5, 'Q', DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
#ifdef COMPAT_HOST
    g_wpath = path;
    cf_fault = note_size;
#endif
    TF_EQ(dbt_append(m, "X", 1, &blk), DBF_OK);
#ifdef COMPAT_HOST
    cf_fault = NULL;
    TF_EQ(g_wsize[0], 5L * DBT_BLOCK);
#endif
    TF_EQ(blk, 5);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), 5L * DBT_BLOCK + 3);
    TF_EQ(get_le32(g_raw), 6);
    TF_CHECK(all_bytes(DBT_BLOCK, 5L * DBT_BLOCK, 0));
    TF_CHECK(memcmp(g_raw + 5 * DBT_BLOCK, "X\032\032", 3) == 0);
}

/* The largest next-free number whose byte offset fits in a u32. */
#define MAXBLOCK (0xFFFFFFFFUL / (u32)DBT_BLOCK)

static void test_bounds(const char *path)
{
    dbt *m;
    u32 blk;

    TF_EQ(make_file(path, MAXBLOCK + 1UL, 'Q', DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_EFORMAT);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_EFORMAT);
    TF_EQ(make_file(path, 0xFFFFFFFFUL, 'Q', DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_EFORMAT);

    TF_EQ(make_file(path, MAXBLOCK, 'Q', DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    blk = 99;
    TF_EQ(dbt_append(m, "A", 1, &blk), DBF_EFULL);
    TF_EQ(blk, 0);
    TF_EQ(dbt_append(m, NULL, 0, &blk), DBF_EFULL);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), DBT_BLOCK);
    TF_EQ(get_le32(g_raw), MAXBLOCK);

    TF_EQ(make_file(path, MAXBLOCK - 1UL, 'Q', DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_append(m, g_text, 511, &blk), DBF_EFULL);
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), DBT_BLOCK);
}

/* Memos at and past the longest length.  The 64 KB buffer this needs
   is only requested where size_t is wider than 16 bits (the host); on
   DOS that part is skipped, since a 16-bit malloc cannot express the
   size and no single buffer there may exceed 32 KB. */
static void test_long(const char *path)
{
    dbt *m;
    char *big;
    u32 blk;
    u16 len;
    u16 i;
    int same;

    /* A memo of more than 65535 bytes without a terminator. */
    TF_EQ(make_file(path, 2, 'Q', 140L * DBT_BLOCK), 0);
    TF_EQ(dbt_open(&m, path, DBF_RDONLY), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_read(m, 1, g_rbuf, RBUFSZ, &len), DBF_ERANGE);
    TF_EQ(len, RBUFSZ - 1);
    TF_EQ(strlen(g_rbuf), RBUFSZ - 1);
    big = (sizeof(size_t) > 2) ? (char *)malloc(DBT_MAXMEMO + 1u) : NULL;
    if (big != NULL) {
        TF_EQ(dbt_read(m, 1, big, (u16)(DBT_MAXMEMO + 1u), &len),
              DBF_ERANGE);
        TF_EQ(len, DBT_MAXMEMO);
    }
    TF_EQ(dbt_close(m), DBF_OK);

    TF_EQ(dbt_create(path), DBF_OK);
    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL) {
        free(big);
        return;
    }
    blk = 99;
    TF_EQ(dbt_append(m, g_text, (u16)(DBT_MAXMEMO + 1u), &blk), DBF_ERANGE);
    TF_EQ(blk, 0);
    TF_EQ(dbt_append(m, g_text, 0xFFFFu, &blk), DBF_ERANGE);
    if (big != NULL) {
        for (i = 0; i < DBT_MAXMEMO; i++)
            big[i] = (char)('A' + i % 26u);
        TF_EQ(dbt_append(m, big, DBT_MAXMEMO, &blk), DBF_OK);
        TF_EQ(blk, 1);
        memset(big, 0, DBT_MAXMEMO + 1u);
        TF_EQ(dbt_read(m, 1, big, (u16)(DBT_MAXMEMO + 1u), &len), DBF_OK);
        TF_EQ(len, DBT_MAXMEMO);
        same = 1;
        for (i = 0; i < DBT_MAXMEMO; i++)
            if (big[i] != (char)('A' + i % 26u))
                same = 0;
        TF_CHECK(same);
        TF_EQ(dbt_append(m, "", 0, &blk), DBF_OK);
        TF_EQ(blk, 1UL + ((u32)DBT_MAXMEMO + 2UL + (u32)DBT_BLOCK - 1UL) /
                         (u32)DBT_BLOCK);
        free(big);
    }
    TF_EQ(dbt_close(m), DBF_OK);
}

/* dbt_create over a larger file, and an empty memo from NULL text. */
static void test_recreate(const char *path)
{
    dbt *m;
    u32 blk;
    u16 len;

    TF_EQ(make_file(path, 9, 'Q', 9L * DBT_BLOCK), 0);
    TF_EQ(dbt_create(path), DBF_OK);
    TF_EQ(read_raw(path), DBT_BLOCK);
    TF_EQ(get_le32(g_raw), 1);
    TF_CHECK(all_bytes(4, DBT_BLOCK, 0));

    TF_EQ(dbt_open(&m, path, DBF_RDWR), DBF_OK);
    if (m == NULL)
        return;
    TF_EQ(dbt_append(m, NULL, 0, &blk), DBF_OK);
    TF_EQ(blk, 1);
    TF_EQ(dbt_read(m, 1, g_rbuf, RBUFSZ, &len), DBF_OK);
    TF_EQ(len, 0);
    TF_STREQ(g_rbuf, "");
    TF_EQ(dbt_close(m), DBF_OK);
    TF_EQ(read_raw(path), DBT_BLOCK + 2);
    TF_EQ(g_raw[DBT_BLOCK], 0x1A);
    TF_EQ(g_raw[DBT_BLOCK + 1], 0x1A);
}

int main(int argc, char **argv)
{
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    char c[DOS_PATHMAX];
    u32 first[NSIZES];

    tf_begin("T_DBT");
    g_text = (char *)malloc(BIGMEMO + 1);
    g_rbuf = (char *)malloc(RBUFSZ);
    g_raw = (u8 *)malloc(RAWMAX);
    if (!TF_CHECK(g_text != NULL && g_rbuf != NULL && g_raw != NULL))
        return tf_done();
    if (argc > 1)
        tfs_set_dir(argv[1]);
    tfs_wipe(k_scratch);
    strcpy(a, tfs_path(k_scratch[0], ""));
    strcpy(b, tfs_path(k_scratch[1], ""));
    strcpy(c, tfs_path(k_scratch[2], ""));
    fill_text();

    test_create(a);
    test_append(a, first);
    test_layout(a, first);
    test_readonly(a, first);
    test_reopen(a);
    test_exact(b);
    test_damaged(b, c);
    test_eof_full(b);
    test_crash(b);
    test_bounds(b);
    test_long(b);
    test_recreate(b);

    tfs_wipe(k_scratch);
    free(g_text);
    free(g_rbuf);
    free(g_raw);
    return tf_done();
}

/*
 * DBT.C - dBASE III Plus memo files (.DBT).
 *
 * A memo file is a sequence of DBT_BLOCK-byte blocks.  Block 0 is the
 * header:
 *
 *   bytes 0..3    number of the next free block (u32, little-endian),
 *                 where the next memo is appended
 *   other bytes   unused
 *
 * dBASE III Plus stores nothing but the next-free number in the header
 * and leaves the other bytes as they happen to be in its buffer, so they
 * are never read here.  dbt_create writes them as zeros; dbt_flush
 * rewrites bytes 0..3 only, so the rest of a header written by another
 * program is preserved.
 *
 * A memo starts at a block boundary and occupies consecutive blocks: the
 * text, then DBT_TERM DBT_TERM.  As in dBASE III Plus, the rest of the
 * last block is not written, so the file ends right after the last
 * memo's terminator, while the next-free number still counts whole
 * blocks.  When a memo is appended after such a short last block, or
 * past the end of the file, the gap up to the memo's first block is
 * filled with DBT_FILL bytes, because DOS leaves whatever was on the
 * disk in a gap created by seeking past the end.  The text itself may
 * hold any byte except DBT_TERM, including CR LF line ends and the
 * dBASE soft carriage return 0x8D, which are stored and returned
 * unchanged.  A reader stops at the first DBT_TERM.
 *
 * dbt_append changes the next-free number in memory only; it reaches
 * the header at dbt_flush or dbt_close.  If the machine stops in between,
 * the header lags behind memos that a record may already point at, so
 * dbt_open takes as the next free block whichever is further: the
 * header's value or the end of the file rounded up to a whole block.
 * Appends never reuse those blocks, and readers can reach them; a file
 * opened for writing gets the corrected header at dbt_flush or close.
 *
 * The handle holds no buffer: the header is read into a local, memo
 * text is read straight into the caller's buffer and written from the
 * caller's text, the terminator comes from a small constant block and
 * the gap fill from cf_zero.
 */
#define COMPAT_DISK
#include <stddef.h>
#include <stdlib.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"

#define DBT_HDRLEN      4       /* header bytes: next free block        */

struct dbt {
    int h;              /* cf_ handle                                   */
    int rdonly;         /* opened DBF_RDONLY                            */
    int dirty;          /* next free block changed since last flush     */
    u32 next;           /* next free block                              */
};

static const u8 dbt_term[2] = { DBT_TERM, DBT_TERM };
/* The gap fill is written with cf_zero, so DBT_FILL must be 0. */
extern char dbt_fillzero[1];
extern char dbt_fillzero[(DBT_FILL == 0) ? 1 : 2];

/* Byte offset of a block; the caller has checked block <= DBT_MAXBLOCK. */
static u32 dbt_offset(u32 block)
{
    return block * (u32)DBT_BLOCK;
}

int dbt_create(const char *path)
{
    u8 hdr[DBT_HDRLEN];
    int h;
    int rc;
    int rc2;

    put_le32(hdr, 1);
    rc = dbf_map_cf(cf_create(path, &h));
    if (rc != DBF_OK)
        return rc;
    rc = dbf_map_cf(cf_write(h, hdr, DBT_HDRLEN));
    if (rc == DBF_OK)
        rc = dbf_map_cf(cf_zero(h, DBT_BLOCK - DBT_HDRLEN));
    rc2 = dbf_map_cf(cf_close(h));
    if (rc == DBF_OK)
        rc = rc2;
    /* Leave no half-written file behind; the first error stands. */
    if (rc != DBF_OK)
        cf_remove(path);
    return rc;
}

int dbt_open(dbt **out, const char *path, int mode)
{
    dbt *m;
    u8 hdr[DBT_HDRLEN];
    u16 got;
    u32 size;
    u32 end;            /* blocks occupied by the file, rounded up      */
    int rc;

    *out = NULL;

    m = (dbt *)malloc(sizeof(dbt));
    if (m == NULL)
        return DBF_ENOMEM;
    m->rdonly = (mode == DBF_RDONLY);
    m->dirty = 0;

    rc = dbf_cf_open(path, mode, &m->h);
    if (rc != DBF_OK) {
        free(m);
        return rc;
    }
    rc = dbf_map_cf(cf_read(m->h, hdr, DBT_HDRLEN, &got));
    if (rc == DBF_OK)
        rc = dbf_map_cf(cf_size(m->h, &size));
    if (rc == DBF_OK) {
        m->next = get_le32(hdr);
        if (got != DBT_HDRLEN || size < DBT_BLOCK || m->next == 0
            || m->next > DBT_MAXBLOCK)
            rc = DBF_EFORMAT;
    }
    if (rc == DBF_OK) {
        end = size / (u32)DBT_BLOCK;
        if (size % (u32)DBT_BLOCK != 0)
            end++;
        if (end > DBT_MAXBLOCK)
            end = DBT_MAXBLOCK;
        if (end > m->next) {
            m->next = end;
            m->dirty = !m->rdonly;
        }
    }
    if (rc != DBF_OK) {
        /* The open error is the one reported. */
        cf_close(m->h);
        free(m);
        return rc;
    }
    *out = m;
    return DBF_OK;
}

int dbt_flush(dbt *m)
{
    u8 hdr[DBT_HDRLEN];
    int rc;

    if (m->rdonly)
        return DBF_ERDONLY;
    if (!m->dirty)
        return DBF_OK;
    put_le32(hdr, m->next);
    rc = dbf_map_cf(cf_seek(m->h, 0));
    if (rc == DBF_OK)
        rc = dbf_map_cf(cf_write(m->h, hdr, DBT_HDRLEN));
    if (rc == DBF_OK)
        rc = dbf_map_cf(dos_commit_handle(m->h));
    if (rc == DBF_OK)
        m->dirty = 0;
    return rc;
}

int dbt_close(dbt *m)
{
    int rc;
    int rc2;

    rc = DBF_OK;
    if (m->dirty)
        rc = dbt_flush(m);
    rc2 = dbf_map_cf(cf_close(m->h));
    if (rc == DBF_OK)
        rc = rc2;
    free(m);
    return rc;
}

int dbt_read(dbt *m, u32 block, char *buf, u16 bufsz, u16 *len)
{
    u16 used;           /* bytes stored in buf, NUL excluded            */
    u16 want;
    u16 got;
    u16 i;
    u8 extra;           /* the byte after a full buffer                 */
    int rc;

    *len = 0;
    if (bufsz == 0)
        return DBF_EARG;
    buf[0] = '\0';
    if (block == 0 || block >= m->next)
        return DBF_ERANGE;

    rc = dbf_map_cf(cf_seek(m->h, dbt_offset(block)));
    if (rc != DBF_OK)
        return rc;
    /* Read a block at a time into the free part of buf and look for
       the terminator in what arrived, until the file ends or buf is
       full. */
    used = 0;
    rc = DBF_EFORMAT;   /* until the terminator or a full buffer        */
    while (used < (u16)(bufsz - 1u)) {
        i = used;
        want = (u16)(bufsz - 1u - used);
        if (want > DBT_BLOCK)
            want = DBT_BLOCK;
        rc = dbf_map_cf(cf_read(m->h, buf + used, want, &got));
        if (rc != DBF_OK)
            break;
        rc = DBF_EFORMAT;
        if (got == 0)
            break;
        used = (u16)(used + got);
        while (i < used && (u8)buf[i] != DBT_TERM)
            i++;
        if (i < used) {
            used = i;
            rc = DBF_OK;
            break;
        }
    }
    /* A full buffer: the memo fits exactly when its terminator is the
       next byte. */
    if (used == (u16)(bufsz - 1u) && rc == DBF_EFORMAT) {
        rc = dbf_map_cf(cf_read(m->h, &extra, 1, &got));
        if (rc == DBF_OK)
            rc = got == 0 ? DBF_EFORMAT
                          : (extra == DBT_TERM ? DBF_OK : DBF_ERANGE);
    }
    buf[used] = '\0';
    *len = used;
    return rc;
}

int dbt_append(dbt *m, const char *text, u16 len, u32 *block)
{
    u32 nblocks;
    u32 start;
    u32 size;           /* file size before the append                  */
    u32 off;            /* byte offset of the memo's first block        */
    u16 i;
    int rc;

    *block = 0;
    if (m->rdonly)
        return DBF_ERDONLY;
    if (len > DBT_MAXMEMO)
        return DBF_ERANGE;
    for (i = 0; i < len; i++)
        if ((u8)text[i] == DBT_TERM)
            return DBF_EARG;

    /* The text and the two terminator bytes, in whole blocks. */
    nblocks = ((u32)len + (u32)sizeof dbt_term + (u32)(DBT_BLOCK - 1))
              / (u32)DBT_BLOCK;
    start = m->next;
    if (start > DBT_MAXBLOCK || nblocks > DBT_MAXBLOCK - start)
        return DBF_EFULL;

    rc = cf_size(m->h, &size);
    if (rc != 0)
        return dbf_map_cf(rc);
    off = dbt_offset(start);

    /* Fill any gap between the end of the file and the memo, then write
       the text and the terminator; the last write stops after the
       terminator. */
    rc = cf_seek(m->h, size);
    if (rc == 0 && size < off)
        rc = cf_zero(m->h, off - size);
    if (rc == 0)
        rc = cf_seek(m->h, off);
    if (rc == 0 && len > 0)
        rc = cf_write(m->h, text, len);
    if (rc == 0)
        rc = cf_write(m->h, dbt_term, (u16)sizeof dbt_term);
    if (rc != 0) {
        /* Cut the file back to its size before the append, dropping the
           gap fill and whatever part of the memo reached the disk. */
        if (size <= off)
            cf_truncate(m->h, size);
        return dbf_map_cf(rc);
    }
    m->next = start + nblocks;
    m->dirty = 1;
    *block = start;
    return DBF_OK;
}

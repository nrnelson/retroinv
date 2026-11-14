/*
 * DBF.C - dBASE III Plus tables (.DBF).
 *
 * File layout, all integers little-endian:
 *
 *   table header, DBF_HDRSIZE bytes
 *     0      version: 0x03, or 0x83 when the table has a memo field
 *     1..3   date of last update: year % 100, month, day (binary)
 *     4..7   record count (u32)
 *     8..9   header length in bytes, up to the first record (u16)
 *     10..11 record length in bytes, delete flag included (u16)
 *     12..31 zero
 *   one field descriptor of DBF_DESCSIZE bytes per field
 *     0..10  name, upper-case, padded with 0x00
 *     11     type: C N D L M
 *     12..15 data address; written as 0, ignored when read
 *     16     length
 *     17     decimal count
 *     18..31 zero
 *   header terminator DBF_HDREND
 *   records, each DBF_DELFLAG-or-space followed by the fields
 *   DBF_EOFBYTE
 *
 * dBASE III Plus writes the year byte as the year modulo 100, so 2026 is
 * stored as 26.  Tables written here do the same, for years DBF_YEARMIN
 * to DBF_YEARMAX.  On reading, a value below 100 is a two-digit year:
 * DBF_YEARPIVOT..99 are 19YY and 0..DBF_YEARPIVOT-1 are 20YY.  A value of
 * 100 or more, which some later xBase programs write, is 1900 + value.
 *
 * Numeric fields are converted with integer arithmetic only, so no
 * floating-point support is linked into the DOS programs: a value is
 * handled as an i32 scaled by 10^dec.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"

#define DBF_HDRSIZE     32      /* table header                         */
#define DBF_DESCSIZE    32      /* one field descriptor                 */
#define DBF_HDREND      0x0D    /* ends the field descriptors           */
#define DBF_EOFBYTE     0x1A    /* follows the last record              */
#define DBF_DELFLAG     '*'     /* record byte 0 of a deleted record    */
#define DBF_VER_PLAIN   0x03
#define DBF_VER_MEMO    0x83
#define DBF_HDRDATE     1       /* offset of the last-update date       */
#define DBF_HDRDATELEN  3       /* its bytes: year % 100, month, day    */
#define DBF_HDRCOUNT    4       /* offset of the record count (u32)     */
#define DBF_HDRLEN      8       /* offset of the header length (u16)    */
#define DBF_HDRRECLEN   10      /* offset of the record length (u16)    */
#define DBF_HDRUPD      7       /* date and record count, rewritten by a
                                   flush                                */

/* dbf_create and load_header read and write the table header through
   the buffer of one field descriptor. */
extern char dbf_hdrfits[1];
extern char dbf_hdrfits[(DBF_HDRSIZE <= DBF_DESCSIZE) ? 1 : 2];

/* dbf_put_memo writes a block number of up to TXT_U32LEN digits into an
   M field. */
extern char dbf_memofits[1];
extern char dbf_memofits[(TXT_U32LEN <= DBF_MEMOLEN) ? 1 : 2];

/* Bytes after the descriptors that are counted in the header length.
   Tables written here count only the DBF_HDREND terminator, giving a
   header of DBF_HDRSIZE + DBF_DESCSIZE * nfields + DBF_HDRTAIL bytes.
   dbf_open also accepts one further byte after the terminator (a 0x00
   that some dBASE-family programs write), i.e. DBF_HDRTAIL + 1. */
#define DBF_HDRTAIL     1

#define DBF_YEARPIVOT   80      /* two-digit years from here are 19YY   */
#define DBF_YEARMIN     (1900 + DBF_YEARPIVOT)
#define DBF_YEARMAX     (DBF_YEARMIN + 99)

/* i32 limits written out, since <limits.h> describes int, not i32. */
#define DBF_I32MAX      ((u32)2147483647UL)
#define DBF_I32MINMAG   ((u32)2147483648UL) /* i32 minimum's magnitude  */
#define DBF_U32MAX      ((u32)0xFFFFFFFFUL)

struct dbf {
    int        h;               /* cf_ handle                           */
    int        rdonly;          /* opened DBF_RDONLY                    */
    int        dirty;           /* header must be rewritten             */
    int        eofok;           /* 0x1A known to follow the last record */
    int        grown;           /* size changed since the last commit   */
    int        memo;            /* has an M field                       */
    u16        nfields;
    u16        reclen;
    u16        hdrlen;
    u8         version;         /* header byte 0                        */
    u8         date[DBF_HDRDATELEN]; /* header bytes 1..3               */
    u32        count;
    u32        recno;           /* record in the buffer, 0 if none      */
    dbf_field *fields;
    u8        *rec;             /* reclen bytes, then DBF_EOFBYTE       */
};

static int dbf_today_set;
static u16 dbf_today_y;
static u8  dbf_today_m;
static u8  dbf_today_d;

void dbf_set_today(u16 year, u8 month, u8 day)
{
    dbf_today_set = 1;
    dbf_today_y = year;
    dbf_today_m = month;
    dbf_today_d = day;
}

/* Store the last-update date in header bytes p[0..2]. */
static void put_today(u8 *p)
{
    u16 y;
    u8 m;
    u8 d;

    if (dbf_today_set) {
        y = dbf_today_y;
        m = dbf_today_m;
        d = dbf_today_d;
    } else {
        dos_today(&y, &m, &d);
    }
    if (y < DBF_YEARMIN)
        y = DBF_YEARMIN;
    if (y > DBF_YEARMAX)
        y = DBF_YEARMAX;
    p[0] = (u8)(y % 100);
    p[1] = m;
    p[2] = d;
}

int dbf_map_cf(int cfrc)
{
    switch (cfrc) {
    case 0:
        return DBF_OK;
    case CF_ENOENT:
    case CF_ENOPATH:
        return DBF_ENOENT;
    case CF_EACCES:
    case CF_ESHARE:
    case CF_ELOCK:
        return DBF_EINUSE;
    case CF_EMFILE:
        return DBF_EHANDLES;
    case CF_EFULL:
        return DBF_EFULL;
    case CF_ENOMEM:
        return DBF_ENOMEM;
    default:
        return DBF_EIO;
    }
}

int dbf_cf_open(const char *path, int mode, int *h)
{
    u8 cfmode;

    if (mode == DBF_RDWR)
        cfmode = (u8)(CF_RDWR | CF_DENYRW);
    else if (mode == DBF_RDONLY)
        cfmode = (u8)(CF_READ | CF_DENYW);
    else
        return DBF_EARG;
    return dbf_map_cf(cf_open(path, cfmode, h));
}

static int write_at(int h, u32 pos, const void *buf, u16 n)
{
    int rc;

    rc = cf_seek(h, pos);
    if (rc == 0)
        rc = cf_write(h, buf, n);
    return dbf_map_cf(rc);
}

/* Read exactly n bytes at pos; a short read is DBF_EFORMAT. */
static int read_at(int h, u32 pos, void *buf, u16 n)
{
    int rc;
    u16 got;

    rc = cf_seek(h, pos);
    if (rc == 0)
        rc = cf_read(h, buf, n, &got);
    if (rc != 0)
        return dbf_map_cf(rc);
    if (got != n)
        return DBF_EFORMAT;
    return DBF_OK;
}

/* Check the type, width and decimals of a field. */
static int field_shape_ok(const dbf_field *f)
{
    switch (f->type) {
    case 'C':
        return f->len >= 1 && f->len <= DBF_MAXCHAR && f->dec == 0;
    case 'N':
        if (f->len < 1 || f->len > DBF_MAXNUM)
            return 0;
        return f->dec == 0 || f->dec < f->len - 1;
    case 'D':
        return f->len == DBF_DATELEN && f->dec == 0;
    case 'L':
        return f->len == 1 && f->dec == 0;
    case 'M':
        return f->len == DBF_MEMOLEN && f->dec == 0;
    default:
        return 0;
    }
}

/* Check a field name: 1..DBF_NAMELEN of A-Z 0-9 _, starting with A-Z. */
static int field_name_ok(const char *name)
{
    u16 i;
    char c;

    if (!(name[0] >= 'A' && name[0] <= 'Z'))
        return 0;
    for (i = 0; i <= DBF_NAMELEN; i++) {
        c = name[i];
        if (c == '\0')
            return 1;
        if (!((c >= 'A' && c <= 'Z') || TXT_ISDIGIT(c) || c == '_'))
            return 0;
    }
    return 0;
}

/* Validate fields, fill in offsets and the record length; DBF_EARG if
   anything is wrong. */
static int layout_fields(dbf_field *fields, u16 nfields, u16 *reclen,
                         int *memo)
{
    u16 i;
    u16 j;
    u16 off;

    if (nfields < 1 || nfields > DBF_MAXFIELDS)
        return DBF_EARG;
    off = 1;
    *memo = 0;
    for (i = 0; i < nfields; i++) {
        if (!field_name_ok(fields[i].name) || !field_shape_ok(&fields[i]))
            return DBF_EARG;
        for (j = 0; j < i; j++)
            if (txt_ieq(fields[i].name, fields[j].name))
                return DBF_EARG;
        if ((u16)(off + fields[i].len) > DBF_MAXRECLEN)
            return DBF_EARG;
        fields[i].offset = off;
        off = (u16)(off + fields[i].len);
        if (fields[i].type == 'M')
            *memo = 1;
    }
    *reclen = off;
    return DBF_OK;
}

int dbf_create(const char *path, dbf_field *fields, u16 nfields)
{
    u8 buf[DBF_DESCSIZE];
    u16 reclen;
    u16 hdrlen;
    u16 i;
    int memo;
    int h;
    int rc;
    int rc2;

    rc = layout_fields(fields, nfields, &reclen, &memo);
    if (rc != DBF_OK)
        return rc;
    hdrlen = (u16)(DBF_HDRSIZE + DBF_DESCSIZE * nfields + DBF_HDRTAIL);

    rc = dbf_map_cf(cf_create(path, &h));
    if (rc != DBF_OK)
        return rc;

    memset(buf, 0, sizeof buf);
    buf[0] = (u8)(memo ? DBF_VER_MEMO : DBF_VER_PLAIN);
    put_today(buf + DBF_HDRDATE);
    put_le16(buf + DBF_HDRLEN, hdrlen);
    put_le16(buf + DBF_HDRRECLEN, reclen);
    rc = dbf_map_cf(cf_write(h, buf, DBF_HDRSIZE));

    for (i = 0; rc == DBF_OK && i < nfields; i++) {
        memset(buf, 0, sizeof buf);
        strncpy((char *)buf, fields[i].name, DBF_NAMELEN);
        buf[11] = (u8)fields[i].type;
        buf[16] = fields[i].len;
        buf[17] = fields[i].dec;
        rc = dbf_map_cf(cf_write(h, buf, DBF_DESCSIZE));
    }
    if (rc == DBF_OK) {
        buf[0] = DBF_HDREND;
        buf[1] = DBF_EOFBYTE;
        rc = dbf_map_cf(cf_write(h, buf, 2));
    }
    rc2 = dbf_map_cf(cf_close(h));
    if (rc == DBF_OK)
        rc = rc2;
    /* Leave no half-written table behind; the first error stands. */
    if (rc != DBF_OK)
        cf_remove(path);
    return rc;
}

/* Read and check the header of the table open in t->h.  Fills every
   member except h and rdonly, allocating fields and rec. */
static int load_header(dbf *t)
{
    u8 buf[DBF_DESCSIZE];
    u32 size;
    u32 maxcount;
    u32 pos;
    u16 n;
    u16 i;
    u16 off;
    u16 tail;
    u16 reclen;
    u8 ver;
    dbf_field *f;
    int rc;

    rc = read_at(t->h, 0, buf, DBF_HDRSIZE);
    if (rc != DBF_OK)
        return rc;
    ver = buf[0];
    if (ver != DBF_VER_PLAIN && ver != DBF_VER_MEMO)
        return DBF_EFORMAT;
    t->version = ver;
    memcpy(t->date, buf + DBF_HDRDATE, DBF_HDRDATELEN);
    t->count = get_le32(buf + DBF_HDRCOUNT);
    t->hdrlen = get_le16(buf + DBF_HDRLEN);
    reclen = get_le16(buf + DBF_HDRRECLEN);

    if (t->hdrlen < DBF_HDRSIZE + DBF_DESCSIZE + DBF_HDRTAIL)
        return DBF_EFORMAT;
    n = (u16)((t->hdrlen - DBF_HDRSIZE - DBF_HDRTAIL) / DBF_DESCSIZE);
    tail = (u16)(t->hdrlen - DBF_HDRSIZE - DBF_DESCSIZE * n);
    if (n < 1 || n > DBF_MAXFIELDS)
        return DBF_EFORMAT;
    if (tail != DBF_HDRTAIL && tail != DBF_HDRTAIL + 1)
        return DBF_EFORMAT;
    if (reclen < 2 || reclen > DBF_MAXRECLEN)
        return DBF_EFORMAT;

    t->fields = (dbf_field *)malloc(sizeof(dbf_field) * (size_t)n);
    if (t->fields == NULL)
        return DBF_ENOMEM;
    t->nfields = n;

    off = 1;
    t->memo = 0;
    pos = DBF_HDRSIZE;
    for (i = 0; i < n; i++) {
        f = &t->fields[i];
        rc = read_at(t->h, pos, buf, DBF_DESCSIZE);
        if (rc != DBF_OK)
            return rc;
        pos += DBF_DESCSIZE;
        if (buf[0] == '\0' || buf[0] == DBF_HDREND)
            return DBF_EFORMAT;
        memcpy(f->name, buf, DBF_NAMELEN + 1);
        f->name[DBF_NAMELEN] = '\0';
        f->type = (char)TXT_UPPER(buf[11]);
        f->len = buf[16];
        f->dec = buf[17];
        if (!field_shape_ok(f))
            return DBF_EFORMAT;
        if ((u16)(off + f->len) > reclen)
            return DBF_EFORMAT;
        f->offset = off;
        off = (u16)(off + f->len);
        if (f->type == 'M')
            t->memo = 1;
    }
    if (off != reclen)
        return DBF_EFORMAT;
    /* dBASE III Plus ignores the .DBT of a table whose version byte is
       DBF_VER_PLAIN and shows its memos as empty; such a table is
       refused. */
    if (t->memo && ver != DBF_VER_MEMO)
        return DBF_EFORMAT;
    rc = read_at(t->h, pos, buf, 1);
    if (rc != DBF_OK)
        return rc;
    if (buf[0] != DBF_HDREND)
        return DBF_EFORMAT;
    t->reclen = reclen;

    /* Every counted record must be present; bytes after them (a 0x1A
       or anything else) are ignored. */
    maxcount = (DBF_U32MAX - t->hdrlen) / reclen;
    if (t->count > maxcount)
        return DBF_EFORMAT;
    rc = dbf_map_cf(cf_size(t->h, &size));
    if (rc != DBF_OK)
        return rc;
    pos = t->hdrlen + t->count * reclen;
    if (size < pos)
        return DBF_EFORMAT;
    t->eofok = 0;
    if (size > pos) {
        rc = read_at(t->h, pos, buf, 1);
        if (rc != DBF_OK)
            return rc;
        t->eofok = (buf[0] == DBF_EOFBYTE);
    }

    t->rec = (u8 *)malloc((size_t)reclen + 1);
    if (t->rec == NULL)
        return DBF_ENOMEM;
    t->rec[reclen] = DBF_EOFBYTE;
    t->recno = 0;
    t->dirty = 0;
    t->grown = 0;
    memset(t->rec, ' ', reclen);
    return DBF_OK;
}

static void free_table(dbf *t)
{
    free(t->fields);
    free(t->rec);
    free(t);
}

int dbf_open(dbf **out, const char *path, int mode)
{
    dbf *t;
    int rc;

    *out = NULL;
    t = (dbf *)malloc(sizeof(dbf));
    if (t == NULL)
        return DBF_ENOMEM;
    t->fields = NULL;
    t->rec = NULL;
    t->rdonly = (mode == DBF_RDONLY);

    rc = dbf_cf_open(path, mode, &t->h);
    if (rc != DBF_OK) {
        free_table(t);
        return rc;
    }
    rc = load_header(t);
    if (rc != DBF_OK) {
        /* The header error is the one reported. */
        cf_close(t->h);
        free_table(t);
        return rc;
    }
    *out = t;
    return DBF_OK;
}

static u32 rec_pos(dbf *t, u32 recno)
{
    return (u32)t->hdrlen + (recno - 1) * (u32)t->reclen;
}

/* Write the end-of-file byte after the last counted record. */
static int put_eof(dbf *t)
{
    int rc;

    rc = write_at(t->h, rec_pos(t, t->count + 1), t->rec + t->reclen, 1);
    if (rc == DBF_OK)
        t->eofok = 1;
    return rc;
}

/* Flush order after appends.  DOS records a file's new size in its
   directory entry only when a handle to it is closed (here the duplicate
   dos_commit_handle closes), while the header sector holding the record
   count can reach the disk earlier, whenever DOS reuses its buffer.  A
   count above what the recorded size covers makes the table unreadable
   after a stop, so a table that grew is committed first: its size (and
   the appended records) are on the disk while the header still holds
   the old count.  A stop after that leaves only records past the counted
   ones, which readers ignore and the next append overwrites.  The header
   is then rewritten and the file committed again.  A table whose size
   changed with nothing left to write, as after dbf_trim's cut, needs
   only that last commit. */
int dbf_flush(dbf *t)
{
    u8 buf[DBF_HDRUPD];
    int rc;

    if (t->rdonly)
        return DBF_ERDONLY;
    /* A table with nothing pending is neither written nor committed, so
       its file time stays as it was. */
    if (!t->dirty && t->eofok && !t->grown)
        return DBF_OK;
    if (t->grown && (t->dirty || !t->eofok)) {
        rc = dbf_map_cf(dos_commit_handle(t->h));
        if (rc != DBF_OK)
            return rc;
    }
    if (t->dirty) {
        put_today(buf);
        put_le32(buf + (DBF_HDRCOUNT - DBF_HDRDATE), t->count);
        rc = write_at(t->h, DBF_HDRDATE, buf, DBF_HDRUPD);
        if (rc != DBF_OK)
            return rc;
        memcpy(t->date, buf, DBF_HDRDATELEN);
        t->dirty = 0;
    }
    if (!t->eofok) {
        rc = put_eof(t);
        if (rc != DBF_OK)
            return rc;
    }
    rc = dbf_map_cf(dos_commit_handle(t->h));
    if (rc != DBF_OK)
        return rc;
    t->grown = 0;
    return DBF_OK;
}

int dbf_trim(dbf *t)
{
    u32 size;
    u32 end;
    int rc;

    if (t->rdonly)
        return DBF_ERDONLY;
    rc = dbf_map_cf(cf_size(t->h, &size));
    if (rc != DBF_OK)
        return rc;
    end = rec_pos(t, t->count + 1);
    if (size > end + 1u) {
        rc = dbf_map_cf(cf_truncate(t->h, end));
        if (rc != DBF_OK)
            return rc;
        t->eofok = 0;
        t->grown = 1;
    }
    if (!t->eofok)
        rc = put_eof(t);
    return rc;
}

int dbf_close(dbf *t)
{
    int rc;
    int rc2;

    rc = DBF_OK;
    if (!t->rdonly && (t->dirty || !t->eofok))
        rc = dbf_flush(t);
    rc2 = dbf_map_cf(cf_close(t->h));
    if (rc == DBF_OK)
        rc = rc2;
    free_table(t);
    return rc;
}

int dbf_drop(dbf *t)
{
    int rc;

    rc = dbf_map_cf(cf_close(t->h));
    free_table(t);
    return rc;
}

u32 dbf_reccount(dbf *t)
{
    return t->count;
}

u8 dbf_version(dbf *t)
{
    return t->version;
}

u16 dbf_hdrlen(dbf *t)
{
    return t->hdrlen;
}

void dbf_lastupdate(dbf *t, u16 *year, u8 *month, u8 *day)
{
    u8 yy;

    yy = t->date[0];
    /* DBF_YEARPIVOT..99 are 19YY, and 100 and up are 1900 + value. */
    if (yy >= DBF_YEARPIVOT)
        *year = (u16)(1900 + yy);
    else
        *year = (u16)(2000 + yy);
    *month = t->date[1];
    *day = t->date[2];
}

u16 dbf_reclen(dbf *t)
{
    return t->reclen;
}

u16 dbf_nfields(dbf *t)
{
    return t->nfields;
}

int dbf_has_memo(dbf *t)
{
    return t->memo;
}

const dbf_field *dbf_field_at(dbf *t, u16 i)
{
    if (i >= t->nfields)
        return NULL;
    return &t->fields[i];
}

int dbf_field_index(dbf *t, const char *name)
{
    u16 i;

    for (i = 0; i < t->nfields; i++)
        if (txt_ieq(t->fields[i].name, name))
            return (int)i;
    return -1;
}

/* ---------------------------------------------------------------------
 * The record buffer.
 * --------------------------------------------------------------------- */

void dbf_blank(dbf *t)
{
    memset(t->rec, ' ', t->reclen);
    t->recno = 0;
}

int dbf_read(dbf *t, u32 recno)
{
    int rc;

    if (recno < 1 || recno > t->count)
        return DBF_ERANGE;
    t->recno = 0;
    rc = read_at(t->h, rec_pos(t, recno), t->rec, t->reclen);
    if (rc == DBF_EFORMAT)
        rc = DBF_EIO;
    if (rc != DBF_OK)
        return rc;
    t->recno = recno;
    return DBF_OK;
}

int dbf_write(dbf *t)
{
    int rc;

    if (t->rdonly)
        return DBF_ERDONLY;
    if (t->recno < 1 || t->recno > t->count)
        return DBF_ERANGE;
    rc = write_at(t->h, rec_pos(t, t->recno), t->rec, t->reclen);
    if (rc == DBF_OK)
        t->dirty = 1;
    return rc;
}

int dbf_append(dbf *t)
{
    int rc;

    if (t->rdonly)
        return DBF_ERDONLY;
    /* The new record and its 0x1A must end within a u32 file offset. */
    if (t->count >= (DBF_U32MAX - t->hdrlen) / t->reclen - 1)
        return DBF_ERANGE;
    /* rec holds reclen bytes followed by DBF_EOFBYTE: one write stores
       the record and the end-of-file byte. */
    rc = write_at(t->h, rec_pos(t, t->count + 1), t->rec,
                  (u16)(t->reclen + 1));
    if (rc != DBF_OK) {
        /* A partial write may have replaced the old 0x1A, and may have
           made the file longer. */
        t->eofok = 0;
        t->grown = 1;
        return rc;
    }
    t->eofok = 1;
    t->grown = 1;
    t->count++;
    t->recno = t->count;
    t->dirty = 1;
    return DBF_OK;
}

u32 dbf_recno(dbf *t)
{
    return t->recno;
}

int dbf_is_deleted(dbf *t)
{
    return t->rec[0] == DBF_DELFLAG;
}

void dbf_set_deleted(dbf *t, int deleted)
{
    t->rec[0] = (u8)(deleted ? DBF_DELFLAG : ' ');
}

u8 *dbf_recbuf(dbf *t)
{
    return t->rec;
}

/* ---------------------------------------------------------------------
 * Field values.
 * --------------------------------------------------------------------- */

int dbf_get_str(dbf *t, u16 f, char *out, u16 outsz)
{
    const dbf_field *fd;
    const u8 *p;
    u16 start;
    u16 end;
    u16 n;

    fd = dbf_field_at(t, f);
    if (fd == NULL || outsz == 0)
        return DBF_ERANGE;
    p = t->rec + fd->offset;
    start = 0;
    end = fd->len;
    while (end > 0 && p[end - 1] == ' ')
        end--;
    if (fd->type == 'N')
        while (start < end && p[start] == ' ')
            start++;
    n = (u16)(end - start);
    if (n > (u16)(outsz - 1)) {
        memcpy(out, p + start, (size_t)(outsz - 1));
        out[outsz - 1] = '\0';
        return DBF_ERANGE;
    }
    memcpy(out, p + start, (size_t)n);
    out[n] = '\0';
    return DBF_OK;
}

int dbf_put_str(dbf *t, u16 f, const char *s)
{
    const dbf_field *fd;
    u8 *p;
    size_t n;
    char c;

    fd = dbf_field_at(t, f);
    if (fd == NULL)
        return DBF_ERANGE;
    p = t->rec + fd->offset;
    n = strlen(s);
    switch (fd->type) {
    case 'C':
        if (n > fd->len)
            return DBF_ERANGE;
        memset(p, ' ', fd->len);
        memcpy(p, s, n);
        return DBF_OK;
    case 'D':
        if (n == 0) {
            memset(p, ' ', DBF_DATELEN);
            return DBF_OK;
        }
        if (!txt_date_ok(s))
            return DBF_EARG;
        memcpy(p, s, DBF_DATELEN);
        return DBF_OK;
    case 'L':
        if (n == 0) {
            p[0] = ' ';
            return DBF_OK;
        }
        /* Stored as T or F only; '?' (undecided) is read but not written. */
        c = s[0];
        c = (char)TXT_UPPER(c);
        if (c == 'Y')
            c = 'T';
        else if (c == 'N')
            c = 'F';
        if (n != 1 || (c != 'T' && c != 'F'))
            return DBF_EARG;
        p[0] = (u8)c;
        return DBF_OK;
    default:
        return DBF_EARG;
    }
}

/* Add digit d to the magnitude *mag, which must stay at most the limit
   q * 10 + r.  Comparing with the limit's quotient q and last digit r
   takes no 32-bit division per digit. */
static int add_digit(u32 *mag, u8 d, u32 q, u8 r)
{
    if (*mag > q || (*mag == q && d > r))
        return 0;
    *mag = *mag * 10 + d;
    return 1;
}

int dbf_get_num(dbf *t, u16 f, i32 *scaled, int *blank)
{
    const dbf_field *fd;
    const u8 *p;
    u32 mag;
    u8 last;
    u16 i;
    u16 len;
    u16 ndig;
    u16 nfrac;
    int neg;
    int point;

    *scaled = 0;
    *blank = 0;
    fd = dbf_field_at(t, f);
    if (fd == NULL)
        return DBF_ERANGE;
    if (fd->type != 'N')
        return DBF_EARG;
    p = t->rec + fd->offset;
    len = fd->len;

    i = 0;
    while (i < len && p[i] == ' ')
        i++;
    if (i == len) {
        *blank = 1;
        return DBF_OK;
    }
    if (p[i] == '*')
        return DBF_ERANGE;      /* dBASE's overflow marker */
    neg = 0;
    if (p[i] == '-') {
        neg = 1;
        i++;
    }
    /* The magnitude limits of the two signs, DBF_I32MAX and
       DBF_I32MINMAG, differ only in their last digit. */
    last = (u8)(neg ? DBF_I32MINMAG % 10 : DBF_I32MAX % 10);
    mag = 0;
    ndig = 0;
    nfrac = 0;
    point = 0;
    for (; i < len; i++) {
        if (TXT_ISDIGIT(p[i])) {
            if (point) {
                if (nfrac == fd->dec)
                    return DBF_EARG;
                nfrac++;
            }
            if (!add_digit(&mag, (u8)(p[i] - '0'), DBF_I32MAX / 10, last))
                return DBF_ERANGE;
            ndig++;
        } else if (p[i] == '.' && !point && fd->dec > 0) {
            point = 1;
        } else {
            break;
        }
    }
    while (i < len && p[i] == ' ')
        i++;
    if (i < len || ndig == 0)
        return DBF_EARG;
    for (; nfrac < fd->dec; nfrac++)
        if (!add_digit(&mag, 0, DBF_I32MAX / 10, last))
            return DBF_ERANGE;
    if (!neg)
        *scaled = (i32)mag;
    else if (mag == DBF_I32MINMAG)
        *scaled = -(i32)(DBF_I32MINMAG - 1) - 1;
    else
        *scaled = -(i32)mag;
    return DBF_OK;
}

int dbf_put_num(dbf *t, u16 f, i32 scaled)
{
    const dbf_field *fd;
    char ipart[TXT_U32LEN + 1];  /* digits of the integer part          */
    u8 *p;
    u32 mag;
    u32 p10;
    u16 ni;
    u16 n;
    u16 i;
    int neg;

    fd = dbf_field_at(t, f);
    if (fd == NULL)
        return DBF_ERANGE;
    if (fd->type != 'N')
        return DBF_EARG;
    neg = scaled < 0;
    if (neg)
        mag = (u32)(-(scaled + 1)) + 1;
    else
        mag = (u32)scaled;

    /* Split mag into its integer part and its dec decimals; from
       TXT_U32LEN decimals on, every digit of a u32 is a decimal. */
    if (fd->dec >= TXT_U32LEN) {
        ni = txt_u32(ipart, 0);
    } else {
        p10 = 1;
        for (i = 0; i < fd->dec; i++)
            p10 *= 10;
        ni = txt_u32(ipart, mag / p10);
        mag %= p10;
    }
    n = (u16)(neg + ni + (fd->dec > 0 ? 1 + fd->dec : 0));
    /* A zero integer part is dropped when the text would not fit
       otherwise, as in "-.25" for an N(4,2) field. */
    if (neg && n == (u16)(fd->len + 1) && fd->dec > 0 && ipart[0] == '0') {
        ni = 0;
        n--;
    }
    if (n > fd->len)
        return DBF_ERANGE;

    p = t->rec + fd->offset;
    memset(p, ' ', (size_t)(fd->len - n));
    p += fd->len - n;
    if (neg)
        *p++ = '-';
    memcpy(p, ipart, ni);
    p += ni;
    if (fd->dec > 0) {
        *p++ = '.';
        txt_u32w((char *)p, mag, fd->dec, '0');
    }
    return DBF_OK;
}

int dbf_put_blank(dbf *t, u16 f)
{
    const dbf_field *fd;

    fd = dbf_field_at(t, f);
    if (fd == NULL)
        return DBF_ERANGE;
    memset(t->rec + fd->offset, ' ', fd->len);
    return DBF_OK;
}

int dbf_get_memo(dbf *t, u16 f, u32 *block)
{
    const dbf_field *fd;
    const u8 *p;
    u16 i;
    u32 mag;

    *block = 0;
    fd = dbf_field_at(t, f);
    if (fd == NULL)
        return DBF_ERANGE;
    if (fd->type != 'M')
        return DBF_EARG;
    p = t->rec + fd->offset;
    i = 0;
    while (i < fd->len && p[i] == ' ')
        i++;
    mag = 0;
    for (; i < fd->len; i++) {
        if (!TXT_ISDIGIT(p[i]))
            return DBF_EARG;
        if (!add_digit(&mag, (u8)(p[i] - '0'), DBF_U32MAX / 10,
                       (u8)(DBF_U32MAX % 10)))
            return DBF_ERANGE;
    }
    *block = mag;
    return DBF_OK;
}

int dbf_put_memo(dbf *t, u16 f, u32 block)
{
    const dbf_field *fd;
    u8 *p;

    fd = dbf_field_at(t, f);
    if (fd == NULL)
        return DBF_ERANGE;
    if (fd->type != 'M')
        return DBF_EARG;
    p = t->rec + fd->offset;
    /* A u32 has at most DBF_MEMOLEN digits (dbf_memofits). */
    if (block == 0)
        memset(p, ' ', fd->len);
    else
        txt_u32w((char *)p, block, fd->len, ' ');
    return DBF_OK;
}

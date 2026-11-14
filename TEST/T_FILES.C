/*
 * T_FILES.C - scratch files and the disk fault recorder of the unit
 * tests (T_FILES.H).
 *
 * Files are copied and compared through FS_BLK-byte buffers on the
 * stack, so a test that links the module keeps its near data; every
 * file is opened CF_DENYNONE, so a file an open engine holds can still
 * be read under SHARE.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COMPAT_FIND
#include "COMPAT.H"
#include "DBF.H"
/* Only for printf, which TESTFW.H renames in a Windows program. */
#define TF_PART
#include "TESTFW.H"
#include "T_FILES.H"

#define FS_BLK          512     /* bytes moved per cf_read or cf_write  */
#define FS_NOPS         (CF_OP_CLOSE + 1) /* CF_OP_ codes counted       */
#define FS_RECLEN       10      /* .DBF header: record length, le16     */
#define FS_HDRLEN       12      /* .DBF header bytes read for it        */
#define FS_EOFBYTE      0x1A    /* follows the last record of a .DBF    */

static char tfs_pre[TFS_MAXDIR + 1];
/* tfs_path's two result buffers, filled in turn. */
static char tfs_buf[2][DOS_PATHMAX];
static int tfs_next;            /* the buffer tfs_path fills next       */

u8 tfs_ops[TFS_MAXOPS];

static u32 tfs_ncalls;          /* calls since tfs_fault                */
static u32 tfs_nmatch;          /* of them, calls whose op is in ...    */
static int tfs_mask;            /* ... this mask                        */
static u32 tfs_at;              /* first failing match, 0 for none      */
static u32 tfs_last;            /* last failing match, 0: no last       */
static int tfs_code;            /* what a failing call returns          */
static int tfs_first;           /* CF_OP_ of the first failed call      */
static u16 tfs_nop[FS_NOPS];    /* calls of each op                     */

/* ---------------------------------------------------------------------
 * The scratch directory.
 * --------------------------------------------------------------------- */

void tfs_set_dir(const char *arg)
{
    size_t n;
    char sep;

    n = strlen(arg);
    sep = '\0';
    if (n > 0 && arg[n - 1] != '\\' && arg[n - 1] != '/' &&
        arg[n - 1] != ':') {
        if (strchr(arg, '\\') != NULL || strchr(arg, ':') != NULL)
            sep = '\\';
        else
            sep = '/';
    }
    if (n + (sep != '\0' ? 1u : 0u) > TFS_MAXDIR) {
        printf("scratch directory name too long: %s\n", arg);
        exit(2);
    }
    strcpy(tfs_pre, arg);
    tfs_pre[n] = sep;
    if (sep != '\0')
        tfs_pre[n + 1] = '\0';
}

const char *tfs_dir(void)
{
    return tfs_pre;
}

const char *tfs_path(const char *stem, const char *ext)
{
    char *out;
    size_t n;

    n = strlen(tfs_pre);
    if (n + strlen(stem) + strlen(ext) >= DOS_PATHMAX) {
        printf("scratch path too long: %s%s%s\n", tfs_pre, stem, ext);
        exit(2);
    }
    out = tfs_buf[tfs_next];
    tfs_next = 1 - tfs_next;
    strcpy(out, tfs_pre);
    strcpy(out + n, stem);
    strcat(out, ext);
    return out;
}

const char *tfs_subpath(const char *dir, const char *name)
{
    char stem[DOS_PATHMAX];
    size_t n;

    n = strlen(dir);
    if (n + 1 + strlen(name) >= DOS_PATHMAX) {
        printf("scratch path too long: %s %s\n", dir, name);
        exit(2);
    }
    strcpy(stem, dir);
    stem[n] = DOS_DIRSEP;
    strcpy(stem + n + 1, name);
    return tfs_path(stem, "");
}

/* ---------------------------------------------------------------------
 * Whole files.
 * --------------------------------------------------------------------- */

/* Open path for reading, whoever else holds it. */
static int open_read(const char *path, int *h)
{
    return cf_open(path, CF_READ | CF_DENYNONE, h);
}

/* Close h; the first error rc stands. */
static int close_keep(int h, int rc)
{
    int rc2;

    rc2 = cf_close(h);
    return rc != 0 ? rc : rc2;
}

int tfs_copy(const char *src, const char *dst)
{
    u8 buf[FS_BLK];
    u16 got;
    int hs;
    int hd;
    int rc;

    rc = open_read(src, &hs);
    if (rc == CF_ENOENT || rc == CF_ENOPATH)
        cf_remove(dst);
    if (rc != 0)
        return rc;
    rc = cf_create(dst, &hd);
    if (rc != 0)
        return close_keep(hs, rc);
    for (;;) {
        rc = cf_read(hs, buf, (u16)sizeof buf, &got);
        if (rc != 0 || got == 0)
            break;
        rc = cf_write(hd, buf, got);
        if (rc != 0)
            break;
    }
    rc = close_keep(hd, rc);
    return close_keep(hs, rc);
}

int tfs_same(const char *a, const char *b)
{
    u8 ba[FS_BLK];
    u8 bb[FS_BLK];
    u16 na;
    u16 nb;
    int ha;
    int hb;
    int ok;

    if (open_read(a, &ha) != 0)
        return 0;
    if (open_read(b, &hb) != 0) {
        cf_close(ha);
        return 0;
    }
    for (;;) {
        ok = cf_read(ha, ba, (u16)sizeof ba, &na) == 0 &&
             cf_read(hb, bb, (u16)sizeof bb, &nb) == 0 && na == nb &&
             memcmp(ba, bb, na) == 0;
        if (!ok || na == 0)
            break;
    }
    cf_close(hb);
    cf_close(ha);
    return ok;
}

int tfs_exists(const char *path)
{
    return cf_exists(path) == 1;
}

u32 tfs_size(const char *path)
{
    u32 size;
    int h;

    if (open_read(path, &h) != 0)
        return TFS_NOFILE;
    if (cf_size(h, &size) != 0)
        size = TFS_NOFILE;
    cf_close(h);
    return size;
}

int tfs_peek(const char *path, u32 at, void *buf, u16 n)
{
    u16 got;
    int h;
    int rc;

    rc = open_read(path, &h);
    if (rc != 0)
        return rc;
    rc = cf_seek(h, at);
    if (rc == 0)
        rc = cf_read(h, buf, n, &got);
    if (rc == 0 && got != n)
        rc = CF_EINVAL;
    return close_keep(h, rc);
}

int tfs_poke(const char *path, u32 at, const void *buf, u16 n)
{
    int h;
    int rc;

    rc = cf_open(path, CF_RDWR | CF_DENYNONE, &h);
    if (rc != 0)
        return rc;
    rc = cf_seek(h, at);
    if (rc == 0)
        rc = cf_write(h, buf, n);
    return close_keep(h, rc);
}

int tfs_read_all(const char *path, void *buf, u16 max, u16 *got)
{
    u16 n;
    int h;
    int rc;

    *got = 0;
    rc = open_read(path, &h);
    if (rc != 0)
        return rc;
    while (*got < max) {
        rc = cf_read(h, (u8 *)buf + *got, (u16)(max - *got), &n);
        if (rc != 0 || n == 0)
            break;
        *got = (u16)(*got + n);
    }
    return close_keep(h, rc);
}

int tfs_write_all(const char *path, const void *buf, u16 n)
{
    int h;
    int rc;

    rc = cf_create(path, &h);
    if (rc != 0)
        return rc;
    if (n > 0)
        rc = cf_write(h, buf, n);
    return close_keep(h, rc);
}

void tfs_wipe(const char *const *names)
{
    for (; *names != NULL; names++)
        cf_remove(tfs_path(*names, ""));
}

int tfs_dirs(const char *const *names, int make)
{
    dos_find f;
    char name[DOS_FINDNAME];
    int rc;

    for (; *names != NULL; names++) {
        if (make) {
            rc = cf_mkdir(tfs_path(*names, ""));
            if (rc != 0 && rc != CF_EACCES)
                return rc;
        } else {
            rc = dos_find_first(&f, tfs_subpath(*names, "*.*"), name);
            for (; rc == 0; rc = dos_find_next(&f, name))
                cf_remove(tfs_subpath(*names, name));
            cf_rmdir(tfs_path(*names, ""));
        }
    }
    return 0;
}

int tfs_add_tail(const char *stem)
{
    u8 hdr[FS_HDRLEN];
    u8 *rec;
    u32 size;
    u16 got;
    u16 n;
    int h;
    int rc;

    rc = cf_open(tfs_path(stem, ".DBF"), CF_RDWR | CF_DENYRW, &h);
    if (rc != 0)
        return rc;
    rc = cf_read(h, hdr, (u16)sizeof hdr, &got);
    if (rc == 0 && got != sizeof hdr)
        rc = CF_EINVAL;
    if (rc == 0)
        rc = cf_size(h, &size);
    if (rc != 0)
        return close_keep(h, rc);
    n = get_le16(hdr + FS_RECLEN);
    if (n > DBF_MAXRECLEN)
        return close_keep(h, CF_EINVAL);
    rec = (u8 *)malloc((size_t)n + 1u);
    if (rec == NULL)
        return close_keep(h, CF_ENOMEM);
    memset(rec, ' ', n);
    rec[n] = FS_EOFBYTE;
    rc = cf_seek(h, size - 1u);
    if (rc == 0)
        rc = cf_write(h, rec, (u16)(n + 1u));
    free(rec);
    return close_keep(h, rc);
}

/* ---------------------------------------------------------------------
 * The fault recorder.
 * --------------------------------------------------------------------- */

/* The cf_fault hook: log op, and fail it when it is a chosen call. */
static int record(int op)
{
    if (op == CF_OP_CLOSE && !(tfs_mask & TFS_OP(op)))
        return 0;
    if (tfs_ncalls < TFS_MAXOPS)
        tfs_ops[(size_t)tfs_ncalls] = (u8)op;
    tfs_ncalls++;
    if (op >= 0 && op < FS_NOPS)
        tfs_nop[op]++;
    if (op < 0 || op >= FS_NOPS || !(tfs_mask & TFS_OP(op)))
        return 0;
    tfs_nmatch++;
    if (tfs_at == 0 || tfs_nmatch < tfs_at ||
        (tfs_last != 0 && tfs_nmatch > tfs_last))
        return 0;
    if (tfs_first == 0)
        tfs_first = op;
    return tfs_code;
}

void tfs_fault(int ops, u32 at, u32 count, int code)
{
    tfs_ncalls = 0;
    tfs_nmatch = 0;
    tfs_mask = ops;
    tfs_at = at;
    tfs_last = at != 0 && count != 0 ? at + count - 1u : 0;
    tfs_code = code != 0 ? code : CF_EFULL;
    tfs_first = 0;
    memset(tfs_ops, 0, sizeof tfs_ops);
    memset(tfs_nop, 0, sizeof tfs_nop);
    cf_fault = record;
}

void tfs_fault_off(void)
{
    cf_fault = NULL;
}

u32 tfs_calls(void)
{
    return tfs_ncalls;
}

u16 tfs_count(int op)
{
    return op >= 0 && op < FS_NOPS ? tfs_nop[op] : 0;
}

int tfs_failop(void)
{
    return tfs_first;
}

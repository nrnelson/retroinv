/*
 * T_CFILE.C - unit test for the DOS-handle file access in COMPAT.H.
 *
 * usage: T_CFILE [dir [SHARE | NODISK]]
 *
 * dir is the scratch directory (default the current one).  Positions
 * above 65535 are used so that a 16-bit split of a 32-bit offset is
 * exercised, and the critical-error handler is installed (twice, as a
 * second install must change nothing) for the whole run.
 *
 * Checks:
 * - a file is created, filled, committed with dos_commit_handle, sized
 *   and read back through a read-only handle; a read at the end gives 0
 *   bytes and no error, a short read what is left, and a write through
 *   the read-only handle CF_EACCES;
 * - cf_readblk reads a file of several sectors into a far block, at an
 *   odd offset, with one call, leaving the bytes around it alone; a read
 *   at the end gives 0 bytes, a closed handle CF_EBADF, and a read the
 *   cf_fault_rd hook fails its error;
 * - cf_writeblk writes from an odd offset of a far block with one call;
 *   a write cf_fault fails in the partial mode writes cf_fault_part
 *   bytes and gives CF_EFULL, a read-only handle CF_EACCES and a closed
 *   one CF_EBADF;
 * - cf_size leaves the position where it was, an overwrite in the
 *   middle reads back, and a write beyond 64 KB extends the file;
 * - cf_truncate leaves the position at the new size, down to 0, and a
 *   closed handle gives CF_EBADF, to cf_close and to dos_commit_handle;
 * - cf_rename and cf_remove work, a rename onto an existing name gives
 *   CF_EACCES, a missing file CF_ENOENT and an invalid access value
 *   CF_EINVACC;
 * - a name in a directory that does not exist gives CF_ENOPATH;
 * - cf_exists offers each call to the cf_fault_rd hook, once, as
 *   CF_OP_EXISTS: with the hook answering 0 it gives 1 for a file, 0
 *   for a missing file, a directory and a name in a missing directory;
 *   an answer CF_ENOENT or CF_ENOPATH gives 0 and any other code its
 *   negation for each of them, a file that is there included;
 * - cf_create_new makes a file that is not there and gives CF_EEXIST,
 *   with the handle -1 and the file unchanged, for one that is, and
 *   CF_ENOPATH in a directory that does not exist;
 * - dos_find_first and dos_find_next give each file a pattern matches
 *   once, '?' standing for one character or none at the end of a part,
 *   '*' for the rest of a part, the characters after it in the part
 *   ignored, letters of either case, and neither a
 *   directory, a file of another extension nor, on the host, a name
 *   that is no 8.3 name; then CF_ENOENT and "",
 *   again on a later call; two searches at once each give every file,
 *   and so does a search whose files are removed as they are found;
 *   a pattern matching nothing gives CF_ENOENT, one in a directory that
 *   does not exist CF_ENOPATH and one of DOS_PATHMAX bytes CF_EINVAL,
 *   each with "" and CF_ENOENT on the next call;
 * - cf_mkdir makes a directory, which a file can be made in, and gives
 *   CF_EACCES for one that exists and CF_ENOPATH under one that does
 *   not; cf_rmdir gives CF_EACCES for one not empty, removes an empty
 *   one and gives CF_ENOPATH for one that is not there;
 * - dos_dup gives the lowest free handle, open on the file of the one it
 *   duplicates: the two share one position and access mode, and each
 *   stays usable until it is closed itself; dos_dup2 closes the second
 *   handle it is given, that file's size then on disk, and points it at
 *   the file of the first.  dos_dup gives CF_EBADF and -1 for a closed
 *   handle and for -1, dos_dup2 CF_EBADF for a closed first handle and
 *   for a second of -1 (which DOSBox-X takes); neither is offered to
 *   cf_fault nor to cf_fault_rd.  Under DOS, with the standard handles
 *   alone open, dos_dup fills the 20-entry table with exactly the
 *   handles it leaves, then gives CF_EMFILE and -1.  dos_commit_handle
 *   puts the size on disk and takes no handle, and one that cf_fault
 *   fails as CF_OP_COMMIT gives that error and takes none either;
 * - dos_today returns a plausible date;
 * - dos_time returns a plausible time;
 * - dos_set_handles enlarges the handle table in two steps, the second
 *   with files open, and refuses a smaller table; then one file is
 *   opened read-only up to HOPEN times at once.  Every open must
 *   succeed until the system-wide limit (FILES= in CONFIG.SYS) is
 *   reached, which must be more than the OLD_USABLE handles a 20-entry
 *   table leaves; the count reached is printed.  All HOPEN opens
 *   succeed under DOS with FILES=45;
 * - dos_disk_free reports the default drive's free space and refuses a
 *   drive that does not exist;
 * - dos_dev_raw refuses a file handle and switches the printer device
 *   (handle 4) between raw and cooked mode, leaving it as it was;
 * - with the word SHARE after dir, SHARE.EXE must be loaded: a second
 *   open of a file held deny-read/write must then fail with CF_ESHARE,
 *   and so must a create of it in compatibility mode (DOS raises a
 *   critical error for that one), both without a critical-error prompt;
 *   a file the program itself holds deny-write is removed all the same,
 *   its handle going with it, as MS-DOS 3.10 does (so a test cannot
 *   play a reader that holds a file open against its own removal);
 * - with the word NODISK after dir, B: must be a second physical floppy
 *   drive with no disk in it (on a one-drive machine B: is the phantom
 *   of A:, and DOS prompts for its disk, which hangs the run):
 *   dos_disk_free of B: must then fail, without a prompt, with an error
 *   other than CF_EINVAL (the critical error DOS raised), and a drive
 *   that does not exist must still give CF_EINVAL after it.
 *
 * The scratch files are removed at the start and at the end.  Prints
 * each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when the directory name is too long.
 */
#include <stdio.h>
#include <string.h>
#define COMPAT_FIND
#define COMPAT_DUP
#include "COMPAT.H"
#include "TESTFW.H"
#include "T_FILES.H"

#define NBYTES          600
#define FAR_POS         70000UL
#define HOPEN           30      /* opens of one file held at once       */
#define HTABLE          40      /* handles asked of dos_set_handles     */
#define OLD_USABLE      15      /* handles a 20-entry table leaves
                                   after 0..4                           */
#define RB_COPIES       9       /* pattern copies cf_readblk reads      */
#define RB_OFF          7       /* where in the block it reads them to  */
#define RB_GUARD        0xEE    /* the block's bytes around them        */
#define WB_PART         10      /* bytes a write failed in the partial
                                   mode makes                           */
#define NFOUND          3       /* files the pattern "*.FND" matches    */

/* The files of the search phase: those "*.FND" matches, then one of
   another extension.  "CFF?.FND" matches the first two. */
static const char *const k_found[] = {
    "CFF1.FND", "CFF2.FND", "CFF10.FND", "CFFX.FNX", NULL
};

#ifdef COMPAT_HOST
/* Host names that are no 8.3 names, which no search gives. */
static const char *const k_long[] = {
    ".FND", "CFFLONGER.FND", "C.F.FND", "CFF3.FNDX", NULL
};
#endif

static u8 g_wbuf[NBYTES];
static u8 g_rbuf[NBYTES];

static void fill_pattern(void)
{
    int i;

    for (i = 0; i < NBYTES; i++)
        g_wbuf[i] = (u8)((i * 7 + 3) & 0xFF);
}

/* Create and fill a file, then read it back through a read-only handle;
   a read at end of file gives 0 bytes and no error. */
static void test_round_trip(const char *a)
{
    int h;
    u16 got;
    u32 size;

    TF_EQ(cf_create(a, &h), 0);
    TF_EQ(cf_write(h, g_wbuf, (u16)NBYTES), 0);
    TF_EQ(cf_write(h, g_wbuf, 0), 0);
    TF_EQ(dos_commit_handle(h), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, NBYTES);
    TF_EQ(cf_close(h), 0);

    TF_EQ(cf_open(a, CF_READ | CF_DENYW, &h), 0);
    memset(g_rbuf, 0, sizeof g_rbuf);
    TF_EQ(cf_read(h, g_rbuf, (u16)NBYTES, &got), 0);
    TF_EQ(got, NBYTES);
    TF_CHECK(memcmp(g_rbuf, g_wbuf, NBYTES) == 0);
    got = 1;
    TF_EQ(cf_read(h, g_rbuf, (u16)NBYTES, &got), 0);
    TF_EQ(got, 0);

    /* A short read returns what is left. */
    TF_EQ(cf_seek(h, (u32)(NBYTES - 10)), 0);
    TF_EQ(cf_read(h, g_rbuf, (u16)NBYTES, &got), 0);
    TF_EQ(got, 10);
    TF_CHECK(memcmp(g_rbuf, g_wbuf + NBYTES - 10, 10) == 0);

    /* Writing through a read-only handle is refused. */
    TF_EQ(cf_write(h, g_wbuf, 1), CF_EACCES);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, NBYTES);
    TF_EQ(cf_close(h), 0);
}

/* A cf_fault_rd hook that fails every read with CF_EACCES. */
static int fail_read(int op)
{
    return op == CF_OP_READ ? CF_EACCES : 0;
}

/* Write RB_COPIES copies of the pattern and read them back into a far
   block in one call. */
static void test_readblk(const char *a)
{
    cmem_blk *b;
    int h;
    int i;
    u16 got;

    TF_EQ(cf_create(a, &h), 0);
    for (i = 0; i < RB_COPIES; i++)
        TF_EQ(cf_write(h, g_wbuf, (u16)NBYTES), 0);
    TF_EQ(cf_close(h), 0);
    b = cmem_alloc((u16)(RB_OFF + RB_COPIES * NBYTES + 1));
    if (!TF_CHECK(b != NULL))
        return;
    cmem_set(b, 0, RB_GUARD, cmem_size(b));
    TF_EQ(cf_open(a, CF_READ | CF_DENYW, &h), 0);
    TF_EQ(cf_readblk(h, b, RB_OFF, (u16)(RB_COPIES * NBYTES), &got), 0);
    TF_EQ(got, RB_COPIES * NBYTES);
    for (i = 0; i < RB_COPIES; i++) {
        memset(g_rbuf, 0, sizeof g_rbuf);
        cmem_get(b, (u16)(RB_OFF + i * NBYTES), g_rbuf, (u16)NBYTES);
        TF_CHECK(memcmp(g_rbuf, g_wbuf, NBYTES) == 0);
    }
    cmem_get(b, 0, g_rbuf, RB_OFF);
    cmem_get(b, (u16)(RB_OFF + RB_COPIES * NBYTES), g_rbuf + RB_OFF, 1);
    for (i = 0; i <= RB_OFF; i++)
        TF_EQ(g_rbuf[i], RB_GUARD);
    got = 1;
    TF_EQ(cf_readblk(h, b, 0, 1, &got), 0);
    TF_EQ(got, 0);
    TF_EQ(cf_seek(h, 0), 0);
    cf_fault_rd = fail_read;
    got = 1;
    TF_EQ(cf_readblk(h, b, 0, 1, &got), CF_EACCES);
    TF_EQ(got, 0);
    cf_fault_rd = NULL;
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_readblk(h, b, 0, 1, &got), CF_EBADF);
    cmem_free(b);
}

/* Write the pattern from an odd offset of a far block and read it back;
   then the failures. */
static void test_writeblk(const char *a)
{
    cmem_blk *b;
    int h;
    u16 got;
    u32 size;

    b = cmem_alloc((u16)(RB_OFF + NBYTES));
    if (!TF_CHECK(b != NULL))
        return;
    cmem_put(b, RB_OFF, g_wbuf, (u16)NBYTES);
    TF_EQ(cf_create(a, &h), 0);
    TF_EQ(cf_writeblk(h, b, RB_OFF, (u16)NBYTES), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, NBYTES);
    cf_fault_part = WB_PART;
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, 0);
    TF_EQ(cf_writeblk(h, b, RB_OFF, (u16)NBYTES), CF_EFULL);
    tfs_fault_off();
    cf_fault_part = 0;
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, NBYTES + WB_PART);
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_open(a, CF_READ, &h), 0);
    memset(g_rbuf, 0, sizeof g_rbuf);
    TF_EQ(cf_read(h, g_rbuf, (u16)NBYTES, &got), 0);
    TF_EQ(got, NBYTES);
    TF_CHECK(memcmp(g_rbuf, g_wbuf, NBYTES) == 0);
    TF_EQ(cf_read(h, g_rbuf, (u16)NBYTES, &got), 0);
    TF_EQ(got, WB_PART);
    TF_CHECK(memcmp(g_rbuf, g_wbuf, WB_PART) == 0);
    TF_EQ(cf_writeblk(h, b, RB_OFF, 1), CF_EACCES);
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_writeblk(h, b, RB_OFF, 1), CF_EBADF);
    cmem_free(b);
}

/* Seek, and check that cf_size leaves the position where it was. */
static void test_seek_size(const char *a)
{
    int h;
    u16 got;
    u32 size;

    TF_EQ(cf_open(a, CF_RDWR | CF_DENYRW, &h), 0);
    TF_EQ(cf_seek(h, 100), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, NBYTES);
    TF_EQ(cf_read(h, g_rbuf, 10, &got), 0);
    TF_EQ(got, 10);
    TF_CHECK(memcmp(g_rbuf, g_wbuf + 100, 10) == 0);

    /* Overwrite in the middle and read it back. */
    TF_EQ(cf_seek(h, 200), 0);
    TF_EQ(cf_write(h, "XYZ", 3), 0);
    TF_EQ(cf_seek(h, 199), 0);
    TF_EQ(cf_read(h, g_rbuf, 5, &got), 0);
    TF_EQ(got, 5);
    TF_EQ(g_rbuf[0], g_wbuf[199]);
    TF_CHECK(memcmp(g_rbuf + 1, "XYZ", 3) == 0);
    TF_EQ(g_rbuf[4], g_wbuf[203]);

    /* A write beyond 64 KB extends the file. */
    TF_EQ(cf_seek(h, FAR_POS), 0);
    TF_EQ(cf_write(h, "Q", 1), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, FAR_POS + 1);
    TF_EQ(cf_seek(h, FAR_POS), 0);
    TF_EQ(cf_read(h, g_rbuf, 4, &got), 0);
    TF_EQ(got, 1);
    TF_EQ(g_rbuf[0], 'Q');
    TF_EQ(cf_close(h), 0);
}

/* Truncate, and check the position is left at the new size. */
static void test_truncate(const char *a)
{
    int h;
    u16 got;
    u32 size;

    TF_EQ(cf_open(a, CF_RDWR | CF_DENYNONE, &h), 0);
    TF_EQ(cf_truncate(h, FAR_POS - 1), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, FAR_POS - 1);
    TF_EQ(cf_truncate(h, 300), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, 300);
    TF_EQ(cf_write(h, "E", 1), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, 301);
    TF_EQ(cf_seek(h, 298), 0);
    TF_EQ(cf_read(h, g_rbuf, 10, &got), 0);
    TF_EQ(got, 3);
    TF_EQ(g_rbuf[0], g_wbuf[298]);
    TF_EQ(g_rbuf[1], g_wbuf[299]);
    TF_EQ(g_rbuf[2], 'E');
    TF_EQ(cf_truncate(h, 0), 0);
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, 0);
    TF_EQ(cf_close(h), 0);

    /* The handle is gone after a close, also for a commit. */
    TF_EQ(cf_close(h), CF_EBADF);
    TF_EQ(dos_commit_handle(h), CF_EBADF);
}

/* Rename, rename onto an existing name, remove, and the missing-file
   errors. */
static void test_names(const char *a, const char *b, const char *c)
{
    int h;
    int rc;

    TF_EQ(cf_rename(a, b), 0);
    h = 1234;
    TF_EQ(cf_open(a, CF_READ, &h), CF_ENOENT);
    TF_EQ(cf_open(b, CF_READ, &h), 0);
    TF_EQ(cf_close(h), 0);

    TF_EQ(cf_create(c, &h), 0);
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_rename(b, c), CF_EACCES);
    TF_EQ(cf_open(b, CF_READ, &h), 0);
    TF_EQ(cf_close(h), 0);

    TF_EQ(cf_remove(b), 0);
    TF_EQ(cf_remove(c), 0);
    /* DOS reports file not found; DOSBox-X reports access denied for a
       missing file in a directory mounted from the host. */
    rc = cf_remove(b);
    TF_CHECK(rc == CF_ENOENT || rc == CF_EACCES);
    TF_EQ(cf_open(b, CF_RDWR, &h), CF_ENOENT);
    TF_EQ(cf_rename(b, a), CF_ENOENT);
    /* Access 3 (CF_RDWR with the low bit set) is no DOS access code. */
    TF_EQ(cf_open(a, (u8)(CF_RDWR | 0x01), &h), CF_EINVACC);
}

/* cf_create_new makes a new file, never truncates one that exists, and
   needs the directory. */
static void test_create_new(const char *a, const char *d)
{
    u16 got;
    int h;

    TF_EQ(cf_create_new(a, &h), 0);
    TF_EQ(cf_write(h, g_wbuf, NBYTES), 0);
    TF_EQ(cf_close(h), 0);
    h = 1234;
    TF_EQ(cf_create_new(a, &h), CF_EEXIST);
    TF_EQ(h, -1);
    TF_EQ(cf_open(a, CF_READ, &h), 0);
    TF_EQ(cf_read(h, g_rbuf, NBYTES, &got), 0);
    TF_EQ(got, NBYTES);
    TF_CHECK(memcmp(g_rbuf, g_wbuf, NBYTES) == 0);
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_create_new(d, &h), CF_ENOPATH);
    TF_EQ(cf_remove(a), 0);
}

/* A name in a directory that does not exist gives path not found. */
static void test_nopath(const char *d)
{
    int h;
    int rc;

    TF_EQ(cf_open(d, CF_READ, &h), CF_ENOPATH);
    TF_EQ(cf_create(d, &h), CF_ENOPATH);
    /* DOS reports path not found; DOSBox-X reports file not found for a
       remove in a missing directory of a drive mounted from the host. */
    rc = cf_remove(d);
    TF_CHECK(rc == CF_ENOPATH || rc == CF_ENOENT);
}

/* What cf_exists gives for each answer of the hook: for a file that is
   there, and for a missing file, a directory and a name in a missing
   directory. */
static const struct {
    int answer;
    int file;
    int other;
} k_exists[] = {
    { 0, 1, 0 },
    { CF_ENOENT, 0, 0 },
    { CF_ENOPATH, 0, 0 },
    { CF_EACCES, -CF_EACCES, -CF_EACCES },
    { CF_ESHARE, -CF_ESHARE, -CF_ESHARE }
};
#define NEXISTS         (sizeof k_exists / sizeof k_exists[0])

static int g_answer;            /* what exists_hook answers             */
static int g_asked;             /* CF_OP_EXISTS calls it saw            */

/* A cf_fault_rd hook that counts the CF_OP_EXISTS calls and answers
   them g_answer, letting every other op go ahead. */
static int exists_hook(int op)
{
    if (op != CF_OP_EXISTS)
        return 0;
    g_asked++;
    return g_answer;
}

/* cf_exists under the read hook, for each answer of k_exists, of the
   file a, the missing file b, the directory c (made and removed here)
   and the name d in a missing directory. */
static void test_exists(const char *a, const char *b, const char *c,
                        const char *d)
{
    const char *path[4];
    unsigned i;
    int k;
    int n;

    path[0] = a;
    path[1] = b;
    path[2] = c;
    path[3] = d;
    TF_EQ(tfs_write_all(a, "E", 1), 0);
    cf_remove(b);
    TF_EQ(cf_mkdir(c), 0);
    cf_fault_rd = exists_hook;
    for (i = 0; i < NEXISTS; i++) {
        g_answer = k_exists[i].answer;
        for (k = 0; k < 4; k++) {
            n = g_asked;
            TF_EQ(cf_exists(path[k]),
                  k == 0 ? k_exists[i].file : k_exists[i].other);
            TF_EQ(g_asked, n + 1);
        }
    }
    cf_fault_rd = NULL;
    TF_EQ(cf_exists(a), 1);
    TF_EQ(cf_rmdir(c), 0);
    TF_EQ(cf_remove(a), 0);
}

/* Copy into out (DOS_PATHMAX bytes) the scratch path of name.DAT, or,
   when sub is not NULL, that of name.DAT in the subdirectory sub of the
   scratch directory, written with the separator the scratch prefix ends
   in ('\' after a drive colon, '/' for the current directory). */
static void scratch_name(char *out, const char *sub, const char *name)
{
    const char *dir;
    char stem[DOS_PATHMAX];
    size_t n;

    stem[0] = '\0';
    if (sub != NULL) {
        dir = tfs_dir();
        n = strlen(dir);
        strcpy(stem, sub);
        if (n == 0)
            strcat(stem, "/");
        else if (dir[n - 1] == ':')
            strcat(stem, "\\");
        else
            strcat(stem, dir + n - 1);
    }
    strcat(stem, name);
    strcpy(out, tfs_path(stem, ".DAT"));
}

/* The bit of k_found's file name; 1 << 8 for another. */
static int bit(const char *name)
{
    int i;

    for (i = 0; k_found[i] != NULL; i++)
        if (strcmp(name, k_found[i]) == 0)
            return 1 << i;
    return 1 << 8;
}

/* Walk the search of pat to its end and return the bits of the files
   it gave, checking that it gave n names; with drop, each file is
   removed as it is given. */
static int walk(const char *pat, int n, int drop)
{
    dos_find f;
    char name[DOS_FINDNAME];
    int seen;
    int k;
    int rc;

    seen = 0;
    k = 0;
    rc = dos_find_first(&f, pat, name);
    while (rc == 0 && k++ <= n) {
        seen |= bit(name);
        if (drop)
            TF_EQ(cf_remove(tfs_path(name, "")), 0);
        rc = dos_find_next(&f, name);
    }
    TF_EQ(rc, CF_ENOENT);
    TF_EQ(k, n);
    TF_STREQ(name, "");
    TF_EQ(dos_find_next(&f, name), CF_ENOENT);
    return seen;
}

/* A search that ends at once with code: "" and CF_ENOENT next. */
static void find_none(const char *pat, int code)
{
    dos_find f;
    char name[DOS_FINDNAME];

    strcpy(name, "X");
    TF_EQ(dos_find_first(&f, pat, name), code);
    TF_STREQ(name, "");
    strcpy(name, "X");
    TF_EQ(dos_find_next(&f, name), CF_ENOENT);
    TF_STREQ(name, "");
}

/* d is a name in a directory that does not exist. */
static void test_find(const char *d)
{
    dos_find f1;
    dos_find f2;
    char n1[DOS_FINDNAME];
    char n2[DOS_FINDNAME];
    char dir[DOS_PATHMAX];
    char in[DOS_PATHMAX];
    char lng[DOS_PATHMAX + 1];
    int s1;
    int s2;
    int i;

    tfs_wipe(k_found);
    for (i = 0; k_found[i] != NULL; i++)
        TF_EQ(tfs_write_all(tfs_path(k_found[i], ""), "F", 1), 0);
    strcpy(dir, tfs_path("CFFD", ".FND"));
    scratch_name(in, "CFFD.FND", "IN");
    TF_EQ(cf_mkdir(dir), 0);
    TF_EQ(tfs_write_all(in, "I", 1), 0);
#ifdef COMPAT_HOST
    for (i = 0; k_long[i] != NULL; i++)
        TF_EQ(tfs_write_all(tfs_path(k_long[i], ""), "F", 1), 0);
#endif
    TF_EQ(walk(tfs_path("CFF?", ".FND"), 2, 0), 3);
    TF_EQ(walk(tfs_path("*", ".FND"), NFOUND, 0), 7);
    TF_EQ(walk(tfs_path("cff*", ".fnd"), NFOUND, 0), 7);
    TF_EQ(walk(tfs_path("CFFX", ".*"), 1, 0), 8);
    TF_EQ(walk(tfs_path("CFF*Z", ".FND"), NFOUND, 0), 7);
    TF_EQ(walk(tfs_path("*", ".F*X"), NFOUND + 1, 0), 15);

    /* Two searches at once. */
    TF_EQ(dos_find_first(&f1, tfs_path("*", ".FND"), n1), 0);
    TF_EQ(dos_find_first(&f2, tfs_path("*", ".FND"), n2), 0);
    s1 = 0;
    s2 = 0;
    for (i = 0; i < NFOUND; i++) {
        s1 |= bit(n1);
        s2 |= bit(n2);
        TF_EQ(dos_find_next(&f1, n1), i < NFOUND - 1 ? 0 : CF_ENOENT);
        TF_EQ(dos_find_next(&f2, n2), i < NFOUND - 1 ? 0 : CF_ENOENT);
    }
    TF_EQ(s1, 7);
    TF_EQ(s2, 7);

    find_none(tfs_path("CFFNONE", ".FND"), CF_ENOENT);
    find_none(d, CF_ENOPATH);
    memset(lng, 'A', DOS_PATHMAX);
    lng[DOS_PATHMAX] = '\0';
    find_none(lng, CF_EINVAL);
    TF_EQ(walk(tfs_path("*", ".FND"), NFOUND, 1), 7);
    find_none(tfs_path("*", ".FND"), CF_ENOENT);
    TF_EQ(tfs_exists(tfs_path(k_found[3], "")), 1);

    TF_EQ(cf_mkdir(dir), CF_EACCES);
    TF_EQ(cf_rmdir(dir), CF_EACCES);
    TF_EQ(cf_remove(in), 0);
    TF_EQ(cf_rmdir(dir), 0);
    TF_EQ(cf_rmdir(dir), CF_ENOPATH);
    scratch_name(in, "CFNODIR", "SUB");
    TF_EQ(cf_mkdir(in), CF_ENOPATH);
    tfs_wipe(k_found);
#ifdef COMPAT_HOST
    tfs_wipe(k_long);
#endif
}

/* Run with SHARE.EXE loaded: a file held deny-read/write cannot be
   opened again, nor created over in compatibility mode; the program's
   own deny-write open does not keep its remove from working. */
static void test_share(const char *a)
{
    int h;
    int h2;
    int rc;

    TF_EQ(cf_create(a, &h), 0);
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_open(a, CF_RDWR | CF_DENYRW, &h), 0);
    TF_EQ(cf_open(a, CF_READ | CF_DENYNONE, &h2), CF_ESHARE);
    /* DOS raises a critical error for this conflict; the handler answers
       Fail, so the call returns the sharing violation instead of
       prompting. */
    rc = cf_create(a, &h2);
    TF_EQ(rc, CF_ESHARE);
    if (rc == 0)
        cf_close(h2);
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_open(a, CF_READ | CF_DENYW, &h), 0);
    TF_EQ(cf_remove(a), 0);
    TF_EQ(cf_exists(a), 0);
    TF_EQ(cf_close(h), CF_EBADF);
}

static void test_today(void)
{
    u16 year;
    u8 month;
    u8 day;

    year = 0;
    month = 0;
    day = 0;
    dos_today(&year, &month, &day);
    TF_CHECK(year >= 1980 && year <= 2099);
    TF_CHECK(month >= 1 && month <= 12);
    TF_CHECK(day >= 1 && day <= 31);
}

static void test_time(void)
{
    u8 hour;
    u8 min;
    u8 sec;

    hour = 0xFF;
    min = 0xFF;
    sec = 0xFF;
    dos_time(&hour, &min, &sec);
    TF_CHECK(hour <= 23);
    TF_CHECK(min <= 59);
    TF_CHECK(sec <= 59);
}

static int g_rdop;              /* the op refuse_rd last saw, 0 none    */

/* A cf_fault_rd hook that notes each call's op and refuses it. */
static int refuse_rd(int op)
{
    g_rdop = op;
    return CF_EACCES;
}

/* Duplicate handles of the file a (b the file a duplicate is forced
   over), and fill the handle table under DOS. */
static void test_dup(const char *a, const char *b)
{
    int h1;
    int h2;
    int h3;
    int r;
    int d;
    int e;
    int t;
    int rc;
    u16 got;
#ifndef COMPAT_HOST
    int fill[OLD_USABLE];
    int n;
#endif

    /* Opened in turn, h1 and h2 take the lowest free handles. */
    TF_EQ(cf_create(a, &h1), 0);
    TF_EQ(cf_open(a, CF_RDWR, &h2), 0);
    TF_EQ(cf_open(a, CF_RDWR, &h3), 0);
    TF_EQ(cf_close(h2), 0);
    TF_EQ(cf_close(h1), 0);
    d = 1234;
    TF_EQ(dos_dup(h3, &d), 0);
    TF_EQ(d, h1);
    TF_EQ(dos_dup(h3, &e), 0);
    TF_EQ(e, h2);

    /* One position: a write through d goes after one through h3, and a
       read through e from where d seeks to. */
    TF_EQ(cf_write(h3, "ABCDEF", 6), 0);
    TF_EQ(cf_write(d, "G", 1), 0);
    TF_EQ(cf_seek(d, 1), 0);
    TF_EQ(cf_read(e, g_rbuf, 3, &got), 0);
    TF_EQ(got, 3);
    TF_CHECK(memcmp(g_rbuf, "BCD", 3) == 0);
    TF_EQ(cf_close(h3), 0);
    TF_EQ(cf_close(e), 0);
    TF_EQ(cf_read(d, g_rbuf, 8, &got), 0);
    TF_EQ(got, 3);
    TF_CHECK(memcmp(g_rbuf, "EFG", 3) == 0);
    TF_EQ(cf_close(d), 0);
    TF_EQ(cf_close(d), CF_EBADF);

    d = 1234;
    TF_EQ(dos_dup(h3, &d), CF_EBADF);
    TF_EQ(d, -1);
    d = 1234;
    TF_EQ(dos_dup(-1, &d), CF_EBADF);
    TF_EQ(d, -1);

    /* One access mode; t, forced onto r's file, reads it from r's
       position, its own file b closed with its 10 bytes. */
    TF_EQ(cf_open(a, CF_READ, &r), 0);
    TF_EQ(dos_dup(r, &d), 0);
    TF_EQ(cf_write(d, "X", 1), CF_EACCES);
    TF_EQ(cf_close(d), 0);
    TF_EQ(cf_create(b, &t), 0);
    TF_EQ(cf_write(t, g_wbuf, 10), 0);
    TF_EQ(dos_dup2(r, t), 0);
    TF_EQ(tfs_size(b), 10);
    TF_EQ(cf_read(t, g_rbuf, 2, &got), 0);
    TF_EQ(got, 2);
    TF_CHECK(memcmp(g_rbuf, "AB", 2) == 0);
    TF_EQ(cf_read(r, g_rbuf, 2, &got), 0);
    TF_EQ(got, 2);
    TF_CHECK(memcmp(g_rbuf, "CD", 2) == 0);
    TF_EQ(cf_write(t, "X", 1), CF_EACCES);
    /* DOS refuses a handle outside the table; DOSBox-X takes it. */
    rc = dos_dup2(r, -1);
    TF_CHECK(rc == CF_EBADF || rc == 0);

    /* Every call of both hooks fails, but neither sees a duplicate. */
    tfs_fault(TFS_ALLOPS | TFS_OP(CF_OP_CLOSE), 1, 0, CF_EACCES);
    cf_fault_rd = refuse_rd;
    g_rdop = 0;
    TF_EQ(dos_dup(r, &d), 0);
    TF_EQ(dos_dup2(r, d), 0);
    tfs_fault_off();
    cf_fault_rd = NULL;
    TF_EQ(tfs_calls(), 0);
    TF_EQ(g_rdop, 0);
    TF_EQ(cf_close(d), 0);
    TF_EQ(dos_dup2(d, t), CF_EBADF);
    cf_close(t);

#ifndef COMPAT_HOST
    /* r open: every other handle the table leaves, then no more. */
    rc = 0;
    for (n = 0; n < OLD_USABLE; n++) {
        rc = dos_dup(r, &fill[n]);
        if (rc != 0)
            break;
    }
    TF_EQ(n, OLD_USABLE - 1);
    TF_EQ(rc, CF_EMFILE);
    if (n < OLD_USABLE)
        TF_EQ(fill[n], -1);
    while (n > 0)
        TF_EQ(cf_close(fill[--n]), 0);
#endif

    /* The commit hook fails without a handle taken; a commit that goes
       ahead puts the size on disk, where an open that SHARE allows
       beside t's reads it, and leaves no handle taken either. */
    TF_EQ(tfs_write_all(b, "", 0), 0);
    TF_EQ(cf_open(b, CF_RDWR | CF_DENYNONE, &t), 0);
    TF_EQ(cf_write(t, g_wbuf, NBYTES), 0);
    TF_EQ(dos_dup(t, &e), 0);
    TF_EQ(cf_close(e), 0);
    tfs_fault(TFS_OP(CF_OP_COMMIT), 1, 1, CF_EACCES);
    TF_EQ(dos_commit_handle(t), CF_EACCES);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_COMMIT), 1);
    TF_EQ(dos_dup(t, &d), 0);
    TF_EQ(d, e);
    TF_EQ(cf_close(d), 0);
    TF_EQ(dos_commit_handle(t), 0);
    TF_EQ(tfs_size(b), NBYTES);
    TF_EQ(dos_dup(t, &d), 0);
    TF_EQ(d, e);
    TF_EQ(cf_close(d), 0);
    TF_EQ(cf_close(t), 0);
    TF_EQ(cf_close(r), 0);
    TF_EQ(cf_remove(a), 0);
    TF_EQ(cf_remove(b), 0);
}

/* Free space of the default drive: the host reports the cap, DOS a
   non-zero multiple of 128 (no sector is smaller: VDISK allows 128-byte
   sectors) unless it is capped.  A drive that does not exist gives
   CF_EINVAL under DOS. */
static void test_disk_free(void)
{
    u32 bytes;
    int rc;

    bytes = 0;
    TF_EQ(dos_disk_free(0, &bytes), 0);
    printf("T_CFILE: %lu bytes free\n", (unsigned long)bytes);
#ifdef COMPAT_HOST
    TF_CHECK(bytes == 0xFFFFFFFFUL);
#else
    TF_CHECK(bytes != 0);
    TF_CHECK(bytes == 0xFFFFFFFFUL || bytes % 128UL == 0);
#endif
    rc = dos_disk_free(99, &bytes);
#ifdef COMPAT_HOST
    TF_EQ(rc, 0);
#else
    TF_EQ(rc, CF_EINVAL);
    TF_CHECK(bytes == 0);
#endif
}

/* B: holds no disk: the critical error answered Fail is the error, and
   the next drive that does not exist is CF_EINVAL again. */
static void test_nodisk(void)
{
    u32 bytes;
    int rc;

    bytes = 1;
    rc = dos_disk_free(2, &bytes);
    printf("T_CFILE: empty drive B: gives error %d\n", rc);
    TF_CHECK(rc != 0 && rc != CF_EINVAL);
    TF_CHECK(bytes == 0);
    TF_EQ(dos_disk_free(99, &bytes), CF_EINVAL);
}

/* Raw mode is refused for a file handle under DOS.  The printer device
   on handle 4 is switched to raw and back to cooked, and left in the
   mode it had; the host only reports raw. */
static void test_dev_raw(const char *a)
{
    int h;
    int was;
    int orig;

    TF_EQ(cf_create(a, &h), 0);
    was = -1;
#ifdef COMPAT_HOST
    TF_EQ(dos_dev_raw(h, 1, &was), 0);
    TF_EQ(was, 1);
#else
    TF_EQ(dos_dev_raw(h, 1, &was), CF_EINVAL);
    TF_EQ(was, 0);
#endif
    TF_EQ(cf_close(h), 0);
    TF_EQ(cf_remove(a), 0);

    orig = -1;
    TF_EQ(dos_dev_raw(4, 1, &orig), 0);
    TF_CHECK(orig == 0 || orig == 1);
    was = -1;
    TF_EQ(dos_dev_raw(4, 0, &was), 0);
    TF_EQ(was, 1);
    was = -1;
    TF_EQ(dos_dev_raw(4, orig, &was), 0);
#ifdef COMPAT_HOST
    TF_EQ(was, 1);
#else
    TF_EQ(was, 0);
#endif
}

/* Enlarge the handle table, keep handles open across a second move of
   the table, and hold up to HOPEN read-only opens of one file. */
static void test_handles(const char *a)
{
    int h[HOPEN];
    int f;
    int n;
    int k;
    int rc;
    u16 got;

    TF_EQ(cf_create(a, &f), 0);
    TF_EQ(cf_write(f, "H", 1), 0);
    TF_EQ(cf_close(f), 0);

    TF_EQ(dos_set_handles(24), 0);
    TF_EQ(dos_set_handles(24), 0);
    TF_EQ(dos_set_handles(23), CF_EINVAL);
    rc = 0;
    for (n = 0; n < 3; n++) {
        rc = cf_open(a, CF_READ | CF_DENYNONE, &h[n]);
        TF_EQ(rc, 0);
        if (rc != 0)
            break;
    }
    TF_EQ(dos_set_handles(HTABLE), 0);
    TF_EQ(dos_set_handles(HTABLE - 1), CF_EINVAL);
    TF_EQ(dos_set_handles(20), CF_EINVAL);
    TF_EQ(dos_set_handles(DOS_MAXHANDLES + 1), CF_EINVAL);
    for (; rc == 0 && n < HOPEN; n++) {
        rc = cf_open(a, CF_READ | CF_DENYNONE, &h[n]);
        if (rc != 0)
            break;
    }
    printf("T_CFILE: %d of %d opens held at once\n", n, HOPEN);
    TF_CHECK(n > OLD_USABLE);
    if (n < HOPEN) {
        TF_EQ(rc, CF_EMFILE);
        printf("T_CFILE: too few handles; raise FILES= in CONFIG.SYS to 45\n");
    }

    /* Every handle, those opened before the second move included, reads
       the file from its own position. */
    for (k = 0; k < n; k++) {
        g_rbuf[0] = 0;
        got = 0;
        TF_EQ(cf_read(h[k], g_rbuf, 4, &got), 0);
        TF_EQ(got, 1);
        TF_EQ(g_rbuf[0], 'H');
    }
    for (k = 0; k < n; k++)
        TF_EQ(cf_close(h[k]), 0);
    TF_EQ(cf_open(a, CF_READ, &f), 0);
    TF_EQ(cf_close(f), 0);
    TF_EQ(cf_remove(a), 0);
}

int main(int argc, char **argv)
{
    char a[DOS_PATHMAX];
    char b[DOS_PATHMAX];
    char c[DOS_PATHMAX];
    char d[DOS_PATHMAX];

    cmem_init();
    tf_begin("T_CFILE");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    scratch_name(a, NULL, "CFTEST_A");
    scratch_name(b, NULL, "CFTEST_B");
    scratch_name(c, NULL, "CFTEST_C");
    scratch_name(d, "CFNODIR", "CFTEST_D");
    /* A second install, and later a second remove, must change nothing. */
    dos_crit_install();
    dos_crit_install();
    cf_remove(a);
    cf_remove(b);
    cf_remove(c);
    fill_pattern();

    test_readblk(a);
    test_writeblk(a);
    test_round_trip(a);
    test_seek_size(a);
    test_truncate(a);
    test_names(a, b, c);
    test_nopath(d);
    test_create_new(a, d);
    test_exists(a, b, c, d);
    test_find(d);
    test_today();
    test_time();
    test_dup(a, b);
    test_handles(a);
    test_disk_free();
    test_dev_raw(a);
    if (argc > 2 && strcmp(argv[2], "SHARE") == 0)
        test_share(a);
    if (argc > 2 && strcmp(argv[2], "NODISK") == 0)
        test_nodisk();

    cf_remove(a);
    cf_remove(b);
    cf_remove(c);
    dos_crit_remove();
    dos_crit_remove();
    return tf_done();
}

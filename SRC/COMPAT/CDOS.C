/*
 * CDOS.C - MS-DOS services (INT 21h).
 *
 * This is the only program module that issues INT 21h (the DOS-only
 * tests in SRC/COMPAT/ make their own calls).  Every function used here
 * exists in MS-DOS 3.1 or earlier.  A Windows program makes the same
 * calls but for the services Windows keeps for itself, the handle
 * table, the critical-error and break handlers and the idle call, which
 * its section serves through Windows.
 */
/* The host build uses POSIX file calls, which strict C89 headers hide
   unless the POSIX level is chosen before any system header is read. */
#ifdef HOST_LINUX
#define _POSIX_C_SOURCE 200112L
#endif

#define COMPAT_INTERNAL
#define COMPAT_FIND
#define COMPAT_HEAP
#include "COMPAT.H"

#ifndef COMPAT_HOST
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#endif
#ifdef COMPAT_WIN16
#include "CWIN.H"
#endif

#ifdef COMPAT_HOST
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#define CF_JFTNEW       20      /* handles of a new program's table     */

#ifdef COMPAT_TC
/* Turbo C reads its stack size from this variable at start-up; its
   default is smaller than the stack the engine and the protocol
   dispatcher are budgeted for.  The MAKEFILE and WC65/mkbat.sh read
   this line for the stack the Watcom builds link with (DOS_STACK), so
   every build has the same stack. */
unsigned _stklen = 8192;
#endif

cf_fault_fn cf_fault = NULL;

/* The fault hook's verdict on a disk-changing call op: 0 to go ahead,
   or the error to return. */
static int faulted(int op)
{
    if (cf_fault == NULL)
        return 0;
    return (*cf_fault)(op);
}

cf_fault_fn cf_fault_rd = NULL;

/* The read hook's verdict on cf_open, cf_read or cf_exists (op): 0 to
   go ahead, or the error to return. */
static int rd_faulted(int op)
{
    if (cf_fault_rd == NULL)
        return 0;
    return (*cf_fault_rd)(op);
}

u16 cf_fault_part = 0;

/* The fault hook's verdict on a write of *n bytes: 0 to go ahead, *n cut
   to cf_fault_part when the hook fails a longer write in the partial
   mode, or the error to return. */
static int write_faulted(u16 *n)
{
    int f;

    f = faulted(CF_OP_WRITE);
    if (f != 0 && cf_fault_part != 0 && cf_fault_part < *n) {
        *n = cf_fault_part;
        return 0;
    }
    return f;
}

#ifdef COMPAT_WIN16
/* A near buffer goes as its offset with segment 0, which int21_ds turns
   into the data segment as it issues the call: real-mode Windows may
   move DGROUP during any call in between, which would leave an address
   made here pointing at its old place. */
int cf_read(int h, void *buf, u16 n, u16 *got)
{
    return cf_readfar(h, CFP(u8, 0, (unsigned)buf), n, got);
}

int cf_write(int h, const void *buf, u16 n)
{
    return cf_writefar(h, CFP(const u8, 0, (unsigned)buf), n);
}
#else
/* A near buffer is the far address of its offset in the data segment
   (the stack included, as SS == DS). */
int cf_read(int h, void *buf, u16 n, u16 *got)
{
    return cf_readfar(h, (FARP(u8))buf, n, got);
}

int cf_write(int h, const void *buf, u16 n)
{
    return cf_writefar(h, (FARP(const u8))buf, n);
}
#endif

#ifdef COMPAT_HOST

/* ---------------------------------------------------------------------
 * Host file access.
 *
 * Handles are POSIX file descriptors and errno values are translated into
 * the DOS error codes that the DOS build returns.
 * --------------------------------------------------------------------- */
static int host_err(int e)
{
    switch (e) {
    case ENOENT:
        return CF_ENOENT;
    case ENOTDIR:
        return CF_ENOPATH;
    case EMFILE:
    case ENFILE:
        return CF_EMFILE;
    case EBADF:
        return CF_EBADF;
    case ENOSPC:
        return CF_EFULL;
    default:
        return CF_EACCES;
    }
}

/* POSIX reports a transfer against the wrong access direction (writing
   through a read-only descriptor) as EBADF; DOS reports access denied.
   An open descriptor therefore turns EBADF into CF_EACCES. */
static int host_io_err(int h, int e)
{
    if (e == EBADF && fcntl(h, F_GETFD) != -1)
        return CF_EACCES;
    return host_err(e);
}

/* Longest directory part of a path that host_path_err examines. */
#define CF_HOST_DIRMAX  512

/* POSIX reports ENOENT both for a missing file and for a missing
   directory on the way to it; DOS tells them apart (file not found, path
   not found).  For ENOENT the directory part of path is examined: when
   it does not exist or is not a directory the error is CF_ENOPATH.  A
   name too long for the host is also reported as CF_ENOPATH, as DOS
   does for an over-long path. */
static int host_path_err(const char *path, int e)
{
    char dir[CF_HOST_DIRMAX];
    const char *slash;
    size_t len;
    struct stat st;

    if (e == ENAMETOOLONG)
        return CF_ENOPATH;
    if (e != ENOENT)
        return host_err(e);
    slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
        return CF_ENOENT;
    len = (size_t)(slash - path);
    if (len >= sizeof dir)
        return CF_ENOPATH;
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return CF_ENOPATH;
    return CF_ENOENT;
}

int cf_open(const char *path, u8 mode, int *h)
{
    int flags;
    int fd;
    int f;

    *h = -1;
    f = rd_faulted(CF_OP_OPEN);
    if (f != 0)
        return f;
    switch (mode & 0x03) {
    case CF_READ:
        flags = O_RDONLY;
        break;
    case 0x01:
        flags = O_WRONLY;
        break;
    case CF_RDWR:
        flags = O_RDWR;
        break;
    default:
        return CF_EINVACC;
    }
    fd = open(path, flags);
    if (fd < 0)
        return host_path_err(path, errno);
    *h = fd;
    return 0;
}

/* Create path with the open flags of a create: O_TRUNC for cf_create,
   O_EXCL for cf_create_new, whose EEXIST is DOS's file exists. */
static int create(const char *path, int flags, int *h)
{
    int fd;
    int f;

    *h = -1;
    f = faulted(CF_OP_CREATE);
    if (f != 0)
        return f;
    fd = open(path, O_RDWR | O_CREAT | flags, 0666);
    if (fd < 0)
        return errno == EEXIST ? CF_EEXIST : host_path_err(path, errno);
    *h = fd;
    return 0;
}

int cf_create(const char *path, int *h)
{
    return create(path, O_TRUNC, h);
}

int cf_create_new(const char *path, int *h)
{
    return create(path, O_EXCL, h);
}

/* Close fd; 0 or a CF_E code. */
static int host_close(int fd)
{
    if (close(fd) != 0)
        return host_err(errno);
    return 0;
}

int cf_close(int h)
{
    int f;
    int rc;

    f = faulted(CF_OP_CLOSE);
    rc = host_close(h);
    return f != 0 ? f : rc;
}

int dos_commit_handle(int handle)
{
    int dup_h;
    int f;

    f = faulted(CF_OP_COMMIT);
    if (f != 0)
        return f;
    dup_h = dup(handle);
    if (dup_h < 0)
        return host_err(errno);
    return host_close(dup_h);
}

int cf_readfar(int h, FARP(u8) p, u16 n, u16 *got)
{
    ssize_t r;
    int f;

    *got = 0;
    f = rd_faulted(CF_OP_READ);
    if (f != 0)
        return f;
    do {
        r = read(h, p, (size_t)n);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return host_io_err(h, errno);
    *got = (u16)r;
    return 0;
}

int cf_writefar(int h, FARP(const u8) p, u16 n)
{
    ssize_t r;
    u16 m;
    int f;

    if (n == 0)
        return 0;
    m = n;
    f = write_faulted(&m);
    if (f != 0)
        return f;
    do {
        r = write(h, p, (size_t)m);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return host_io_err(h, errno);
    if (r != (ssize_t)n)
        return CF_EFULL;
    return 0;
}

int cf_seek(int h, u32 pos)
{
    if (lseek(h, (off_t)pos, SEEK_SET) == (off_t)-1)
        return host_err(errno);
    return 0;
}

int cf_size(int h, u32 *size)
{
    off_t cur;
    off_t end;

    *size = 0;
    cur = lseek(h, (off_t)0, SEEK_CUR);
    if (cur == (off_t)-1)
        return host_err(errno);
    end = lseek(h, (off_t)0, SEEK_END);
    if (end == (off_t)-1)
        return host_err(errno);
    if (lseek(h, cur, SEEK_SET) == (off_t)-1)
        return host_err(errno);
    *size = (u32)end;
    return 0;
}

int cf_truncate(int h, u32 size)
{
    int f;

    f = faulted(CF_OP_TRUNC);
    if (f != 0)
        return f;
    if (ftruncate(h, (off_t)size) != 0)
        return host_io_err(h, errno);
    return cf_seek(h, size);
}

int cf_remove(const char *path)
{
    int f;

    f = faulted(CF_OP_REMOVE);
    if (f != 0)
        return f;
    if (unlink(path) != 0)
        return host_path_err(path, errno);
    return 0;
}

/* The checks follow DOS function 56h: a missing source is reported
   first, then a new name that already exists is refused with access
   denied, whereas POSIX rename would replace it, so code that relies on
   replacement fails in host tests as it would under DOS.  A rename to
   another file system (EXDEV), which DOS reports as "not same device",
   falls back to CF_EACCES like every other unmapped error. */
int cf_rename(const char *from, const char *to)
{
    struct stat st;
    int f;

    f = faulted(CF_OP_RENAME);
    if (f != 0)
        return f;
    if (stat(from, &st) != 0)
        return host_path_err(from, errno);
    if (stat(to, &st) == 0)
        return CF_EACCES;
    if (errno != ENOENT)
        return host_path_err(to, errno);
    if (host_path_err(to, ENOENT) == CF_ENOPATH)
        return CF_ENOPATH;
    if (rename(from, to) != 0)
        return host_path_err(to, errno);
    return 0;
}

/* A directory counts as absent, as DOS's find-first, which is not asked
   for directories, reports it. */
int cf_exists(const char *path)
{
    struct stat st;
    int rc;

    rc = rd_faulted(CF_OP_EXISTS);
    if (rc == 0) {
        if (stat(path, &st) == 0)
            return S_ISDIR(st.st_mode) ? 0 : 1;
        rc = host_path_err(path, errno);
    }
    if (rc == CF_ENOENT || rc == CF_ENOPATH)
        return 0;
    return -rc;
}

/* ---------------------------------------------------------------------
 * File search.
 *
 * A dos_find holds the pattern, "" once the search has ended, and at
 * HF_LAST the last name given, "" before the first.  Each call reads the
 * directory and gives the least name after the last that matches, so files
 * removed or renamed meanwhile disturb nothing.
 * --------------------------------------------------------------------- */
#define HF_LAST         DOS_PATHMAX     /* offset of the last name      */

/* Put the 8.3 name s into f (11 bytes) as a directory entry holds it,
   upper-cased and padded with spaces, and return 1; 0 when s is no 8.3
   name.  With wild, s may hold '?' and '*', which fills the rest of its
   part with '?', the characters up to the part's end ignored, as DOS
   does. */
static int host_fcb(const char *s, char *f, int wild)
{
    int part;
    int n;
    int max;
    int c;

    memset(f, ' ', 11);
    for (part = 0; part < 2; part++) {
        max = part == 0 ? 8 : 3;
        for (n = 0; *s != '\0' && *s != '.'; s++) {
            c = (unsigned char)*s;
            if (!wild && (c == '*' || c == '?'))
                return 0;
            if (c == '*') {
                while (n < max)
                    f[part * 8 + n++] = '?';
                while (s[1] != '\0' && s[1] != '.')
                    s++;
            } else if (n == max) {
                return 0;
            } else {
                f[part * 8 + n++] = (char)TXT_UPPER(c);
            }
        }
        if (part == 0 && n == 0)
            return 0;
        if (part == 0 && *s == '.')
            s++;
    }
    return *s == '\0';
}

/* 1 when the entry form name matches the pattern form pat, '?' matching
   any character, the padding included. */
static int host_match(const char *pat, const char *name)
{
    int i;

    for (i = 0; i < 11; i++)
        if (pat[i] != '?' && pat[i] != name[i])
            return 0;
    return 1;
}

/* The next name of the search f into name, as described above. */
static int host_find(dos_find *f, char *name)
{
    char *pat;
    char *last;
    char path[DOS_PATHMAX + DOS_FINDNAME];
    char want[11];
    char have[11];
    const char *base;
    struct dirent *e;
    struct stat st;
    size_t dlen;
    DIR *d;
    int rc;

    pat = (char *)f->b;
    last = (char *)f->b + HF_LAST;
    name[0] = '\0';
    if (pat[0] == '\0')
        return CF_ENOENT;
    base = strrchr(pat, '/');
    base = base != NULL ? base + 1 : pat;
    dlen = (size_t)(base - pat);
    memcpy(path, pat, dlen);
    path[dlen] = '\0';
    d = opendir(dlen == 0 ? "." : path);
    if (d == NULL) {
        rc = errno == ENOENT || errno == ENOTDIR ? CF_ENOPATH :
             host_err(errno);
        pat[0] = '\0';
        return rc;
    }
    if (host_fcb(base, want, 1)) {
        while ((e = readdir(d)) != NULL) {
            if (!host_fcb(e->d_name, have, 0) || !host_match(want, have) ||
                strcmp(e->d_name, last) <= 0 ||
                (name[0] != '\0' && strcmp(e->d_name, name) >= 0))
                continue;
            strcpy(path + dlen, e->d_name);
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
                strcpy(name, e->d_name);
        }
    }
    closedir(d);
    if (name[0] == '\0') {
        pat[0] = '\0';
        return CF_ENOENT;
    }
    strcpy(last, name);
    return 0;
}

int dos_find_first(dos_find *f, const char *pattern, char *name)
{
    name[0] = '\0';
    f->b[0] = '\0';
    if (strlen(pattern) >= DOS_PATHMAX)
        return CF_EINVAL;
    strcpy((char *)f->b, pattern);
    f->b[HF_LAST] = '\0';
    return host_find(f, name);
}

int dos_find_next(dos_find *f, char *name)
{
    return host_find(f, name);
}

int cf_mkdir(const char *path)
{
    if (mkdir(path, 0777) != 0)
        return host_path_err(path, errno);
    return 0;
}

/* A directory that is not there is a path not found, as under DOS. */
int cf_rmdir(const char *path)
{
    if (rmdir(path) != 0)
        return errno == ENOENT ? CF_ENOPATH : host_err(errno);
    return 0;
}

void dos_today(u16 *year, u8 *month, u8 *day)
{
    time_t now;
    struct tm *tm;

    now = time(NULL);
    tm = localtime(&now);
    if (tm == NULL) {
        *year = 1980;
        *month = 1;
        *day = 1;
        return;
    }
    *year = (u16)(tm->tm_year + 1900);
    *month = (u8)(tm->tm_mon + 1);
    *day = (u8)tm->tm_mday;
}

void dos_time(u8 *hour, u8 *min, u8 *sec)
{
    time_t now;
    struct tm *tm;

    now = time(NULL);
    tm = localtime(&now);
    if (tm == NULL) {
        *hour = 0;
        *min = 0;
        *sec = 0;
        return;
    }
    *hour = (u8)tm->tm_hour;
    *min = (u8)tm->tm_min;
    /* POSIX allows a leap second, 60; it is given as 59, as MS-DOS
       3.10's second 60 is. */
    *sec = (u8)(tm->tm_sec > 59 ? 59 : tm->tm_sec);
}

int dos_version(u8 *major, u8 *minor)
{
    *major = 3;
    *minor = 10;
    return 1;
}

/* The host has no PSP handle table; only the size a DOS program would
   have is kept, so that the argument checks match the DOS build. */
static u16 cf_host_handles = CF_JFTNEW;

int dos_set_handles(u16 n)
{
    if (n < cf_host_handles || n > DOS_MAXHANDLES)
        return CF_EINVAL;
    cf_host_handles = n;
    return 0;
}

void dos_disk_reset(void)
{
}

void dos_idle(void)
{
}

void dos_crit_install(void)
{
}

void dos_crit_remove(void)
{
}

int dos_machine_name(char *name)
{
    name[0] = '\0';
    return 1;
}

void dos_break_off(void)
{
}

void dos_break_restore(void)
{
}

int dos_break_get(void)
{
    return 0;
}

u32 dos_free_host = 0xFFFFFFFFUL;

int dos_disk_free(u8 drive, u32 *bytes)
{
    (void)drive;
    *bytes = dos_free_host;
    return 0;
}

int dos_dev_raw(int handle, int raw, int *was)
{
    (void)handle;
    (void)raw;
    *was = 1;
    return 0;
}

void cstk_fill(void)
{
}

u16 cstk_unused(void)
{
    return CSTK_UNKNOWN;
}

#else

/* Clear every register of r, then set AX to ax: each call below starts
   so, and passes 0 in every input register it does not set. */
static void regs_ax(union REGS *r, u16 ax)
{
    memset(r, 0, sizeof *r);
    r->x.ax = ax;
}

/* ---------------------------------------------------------------------
 * DOS file access.
 *
 * The handle calls exist in MS-DOS 2.0 (3Ch create, 3Dh open, 3Eh
 * close, 3Fh read, 40h write, 41h delete, 42h seek, 45h duplicate, 56h
 * rename, 39h/3Ah make/remove directory); the sharing bits of the 3Dh
 * open mode, the create-new 5Bh and the extended error 59h exist from
 * DOS 3.0.
 *
 * Paths and buffers are passed as offsets in DS (and ES for the second
 * name of a rename), which is right only when data pointers are near, as
 * in the small and medium models used for every executable.  A pointer
 * to a local (stack) buffer is an offset in SS, so this also relies on
 * SS == DS, which holds in those models (DGROUP holds data and stack).
 * --------------------------------------------------------------------- */
#if defined(__COMPACT__) || defined(__LARGE__) || defined(__HUGE__)
#error CDOS.C file access needs near data (small or medium model)
#endif
#if defined(M_I86CM) || defined(M_I86LM) || defined(M_I86HM)
#error CDOS.C file access needs near data (small or medium model)
#endif

/* Error code DOS gives a call whose critical error was answered Fail. */
#define CF_EFAIL24      0x53

#ifndef COMPAT_WIN16
/* Written by the critical-error handler (dos_crit_install): the
   extended error code it read with function 59h and the DI it was
   entered with; 0xFFFF when no critical error is pending. */
static volatile u16 cf_crit_ext = 0xFFFFu;
static volatile u16 cf_crit_di = 0xFFFFu;
#endif

/* ---------------------------------------------------------------------
 * Extended error.
 *
 * The extended error of the last failed DOS call (function 59h, BX = 0,
 * DOS 3.0) is the code in AX.  On DOS 3.x function 59h also changes CL,
 * DX, SI, DI, BP, DS and ES.  The compilers' intdosx wrappers do not
 * survive that: they store the result registers through DS, which then
 * no longer addresses the data segment, so the store lands elsewhere and
 * the program goes astray (DOS 3.10 without SHARE).  The call is
 * therefore made by inline machine code that saves every register it
 * may change except AX and BX:
 *     PUSH BP SI DI DS ES CX DX    55 56 57 1E 06 51 52
 *     XOR BX,BX                    31 DB
 *     MOV AH,59h                   B4 59
 *     INT 21h                      CD 21
 *     POP DX CX ES DS DI SI BP     5A 59 07 1F 5F 5E 5D
 * The Watcom compilers expand it in place from the aux pragma, which
 * tells them the result is in AX and only AX and BX change, so no
 * symbol is emitted or referenced; Turbo C emits the bytes into the
 * function body and reads AX afterwards.
 * --------------------------------------------------------------------- */
#ifdef COMPAT_TC
static u16 exterr(void)
{
    __emit__(0x55, 0x56, 0x57, 0x1E, 0x06, 0x51, 0x52,
             0x31, 0xDB, 0xB4, 0x59, 0xCD, 0x21,
             0x5A, 0x59, 0x07, 0x1F, 0x5F, 0x5E, 0x5D);
    return (u16)_AX;
}
#else
/* Private to this file but not static: Watcom C 6.5 rejects a static
   function whose body is an aux pragma (E055). */
u16 exterr(void);
#pragma aux exterr = \
    0x55 0x56 0x57 0x1E 0x06 0x51 0x52 \
    0x31 0xDB 0xB4 0x59 0xCD 0x21 \
    0x5A 0x59 0x07 0x1F 0x5F 0x5E 0x5D \
    value [ax] modify [ax bx];
#endif

#ifdef COMPAT_WIN16
/* Windows answers the critical errors (dos_crit_install), and nothing
   records them: a failed call's error is its own. */
#define crit_take(rc, failed)   (rc)
#else
/* The error of a failed call: rc, or when failed is nonzero (the call's
   critical error was answered Fail) the error the critical-error handler
   recorded, that is the extended error it read, else the critical error
   code in the low byte of DI plus 13h, or rc if it recorded nothing.
   Clears the record. */
static int crit_take(int rc, int failed)
{
    if (failed) {
        if (cf_crit_ext != 0xFFFFu && cf_crit_ext != 0)
            rc = (int)cf_crit_ext;
        else if (cf_crit_di != 0xFFFFu)
            rc = (int)(cf_crit_di & 0xFFu) + 0x13;
    }
    cf_crit_ext = 0xFFFFu;
    cf_crit_di = 0xFFFFu;
    return rc;
}
#endif

#ifdef COMPAT_WIN16
/* INT 21h with the registers of r, ES the data segment and DS ds, or
   for 0 the data segment too, both taken from DS right before the
   interrupt, and the result registers and carry back in r: DS is the
   one copy of DGROUP's address that real-mode Windows keeps up to date
   when it moves DGROUP, as the library's intdosx, a call into code
   that may have to be loaded, could make it do.  r lies in DGROUP, as
   does the stack, and is reached through SS:BP after the call; the
   carry comes back as 0 or FFFFh (SBB AX,AX). */
void int21_regs(union REGS *r, u16 ds);
#pragma aux int21_regs = \
    "push ds" \
    "push es" \
    "push bp" \
    "push bx" \
    "test ax,ax" \
    "jnz have" \
    "mov ax,ds" \
    "have:" \
    "push ax" \
    "mov ax,ds" \
    "mov es,ax" \
    "mov ax,[bx]" \
    "mov cx,[bx+4]" \
    "mov dx,[bx+6]" \
    "mov si,[bx+8]" \
    "mov di,[bx+10]" \
    "mov bx,[bx+2]" \
    "pop ds" \
    "int 21h" \
    "mov bp,sp" \
    "mov bp,[bp]" \
    "mov [bp],ax" \
    "mov [bp+2],bx" \
    "mov [bp+4],cx" \
    "mov [bp+6],dx" \
    "mov [bp+8],si" \
    "mov [bp+10],di" \
    "sbb ax,ax" \
    "mov [bp+12],ax" \
    "pop bx" \
    "pop bp" \
    "pop es" \
    "pop ds" \
    parm [bx] [ax] \
    modify [ax cx dx si di];
#endif

/* Issue INT 21h with ES set to the program's data segment and DS to ds,
   or for 0 (never a buffer's segment: the interrupt vectors lie there)
   to the data segment too.  Returns 0, or when carry is set the
   extended error code (exterr, function 59h).  The code in AX is less
   precise: an open that conflicts with another open's sharing mode, for
   example, reports access denied in AX and sharing violation as the
   extended error.  AX is kept if the extended error reads as 0.  When
   the call failed because the critical-error handler answered Fail, the
   error is CF_EFAIL24, and the extended error the handler read while
   DOS was reporting the critical error is returned instead (for a
   create over a file held deny-read/write that is the sharing violation
   CF_ESHARE).  If the handler read 0, the critical error code DOS
   passed in the low byte of DI is used, converted to an error code by
   adding 13h; DOS 3.10 passes 0Ch (general failure, error 31) for that
   sharing violation, which is why the extended error is preferred. */
static int int21_ds(union REGS *r, u16 ds)
{
#ifndef COMPAT_WIN16
    struct SREGS s;
#endif
    u16 ext;
    int rc;

#ifdef COMPAT_WIN16
    int21_regs(r, ds);
#else
    segread(&s);
    s.es = s.ds;
    if (ds != 0)
        s.ds = ds;
    intdosx(r, r, &s);
#endif
    if (!r->x.cflag)
        return 0;
    rc = (int)r->x.ax;
    ext = exterr();
    if (ext != 0)
        rc = (int)ext;
    return crit_take(rc, rc == CF_EFAIL24);
}

/* int21_ds with DS set to the program's data segment. */
static int int21(union REGS *r)
{
    return int21_ds(r, 0);
}

int cf_open(const char *path, u8 mode, int *h)
{
    union REGS r;
    int rc;
    int f;

    *h = -1;
    f = rd_faulted(CF_OP_OPEN);
    if (f != 0)
        return f;
    if ((mode & 0x03) == 0x03)
        return CF_EINVACC;
    regs_ax(&r, (u16)(0x3D00u | mode));
    r.x.dx = (unsigned)path;
    rc = int21(&r);
    if (rc == 0)
        *h = (int)r.x.ax;
    return rc;
}

/* Create path with function ax (3Ch or 5Bh), CX 0 for a normal file. */
static int create(const char *path, u16 ax, int *h)
{
    union REGS r;
    int rc;
    int f;

    *h = -1;
    f = faulted(CF_OP_CREATE);
    if (f != 0)
        return f;
    regs_ax(&r, ax);
    r.x.dx = (unsigned)path;
    rc = int21(&r);
    if (rc == 0)
        *h = (int)r.x.ax;
    return rc;
}

int cf_create(const char *path, int *h)
{
    return create(path, 0x3C00, h);
}

int cf_create_new(const char *path, int *h)
{
    return create(path, 0x5B00, h);
}

/* Close handle h (function 3Eh, DOS 2.0); 0 or a CF_E code. */
static int close_handle(int h)
{
    union REGS r;

    regs_ax(&r, 0x3E00);
    r.x.bx = (unsigned)h;
    return int21(&r);
}

int cf_close(int h)
{
    int f;
    int rc;

    f = faulted(CF_OP_CLOSE);
    rc = close_handle(h);
    return f != 0 ? f : rc;
}

int dos_commit_handle(int handle)
{
    union REGS r;
    int rc;
    int f;

    f = faulted(CF_OP_COMMIT);
    if (f != 0)
        return f;
    regs_ax(&r, 0x4500);
    r.x.bx = (unsigned)handle;
    rc = int21(&r);
    if (rc != 0)
        return rc;
    return close_handle((int)r.x.ax);
}

int cf_readfar(int h, FARP(u8) p, u16 n, u16 *got)
{
    union REGS r;
    int rc;
    int f;

    *got = 0;
    f = rd_faulted(CF_OP_READ);
    if (f != 0)
        return f;
    regs_ax(&r, 0x3F00);
    r.x.bx = (unsigned)h;
    r.x.cx = n;
    r.x.dx = FP_OFF(p);
    rc = int21_ds(&r, FP_SEG(p));
    if (rc == 0)
        *got = (u16)r.x.ax;
    return rc;
}

/* A zero-length write (CX = 0) would truncate the file at the current
   position, so it is never issued here; cf_truncate does that. */
int cf_writefar(int h, FARP(const u8) p, u16 n)
{
    union REGS r;
    u16 m;
    int rc;
    int f;

    if (n == 0)
        return 0;
    m = n;
    f = write_faulted(&m);
    if (f != 0)
        return f;
    regs_ax(&r, 0x4000);
    r.x.bx = (unsigned)h;
    r.x.cx = m;
    r.x.dx = FP_OFF(p);
    rc = int21_ds(&r, FP_SEG(p));
    if (rc == 0 && (u16)r.x.ax != n)
        rc = CF_EFULL;
    return rc;
}

/* Function 42h: move the position of handle h by CX:DX = off relative to
   the start (method 0), the current position (1) or the end (2).  *pos
   receives the new position from DX:AX. */
static int seek_from(int h, u8 method, u32 off, u32 *pos)
{
    union REGS r;
    int rc;

    regs_ax(&r, (u16)(0x4200u | method));
    r.x.bx = (unsigned)h;
    r.x.cx = (unsigned)(off >> 16);
    r.x.dx = (unsigned)(off & 0xFFFFUL);
    rc = int21(&r);
    if (rc == 0)
        *pos = ((u32)r.x.dx << 16) | (u32)r.x.ax;
    return rc;
}

int cf_seek(int h, u32 pos)
{
    u32 at;

    return seek_from(h, 0, pos, &at);
}

int cf_size(int h, u32 *size)
{
    u32 cur;
    u32 end;
    int rc;

    *size = 0;
    rc = seek_from(h, 1, 0UL, &cur);
    if (rc != 0)
        return rc;
    rc = seek_from(h, 2, 0UL, &end);
    if (rc != 0)
        return rc;
    rc = seek_from(h, 0, cur, &cur);
    if (rc != 0)
        return rc;
    *size = end;
    return 0;
}

/* A write of zero bytes (function 40h, CX = 0) sets the file size to the
   current position, shrinking or extending the file. */
int cf_truncate(int h, u32 size)
{
    union REGS r;
    int rc;
    int f;

    f = faulted(CF_OP_TRUNC);
    if (f != 0)
        return f;
    rc = cf_seek(h, size);
    if (rc != 0)
        return rc;
    regs_ax(&r, 0x4000);
    r.x.bx = (unsigned)h;
    return int21(&r);
}

int cf_remove(const char *path)
{
    union REGS r;
    int f;

    f = faulted(CF_OP_REMOVE);
    if (f != 0)
        return f;
    regs_ax(&r, 0x4100);
    r.x.dx = (unsigned)path;
    return int21(&r);
}

/* Function 56h: DS:DX old name, ES:DI new name.  DOS refuses a new name
   that already exists (access denied) or is on another drive. */
int cf_rename(const char *from, const char *to)
{
    union REGS r;
    int f;

    f = faulted(CF_OP_RENAME);
    if (f != 0)
        return f;
    regs_ax(&r, 0x5600);
    r.x.dx = (unsigned)from;
    r.x.di = (unsigned)to;
    return int21(&r);
}

/* ---------------------------------------------------------------------
 * File search.
 *
 * Find-first (function 4Eh, DOS 2.0) and find-next (4Fh) write the match,
 * and keep the state of the search, in the disk transfer area (DTA): the
 * 43-byte record of a dos_find, or the local one of cf_exists.  It is made
 * the DTA for the call (function 1Ah, DOS 1.0) and the program's own DTA,
 * read with function 2Fh (DOS 2.0), is set back afterwards.  "No more
 * files" is how DOS reports a pattern that matches nothing with the
 * attributes asked for, a directory for one.  The byte of a dos_find after
 * its DTA is 1 while its search goes on.
 * --------------------------------------------------------------------- */
#define CF_FIND_ATTR    0x07    /* read-only, hidden and system files   */
#define CF_ENOMORE      18      /* no more files                        */
#define CF_FLIVE        DOS_DTASIZE /* the dos_find byte after it       */

/* A dos_find holds the DTA and its flag. */
extern char cf_findsz_ok[1];
extern char cf_findsz_ok[(DOS_FINDSZ > CF_FLIVE) ? 1 : 2];

/* Issue find call ax (4E00h or 4F00h) with the attributes attr and
   pattern, the DTA at dta, the program's own set back afterwards: 0 or
   a CF_E code, "no more files" given as CF_ENOENT. */
static int find_in(u16 ax, u8 attr, const char *pattern, u8 *dta)
{
    union REGS r;
    union REGS d;
    struct SREGS s;
    u16 oseg;
    u16 ooff;
    int rc;

    regs_ax(&d, 0x2F00);
    segread(&s);
    intdosx(&d, &d, &s);
    oseg = (u16)s.es;
    ooff = (u16)d.x.bx;
    regs_ax(&d, 0x1A00);
    d.x.dx = (unsigned)dta;
    intdos(&d, &d);
    regs_ax(&r, ax);
    r.x.cx = attr;
    r.x.dx = (unsigned)pattern;
    rc = int21(&r);
    regs_ax(&d, 0x1A00);
    d.x.dx = ooff;
    segread(&s);
    s.ds = oseg;
    intdosx(&d, &d, &s);
    return rc == CF_ENOMORE ? CF_ENOENT : rc;
}

int dos_find_entry(const char *pattern, u8 attr, u8 *dta)
{
    return find_in(0x4E00, attr, pattern, dta);
}

/* Find-first answers for a missing name on every drive, where an open
   of one on a DOSBox-X host-mounted drive can report access denied. */
int cf_exists(const char *path)
{
    u8 dta[DOS_DTASIZE];
    int rc;

    rc = rd_faulted(CF_OP_EXISTS);
    if (rc == 0) {
        rc = dos_find_entry(path, CF_FIND_ATTR, dta);
        if (rc == 0)
            return 1;
    }
    if (rc == CF_ENOENT || rc == CF_ENOPATH)
        return 0;
    return -rc;
}

/* The end of a find on f that returned rc: the name found into name,
   and whether the search goes on; returns rc. */
static int find_step(dos_find *f, int rc, char *name)
{
    f->b[CF_FLIVE] = (u8)(rc == 0);
    if (rc == 0) {
        memcpy(name, f->b + DOS_DTANAME, DOS_FINDNAME - 1);
        name[DOS_FINDNAME - 1] = '\0';
    }
    return rc;
}

/* CX 0 asks for normal files only. */
int dos_find_first(dos_find *f, const char *pattern, char *name)
{
    name[0] = '\0';
    f->b[CF_FLIVE] = 0;
    if (strlen(pattern) >= DOS_PATHMAX)
        return CF_EINVAL;
    return find_step(f, dos_find_entry(pattern, 0, f->b), name);
}

int dos_find_next(dos_find *f, char *name)
{
    name[0] = '\0';
    if (!f->b[CF_FLIVE])
        return CF_ENOENT;
    return find_step(f, find_in(0x4F00, 0, NULL, f->b), name);
}

int cf_mkdir(const char *path)
{
    union REGS r;

    regs_ax(&r, 0x3900);
    r.x.dx = (unsigned)path;
    return int21(&r);
}

int cf_rmdir(const char *path)
{
    union REGS r;

    regs_ax(&r, 0x3A00);
    r.x.dx = (unsigned)path;
    return int21(&r);
}

/* Function 0Dh (DOS 1.0): write every modified DOS buffer, directory
   sectors included, to its disk.  It reports no error. */
void dos_disk_reset(void)
{
    union REGS r;

    regs_ax(&r, 0x0D00);
    intdos(&r, &r);
}

#ifdef COMPAT_WIN16
void (*dos_yield)(void) = NULL;
u16 dos_inst;

void dos_idle(void)
{
    if (dos_yield != NULL)
        (*dos_yield)();
}
#else
void dos_idle(void)
{
    union REGS r;

    regs_ax(&r, 0);
    int86(0x28, &r, &r);
}
#endif

/* Function 2Ah: CX year, DH month, DL day. */
void dos_today(u16 *year, u8 *month, u8 *day)
{
    union REGS r;

    regs_ax(&r, 0x2A00);
    intdos(&r, &r);
    *year = (u16)r.x.cx;
    *month = (u8)r.h.dh;
    *day = (u8)r.h.dl;
}

/* Function 2Ch: CH hour, CL minute, DH second, DL hundredths. */
void dos_time(u8 *hour, u8 *min, u8 *sec)
{
    union REGS r;

    regs_ax(&r, 0x2C00);
    intdos(&r, &r);
    *hour = (u8)r.h.ch;
    *min = (u8)r.h.cl;
    /* The CLOCK$ driver of MS-DOS 3.10 reads the last tick of two
       minutes in every five as second 60 of that minute (0:00:60 for
       tick 1092, 59.98 s); 59 keeps such a reading in range and in
       order. */
    *sec = (u8)(r.h.dh > 59 ? 59 : r.h.dh);
}

/* Function 30h (DOS 2.0): AL major, AH minor version.  DOS 1.x returns
   without setting AL, which the zeroed registers turn into major 0. */
int dos_version(u8 *major, u8 *minor)
{
    union REGS r;

    regs_ax(&r, 0x3000);
    intdos(&r, &r);
    *major = (u8)r.h.al;
    *minor = (u8)r.h.ah;
    return *major > 3 || (*major == 3 && *minor >= 10);
}

#ifdef COMPAT_WIN16
/* ---------------------------------------------------------------------
 * The services Windows keeps.
 *
 * Windows gives each task its own handle table, which SetHandleCount
 * enlarges, and owns INT 23h and INT 24h: a program only sets the error
 * mode that tells Windows to fail a call on a critical error at once,
 * rather than ask the user, and leaves Ctrl-Break to it.
 * --------------------------------------------------------------------- */
static u16 cf_handles = CF_JFTNEW; /* the task's handle table now       */

int dos_set_handles(u16 n)
{
    if (n < cf_handles || n > DOS_MAXHANDLES)
        return CF_EINVAL;
    if (SetHandleCount(n) < n)
        return CF_ENOMEM;
    cf_handles = n;
    return 0;
}

static UINT cf_old_mode;        /* the error mode dos_crit_install found */
static u8 cf_crit_on;

void dos_crit_install(void)
{
    if (cf_crit_on)
        return;
    cf_old_mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    cf_crit_on = 1;
}

void dos_crit_remove(void)
{
    if (!cf_crit_on)
        return;
    SetErrorMode(cf_old_mode);
    cf_crit_on = 0;
}

void dos_break_off(void)
{
}

void dos_break_restore(void)
{
}
#else
/* ---------------------------------------------------------------------
 * Per-process handle table.
 *
 * A DOS handle is an index into the job file table (JFT) of the program's
 * PSP; each entry holds the number of the system file table entry it uses,
 * 0FFh when free.  DOS 3.0 and later find the table through the PSP's far
 * pointer at offset 34h (offset word first, then segment) and take its
 * size from the word at offset 32h; a new PSP points them at its own
 * CF_JFTNEW entries at offset 18h.  The table is reached through far
 * pointers, and interrupts are held off while the entries are copied and
 * the PSP fields rewritten, so that nothing runs against a half-written
 * pointer.
 * Interrupts are enabled afterwards whatever their state on entry, so a
 * caller must not have them off.
 * --------------------------------------------------------------------- */

#define CF_PSP_JFTSIZE  0x32    /* word: entries in the handle table    */
#define CF_PSP_JFTPTR   0x34    /* far pointer to the handle table      */
#define CF_JFT_FREE     0xFFu   /* a handle table entry not in use      */

/* The table installed by the last successful dos_set_handles, or NULL. */
static u8 *cf_jft;

/* Segment of the running program's PSP (function 51h, BX). */
static u16 psp_seg(void)
{
    union REGS r;

    regs_ax(&r, 0x5100);
    intdos(&r, &r);
    return (u16)r.x.bx;
}

int dos_set_handles(u16 n)
{
    FARP(u8) psp;
    FARP(u8) from;
    FARP(u8) to;
    u8 *tab;
    u16 cur;
    u16 i;

    psp = CFP(u8, psp_seg(), 0);
    cur = get_le16(psp + CF_PSP_JFTSIZE);
    if (n < cur || n > DOS_MAXHANDLES)
        return CF_EINVAL;
    if (n == cur)
        return 0;
    tab = (u8 *)malloc((size_t)n);
    if (tab == NULL)
        return CF_ENOMEM;
    to = (FARP(u8))tab;
    IRQ_OFF();
    from = CFP(u8, get_le16(psp + CF_PSP_JFTPTR + 2),
               get_le16(psp + CF_PSP_JFTPTR));
    for (i = 0; i < cur; i++)
        to[i] = from[i];
    for (; i < n; i++)
        to[i] = (u8)CF_JFT_FREE;
    put_le16(psp + CF_PSP_JFTPTR, FP_OFF(to));
    put_le16(psp + CF_PSP_JFTPTR + 2, FP_SEG(to));
    put_le16(psp + CF_PSP_JFTSIZE, n);
    IRQ_ON();
    free(cf_jft);
    cf_jft = tab;
    return 0;
}

/* ---------------------------------------------------------------------
 * Interrupt vectors.
 *
 * Function 35h reads one into ES:BX, function 25h points one at DS:DX.
 * --------------------------------------------------------------------- */
void dos_getvect(u8 n, u16 *seg, u16 *off)
{
    union REGS r;
    struct SREGS s;

    regs_ax(&r, (u16)(0x3500u | n));
    segread(&s);
    intdosx(&r, &r, &s);
    *seg = (u16)s.es;
    *off = (u16)r.x.bx;
}

void dos_setvect(u8 n, u16 seg, u16 off)
{
    union REGS r;
    struct SREGS s;

    regs_ax(&r, (u16)(0x2500u | n));
    r.x.dx = off;
    segread(&s);
    s.ds = seg;
    intdosx(&r, &r, &s);
}

/* ---------------------------------------------------------------------
 * Critical-error handler.
 *
 * DOS calls INT 24h with flags in AH and the critical error code in the
 * low byte of DI, and expects the answer in AL; 3 means Fail (DOS 3.0 and
 * later), so the DOS call that hit the error returns with carry set.
 * Inside INT 24h a handler may call function 59h, which then returns the
 * extended error of the call in progress, a finer code than DI (sharing
 * violation rather than general failure, for example).  The handler is
 * machine code kept in the data segment, so the vector's segment is the
 * data segment and CS addresses it while the handler runs:
 *     PUSH AX BX CX DX SI DI BP DS ES   50 53 51 52 56 57 55 1E 06
 *     MOV CS:[cf_crit_di],DI            2E 89 3E lo hi
 *     XOR BX,BX                         31 DB
 *     MOV AH,59h                        B4 59
 *     INT 21h                           CD 21
 *     MOV CS:[cf_crit_ext],AX           2E A3 lo hi
 *     POP ES DS BP DI SI DX CX BX AX    07 1F 5D 5F 5E 5A 59 5B 58
 *     MOV AL,3                          B0 03
 *     TEST AH,8                         F6 C4 08
 *     JNZ done                          75 02
 *     MOV AL,1                          B0 01
 * done: IRET                            CF
 * Function 59h changes BX, CX, DX, SI, DI, BP, DS and ES, so all are
 * saved; the handler returns with only AL changed.  It is the same for
 * every compiler and memory model; dos_crit_install fills in the
 * offsets of cf_crit_di and cf_crit_ext.
 *
 * DOS allows Fail only when bit 3 of AH is set on entry, and would take
 * a Fail it does not allow as Abort, ending the program at once without
 * the C run-time's cleanup, its exit functions or the program's own
 * exit path, leaving a serial port's interrupt handler or a packet
 * driver's receiver in place.  Such an error is answered Retry
 * (AL = 1) instead: the call waits until the error clears (a disk put
 * in, a server back).  On DOS 3.x Fail is allowed for errors on handle
 * calls such as the ones in this file.
 *
 * A program started through EXEC inherits the vector, and with it this
 * handler, until it sets its own.  DOS reloads the INT 24h vector from
 * the program's PSP when the program terminates.
 * --------------------------------------------------------------------- */
#define CF_CRIT_DI      12      /* offset of the cf_crit_di address     */
#define CF_CRIT_EXT     22      /* offset of the cf_crit_ext address    */
#define CF_CRIT_IRET    42      /* offset of the closing IRET           */
static u8 cf_crit_code[CF_CRIT_IRET + 1] = {
    0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x1E, 0x06,
    0x2E, 0x89, 0x3E, 0, 0,
    0x31, 0xDB,
    0xB4, 0x59,
    0xCD, 0x21,
    0x2E, 0xA3, 0, 0,
    0x07, 0x1F, 0x5D, 0x5F, 0x5E, 0x5A, 0x59, 0x5B, 0x58,
    0xB0, 0x03,
    0xF6, 0xC4, 0x08,
    0x75, 0x02,
    0xB0, 0x01,
    0xCF
};
static u16 cf_old24_seg;
static u16 cf_old24_off;
static u8 cf_crit_on;

void dos_crit_install(void)
{
    struct SREGS s;
    unsigned a;

    if (cf_crit_on)
        return;
    a = (unsigned)&cf_crit_di;
    cf_crit_code[CF_CRIT_DI] = (u8)(a & 0xFFu);
    cf_crit_code[CF_CRIT_DI + 1] = (u8)(a >> 8);
    a = (unsigned)&cf_crit_ext;
    cf_crit_code[CF_CRIT_EXT] = (u8)(a & 0xFFu);
    cf_crit_code[CF_CRIT_EXT + 1] = (u8)(a >> 8);
    cf_crit_di = 0xFFFFu;
    cf_crit_ext = 0xFFFFu;
    dos_getvect(0x24, &cf_old24_seg, &cf_old24_off);
    segread(&s);
    dos_setvect(0x24, (u16)s.ds, (u16)(unsigned)cf_crit_code);
    cf_crit_on = 1;
}

/* The saved vector is put back only while the vector still points at
   this handler.  If something has hooked INT 24h since, restoring would
   cut that hook out, so the vector is left alone; the handler then
   stays reachable until the program ends, when DOS reloads the vector
   from the PSP. */
void dos_crit_remove(void)
{
    struct SREGS s;
    u16 seg;
    u16 off;

    if (!cf_crit_on)
        return;
    cf_crit_on = 0;
    dos_getvect(0x24, &seg, &off);
    segread(&s);
    if (seg == (u16)s.ds && off == (u16)(unsigned)cf_crit_code)
        dos_setvect(0x24, cf_old24_seg, cf_old24_off);
}
#endif

/* ---------------------------------------------------------------------
 * Machine name.
 *
 * Function 5E00h (DOS 3.1): DS:DX points at a 16-byte buffer that receives
 * the name, padded with spaces and ended by a NUL; CH is 0 when no name
 * has been set.  Without network software DOS either reports no name or
 * refuses the function with carry set.
 * --------------------------------------------------------------------- */
int dos_machine_name(char *name)
{
    union REGS r;
    char buf[DOS_MACHLEN + 1];
    int n;

    name[0] = '\0';
    memset(buf, 0, sizeof buf);
    regs_ax(&r, 0x5E00);
    r.x.dx = (unsigned)buf;
    if (int21(&r) != 0 || r.h.ch == 0)
        return 1;
    for (n = 0; n < DOS_MACHLEN && buf[n] != '\0'; n++)
        name[n] = buf[n];
    while (n > 0 && name[n - 1] == ' ')
        n--;
    name[n] = '\0';
    return n == 0 ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * Ctrl-C and Ctrl-Break.
 *
 * Function 33h (DOS 2.0) reads (AL 00h, result in DL) and sets (AL 01h, DL
 * the new state) the BREAK setting.  The INT 23h handler is the
 * critical-error handler's closing IRET, in the data segment: DOS then
 * carries on with the call it was making when it saw the break.  The
 * vector is set with function 25h and not restored, since DOS reloads
 * INT 23h from the PSP when the program ends.
 * --------------------------------------------------------------------- */
#ifndef COMPAT_WIN16
static u8 cf_break_was;         /* BREAK found + 1, 0 while not off     */

void dos_break_off(void)
{
    union REGS r;
    struct SREGS s;

    if (cf_break_was)
        return;
    cf_break_was = (u8)(dos_break_get() + 1);
    regs_ax(&r, 0x3301);
    intdos(&r, &r);
    segread(&s);
    dos_setvect(0x23, (u16)s.ds,
                (u16)(unsigned)&cf_crit_code[CF_CRIT_IRET]);
}

void dos_break_restore(void)
{
    union REGS r;

    if (!cf_break_was)
        return;
    regs_ax(&r, 0x3301);
    r.h.dl = (u8)(cf_break_was - 1);
    intdos(&r, &r);
    cf_break_was = 0;
}
#endif

int dos_break_get(void)
{
    union REGS r;

    regs_ax(&r, 0x3300);
    intdos(&r, &r);
    return r.h.dl != 0 ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * Free disk space.
 *
 * Function 36h (DOS 2.0): DL the drive (0 default, 1 A:), AX sectors per
 * cluster, BX free clusters, CX bytes per sector.  AX is FFFFh, with carry
 * clear, both for a drive that does not exist and for one whose critical
 * error (no disk in the drive) the handler answered Fail; only the
 * handler's record tells them apart.  Sectors per cluster times bytes per
 * sector fits in 32 bits (each is below 10000h); the multiplication by the
 * free clusters is capped instead of wrapping.
 * --------------------------------------------------------------------- */
int dos_disk_free(u8 drive, u32 *bytes)
{
    union REGS r;
    u32 csize;
    u32 nfree;

    *bytes = 0;
#ifndef COMPAT_WIN16
    /* Only a critical error of this call may be its error. */
    cf_crit_ext = 0xFFFFu;
    cf_crit_di = 0xFFFFu;
#endif
    regs_ax(&r, 0x3600);
    r.h.dl = drive;
    intdos(&r, &r);
    if ((u16)r.x.ax == 0xFFFFu)
        return crit_take(CF_EINVAL, 1);
    csize = (u32)(u16)r.x.ax * (u32)(u16)r.x.cx;
    nfree = (u32)(u16)r.x.bx;
    if (nfree != 0 && csize > 0xFFFFFFFFUL / nfree)
        *bytes = 0xFFFFFFFFUL;
    else
        *bytes = csize * nfree;
    return 0;
}

/* ---------------------------------------------------------------------
 * Raw or cooked mode of a character device.
 *
 * IOCTL (DOS 2.0): function 4400h returns the device information word
 * of handle BX in DX, bit 7 set for a device (clear for a file), bit 5
 * set for raw mode; function 4401h sets it from DX, whose high byte must
 * be 0.
 * --------------------------------------------------------------------- */
#define CF_DEV_ISDEV    0x80u   /* information bit 7: a device          */
#define CF_DEV_RAW      0x20u   /* information bit 5: raw mode          */

int dos_dev_raw(int handle, int raw, int *was)
{
    union REGS r;
    u16 info;
    int rc;

    *was = 0;
    regs_ax(&r, 0x4400);
    r.x.bx = (unsigned)handle;
    rc = int21(&r);
    if (rc != 0)
        return rc;
    info = (u16)r.x.dx;
    if ((info & CF_DEV_ISDEV) == 0)
        return CF_EINVAL;
    *was = (info & CF_DEV_RAW) != 0 ? 1 : 0;
    info &= 0x00FFu;
    if (raw)
        info |= CF_DEV_RAW;
    else
        info &= (u16)~CF_DEV_RAW;
    regs_ax(&r, 0x4401);
    r.x.bx = (unsigned)handle;
    r.x.dx = info;
    return int21(&r);
}

#ifdef COMPAT_WC6
/* ---------------------------------------------------------------------
 * Near heap.
 *
 * Watcom C 6.5's malloc grows the near heap by _amblksiz bytes, or to
 * the top of DGROUP when that is nearer, and that last step is faulty
 * when it begins 6 bytes below the top: it makes a free block of 4
 * bytes and writes the heap's end marker over the block's link to the
 * next one; the same malloc then follows that link and never returns.
 * The library's steps of 8 KB end there from one break in 4096, and
 * the break a program starts with comes from its static data, its
 * arguments and the environment.  So dos_heap_top asks for DOS_NOHEAP
 * bytes, which no heap in DGROUP can give: malloc refuses, having
 * grown the heap to the top in one step from the break the program
 * started with.  When DOS has not the memory for all of DGROUP that
 * step fails, and later steps end below the faulty break: DOS gives
 * memory in paragraphs, so every break above FFF0h needs the memory
 * the top needs, which DOS has refused.
 * --------------------------------------------------------------------- */
#define DOS_NOHEAP      0xFFF0u

void dos_heap_top(void)
{
    malloc(DOS_NOHEAP);
}
#endif

/* ---------------------------------------------------------------------
 * Stack check.
 *
 * The stack and the data share DGROUP (SS equals DS in the small and
 * medium models), so the stack is addressed with near pointers.  The
 * Watcom start-up code keeps the stack's lowest address in _STACKLOW.
 * Turbo C's start-up code lays out DGROUP as the static data (which ends
 * where __heaptop points, where the near heap starts), the _heaplen bytes
 * reserved for the near heap and the _stklen bytes of the stack, so the
 * stack's lowest address is __heaptop + _heaplen; with _heaplen 0 (a
 * program that does not link CMEM.C) no bottom is known, and the near
 * heap's present top, __brklvl, stands in.  Turbo C's allocator does not
 * stop at the reservation, only near the stack pointer, so a near heap
 * that outgrows it takes the bottom of the stack: cstk_unused then reports
 * 0, which the stack checks of the tests refuse.  The fill stops CSTK_GAP
 * bytes below a local of cstk_fill, which keeps clear of its own frame.
 * --------------------------------------------------------------------- */
#define CSTK_GAP        64      /* bytes left untouched below the frame */

#ifdef COMPAT_TC
extern unsigned __brklvl;
extern unsigned __heaptop;
extern unsigned _heaplen;
#define CSTK_LOW()      (_heaplen != 0 ? __heaptop + _heaplen : __brklvl)
#else
extern unsigned _STACKLOW;
#define CSTK_LOW()      (_STACKLOW)
#endif

static u16 cstk_low;            /* bottom of the filled area, 0 = none  */
static u16 cstk_top;            /* first byte above it                  */

void cstk_fill(void)
{
    volatile u8 *p;
    u8 here;
    u16 top;

    top = (u16)((unsigned)&here - CSTK_GAP);
    cstk_low = (u16)CSTK_LOW();
#ifdef COMPAT_TC
    if ((u16)__brklvl > cstk_low)
        cstk_low = (u16)__brklvl;
#endif
    cstk_top = top;
    if (top <= cstk_low) {
        cstk_low = 0;
        return;
    }
    for (p = (volatile u8 *)cstk_low; p < (volatile u8 *)top; p++)
        *p = CSTK_PAT;
}

/* 0 when the near heap now reaches above the stack's bottom (Turbo C);
   CSTK_UNKNOWN when nothing was filled. */
u16 cstk_unused(void)
{
    volatile u8 *p;
    u16 n;

    if (cstk_low == 0)
        return CSTK_UNKNOWN;
#ifdef COMPAT_TC
    if ((u16)__brklvl > (u16)CSTK_LOW())
        return 0;
#endif
    n = 0;
    for (p = (volatile u8 *)cstk_low; p < (volatile u8 *)cstk_top &&
         *p == CSTK_PAT; p++)
        n++;
    return n;
}

#endif

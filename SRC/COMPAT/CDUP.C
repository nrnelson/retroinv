/*
 * CDUP.C - handle duplication (dos_dup, dos_dup2, COMPAT.H) for the
 * tests that point a standard handle at a file; no program links it.
 * DOS functions 45h and 46h (DOS 2.0), whose error code in AX is exact
 * (CF_EBADF or CF_EMFILE); the host build uses dup and dup2.
 */
#ifdef HOST_LINUX
#define _POSIX_C_SOURCE 200112L
#endif

#define COMPAT_DUP
#include "COMPAT.H"

#ifdef COMPAT_HOST
#include <errno.h>
#include <unistd.h>

/* The CF_E code of a failed dup or dup2, as CDOS.C translates errno. */
static int dup_err(void)
{
    if (errno == EBADF)
        return CF_EBADF;
    if (errno == EMFILE || errno == ENFILE)
        return CF_EMFILE;
    return CF_EACCES;
}

int dos_dup(int h, int *nh)
{
    *nh = dup(h);
    return *nh < 0 ? dup_err() : 0;
}

int dos_dup2(int h, int to)
{
    return dup2(h, to) < 0 ? dup_err() : 0;
}

#else
#include <dos.h>

/* Issue function ax with BX = h and CX = to: 0 with AX in *nh, or the
   error code in AX with *nh -1. */
static int dup_call(u16 ax, int h, int to, int *nh)
{
    union REGS r;

    r.x.ax = ax;
    r.x.bx = (unsigned)h;
    r.x.cx = (unsigned)to;
    intdos(&r, &r);
    *nh = r.x.cflag ? -1 : (int)r.x.ax;
    return r.x.cflag ? (int)r.x.ax : 0;
}

int dos_dup(int h, int *nh)
{
    return dup_call(0x4500, h, 0, nh);
}

int dos_dup2(int h, int to)
{
    int nh;

    return dup_call(0x4600, h, to, &nh);
}
#endif

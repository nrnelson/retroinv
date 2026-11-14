/*
 * CPRN.C - printer output (COMPAT.H): the BIOS parallel ports, the DOS
 * device PRN and a capture file.
 *
 * One job is open at a time; its state is a few static bytes.  A BIOS
 * port (PRN_LPT1..PRN_LPT3) is driven through INT 17h: function 02h
 * reads the status, function 00h sends one byte and returns the status
 * after it.  The status byte in AH has the bits
 *   01h  time-out: the byte was not taken within the BIOS's wait
 *   08h  I/O error
 *   10h  selected (on line)
 *   20h  out of paper
 *   40h  acknowledge
 *   80h  not busy
 * A port exists when the BIOS data area holds a non-zero I/O address
 * for it (the word at 0040:0008 + 2 x port) and its status is not FFh
 * (an old BIOS may leave garbage in the word of a port it has not got,
 * and an unconnected port reads back all ones).  A printer is ready
 * when it is selected and reports no error, no paper-out and no
 * time-out; a byte counts as taken when function 00h returns none of
 * time-out, I/O error and paper-out.  A status read that shows only
 * the time-out bit (a printer still busy) gives PRN_ETIMEOUT, which the
 * caller may retry or cancel like a byte not taken.
 *
 * PRN_DOS writes to DOS handle 4, the standard printer, switched to raw
 * mode for the job so that DOS passes every byte (1Ah included) to the
 * device driver, and back to its earlier mode at the end.  DOS reports
 * a failed device write after its critical-error handler (the program's
 * own, from dos_crit_install, answers Fail) and does not tell how many
 * bytes of it went out, so a failed write counts as none taken and a
 * retry sends it whole.  Its status is not asked for (that needs an
 * IOCTL beyond the two raw-mode calls), so prn_status reports PRN_OK.
 *
 * A capture file takes every job instead of its port: each job opens
 * it and appends, PP_PIECE bytes per DOS write, so that a write that
 * fails leaves the pieces before it taken, as a printer leaves the
 * bytes it took; the failed piece is cut back off the file (as far as
 * the disk allows), so a retry adds exactly the bytes not yet taken.
 * The capture path is copied, and one that DOS opens as a device (PRN,
 * LPT1, NUL: IOCTL 4400h, through dos_dev_raw, reports the device bit on
 * the new handle) is refused, since the job would go to that device and
 * the capture file's appending would fail on it.  Without a capture file
 * the host build has no printer: every port is PRN_ENODEV there.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include <stddef.h>
#include <string.h>

#ifndef COMPAT_HOST
#include <dos.h>
#endif
#ifdef COMPAT_OW
#include <i86.h>
#endif

#define PP_STDPRN       4       /* the DOS handle of PRN                */
#define PP_PIECE        16      /* bytes per capture file write         */

/* INT 17h status bits. */
#define PS_TIMEOUT      0x01
#define PS_IOERR        0x08
#define PS_SELECT       0x10
#define PS_PAPER        0x20

#define PJ_NONE         0       /* no job open                          */
#define PJ_BIOS         1       /* a BIOS port                          */
#define PJ_DOS          2       /* DOS handle PP_STDPRN                 */
#define PJ_FILE         3       /* the capture file                     */

static char prn_cap[DOS_PATHMAX];       /* capture path, "" for none    */
static u32 prn_cpos;            /* bytes in the capture file            */
static int prn_cfh;             /* its handle while a job is open       */
static int prn_was;             /* PRN's mode before the job (1 raw)    */
static u8 prn_job;              /* PJ_ kind of the open job             */
static u8 prn_port;             /* its PRN_ port                        */

#ifndef COMPAT_HOST

/* The PRN_ code of an INT 17h status byte read before sending. */
static int bios_code(u8 st)
{
    if (st == 0xFFu)
        return PRN_ENODEV;
    if (st & PS_PAPER)
        return PRN_EPAPER;
    if ((st & PS_IOERR) != 0 || (st & PS_SELECT) == 0)
        return PRN_EOFFLINE;
    if (st & PS_TIMEOUT)
        return PRN_ETIMEOUT;
    return PRN_OK;
}

/* INT 17h function fn (00h with byte b, or 02h) on BIOS port p; the
   status byte. */
static u8 bios_call(u8 fn, u8 p, u8 b)
{
    union REGS r;

    r.h.ah = fn;
    r.h.al = b;
    r.x.dx = p;
    int86(0x17, &r, &r);
    return r.h.ah;
}

/* 1 when the BIOS data area names an I/O address for port p. */
static int bios_has(u8 p)
{
    FARP(volatile u16) w;

    w = CFP(volatile u16, BDA_SEG, 0x0008);
    return w[p] != 0;
}

/* The PRN_ code of BIOS port p's status: PRN_ENODEV when the BIOS data
   area names no port p. */
static int bios_status(u8 p)
{
    if (!bios_has(p))
        return PRN_ENODEV;
    return bios_code(bios_call(0x02, p, 0));
}

/* Send bytes until one is not taken. */
static int bios_write(u8 p, const u8 *buf, u16 n, u16 *done)
{
    u8 st;

    while (*done < n) {
        st = bios_call(0x00, p, buf[*done]);
        if ((st & (PS_TIMEOUT | PS_IOERR | PS_PAPER)) != 0) {
            if (st & PS_PAPER)
                return PRN_EPAPER;
            if (st & PS_IOERR)
                return PRN_EOFFLINE;
            return PRN_ETIMEOUT;
        }
        (*done)++;
    }
    return PRN_OK;
}

#endif

/* Open the capture file at its end for a job, prn_cfh its handle and
   prn_cpos its size: PRN_OK, or PRN_EIO with nothing open. */
static int open_capture(void)
{
    int rc;

    rc = cf_open(prn_cap, CF_RDWR, &prn_cfh);
    if (rc != 0)
        return PRN_EIO;
    if (cf_size(prn_cfh, &prn_cpos) != 0 || cf_seek(prn_cfh, prn_cpos) != 0) {
        cf_close(prn_cfh);
        return PRN_EIO;
    }
    return PRN_OK;
}

int prn_open(u8 port)
{
    int rc;

    prn_close();
    if (port > PRN_DOS)
        return PRN_ENODEV;
    if (prn_cap[0] != '\0') {
        rc = open_capture();
        if (rc == PRN_OK) {
            prn_job = PJ_FILE;
            prn_port = port;
        }
        return rc;
    }
#ifdef COMPAT_HOST
    return PRN_ENODEV;
#else
    if (port == PRN_DOS) {
        if (dos_dev_raw(PP_STDPRN, 1, &prn_was) != 0)
            return PRN_ENODEV;
        prn_job = PJ_DOS;
        prn_port = port;
        return PRN_OK;
    }
    rc = bios_status(port);
    if (rc == PRN_OK) {
        prn_job = PJ_BIOS;
        prn_port = port;
    }
    return rc;
#endif
}

int prn_status(void)
{
    switch (prn_job) {
    case PJ_NONE:
        return PRN_ENODEV;
#ifndef COMPAT_HOST
    case PJ_BIOS:
        return bios_status(prn_port);
#endif
    default:
        return PRN_OK;
    }
}

int prn_write(const u8 *buf, u16 n, u16 *done)
{
    u16 k;

    *done = 0;
    if (n == 0)
        return prn_job == PJ_NONE ? PRN_ENODEV : PRN_OK;
    switch (prn_job) {
    case PJ_FILE:
        while (*done < n) {
            k = (u16)(n - *done < PP_PIECE ? n - *done : PP_PIECE);
            if (cf_write(prn_cfh, buf + *done, k) != 0) {
                /* Whatever part of the piece reached the file is cut
                   off again. */
                if (cf_truncate(prn_cfh, prn_cpos) != 0)
                    cf_seek(prn_cfh, prn_cpos);
                return PRN_EIO;
            }
            prn_cpos += k;
            *done = (u16)(*done + k);
        }
        return PRN_OK;
#ifndef COMPAT_HOST
    case PJ_DOS:
        if (cf_write(PP_STDPRN, buf, n) != 0)
            return PRN_EIO;
        *done = n;
        return PRN_OK;
    case PJ_BIOS:
        return bios_write(prn_port, buf, n, done);
#endif
    default:
        return PRN_ENODEV;
    }
}

void prn_close(void)
{
    int was;

    switch (prn_job) {
    case PJ_FILE:
        cf_close(prn_cfh);
        break;
    case PJ_DOS:
        dos_dev_raw(PP_STDPRN, prn_was, &was);
        break;
    default:
        break;
    }
    prn_job = PJ_NONE;
}

int prn_capture(const char *path)
{
    u16 n;
    int h;
    int rc;
#ifndef COMPAT_HOST
    int was;
#endif

    prn_cap[0] = '\0';
    if (path == NULL)
        return 0;
    for (n = 0; path[n] != '\0'; n++)
        if (n == DOS_PATHMAX - 1)
            return CF_EINVAL;
    rc = cf_create(path, &h);
    if (rc != 0)
        return rc;
#ifndef COMPAT_HOST
    /* Only a file makes dos_dev_raw refuse with CF_EINVAL; the mode it
       sets on a device goes with the handle. */
    if (dos_dev_raw(h, 0, &was) != CF_EINVAL) {
        cf_close(h);
        return CF_EINVAL;
    }
#endif
    rc = cf_close(h);
    if (rc == 0)
        memcpy(prn_cap, path, (size_t)(n + 1));
    return rc;
}

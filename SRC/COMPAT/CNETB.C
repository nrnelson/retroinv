/*
 * CNETB.C - NetBIOS through INT 5Ch.
 *
 * A request is a 64-byte Network Control Block (NCB) passed to INT 5Ch in
 * ES:BX.  The NCB is kept as a byte array with the offsets below instead
 * of a struct, so its layout never depends on compiler padding.  Far
 * pointers inside it are stored offset first, then segment.
 *
 * Every command goes through ncb_int(), which issues INT 5Ch for one NCB.
 * The nb_ functions other than nb_listen_wait use the "wait" form of a
 * command (high bit clear) on the static NCB nb_ncb, so INT 5Ch returns
 * once the command is complete.  The other commands use the "no-wait"
 * form without a post routine: while the provider owns the NCB its
 * completion byte reads NB_RC_PENDING, and the provider stores the final
 * code there, possibly from an interrupt, so the byte is always read
 * afresh through a volatile far pointer.  That byte is the only record
 * that a command is in progress: an NCB never posted is zero, and a post
 * the provider refuses leaves the NCB not pending (an NCB of an nba set
 * exactly as it was before).
 *
 * nb_listen_wait issues a no-wait LISTEN on its own NCB nb_lncb and polls
 * it against a deadline in timer ticks (tmr_ticks); on expiry it cancels
 * the LISTEN through nb_ncb.  An nba set keeps its NCBs in one far block
 * that neither moves nor is freed while the provider owns one of them:
 * nba_close keeps such a set on the list nba_kept until nba_stuck finds
 * every NCB of it complete.
 *
 * Under Windows ncb_int calls KERNEL's NetBIOSCall, and every NCB the
 * provider owns lies in a fixed block of nba_alloc, as the buffers do:
 * a set's NCBs, and nb_twin, the copy of nb_ncb in which twin_run runs
 * each wait-form command as its no-wait form, polled with dos_idle, so
 * that the other programs run meanwhile.  The near NCB of nb_listen_wait
 * and the near buffers of nb_send and nb_recv have no place there, so
 * these are left out.  Polling needs no post routine: in standard and
 * 386 enhanced mode of Windows 3.0 alike, the completion byte of a
 * no-wait NCB that Windows maps shows the end of its command.
 */
#define COMPAT_INTERNAL
#define COMPAT_NETBIOS
#include "COMPAT.H"
#include <stdlib.h>
#include <string.h>
#ifndef COMPAT_HOST
#include <dos.h>
#endif
#ifdef COMPAT_WIN16
#include "CWIN.H"
#endif

/* NCB field offsets and size. */
#define NCB_COMMAND     0       /* command code                         */
#define NCB_RETCODE     1       /* return code                          */
#define NCB_LSN         2       /* local session number                 */
#define NCB_NUM         3       /* name number                          */
#define NCB_BUFFER      4       /* far pointer to the data buffer       */
#define NCB_LENGTH      8       /* buffer length, little-endian 16 bits */
#define NCB_CALLNAME    10      /* remote name, NB_NAMELEN bytes        */
#define NCB_NAME        26      /* local name, NB_NAMELEN bytes         */
#define NCB_RTO         42      /* receive timeout, 500 ms units        */
#define NCB_STO         43      /* send timeout, 500 ms units           */
#define NCB_POST        44      /* far pointer to the post routine      */
#define NCB_LANA        48      /* adapter number                       */
#define NCB_CPLT        49      /* completion code for no-wait commands */
#define NCB_RESERVE     50      /* reserved, 14 bytes                   */
#define NCB_SIZE        64

/* Command codes, wait form. */
#define NC_CALL         0x10
#define NC_LISTEN       0x11
#define NC_HANGUP       0x12
#define NC_SEND         0x14
#define NC_RECEIVE      0x15
#define NC_ADDNAME      0x30
#define NC_DELNAME      0x31
#define NC_CANCEL       0x35    /* buffer field: the NCB to cancel      */
#define NC_INVALID      0x7F    /* never a valid command; used to probe */

#define NC_NOWAIT       0x80    /* ORed in: the no-wait form            */
#define NC_CANCEL_LATE  0x24    /* CANCEL found the command completing  */

/* Ticks in at least s seconds: TMR_10SEC / 10 a second, rounded down,
   plus one, as the count may move on just after the start of a wait is
   read.  A macro, so that a constant s gives a constant. */
#define NC_TICKS(s)     ((u32)(s) * TMR_10SEC / 10 + 1)

/* Grace period for a cancelled no-wait command to report completion. */
#define NC_GRACE        NC_TICKS(5)

/* The reserved tail completes the NCB, and the NCBs of a set fit in one
   far block (the build fails otherwise). */
extern char cnetb_ncb_size_check[1];
extern char cnetb_ncb_size_check[(NCB_RESERVE + 14 == NCB_SIZE &&
                                  NBA_MAX * NCB_SIZE <= CMEM_MAXBLK) ?
                                 1 : 2];

/* NCB for the wait-form commands; only one is in progress at a time. */
static u8 nb_ncb[NCB_SIZE];

#ifdef COMPAT_WIN16
/* The provider's copy of nb_ncb, a block of nba_alloc that nb_present
   allocates once, for the run. */
static cmem_blk *nb_twin;
#else
/* NCB for the no-wait LISTEN of nb_listen_wait. */
static u8 nb_lncb[NCB_SIZE];
#endif

/* Set once nb_present() has found a provider. */
static int nb_found;

#ifndef COMPAT_WIN16
/* Return code of the last CANCEL issued by nb_listen_wait. */
static u8 nb_cancel_rc = NB_OK;
#endif

struct nba {
    cmem_blk *ncbs;             /* n NCBs, handle h at h * NCB_SIZE     */
    u16 n;                      /* NCBs in the set, 1..NBA_MAX          */
    nba *next;                  /* next set on nba_kept                 */
};

/* Sets closed while the provider still owned one of their NCBs. */
static nba *nba_kept;

#ifndef COMPAT_WIN16
u8 nb_last_cancel_rc(void)
{
    return nb_cancel_rc;
}
#endif

/* Clear an NCB and set its command code. */
static void ncb_init(u8 *ncb, u8 cmd)
{
    memset(ncb, 0, NCB_SIZE);
    ncb[NCB_COMMAND] = cmd;
}

/* Store name into a 16-byte NetBIOS name field: up to NB_NAMELEN - 1
   characters, space padded, last byte 0x00.  The wildcard "*" is stored
   as '*' followed by spaces. */
static void ncb_name(u8 *field, const char *name)
{
    int i;

    for (i = 0; i < NB_NAMELEN - 1 && name[i] != '\0'; i++)
        field[i] = (u8)name[i];
    for (; i < NB_NAMELEN; i++)
        field[i] = (u8)' ';
    if (!(name[0] == '*' && name[1] == '\0'))
        field[NB_NAMELEN - 1] = 0x00;
}

/* Fill the name fields and timeouts of a CALL or LISTEN NCB. */
static void ncb_session(u8 *ncb, u8 cmd, const char *local,
                        const char *remote, u8 rto, u8 sto)
{
    ncb_init(ncb, cmd);
    ncb_name(ncb + NCB_NAME, local);
    ncb_name(ncb + NCB_CALLNAME, remote);
    ncb[NCB_RTO] = rto;
    ncb[NCB_STO] = sto;
}

#ifdef COMPAT_HOST

/* The host has no NetBIOS; every command reports that. */
static u8 ncb_int(FARP(u8) ncb)
{
    ncb[NCB_RETCODE] = NB_RC_NOT_PRESENT;
    return NB_RC_NOT_PRESENT;
}

/* No buffer pointer is needed on the host. */
static void ncb_buffer(u8 *ncb, FARP(const u8) buf)
{
    (void)ncb;
    (void)buf;
}

int nb_present(void)
{
    nb_found = 0;
    return 0;
}

#else
#ifdef COMPAT_WIN16

/* KERNEL's NetBIOSCall, which a Windows program calls in place of INT
   5Ch, with the same registers; and KERNEL's constant selector __0000H,
   whose base is linear address 0, so that it reads the real-mode
   interrupt vectors in every mode. */
u8 NetBIOSCall(FARP(u8) ncb);
#pragma aux NetBIOSCall "NETBIOSCALL" __far \
    parm [es bx] value [al] modify [ax bx cx dx si di es];
extern char __0000H;
#pragma aux __0000H "__0000H";

static u8 ncb_int(FARP(u8) ncb)
{
    return NetBIOSCall(ncb);
}

#else

/* Issue INT 5Ch with ES:BX = ncb.  Returns AL: the final return code of
   a wait command, or the immediate return code of a no-wait command. */
static u8 ncb_int(FARP(u8) ncb)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    s.es = FP_SEG(ncb);
    r.x.bx = FP_OFF(ncb);
    int86x(0x5C, &r, &r, &s);
    return (u8)r.h.al;
}

#endif

/* Store the far address buf into the NCB buffer field. */
static void ncb_buffer(u8 *ncb, FARP(const u8) buf)
{
    put_le16(ncb + NCB_BUFFER, (u16)FP_OFF(buf));
    put_le16(ncb + NCB_BUFFER + 2, (u16)FP_SEG(buf));
}

#ifndef COMPAT_WIN16
/* A provider is assumed present when the INT 5Ch vector is not null,
   does not point at a bare IRET (0xCF), and the invalid command 7Fh
   comes back with return code 03h "invalid command". */
int nb_present(void)
{
    FARP(u16) vec;
    u16 off;
    u16 seg;

    nb_found = 0;
    vec = CFP(u16, 0, 0x5C * 4);
    off = vec[0];
    seg = vec[1];
    if (off == 0 && seg == 0)
        return 0;
    if (*CFP(u8, seg, off) == 0xCF)
        return 0;

    ncb_init(nb_ncb, NC_INVALID);
    ncb_int((FARP(u8))nb_ncb);
    if (nb_ncb[NCB_RETCODE] != NB_RC_ILLCMD)
        return 0;
    nb_found = 1;
    return 1;
}

#endif
#endif

/* The completion byte of the NCB at ncb, read afresh. */
static u8 ncb_cplt(FARP(u8) ncb)
{
    return *(FARP(volatile u8))(ncb + NCB_CPLT);
}

/* Poll the completion byte of the NCB at ncb until it is no longer
   pending or limit ticks (0 = no limit) have passed, idling meanwhile so
   that resident programs and the redirector run.  1 if completed. */
static int cplt_wait(FARP(u8) ncb, u32 limit)
{
    u32 start;

    start = tmr_ticks();
    while (ncb_cplt(ncb) == NB_RC_PENDING) {
        if (limit != 0 && tmr_since(start) >= limit)
            return 0;
        dos_idle();
    }
    return 1;
}

#ifdef COMPAT_WIN16
/* Issue the command built in nb_ncb through its copy nb_twin, which the
   provider reaches while the program yields, and wait for it: in its
   no-wait form, polled with dos_idle, as a wait-form command would
   stop every program, but for CANCEL and the invalid command of
   nb_present, which have no no-wait form.  nb_ncb then holds what the
   provider left, its return code the command's: the completion byte
   once it is set, else the code of a post the provider refused.
   Standard mode's DOSX completes a no-wait DELETE NAME inside the call
   and returns a stray AL (2Ah), so a completion byte already set
   outweighs AL. */
static void twin_run(void)
{
    FARP(u8) ncb;
    u8 rc;
    int nowait;

    ncb = cmem_ptr(nb_twin);
    nowait = nb_ncb[NCB_COMMAND] != NC_CANCEL &&
             nb_ncb[NCB_COMMAND] != NC_INVALID;
    if (nowait) {
        nb_ncb[NCB_COMMAND] |= NC_NOWAIT;
        nb_ncb[NCB_RETCODE] = NB_RC_PENDING;
        nb_ncb[NCB_CPLT] = NB_RC_PENDING;
    }
    cmem_put(nb_twin, 0, nb_ncb, NCB_SIZE);
    rc = ncb_int(ncb);
    if (nowait && (rc == NB_OK || rc == NB_RC_PENDING))
        cplt_wait(ncb, 0);
    cmem_get(nb_twin, 0, nb_ncb, NCB_SIZE);
    if (nowait)
        nb_ncb[NCB_RETCODE] = nb_ncb[NCB_CPLT] != NB_RC_PENDING ?
                              nb_ncb[NCB_CPLT] : rc;
}

/* A provider is assumed present when the real-mode INT 5Ch vector is
   not null and the invalid command 7Fh comes back with return code 03h
   "invalid command"; a vector at a bare IRET leaves the code 0. */
int nb_present(void)
{
    FARP(u16) vec;

    nb_found = 0;
    vec = (FARP(u16))MAKELP((UINT)&__0000H, 0x5C * 4);
    if (vec[0] == 0 && vec[1] == 0)
        return 0;
    if (nb_twin == NULL)
        nb_twin = nba_alloc(NCB_SIZE);
    if (nb_twin == NULL)
        return NB_PRES_NOMEM;
    ncb_init(nb_ncb, NC_INVALID);
    twin_run();
    if (nb_ncb[NCB_RETCODE] != NB_RC_ILLCMD)
        return 0;
    nb_found = 1;
    return 1;
}

#endif

/* Run a wait-form command in nb_ncb unless no provider has been found;
   returns the NCB's return code. */
static u8 ncb_run(void)
{
    if (!nb_found)
        return NB_RC_NOT_PRESENT;
#ifdef COMPAT_WIN16
    twin_run();
#else
    ncb_int((FARP(u8))nb_ncb);
#endif
    return nb_ncb[NCB_RETCODE];
}

/* Cancel the no-wait command in the NCB at ncb; returns the CANCEL's
   return code. */
static u8 ncb_cancel(FARP(u8) ncb)
{
    ncb_init(nb_ncb, NC_CANCEL);
    ncb_buffer(nb_ncb, ncb);
    return ncb_run();
}

#ifndef COMPAT_WIN16
u8 nb_listen_wait(const char *local, const char *remote, u8 *lsn,
                  u8 rto, u8 sto, u16 seconds)
{
    FARP(u8) ncb;
    u8 rc;
    u32 limit;

    *lsn = 0;
    if (!nb_found)
        return NB_RC_NOT_PRESENT;
    ncb = (FARP(u8))nb_lncb;
    if (ncb_cplt(ncb) == NB_RC_PENDING)
        return NB_RC_PENDING;

    ncb_session(nb_lncb, (u8)(NC_LISTEN | NC_NOWAIT),
                local, remote, rto, sto);
    nb_lncb[NCB_RETCODE] = NB_RC_PENDING;
    nb_lncb[NCB_CPLT] = NB_RC_PENDING;
    rc = ncb_int(ncb);
    if (rc != NB_OK && rc != NB_RC_PENDING) {
        nb_lncb[NCB_CPLT] = rc;
        return rc;
    }

    limit = seconds == 0 ? 0 : NC_TICKS(seconds);
    if (!cplt_wait(ncb, limit)) {
        nb_cancel_rc = ncb_cancel(ncb);
        /* Unless the provider accepted the cancel, it still owns the
           LISTEN, which may complete at any later time. */
        if (nb_cancel_rc != NB_OK && nb_cancel_rc != NC_CANCEL_LATE)
            return NB_RC_PENDING;
        if (!cplt_wait(ncb, NC_GRACE))
            return NB_RC_PENDING;
        if (ncb_cplt(ncb) == NB_RC_CANCELLED)
            return NB_RC_TIMEOUT;
    }
    /* NB_OK if a call arrived, possibly while cancelling. */
    rc = ncb_cplt(ncb);
    if (rc == NB_OK)
        *lsn = nb_lncb[NCB_LSN];
    return rc;
}
#endif

u8 nb_add_name(const char *name, u8 *num)
{
    u8 rc;

    ncb_init(nb_ncb, NC_ADDNAME);
    ncb_name(nb_ncb + NCB_NAME, name);
    rc = ncb_run();
    *num = nb_ncb[NCB_NUM];
    return rc;
}

u8 nb_delete_name(const char *name)
{
    ncb_init(nb_ncb, NC_DELNAME);
    ncb_name(nb_ncb + NCB_NAME, name);
    return ncb_run();
}

/* Shared body of the wait-form CALL and LISTEN. */
static u8 wait_session(u8 cmd, const char *local, const char *remote,
                       u8 *lsn, u8 rto, u8 sto)
{
    u8 rc;

    ncb_session(nb_ncb, cmd, local, remote, rto, sto);
    rc = ncb_run();
    *lsn = rc == NB_OK ? nb_ncb[NCB_LSN] : (u8)0;
    return rc;
}

u8 nb_call(const char *local, const char *remote, u8 *lsn, u8 rto, u8 sto)
{
    return wait_session(NC_CALL, local, remote, lsn, rto, sto);
}

u8 nb_listen(const char *local, const char *remote, u8 *lsn, u8 rto, u8 sto)
{
    return wait_session(NC_LISTEN, local, remote, lsn, rto, sto);
}

#ifndef COMPAT_WIN16
u8 nb_send(u8 lsn, const void *buf, u16 len)
{
    ncb_init(nb_ncb, NC_SEND);
    nb_ncb[NCB_LSN] = lsn;
    ncb_buffer(nb_ncb, (FARP(const u8))buf);
    put_le16(nb_ncb + NCB_LENGTH, len);
    return ncb_run();
}

u8 nb_recv(u8 lsn, void *buf, u16 *len)
{
    u8 rc;

    ncb_init(nb_ncb, NC_RECEIVE);
    nb_ncb[NCB_LSN] = lsn;
    ncb_buffer(nb_ncb, (FARP(const u8))buf);
    put_le16(nb_ncb + NCB_LENGTH, *len);
    rc = ncb_run();
    if (rc == NB_OK || rc == NB_RC_INCOMPLETE)
        *len = get_le16(nb_ncb + NCB_LENGTH);
    else
        *len = 0;
    return rc;
}
#endif

u8 nb_hangup(u8 lsn)
{
    ncb_init(nb_ncb, NC_HANGUP);
    nb_ncb[NCB_LSN] = lsn;
    return ncb_run();
}

/* ---------------------------------------------------------------------
 * No-wait command sets.
 * --------------------------------------------------------------------- */

/* Far address of NCB h of s. */
static FARP(u8) set_ncb(nba *s, u16 h)
{
    return cmem_ptr(s->ncbs) + h * NCB_SIZE;
}

/* The number of NCBs of s the provider owns. */
static u16 set_busy(nba *s)
{
    u16 h;
    u16 n;

    n = 0;
    for (h = 0; h < s->n; h++)
        if (ncb_cplt(set_ncb(s, h)) == NB_RC_PENDING)
            n++;
    return n;
}

static void set_free(nba *s)
{
    cmem_free(s->ncbs);
    free(s);
}

#ifdef COMPAT_WIN16
cmem_blk *nba_alloc(u16 size)
{
    return cmem_alloc_dos(size);
}
#endif

int nba_open(nba **out, u16 n)
{
    nba *s;

    *out = NULL;
    if (n == 0 || n > NBA_MAX)
        return CF_EINVAL;
    s = (nba *)malloc(sizeof(nba));
    if (s == NULL)
        return CF_ENOMEM;
    s->ncbs = nba_alloc((u16)(n * NCB_SIZE));
    if (s->ncbs == NULL) {
        free(s);
        return CF_ENOMEM;
    }
    cmem_set(s->ncbs, 0, 0, (u16)(n * NCB_SIZE));
    s->n = n;
    *out = s;
    return 0;
}

/* Issue the command built in ncb, in its no-wait form, on NCB h of s.
   Returns NB_OK when the provider took it, else a final code with NCB h
   left as it was. */
static u8 set_post(nba *s, u16 h, u8 *ncb)
{
    u8 old[NCB_SIZE];
    u16 off;
    u8 rc;
#ifdef COMPAT_WIN16
    u8 c;
#endif

    if (h >= s->n || ncb_cplt(set_ncb(s, h)) == NB_RC_PENDING)
        return NB_RC_ILLCMD;
    if (!nb_found)
        return NB_RC_NOT_PRESENT;
    off = (u16)(h * NCB_SIZE);
    cmem_get(s->ncbs, off, old, NCB_SIZE);
    ncb[NCB_COMMAND] |= NC_NOWAIT;
    ncb[NCB_RETCODE] = NB_RC_PENDING;
    ncb[NCB_CPLT] = NB_RC_PENDING;
    cmem_put(s->ncbs, off, ncb, NCB_SIZE);
    rc = ncb_int(set_ncb(s, h));
    if (rc == NB_OK || rc == NB_RC_PENDING)
        return NB_OK;
#ifdef COMPAT_WIN16
    /* A completion byte set to another code than AL's is a command that
       Windows completed inside the call, AL being stray (twin_run); a
       refused post leaves the byte pending or sets it to AL's code. */
    c = ncb_cplt(set_ncb(s, h));
    if (c != NB_RC_PENDING && c != rc)
        return NB_OK;
#endif
    cmem_put(s->ncbs, off, old, NCB_SIZE);
    return rc;
}

/* Post a RECEIVE or SEND (cmd) of the len bytes at offset off of b on
   session lsn; a buffer that b does not hold is NB_RC_ILLCMD. */
static u8 set_xfer(nba *s, u16 h, u8 cmd, u8 lsn, cmem_blk *b, u16 off,
                   u16 len)
{
    u8 ncb[NCB_SIZE];

    if (len == 0 || len > NB_MSGMAX || (u32)off + len > cmem_size(b))
        return NB_RC_ILLCMD;
#ifdef COMPAT_WIN16
    if (cmem_ptr(b) == NULL)
        return NB_RC_ILLCMD;
#endif
    ncb_init(ncb, cmd);
    ncb[NCB_LSN] = lsn;
    ncb_buffer(ncb, cmem_ptr(b) + off);
    put_le16(ncb + NCB_LENGTH, len);
    return set_post(s, h, ncb);
}

u8 nba_listen(nba *s, u16 h, const char *local, const char *remote,
              u8 rto, u8 sto)
{
    u8 ncb[NCB_SIZE];

    ncb_session(ncb, NC_LISTEN, local, remote, rto, sto);
    return set_post(s, h, ncb);
}

u8 nba_call(nba *s, u16 h, const char *local, const char *remote,
            u8 rto, u8 sto)
{
    u8 ncb[NCB_SIZE];

    ncb_session(ncb, NC_CALL, local, remote, rto, sto);
    return set_post(s, h, ncb);
}

u8 nba_recv(nba *s, u16 h, u8 lsn, cmem_blk *b, u16 off, u16 len)
{
    return set_xfer(s, h, NC_RECEIVE, lsn, b, off, len);
}

u8 nba_send(nba *s, u16 h, u8 lsn, cmem_blk *b, u16 off, u16 len)
{
    return set_xfer(s, h, NC_SEND, lsn, b, off, len);
}

u8 nba_poll(nba *s, u16 h)
{
    if (h >= s->n)
        return NB_RC_ILLCMD;
    return ncb_cplt(set_ncb(s, h));
}

/* Copy NCB h of s into ncb and return its command code, without the
   no-wait bit, once the command has completed; 0 while it is in
   progress, for an NCB never posted, and for h out of range.  The
   completion byte is read before the copy, so that the copy never sees
   an NCB the provider is still writing. */
static u8 set_done(nba *s, u16 h, u8 *ncb)
{
    if (h >= s->n || ncb_cplt(set_ncb(s, h)) == NB_RC_PENDING)
        return 0;
    cmem_get(s->ncbs, (u16)(h * NCB_SIZE), ncb, NCB_SIZE);
    return (u8)(ncb[NCB_COMMAND] & ~NC_NOWAIT);
}

u8 nba_lsn(nba *s, u16 h)
{
    u8 ncb[NCB_SIZE];
    u8 cmd;

    cmd = set_done(s, h, ncb);
    if (cmd == NC_RECEIVE || cmd == NC_SEND ||
        ((cmd == NC_LISTEN || cmd == NC_CALL) &&
         ncb[NCB_CPLT] == NB_OK))
        return ncb[NCB_LSN];
    return 0;
}

u16 nba_len(nba *s, u16 h)
{
    u8 ncb[NCB_SIZE];
    u8 cmd;

    cmd = set_done(s, h, ncb);
    if ((cmd == NC_RECEIVE && (ncb[NCB_CPLT] == NB_OK ||
                               ncb[NCB_CPLT] == NB_RC_INCOMPLETE)) ||
        (cmd == NC_SEND && ncb[NCB_CPLT] == NB_OK))
        return get_le16(ncb + NCB_LENGTH);
    return 0;
}

void nba_callname(nba *s, u16 h, char *out)
{
    u8 ncb[NCB_SIZE];
    u8 c;
    int n;

    n = 0;
    if (set_done(s, h, ncb) == NC_LISTEN && ncb[NCB_CPLT] == NB_OK) {
        n = NB_NAMELEN - 1;
        while (n > 0 && ((c = ncb[NCB_CALLNAME + n - 1]) == ' ' || c == 0))
            n--;
        memcpy(out, ncb + NCB_CALLNAME, (size_t)n);
    }
    out[n] = '\0';
}

u8 nba_cancel(nba *s, u16 h)
{
    FARP(u8) ncb;

    if (h >= s->n)
        return NB_RC_ILLCMD;
    ncb = set_ncb(s, h);
    if (ncb_cplt(ncb) != NB_RC_PENDING)
        return NB_OK;
    ncb_cancel(ncb);
    return ncb_cplt(ncb) == NB_RC_PENDING ? NB_RC_PENDING : NB_OK;
}

u16 nba_close(nba *s)
{
    u16 h;
    u16 n;
    u32 start;

    if (s == NULL)
        return 0;
    for (h = 0; h < s->n; h++)
        nba_cancel(s, h);
    start = tmr_ticks();
    while ((n = set_busy(s)) != 0 && tmr_since(start) < NC_GRACE)
        dos_idle();
    if (n == 0) {
        set_free(s);
    } else {
        s->next = nba_kept;
        nba_kept = s;
    }
    return n;
}

u16 nba_stuck(void)
{
    nba **link;
    nba *s;
    u16 n;
    u16 total;

    total = 0;
    link = &nba_kept;
    while ((s = *link) != NULL) {
        n = set_busy(s);
        if (n == 0) {
            *link = s->next;
            set_free(s);
        } else {
            total += n;
            link = &s->next;
        }
    }
    return total;
}

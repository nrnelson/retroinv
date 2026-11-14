/*
 * MBXCLI.C - the mailbox client (MBXCLI.H).
 *
 * A session keeps the queue in one far block, written to the request
 * file with one call, and the last response file read whole in the
 * other, with the offset of the first byte not yet handed on.  From
 * there the bytes go into the reader of RESPRD.H, which frames the
 * responses; the end of the file is the end of its stream.
 */
#define COMPAT_FIND
#define COMPAT_DISK
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RESPRD.H"
#include "MBXCLI.H"

/* A path in the mailbox: the dir, RESPONSE\ (the longer directory) and
   an 8.3 name with its NUL. */
#define MC_PATH         (MBXCLI_MAXDIR + sizeof MBXCLI_RSPDIR + DOS_FINDNAME)

/* Response lines are cut where the reader cuts them.  The pair declares
   one array twice, the second time with size 2 when that fails: the
   declarations then disagree, which every compiler refuses. */
extern char mbxcli_resp_ok[1];
extern char mbxcli_resp_ok[(MBXCLI_MAXRESP == LINK_MAXRESP) ? 1 : 2];

struct mbxcli {
    cmem_blk *q;                /* the queue, MBXCLI_QMAX bytes         */
    cmem_blk *rsp;              /* the response file, MBXCLI_RSPMAX     */
    u16  qlen;                  /* bytes queued                         */
    u16  rlen;                  /* bytes of the response file           */
    u16  rpos;                  /* of them handed to rr                 */
    u8   due;                   /* 1 while a post waits for a response  */
    u8   zero;                  /* 1 while the next response file read is
                                   to be zeroed                         */
    u8   step;                  /* MBXCLI_S of the first failed call    */
    u8   err;                   /* its CF_E code, 0 while none failed   */
    char dir[MBXCLI_MAXDIR + 1];        /* with its separator           */
    char sta[MBXCLI_STALEN + 1];
    rr_buf rr;                  /* the response being read              */
};

/* Write into out (MC_PATH bytes) the path of m's station's file of
   extension ext in the mailbox directory sub. */
static void mc_path(const mbxcli *m, char *out, const char *sub,
                    const char *ext)
{
    size_t n;

    strcpy(out, m->dir);
    strcat(out, sub);
    n = strlen(out);
    out[n++] = DOS_DIRSEP;
    strcpy(out + n, m->sta);
    strcat(out, ".");
    strcat(out, ext);
}

int mbxcli_open(mbxcli **out, const char *dir, const char *station)
{
    mbxcli *m;
    size_t n;
    size_t i;
    int sep;
    int c;

    *out = NULL;
    n = strlen(dir);
    sep = n > 0 && dir[n - 1] != '\\' && dir[n - 1] != '/' &&
          dir[n - 1] != ':';
    if (n + (size_t)sep > MBXCLI_MAXDIR)
        return MBXCLI_EARG;
    for (i = 0; station[i] != '\0'; i++) {
        c = (unsigned char)station[i];
        if (i == MBXCLI_STALEN ||
            !(TXT_ISALPHA(c) || TXT_ISDIGIT(c) || c == '-' || c == '_'))
            return MBXCLI_EARG;
    }
    if (i == 0 || txt_dosdev(station, (u16)i))
        return MBXCLI_EARG;
    m = (mbxcli *)malloc(sizeof *m);
    if (m == NULL)
        return MBXCLI_ENOMEM;
    memset(m, 0, sizeof *m);
    m->q = cmem_alloc(MBXCLI_QMAX);
    m->rsp = cmem_alloc(MBXCLI_RSPMAX);
    if (m->q == NULL || m->rsp == NULL) {
        mbxcli_close(m);
        return MBXCLI_ENOMEM;
    }
    strcpy(m->dir, dir);
    if (sep)
        m->dir[n] = DOS_DIRSEP;
    for (i = 0; station[i] != '\0'; i++) {
        c = (unsigned char)station[i];
        m->sta[i] = (char)TXT_UPPER(c);
    }
    *out = m;
    return MBXCLI_OK;
}

int mbxcli_queue(mbxcli *m, const char *line, u16 len)
{
    if ((u32)m->qlen + len + 2 > MBXCLI_QMAX)
        return MBXCLI_EFULL;
    cmem_put(m->q, m->qlen, line, len);
    cmem_put(m->q, (u16)(m->qlen + len), "\r\n", 2);
    m->qlen = (u16)(m->qlen + len + 2);
    return MBXCLI_OK;
}

/* Note rc, the CF_E code of the call of step, as the failure of the
   attempt or look when it is its first; returns rc. */
static int note(mbxcli *m, int rc, int step)
{
    if (rc != 0 && m->err == 0) {
        m->step = (u8)step;
        m->err = (u8)rc;
    }
    return rc;
}

/* Remove the file at path: 0 once it is gone, else the CF_E code.  A
   removal that fails counts only while the file is there, as a
   redirector whose link broke repeats a removal it had done and
   reports it failed, with whatever code its new session gives. */
static int gone(const char *path)
{
    int rc;

    rc = cf_remove(path);
    return rc != 0 && cf_exists(path) != 0 ? rc : 0;
}

/* Overwrite the file at path, if it is there, with zeros, each step as
   far as it goes. */
static void blank(const char *path)
{
    u32 size;
    int h;

    if (cf_open(path, CF_RDWR, &h) == 0) {
        if (cf_size(h, &size) == 0)
            cf_zero(h, size);
        cf_close(h);
    }
}

/* Blank the file at path and remove it, as far as that goes. */
static void scrub(const char *path)
{
    blank(path);
    cf_remove(path);
}

/* Write the queue as REQUEST\<station>.TMP, commit it by closing it and
   rename it to .REQ: 0 or the first CF_E code, noted, the .TMP
   scrubbed then.  A .TMP left by a client of the station that stopped
   before its rename is scrubbed first: creating the file anew would
   free its clusters with the request, password and all, still in
   them.  A rename that fails while the .TMP is gone was done: the
   redirector sent it again after its link broke.  cf_exists also
   gives 0 when REQUEST\ itself is gone, made a moment before; the post
   then counts as done and its wait finds no response, a failure all
   the same.  *again is set to 0 when the rename fails and the .TMP
   cannot be looked for, the request then perhaps in REQUEST\ all the
   same, and is left as it is otherwise. */
static int put_request(mbxcli *m, const char *req, int *again)
{
    char tmp[MC_PATH];
    int h;
    int rc;
    int rc2;

    mc_path(m, tmp, MBXCLI_REQDIR, MBXCLI_TMPEXT);
    scrub(tmp);
    rc = note(m, cf_create(tmp, &h), MBXCLI_SCREATE);
    if (rc != 0)
        return rc;
    rc = note(m, cf_writeblk(h, m->q, 0, m->qlen), MBXCLI_SWRITE);
    rc2 = note(m, cf_close(h), MBXCLI_SCLOSE);
    if (rc == 0)
        rc = rc2;
    if (rc == 0) {
        rc = cf_rename(tmp, req);
        if (rc != 0) {
            rc2 = cf_exists(tmp);
            if (rc2 == 0)
                rc = 0;
            else if (rc2 < 0)
                *again = 0;
        }
        rc = note(m, rc, MBXCLI_SRENAME);
    }
    if (rc != 0)
        scrub(tmp);
    return rc;
}

/* Post the queue once, the record of the failed call cleared first:
   MBXCLI_OK, MBXCLI_EBUSY while REQUEST\<station>.REQ (path req) is
   there, or MBXCLI_EIO, *again then 1 when nothing of the request is
   in REQUEST\, so that it may be posted again.  A .RSP left from before
   (path rsp) is blanked before it is removed, as it may hold a ticket:
   one whose zeroing failed, or one that came after its client gave up
   the wait. */
static int post_once(mbxcli *m, const char *req, const char *rsp,
                     int *again)
{
    int rc;

    m->err = 0;
    *again = 1;
    rc = cf_exists(req);
    if (rc == 1)
        return MBXCLI_EBUSY;
    rc = note(m, -rc, MBXCLI_SFIND);
    if (rc == 0) {
        blank(rsp);
        rc = note(m, gone(rsp), MBXCLI_SCLEAR);
    }
    if (rc == 0)
        rc = put_request(m, req, again);
    return rc == 0 ? MBXCLI_OK : MBXCLI_EIO;
}

/* Look once for RESPONSE\<station>.RSP (path rsp), the record of the
   failed call cleared first; read it whole, close it and remove it:
   MBXCLI_OK, MBXCLI_EWAIT when it is not there, MBXCLI_EFULL (removed)
   or MBXCLI_EIO, the file then left for the next look.  A file to be
   zeroed (m->zero) is zeroed before it is closed, and once read it
   stands as read: its zeroing may have begun, so it could not be read
   again.  Its zeroing, close and removal then go as far as they go, and
   a file left is the next post's to blank and remove. */
static int fetch(mbxcli *m, const char *rsp)
{
    u32 size;
    u16 got;
    int h;
    int rc;
    int rc2;

    m->err = 0;
    rc = cf_open(rsp, (m->zero ? CF_RDWR : CF_READ) | CF_DENYW, &h);
    if (rc == CF_ENOENT)
        return MBXCLI_EWAIT;
    if (note(m, rc, MBXCLI_SOPEN) != 0)
        return MBXCLI_EIO;
    cmem_set(m->rsp, 0, 0, m->rlen);
    txt_wipe(&m->rr, (u16)sizeof m->rr);
    m->rlen = 0;
    m->rpos = 0;
    got = 0;
    rc = note(m, cf_size(h, &size), MBXCLI_SREAD);
    if (rc == 0 && size <= MBXCLI_RSPMAX)
        rc = note(m, cf_readblk(h, m->rsp, 0, (u16)size, &got),
                  MBXCLI_SREAD);
    if (rc == 0 && m->zero) {
        if (cf_seek(h, 0) == 0)
            cf_zero(h, size);
        cf_close(h);
        cf_remove(rsp);
    } else {
        rc2 = note(m, cf_close(h), MBXCLI_SRCLOSE);
        if (rc == 0)
            rc = rc2;
        if (rc == 0)
            rc = note(m, gone(rsp), MBXCLI_SREMOVE);
    }
    if (rc != 0) {
        cmem_set(m->rsp, 0, 0, got);
        return MBXCLI_EIO;
    }
    m->due = 0;
    m->zero = 0;
    if (size > MBXCLI_RSPMAX)
        return MBXCLI_EFULL;
    m->rlen = got;
    return MBXCLI_OK;
}

int mbxcli_post(mbxcli *m, u16 waitsecs)
{
    char req[MC_PATH];
    char rsp[MC_PATH];
    u16 i;
    int again;
    int rc;

    mc_path(m, rsp, MBXCLI_RSPDIR, MBXCLI_RSPEXT);
    if (m->qlen > 0) {
        if (m->due)
            return MBXCLI_EBUSY;
        mc_path(m, req, MBXCLI_REQDIR, MBXCLI_REQEXT);
        rc = post_once(m, req, rsp, &again);
        if (rc == MBXCLI_EIO && again) {
            tmr_delay(TMR_SEC);
            rc = post_once(m, req, rsp, &again);
        }
        if (rc != MBXCLI_OK)
            return rc;
        cmem_set(m->q, 0, 0, m->qlen);
        m->qlen = 0;
        m->due = 1;
    }
    for (i = 0; m->due; i++) {
        rc = fetch(m, rsp);
        if ((rc != MBXCLI_EWAIT && rc != MBXCLI_EIO) || i == waitsecs)
            return rc;
        tmr_delay(TMR_SEC);
    }
    return MBXCLI_OK;
}

/* rr_fill_fn: the next bytes of the response file, 0 at its end. */
static u16 fill(void *arg, char *dst, u16 room)
{
    mbxcli *m;
    u16 n;

    m = (mbxcli *)arg;
    n = (u16)(m->rlen - m->rpos);
    if (n > room)
        n = room;
    cmem_get(m->rsp, m->rpos, dst, n);
    m->rpos = (u16)(m->rpos + n);
    return n;
}

int mbxcli_response(mbxcli *m, mbxcli_line_fn fn, void *arg)
{
    int rc;

    if (m->rpos == m->rlen && m->rr.lpos == m->rr.llen)
        return MBXCLI_ENONE;
    rc = rr_response(&m->rr, fill, m, fn, arg);
    if (rc == RR_ELOST) {
        txt_wipe(&m->rr, (u16)sizeof m->rr);
        return MBXCLI_ECUT;
    }
    return rc == RR_EOUTPUT ? MBXCLI_EOUTPUT : MBXCLI_OK;
}

void mbxcli_zero_next(mbxcli *m)
{
    m->zero = 1;
}

void mbxcli_ioerr(mbxcli *m, int *step, int *err)
{
    *step = m->step;
    *err = m->err;
}

void mbxcli_close(mbxcli *m)
{
    if (m == NULL)
        return;
    if (m->q != NULL)
        cmem_set(m->q, 0, 0, MBXCLI_QMAX);
    if (m->rsp != NULL)
        cmem_set(m->rsp, 0, 0, MBXCLI_RSPMAX);
    cmem_free(m->q);
    cmem_free(m->rsp);
    txt_wipe(m, (u16)sizeof *m);
    free(m);
}

void mbxcli_blocks(mbxcli *m, cmem_blk **q, cmem_blk **rsp)
{
    *q = m->q;
    *rsp = m->rsp;
}

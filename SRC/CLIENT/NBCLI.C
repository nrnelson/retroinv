/*
 * NBCLI.C - a client's NetBIOS session to the server (NBCLI.H).
 *
 * A session owns two no-wait NCBs, NBC_HRX for its RECEIVE and NBC_HTX
 * for its SEND; its far block holds a received message at NBC_RX and a
 * message being sent at NBC_TX.
 *
 * A RECEIVE is kept posted whenever every byte at NBC_RX has been read:
 * from the open, and again as soon as a message has been read up.  The
 * server's provider may acknowledge a request in the frame that carries
 * the response, so the response arrives as the SEND completes; a
 * RECEIVE posted only then would be too late, and NetBEUI, finding
 * none, has the server send the response again.  A message that
 * arrives while bytes of the one before are still unread (a response
 * of several messages, or responses sent ahead) finds none either.
 *
 * Received bytes go from the far block into the reader of RESPRD.H, as
 * many as fit; what follows the end of a response stays in the reader
 * and the far block for the next one, so a request's response never
 * needs a RECEIVE of its own when the server sent it along with an
 * earlier one.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "RESPRD.H"
#include "NBCLI.H"

#define NBC_RX          0       /* received message, NB_MSGMAX bytes    */
#define NBC_TX          NB_MSGMAX  /* message being sent, NB_MSGMAX     */
#define NBC_BUFSZ       (2 * NB_MSGMAX)  /* far block of both           */
#define NBC_HRX         0       /* the RECEIVE's NCB                    */
#define NBC_HTX         1       /* the SEND's NCB                       */
#define NBC_NCBS        2

/* The machine name is used as the local name, in a buffer of
   NB_NAMELEN bytes. */
extern char nbc_machlen_ok[1];
extern char nbc_machlen_ok[(DOS_MACHLEN < NB_NAMELEN) ? 1 : 2];

/* The response results equal rr_response's. */
extern char nbc_codes_ok[1];
extern char nbc_codes_ok[(NBCLI_ELOST == RR_ELOST &&
                          NBCLI_EOUTPUT == RR_EOUTPUT) ? 1 : 2];

struct nbcli {
    nba  *set;                  /* NBC_HRX and NBC_HTX                  */
    cmem_blk *buf;              /* NBC_RX and NBC_TX                    */
    u16  rpos;                  /* next byte of the message at NBC_RX   */
    u16  rend;                  /* end of that message                  */
    u16  npend;                 /* requests whose response is unread    */
    u16  wait;                  /* seconds a RECEIVE of the response
                                   being read may take                  */
    u8   lsn;
    u8   added;                 /* name was added by nbcli_open         */
    u8   closed;                /* the session has ended                */
    u8   rxon;                  /* a RECEIVE posted and not yet taken   */
    char name[NB_NAMELEN];
    rr_buf rr;                  /* the response being read              */
};

/* 1 when rc, the final code of a command of the session, shows that
   the provider has removed the session: it reported the session ended,
   or refused a post for an LSN it no longer knows. */
static int gone(u8 rc)
{
    return rc == NB_RC_SESSCLOSED || rc == NB_RC_ABORTED ||
           rc == NB_RC_BADLSN;
}

/* Post the RECEIVE into NBC_RX unless one is posted or bytes there
   are unread: NB_OK then, else the post's code.  A post the provider
   refuses is made again before the next SEND, or when the next message
   is waited for. */
static u8 arm(nbcli *n)
{
    u8 rc;

    rc = NB_OK;
    if (!n->rxon && n->rpos == n->rend) {
        rc = nba_recv(n->set, NBC_HRX, n->lsn, n->buf, NBC_RX, NB_MSGMAX);
        n->rxon = (u8)(rc == NB_OK);
    }
    return rc;
}

/* End the session after a command gave rc (NB_OK for none, at the
   close): hang up unless rc, or the final code of a RECEIVE still
   posted, says that the provider has removed the session (the cancel
   of a SEND makes it abort the session).  A RECEIVE in progress ends
   with the session, or at the close (nba_close).  Returns
   NBCLI_ELOST. */
static int end(nbcli *n, u8 rc)
{
    if (n->rxon && !gone(rc))
        rc = nba_poll(n->set, NBC_HRX);
    if (!gone(rc))
        nb_hangup(n->lsn);
    n->closed = 1;
    return NBCLI_ELOST;
}

/* Run a SEND of the len bytes at NBC_TX, or with len 0 the RECEIVE
   into NBC_RX (posted unless it is already), for at most secs seconds,
   posting again while the provider has no room for it.  A command still
   in progress when the time is up is cancelled, and its final code read
   again once the cancel is confirmed, as it may have completed
   meanwhile.  Returns NBCLI_OK; or NBCLI_ELOST when the command failed
   or ran out of time, the session then ended (end). */
static int xfer(nbcli *n, u16 len, u16 secs)
{
    u32 start;
    u32 limit;
    u16 h;
    u8 rc;

    limit = (u32)secs * TMR_10SEC / 10;
    start = tmr_ticks();
    h = len != 0 ? NBC_HTX : NBC_HRX;
    for (;;) {
        if (len != 0)
            rc = nba_send(n->set, NBC_HTX, n->lsn, n->buf, NBC_TX, len);
        else
            rc = arm(n);
        if (rc != NB_RC_TOOMANY || tmr_since(start) >= limit)
            break;
        dos_idle();
    }
    if (rc == NB_OK) {
        while ((rc = nba_poll(n->set, h)) == NB_RC_PENDING) {
            if (tmr_since(start) >= limit) {
                if (nba_cancel(n->set, h) == NB_OK)
                    rc = nba_poll(n->set, h);
                break;
            }
            dos_idle();
        }
    }
    if (len == 0)
        n->rxon = 0;
    if (rc == NB_OK || rc == NB_RC_INCOMPLETE)
        return NBCLI_OK;
    return end(n, rc);
}

int nbcli_open(nbcli **out, const char *server, const char *local,
               u8 *nbrc)
{
    char srv[NB_NAMELEN];
    nbcli *n;
    int pres;
    int i;
    u8 rc;

    *out = NULL;
    *nbrc = NB_OK;
    pres = nb_present();
    if (pres != 1)
        return pres == NB_PRES_NOMEM ? NBCLI_ENOMEM : NBCLI_ENONB;
    n = (nbcli *)malloc(sizeof *n);
    if (n == NULL)
        return NBCLI_ENOMEM;
    memset(n, 0, sizeof *n);
    n->closed = 1;
    n->buf = nba_alloc(NBC_BUFSZ);
    if (n->buf == NULL || nba_open(&n->set, NBC_NCBS) != 0) {
        nbcli_close(n);
        return NBCLI_ENOMEM;
    }
    rc = nb_local_name(n->name, local, "RINV", &n->added);
    if (rc != NB_OK) {
        *nbrc = rc;
        nbcli_close(n);
        return NBCLI_ENAME;
    }
    txt_ucopy(srv, server, NB_NAMELEN);
    i = 0;
    while (((rc = nb_call(n->name, srv, &n->lsn, 0, 0)) == NB_RC_TIMEOUT ||
            rc == NB_RC_ABORTED) && ++i < NBCLI_CALLTRIES)
        tmr_delay(TMR_SEC);
    if (rc != NB_OK) {
        *nbrc = rc;
        nbcli_close(n);
        return NBCLI_ECALL;
    }
    n->closed = 0;
    /* Posted now, as after every read, so that the close sees a session
       the server ends before the first request as ended. */
    arm(n);
    *out = n;
    return NBCLI_OK;
}

const char *nbcli_name(nbcli *n)
{
    return n->name;
}

int nbcli_send(nbcli *n, const char *line, u16 len)
{
    u16 off;
    u16 m;
    u16 c;
    u16 eol;
    int rc;

    if (n->closed)
        return NBCLI_ELOST;
    arm(n);
    off = 0;
    eol = 0;
    do {
        m = (u16)(len - off);
        if (m > NB_MSGMAX)
            m = NB_MSGMAX;
        cmem_put(n->buf, NBC_TX, line + off, m);
        off = (u16)(off + m);
        if (off == len) {
            c = (u16)(NB_MSGMAX - m);
            if (c > 2 - eol)
                c = (u16)(2 - eol);
            cmem_put(n->buf, (u16)(NBC_TX + m), "\r\n" + eol, c);
            eol = (u16)(eol + c);
            m = (u16)(m + c);
        }
        rc = xfer(n, m, NBCLI_SENDWAIT);
        /* Zeroed at once, even after a SEND whose cancel the provider has
           not confirmed: the session is being dropped, and this is the
           only wipe the copy gets if nbcli_close has to keep the block. */
        cmem_set(n->buf, NBC_TX, 0, m);
    } while (rc == NBCLI_OK && eol < 2);
    if (rc == NBCLI_OK && rr_request(line, len))
        n->npend++;
    return rc;
}

/* rr_fill_fn: the next bytes of the received message, a RECEIVE
   waiting at most n->wait seconds for a message when none is left. */
static u16 fill(void *arg, char *dst, u16 room)
{
    nbcli *n;
    u16 used;

    n = (nbcli *)arg;
    while (n->rpos == n->rend) {
        if (xfer(n, 0, n->wait) != NBCLI_OK)
            return 0;
        n->rpos = 0;
        n->rend = nba_len(n->set, NBC_HRX);
    }
    used = (u16)(n->rend - n->rpos);
    if (used > room)
        used = room;
    cmem_get(n->buf, (u16)(NBC_RX + n->rpos), dst, used);
    n->rpos = (u16)(n->rpos + used);
    arm(n);
    return used;
}

int nbcli_response(nbcli *n, nbcli_line_fn fn, void *arg, u16 waitsecs)
{
    int rc;

    if (n->closed)
        return NBCLI_ELOST;
    if (n->npend == 0)
        return NBCLI_OK;
    n->wait = waitsecs;
    rc = rr_response(&n->rr, fill, n, fn, arg);
    if (rc != RR_ELOST)
        n->npend--;
    return rc;
}

int nbcli_closed(nbcli *n)
{
    return n->closed;
}

void nbcli_close(nbcli *n)
{
    if (n == NULL)
        return;
    if (!n->closed)
        end(n, NB_OK);
    if (n->added)
        nb_delete_name(n->name);
    /* A block whose RECEIVE or SEND the provider has not given back is
       kept, not freed, for the rest of the run: the provider may still
       write into it, and nba_stuck tells the program when that is over.
       Its send copy was zeroed when the SEND ended. */
    if (nba_close(n->set) == 0)
        cmem_free(n->buf);
    txt_wipe(n, (u16)sizeof *n);
    free(n);
}

/*
 * FAKENB.C - a simulated NetBIOS provider for the tests (FAKENB.H).
 *
 * Every command is a record, fn_ncb: the NCBs of a set, the LISTEN of
 * nb_listen_wait (fn_lncb, kept while a stalled cancel leaves it in
 * progress), or one on the stack of another wait-form call.  While in
 * progress it is linked on fn_busy in the order it was posted, so the
 * first record the list gives for a session or a name is the oldest.
 * Each change (a post, a hang-up, a remote station's message) settles
 * the ends it touches at once: their RECEIVEs take what waits for them,
 * and the commands of an end whose session has ended complete.  A
 * record frozen by a stalled cancel stays on the list but takes part in
 * nothing until a cancel, the closing of its end or a name's deletion
 * completes it; one held (fnb_hold) waits for a cancel alone, its LSN
 * or name no longer its own.
 *
 * Session end lsn is fn_ends[lsn - 1]; its peer field is the LSN of the
 * other end, 0 once that end has hung up or the session was aborted,
 * which is what "the session has ended" means below.  A remote
 * station's messages wait in a far block of the local end that
 * receives them, each as a 2-byte length and its bytes.  The block is
 * sized to the messages waiting, grown when one more arrives and freed
 * once none waits, so that a test with several sessions and long
 * messages fits the memory MS-DOS 3.10 leaves it.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "FAKENB.H"

#define FN_NUM1         2       /* number of the first name; 1 is the
                                   adapter's permanent name             */
/* The largest queue block: each message takes its bytes and 2 more,
   so FNB_QMAX bytes of messages of at least 1 byte fill at most three
   times as many; fnb_peer_send keeps zero-length ones within it too. */
#define FN_QBLK         (3 * FNB_QMAX)
#define FN_CHUNK        128     /* bytes copied between buffers at once */

/* Command codes, as bits so that one search takes several. */
#define FN_LISTEN       1
#define FN_RECV         2
#define FN_SEND         4
#define FN_CALL         8

/* States of a record's frozen. */
#define FN_STALLED      1       /* a stalled cancel left it in progress */
#define FN_HELD         2       /* and a hold kept it past its end      */

extern char fakenb_qblk_check[1];
extern char fakenb_qblk_check[(FN_QBLK <= CMEM_MAXBLK) ? 1 : 2];

/* A buffer: len bytes at off of a far block, or at a near pointer. */
typedef struct {
    cmem_blk *b;                /* the far block, NULL for p            */
    u8 *p;
    u16 off;
} fn_buf;

typedef struct fn_ncb fn_ncb;
struct fn_ncb {
    fn_ncb *next;               /* next on fn_busy while in progress    */
    fn_buf buf;
    u16 len;                    /* buffer size; RECEIVE: bytes received */
    u16 done;                   /* SEND: bytes the other end has taken  */
    u16 delay;                  /* set NCB: polls still to show pending */
    u16 tries;                  /* polls while in progress              */
    u8 frozen;                  /* FN_STALLED, FN_HELD or 0             */
    u8 cmd;                     /* FN_ code; 0 never posted             */
    u8 rc;                      /* final code, or NB_RC_PENDING         */
    u8 lsn;                     /* LISTEN, CALL: the session it opened  */
    u8 rto;                     /* LISTEN: the new session's timeouts   */
    u8 sto;
    u8 name[NB_NAMELEN];        /* LISTEN: the local name               */
    u8 call[NB_NAMELEN];        /* LISTEN: the remote name, then the
                                   caller's                             */
};

struct nba {
    nba *next;                  /* every set since fnb_reset            */
    fn_ncb *ncb;                /* NULL once closed with none stuck     */
    u16 n;
    u8 closed;
};

/* The buffer of a no-wait RECEIVE or SEND posted. */
typedef struct {
    fn_buf buf;
    u16 len;
} fn_post;

typedef struct {
    cmem_blk *q;                /* remote messages waiting, or NULL     */
    u16 qhead;                  /* offset of the oldest message         */
    u16 qtail;                  /* where the next message goes          */
    u16 qbytes;                 /* message bytes waiting                */
    u16 qpart;                  /* bytes of the oldest already taken    */
    u8 used;
    u8 remote;                  /* a remote station's end               */
    u8 peer;                    /* LSN of the other end, 0 once ended   */
    u8 name;                    /* its local name's index + 1; remote 0 */
    u8 rto;                     /* receive and send timeouts            */
    u8 sto;
} fn_end;

static fn_end fn_ends[FNB_MAXEND];
static u8 fn_names[FNB_NAMES][NB_NAMELEN];  /* first byte 0: free       */
static u8 fn_gone[FNB_NAMES];   /* deleted, waiting for its ends        */
static u8 fn_others[FNB_OTHERS][NB_NAMELEN];
static u16 fn_nothers;
static fn_ncb *fn_busy;
static fn_ncb fn_lncb;          /* the LISTEN of nb_listen_wait         */
static u8 fn_lcancel;           /* nb_last_cancel_rc                    */
static nba *fn_sets;
static u8 fn_found;
static int fn_present;          /* what nb_present returns              */
static u16 fn_lat;
static u16 fn_max;
static u16 fn_stalls;
static u16 fn_holds;
static u16 fn_refuse;           /* posts still to refuse (fnb_refuse)   */
static u8 fn_refrc;             /* the code they are refused with       */
static u8 fn_lrc;               /* fnb_listen_rc, NB_OK for none        */
static u16 fn_cfails;           /* CALLs still to fail (fnb_call_fail)  */
static u8 fn_cfrc;              /* the code they fail with              */
static u16 fn_cholds;           /* no-wait CALLs still to hold          */
static u16 fn_badhang;          /* HANG UPs refused with NB_RC_BADLSN   */
static u16 fn_late;             /* RECEIVEs posted after their message  */
static fn_post fn_posts[2];     /* the last RECEIVE, the last SEND      */
static fnb_pump_fn fn_pumpfn;
static void *fn_pumparg;
static u8 fn_inpump;

/* Store name into a 16-byte NetBIOS name field, as CNETB.C does. */
static void field(u8 *f, const char *name)
{
    int i;

    for (i = 0; i < NB_NAMELEN - 1 && name[i] != '\0'; i++)
        f[i] = (u8)name[i];
    for (; i < NB_NAMELEN; i++)
        f[i] = (u8)' ';
    if (!(name[0] == '*' && name[1] == '\0'))
        f[NB_NAMELEN - 1] = 0x00;
}

/* Index of field f in the n entries of tab, or -1. */
static int name_at(u8 (*tab)[NB_NAMELEN], int n, const u8 *f)
{
    int i;

    for (i = 0; i < n; i++)
        if (memcmp(tab[i], f, NB_NAMELEN) == 0)
            return i;
    return -1;
}

/* Index of field f in the local name table if it is not being deleted,
   or -1. */
static int usable(const u8 *f)
{
    int i;

    i = name_at(fn_names, FNB_NAMES, f);
    return i >= 0 && !fn_gone[i] ? i : -1;
}

static void pump(void)
{
    if (fn_pumpfn != NULL && !fn_inpump) {
        fn_inpump = 1;
        fn_pumpfn(fn_pumparg);
        fn_inpump = 0;
    }
}

/* Copy n bytes from offset from of buffer src to offset to of dst. */
static void copy(const fn_buf *dst, u16 to, const fn_buf *src, u16 from,
                 u16 n)
{
    u8 tmp[FN_CHUNK];
    u16 k;

    while (n > 0) {
        k = n < FN_CHUNK ? n : FN_CHUNK;
        if (src->b != NULL)
            cmem_get(src->b, (u16)(src->off + from), tmp, k);
        else
            memcpy(tmp, src->p + from, k);
        if (dst->b != NULL)
            cmem_put(dst->b, (u16)(dst->off + to), tmp, k);
        else
            memcpy(dst->p + to, tmp, k);
        from = (u16)(from + k);
        to = (u16)(to + k);
        n = (u16)(n - k);
    }
}

/* 1 while the program may not reuse the NCB. */
static int busy(const fn_ncb *c)
{
    return c->rc == NB_RC_PENDING || c->delay > 0;
}

/* 1 while the NCB c of s counts as in progress: for a closed set, while
   the provider still owns it. */
static int owned(const nba *s, const fn_ncb *c)
{
    return s->closed ? c->rc == NB_RC_PENDING : busy(c);
}

/* Start the record c of command cmd on lsn and link it on fn_busy. */
static void start(fn_ncb *c, u8 cmd, u8 lsn)
{
    fn_ncb **pp;

    c->cmd = cmd;
    c->rc = NB_RC_PENDING;
    c->lsn = lsn;
    c->done = 0;
    c->tries = 0;
    c->frozen = 0;
    for (pp = &fn_busy; *pp != NULL; pp = &(*pp)->next)
        ;
    c->next = NULL;
    *pp = c;
}

/* Complete c with rc: off fn_busy, its latency starting. */
static void finish(fn_ncb *c, u8 rc)
{
    fn_ncb **pp;

    for (pp = &fn_busy; *pp != c; pp = &(*pp)->next)
        ;
    *pp = c->next;
    c->rc = rc;
    c->delay = fn_lat;
}

/* The oldest RECEIVE or SEND in progress on lsn whose code is in cmds,
   or NULL. */
static fn_ncb *oldest(u8 cmds, u8 lsn)
{
    fn_ncb *c;

    for (c = fn_busy; c != NULL; c = c->next)
        if (!c->frozen && (c->cmd & cmds) != 0 && c->lsn == lsn)
            return c;
    return NULL;
}

/* Complete the stalled command c with rc, as the closing of its end or
   a name's deletion does, unless a hold keeps it. */
static void thaw(fn_ncb *c, u8 rc)
{
    if (fn_holds > 0) {
        fn_holds--;
        c->frozen = FN_HELD;
    } else {
        finish(c, rc);
    }
}

/* Complete every RECEIVE and SEND in progress on lsn with rc, the
   stalled ones too, as thaw does, when frozen is 1; the number of
   commands not stalled that it completed. */
static u16 finish_all(u8 lsn, u8 rc, int frozen)
{
    fn_ncb *c;
    fn_ncb *next;
    u16 n;

    n = 0;
    for (c = fn_busy; c != NULL; c = next) {
        next = c->next;
        if ((c->cmd & (FN_RECV | FN_SEND)) == 0 || c->lsn != lsn)
            continue;
        if (!c->frozen) {
            finish(c, rc);
            n++;
        } else if (frozen && c->frozen == FN_STALLED) {
            thaw(c, rc);
        }
    }
    return n;
}

/* The end lsn of the program (remote 0) or of a remote station, or
   NULL when no such end is open. */
static fn_end *end_of(u8 lsn, u8 remote)
{
    fn_end *e;

    if (lsn == 0 || lsn > FNB_MAXEND)
        return NULL;
    e = &fn_ends[lsn - 1];
    return e->used && e->remote == remote ? e : NULL;
}

static u16 ends_free(void)
{
    int i;
    u16 n;

    n = 0;
    for (i = 0; i < FNB_MAXEND; i++)
        if (!fn_ends[i].used)
            n++;
    return n;
}

/* Open the lowest free end for the local name of index name - 1 (0 for
   a remote end); its LSN.  The caller has checked that one is free. */
static u8 end_new(u8 remote, u8 name, u8 rto, u8 sto)
{
    fn_end *e;

    for (e = fn_ends; e->used; e++)
        ;
    memset(e, 0, sizeof *e);
    e->used = 1;
    e->remote = remote;
    e->name = name;
    e->rto = rto;
    e->sto = sto;
    return (u8)(e - fn_ends + 1);
}

/* The number of open ends of the local name of index i. */
static u16 name_ends(int i)
{
    int k;
    u16 n;

    n = 0;
    for (k = 0; k < FNB_MAXEND; k++)
        if (fn_ends[k].used && fn_ends[k].name == i + 1)
            n++;
    return n;
}

/* Free e's queue block, with whatever messages still wait in it. */
static void drop_queue(fn_end *e)
{
    cmem_free(e->q);
    e->q = NULL;
    e->qhead = 0;
    e->qtail = 0;
}

/* Give the next message waiting at end lsn, or as much of it as fits
   size bytes, to dst; *got the bytes given.  NB_OK when it was the
   whole message, or its rest, NB_RC_INCOMPLETE when more of it waits,
   NB_RC_PENDING (*got untouched) when no message waits.  A local
   sender's SEND completes with its last byte. */
static u8 take(u8 lsn, const fn_buf *dst, u16 size, u16 *got)
{
    fn_end *e;
    fn_ncb *s;
    fn_buf src;
    u8 hdr[2];
    u16 at;
    u16 left;
    u16 n;

    e = &fn_ends[lsn - 1];
    s = NULL;
    if (e->qhead == e->qtail && e->peer != 0)
        s = oldest(FN_SEND, e->peer);
    if (e->qhead != e->qtail) {
        cmem_get(e->q, e->qhead, hdr, 2);
        src.b = e->q;
        src.off = 0;
        at = (u16)(e->qhead + 2 + e->qpart);
        left = (u16)(get_le16(hdr) - e->qpart);
    } else if (s != NULL) {
        src = s->buf;
        at = s->done;
        left = (u16)(s->len - s->done);
    } else {
        return NB_RC_PENDING;
    }
    n = left < size ? left : size;
    copy(dst, 0, &src, at, n);
    *got = n;
    if (s != NULL) {
        s->done = (u16)(s->done + n);
        if (n == left)
            finish(s, NB_OK);
    } else if (n < left) {
        e->qpart = (u16)(e->qpart + n);
    } else {
        e->qbytes = (u16)(e->qbytes - get_le16(hdr));
        e->qhead = (u16)(at + n);
        e->qpart = 0;
        if (e->qhead == e->qtail)
            drop_queue(e);
    }
    return n == left ? NB_OK : NB_RC_INCOMPLETE;
}

/* Close the open end lsn: its commands in progress complete with rc,
   the stalled ones as thaw does, its waiting messages go, and with the
   last end of a deleted name the name leaves the table. */
static void end_close(u8 lsn, u8 rc)
{
    fn_end *e;

    e = &fn_ends[lsn - 1];
    finish_all(lsn, rc, 1);
    drop_queue(e);
    e->used = 0;
    if (e->name != 0 && fn_gone[e->name - 1] && name_ends(e->name - 1) == 0) {
        memset(fn_names[e->name - 1], 0, NB_NAMELEN);
        fn_gone[e->name - 1] = 0;
    }
}

/* Bring the commands of end lsn up to date: its RECEIVEs take what
   waits; once its session has ended, the rest complete with
   NB_RC_SESSCLOSED (a RECEIVE still in progress then has nothing left
   to wait for), and a local end that so reported the end closes. */
static void settle(u8 lsn)
{
    fn_ncb *r;
    u8 rc;
    u16 n;

    for (r = oldest(FN_RECV, lsn); r != NULL; r = oldest(FN_RECV, lsn)) {
        rc = take(lsn, &r->buf, r->len, &n);
        if (rc == NB_RC_PENDING)
            break;
        r->len = n;
        finish(r, rc);
    }
    if (fn_ends[lsn - 1].peer == 0 &&
        finish_all(lsn, NB_RC_SESSCLOSED, 0) > 0)
        end_close(lsn, NB_RC_SESSCLOSED);
}

/* End the session of end lsn abnormally on that end: its commands in
   progress complete with NB_RC_ABORTED, its waiting messages go, and a
   local end that so reported the end closes. */
static void end_abort(u8 lsn)
{
    fn_end *e;

    e = &fn_ends[lsn - 1];
    e->peer = 0;
    drop_queue(e);
    if (finish_all(lsn, NB_RC_ABORTED, 0) > 0)
        end_close(lsn, NB_RC_ABORTED);
}

/* End the session of the end lsn abnormally, on both its ends. */
static void session_abort(u8 lsn)
{
    u8 p;

    p = fn_ends[lsn - 1].peer;
    end_abort(lsn);
    if (p != 0)
        end_abort(p);
}

/* Hang up the open end lsn: NB_OK, or NB_RC_SESSCLOSED when its session
   had ended.  The end closes either way. */
static u8 hangup(u8 lsn)
{
    u8 p;

    p = fn_ends[lsn - 1].peer;
    end_close(lsn, NB_RC_SESSCLOSED);
    if (p == 0)
        return NB_RC_SESSCLOSED;
    fn_ends[p - 1].peer = 0;
    settle(p);
    return NB_OK;
}

/* Call the local name to from the name from, for the program (remote 0,
   from then a local name) or a remote station; *lsn the caller's end. */
static u8 call(const char *from, const char *to, u8 remote, u8 *lsn,
               u8 rto, u8 sto)
{
    fn_ncb *l;
    u8 f[NB_NAMELEN];
    u8 t[NB_NAMELEN];
    u8 any[NB_NAMELEN];
    int fi;
    int ti;
    u8 a;

    *lsn = 0;
    field(f, from);
    field(t, to);
    field(any, "*");
    fi = usable(f);
    if (!remote && fi < 0)
        return NB_RC_NONAME;
    ti = usable(t);
    if (ti < 0)
        return NB_RC_NOANSWER;
    for (l = fn_busy; l != NULL; l = l->next)
        if (!l->frozen && l->cmd == FN_LISTEN &&
            memcmp(l->name, t, NB_NAMELEN) == 0 &&
            (memcmp(l->call, any, NB_NAMELEN) == 0 ||
             memcmp(l->call, f, NB_NAMELEN) == 0))
            break;
    if (l == NULL)
        return NB_RC_REJECTED;
    if (ends_free() < 2)
        return NB_RC_SESSFULL;
    a = end_new(0, (u8)(ti + 1), l->rto, l->sto);
    *lsn = end_new(remote, (u8)(remote ? 0 : fi + 1), rto, sto);
    fn_ends[a - 1].peer = *lsn;
    fn_ends[*lsn - 1].peer = a;
    memcpy(l->call, f, NB_NAMELEN);
    l->lsn = a;
    finish(l, NB_OK);
    return NB_OK;
}

/* Run the CALL c of the program (nb_call or nba_call) to completion:
   as call makes it, unless fnb_call_fail fails it.  One refused for
   want of an answer or a LISTEN, or failed so, then runs the pump
   once, for the time a provider spends on it. */
static void call_out(fn_ncb *c, const char *local, const char *remote,
                     u8 rto, u8 sto)
{
    u8 rc;
    int failed;

    start(c, FN_CALL, 0);
    failed = fn_cfails > 0;
    if (failed) {
        fn_cfails--;
        rc = fn_cfrc;
    } else {
        rc = call(local, remote, 0, &c->lsn, rto, sto);
    }
    finish(c, rc);
    if (failed || rc == NB_RC_NOANSWER || rc == NB_RC_REJECTED)
        pump();
}

/* Post the LISTEN c, unless its name is not usable or no end is free;
   it completes at once when fnb_listen_rc asks. */
static u8 post_listen(fn_ncb *c, const char *local, const char *remote,
                      u8 rto, u8 sto)
{
    u8 f[NB_NAMELEN];

    field(f, local);
    if (usable(f) < 0)
        return NB_RC_NONAME;
    if (ends_free() == 0)
        return NB_RC_SESSFULL;
    memcpy(c->name, f, NB_NAMELEN);
    field(c->call, remote);
    c->rto = rto;
    c->sto = sto;
    start(c, FN_LISTEN, 0);
    if (fn_lrc != NB_OK) {
        finish(c, fn_lrc);
        fn_lrc = NB_OK;
    }
    return NB_OK;
}

/* 1 when the start of a message waits at end lsn: one the remote
   station queued, or the SEND of the other end, none of it taken. */
static int waiting(u8 lsn)
{
    fn_end *e;
    fn_ncb *s;

    e = &fn_ends[lsn - 1];
    if (e->qhead != e->qtail)
        return e->qpart == 0;
    s = e->peer != 0 ? oldest(FN_SEND, e->peer) : NULL;
    return s != NULL && s->done == 0;
}

/* Post the RECEIVE or SEND c of len bytes, its buffer set, on lsn, which
   a local end uses, and run it as far as it goes. */
static void post_io(fn_ncb *c, u8 cmd, u8 lsn, u16 len)
{
    u8 peer;

    if (cmd == FN_RECV && waiting(lsn))
        fn_late++;
    c->len = len;
    start(c, cmd, lsn);
    peer = fn_ends[lsn - 1].peer;
    if (cmd == FN_SEND && peer != 0)
        settle(peer);
    settle(lsn);
}

/* The tries the session's timeout gives the RECEIVE or SEND c in
   progress; 0 for none. */
static u16 timeout(const fn_ncb *c)
{
    const fn_end *e;

    e = &fn_ends[c->lsn - 1];
    return c->cmd == FN_RECV ? e->rto : e->sto;
}

/* Time out the command c in progress; a SEND's session ends. */
static void expire(fn_ncb *c)
{
    finish(c, NB_RC_TIMEOUT);
    if (c->cmd == FN_SEND)
        session_abort(c->lsn);
}

/* Cancel c, which must be in progress for the program: NB_OK when the
   NCB is free afterwards, NB_RC_PENDING when a stall keeps it.  The
   first cancel of a SEND aborts its session. */
static u8 cancel(fn_ncb *c)
{
    u8 first;

    if (c->rc != NB_RC_PENDING) {
        c->delay = 0;
        return NB_OK;
    }
    first = !c->frozen;
    if (fn_stalls > 0) {
        fn_stalls--;
        if (first)
            c->frozen = FN_STALLED;
    } else {
        finish(c, NB_RC_CANCELLED);
        c->delay = 0;
    }
    if (first && c->cmd == FN_SEND)
        session_abort(c->lsn);
    return c->rc == NB_RC_PENDING ? NB_RC_PENDING : NB_OK;
}

/* Wait for the wait-form command c: pump between tries, at most limit
   tries (0 or more than FNB_SPINS: FNB_SPINS).  Then it times out,
   except the LISTEN of nb_listen_wait, which is cancelled and gives
   NB_RC_TIMEOUT, or NB_RC_PENDING when a stall keeps it. */
static u8 spin(fn_ncb *c, u16 limit)
{
    int i;

    if (limit == 0 || limit > FNB_SPINS)
        limit = FNB_SPINS;
    for (i = 0; c->rc == NB_RC_PENDING && i < limit; i++)
        pump();
    if (c->rc != NB_RC_PENDING)
        return c->rc;
    if (c != &fn_lncb) {
        expire(c);
        return c->rc;
    }
    if (cancel(c) == NB_RC_PENDING) {
        fn_lcancel = NB_RC_NOCANCEL;
        return NB_RC_PENDING;
    }
    fn_lcancel = NB_OK;
    return NB_RC_TIMEOUT;
}

/* Run the wait-form RECEIVE or SEND c of len bytes at buf on lsn. */
static u8 wait_io(fn_ncb *c, u8 cmd, u8 lsn, void *buf, u16 len)
{
    if (end_of(lsn, 0) == NULL)
        return NB_RC_BADLSN;
    c->buf.b = NULL;
    c->buf.p = (u8 *)buf;
    post_io(c, cmd, lsn, len);
    return spin(c, timeout(c));
}

/* NCB h of s, or NULL when h is out of range.  A set already closed
   ends the program. */
static fn_ncb *ncb_at(nba *s, u16 h)
{
    if (s->closed)
        abort();
    return h < s->n ? &s->ncb[h] : NULL;
}

/* The NCB h of s for a new no-wait command, args telling whether its
   arguments are valid, or NULL with *rc the code the post gives. */
static fn_ncb *ncb_post(nba *s, u16 h, int args, u8 *rc)
{
    fn_ncb *c;

    c = ncb_at(s, h);
    *rc = NB_OK;
    if (!fn_found) {
        *rc = NB_RC_NOT_PRESENT;
    } else if (c == NULL || busy(c) || !args) {
        *rc = NB_RC_ILLCMD;
    } else if (fn_refuse > 0) {
        fn_refuse--;
        *rc = fn_refrc;
    } else if (fn_max != 0 && fnb_pending() >= fn_max) {
        *rc = NB_RC_TOOMANY;
    }
    return *rc == NB_OK ? c : NULL;
}

/* ---------------------------------------------------------------------
 * The test's controls.
 * --------------------------------------------------------------------- */

void fnb_reset(void)
{
    nba *s;
    int i;

    while (fn_sets != NULL) {
        s = fn_sets;
        fn_sets = s->next;
        free(s->ncb);
        free(s);
    }
    for (i = 0; i < FNB_MAXEND; i++)
        cmem_free(fn_ends[i].q);
    memset(fn_ends, 0, sizeof fn_ends);
    memset(fn_names, 0, sizeof fn_names);
    memset(fn_gone, 0, sizeof fn_gone);
    memset(&fn_lncb, 0, sizeof fn_lncb);
    fn_lcancel = NB_OK;
    fn_nothers = 0;
    fn_busy = NULL;
    fn_found = 0;
    fn_present = 1;
    fn_lat = 0;
    fn_max = 0;
    fn_stalls = 0;
    fn_holds = 0;
    fn_refuse = 0;
    fn_lrc = NB_OK;
    fn_cfails = 0;
    fn_cfrc = NB_RC_TIMEOUT;
    fn_cholds = 0;
    fn_badhang = 0;
    fn_late = 0;
    memset(fn_posts, 0, sizeof fn_posts);
    fn_pumpfn = NULL;
    fn_pumparg = NULL;
}

void fnb_latency(u16 polls)
{
    fn_lat = polls;
}

void fnb_ncbmax(u16 n)
{
    fn_max = n;
}

void fnb_stall(u16 n)
{
    fn_stalls = n;
}

void fnb_hold(u16 n)
{
    fn_holds = n;
}

void fnb_refuse(u16 n, u8 rc)
{
    fn_refuse = n;
    fn_refrc = rc;
}

void fnb_listen_rc(u8 rc)
{
    fn_lrc = rc;
}

void fnb_call_fail(u16 n, u8 rc)
{
    fn_cfails = n;
    fn_cfrc = rc;
}

void fnb_call_hold(u16 n)
{
    fn_cholds = n;
}

cmem_blk *fnb_lastbuf(int send, u16 *off, u16 *len)
{
    const fn_post *p;

    p = &fn_posts[send ? 1 : 0];
    *off = p->buf.off;
    *len = p->len;
    return p->buf.b;
}

void fnb_dupname(const char *name)
{
    if (fn_nothers == FNB_OTHERS)
        abort();
    field(fn_others[fn_nothers++], name);
}

void fnb_abort(u8 lsn)
{
    if (end_of(lsn, 0) != NULL || end_of(lsn, 1) != NULL)
        session_abort(lsn);
}

void fnb_fail(u8 rc)
{
    fn_ncb *c;

    for (c = fn_busy; c != NULL; c = c->next)
        if (!c->frozen && (c->cmd & (FN_RECV | FN_SEND)) != 0) {
            finish(c, rc);
            return;
        }
}

u8 fnb_peer_call(const char *from, const char *to, u8 *lsn)
{
    return call(from, to, 1, lsn, 0, 0);
}

/* Give e's queue a block just large enough for the messages waiting
   and len more bytes, moving them to its start, so that every message
   queued reallocates it: 1, or 0 (nothing changed) when there is no far
   memory for it. */
static int grow(fn_end *e, u16 len)
{
    cmem_blk *q;
    u16 used;

    used = (u16)(e->qtail - e->qhead);
    q = cmem_alloc((u16)(used + 2 + len));
    if (q == NULL)
        return 0;
    if (used > 0)
        cmem_copy(q, 0, e->q, e->qhead, used);
    cmem_free(e->q);
    e->q = q;
    e->qhead = 0;
    e->qtail = used;
    return 1;
}

u8 fnb_peer_send(u8 lsn, const void *buf, u16 len)
{
    fn_end *e;
    fn_end *p;
    u8 hdr[2];

    e = end_of(lsn, 1);
    if (e == NULL)
        return NB_RC_BADLSN;
    if (e->peer == 0)
        return NB_RC_SESSCLOSED;
    if (len > NB_MSGMAX)
        return NB_RC_ILLCMD;
    p = &fn_ends[e->peer - 1];
    if (p->qbytes + len > FNB_QMAX ||
        p->qtail - p->qhead + 2 + len > FN_QBLK)
        return NB_RC_TOOMANY;
    if (!grow(p, len))
        return NB_RC_TOOMANY;
    put_le16(hdr, len);
    cmem_put(p->q, p->qtail, hdr, 2);
    cmem_put(p->q, (u16)(p->qtail + 2), buf, len);
    p->qtail = (u16)(p->qtail + 2 + len);
    p->qbytes = (u16)(p->qbytes + len);
    settle(e->peer);
    return NB_OK;
}

u8 fnb_peer_recv(u8 lsn, void *buf, u16 size, u16 *len)
{
    fn_buf dst;
    u8 rc;

    *len = 0;
    if (end_of(lsn, 1) == NULL)
        return NB_RC_BADLSN;
    dst.b = NULL;
    dst.p = (u8 *)buf;
    rc = take(lsn, &dst, size, len);
    if (rc == NB_RC_PENDING && fn_ends[lsn - 1].peer == 0)
        rc = NB_RC_SESSCLOSED;
    return rc;
}

u8 fnb_peer_hangup(u8 lsn)
{
    return end_of(lsn, 1) != NULL ? hangup(lsn) : NB_RC_BADLSN;
}

void fnb_pump(fnb_pump_fn fn, void *arg)
{
    fn_pumpfn = fn;
    fn_pumparg = arg;
}

u16 fnb_pending(void)
{
    nba *s;
    int h;
    u16 n;

    n = 0;
    for (s = fn_sets; s != NULL; s = s->next)
        for (h = 0; s->ncb != NULL && h < s->n; h++)
            if (owned(s, &s->ncb[h]))
                n++;
    return n;
}

u16 fnb_sets(void)
{
    nba *s;
    u16 n;

    n = 0;
    for (s = fn_sets; s != NULL; s = s->next)
        if (!s->closed)
            n++;
    return n;
}

u16 fnb_names(void)
{
    int i;
    u16 n;

    n = 0;
    for (i = 0; i < FNB_NAMES; i++)
        if (fn_names[i][0] != 0)
            n++;
    return n;
}

u16 fnb_sessions(void)
{
    return (u16)(FNB_MAXEND - ends_free());
}

u16 fnb_badhangups(void)
{
    return fn_badhang;
}

u16 fnb_late(void)
{
    return fn_late;
}

void fnb_present(int rc)
{
    fn_present = rc;
}

/* ---------------------------------------------------------------------
 * The wait-form commands.
 * --------------------------------------------------------------------- */

int nb_present(void)
{
    fn_found = fn_present == 1;
    return fn_present;
}

u8 nb_last_cancel_rc(void)
{
    return fn_lcancel;
}

u8 nb_add_name(const char *name, u8 *num)
{
    u8 f[NB_NAMELEN];
    u8 empty[NB_NAMELEN];
    int i;

    *num = 0;
    if (!fn_found)
        return NB_RC_NOT_PRESENT;
    field(f, name);
    memset(empty, 0, sizeof empty);
    if (name_at(fn_names, FNB_NAMES, f) >= 0)
        return NB_RC_DUPNAME;
    if (name_at(fn_others, (int)fn_nothers, f) >= 0)
        return NB_RC_NAMEINUSE;
    i = name_at(fn_names, FNB_NAMES, empty);
    if (i < 0)
        return NB_RC_NAMEFULL;
    memcpy(fn_names[i], f, NB_NAMELEN);
    *num = (u8)(i + FN_NUM1);
    return NB_OK;
}

u8 nb_delete_name(const char *name)
{
    fn_ncb *c;
    fn_ncb *next;
    u8 f[NB_NAMELEN];
    int i;

    if (!fn_found)
        return NB_RC_NOT_PRESENT;
    field(f, name);
    i = usable(f);
    if (i < 0)
        return NB_RC_NONAME;
    for (c = fn_busy; c != NULL; c = next) {
        next = c->next;
        if (c->cmd != FN_LISTEN || c->frozen == FN_HELD ||
            memcmp(c->name, f, NB_NAMELEN) != 0)
            continue;
        if (c->frozen)
            thaw(c, NB_RC_NAMEDEL);
        else
            finish(c, NB_RC_NAMEDEL);
    }
    if (name_ends(i) > 0) {
        fn_gone[i] = 1;
        return NB_RC_DELPEND;
    }
    memset(fn_names[i], 0, NB_NAMELEN);
    return NB_OK;
}

u8 nb_call(const char *local, const char *remote, u8 *lsn, u8 rto, u8 sto)
{
    fn_ncb c;

    *lsn = 0;
    if (!fn_found)
        return NB_RC_NOT_PRESENT;
    call_out(&c, local, remote, rto, sto);
    *lsn = c.lsn;
    return c.rc;
}

/* Run the wait-form LISTEN c, at most limit tries as spin counts them;
   *lsn the session it opened, else 0. */
static u8 wait_listen(fn_ncb *c, const char *local, const char *remote,
                      u8 *lsn, u8 rto, u8 sto, u16 limit)
{
    u8 rc;

    *lsn = 0;
    if (!fn_found)
        return NB_RC_NOT_PRESENT;
    rc = post_listen(c, local, remote, rto, sto);
    if (rc == NB_OK)
        rc = spin(c, limit);
    if (rc == NB_OK)
        *lsn = c->lsn;
    return rc;
}

u8 nb_listen(const char *local, const char *remote, u8 *lsn, u8 rto, u8 sto)
{
    fn_ncb c;

    return wait_listen(&c, local, remote, lsn, rto, sto, 0);
}

u8 nb_listen_wait(const char *local, const char *remote, u8 *lsn,
                  u8 rto, u8 sto, u16 seconds)
{
    if (fn_found && fn_lncb.rc == NB_RC_PENDING) {
        *lsn = 0;
        return NB_RC_PENDING;
    }
    return wait_listen(&fn_lncb, local, remote, lsn, rto, sto, seconds);
}

u8 nb_send(u8 lsn, const void *buf, u16 len)
{
    fn_ncb c;

    if (!fn_found)
        return NB_RC_NOT_PRESENT;
    return wait_io(&c, FN_SEND, lsn, (void *)buf, len);
}

u8 nb_recv(u8 lsn, void *buf, u16 *len)
{
    fn_ncb c;
    u8 rc;

    if (!fn_found) {
        *len = 0;
        return NB_RC_NOT_PRESENT;
    }
    rc = wait_io(&c, FN_RECV, lsn, buf, *len);
    *len = rc == NB_OK || rc == NB_RC_INCOMPLETE ? c.len : 0;
    return rc;
}

u8 nb_hangup(u8 lsn)
{
    if (!fn_found)
        return NB_RC_NOT_PRESENT;
    if (end_of(lsn, 0) != NULL)
        return hangup(lsn);
    fn_badhang++;
    return NB_RC_BADLSN;
}

/* ---------------------------------------------------------------------
 * The no-wait commands.
 * --------------------------------------------------------------------- */

int nba_open(nba **out, u16 n)
{
    nba *s;

    *out = NULL;
    if (n == 0 || n > NBA_MAX)
        return CF_EINVAL;
    s = (nba *)malloc(sizeof *s);
    if (s == NULL)
        return CF_ENOMEM;
    s->ncb = (fn_ncb *)malloc(n * sizeof *s->ncb);
    if (s->ncb == NULL) {
        free(s);
        return CF_ENOMEM;
    }
    memset(s->ncb, 0, n * sizeof *s->ncb);
    s->n = n;
    s->closed = 0;
    s->next = fn_sets;
    fn_sets = s;
    *out = s;
    return 0;
}

u16 nba_close(nba *s)
{
    u16 h;
    u16 stuck;

    if (s == NULL)
        return 0;
    stuck = 0;
    for (h = 0; h < s->n; h++)
        if (cancel(ncb_at(s, h)) == NB_RC_PENDING)
            stuck++;
    s->closed = 1;
    if (stuck == 0) {
        free(s->ncb);
        s->ncb = NULL;
    }
    return stuck;
}

u16 nba_stuck(void)
{
    nba *s;
    int h;
    u16 k;
    u16 n;

    n = 0;
    for (s = fn_sets; s != NULL; s = s->next) {
        if (!s->closed || s->ncb == NULL)
            continue;
        k = 0;
        for (h = 0; h < s->n; h++) {
            if (s->ncb[h].frozen == FN_HELD)
                cancel(&s->ncb[h]);
            if (owned(s, &s->ncb[h]))
                k++;
        }
        if (k == 0) {
            free(s->ncb);
            s->ncb = NULL;
        }
        n = (u16)(n + k);
    }
    return n;
}

u8 nba_listen(nba *s, u16 h, const char *local, const char *remote,
              u8 rto, u8 sto)
{
    fn_ncb *c;
    u8 rc;

    c = ncb_post(s, h, 1, &rc);
    return c != NULL ? post_listen(c, local, remote, rto, sto) : rc;
}

/* Post a no-wait RECEIVE or SEND. */
static u8 post_nowait(nba *s, u16 h, u8 cmd, u8 lsn, cmem_blk *b, u16 off,
                      u16 len)
{
    fn_ncb *c;
    fn_post *p;
    u8 rc;

    c = ncb_post(s, h, len > 0 && len <= NB_MSGMAX, &rc);
    if (c == NULL)
        return rc;
    if (end_of(lsn, 0) == NULL)
        return NB_RC_BADLSN;
    c->buf.b = b;
    c->buf.off = off;
    post_io(c, cmd, lsn, len);
    p = &fn_posts[cmd == FN_SEND];
    p->buf = c->buf;
    p->len = len;
    return NB_OK;
}

u8 nba_recv(nba *s, u16 h, u8 lsn, cmem_blk *b, u16 off, u16 len)
{
    return post_nowait(s, h, FN_RECV, lsn, b, off, len);
}

u8 nba_send(nba *s, u16 h, u8 lsn, cmem_blk *b, u16 off, u16 len)
{
    return post_nowait(s, h, FN_SEND, lsn, b, off, len);
}

u8 nba_call(nba *s, u16 h, const char *local, const char *remote,
            u8 rto, u8 sto)
{
    fn_ncb *c;
    u8 rc;

    c = ncb_post(s, h, 1, &rc);
    if (c == NULL)
        return rc;
    if (fn_cholds > 0) {
        fn_cholds--;
        start(c, FN_CALL, 0);
    } else {
        call_out(c, local, remote, rto, sto);
    }
    return NB_OK;
}

u8 nba_poll(nba *s, u16 h)
{
    fn_ncb *c;
    u16 t;

    c = ncb_at(s, h);
    if (c == NULL)
        return NB_RC_ILLCMD;
    pump();
    if (c->rc == NB_RC_PENDING && !c->frozen &&
        (c->cmd & (FN_RECV | FN_SEND)) != 0) {
        t = timeout(c);
        if (t != 0 && ++c->tries == t)
            expire(c);
    }
    if (c->rc == NB_RC_PENDING)
        return NB_RC_PENDING;
    if (c->delay > 0) {
        c->delay--;
        return NB_RC_PENDING;
    }
    return c->rc;
}

u8 nba_lsn(nba *s, u16 h)
{
    fn_ncb *c;

    c = ncb_at(s, h);
    return c == NULL || busy(c) ? (u8)0 : c->lsn;
}

u16 nba_len(nba *s, u16 h)
{
    fn_ncb *c;

    c = ncb_at(s, h);
    if (c == NULL || busy(c) || (c->cmd & (FN_RECV | FN_SEND)) == 0 ||
        (c->rc != NB_OK && c->rc != NB_RC_INCOMPLETE))
        return 0;
    return c->len;
}

void nba_callname(nba *s, u16 h, char *out)
{
    fn_ncb *c;
    int n;

    c = ncb_at(s, h);
    n = 0;
    if (c != NULL && !busy(c) && c->cmd == FN_LISTEN && c->rc == NB_OK) {
        memcpy(out, c->call, NB_NAMELEN - 1);
        for (n = NB_NAMELEN - 1; n > 0 && out[n - 1] == ' '; n--)
            ;
    }
    out[n] = '\0';
}

u8 nba_cancel(nba *s, u16 h)
{
    fn_ncb *c;

    c = ncb_at(s, h);
    return c != NULL ? cancel(c) : NB_RC_ILLCMD;
}

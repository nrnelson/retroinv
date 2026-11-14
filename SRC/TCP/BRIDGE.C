/*
 * BRIDGE.C - the TCP bridge's slots, each relaying one TCP connection
 * over one NetBIOS session (BRIDGE.H).
 *
 * Slot k uses socket k, NCB 2k for its CALL and then its RECEIVEs, NCB
 * 2k + 1 for its SENDs, and the transmit and receive areas at
 * 2k * NB_MSGMAX in the one far block.  It keeps a mask of its commands
 * in progress.  A pass takes at most one completed command of a slot,
 * then relays and posts what that made possible.  Ending a session
 * cancels its commands, the SEND first, and hangs it up unless the
 * provider has removed it; a cancel the provider has not confirmed
 * leaves the slot to end on a later pass, since the NCB and its area
 * are the provider's until then.  A slot listens again only once its
 * NCBs are free and its connection is TS_CLOSED.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"

/* Two NCBs per slot fit one set, every slot's areas one far block, and
   the machine name a buffer of NB_NAMELEN bytes.  Each pair below
   declares one array twice, the second time with size 2 when the
   condition fails: the declarations then disagree, which every
   compiler refuses. */
extern char br_ncbs_ok[1];
extern char br_ncbs_ok[(2 * TS_SLOTS <= NBA_MAX) ? 1 : 2];
extern char br_blk_ok[1];
extern char br_blk_ok[(2 * TS_SLOTS * (u32)NB_MSGMAX <= CMEM_MAXBLK) ?
                      1 : 2];
extern char br_machlen_ok[1];
extern char br_machlen_ok[(DOS_MACHLEN < NB_NAMELEN) ? 1 : 2];

#define BR_BLKSZ        ((u16)(2 * TS_SLOTS * NB_MSGMAX)) /* all areas  */
#define BR_HEXSZ        3       /* NOCALL's text: two hex digits, NUL   */
/* OPEN's text: each octet's digits and a '.' or ':', then the port in
   the TXT_U32LEN + 1 bytes txt_u32 writes into. */
#define BR_PEERSZ       (TS_IPLEN * 4 + TXT_U32LEN + 1)

/* Slot states. */
#define BS_LISTEN       0       /* the socket listens                   */
#define BS_CALLING      1       /* a connection, its CALL due or posted */
#define BS_OPEN         2       /* a session: bytes are relayed         */
#define BS_DRAIN        3       /* the session ends, the client gets
                                   the bytes received                   */
#define BS_CLOSING      4       /* the connection ends                  */

/* Bits of busy: the commands in progress. */
#define BB_CALL         1       /* the CALL, on NCB 2k                  */
#define BB_RX           2       /* a RECEIVE, on NCB 2k                 */
#define BB_TX           4       /* a SEND, on NCB 2k + 1                */

typedef struct {
    u8   state;                 /* BS_ state                            */
    u8   busy;                  /* BB_ bits                             */
    u8   lsn;                   /* the session, 0 for none              */
    u8   retried;               /* 1 once the CALL has timed out        */
    u16  txlen;                 /* bytes in the transmit area           */
    u16  rxlen;                 /* bytes the last RECEIVE received      */
    u16  rxoff;                 /* of them, bytes given to the client   */
    u32  since;                 /* tick the CALL timed out, the client
                                   last took bytes, or the drain or
                                   close began                          */
} br_slot;

typedef struct {
    nba *set;                   /* the NCBs, NULL when not started      */
    cmem_blk *buf;              /* every slot's areas                   */
    u8   added;                 /* 1 when br_start added local          */
    u8   first;                 /* the slot the last pass began with    */
    const br_net *allow;        /* the allow-list, br_start's           */
    u8   nallow;                /* its networks, 0 for none             */
    char local[NB_NAMELEN];     /* the bridge's name                    */
    char server[NB_NAMELEN];    /* the server's name                    */
    u8   bounce[BR_BOUNCE];     /* the near side of every copy          */
    br_slot s[TS_SLOTS];
} bridge;

static const char k_lost[] = "LOST";

static bridge br_b;
static cmem_blk *br_kept;       /* the block a stuck stop kept, or NULL */
static br_event_fn br_fn;
static void *br_arg;

/* Report event ev of slot k with text to the receiver of br_events. */
static void emit(u8 k, u8 ev, const char *text)
{
    if (br_fn != NULL)
        br_fn(br_arg, k, ev, text);
}

/* Offset of slot k's transmit area in the block; its receive area
   follows it. */
static u16 area(u8 k)
{
    return (u16)(2 * k * NB_MSGMAX);
}

/* The NCB of slot k's command bit. */
static u16 ncb(u8 k, u8 bit)
{
    return (u16)(2 * k + (bit == BB_TX));
}

/* 1 when rc, the final code of a command of a session, shows that the
   provider has removed the session: it reported the session ended, or
   refused a post for an LSN it no longer knows. */
static int gone(u8 rc)
{
    return rc == NB_RC_SESSCLOSED || rc == NB_RC_ABORTED ||
           rc == NB_RC_BADLSN;
}

/* Slot k enters state, a wait, at time now. */
static void become(u8 k, u8 state, u32 now)
{
    br_b.s[k].state = state;
    br_b.s[k].since = now;
}

/* Report event ev of slot k with text, close its connection (nothing
   to a connection no longer TS_OPEN) and let it end from time now; the
   session, if any, is ended by the closing passes. */
static void shut(u8 k, u8 ev, const char *text, u32 now)
{
    emit(k, ev, text);
    ts_close(k);
    become(k, BS_CLOSING, now);
}

/* The client of slot k has closed or reset its connection. */
static void left(u8 k, u32 now)
{
    shut(k, BR_EV_CLOSE, "TCP", now);
}

/* Slot k's CALL failed with rc: the connection is closed without a
   byte. */
static void nocall(u8 k, u8 rc, u32 now)
{
    char text[BR_HEXSZ];

    text[0] = txt_hexdig[rc >> 4];
    text[1] = txt_hexdig[rc & 0x0F];
    text[2] = '\0';
    shut(k, BR_EV_NOCALL, text, now);
}

/* A RECEIVE or SEND of slot k failed with rc at time now: the session
   ends, NETB when the provider reported it closed, else LOST, and the
   slot drains. */
static void ended(u8 k, u8 rc, u32 now)
{
    if (gone(rc))
        br_b.s[k].lsn = 0;
    emit(k, BR_EV_CLOSE, rc == NB_RC_SESSCLOSED ? "NETB" : k_lost);
    become(k, BS_DRAIN, now);
}

/* Record the post of slot k's command bit at time now, which gave rc:
   the command is in progress on NB_OK, left for a later pass on
   NB_RC_TOOMANY, and has failed otherwise.  Returns 1 when it is in
   progress. */
static u16 posted(u8 k, u8 bit, u8 rc, u32 now)
{
    if (rc == NB_OK) {
        br_b.s[k].busy |= bit;
        return 1;
    }
    if (rc == NB_RC_TOOMANY)
        return 0;
    if (bit == BB_CALL)
        nocall(k, rc, now);
    else
        ended(k, rc, now);
    return 0;
}

/* The final code of slot k's command bit, which is then no longer in
   progress; NB_RC_PENDING while it is, or when it is not posted. */
static u8 reap(u8 k, u8 bit)
{
    u8 rc;

    if (!(br_b.s[k].busy & bit))
        return NB_RC_PENDING;
    rc = nba_poll(br_b.set, ncb(k, bit));
    if (rc != NB_RC_PENDING)
        br_b.s[k].busy &= (u8)~bit;
    return rc;
}

/* Zero slot k's transmit area, which no SEND holds, and forget its
   bytes. */
static void drop_tx(u8 k)
{
    cmem_set(br_b.buf, area(k), 0, br_b.s[k].txlen);
    br_b.s[k].txlen = 0;
}

/* Cancel slot k's command bit if it is in progress.  Returns 1 when it
   no longer is, the slot then forgetting a session the command's final
   code shows removed and taking the session of a CALL that completed
   before its cancel took effect; 0 when the provider has not confirmed
   the cancel, or 1 with force, the command being left to nba_close. */
static int cancel(u8 k, u8 bit, int force)
{
    br_slot *t;
    u16 h;
    u8 rc;

    t = &br_b.s[k];
    h = ncb(k, bit);
    if (!(t->busy & bit))
        return 1;
    if (nba_cancel(br_b.set, h) != NB_OK)
        return force;
    t->busy &= (u8)~bit;
    rc = nba_poll(br_b.set, h);
    if (gone(rc))
        t->lsn = 0;
    else if (bit == BB_CALL && rc == NB_OK)
        t->lsn = nba_lsn(br_b.set, h);
    return 1;
}

/* End slot k's session: cancel its commands, hang it up unless the
   provider has removed it, and drop the client's bytes not yet sent.
   Returns 0, having ended nothing for sure, when the provider has not
   confirmed a cancel, unless force; else 1. */
static int quit(u8 k, int force)
{
    br_slot *t;

    t = &br_b.s[k];
    if (!cancel(k, BB_TX, force) || !cancel(k, BB_RX, force) ||
        !cancel(k, BB_CALL, force))
        return 0;
    if (t->lsn != 0) {
        nb_hangup(t->lsn);
        t->lsn = 0;
    }
    if (!(t->busy & BB_TX))
        drop_tx(k);
    return 1;
}

/* Slot k's connection has ended and its NCBs are free: zero its areas
   and listen again. */
static void relisten(u8 k)
{
    cmem_set(br_b.buf, area(k), 0, 2 * NB_MSGMAX);
    memset(&br_b.s[k], 0, sizeof br_b.s[k]);
    ts_listen(k, BR_PORT);
}

/* Report the connection slot k has taken: OPEN when the allow-list is
   empty or names its client's address, else REFUSE.  Returns 1 for
   OPEN. */
static int opened(u8 k)
{
    char text[BR_PEERSZ];
    u8 ip[TS_IPLEN];
    char *p;
    u32 a;
    u16 port;
    u8 i;
    int ok;

    ts_peer(k, ip, &port);
    p = text;
    a = 0;
    for (i = 0; i < TS_IPLEN; i++) {
        a = (a << 8) | ip[i];
        p += txt_u32(p, ip[i]);
        *p++ = (char)(i < TS_IPLEN - 1 ? '.' : ':');
    }
    txt_u32(p, port);
    ok = br_b.nallow == 0;
    for (i = 0; i < br_b.nallow; i++)
        if (((a ^ br_b.allow[i].addr) & br_b.allow[i].mask) == 0)
            ok = 1;
    emit(k, ok ? BR_EV_OPEN : BR_EV_REFUSE, text);
    return ok;
}

/* Post slot k's CALL at time now; returns 1 when it is in progress. */
static u16 call(u8 k, u32 now)
{
    return posted(k, BB_CALL, nba_call(br_b.set, ncb(k, BB_CALL),
                                       br_b.local, br_b.server, 0, 0),
                  now);
}

/* Slot k's CALL completed with rc at time now. */
static void called(u8 k, u8 rc, u32 now)
{
    br_slot *t;

    t = &br_b.s[k];
    if (rc == NB_OK) {
        t->lsn = nba_lsn(br_b.set, ncb(k, BB_CALL));
        t->state = BS_OPEN;
        emit(k, BR_EV_CALL, "");
    } else if (rc == NB_RC_TIMEOUT && !t->retried) {
        t->retried = 1;
        t->since = now;
    } else {
        nocall(k, rc, now);
    }
}

/* Take at most one completed RECEIVE or SEND of slot k at time now;
   returns 1 when one was taken. */
static u16 complete(u8 k, u32 now)
{
    br_slot *t;
    u8 rc;

    t = &br_b.s[k];
    rc = reap(k, BB_RX);
    if (rc == NB_OK || rc == NB_RC_INCOMPLETE) {
        t->rxlen = nba_len(br_b.set, ncb(k, BB_RX));
        t->rxoff = 0;
        t->since = now;
        return 1;
    }
    if (rc != NB_RC_PENDING) {
        ended(k, rc, now);
        return 1;
    }
    rc = reap(k, BB_TX);
    if (rc == NB_RC_PENDING)
        return 0;
    drop_tx(k);
    if (rc != NB_OK)
        ended(k, rc, now);
    return 1;
}

/* Give slot k's client the bytes received and not yet given, as far as
   ts_room allows (ts_write takes all of that); returns 1 when some
   went. */
static u16 deliver(u8 k)
{
    br_slot *t;
    u16 n;
    u16 w;
    u16 done;

    t = &br_b.s[k];
    done = 0;
    while (t->rxoff < t->rxlen) {
        n = (u16)(t->rxlen - t->rxoff);
        if (n > BR_BOUNCE)
            n = BR_BOUNCE;
        w = ts_room(k);
        if (n > w)
            n = w;
        if (n == 0)
            break;
        cmem_get(br_b.buf, (u16)(area(k) + NB_MSGMAX + t->rxoff),
                 br_b.bounce, n);
        ts_write(k, br_b.bounce, n);
        memset(br_b.bounce, 0, (size_t)n);
        t->rxoff = (u16)(t->rxoff + n);
        done = 1;
    }
    return done;
}

/* While slot k has no SEND in progress, read what its client sent into
   the transmit area, up to NB_MSGMAX bytes, and post them as one SEND
   at time now; returns 1 when the SEND is in progress. */
static u16 forward(u8 k, u32 now)
{
    br_slot *t;
    u16 max;
    u16 n;

    t = &br_b.s[k];
    if (t->busy & BB_TX)
        return 0;
    while (t->txlen < NB_MSGMAX) {
        max = (u16)(NB_MSGMAX - t->txlen);
        if (max > BR_BOUNCE)
            max = BR_BOUNCE;
        n = ts_read(k, br_b.bounce, max);
        if (n == 0)
            break;
        cmem_put(br_b.buf, (u16)(area(k) + t->txlen), br_b.bounce, n);
        memset(br_b.bounce, 0, (size_t)n);
        t->txlen = (u16)(t->txlen + n);
    }
    if (t->txlen == 0)
        return 0;
    return posted(k, BB_TX, nba_send(br_b.set, ncb(k, BB_TX), t->lsn,
                                     br_b.buf, area(k), t->txlen), now);
}

/* Post slot k's next RECEIVE at time now once its client has been
   given every byte of the last; returns 1 when it is in progress. */
static u16 receive(u8 k, u32 now)
{
    br_slot *t;

    t = &br_b.s[k];
    if ((t->busy & BB_RX) || t->rxoff < t->rxlen)
        return 0;
    return posted(k, BB_RX, nba_recv(br_b.set, ncb(k, BB_RX), t->lsn,
                                     br_b.buf, (u16)(area(k) + NB_MSGMAX),
                                     NB_MSGMAX), now);
}

/* The passes of slot k in each state, at time now, its socket in state
   ts; each returns the slot's count for br_poll. */
static u16 listening(u8 k, u8 ts, u32 now)
{
    if (ts == TS_OPEN) {
        if (!opened(k)) {
            ts_abort(k);
            ts_listen(k, BR_PORT);
            return 0;
        }
        br_b.s[k].state = BS_CALLING;
        return call(k, now);
    }
    /* A connection that ended before the bridge saw it open. */
    if (ts != TS_LISTEN)
        become(k, BS_CLOSING, now);
    return 0;
}

static u16 calling(u8 k, u8 ts, u32 now)
{
    br_slot *t;
    u8 rc;

    t = &br_b.s[k];
    if (ts != TS_OPEN) {
        left(k, now);
        return 0;
    }
    rc = reap(k, BB_CALL);
    if (rc != NB_RC_PENDING) {
        called(k, rc, now);
        return 1;
    }
    if (t->busy || (t->retried && now - t->since < TMR_SEC))
        return 0;
    return call(k, now);
}

static u16 relaying(u8 k, u8 ts, u32 now)
{
    br_slot *t;
    u16 n;

    t = &br_b.s[k];
    if (ts != TS_OPEN) {
        left(k, now);
        return 0;
    }
    n = complete(k, now);
    if (t->state != BS_OPEN)
        return n;
    if (deliver(k)) {
        t->since = now;
        n++;
    } else if (t->rxoff < t->rxlen && now - t->since >= BR_DELIVERWAIT) {
        shut(k, BR_EV_CLOSE, k_lost, now);
        return n;
    }
    n = (u16)(n + forward(k, now));
    if (t->state == BS_OPEN)
        n = (u16)(n + receive(k, now));
    return n;
}

static u16 draining(u8 k, u8 ts, u32 now)
{
    br_slot *t;
    u16 n;

    t = &br_b.s[k];
    if (!quit(k, 0))
        return 0;
    n = deliver(k);
    if (ts == TS_OPEN && now - t->since < BR_CLOSEWAIT &&
        (t->rxoff < t->rxlen || ts_unsent(k) != 0))
        return n;
    ts_close(k);
    become(k, BS_CLOSING, now);
    return n;
}

static u16 closing(u8 k, u8 ts, u32 now)
{
    if (!quit(k, 0))
        return 0;
    if (ts != TS_CLOSED) {
        if (now - br_b.s[k].since < BR_CLOSEWAIT)
            return 0;
        ts_abort(k);
    }
    relisten(k);
    return 0;
}

/* Run slot k for a pass at time now; returns its count for br_poll. */
static u16 step(u8 k, u32 now)
{
    u8 ts;

    ts = ts_state(k);
    switch (br_b.s[k].state) {
    case BS_LISTEN:
        return listening(k, ts, now);
    case BS_CALLING:
        return calling(k, ts, now);
    case BS_OPEN:
        return relaying(k, ts, now);
    case BS_DRAIN:
        return draining(k, ts, now);
    default:
        return closing(k, ts, now);
    }
}

/* Zero and free block b of the slots' areas, if any. */
static void free_blk(cmem_blk *b)
{
    if (b != NULL) {
        cmem_set(b, 0, 0, BR_BLKSZ);
        cmem_free(b);
    }
}

/* Delete the local name if br_start added it, close the NCBs, then
   free the block unless the provider may still use it; then br_kept
   keeps it.  The bridge is then stopped.  Returns what nba_close
   returned. */
static u16 release(void)
{
    u16 stuck;

    if (br_b.added)
        nb_delete_name(br_b.local);
    stuck = nba_close(br_b.set);
    br_b.set = NULL;
    if (stuck == 0)
        free_blk(br_b.buf);
    else
        br_kept = br_b.buf;
    br_b.buf = NULL;
    return stuck;
}

/* Free the block a stuck stop kept once the provider owns no NCB;
   returns 1 when no block is kept.  The kept test only spares the
   nba_stuck call when there is none. */
static int reclaimed(void)
{
    if (br_kept != NULL && nba_stuck() == 0) {
        free_blk(br_kept);
        br_kept = NULL;
    }
    return br_kept == NULL;
}

int br_start(const char *server, const char *local, const br_net *allow,
             u8 nallow, u8 *nbrc)
{
    u8 rc;
    u8 k;

    *nbrc = NB_OK;
    if (br_b.set != NULL || !reclaimed() || server[0] == '\0' ||
        (local != NULL && local[0] == '\0') || nallow > BR_ALLOWMAX)
        return BR_EARG;
    memset(&br_b, 0, sizeof br_b);
    br_b.allow = allow;
    br_b.nallow = nallow;
    if (nba_open(&br_b.set, 2 * TS_SLOTS) != 0)
        return BR_ENOMEM;
    br_b.buf = cmem_alloc(BR_BLKSZ);
    if (br_b.buf == NULL) {
        release();
        return BR_ENOMEM;
    }
    txt_ucopy(br_b.server, server, NB_NAMELEN);
    rc = nb_local_name(br_b.local, local, "RTCP", &br_b.added);
    if (rc != NB_OK) {
        *nbrc = rc;
        release();
        return rc == NB_RC_NAMEINUSE || rc == NB_RC_DUPNAME ?
               BR_ENAME : BR_ENB;
    }
    for (k = 0; k < TS_SLOTS; k++) {
        if (ts_listen(k, BR_PORT) != TS_OK) {
            while (k > 0)
                ts_abort(--k);
            release();
            return BR_ELISTEN;
        }
    }
    return BR_OK;
}

/* The slot after k, round. */
static u8 next(u8 k)
{
    return (u8)(k + 1 == TS_SLOTS ? 0 : k + 1);
}

u16 br_poll(u32 now)
{
    u16 n;
    u8 i;
    u8 k;

    if (br_b.set == NULL)
        return 0;
    k = br_b.first = next(br_b.first);
    n = 0;
    for (i = 0; i < TS_SLOTS; i++) {
        n = (u16)(n + step(k, now));
        k = next(k);
    }
    return n;
}

u16 br_stop(void)
{
    u8 k;

    if (br_b.set == NULL) {
        reclaimed();
        return 0;
    }
    for (k = 0; k < TS_SLOTS; k++) {
        if (ts_state(k) != TS_FREE)
            ts_abort(k);
        if (br_b.s[k].state == BS_CALLING || br_b.s[k].state == BS_OPEN)
            emit(k, BR_EV_CLOSE, "STOP");
        quit(k, 1);
    }
    return release();
}

u8 br_sessions(void)
{
    u8 n;
    u8 k;

    n = 0;
    for (k = 0; k < TS_SLOTS; k++)
        if (br_b.s[k].lsn != 0)
            n++;
    return n;
}

void br_events(br_event_fn fn, void *arg)
{
    br_fn = fn;
    br_arg = arg;
}

cmem_blk *br_buffers(void)
{
    return br_b.buf;
}

const u8 *br_bounce(void)
{
    return br_b.bounce;
}

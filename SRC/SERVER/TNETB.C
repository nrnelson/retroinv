/*
 * TNETB.C - the server's NetBIOS transport (TNETB.H).
 *
 * Transport slot k drives one CONN slot at a time on two NCBs of the
 * set: 2k for the LISTEN and then the session's RECEIVE, 2k + 1 for
 * its SEND, with its receive buffer at k * CONN_RXSZ in one far block.
 * Each slot keeps two masks of TN_ bits: the commands in progress and
 * the work due.  The callbacks of conn_tops only mark work due; a pass
 * does the work due on a slot, then takes at most one of its completed
 * commands, so whatever a completion makes due (the next RECEIVE or
 * SEND, a LISTEN tried again) is done on the next pass.  Ending a slot
 * (TN_END) comes before anything else: its commands are cancelled, its
 * session hung up and its buffer zeroed, since a RECEIVE that completed
 * after the end was asked for may have left a request there, and only
 * then is the CONN slot given back.  A session the provider has removed
 * is not hung up, as its LSN may already be another session's: one a
 * command reported closed or aborted (the slot's own cancel of its
 * SEND aborts the session, the SEND then reporting NB_RC_CANCELLED and
 * the RECEIVE NB_RC_ABORTED), or one a post was refused for with
 * NB_RC_BADLSN.
 *
 * A LISTEN that fails for any reason but a lack of room keeps its slot,
 * with the LISTEN due again, and every LISTEN rests for TMR_SEC ticks
 * from the failure: a name gone from under the server would otherwise
 * fail a LISTEN on every pass.
 *
 * Sessions are opened with no receive or send timeout: an idle session,
 * or one whose SEND never completes, is ended by CONN's idle timeout
 * (conn_timeouts).
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TNETB.H"

/* Two NCBs per slot fit one set, and every slot's receive buffer one
   far block.  Each pair below declares one array twice, the second time
   with size 2 when the condition fails: the declarations then
   disagree, which every compiler refuses. */
extern char tnetb_ncbs_ok[1];
extern char tnetb_ncbs_ok[(2 * CONN_NETMAX <= NBA_MAX) ? 1 : 2];
extern char tnetb_rxblk_ok[1];
extern char tnetb_rxblk_ok[(CONN_NETMAX * (u32)CONN_RXSZ <= CMEM_MAXBLK) ?
                           1 : 2];

/* Slot states. */
#define TN_FREE         0       /* no CONN slot                         */
#define TN_LISTEN       1       /* a CONN_LISTEN slot                   */
#define TN_OPEN         2       /* a session, lsn                       */

/* Bits of busy and due. */
#define TN_RX           1       /* the LISTEN or RECEIVE                */
#define TN_TX           2       /* the SEND                             */
#define TN_END          4       /* due only: end the slot               */

typedef struct {
    u8   conn;                  /* the CONN slot                        */
    u8   state;                 /* TN_ state                            */
    u8   lsn;                   /* the session, 0 for none              */
    u8   busy;                  /* TN_RX, TN_TX: in progress            */
    u8   due;                   /* TN_RX, TN_TX, TN_END: to do          */
} tn_slot;

typedef struct {
    nba *set;                   /* the NCBs, NULL when not started      */
    cmem_blk *rx;               /* the receive buffers                  */
    u8   n;                     /* slots                                */
    u8   listens;               /* LISTENs posted at once at most       */
    u32  failed;                /* tick of the last LISTEN failure      */
    u8   rest;                  /* 1 while the LISTENs rest             */
    u8   of[CONN_MAX];          /* transport slot of each CONN slot     */
    char name[NB_NAMELEN];      /* the server's name                    */
    tn_slot s[CONN_NETMAX];
} transport;

static transport tnetb_tp;
static cmem_blk *tnetb_kept;    /* the block a stuck stop kept, or NULL */

/* The slot driving CONN slot c. */
static tn_slot *slot_of(u8 c)
{
    return &tnetb_tp.s[tnetb_tp.of[c]];
}

static void tn_kick(u8 c)
{
    slot_of(c)->due |= TN_TX;
}

static void tn_rx_done(u8 c)
{
    slot_of(c)->due |= TN_RX;
}

static void tn_close(u8 c)
{
    slot_of(c)->due |= TN_END;
}

static const conn_tops tnetb_ops = {
    tn_kick, tn_rx_done, tn_close, "NETB", 0
};

/* 1 when rc says the provider has no room for now: try again later. */
static int later(u8 rc)
{
    return rc == NB_RC_TOOMANY || rc == NB_RC_SESSFULL;
}

/* A LISTEN failed at time now: the LISTENs rest. */
static void listen_failed(u32 now)
{
    tnetb_tp.rest = 1;
    tnetb_tp.failed = now;
}

/* 1 while the LISTENs rest at time now; a rest of TMR_SEC ticks or
   more ends here. */
static int resting(u32 now)
{
    if (tnetb_tp.rest && now - tnetb_tp.failed >= TMR_SEC)
        tnetb_tp.rest = 0;
    return tnetb_tp.rest;
}

/* 1 when rc, the final code of a command of a session, shows that the
   provider has removed the session: it reported the session ended, or
   refused a post for an LSN it no longer knows. */
static int gone(u8 rc)
{
    return rc == NB_RC_SESSCLOSED || rc == NB_RC_ABORTED ||
           rc == NB_RC_BADLSN;
}

/* After a cancel of slot t's command on NCB h: the slot forgets its
   session when the command's final code shows the provider has removed
   it. */
static void forget_gone(tn_slot *t, u16 h)
{
    if (gone(nba_poll(tnetb_tp.set, h)))
        t->lsn = 0;
}

/* A command of slot t's session failed with rc: the slot is to end,
   and forgets a session the provider has removed. */
static void failed(tn_slot *t, u8 rc)
{
    if (gone(rc))
        t->lsn = 0;
    t->due |= TN_END;
}

/* Record the post of command bit on t at time now, which gave rc: the
   command is in progress on NB_OK; it is left due for a later pass when
   later(rc); otherwise a LISTEN rests and any other command fails.
   Returns 1 when the command is in progress. */
static int posted(tn_slot *t, u8 bit, u8 rc, u32 now)
{
    if (rc == NB_OK) {
        t->due &= (u8)~bit;
        t->busy |= bit;
        return 1;
    }
    if (later(rc))
        return 0;
    if (t->state == TN_LISTEN)
        listen_failed(now);
    else
        failed(t, rc);
    return 0;
}

/* End slot k: cancel its commands, hang up its session unless the
   provider has removed it, zero its buffer and give its CONN slot back.
   A cancel the provider has not confirmed leaves the slot to be ended
   on a later pass (returns 0), unless force, when the command is left
   to nba_close and the buffer of a RECEIVE so left to the provider is
   not zeroed: the block is zeroed only when it is freed, once the
   provider owns no NCB (release, or reclaimed), as until then it may
   still write into it.  Returns 1 when the slot was ended. */
static int end_slot(u8 k, int force)
{
    tn_slot *t;
    u16 h;
    int kept;

    t = &tnetb_tp.s[k];
    h = (u16)(2 * k);
    if (t->busy & TN_TX) {
        if (nba_cancel(tnetb_tp.set, (u16)(h + 1)) != NB_OK) {
            if (!force)
                return 0;
        } else {
            forget_gone(t, (u16)(h + 1));
        }
        t->busy &= (u8)~TN_TX;
    }
    kept = 0;
    if (t->busy & TN_RX) {
        if (nba_cancel(tnetb_tp.set, h) != NB_OK) {
            if (!force)
                return 0;
            kept = 1;
        } else if (t->state == TN_LISTEN) {
            /* A LISTEN that completed before its cancel took effect
               opened a session, which is hung up too. */
            if (nba_poll(tnetb_tp.set, h) == NB_OK)
                t->lsn = nba_lsn(tnetb_tp.set, h);
        } else {
            forget_gone(t, h);
        }
    }
    if (t->lsn != 0)
        nb_hangup(t->lsn);
    if (!kept)
        cmem_set(tnetb_tp.rx, (u16)(k * CONN_RXSZ), 0, CONN_RXSZ);
    conn_closed(t->conn);
    memset(t, 0, sizeof *t);
    return 1;
}

/* Post the commands due on slot k at time now, its LISTEN only while
   the LISTENs do not rest; returns the number posted. */
static int post(u8 k, u32 now)
{
    tn_slot *t;
    cmem_blk *b;
    u16 h, len, off;
    int n;
    u8 ready;

    t = &tnetb_tp.s[k];
    h = (u16)(2 * k);
    n = 0;
    ready = (u8)(t->due & ~t->busy);
    if ((ready & TN_RX) && t->state == TN_LISTEN && !resting(now))
        n = posted(t, TN_RX, nba_listen(tnetb_tp.set, h, tnetb_tp.name,
                                        "*", 0, 0), now);
    else if ((ready & TN_RX) && t->state == TN_OPEN)
        n = posted(t, TN_RX, nba_recv(tnetb_tp.set, h, t->lsn, tnetb_tp.rx,
                                      (u16)(k * CONN_RXSZ), CONN_RXSZ),
                   now);
    if (ready & TN_TX) {
        len = conn_out_peek(t->conn, &b, &off);
        if (len == 0) {
            t->due &= (u8)~TN_TX;
        } else {
            if (len > NB_MSGMAX)
                len = NB_MSGMAX;
            n += posted(t, TN_TX, nba_send(tnetb_tp.set, (u16)(h + 1),
                                           t->lsn, b, off, len), now);
        }
    }
    return n;
}

/* The LISTEN of slot t on NCB h completed with rc at time now. */
static void heard(tn_slot *t, u16 h, u8 rc, u32 now)
{
    char station[NB_NAMELEN];

    if (rc != NB_OK) {
        t->due |= TN_RX;
        if (!later(rc))
            listen_failed(now);
        return;
    }
    t->lsn = nba_lsn(tnetb_tp.set, h);
    t->state = TN_OPEN;
    t->due |= TN_RX;
    nba_callname(tnetb_tp.set, h, station);
    conn_open(t->conn, now, station);
}

/* The RECEIVE of slot k (t) on NCB h completed with rc. */
static void received(u8 k, tn_slot *t, u16 h, u8 rc)
{
    u16 len;

    if (rc != NB_OK && rc != NB_RC_INCOMPLETE) {
        failed(t, rc);
        return;
    }
    len = nba_len(tnetb_tp.set, h);
    if (len == 0)
        t->due |= TN_RX;
    else
        conn_input(t->conn, tnetb_tp.rx, (u16)(k * CONN_RXSZ), len);
}

/* Take at most one completed command of slot k at time now; returns 1
   when one was taken. */
static int complete(u8 k, u32 now)
{
    tn_slot *t;
    u16 h;
    u8 rc;

    t = &tnetb_tp.s[k];
    h = (u16)(2 * k);
    if (t->busy & TN_RX) {
        rc = nba_poll(tnetb_tp.set, h);
        if (rc != NB_RC_PENDING) {
            t->busy &= (u8)~TN_RX;
            if (t->state == TN_LISTEN)
                heard(t, h, rc, now);
            else
                received(k, t, h, rc);
            return 1;
        }
    }
    if (!(t->busy & TN_TX))
        return 0;
    rc = nba_poll(tnetb_tp.set, (u16)(h + 1));
    if (rc == NB_RC_PENDING)
        return 0;
    t->busy &= (u8)~TN_TX;
    if (rc != NB_OK) {
        failed(t, rc);
    } else {
        t->due |= TN_TX;
        conn_out_done(t->conn, now, nba_len(tnetb_tp.set, (u16)(h + 1)));
    }
    return 1;
}

/* Claim CONN slots for the LISTENs missing, as far as slots are free. */
static void claim(void)
{
    tn_slot *t;
    u8 c, k, listening;

    listening = 0;
    for (k = 0; k < tnetb_tp.n; k++)
        if (tnetb_tp.s[k].state == TN_LISTEN)
            listening++;
    for (k = 0; k < tnetb_tp.n && listening < tnetb_tp.listens; k++) {
        t = &tnetb_tp.s[k];
        if (t->state != TN_FREE)
            continue;
        if (!conn_claim(&tnetb_ops, &c))
            return;
        tnetb_tp.of[c] = k;
        t->conn = c;
        t->state = TN_LISTEN;
        t->due = TN_RX;
        listening++;
    }
}

/* Zero and free block b of receive buffers, if any. */
static void free_rx(cmem_blk *b)
{
    if (b != NULL) {
        cmem_set(b, 0, 0, cmem_size(b));
        cmem_free(b);
    }
}

/* Close the NCBs, then free the receive buffers unless the provider may
   still write into them; then tnetb_kept keeps them.  Returns what
   nba_close returned. */
static u16 release(void)
{
    u16 stuck;

    stuck = nba_close(tnetb_tp.set);
    tnetb_tp.set = NULL;
    if (stuck == 0)
        free_rx(tnetb_tp.rx);
    else
        tnetb_kept = tnetb_tp.rx;
    tnetb_tp.rx = NULL;
    return stuck;
}

/* Free the block a stuck stop kept once the provider owns no NCB;
   returns 1 when no block is kept.  The kept test only spares the
   nba_stuck call when there is none. */
static int reclaimed(void)
{
    if (tnetb_kept != NULL && nba_stuck() == 0) {
        free_rx(tnetb_kept);
        tnetb_kept = NULL;
    }
    return tnetb_kept == NULL;
}

int tnetb_start(const char *name, u8 sessions, u8 listens, u8 *nbrc)
{
    u8 num;

    *nbrc = NB_OK;
    if (tnetb_tp.set != NULL || !reclaimed() || name[0] == '\0' ||
        strlen(name) >= (size_t)NB_NAMELEN ||
        sessions < 1 || sessions > CONN_NETMAX ||
        listens < 1 || listens > sessions)
        return TNETB_EARG;
    if (nba_open(&tnetb_tp.set, (u16)(2 * sessions)) != 0)
        return TNETB_ENOMEM;
    tnetb_tp.rx = cmem_alloc((u16)(sessions * CONN_RXSZ));
    if (tnetb_tp.rx == NULL) {
        release();
        return TNETB_ENOMEM;
    }
    *nbrc = nb_add_name(name, &num);
    if (*nbrc != NB_OK) {
        release();
        if (*nbrc == NB_RC_NOT_PRESENT)
            return TNETB_ENONB;
        if (*nbrc == NB_RC_DUPNAME || *nbrc == NB_RC_NAMEINUSE)
            return TNETB_ENAME;
        return TNETB_ENB;
    }
    txt_copy(tnetb_tp.name, name, (u16)sizeof tnetb_tp.name);
    memset(tnetb_tp.s, 0, sizeof tnetb_tp.s);
    tnetb_tp.rest = 0;
    tnetb_tp.n = sessions;
    tnetb_tp.listens = listens;
    return TNETB_OK;
}

u16 tnetb_poll(u32 now)
{
    u16 n;
    u8 k;

    if (tnetb_tp.set == NULL)
        return 0;
    claim();
    n = 0;
    for (k = 0; k < tnetb_tp.n; k++) {
        if (tnetb_tp.s[k].due & TN_END)
            n = (u16)(n + end_slot(k, 0));
        else
            n = (u16)(n + post(k, now) + complete(k, now));
    }
    return n;
}

u16 tnetb_stop(void)
{
    u8 k;

    if (tnetb_tp.set == NULL) {
        reclaimed();
        return 0;
    }
    for (k = 0; k < tnetb_tp.n; k++)
        if (tnetb_tp.s[k].state != TN_FREE)
            end_slot(k, 1);
    nb_delete_name(tnetb_tp.name);
    return release();
}

u16 tnetb_listening(void)
{
    u16 n;
    u8 k;

    n = 0;
    for (k = 0; k < tnetb_tp.n; k++)
        if (tnetb_tp.s[k].state == TN_LISTEN &&
            (tnetb_tp.s[k].busy & TN_RX))
            n++;
    return n;
}

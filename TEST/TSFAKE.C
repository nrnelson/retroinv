/*
 * TSFAKE.C - a simulated TCP/IP stack for the tests (TSFAKE.H).
 *
 * Each slot holds both what the program sees of it and the connection
 * it took last, which the peer's calls name.  The two directions of a
 * connection are rings of TSF_QMAX bytes, kept in one far block of
 * FS_BLK bytes for all the slots so that the near data of the tests
 * does not grow by them: ts_init allocates the block, failing with
 * TS_EINIT, as a stack without memory refuses to start, when far memory
 * has run out, and ts_term and tsf_reset free it.
 *
 * Bytes are acknowledged and read in the order they were written, so
 * the bytes written that hold their room are the more of those still
 * in the ring and those not acknowledged.  ts_poll always returns at
 * once: the fake has no ARP cache to fill, so it never stalls as the
 * stack's may.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "TSFAKE.H"

#define FS_IN           0       /* ring of the bytes the peer sent      */
#define FS_OUT          1       /* ring of the bytes the program wrote  */
#define FS_BLK          (2u * TS_SLOTS * TSF_QMAX)  /* every ring       */

/* The layer's phases. */
#define FS_OFF          0       /* ts_init not called                   */
#define FS_ON           1       /* started                              */
#define FS_DONE         2       /* ts_init failed, or ts_term ran       */

/* A connection as its peer sees it. */
#define FS_NONE         0       /* never opened, or gone (TSF_GONE)     */
#define FS_UP           1       /* open                                 */
#define FS_SHUT         2       /* closed by the program                */

extern char tsfake_blk_check[1];
extern char tsfake_blk_check[(FS_BLK <= CMEM_MAXBLK) ? 1 : 2];

typedef struct {
    u16 head;                   /* offset of the oldest byte            */
    u16 len;                    /* bytes waiting                        */
} fs_ring;

typedef struct {
    fs_ring q[2];               /* FS_IN and FS_OUT                     */
    u16 port;                   /* the port it listens on               */
    u16 pport;                  /* the peer's port                      */
    u16 unacked;                /* bytes written and not acknowledged   */
    u16 cap;                    /* tsf_room's bytes left, or TSF_NOCAP  */
    u8 state;                   /* TS_ state                            */
    u8 conn;                    /* FS_NONE, FS_UP or FS_SHUT            */
    u8 shown;                   /* ts_peer gives the peer               */
    u8 noack;
    u8 nofinack;
} fs_slot;

static fs_slot fs_slots[TS_SLOTS];
static cmem_blk *fs_blk;        /* the rings; NULL unless FS_ON         */
static u8 fs_phase;             /* FS_OFF, FS_ON or FS_DONE             */
static int fs_initrc;           /* tsf_init_rc                          */
static u16 fs_taken;            /* connections taken since tsf_reset    */
static u16 fs_nrefused;
static u16 fs_nterms;
static tsf_pump_fn fs_pumpfn;
static void *fs_pumparg;
static u8 fs_inpump;
static const u8 *fs_lastp;      /* tsf_lastbuf                          */
static u16 fs_lastn;

/* Offset in fs_blk of ring d of slot s. */
static u16 base(u8 s, int d)
{
    return (u16)((2 * s + d) * TSF_QMAX);
}

/* Append n bytes of src to ring d of slot s, which has room for them. */
static void put(u8 s, int d, const u8 *src, u16 n)
{
    fs_ring *r;
    u16 at;
    u16 k;

    r = &fs_slots[s].q[d];
    at = (u16)(r->head + r->len);
    if (at >= TSF_QMAX)
        at = (u16)(at - TSF_QMAX);
    r->len = (u16)(r->len + n);
    while (n > 0) {
        k = (u16)(TSF_QMAX - at);
        if (k > n)
            k = n;
        cmem_put(fs_blk, (u16)(base(s, d) + at), src, k);
        src += k;
        n = (u16)(n - k);
        at = 0;
    }
}

/* Take up to n of the oldest bytes of ring d of slot s into dst: the
   count taken. */
static u16 take(u8 s, int d, u8 *dst, u16 n)
{
    fs_ring *r;
    u16 got;
    u16 k;

    r = &fs_slots[s].q[d];
    if (n > r->len)
        n = r->len;
    got = n;
    while (n > 0) {
        k = (u16)(TSF_QMAX - r->head);
        if (k > n)
            k = n;
        cmem_get(fs_blk, (u16)(base(s, d) + r->head), dst, k);
        dst += k;
        r->head = (u16)(r->head + k);
        if (r->head == TSF_QMAX)
            r->head = 0;
        r->len = (u16)(r->len - k);
        n = (u16)(n - k);
    }
    return got;
}

/* End the connection of slot s for its peer, every byte held either way
   dropped. */
static void drop(u8 s)
{
    fs_slots[s].q[FS_IN].len = 0;
    fs_slots[s].q[FS_OUT].len = 0;
    fs_slots[s].unacked = 0;
    fs_slots[s].conn = FS_NONE;
}

/* 1 when slot is TS_OPEN or TS_CLOSING: the stack holds its
   connection. */
static int held(u8 slot)
{
    u8 st;

    st = ts_state(slot);
    return st == TS_OPEN || st == TS_CLOSING;
}

/* Drop every connection, set every slot TS_FREE and free the rings. */
static void stop(void)
{
    u8 s;

    for (s = 0; s < TS_SLOTS; s++) {
        drop(s);
        fs_slots[s].state = TS_FREE;
    }
    cmem_free(fs_blk);
    fs_blk = NULL;
}

/* ---------------------------------------------------------------------
 * The layer, as TSOCK.H gives it.
 * --------------------------------------------------------------------- */

int ts_init(void)
{
    int rc;

    if (fs_phase != FS_OFF)
        return TS_EARG;
    rc = fs_initrc;
    if (rc == TS_OK) {
        fs_blk = cmem_alloc(FS_BLK);
        if (fs_blk == NULL)
            rc = TS_EINIT;
    }
    fs_phase = rc == TS_OK ? FS_ON : FS_DONE;
    return rc;
}

void ts_term(void)
{
    if (fs_phase != FS_ON)
        return;
    stop();
    fs_phase = FS_DONE;
    fs_nterms++;
}

int ts_listen(u8 slot, u16 port)
{
    fs_slot *c;

    if (fs_phase != FS_ON || slot >= TS_SLOTS || port == 0)
        return TS_EARG;
    c = &fs_slots[slot];
    if (c->state != TS_FREE && c->state != TS_CLOSED)
        return TS_EARG;
    c->state = TS_LISTEN;
    c->port = port;
    c->shown = 0;
    return TS_OK;
}

void ts_poll(void)
{
    fs_slot *c;

    if (fs_phase != FS_ON)
        return;
    for (c = fs_slots; c < fs_slots + TS_SLOTS; c++) {
        if (!c->noack)
            c->unacked = 0;
        if (c->state == TS_CLOSING &&
            (c->conn == FS_NONE || (!c->nofinack && !c->noack)))
            c->state = TS_CLOSED;
    }
    if (fs_pumpfn != NULL && !fs_inpump) {
        fs_inpump = 1;
        fs_pumpfn(fs_pumparg);
        fs_inpump = 0;
    }
}

u8 ts_state(u8 slot)
{
    return slot < TS_SLOTS ? fs_slots[slot].state : (u8)TS_FREE;
}

u16 ts_read(u8 slot, void *buf, u16 max)
{
    u16 n;

    if (ts_state(slot) != TS_OPEN)
        return 0;
    n = take(slot, FS_IN, (u8 *)buf, max);
    if (n > 0) {
        fs_lastp = (const u8 *)buf;
        fs_lastn = n;
    }
    return n;
}

u16 ts_room(u8 slot)
{
    const fs_slot *c;
    u16 n;

    if (ts_state(slot) != TS_OPEN)
        return 0;
    c = &fs_slots[slot];
    n = c->q[FS_OUT].len > c->unacked ? c->q[FS_OUT].len : c->unacked;
    n = (u16)(TSF_QMAX - n);
    return n < c->cap ? n : c->cap;
}

u16 ts_write(u8 slot, const void *buf, u16 len)
{
    fs_slot *c;
    u16 n;

    n = ts_room(slot);
    if (len < n)
        n = len;
    if (n == 0)
        return 0;
    c = &fs_slots[slot];
    put(slot, FS_OUT, (const u8 *)buf, n);
    c->unacked = (u16)(c->unacked + n);
    if (c->cap != TSF_NOCAP)
        c->cap = (u16)(c->cap - n);
    return n;
}

u16 ts_unsent(u8 slot)
{
    return held(slot) ? fs_slots[slot].unacked : 0;
}

void ts_close(u8 slot)
{
    if (ts_state(slot) != TS_OPEN)
        return;
    fs_slots[slot].state = TS_CLOSING;
    fs_slots[slot].conn = FS_SHUT;
}

void ts_abort(u8 slot)
{
    if (ts_state(slot) == TS_FREE)
        return;
    if (held(slot))
        drop(slot);
    fs_slots[slot].state = TS_CLOSED;
}

void ts_peer(u8 slot, u8 *ip, u16 *port)
{
    int i;

    if (ts_state(slot) != TS_FREE && fs_slots[slot].shown) {
        for (i = 0; i < TS_IPLEN; i++)
            ip[i] = (u8)(TSF_PEERIP >> (8 * (TS_IPLEN - 1 - i)));
        *port = fs_slots[slot].pport;
    } else {
        memset(ip, 0, TS_IPLEN);
        *port = 0;
    }
}

/* ---------------------------------------------------------------------
 * The peers and the test's controls.
 * --------------------------------------------------------------------- */

void tsf_reset(void)
{
    stop();
    fs_phase = FS_OFF;
    fs_initrc = TS_OK;
    fs_taken = 0;
    fs_nrefused = 0;
    fs_nterms = 0;
    fs_pumpfn = NULL;
    fs_pumparg = NULL;
    fs_lastp = NULL;
    fs_lastn = 0;
}

void tsf_init_rc(int rc)
{
    fs_initrc = rc;
}

int tsf_connect(u16 port, u8 *slot)
{
    fs_slot *c;
    u8 s;

    for (s = 0; s < TS_SLOTS; s++)
        if (ts_state(s) == TS_LISTEN && fs_slots[s].port == port)
            break;
    if (s == TS_SLOTS) {
        fs_nrefused++;
        return TSF_REFUSED;
    }
    drop(s);
    c = &fs_slots[s];
    c->state = TS_OPEN;
    c->conn = FS_UP;
    c->pport = (u16)(TSF_PORT0 + fs_taken++);
    c->shown = 1;
    c->cap = TSF_NOCAP;
    c->noack = 0;
    c->nofinack = 0;
    *slot = s;
    return TSF_OK;
}

u16 tsf_send(u8 slot, const void *buf, u16 len)
{
    u16 n;

    if (ts_state(slot) != TS_OPEN)
        return 0;
    n = (u16)(TSF_QMAX - fs_slots[slot].q[FS_IN].len);
    if (len < n)
        n = len;
    put(slot, FS_IN, (const u8 *)buf, n);
    return n;
}

int tsf_recv(u8 slot, void *buf, u16 size, u16 *n)
{
    const fs_slot *c;

    *n = 0;
    if (slot >= TS_SLOTS)
        return TSF_GONE;
    c = &fs_slots[slot];
    if (c->conn == FS_NONE)
        return TSF_GONE;
    if (c->conn == FS_SHUT && c->q[FS_OUT].len == 0)
        return TSF_EOF;
    *n = take(slot, FS_OUT, (u8 *)buf, size);
    return TSF_UP;
}

void tsf_fin(u8 slot)
{
    if (!held(slot))
        return;
    drop(slot);
    fs_slots[slot].state = TS_CLOSING;
}

void tsf_rst(u8 slot)
{
    if (!held(slot))
        return;
    drop(slot);
    fs_slots[slot].state = TS_CLOSED;
}

void tsf_room(u8 slot, u16 n)
{
    if (slot < TS_SLOTS)
        fs_slots[slot].cap = n;
}

void tsf_noack(u8 slot)
{
    if (slot < TS_SLOTS)
        fs_slots[slot].noack = 1;
}

void tsf_nofinack(u8 slot)
{
    if (slot < TS_SLOTS)
        fs_slots[slot].nofinack = 1;
}

void tsf_pump(tsf_pump_fn fn, void *arg)
{
    fs_pumpfn = fn;
    fs_pumparg = arg;
}

u16 tsf_refused(void)
{
    return fs_nrefused;
}

u16 tsf_terms(void)
{
    return fs_nterms;
}

const u8 *tsf_lastbuf(u16 *len)
{
    *len = fs_lastn;
    return fs_lastp;
}

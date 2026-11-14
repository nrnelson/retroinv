/*
 * FAKEUART.C - simulated serial ports for the tests (FAKEUART.H).
 *
 * Every queue is a ring: a far block, the offset of its first byte, its
 * size, and a head (the next byte to take) and tail (the next byte to
 * fill) that never meet unless it is empty, so a ring of size bytes
 * holds size - 1.  An open port's receive and transmit rings share the
 * block uart_open takes; the test's two queues of a port share one
 * block from fu_port to its removal, so that what a port sent before it
 * closed stays for the test to read.  A caller's block is seen as a
 * ring of its n bytes at off, filled or emptied once.  Bytes move
 * between rings in contiguous runs with cmem_copy, or cmem_move inside
 * one block, never through near memory, and every byte that leaves one
 * of the fake's own rings is zeroed there.
 *
 * A port that met CTS down is stalled, as CUART.C's handler is once it
 * has turned its transmitter off, and sends again only after CTS rises
 * (the modem-status interrupt) or a uart_write.
 */
#define COMPAT_UART
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "FAKEUART.H"

#define FU_QSZ          (FU_PEERQ + 1)  /* ring of a test's queue       */

/* Two ports: a port's only partner is the other one. */
extern char fakeuart_two_ports[1];
extern char fakeuart_two_ports[(UART_MAXPORT == 2) ? 1 : 2];

extern char fakeuart_blk_check[1];
extern char fakeuart_blk_check[(UART_RXSZ + UART_TXSZ <= CMEM_MAXBLK &&
                                2 * (u32)FU_QSZ <= CMEM_MAXBLK) ? 1 : 2];

typedef struct {
    cmem_blk *b;
    u16 base;                   /* offset of the ring in b              */
    u16 size;
    u16 head;                   /* next byte to take                    */
    u16 tail;                   /* next byte to fill                    */
    u8 wipe;                    /* 1: zero each byte taken out          */
} fu_ring;

typedef struct {
    fu_ring rx;                 /* while open: the receive ring         */
    fu_ring tx;                 /* and the transmit ring                */
    fu_ring in;                 /* the test's bytes towards the port    */
    fu_ring out;                /* the port's bytes for the test        */
    uart_info info;             /* while open: base, IRQ, chip, counts  */
    u16 rate;                   /* bytes per step each way, 0 no limit  */
    u16 obaud;                  /* the last uart_open's rate, or 0      */
    u16 nfault;                 /* bytes still to mark with fault       */
    u8 chip;                    /* UART_T_, 0 when the port is absent   */
    u8 open;
    u8 flags;                   /* uart_open's                          */
    u8 oflags;                  /* the last uart_open's flags           */
    u8 peerlines;               /* the test's DSR and DCD (UART_M_)     */
    u8 rts;                     /* RTS set in the port's MCR            */
    u8 peerrts;                 /* the test's RTS                       */
    u8 pair;                    /* the paired port, 0 for the test      */
    u8 stalled;                 /* met CTS down, waits for it to rise   */
    u8 held;                    /* the receive ring filled: nothing
                                   arrives until it is drained to
                                   UART_RXLOW                           */
    u8 fault;                   /* FU_ kind of the marked bytes         */
} fu_pt;

static fu_pt fu_ports[UART_MAXPORT];
static fu_pump_fn fu_pumpfn;
static void *fu_pumparg;
static u8 fu_inpump;

static const u16 fu_bases[UART_MAXPORT] = { 0x3F8, 0x2F8 };
static const u8 fu_irqs[UART_MAXPORT] = { 4, 3 };

static u16 ring_count(const fu_ring *r)
{
    return (u16)((r->tail + r->size - r->head) % r->size);
}

static u16 ring_room(const fu_ring *r)
{
    return (u16)(r->size - 1 - ring_count(r));
}

/* Set r empty over size bytes at base of b; wipe as in fu_ring. */
static void ring_init(fu_ring *r, cmem_blk *b, u16 base, u16 size,
                      u8 wipe)
{
    r->b = b;
    r->base = base;
    r->size = size;
    r->head = 0;
    r->tail = 0;
    r->wipe = wipe;
}

/* Take n bytes (at most ring_count(s)) from s, dropping them. */
static void ring_skip(fu_ring *s, u16 n)
{
    u16 k;

    if (s->wipe) {
        k = (u16)(s->size - s->head);
        if (k > n)
            k = n;
        cmem_set(s->b, (u16)(s->base + s->head), 0, k);
        cmem_set(s->b, s->base, 0, (u16)(n - k));
    }
    s->head = (u16)((s->head + (u32)n) % s->size);
}

/* Move n bytes from s to d, n at most what s holds and d has room
   for; either may be a caller's block seen as a ring. */
static void ring_xfer(fu_ring *d, fu_ring *s, u16 n)
{
    u16 k;

    while (n > 0) {
        k = (u16)(s->size - s->head);
        if (k > d->size - d->tail)
            k = (u16)(d->size - d->tail);
        if (k > n)
            k = n;
        if (d->b == s->b)
            cmem_move(d->b, (u16)(d->base + d->tail),
                      (u16)(s->base + s->head), k);
        else
            cmem_copy(d->b, (u16)(d->base + d->tail), s->b,
                      (u16)(s->base + s->head), k);
        ring_skip(s, k);
        d->tail = (u16)((d->tail + (u32)k) % d->size);
        n = (u16)(n - k);
    }
}

/* The open port numbered port, or NULL. */
static fu_pt *open_port(u8 port)
{
    if (port < 1 || port > UART_MAXPORT || !fu_ports[port - 1].open)
        return NULL;
    return &fu_ports[port - 1];
}

/* The port numbered port, which fu_port must have made; anything else
   is a fault of the test, which ends it. */
static fu_pt *made_port(u8 port)
{
    if (port < 1 || port > UART_MAXPORT || fu_ports[port - 1].chip == 0)
        abort();
    return &fu_ports[port - 1];
}

/* 1 when p is open without loopback, so that its line carries bytes
   to it and its RTS. */
static int on_line(const fu_pt *p)
{
    return p->open && !(p->flags & UART_F_LOOP);
}

/* 1 when the CTS of p is up. */
static int cts(const fu_pt *p)
{
    const fu_pt *f;

    if (p->flags & UART_F_LOOP)
        return p->rts;
    if (p->pair == 0)
        return p->peerrts;
    f = &fu_ports[p->pair - 1];
    return on_line(f) && f->rts;
}

/* The most bytes p may take in now: none while its receiver is held,
   else as many as its receive ring has room for or, under
   UART_F_RTSCTS, as bring it to UART_RXHIGH, where its RTS drops, so
   that a sender watching it as CTS stops in time. */
static u16 below_high(const fu_pt *p)
{
    u16 n;

    if (p->held)
        return 0;
    n = ring_count(&p->rx);
    if (!(p->flags & UART_F_RTSCTS))
        return ring_room(&p->rx);
    return (u16)(n < UART_RXHIGH ? UART_RXHIGH - n : 0);
}

/* n bytes of s (at most below_high(p)) arrive at the open port p: the
   marked ones as their fault says, the rest into its receive ring; a
   ring left full holds the receiver. */
static void arrive(fu_pt *p, fu_ring *s, u16 n)
{
    while (n > 0 && p->nfault > 0) {
        p->nfault--;
        n--;
        switch (p->fault) {
        case FU_OVERRUN:
            p->info.overrun++;
            ring_skip(s, 1);
            continue;
        case FU_LOST:
            p->info.lost++;
            ring_skip(s, 1);
            continue;
        case FU_FRAMING:
            p->info.framing++;
            break;
        case FU_PARITY:
            p->info.parity++;
            break;
        default:
            break;
        }
        ring_xfer(&p->rx, s, 1);
    }
    ring_xfer(&p->rx, s, n);
    if (ring_room(&p->rx) == 0)
        p->held = 1;
    if ((p->flags & UART_F_RTSCTS) && ring_count(&p->rx) >= UART_RXHIGH)
        p->rts = 0;
}

/* Send up to max bytes of p's transmit ring down its line, unless it
   is stalled; it stalls on meeting CTS down. */
static void send_out(fu_pt *p, u16 max)
{
    fu_pt *f;
    u16 k;

    if (p->stalled)
        return;
    if (p->flags & UART_F_LOOP)
        f = p;
    else if (p->pair != 0)
        f = &fu_ports[p->pair - 1];
    else
        f = NULL;
    while (max > 0 && ring_count(&p->tx) > 0) {
        if ((p->flags & UART_F_RTSCTS) && !cts(p)) {
            p->stalled = 1;
            break;
        }
        k = ring_count(&p->tx);
        if (k > max)
            k = max;
        if (f == NULL) {
            if (k > ring_room(&p->out))
                k = ring_room(&p->out);
            if (k == 0)
                break;
            ring_xfer(&p->out, &p->tx, k);
        } else if (f == p || on_line(f)) {
            if (k > below_high(f))
                k = below_high(f);
            if (k == 0)
                break;
            arrive(f, &p->tx, k);
        } else {
            ring_skip(&p->tx, k);
        }
        max = (u16)(max - k);
    }
}

/* Bring up to max bytes of the test's queue to p, unless p's line
   leads to another port, while its RTS is up on the line. */
static void take_in(fu_pt *p, u16 max)
{
    u16 k;

    if (p->pair != 0)
        return;
    while (max > 0 && ring_count(&p->in) > 0 && on_line(p) && p->rts) {
        k = ring_count(&p->in);
        if (k > max)
            k = max;
        if (k > below_high(p))
            k = below_high(p);
        if (k == 0)
            break;
        arrive(p, &p->in, k);
        max = (u16)(max - k);
    }
}

/* One step of the line, port by port, in a call on port (0 for the
   test's calls): a port in loopback has no interrupts and moves its
   bytes only in its own calls, which poll it. */
static void step(u8 port)
{
    fu_pt *p;
    u16 max;
    int i;

    for (i = 0; i < UART_MAXPORT; i++) {
        p = &fu_ports[i];
        if (p->chip == 0)
            continue;
        max = p->rate != 0 ? p->rate : 0xFFFFu;
        if (p->open && (!(p->flags & UART_F_LOOP) || i + 1 == port))
            send_out(p, max);
        take_in(p, max);
    }
}

/* Run the pump, unless it is running already. */
static void pump(void)
{
    if (fu_pumpfn != NULL && !fu_inpump) {
        fu_inpump = 1;
        fu_pumpfn(fu_pumparg);
        fu_inpump = 0;
    }
}

/* The pump, then a step, in a call on port. */
static void poll_line(u8 port)
{
    pump();
    step(port);
}

/* Raise p's RTS; when it was down, the port whose CTS it is sees CTS
   rise. */
static void rts_up(fu_pt *p)
{
    if (p->rts)
        return;
    p->rts = 1;
    if (p->flags & UART_F_LOOP)
        p->stalled = 0;
    else if (p->pair != 0)
        fu_ports[p->pair - 1].stalled = 0;
}

/* After a read or flush: once UART_RXLOW bytes or fewer are left, the
   receiver no longer held, and RTS up again. */
static void rx_check(fu_pt *p)
{
    if (ring_count(&p->rx) > UART_RXLOW)
        return;
    p->held = 0;
    if (p->flags & UART_F_RTSCTS)
        rts_up(p);
}

int uart_open(u8 port, u16 baud, u8 flags)
{
    fu_pt *p;
    cmem_blk *b;

    if (port < 1 || port > UART_MAXPORT || !UART_BAUDOK(baud) ||
        (flags & ~(UART_F_RTSCTS | UART_F_LOOP | UART_F_NOFIFO)) != 0)
        return UART_EARG;
    p = &fu_ports[port - 1];
    if (p->chip == 0)
        return UART_ENOPORT;
    if (p->open)
        return UART_EBUSY;
    b = cmem_alloc(UART_RXSZ + UART_TXSZ);
    if (b == NULL)
        return UART_ENOMEM;
    cmem_set(b, 0, 0, UART_RXSZ + UART_TXSZ);
    ring_init(&p->rx, b, 0, UART_RXSZ, 1);
    ring_init(&p->tx, b, UART_RXSZ, UART_TXSZ, 1);
    memset(&p->info, 0, sizeof p->info);
    p->info.base = fu_bases[port - 1];
    p->info.irq = fu_irqs[port - 1];
    p->info.chip = p->chip;
    p->flags = flags;
    p->obaud = baud;
    p->oflags = flags;
    p->stalled = 0;
    p->held = 0;
    p->open = 1;
    rts_up(p);
    return UART_OK;
}

void uart_close(u8 port)
{
    fu_pt *p;

    p = open_port(port);
    if (p == NULL)
        return;
    cmem_set(p->rx.b, 0, 0, UART_RXSZ + UART_TXSZ);
    cmem_free(p->rx.b);
    memset(&p->rx, 0, sizeof p->rx);
    memset(&p->tx, 0, sizeof p->tx);
    memset(&p->info, 0, sizeof p->info);
    p->open = 0;
    p->flags = 0;
    p->rts = 0;
    p->stalled = 0;
    p->held = 0;
}

u16 uart_read(u8 port, cmem_blk *b, u16 off, u16 n)
{
    fu_pt *p;
    fu_ring d;

    poll_line(port);
    p = open_port(port);
    if (p == NULL)
        return 0;
    if (n > ring_count(&p->rx))
        n = ring_count(&p->rx);
    ring_init(&d, b, off, n, 0);
    ring_xfer(&d, &p->rx, n);
    rx_check(p);
    return n;
}

u16 uart_write(u8 port, cmem_blk *b, u16 off, u16 n)
{
    fu_pt *p;
    fu_ring s;

    pump();
    p = open_port(port);
    if (p != NULL) {
        if (n > ring_room(&p->tx))
            n = ring_room(&p->tx);
        ring_init(&s, b, off, n, 0);
        ring_xfer(&p->tx, &s, n);
        p->stalled = 0;
    }
    step(port);
    return p != NULL ? n : 0;
}

u16 uart_rxcount(u8 port)
{
    fu_pt *p;

    poll_line(port);
    p = open_port(port);
    return p != NULL ? ring_count(&p->rx) : 0;
}

int uart_txidle(u8 port)
{
    fu_pt *p;

    poll_line(port);
    p = open_port(port);
    return p == NULL || ring_count(&p->tx) == 0;
}

void uart_rxflush(u8 port)
{
    fu_pt *p;

    p = open_port(port);
    if (p == NULL)
        return;
    ring_skip(&p->rx, ring_count(&p->rx));
    rx_check(p);
}

void uart_getinfo(u8 port, uart_info *i)
{
    fu_pt *p;

    p = open_port(port);
    if (p != NULL)
        *i = p->info;
    else
        memset(i, 0, sizeof *i);
}

u16 uart_rxerrs(u8 port)
{
    fu_pt *p;

    p = open_port(port);
    if (p == NULL)
        return 0;
    return (u16)(p->info.overrun + p->info.framing + p->info.parity +
                 p->info.lost);
}

void fu_reset(void)
{
    u8 i;

    for (i = 1; i <= UART_MAXPORT; i++) {
        uart_close(i);
        cmem_free(fu_ports[i - 1].in.b);
    }
    memset(fu_ports, 0, sizeof fu_ports);
    fu_pumpfn = NULL;
    fu_pumparg = NULL;
}

void fu_port(u8 port, u8 chip)
{
    fu_pt *p;
    cmem_blk *b;

    if (port < 1 || port > UART_MAXPORT || fu_ports[port - 1].open)
        abort();
    p = &fu_ports[port - 1];
    if (chip == 0) {
        cmem_free(p->in.b);
        if (p->pair != 0)
            fu_ports[p->pair - 1].pair = 0;
        memset(p, 0, sizeof *p);
        return;
    }
    if (p->chip == 0) {
        b = cmem_alloc(2 * FU_QSZ);
        if (b == NULL)
            abort();
        ring_init(&p->in, b, 0, FU_QSZ, 1);
        ring_init(&p->out, b, FU_QSZ, FU_QSZ, 1);
        p->peerrts = 1;
    }
    p->chip = chip;
}

void fu_pair(u8 p1, u8 p2)
{
    fu_pt *a;
    fu_pt *b;

    a = made_port(p1);
    b = made_port(p2);
    if (a == b)
        abort();
    a->pair = p2;
    b->pair = p1;
}

void fu_rate(u8 port, u16 n)
{
    made_port(port)->rate = n;
}

void fu_inject(u8 port, u8 kind, u16 n)
{
    fu_pt *p;

    p = made_port(port);
    p->fault = kind;
    p->nfault = n;
}

u16 fu_peer_send(u8 port, const void *buf, u16 n)
{
    fu_pt *p;
    fu_ring *r;
    u16 k;

    p = made_port(port);
    r = &p->in;
    if (n > ring_room(r))
        n = ring_room(r);
    k = (u16)(r->size - r->tail);
    if (k > n)
        k = n;
    cmem_put(r->b, (u16)(r->base + r->tail), buf, k);
    cmem_put(r->b, r->base, (const u8 *)buf + k, (u16)(n - k));
    r->tail = (u16)((r->tail + (u32)n) % r->size);
    step(0);
    return n;
}

u16 fu_peer_recv(u8 port, void *buf, u16 size)
{
    fu_pt *p;
    fu_ring *r;
    u16 n;
    u16 k;

    p = made_port(port);
    r = &p->out;
    step(0);
    n = ring_count(r);
    if (n > size)
        n = size;
    k = (u16)(r->size - r->head);
    if (k > n)
        k = n;
    cmem_get(r->b, (u16)(r->base + r->head), buf, k);
    cmem_get(r->b, r->base, (u8 *)buf + k, (u16)(n - k));
    ring_skip(r, n);
    return n;
}

void fu_peer_rts(u8 port, int on)
{
    fu_pt *p;

    p = made_port(port);
    if (on && !p->peerrts)
        p->stalled = 0;
    p->peerrts = (u8)(on != 0);
}

u8 uart_modem(u8 port)
{
    fu_pt *p;
    u8 m;

    p = open_port(port);
    if (p == NULL)
        return 0;
    m = (u8)(cts(p) ? UART_M_CTS : 0);
    if (p->flags & UART_F_LOOP)
        return (u8)(m | UART_M_DSR | UART_M_DCD);
    if (p->pair == 0)
        return (u8)(m | p->peerlines);
    return (u8)(m | (on_line(&fu_ports[p->pair - 1]) ?
                     UART_M_DSR | UART_M_DCD : 0));
}

void fu_modem(u8 port, u8 lines)
{
    made_port(port)->peerlines = (u8)(lines & (UART_M_DSR | UART_M_DCD));
}

u16 fu_opened(u8 port, u8 *flags)
{
    fu_pt *p;

    p = made_port(port);
    *flags = p->oflags;
    return p->obaud;
}

void fu_pump(fu_pump_fn fn, void *arg)
{
    fu_pumpfn = fn;
    fu_pumparg = arg;
}

cmem_blk *fu_ringblk(u8 port)
{
    fu_pt *p;

    p = open_port(port);
    return p != NULL ? p->rx.b : NULL;
}

u8 fu_isrstate(u8 port)
{
    fu_pt *p;

    p = open_port(port);
    if (p == NULL)
        return 0;
    return (u8)(FU_S_OPEN | FU_S_IER | FU_S_IRQ | FU_S_VECT |
                (p->rts ? FU_S_RTS : 0));
}

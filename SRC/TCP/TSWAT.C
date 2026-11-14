/*
 * TSWAT.C - the TCP sockets of the bridge (TSOCK.H) on WATTCP's kernel
 * and the packet driver.
 *
 * Slot k is the WATTCP socket of tw_s[k], a passive open on the slot's
 * port that takes any peer, in binary mode and with Nagle's algorithm
 * off, so that the kernel starts sending what ts_write gives it at
 * once, as fast as the peer's window allows.  The slot's state is read
 * from the socket's TCP state: LISTEN and SYN-RECEIVED are TS_LISTEN,
 * ESTABLISHED is TS_OPEN and every other state is TS_CLOSING.  The
 * kernel answers a peer's FIN with its own at once (LAST-ACK), which is
 * why a peer's close ends both ways.  Only tcp_tick brings a socket to
 * CLOSED by itself (let_go's sock_abort makes the slot TS_CLOSED at
 * once), and such a socket may stay on the kernel's list for a while;
 * ts_poll takes it off right after its tcp_tick, as the kernel's
 * tcp_tick does for the socket a program gives it, and the slot is
 * TS_CLOSED: the kernel holds nothing of it, and it may listen again.
 *
 * sock_close sends the FIN once the bytes not yet sent have gone, and
 * calls tcp_tick, which TSOCK.H allows only inside ts_poll: ts_close
 * marks the slot (TW_SHUT, TS_CLOSING), and the next ts_poll closes it.
 * Every ts_poll first stirs the pool of the kernel's initial sequence
 * numbers (wt_stir, CWATT.C).
 *
 * Received bytes wait in the socket's receive buffer from its start up
 * to rdatalen; everything past that is kept zero.  ts_read zeroes the
 * bytes the kernel's read leaves behind as it moves the rest down,
 * ts_close and every ts_poll zero those of a slot no longer TS_OPEN,
 * and a slot that reaches TS_CLOSED has its whole buffer zeroed, since
 * a reset clears rdatalen without zeroing the bytes.
 *
 * The kernel's one call of exit, under Turbo C for a socket that starts
 * within its own size of the top of the data segment (a socket on the
 * stack), cannot happen to these static sockets.  Like every file that
 * includes WATTCP's headers, this one is built with WATTCP's byte
 * packing and signed char.
 */
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "WATTCP.H"
#include "TSOCK.H"

/* The layer's phases. */
#define TW_OFF          0       /* ts_init not called                   */
#define TW_ON           1       /* started                              */
#define TW_END          2       /* ts_init failed, or ts_term ran       */

/* A slot's phases. */
#define TW_FREE         0       /* never listened since ts_init         */
#define TW_LIVE         1       /* its socket is on the kernel's list   */
#define TW_SHUT         2       /* ts_close called, the FIN not sent    */
#define TW_GONE         3       /* the kernel let it go: TS_CLOSED      */

typedef struct {
    sock_type sock;
    u8 phase;                   /* TW_ phase                            */
    u8 opened;                  /* 1 once its connection opened         */
} tw_slot;

static tw_slot tw_s[TS_SLOTS];
static u8 tw_run;               /* TW_OFF, TW_ON or TW_END              */

/* Zero the received bytes waiting in s: they are dropped. */
static void drop(tcp_Socket *s)
{
    memset(s->rdata, 0, (size_t)s->rdatalen);
    s->rdatalen = 0;
}

/* Let slot t's socket go: the kernel aborts it (RST unless it is
   LISTEN or CLOSED) and takes it off its list, its whole receive buffer
   is zeroed, and the slot is TS_CLOSED. */
static void let_go(tw_slot *t)
{
    tcp_Socket *s;

    s = &t->sock.tcp;
    if (s->ip_type == TCP_PROTO)
        sock_abort(s);
    memset(s->rddata, 0, sizeof s->rddata);
    s->rdatalen = 0;
    t->phase = TW_GONE;
}

/* Bring slot t, TW_LIVE, up to date with the kernel's work: let its
   socket go once it is CLOSED, note that its connection has opened, and
   drop what it has received once it is no longer TS_OPEN. */
static void settle(tw_slot *t)
{
    tcp_Socket *s;

    s = &t->sock.tcp;
    if (s->state == tcp_StateCLOSED) {
        let_go(t);
    } else if (s->state >= tcp_StateESTAB) {
        t->opened = 1;
        if (s->state != tcp_StateESTAB)
            drop(s);
    }
}

int ts_init(void)
{
    int rc;

    if (tw_run != TW_OFF)
        return TS_EARG;
    tw_run = TW_END;
    tcp_init();
    switch (pkt_error()) {
    case PKT_OK:
        /* WATTCP.CFG is found as WATTCP's own programs find it. */
        rc = tcp_config(NULL) != 0 || my_ip_addr == 0 ? TS_ECFG : TS_OK;
        break;
    case PKT_ENONE:
        rc = TS_ENOPKT;
        break;
    default:
        rc = TS_EINIT;
        break;
    }
    if (rc == TS_OK && atexit(ts_term) != 0)
        rc = TS_EINIT;
    if (rc != TS_OK) {
        tcp_shutdown();
        return rc;
    }
    tw_run = TW_ON;
    return TS_OK;
}

void ts_term(void)
{
    if (tw_run != TW_ON)
        return;
    tcp_shutdown();
    memset(tw_s, 0, sizeof tw_s);
    tw_run = TW_END;
}

int ts_listen(u8 slot, u16 port)
{
    tw_slot *t;

    if (tw_run != TW_ON || slot >= TS_SLOTS || port == 0)
        return TS_EARG;
    t = &tw_s[slot];
    if (t->phase != TW_FREE && t->phase != TW_GONE)
        return TS_EARG;
    tcp_listen(&t->sock.tcp, port, 0, 0, NULL, 0);
    /* tcp_listen takes port 65535 as a request for a free port. */
    t->sock.tcp.myport = port;
    sock_mode(&t->sock, TCP_MODE_BINARY | TCP_MODE_NONAGLE);
    t->phase = TW_LIVE;
    t->opened = 0;
    return TS_OK;
}

void ts_poll(void)
{
    tw_slot *t;

    if (tw_run != TW_ON)
        return;
    wt_stir();
    for (t = tw_s; t < tw_s + TS_SLOTS; t++) {
        if (t->phase == TW_SHUT) {
            t->phase = TW_LIVE;
            sock_close(&t->sock);
        }
    }
    /* NULL, as the kernel's own ARP wait passes it; the result tcp_tick
       then reads through it is not used. */
    tcp_tick(NULL);
    for (t = tw_s; t < tw_s + TS_SLOTS; t++)
        if (t->phase == TW_LIVE)
            settle(t);
}

u8 ts_state(u8 slot)
{
    if (slot >= TS_SLOTS)
        return TS_FREE;
    switch (tw_s[slot].phase) {
    case TW_FREE:
        return TS_FREE;
    case TW_SHUT:
        return TS_CLOSING;
    case TW_GONE:
        return TS_CLOSED;
    default:
        break;
    }
    switch (tw_s[slot].sock.tcp.state) {
    case tcp_StateLISTEN:
    case tcp_StateSYNREC:
        return TS_LISTEN;
    case tcp_StateESTAB:
        return TS_OPEN;
    default:
        return TS_CLOSING;
    }
}

u16 ts_read(u8 slot, void *buf, u16 max)
{
    tcp_Socket *s;
    u16 n;

    if (ts_state(slot) != TS_OPEN)
        return 0;
    s = &tw_s[slot].sock.tcp;
    n = (u16)s->rdatalen;
    if (n > max)
        n = max;
    if (n == 0)
        return 0;
    sock_fastread(&tw_s[slot].sock, (byte *)buf, (int)n);
    /* The read moved the bytes left down by n: zero the n at the end. */
    memset(s->rdata + s->rdatalen, 0, (size_t)n);
    return n;
}

/* The room tcp_write, beneath sock_fastwrite, fills in ESTABLISHED: it
   takes all it is given up to this and fails in no other way. */
u16 ts_room(u8 slot)
{
    if (ts_state(slot) != TS_OPEN)
        return 0;
    return (u16)sock_tbleft(&tw_s[slot].sock.tcp);
}

u16 ts_write(u8 slot, const void *buf, u16 len)
{
    u16 n;

    n = ts_room(slot);
    if (len < n)
        n = len;
    if (n == 0)
        return 0;
    return (u16)sock_fastwrite(&tw_s[slot].sock, (byte *)buf, (int)n);
}

u16 ts_unsent(u8 slot)
{
    u8 st;

    st = ts_state(slot);
    if (st != TS_OPEN && st != TS_CLOSING)
        return 0;
    return (u16)tw_s[slot].sock.tcp.datalen;
}

void ts_close(u8 slot)
{
    if (ts_state(slot) != TS_OPEN)
        return;
    drop(&tw_s[slot].sock.tcp);
    tw_s[slot].phase = TW_SHUT;
}

void ts_abort(u8 slot)
{
    if (ts_state(slot) != TS_FREE)
        let_go(&tw_s[slot]);
}

void ts_peer(u8 slot, u8 *ip, u16 *port)
{
    const tcp_Socket *s;
    int i;

    if (slot < TS_SLOTS && tw_s[slot].opened) {
        s = &tw_s[slot].sock.tcp;
        for (i = 0; i < TS_IPLEN; i++)
            ip[i] = (u8)(s->hisaddr >> (8 * (TS_IPLEN - 1 - i)));
        *port = s->hisport;
    } else {
        memset(ip, 0, TS_IPLEN);
        *port = 0;
    }
}

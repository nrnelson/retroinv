/*
 * TSERIAL.C - the server's serial transport (TSERIAL.H).
 *
 * Each port served has one tsr_port, which holds one CONN slot at a time
 * and goes through the states below in order, back to TSR_FREE:
 * claimed, it listens, dropping what comes with a receive error; the
 * first byte received opens the session; the session's end (ops->close)
 * flushes the receive ring and starts the drain, whose first pass notes
 * the time; the drain gives the slot back once the transmitter is idle
 * or CONN_SHUTWAIT ticks have gone, the port then closed and opened
 * again so that what its transmit ring still held is dropped (a port
 * that does not open again stays closed).
 * A caller that hangs up (TSERIAL.H) skips the drain: its slot is
 * given back and its port reopened in one pass; a damaged line's slot
 * is given back as fast, the port's receive ring only flushed.  One
 * pass of a port may take it through several of these steps.
 *
 * Input goes from the receive ring into the port's TSER_CHUNK bytes of
 * one far block, from where CONN reads it.  CONN zeroes every byte it
 * is handed, read or dropped, so the block holds no request when the
 * transport frees it.  The port's error count (uart_rxerrs) is noted
 * wherever its receive ring is emptied and looked at after each read of
 * the ring, by when it has risen for any byte read that may be damaged
 * (COMPAT.H); once it has changed, the bytes read are zeroed and kept
 * from CONN, and the first LF among them ends the session.  Output goes
 * from CONN's block straight into the transmit ring, from ops->kick as
 * from a pass; the bytes the ring took are reported with conn_out_done
 * on the port's next pass, since kick may not call it.
 */
#define COMPAT_UART
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TSERIAL.H"

/* One slot per port, every port's input within one CONN input and its
   block, and the TSER_ results equal to UART_'s.  Each pair below
   declares one array twice, the second time with size 2 when the
   condition fails: the declarations then disagree, which every compiler
   refuses. */
extern char tserial_ports_ok[1];
extern char tserial_ports_ok[(CONN_SERMAX == UART_MAXPORT) ? 1 : 2];
extern char tserial_chunk_ok[1];
extern char tserial_chunk_ok[(TSER_CHUNK <= CONN_RXSZ &&
                              UART_MAXPORT * (u32)TSER_CHUNK <=
                              CMEM_MAXBLK) ? 1 : 2];
extern char tserial_codes_ok[1];
extern char tserial_codes_ok[(TSER_EARG == UART_EARG &&
                              TSER_ENOPORT == UART_ENOPORT &&
                              TSER_EBUSY == UART_EBUSY &&
                              TSER_ENOMEM == UART_ENOMEM) ? 1 : 2];

/* Port states. */
#define TSR_FREE        0       /* no CONN slot                         */
#define TSR_LISTEN      1       /* a CONN_LISTEN slot                   */
#define TSR_OPEN        2       /* a session                            */
#define TSR_CLOSE       3       /* ended: the drain starts next pass    */
#define TSR_DRAIN       4       /* the last output goes out             */

typedef struct {
    u8   port;                  /* 1..UART_MAXPORT                      */
    u8   conn;                  /* the CONN slot                        */
    u8   state;                 /* TSR_ state                           */
    u8   rxbusy;                /* input handed to CONN, not yet done   */
    u8   armed;                 /* DSR or DCD seen up in the session    */
    u8   low;                   /* armed, and both down since downat    */
    u16  sent;                  /* output in the ring, not yet reported */
    u16  errs;                  /* uart_rxerrs when the receive ring was
                                   last emptied                         */
    u32  since;                 /* tick the drain began                 */
    u32  downat;
} tsr_port;

typedef struct {
    cmem_blk *rx;               /* the input buffers, NULL when stopped */
    u16  baud;                  /* tserial_start's, for a reopen        */
    u8   flags;
    u8   n;                     /* ports served                         */
    u8   of[CONN_MAX];          /* index in s of each CONN slot         */
    tsr_port s[UART_MAXPORT];
} transport;

static transport tserial_tp;

/* The port holding CONN slot c. */
static tsr_port *port_of(u8 c)
{
    return &tserial_tp.s[tserial_tp.of[c]];
}

/* Put as much of t's output as the transmit ring takes into it; the
   bytes it took, held in sent until the next pass reports them, unless
   the session is closed first (an idle timeout in the same pass).  No
   bytes put there before are unreported then: a pass reports them
   before it sends, and CONN kicks only a slot whose output was all
   reported. */
static u16 send(tsr_port *t)
{
    cmem_blk *b;
    u16 off;
    u16 len;

    len = conn_out_peek(t->conn, &b, &off);
    t->sent = len != 0 ? uart_write(t->port, b, off, len) : 0;
    return t->sent;
}

static void tsr_kick(u8 c)
{
    send(port_of(c));
}

static void tsr_rx_done(u8 c)
{
    port_of(c)->rxbusy = 0;
}

/* Note the error count, then drop what t's receive ring holds: an
   error counted during the flush, whose byte may stay, drops the next
   line rather than pass it. */
static void flush(tsr_port *t)
{
    t->errs = uart_rxerrs(t->port);
    uart_rxflush(t->port);
}

static void tsr_close(u8 c)
{
    tsr_port *t;

    t = port_of(c);
    flush(t);
    t->state = TSR_CLOSE;
    t->rxbusy = 0;
    t->sent = 0;
}

static const conn_tops tsr_ops[UART_MAXPORT] = {
    { tsr_kick, tsr_rx_done, tsr_close, "COM1", 0 },
    { tsr_kick, tsr_rx_done, tsr_close, "COM2", 0 }
};

/* Close t's port and open it again, which drops what its rings hold
   and starts its error count from 0, as that of a port that does not
   open again, which stays closed. */
static void reopen(tsr_port *t)
{
    uart_close(t->port);
    uart_open(t->port, tserial_tp.baud, tserial_tp.flags);
    t->errs = 0;
}

/* End t's session at once, its slot given back.  After a hang-up (hup
   1) the port is reopened, which drops what its rings hold; after a
   damaged line only its receive ring is flushed, so that its error
   counts stay for the status screen. */
static void end_now(tsr_port *t, int hup)
{
    conn_closed(t->conn);
    if (hup)
        reopen(t);
    else
        flush(t);
    t->state = TSR_FREE;
    t->rxbusy = 0;
    t->sent = 0;
}

/* 1 when the n bytes at off of the input buffers hold an LF; they are
   zeroed there, as is the copy looked through. */
static int drop_lf(u16 off, u16 n)
{
    char b[TSER_CHUNK];
    int lf;

    cmem_get(tserial_tp.rx, off, b, n);
    cmem_set(tserial_tp.rx, off, 0, n);
    lf = memchr(b, '\n', n) != NULL;
    txt_wipe(b, n);
    return lf;
}

/* 1 when t's caller has hung up at time now: its DSR and DCD, once
   either was up in the session, have both been down for TSER_HANGUP
   ticks. */
static int hung_up(tsr_port *t, u32 now)
{
    if (uart_modem(t->port) & (UART_M_DSR | UART_M_DCD)) {
        t->armed = 1;
        t->low = 0;
        return 0;
    }
    if (!t->armed)
        return 0;
    if (!t->low) {
        t->low = 1;
        t->downat = now;
    }
    return now - t->downat >= TSER_HANGUP;
}

/* One pass of port k at time now; returns the bytes moved, sessions
   opened and slots given back or claimed. */
static u16 serve(u8 k, u32 now)
{
    tsr_port *t;
    u16 off;
    u16 n;
    u16 m;

    t = &tserial_tp.s[k];
    n = 0;
    if (t->state == TSR_CLOSE) {
        t->since = now;
        t->state = TSR_DRAIN;
    }
    if (t->state == TSR_DRAIN) {
        if (!uart_txidle(t->port)) {
            if (now - t->since < CONN_SHUTWAIT)
                return 0;
            reopen(t);
        }
        conn_closed(t->conn);
        t->state = TSR_FREE;
        n++;
    }
    if (t->state == TSR_FREE) {
        if (!conn_claim(&tsr_ops[t->port - 1], &t->conn))
            return n;
        tserial_tp.of[t->conn] = k;
        t->state = TSR_LISTEN;
        n++;
    }
    if (t->state == TSR_LISTEN) {
        if (uart_rxcount(t->port) == 0)
            return n;
        /* What came with a receive error while no session ran is line
           noise (a cable plugged in, the other machine switched on):
           dropped, so that it does not cost the next caller its first
           line. */
        if (uart_rxerrs(t->port) != t->errs) {
            flush(t);
            return n;
        }
        /* Open first: conn_open closes at once during a shutdown. */
        t->state = TSR_OPEN;
        t->armed = 0;
        t->low = 0;
        conn_open(t->conn, now, NULL);
        n++;
    }
    if (t->sent != 0) {
        m = t->sent;
        t->sent = 0;
        conn_out_done(t->conn, now, m);
        n = (u16)(n + m);
    }
    if (t->state != TSR_OPEN)
        return n;
    if (hung_up(t, now)) {
        end_now(t, 1);
        return (u16)(n + 1);
    }
    n = (u16)(n + send(t));
    if (t->rxbusy)
        return n;
    off = (u16)(k * TSER_CHUNK);
    m = uart_read(t->port, tserial_tp.rx, off, TSER_CHUNK);
    if (m == 0)
        return n;
    n = (u16)(n + m);
    if (uart_rxerrs(t->port) == t->errs) {
        t->rxbusy = 1;
        conn_input(t->conn, tserial_tp.rx, off, m);
    } else if (drop_lf(off, m)) {
        /* The line may have lost or changed a byte: it is not carried
           out, and gets no response. */
        end_now(t, 0);
        n++;
    }
    return n;
}

/* Close the ports of the first n slots and free the input buffers. */
static void release(u8 n)
{
    while (n > 0)
        uart_close(tserial_tp.s[--n].port);
    cmem_free(tserial_tp.rx);
    tserial_tp.rx = NULL;
    tserial_tp.n = 0;
}

int tserial_start(u8 ports, u16 baud, u8 flags, u8 *bad)
{
    int rc;
    u8 p;
    u8 n;

    *bad = 0;
    if (tserial_tp.rx != NULL || ports == 0 ||
        (ports >> UART_MAXPORT) != 0)
        return TSER_EARG;
    memset(&tserial_tp, 0, sizeof tserial_tp);
    n = 0;
    for (p = 1; p <= UART_MAXPORT; p++)
        if (ports & (1 << (p - 1)))
            tserial_tp.s[n++].port = p;
    tserial_tp.rx = cmem_alloc((u16)(n * TSER_CHUNK));
    if (tserial_tp.rx == NULL)
        return TSER_ENOMEM;
    for (p = 0; p < n; p++) {
        rc = uart_open(tserial_tp.s[p].port, baud, flags);
        if (rc != UART_OK) {
            *bad = tserial_tp.s[p].port;
            release(p);
            return rc;
        }
    }
    tserial_tp.n = n;
    tserial_tp.baud = baud;
    tserial_tp.flags = flags;
    return TSER_OK;
}

u16 tserial_poll(u32 now)
{
    u16 n;
    u8 k;

    n = 0;
    for (k = 0; k < tserial_tp.n; k++)
        n = (u16)(n + serve(k, now));
    return n;
}

void tserial_stop(void)
{
    u8 k;

    for (k = 0; k < tserial_tp.n; k++)
        if (tserial_tp.s[k].state != TSR_FREE)
            conn_closed(tserial_tp.s[k].conn);
    release(tserial_tp.n);
}

int tserial_info(u8 i, u8 *port, uart_info *ui)
{
    if (i >= tserial_tp.n)
        return TSER_EARG;
    *port = tserial_tp.s[i].port;
    uart_getinfo(*port, ui);
    return TSER_OK;
}

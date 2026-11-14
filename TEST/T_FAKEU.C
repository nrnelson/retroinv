/*
 * T_FAKEU.C - unit test for the simulated serial ports (FAKEUART.C).
 *
 * usage: T_FAKEU
 *
 * Checks:
 * - uart_open refuses a port out of range, a baud rate off the list and
 *   an unknown flag with UART_EARG, a port fu_port did not make with
 *   UART_ENOPORT, a second open with UART_EBUSY, and with no far memory
 *   gives UART_ENOMEM, nothing open after any refusal; it opens at every
 *   rate of the list; uart_getinfo gives the base, IRQ and chip of an
 *   open port and zeros otherwise; the calls on a port not open give 0
 *   (uart_txidle 1); uart_close undoes every fu_isrstate bit, twice
 *   harmlessly, and fu_reset closes and removes every port;
 * - bytes cross between the test and a port both ways; a full receive
 *   ring (UART_RXSZ - 1 bytes) holds the receiver, the rest waiting in
 *   the test's queue and arriving once the ring drains to UART_RXLOW,
 *   none lost; a transmit ring
 *   takes UART_TXSZ - 1, and while the test's queue is full the port's
 *   bytes wait in it; the test's queue outlives a close, and a close
 *   drops what the transmit ring held;
 * - fu_rate paces each step; uart_rxflush empties the receive ring;
 * - two paired ports talk both ways, and bytes sent to a closed one are
 *   gone; UART_F_LOOP brings a port's bytes back to itself, in its own
 *   calls only, while the test's wait;
 * - RTS/CTS: a port drops RTS at UART_RXHIGH and raises it at
 *   UART_RXLOW or on a flush, the test, a paired port and the port
 *   itself in loopback then hold their bytes, so none is lost; the
 *   test's RTS holds the port's sending; a port that met CTS down
 *   sends again on a uart_write once CTS is up, also when CTS rose
 *   with no modem-status event (fu_pair rewiring its line);
 * - fu_inject drops or counts the marked bytes by their kind, a marked
 *   byte waiting as any other while the receiver is held;
 * - uart_rxerrs gives the sum of the four counts since the open, 0 for
 *   a port not open and after a reopen, unchanged by clean traffic, and
 *   wraps at 65536 as uart_getinfo's counts do;
 * - every byte that leaves a ring is zeroed there (fu_ringblk); the
 *   test's queues wrap round with their contents; fu_port removing a
 *   port unpairs its partner;
 * - the pump runs at the start of the five polling calls only, never
 *   inside itself.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_UART
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "FAKEUART.H"

#define BIG             2000    /* more than a ring holds               */
#define PIECE           512     /* a part of the rings' block           */

extern char t_fakeu_pieces[1];
extern char t_fakeu_pieces[((UART_RXSZ + UART_TXSZ) % PIECE == 0 &&
                            FU_PEERQ % PIECE == 0) ? 1 : 2];

static cmem_blk *g_b;           /* the program's side of every transfer */
static u8 g_buf[BIG];
static u8 g_got[BIG];
static u16 g_pumps;
static u8 g_inpump;
static u8 g_nested;
static u16 g_pumpsz;

/* n bytes of a pattern that starts at seed into buf. */
static void fill(u8 *buf, u16 n, u8 seed)
{
    u16 i;

    for (i = 0; i < n; i++)
        buf[i] = (u8)(i * 7u + seed);
}

/* Write n bytes of g_buf to port; the count taken. */
static u16 put(u8 port, u16 n)
{
    cmem_put(g_b, 0, g_buf, n);
    return uart_write(port, g_b, 0, n);
}

/* Read up to n bytes of port into g_got; the count read. */
static u16 get(u8 port, u16 n)
{
    u16 k;

    k = uart_read(port, g_b, 0, n);
    cmem_get(g_b, 0, g_got, k);
    return k;
}

/* 1 when every byte of port's rings is 0: none that left them stayed;
   g_got is overwritten. */
static int wiped(u8 port)
{
    cmem_blk *b;
    u16 off;
    u16 i;

    b = fu_ringblk(port);
    if (b == NULL)
        return 0;
    for (off = 0; off < UART_RXSZ + UART_TXSZ; off = (u16)(off + PIECE)) {
        cmem_get(b, off, g_got, PIECE);
        for (i = 0; i < PIECE; i++)
            if (g_got[i] != 0)
                return 0;
    }
    return 1;
}

static int fail_alloc(u16 size)
{
    return size != 0;
}

/* Ports 1 and 2 made, no pair. */
static void begin(void)
{
    fu_reset();
    fu_port(1, UART_T_16550A);
    fu_port(2, UART_T_8250);
}

static void test_open(void)
{
    static const u16 bauds[] = { 1200, 2400, 4800, 9600, 19200, 38400u };
    uart_info i;
    u16 k;

    fu_reset();
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_ENOPORT);
    begin();
    TF_EQ(uart_open(0, UART_DEFBAUD, 0), UART_EARG);
    TF_EQ(uart_open(UART_MAXPORT + 1, UART_DEFBAUD, 0), UART_EARG);
    TF_EQ(uart_open(1, 600, 0), UART_EARG);
    TF_EQ(uart_open(1, 9601, 0), UART_EARG);
    TF_EQ(uart_open(1, 57600u, 0), UART_EARG);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0x08), UART_EARG);
    TF_EQ(fu_isrstate(1), 0);
    cmem_fault = fail_alloc;
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_ENOMEM);
    cmem_fault = NULL;
    TF_EQ(fu_isrstate(1), 0);
    for (k = 0; k < sizeof bauds / sizeof bauds[0]; k++) {
        TF_EQ(uart_open(1, bauds[k], UART_F_NOFIFO), UART_OK);
        uart_close(1);
    }
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_EBUSY);
    TF_EQ(fu_isrstate(1), FU_S_OPEN | FU_S_IER | FU_S_IRQ | FU_S_VECT |
                          FU_S_RTS);
    uart_getinfo(1, &i);
    TF_EQ(i.base, 0x3F8);
    TF_EQ(i.irq, 4);
    TF_EQ(i.chip, UART_T_16550A);
    TF_EQ(uart_open(2, UART_MAXBAUD, 0), UART_OK);
    uart_getinfo(2, &i);
    TF_EQ(i.base, 0x2F8);
    TF_EQ(i.irq, 3);
    TF_EQ(i.chip, UART_T_8250);
    uart_close(1);
    TF_EQ(fu_isrstate(1), 0);
    uart_close(1);
    uart_getinfo(1, &i);
    TF_EQ(i.base + i.irq + i.chip + i.overrun + i.framing + i.parity +
          i.lost, 0);
    TF_EQ(uart_rxcount(1), 0);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(get(1, 10), 0);
    TF_EQ(put(1, 10), 0);
    uart_rxflush(1);
    TF_EQ(uart_rxcount(0), 0);
    TF_EQ(uart_rxcount(UART_MAXPORT + 1), 0);
    fu_reset();
    TF_EQ(fu_isrstate(2), 0);
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_ENOPORT);
}

static void test_peer(void)
{
    uart_info i;
    u16 k;

    begin();
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    fill(g_buf, 5, 1);
    TF_EQ(fu_peer_send(1, g_buf, 5), 5);
    TF_EQ(uart_rxcount(1), 5);
    TF_EQ(get(1, 10), 5);
    TF_CHECK(memcmp(g_got, g_buf, 5) == 0);
    fill(g_buf, 7, 2);
    TF_EQ(put(1, 7), 7);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(fu_peer_recv(1, g_got, sizeof g_got), 7);
    TF_CHECK(memcmp(g_got, g_buf, 7) == 0);
    TF_EQ(fu_peer_recv(1, g_got, sizeof g_got), 0);
    TF_CHECK(wiped(1));

    fill(g_buf, BIG, 3);
    TF_EQ(fu_peer_send(1, g_buf, BIG), BIG);
    TF_EQ(uart_rxcount(1), UART_RXSZ - 1);
    TF_EQ(get(1, UART_RXSZ - 1 - UART_RXLOW - 1),
          UART_RXSZ - 1 - UART_RXLOW - 1);
    TF_EQ(uart_rxcount(1), UART_RXLOW + 1);
    TF_EQ(get(1, 1), 1);
    TF_EQ(uart_rxcount(1), UART_RXSZ - 1);
    TF_EQ(get(1, BIG), UART_RXSZ - 1);
    TF_CHECK(memcmp(g_got, g_buf + UART_RXSZ - 1 - UART_RXLOW,
                    UART_RXSZ - 1) == 0);
    TF_EQ(get(1, BIG), BIG - 2 * (UART_RXSZ - 1) + UART_RXLOW);
    uart_getinfo(1, &i);
    TF_EQ(i.lost, 0);
    TF_EQ(fu_peer_send(1, g_buf, 3), 3);
    uart_rxflush(1);
    TF_EQ(uart_rxcount(1), 0);
    TF_CHECK(wiped(1));

    /* The test's queue wraps round with its contents. */
    fill(g_buf, 100, 9);
    TF_EQ(fu_peer_send(1, g_buf, 100), 100);
    TF_EQ(get(1, BIG), 100);
    TF_CHECK(memcmp(g_got, g_buf, 100) == 0);

    /* The test's queue full: the port's bytes wait in its ring. */
    for (k = 0; k < FU_PEERQ; k = (u16)(k + PIECE))
        TF_EQ(put(1, PIECE), PIECE);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(put(1, BIG), UART_TXSZ - 1);
    TF_EQ(uart_txidle(1), 0);
    TF_EQ(fu_peer_recv(1, g_got, BIG), BIG);
    TF_CHECK(memcmp(g_got, g_buf, PIECE) == 0);
    TF_CHECK(memcmp(g_got + 3 * PIECE, g_buf, BIG - 3 * PIECE) == 0);
    TF_EQ(fu_peer_recv(1, g_got, BIG), FU_PEERQ - BIG + UART_TXSZ - 1);
    TF_CHECK(memcmp(g_got, g_buf + BIG - 3 * PIECE, FU_PEERQ - BIG) == 0);
    TF_CHECK(memcmp(g_got + FU_PEERQ - BIG, g_buf, UART_TXSZ - 1) == 0);
    TF_EQ(uart_txidle(1), 1);
    TF_CHECK(wiped(1));

    /* What reached the queue outlives the close; the ring's rest not. */
    TF_EQ(put(1, 4), 4);
    fu_peer_rts(1, 0);
    uart_close(1);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    TF_EQ(put(1, 6), 6);
    TF_EQ(uart_txidle(1), 0);
    uart_close(1);
    fu_peer_rts(1, 1);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 4);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 0);
    fu_reset();
}

static void test_rate(void)
{
    begin();
    fu_rate(1, 10);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    fill(g_buf, 25, 4);
    TF_EQ(fu_peer_send(1, g_buf, 25), 25);
    TF_EQ(uart_rxcount(1), 20);
    TF_EQ(uart_rxcount(1), 25);
    TF_EQ(put(1, 25), 25);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 20);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 5);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 0);
    fu_rate(1, 0);
    TF_EQ(put(1, 25), 25);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 25);
    fu_reset();
}

static void test_pair(void)
{
    begin();
    fu_pair(1, 2);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_OK);
    fill(g_buf, 9, 5);
    TF_EQ(put(1, 9), 9);
    TF_EQ(get(2, BIG), 9);
    TF_CHECK(memcmp(g_got, g_buf, 9) == 0);
    fill(g_buf, 11, 6);
    TF_EQ(put(2, 11), 11);
    TF_EQ(get(1, BIG), 11);
    TF_CHECK(memcmp(g_got, g_buf, 11) == 0);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 0);
    uart_close(2);
    TF_EQ(put(1, 9), 9);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_rxcount(2), 0);

    /* Loopback: back to the port itself, not to the far end... */
    uart_close(1);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_LOOP), UART_OK);
    TF_EQ(put(1, 9), 9);
    TF_EQ(get(1, BIG), 9);
    TF_CHECK(memcmp(g_got, g_buf, 9) == 0);
    TF_EQ(uart_rxcount(2), 0);

    /* ... while the test's bytes wait. */
    begin();
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_LOOP), UART_OK);
    TF_EQ(fu_peer_send(1, g_buf, 3), 3);
    TF_EQ(uart_rxcount(1), 0);
    uart_close(1);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_rxcount(1), 3);
    fu_reset();
}

/* RTS/CTS on a pair and against the test: no byte lost. */
static void test_rtscts(void)
{
    uart_info i;

    begin();
    fu_pair(1, 2);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    TF_EQ(uart_open(2, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    fill(g_buf, BIG, 7);
    TF_EQ(put(1, UART_TXSZ - 1), UART_TXSZ - 1);
    TF_EQ(uart_rxcount(2), UART_RXHIGH);
    TF_EQ(fu_isrstate(2) & FU_S_RTS, 0);
    TF_EQ(uart_txidle(1), 0);
    TF_EQ(get(2, UART_RXHIGH - UART_RXLOW - 1),
          UART_RXHIGH - UART_RXLOW - 1);
    TF_EQ(fu_isrstate(2) & FU_S_RTS, 0);
    TF_EQ(get(2, 1), 1);
    TF_EQ(fu_isrstate(2) & FU_S_RTS, FU_S_RTS);
    TF_EQ(uart_rxcount(2), UART_TXSZ - 1 - (UART_RXHIGH - UART_RXLOW));
    TF_EQ(uart_txidle(1), 1);
    uart_getinfo(2, &i);
    TF_EQ(i.lost, 0);

    /* Against the test. */
    begin();
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    TF_EQ(fu_peer_send(1, g_buf, BIG), BIG);
    TF_EQ(uart_rxcount(1), UART_RXHIGH);
    TF_EQ(get(1, UART_RXHIGH), UART_RXHIGH);
    TF_EQ(uart_rxcount(1), UART_RXHIGH);
    TF_EQ(get(1, BIG), UART_RXHIGH);
    TF_EQ(uart_rxcount(1), BIG - 2 * UART_RXHIGH);
    uart_getinfo(1, &i);
    TF_EQ(i.lost, 0);
    uart_rxflush(1);
    fu_peer_rts(1, 0);
    TF_EQ(put(1, 5), 5);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 0);
    fu_peer_rts(1, 1);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 5);

    /* Once stalled, only a rise of CTS or a uart_write sends again:
       pairing port 1 with the open port 2 raises its CTS with no
       modem-status event. */
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_OK);
    fu_peer_rts(1, 0);
    TF_EQ(put(1, 5), 5);
    fu_pair(1, 2);
    TF_EQ(uart_rxcount(2), 0);
    TF_EQ(put(1, 1), 1);
    TF_EQ(uart_rxcount(2), 6);
    fu_reset();
}

static void test_inject(void)
{
    uart_info i;

    begin();
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    fill(g_buf, 20, 8);
    fu_inject(1, FU_OVERRUN, 2);
    TF_EQ(fu_peer_send(1, g_buf, 5), 5);
    TF_EQ(get(1, BIG), 3);
    TF_CHECK(memcmp(g_got, g_buf + 2, 3) == 0);
    fu_inject(1, FU_FRAMING, 1);
    TF_EQ(fu_peer_send(1, g_buf, 2), 2);
    fu_inject(1, FU_PARITY, 3);
    TF_EQ(fu_peer_send(1, g_buf, 2), 2);
    fu_inject(1, FU_LOST, 4);
    TF_EQ(fu_peer_send(1, g_buf, 6), 6);
    TF_EQ(get(1, BIG), 4 + 2);
    uart_getinfo(1, &i);
    TF_EQ(i.overrun, 2);
    TF_EQ(i.framing, 1);
    TF_EQ(i.parity, 2);
    TF_EQ(i.lost, 4);
    fu_inject(1, FU_PARITY, 0);
    TF_EQ(fu_peer_send(1, g_buf, 2), 2);
    uart_getinfo(1, &i);
    TF_EQ(i.parity, 2);
    fu_reset();
}

/* uart_rxerrs: the four counts summed, from the open on, mod 65536. */
static void test_rxerrs(void)
{
    u32 left;
    u16 n;

    begin();
    TF_EQ(uart_rxerrs(1), 0);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_rxerrs(1), 0);
    fill(g_buf, 40, 8);
    TF_EQ(fu_peer_send(1, g_buf, 40), 40);
    TF_EQ(get(1, BIG), 40);
    TF_EQ(uart_rxerrs(1), 0);
    fu_inject(1, FU_OVERRUN, 1);
    TF_EQ(fu_peer_send(1, g_buf, 1), 1);
    TF_EQ(uart_rxerrs(1), 1);
    fu_inject(1, FU_FRAMING, 2);
    TF_EQ(fu_peer_send(1, g_buf, 2), 2);
    TF_EQ(uart_rxerrs(1), 3);
    fu_inject(1, FU_PARITY, 3);
    TF_EQ(fu_peer_send(1, g_buf, 3), 3);
    TF_EQ(uart_rxerrs(1), 6);
    fu_inject(1, FU_LOST, 4);
    TF_EQ(fu_peer_send(1, g_buf, 4), 4);
    TF_EQ(uart_rxerrs(1), 10);
    TF_EQ(get(1, BIG), 5);
    TF_EQ(uart_rxerrs(1), 10);
    TF_EQ(fu_peer_send(1, g_buf, 40), 40);
    TF_EQ(get(1, BIG), 40);
    TF_EQ(uart_rxerrs(1), 10);

    /* Closed and opened again, the count starts from 0. */
    uart_close(1);
    TF_EQ(uart_rxerrs(1), 0);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_rxerrs(1), 0);

    /* 65535 errors in one count, one more wraps it to 0, three more
       give 3. */
    fu_inject(1, FU_OVERRUN, 65535u);
    for (left = 65535u; left > 0; left -= n) {
        n = left > BIG ? BIG : (u16)left;
        TF_EQ(fu_peer_send(1, g_buf, n), n);
    }
    TF_EQ(uart_rxerrs(1), 65535u);
    fu_inject(1, FU_LOST, 1);
    TF_EQ(fu_peer_send(1, g_buf, 1), 1);
    TF_EQ(uart_rxerrs(1), 0);
    fu_inject(1, FU_FRAMING, 3);
    TF_EQ(fu_peer_send(1, g_buf, 3), 3);
    TF_EQ(uart_rxerrs(1), 3);
    TF_EQ(get(1, BIG), 3);
    TF_EQ(uart_rxerrs(1), 3);
    fu_reset();
}

/* Loopback under RTS/CTS, a removed port's partner, a marked byte at a
   full ring, and RTS raised by a flush. */
static void test_edges(void)
{
    uart_info i;

    begin();
    fill(g_buf, BIG, 10);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_LOOP | UART_F_RTSCTS),
          UART_OK);
    TF_EQ(put(1, UART_TXSZ - 1), UART_TXSZ - 1);
    TF_EQ(uart_rxcount(1), UART_RXHIGH);
    TF_EQ(fu_isrstate(1) & FU_S_RTS, 0);
    TF_EQ(uart_txidle(1), 0);
    TF_EQ(get(1, UART_RXHIGH - UART_RXLOW - 1), UART_RXHIGH - UART_RXLOW - 1);
    TF_CHECK(memcmp(g_got, g_buf, UART_RXHIGH - UART_RXLOW - 1) == 0);
    TF_EQ(uart_rxcount(1), UART_RXLOW + 1);
    TF_EQ(get(1, UART_RXLOW + 1), UART_RXLOW + 1);
    TF_CHECK(memcmp(g_got, g_buf + UART_RXHIGH - UART_RXLOW - 1,
                    UART_RXLOW + 1) == 0);
    TF_EQ(uart_rxcount(1), UART_TXSZ - 1 - UART_RXHIGH);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(get(1, BIG), UART_TXSZ - 1 - UART_RXHIGH);
    TF_CHECK(memcmp(g_got, g_buf + UART_RXHIGH,
                    UART_TXSZ - 1 - UART_RXHIGH) == 0);
    TF_CHECK(wiped(1));

    /* No interrupts in loopback: only the port's own calls move it. */
    fu_rate(1, 10);
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(put(1, 25), 25);
    TF_EQ(uart_rxcount(2), 0);
    TF_EQ(uart_rxcount(1), 20);
    fu_rate(1, 0);
    uart_close(1);
    uart_close(2);

    fu_pair(1, 2);
    fu_port(2, 0);
    fu_port(2, UART_T_16450);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(put(1, 3), 3);
    TF_EQ(fu_peer_recv(1, g_got, BIG), 3);

    TF_EQ(fu_peer_send(1, g_buf, UART_RXSZ - 1), UART_RXSZ - 1);
    fu_inject(1, FU_FRAMING, 1);
    TF_EQ(fu_peer_send(1, g_buf, 1), 1);
    uart_getinfo(1, &i);
    TF_EQ(i.framing, 0);
    uart_rxflush(1);
    TF_EQ(uart_rxcount(1), 1);
    uart_getinfo(1, &i);
    TF_EQ(i.framing, 1);
    TF_EQ(i.lost, 0);
    uart_close(1);

    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    TF_EQ(fu_peer_send(1, g_buf, UART_RXHIGH + 32), UART_RXHIGH + 32);
    TF_EQ(fu_isrstate(1) & FU_S_RTS, 0);
    uart_rxflush(1);
    TF_EQ(fu_isrstate(1) & FU_S_RTS, FU_S_RTS);
    TF_EQ(uart_rxcount(1), 32);
    fu_reset();
}

static void pump(void *arg)
{
    if (g_inpump)
        g_nested = 1;
    g_inpump = 1;
    g_pumps++;
    g_pumpsz = uart_rxcount(*(u8 *)arg);
    g_inpump = 0;
}

static void test_pump(void)
{
    static u8 port = 2;
    uart_info i;

    begin();
    fu_pair(1, 2);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_OK);
    g_pumps = 0;
    g_nested = 0;
    fu_pump(pump, &port);
    TF_EQ(put(1, 4), 4);
    TF_EQ(g_pumpsz, 0);
    TF_EQ(uart_rxcount(1), 0);
    TF_EQ(g_pumpsz, 4);
    TF_EQ(uart_txidle(1), 1);
    TF_EQ(get(1, 1), 0);
    TF_EQ(g_pumps, 4);
    uart_rxflush(1);
    uart_getinfo(1, &i);
    TF_EQ(fu_peer_send(1, g_buf, 1), 1);
    TF_EQ(fu_peer_recv(1, g_got, 1), 0);
    TF_EQ(g_pumps, 4);
    TF_EQ(g_nested, 0);
    fu_pump(NULL, NULL);
    TF_EQ(uart_rxcount(1), 0);
    TF_EQ(g_pumps, 4);
    fu_reset();
}

int main(void)
{
    tf_begin("T_FAKEU");
    cmem_init();
    g_b = cmem_alloc(BIG);
    if (!TF_CHECK(g_b != NULL))
        return tf_done();
    test_open();
    test_peer();
    test_rate();
    test_pair();
    test_rtscts();
    test_inject();
    test_rxerrs();
    test_edges();
    test_pump();
    cmem_free(g_b);
    return tf_done();
}

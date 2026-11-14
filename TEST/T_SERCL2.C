/*
 * T_SERCL2.C - the second half of the T_SERCLI test (see T_SERCLI.C):
 * - the waits: a response that does not come makes the line lost
 *   (SERCLI_ELOST) after the wait given, each byte starting the wait
 *   again, and a line of SERCLI_MAXRESP bytes it cuts off is not
 *   delivered; a lost line sends nothing and reads nothing, and at its
 *   close, bye 1 or 0, puts CR LF, BYE and CR LF in the transmit ring
 *   without waiting for a response, then waits for them to go out at
 *   most SERCLI_BYEWAIT seconds (CTS down keeping them in a full ring);
 *   a transmit ring that stays full, CTS down,
 *   loses the line after SERCLI_SENDWAIT seconds, the copy still
 *   zeroed, and CTS rising in time lets the whole line go, also after
 *   stalls that last longer than SERCLI_SENDWAIT seconds together;
 * - the room of the transmit ring: a line longer than the ring, on a
 *   line that carries a byte a step, sent whole;
 * - the close: with bye 1 BYE is sent and every response due read,
 *   BYE's included; with bye 0 nothing is sent; a BYE never answered
 *   is waited for SERCLI_SENDWAIT seconds; the bytes still in the
 *   transmit ring go out before the port closes; the port closed with
 *   its set-up undone and nothing held;
 * - the rings: every byte that left them zeroed;
 * - receive errors (uart_rxerrs rising while a response arrives, of any
 *   of the four kinds, also in the middle of a response): the response
 *   returns SERCLI_ELOST and the line is lost as by a wait that ran out
 *   (nothing sent or read, a last CR LF and BYE at the close), while
 *   clean exchanges before it pass, also when the damaged bytes come
 *   only after the client has begun to wait; an error that arrived
 *   before the request was sent, between exchanges or right after the
 *   open, is not blamed on that exchange nor on any later one; noise
 *   that arrived while idle and stayed in the ring (framing or parity
 *   errors store their byte), for more than one read of the client,
 *   is dropped before the next request, whose response is exactly the
 *   server's, as is clean noise and an earlier client's answer to its
 *   last BYE; an error counted while that noise is dropped loses the
 *   exchange.
 * The waits of SERCLI_SENDWAIT are checked on the host's virtual clock
 * only.  It is a file of its own only to stay within the compiler
 * memory of Watcom C 6.5; T_SERCLI.H holds what both share.
 */
#define COMPAT_UART
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#define TF_PART
#define TF_FILE "T_SERCL2"
#include "TESTFW.H"
#include "FAKEUART.H"
#include "SERCLI.H"
#include "T_SERCLI.H"

#define W               2       /* seconds of the short waits timed     */
#define LONGLN          3000    /* bytes of the line longer than a ring */
#define RATE            1       /* bytes a step of a slow line carries  */
#define NOISE           600     /* bytes of idle noise: more than two
                                   reads of the client's receive chunk  */
#define LATE            3       /* pump call a late response arrives at */

/* Ticks a wait of secs seconds takes at least and, with a margin for
   the polls around it, at most; the host's timer counts one tick per
   call, and the client reads it once per poll, so a wait takes about
   that many pump calls. */
#define TICKS_MIN(secs) ((u16)((secs) * TMR_SEC))
#define TICKS_MAX(secs) ((u16)((secs) * (TMR_SEC + 1) + 4))

/* Pump calls of a stall of CTS shorter than SERCLI_SENDWAIT, and the
   pump call at which the first begins, once the ring is full. */
#define STALL           (TICKS_MIN(SERCLI_SENDWAIT) * 3 / 4)
#define STALLAT         20

static char g_line[LONGLN + 1];

/* g_line: LONGLN bytes of a request, every byte telling its place. */
static void long_line(void)
{
    u16 i;

    memcpy(g_line, "ITEM FIND ", 10);
    for (i = 10; i < LONGLN; i++)
        g_line[i] = (char)('a' + i % 26);
    g_line[LONGLN] = '\0';
}

/* 1 when the server received g_line and CR LF, and nothing else. */
static int got_line(void)
{
    return g_nrx == LONGLN + 2 && memcmp(g_rx, g_line, LONGLN) == 0 &&
           memcmp(g_rx + LONGLN, "\r\n", 2) == 0;
}

/* 1 when every byte of COM1's rings is zero. */
static int ringzero(void)
{
    cmem_blk *b;
    char buf[64];
    u16 off;
    u16 i;

    b = fu_ringblk(PORT);
    if (b == NULL)
        return 0;
    for (off = 0; off < UART_RXSZ + UART_TXSZ; off += sizeof buf) {
        cmem_get(b, off, buf, sizeof buf);
        for (i = 0; i < sizeof buf; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

#ifdef COMPAT_HOST
/* CTS down: the ring fills and the line is lost, unless CTS rises
   within SERCLI_SENDWAIT seconds of the last byte taken. */
static void cts_wait(void)
{
    sercli *s;
    int late;

    long_line();
    for (late = 1; late >= 0; late--) {
        fresh();
        s = NULL;
        TF_EQ(sercli_open(&s, PORT, UART_DEFBAUD, UART_F_RTSCTS),
              SERCLI_OK);
        fu_peer_rts(PORT, 0);
        srv_at(0, 0, NULL);
        if (!late)
            srv_at(TICKS_MIN(SERCLI_SENDWAIT) * 3 / 4, EV_RTSON, NULL);
        TF_EQ(sercli_send(s, g_line, LONGLN),
              late ? SERCLI_ELOST : SERCLI_OK);
        TF_CHECK(txwiped(s, LONGLN));
        if (late) {
            TF_CHECK(g_pumps >= TICKS_MIN(SERCLI_SENDWAIT));
            TF_CHECK(g_pumps <= TICKS_MAX(SERCLI_SENDWAIT));
        }
        srv_at(0, 0, NULL);
        sercli_close(s, 0);
        if (late) {
            TF_CHECK(g_pumps >= TICKS_MIN(SERCLI_BYEWAIT));
            TF_CHECK(g_pumps <= TICKS_MAX(SERCLI_BYEWAIT));
        }
        srv_run();
        TF_EQ(got_line(), !late);
        finish();
    }

    /* On a line of a byte a step, CTS down twice while the same piece
       of the line goes into the full ring, each time for less than
       SERCLI_SENDWAIT seconds but longer than that together: each byte
       taken starts the wait again. */
    fresh();
    s = NULL;
    TF_EQ(sercli_open(&s, PORT, UART_DEFBAUD, UART_F_RTSCTS), SERCLI_OK);
    fu_rate(PORT, 1);
    srv_at(0, 0, NULL);
    for (late = 0; late < 2; late++) {
        srv_at((u16)(STALLAT + late * (STALL + STALLAT)), EV_RTSOFF, NULL);
        srv_at((u16)(STALL + late * (STALL + STALLAT)), EV_RTSON, NULL);
    }
    TF_EQ(sercli_send(s, g_line, LONGLN), SERCLI_OK);
    TF_CHECK(g_pumps >= 2 * STALL);
    sercli_close(s, 0);
    srv_run();
    TF_CHECK(got_line());
    finish();
}
#endif

static void test_wait(void)
{
    sercli *s;

    fresh();
    s = connect();
    TF_EQ(sendl(s, "A"), SERCLI_OK);
    srv_at(0, 0, NULL);
    TF_EQ(resp(s, W), SERCLI_ELOST);
#ifdef COMPAT_HOST
    TF_CHECK(g_pumps >= TICKS_MIN(W) && g_pumps <= TICKS_MAX(W));
#endif
    srv_at(0, 0, NULL);
    TF_EQ(sendl(s, "B"), SERCLI_ELOST);
    TF_EQ(resp(s, W), SERCLI_ELOST);
    TF_EQ(g_pumps, 0);
    g_byeok = 1;
    sercli_close(s, 1);
    TF_CHECK(g_pumps <= TICKS_MAX(SERCLI_BYEWAIT));
    srv_run();
    TF_STREQ(g_rx, "A\r\n\r\nBYE\r\n");
    finish();

    /* With bye 0, as the link closes a lost line, the BYE goes all the
       same. */
    fresh();
    s = connect();
    TF_EQ(sendl(s, "A"), SERCLI_OK);
    srv_at(0, 0, NULL);
    TF_EQ(resp(s, W), SERCLI_ELOST);
    sercli_close(s, 0);
    srv_run();
    TF_STREQ(g_rx, "A\r\n\r\nBYE\r\n");
    finish();

    /* A line of SERCLI_MAXRESP bytes cut off by the wait is not
       delivered. */
    fresh();
    s = connect();
    TF_EQ(sendl(s, "A"), SERCLI_OK);
    memset(g_line, 'x', SERCLI_MAXRESP);
    g_line[SERCLI_MAXRESP] = '\0';
    srv_text("+DATA\r\n");
    srv_text(g_line);
    TF_EQ(resp(s, W), SERCLI_ELOST);
    TF_STREQ(g_ln, "+DATA|");
    sercli_close(s, 0);
    finish();

#ifdef COMPAT_HOST
    /* Each byte starts the wait again. */
    fresh();
    s = connect();
    TF_EQ(sendl(s, "A"), SERCLI_OK);
    srv_at(0, 0, NULL);
    srv_at(TICKS_MIN(W) * 3 / 4, EV_SEND, "+DATA\r\n");
    srv_at(TICKS_MIN(W) * 6 / 4, EV_SEND, "K 1\r\n");
    srv_at(TICKS_MIN(W) * 9 / 4, EV_SEND, ".\r\n");
    TF_EQ(resp(s, W), SERCLI_OK);
    TF_STREQ(g_ln, "+DATA|K 1|.|");
    sercli_close(s, 0);
    finish();
    cts_wait();
#endif
}

static void test_room(void)
{
    sercli *s;

    long_line();
    fresh();
    s = connect();
    fu_rate(PORT, RATE);
    TF_EQ(sercli_send(s, g_line, LONGLN), SERCLI_OK);
    TF_CHECK(txwiped(s, LONGLN));
    sercli_close(s, 0);
    srv_run();
    TF_CHECK(got_line());
    finish();
}

static void test_close(void)
{
    sercli *s;

    /* bye 1: BYE, then the responses due, BYE's included. */
    fresh();
    s = connect();
    g_byeok = 1;
    TF_EQ(sendl(s, "PING"), SERCLI_OK);
    srv_text("+DATA\r\nK 1\r\n.\r\n");
    sercli_close(s, 1);
    srv_run();
    TF_STREQ(g_rx, "PING\r\nBYE\r\n");
    TF_EQ(g_lines, 0);
    finish();

    /* bye 0: nothing sent. */
    fresh();
    s = connect();
    TF_EQ(sendl(s, "PING"), SERCLI_OK);
    srv_text("+OK\r\n");
    TF_EQ(resp(s, W), SERCLI_OK);
    TF_CHECK(ringzero());
    clear();
    sercli_close(s, 0);
    srv_run();
    TF_EQ(g_nrx, 0);
    finish();

    /* What the transmit ring holds goes before the port closes. */
    fresh();
    s = connect();
    fu_rate(PORT, 1);
    TF_EQ(sendl(s, "ITEM FIND ABCDEFGHIJKLMNOPQRSTUVWXYZ"), SERCLI_OK);
    sercli_close(s, 0);
    fu_rate(PORT, 0);
    srv_run();
    TF_STREQ(g_rx, "ITEM FIND ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n");
    finish();

#ifdef COMPAT_HOST
    /* A BYE never answered is waited for SERCLI_SENDWAIT seconds. */
    fresh();
    s = connect();
    srv_at(0, 0, NULL);
    sercli_close(s, 1);
    TF_CHECK(g_pumps >= TICKS_MIN(SERCLI_SENDWAIT));
    TF_CHECK(g_pumps <= TICKS_MAX(SERCLI_SENDWAIT) + 8);
    TF_STREQ(g_rx, "BYE\r\n");
    finish();
#endif
}

/* Send req, have the server answer text, and read it. */
static int trade(sercli *s, const char *req, const char *text)
{
    TF_EQ(sendl(s, req), SERCLI_OK);
    srv_text(text);
    return resp(s, W);
}

/* The line is lost: nothing is sent or read, and the close puts CR LF
   BYE CR LF after the request B in the ring. */
static void lost_line(sercli *s)
{
    srv_at(0, 0, NULL);
    TF_EQ(sendl(s, "C"), SERCLI_ELOST);
    TF_EQ(resp(s, W), SERCLI_ELOST);
    TF_EQ(g_pumps, 0);
    sercli_close(s, 0);
    srv_run();
    TF_STREQ(g_rx, "B\r\n\r\nBYE\r\n");
}

static void test_rxerr(void)
{
    static const u8 kinds[] = { FU_OVERRUN, FU_FRAMING, FU_PARITY, FU_LOST };
    sercli *s;
    u16 k;

    /* An error while the response arrives, of each kind. */
    for (k = 0; k < sizeof kinds; k++) {
        fresh();
        s = connect();
        TF_EQ(trade(s, "A", "+OK a\r\n"), SERCLI_OK);
        TF_STREQ(g_ln, "+OK a|");
        clear();
        fu_inject(PORT, kinds[k], 1);
        TF_EQ(trade(s, "B", "+OK b\r\n"), SERCLI_ELOST);
        lost_line(s);
        finish();
    }

    /* A response that arrives, damaged, only after the client waits:
       the error is seen at the read, not at the start of the wait. */
    for (k = 0; k < sizeof kinds; k++) {
        fresh();
        s = connect();
        clear();
        TF_EQ(sendl(s, "B"), SERCLI_OK);
        srv_at(0, 0, NULL);
        srv_at(LATE, EV_SEND, "+OK b\r\n");
        fu_inject(PORT, kinds[k], 1);
        TF_EQ(resp(s, W), SERCLI_ELOST);
        TF_CHECK(g_pumps >= LATE);
        lost_line(s);
        finish();
    }

    /* Noise that stayed in the ring while idle, longer than one read
       of the client, with framing or parity errors or with none: the
       response to the next request is the server's alone, and so is
       that of the exchange after it. */
    memset(g_line, 'x', NOISE);
    g_line[NOISE] = '\0';
    for (k = 0; k < 3; k++) {
        fresh();
        s = connect();
        if (k < 2)
            fu_inject(PORT, k == 0 ? FU_FRAMING : FU_PARITY, NOISE);
        srv_text(g_line);
        TF_EQ(trade(s, "A", "+OK a\r\n"), SERCLI_OK);
        TF_STREQ(g_ln, "+OK a|");
        TF_EQ(trade(s, "B", "+OK b\r\n"), SERCLI_OK);
        TF_STREQ(g_ln, "+OK a|+OK b|");
        clear();
        sercli_close(s, 0);
        srv_run();
        TF_EQ(g_nrx, 0);
        finish();
    }

    /* In the middle of a "+DATA" response, a byte of a data line. */
    fresh();
    s = connect();
    clear();
    TF_EQ(sendl(s, "B"), SERCLI_OK);
    srv_text("+DATA\r\n");
    fu_inject(PORT, FU_FRAMING, 1);
    srv_text("K 1\r\n.\r\n");
    TF_EQ(resp(s, W), SERCLI_ELOST);
    lost_line(s);
    finish();

    /* An error before the request is sent: between exchanges, and
       right after the open.  Dropped, so that the framing is whole;
       the exchange after it, and the one after that, pass. */
    fresh();
    s = connect();
    fu_inject(PORT, FU_OVERRUN, 1);
    srv_text("x");
    TF_EQ(trade(s, "A", "+OK a\r\n"), SERCLI_OK);
    TF_STREQ(g_ln, "+OK a|");
    fu_inject(PORT, FU_LOST, 2);
    srv_text("yz");
    TF_EQ(trade(s, "B", "+OK b\r\n"), SERCLI_OK);
    TF_STREQ(g_ln, "+OK a|+OK b|");
    TF_EQ(trade(s, "C", "+DATA\r\nK 1\r\n.\r\n"), SERCLI_OK);
    TF_STREQ(g_ln, "+OK a|+OK b|+DATA|K 1|.|");
    clear();
    sercli_close(s, 0);
    srv_run();
    TF_EQ(g_nrx, 0);
    finish();

    /* The answer to an earlier client's last BYE, still in the ring at
       the first request, does not lead its response. */
    fresh();
    s = connect();
    srv_text("+OK\r\n");
    TF_EQ(trade(s, "A", "+OK a\r\n"), SERCLI_OK);
    TF_STREQ(g_ln, "+OK a|");
    clear();
    sercli_close(s, 0);
    srv_run();
    finish();

    /* An error counted while the client drops idle noise, the count
       having been read before the drop: the exchange is lost, as the
       damaged byte may have stayed in the ring. */
    fresh();
    s = connect();
    clear();
    fu_inject(PORT, FU_FRAMING, 2);
    srv_text("x");
    srv_at(0, 0, NULL);
    srv_at(1, EV_SEND, "y");
    TF_EQ(trade(s, "B", "+OK b\r\n"), SERCLI_ELOST);
    lost_line(s);
    finish();
}

void test_sercl2(void)
{
    test_wait();
    test_room();
    test_close();
    test_rxerr();
}

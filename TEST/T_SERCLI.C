/*
 * T_SERCLI.C - unit test for the client's serial session (SERCLI.C)
 * over the simulated ports (FAKEUART.C).
 *
 * usage: T_SERCLI
 *
 * The client opens COM1 with RTS/CTS, as the server, playing by its
 * rules, sends no more than the receive ring holds.  Checks (with
 * T_SERCL2.C):
 * - opening: a port, baud rate or flag out of range (SERCLI_EARG), a
 *   missing port (SERCLI_ENOPORT), a port open already (SERCLI_EBUSY),
 *   no memory for the session, its block or the port's rings
 *   (SERCLI_ENOMEM), each with *out NULL, the port as it was and
 *   nothing held; an open sends nothing;
 * - requests: a line and CR LF reach the server whole, also one longer
 *   than the client's copy; a blank line (no request) is sent but
 *   waits for no response, an over-long one of blanks does; the copy
 *   zeroed after each line; no response waited for when none is due;
 * - the framing: one-line responses, "+DATA" up to the lone ".", dot
 *   stuffing left in place (".." and "..x" are data lines), LF without
 *   CR, a CR inside a line, responses split byte by byte and many in
 *   one burst, kept for the next request; a line longer than
 *   SERCLI_MAXRESP in pieces, a "." or empty piece after a full one
 *   ending nothing; a failing line callback (SERCLI_EOUTPUT, the rest of
 *   the response dropped, the next one whole);
 * - the waits, the room of the transmit ring, the close, the rings and
 *   the receive errors of T_SERCL2.C.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_UART
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKEUART.H"
#include "LINK.H"
#include "SERCLI.H"
#include "T_SERCLI.H"

#define EVMAX           8       /* events of srv_at                     */
#define BIGMAX          1400    /* the longest line built, NUL included */

u16 g_pumps;
char g_rx[RXMAX];
u16 g_nrx;
char g_ln[LNMAX];
u16 g_nln;
u16 g_lines;
u16 g_failat;
u8 g_byeok;
static u16 g_ev_at[EVMAX];
static u8 g_ev[EVMAX];
static const char *g_ev_s[EVMAX];
static u16 g_nev;
static u16 g_byes;              /* BYEs answered                        */
static u16 g_fnth;              /* the cmem_alloc call that fails       */
static u16 g_fcount;
static char g_big[BIGMAX];
static char g_exp[LNMAX];
#ifdef COMPAT_HOST
static u16 g_held;              /* heap blocks held at fresh            */
#endif

/* ---------------------------------------------------------------------
 * The scripted server.
 * --------------------------------------------------------------------- */

void srv_text(const char *s)
{
    u16 n;

    n = (u16)strlen(s);
    TF_EQ(fu_peer_send(PORT, s, n), n);
}

void srv_run(void)
{
    u16 i;
    char *b;

    g_nrx = (u16)(g_nrx + fu_peer_recv(PORT, g_rx + g_nrx,
                                       (u16)(RXMAX - 1 - g_nrx)));
    g_rx[g_nrx] = '\0';
    for (i = 0; i < g_nev; i++) {
        if (g_ev_at[i] != g_pumps)
            continue;
        if (g_ev[i] == EV_SEND)
            srv_text(g_ev_s[i]);
        else
            fu_peer_rts(PORT, g_ev[i] == EV_RTSON);
    }
    if (g_byeok) {
        for (i = 0, b = g_rx; (b = strstr(b, "BYE\r\n")) != NULL; b += 5)
            i++;
        for (; g_byes < i; g_byes++)
            srv_text("+OK\r\n");
    }
}

static void pump(void *arg)
{
    TF_CHECK(arg == NULL);
    g_pumps++;
    srv_run();
}

void srv_at(u16 at, u8 ev, const char *s)
{
    if (at == 0) {
        g_nev = 0;
        g_pumps = 0;
        return;
    }
    if (!TF_CHECK(g_nev < EVMAX))
        return;
    g_ev_at[g_nev] = at;
    g_ev[g_nev] = ev;
    g_ev_s[g_nev] = s;
    g_nev++;
}

void clear(void)
{
    g_nrx = 0;
    g_rx[0] = '\0';
    g_nln = 0;
    g_ln[0] = '\0';
    g_lines = 0;
    g_failat = 0;
}

void fresh(void)
{
    fu_reset();
    fu_port(PORT, UART_T_16550A);
    fu_pump(pump, NULL);
    clear();
    srv_at(0, 0, NULL);
    g_byeok = 0;
    g_byes = 0;
#ifdef COMPAT_HOST
    g_held = tm_held();
#endif
}

sercli *connect(void)
{
    sercli *s;

    s = NULL;
    TF_EQ(sercli_open(&s, PORT, UART_DEFBAUD, UART_F_RTSCTS), SERCLI_OK);
    TF_CHECK(s != NULL);
    return s;
}

void finish(void)
{
    fu_pump(NULL, NULL);
    TF_EQ(fu_isrstate(PORT), 0);
#ifdef COMPAT_HOST
    TM_HELD(g_held);
#endif
    fu_reset();
}

int take(void *arg, const char *line, u16 len)
{
    if (arg != NULL || line[len] != '\0' || len > SERCLI_MAXRESP)
        TF_CHECK(0);
    g_lines++;
    if (g_nln + len + 2 <= LNMAX) {
        memcpy(g_ln + g_nln, line, len);
        g_nln = (u16)(g_nln + len);
        g_ln[g_nln++] = '|';
        g_ln[g_nln] = '\0';
    }
    return g_lines == g_failat;
}

int sendl(sercli *s, const char *text)
{
    return sercli_send(s, text, (u16)strlen(text));
}

int resp(sercli *s, u16 waitsecs)
{
    return sercli_response(s, take, NULL, waitsecs);
}

int txwiped(sercli *s, u16 n)
{
    cmem_blk *b;
    char buf[32];
    u16 off;
    u16 i;

    b = *(cmem_blk **)s;
    if (n > cmem_size(b) / 2)
        n = cmem_size(b) / 2;
    for (off = 0; off < n; off += sizeof buf) {
        cmem_get(b, off, buf, sizeof buf);
        for (i = 0; i < sizeof buf && off + i < n; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * Opening.
 * --------------------------------------------------------------------- */

/* cmem_fault: fail the g_fnth-th allocation from the last arming. */
static int fail_nth(u16 size)
{
    TF_CHECK(size > 0);
    return ++g_fcount == g_fnth;
}

static void fail_alloc(u16 nth)
{
    g_fnth = nth;
    g_fcount = 0;
    cmem_fault = nth != 0 ? fail_nth : NULL;
}

/* sercli_open(port, baud, flags) refused with want, *out NULL, COM1 as
   it was and nothing more held. */
static void refused(u8 port, u16 baud, u8 flags, int want)
{
    sercli *s;
#ifdef COMPAT_HOST
    u16 h;
#endif
    u8 st;

#ifdef COMPAT_HOST
    h = tm_held();
#endif
    st = fu_isrstate(PORT);
    TF_EQ(sercli_open(&s, port, baud, flags), want);
    TF_CHECK(s == NULL);
    TF_EQ(fu_isrstate(PORT), st);
#ifdef COMPAT_HOST
    TM_HELD(h);
#endif
}

static void test_open(void)
{
    sercli *s;
    char buf[4];

    fresh();
    refused(0, UART_DEFBAUD, 0, SERCLI_EARG);
    refused(UART_MAXPORT + 1, UART_DEFBAUD, 0, SERCLI_EARG);
    refused(PORT, 9601, 0, SERCLI_EARG);
    refused(PORT, UART_DEFBAUD, UART_F_LOOP, SERCLI_EARG);
    refused(PORT, UART_DEFBAUD, UART_F_NOFIFO, SERCLI_EARG);
    refused(2, UART_DEFBAUD, 0, SERCLI_ENOPORT);
    TF_EQ(uart_open(PORT, UART_DEFBAUD, 0), UART_OK);
    refused(PORT, UART_DEFBAUD, 0, SERCLI_EBUSY);
    uart_close(PORT);
    fail_alloc(1);
    refused(PORT, UART_DEFBAUD, 0, SERCLI_ENOMEM);
    fail_alloc(2);
    refused(PORT, UART_DEFBAUD, 0, SERCLI_ENOMEM);
    fail_alloc(0);
#ifdef COMPAT_HOST
    tm_fail(1);
    refused(PORT, UART_DEFBAUD, 0, SERCLI_ENOMEM);
    tm_fail(0);
#endif
    s = NULL;
    TF_EQ(sercli_open(&s, PORT, 38400u, UART_F_RTSCTS), SERCLI_OK);
    TF_EQ(fu_isrstate(PORT) & FU_S_OPEN, FU_S_OPEN);
    srv_run();
    TF_EQ(g_nrx, 0);
    TF_EQ(fu_peer_recv(PORT, buf, sizeof buf), 0);
    sercli_close(s, 0);
    sercli_close(NULL, 1);
    finish();
}

/* ---------------------------------------------------------------------
 * Requests.
 * --------------------------------------------------------------------- */

/* g_big: the text head, then n copies of c, then tail. */
static u16 build(const char *head, char c, u16 n, const char *tail)
{
    u16 len;

    len = (u16)strlen(head);
    memcpy(g_big, head, len);
    memset(g_big + len, c, n);
    len = (u16)(len + n);
    strcpy(g_big + len, tail);
    return (u16)(len + strlen(tail));
}

static void test_send(void)
{
    sercli *s;
    u16 len;

    fresh();
    s = connect();
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_EQ(g_lines, 0);
    TF_EQ(sendl(s, "HELLO PC"), SERCLI_OK);
    TF_CHECK(txwiped(s, 8));
    srv_run();
    TF_STREQ(g_rx, "HELLO PC\r\n");
    srv_text("+OK\r\n");
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_STREQ(g_ln, "+OK|");

    /* Blank lines are sent and wait for nothing. */
    clear();
    TF_EQ(sendl(s, ""), SERCLI_OK);
    TF_EQ(sendl(s, " \t "), SERCLI_OK);
    srv_at(0, 0, NULL);
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_EQ(g_pumps, 0);
    srv_run();
    TF_STREQ(g_rx, "\r\n \t \r\n");
    len = build("", ' ', LINK_MAXLINE, "");
    TF_EQ(sercli_send(s, g_big, len), SERCLI_OK);
    srv_at(0, 0, NULL);
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_EQ(g_pumps, 0);
    srv_run();
    TF_EQ(g_nrx, 2 + 5 + len + 2);
    sercli_close(s, 0);
    finish();

    /* An over-long line of blanks is a request; a long line arrives
       whole. */
    fresh();
    s = connect();
    len = build("", '\t', LINK_MAXLINE + 1, "");
    TF_EQ(sercli_send(s, g_big, len), SERCLI_OK);
    srv_run();
    TF_EQ(g_nrx, len + 2);
    srv_text("-ERR 103 line too long\r\n");
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_STREQ(g_ln, "-ERR 103 line too long|");
    clear();
    len = build("ITEM FIND ", 'x', BIGMAX - 20, "!");
    TF_EQ(sercli_send(s, g_big, len), SERCLI_OK);
    TF_CHECK(txwiped(s, len));
    srv_run();
    TF_EQ(g_nrx, len + 2);
    TF_CHECK(memcmp(g_rx, g_big, len) == 0);
    TF_STREQ(g_rx + len, "\r\n");
    sercli_close(s, 0);
    finish();
}

/* ---------------------------------------------------------------------
 * The framing.
 * --------------------------------------------------------------------- */

/* Append to g_exp the pieces of at most SERCLI_MAXRESP bytes that the
   line s of len bytes is delivered in, each ended by '|'. */
static void pieces(const char *s, u16 len)
{
    u16 at;
    u16 n;

    at = (u16)strlen(g_exp);
    do {
        n = len > SERCLI_MAXRESP ? SERCLI_MAXRESP : len;
        memcpy(g_exp + at, s, n);
        at = (u16)(at + n);
        g_exp[at++] = '|';
        s += n;
        len = (u16)(len - n);
    } while (len > 0);
    g_exp[at] = '\0';
}

/* Send req, have the server answer with text, and read it. */
static int exchange(sercli *s, const char *req, const char *text)
{
    clear();
    if (sendl(s, req) != SERCLI_OK)
        return -1;
    srv_text(text);
    return resp(s, 1);
}

static void test_frame(void)
{
    sercli *s;

    fresh();
    s = connect();
    TF_EQ(exchange(s, "A", "+DATA\r\nK 1\r\n..\r\n..x\r\n\r\n.\r\n"),
          SERCLI_OK);
    TF_STREQ(g_ln, "+DATA|K 1|..|..x||.|");
    TF_EQ(exchange(s, "B", "-ERR 200 no\n"), SERCLI_OK);
    TF_STREQ(g_ln, "-ERR 200 no|");
    TF_EQ(exchange(s, "C", "+OK a\rb\r\n"), SERCLI_OK);
    TF_STREQ(g_ln, "+OK a\rb|");

    /* Byte by byte. */
    fu_rate(PORT, 1);
    TF_EQ(exchange(s, "D", "+DATA\r\nR 1\r\n.\r\n"), SERCLI_OK);
    TF_STREQ(g_ln, "+DATA|R 1|.|");
    fu_rate(PORT, 0);

    /* Three requests, their responses in one burst. */
    clear();
    TF_EQ(sendl(s, "E"), SERCLI_OK);
    TF_EQ(sendl(s, "F"), SERCLI_OK);
    TF_EQ(sendl(s, "G"), SERCLI_OK);
    srv_text("+OK e\r\n+DATA\r\n.\r\n+OK g\r\n");
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_STREQ(g_ln, "+OK e|");
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_STREQ(g_ln, "+OK e|+DATA|.|");
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_STREQ(g_ln, "+OK e|+DATA|.|+OK g|");
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_EQ(g_lines, 4);

    /* Over-long lines in pieces; a "." or nothing after a full piece
       is the rest of the line. */
    clear();
    TF_EQ(sendl(s, "H"), SERCLI_OK);
    srv_text("+DATA\r\n");
    build("M ", 'm', 2 * SERCLI_MAXRESP - 2, "\r\n");
    srv_text(g_big);
    build("M ", 'm', SERCLI_MAXRESP - 2, ".\r\n.\r\n");
    srv_text(g_big);
    TF_EQ(resp(s, 1), SERCLI_OK);
    strcpy(g_exp, "+DATA|");
    build("M ", 'm', 2 * SERCLI_MAXRESP - 2, "");
    pieces(g_big, (u16)(2 * SERCLI_MAXRESP));
    build("M ", 'm', SERCLI_MAXRESP - 2, "");
    pieces(g_big, SERCLI_MAXRESP);
    strcat(g_exp, ".|.|");
    TF_STREQ(g_ln, g_exp);

    /* A failing callback: the rest dropped, the next response whole. */
    clear();
    TF_EQ(sendl(s, "I"), SERCLI_OK);
    TF_EQ(sendl(s, "J"), SERCLI_OK);
    srv_text("+DATA\r\nK 1\r\nK 2\r\n.\r\n+OK j\r\n");
    g_failat = 2;
    TF_EQ(resp(s, 1), SERCLI_EOUTPUT);
    TF_STREQ(g_ln, "+DATA|K 1|");
    g_failat = 0;
    TF_EQ(resp(s, 1), SERCLI_OK);
    TF_STREQ(g_ln, "+DATA|K 1|+OK j|");
    sercli_close(s, 0);
    finish();
}

int main(void)
{
    cmem_init();
    tf_begin("T_SERCLI");
    test_open();
    test_send();
    test_frame();
    test_sercl2();
    return tf_done();
}

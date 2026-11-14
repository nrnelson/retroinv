/*
 * T_TSER.C - the server's serial transport (TSERIAL.H) over the
 * simulated ports (FAKEUART.H), the real CONN and the engine.
 *
 * usage: T_TSER dir
 *
 * dir is a data directory prefix ("OBJ/HOST/TSERT/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files there are removed at the start and at the end.  This
 * file holds the helpers, main and these phases:
 * - start-up: ports out of range, none, or the transport started
 *   already (TSER_EARG), a baud rate uart_open refuses (TSER_EARG, *bad
 *   the port), a missing port (TSER_ENOPORT), a port open already
 *   (TSER_EBUSY), no memory for the input buffers (*bad 0) or for a
 *   port's rings (TSER_ENOMEM); every failure leaves every port closed
 *   and nothing held;
 * - before tserial_start and after tserial_stop, tserial_poll does
 *   nothing and tserial_stop is harmless; tserial_info gives the ports
 *   served in port order, uart_getinfo's base, IRQ and chip, and
 *   TSER_EARG past the last;
 * - the first byte: a served port claims a slot and listens, no
 *   session open and nothing sent, until a byte arrives; the session
 *   then opens with no station, HELLO names it, and ADMIN CLIENTS shows
 *   the kind COM1 or COM2;
 * - stop: a live session's slot is given back (CONN_EV_CLOSE) and the
 *   port closed, its interrupt set-up undone;
 * - hang-up: a logged-in session whose caller's DSR and DCD (DCD alone
 *   too) were up ends once both have been down TSER_HANGUP ticks, not a
 *   tick sooner, and a drop shorter than that ends nothing; the port
 *   listens again, its rings zeroed, a response CTS held back and the
 *   input CONN still held dropped, and the next session starts logged
 *   out, unarmed and reading; on a line whose DSR and DCD never rose
 *   the session stays, logged in, however long.
 * The other phases, and what they check, are in T_TSER2.C.  Prints a
 * summary line "T_TSER PASS n"; exit status 0 when every check passes,
 * else 1.
 */
#define COMPAT_UART
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TSERIAL.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKEUART.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "T_TSER.H"

const char k_ok[] = "+OK\r\n";
u32 g_now = 1000;
char *g_rep;
u16 g_replen;
u16 g_opens;
u16 g_closes;
char g_station[ENG_STALEN + 1];
eng *g_e;

static const eng_time g_t0 = { 2026, 10, 8, 12, 0, 0 };
static u16 g_held;              /* heap blocks held when a phase began:
                                   on the host, far blocks too          */
static u8 g_pwset;              /* ADMIN's password is PW               */
static u16 g_fnth;              /* the cmem_alloc call that fails       */
static u16 g_fcount;

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

static void on_event(u8 c, u8 ev, const char *text)
{
    TF_CHECK(c < CONN_MAX);
    if (ev == CONN_EV_OPEN) {
        g_opens++;
        strncpy(g_station, text, ENG_STALEN);
        g_station[ENG_STALEN] = '\0';
    } else if (ev == CONN_EV_CLOSE) {
        g_closes++;
    }
}

/* cmem_fault: fail the g_fnth-th allocation from the last arming
   (fail_alloc). */
static int fail_nth(u16 size)
{
    TF_CHECK(size > 0);
    return ++g_fcount == g_fnth;
}

/* Make the nth cmem_alloc from now on fail, 0 none. */
void fail_alloc(u16 nth)
{
    g_fnth = nth;
    g_fcount = 0;
    cmem_fault = nth != 0 ? fail_nth : NULL;
}

/* Fresh ports, COM1 a 16550A and COM2 a 16450, and a table with a slot
   for each, the idle timeout idle at g_now. */
static void fresh(u32 idle)
{
    fu_reset();
    fu_port(1, UART_T_16550A);
    fu_port(2, UART_T_16450);
    g_held = tm_held();
    TF_EQ(conn_init(g_e, NULL, CONN_SERMAX, idle, g_now), 0);
    conn_watch(on_event);
    g_opens = 0;
    g_closes = 0;
}

/* fresh, then the transport started on ports with flags. */
void begin(u8 ports, u8 flags, u32 idle)
{
    u8 bad;

    fresh(idle);
    bad = 9;
    TF_EQ(tserial_start(ports, UART_DEFBAUD, flags, &bad), TSER_OK);
    TF_EQ(bad, 0);
}

/* Stop the transport and check that it left nothing behind, then free
   the table and check that the heap holds what it held at begin. */
void finish(void)
{
    u8 c;
    u8 p;

    tserial_stop();
    for (p = 1; p <= UART_MAXPORT; p++)
        TF_EQ(fu_isrstate(p), 0);
    TF_EQ(tserial_poll(g_now), 0);
    tserial_stop();
    for (c = 0; c < CONN_MAX; c++)
        TF_EQ(conn_state(c), CONN_FREE);
    TF_EQ(g_closes, g_opens);
    conn_free();
    TM_HELD(g_held);
    fu_reset();
}

/* One pass of the server's loop at g_now; what tserial_poll returned. */
u16 pass(void)
{
    u16 n;

    n = tserial_poll(g_now);
    conn_service(g_now);
    conn_timeouts(g_now);
    return n;
}

void run(u16 n)
{
    while (n-- > 0)
        pass();
}

/* Send text, without its NUL, from the remote station on port. */
void put(u8 port, const char *text)
{
    u16 n;

    n = (u16)strlen(text);
    TF_EQ(fu_peer_send(port, text, n), n);
}

/* 1 when s is one whole response: a line, or "+DATA" up to its ".". */
static int whole(const char *s, u16 n)
{
    if (n < 2 || s[n - 2] != '\r' || s[n - 1] != '\n')
        return 0;
    if (strncmp(s, "+DATA", 5) != 0)
        return 1;
    return n >= 5 && strcmp(s + n - 5, "\r\n.\r\n") == 0;
}

/* Run the server and take what port sends into g_rep until it holds
   one whole response, LOOPS passes have gone or REPSZ bytes have come.
   Returns g_replen. */
u16 reply(u8 port)
{
    u16 i;

    g_replen = 0;
    g_rep[0] = '\0';
    for (i = 0; i < LOOPS && g_replen < REPSZ; i++) {
        pass();
        g_replen = (u16)(g_replen + fu_peer_recv(port, g_rep + g_replen,
                                                 (u16)(REPSZ - g_replen)));
        g_rep[g_replen] = '\0';
        if (whole(g_rep, g_replen))
            break;
    }
    return g_replen;
}

/* Send line and CR LF from the remote station on port: the response. */
const char *ask(u8 port, const char *line)
{
    char buf[PROTO_MAXLINE + 3];

    strcpy(buf, line);
    strcat(buf, "\r\n");
    put(port, buf);
    reply(port);
    return g_rep;
}

/* The first request on the listening port: HELLO opens the session,
   with no station until HELLO names it. */
void hello(u8 port, const char *station)
{
    char line[PROTO_MAXLINE + 1];
    u16 opens;

    opens = g_opens;
    strcpy(line, "HELLO ");
    strcat(line, station);
    TF_STREQ(ask(port, line), k_ok);
    TF_EQ(g_opens, opens + 1);
    TF_STREQ(g_station, "");
}

/* Log in as ADMIN on port, which has said HELLO; the first time, the
   password is changed to PW. */
void login(u8 port)
{
    if (g_pwset) {
        TF_CHECK(tf_starts(ask(port, "LOGIN ADMIN " PW), "+OK LEVEL=3"));
        return;
    }
    TF_STREQ(ask(port, "LOGIN ADMIN admin"), "+OK LEVEL=3 MUSTCHG=1\r\n");
    TF_STREQ(ask(port, "PASSWD admin " PW), k_ok);
    g_pwset = 1;
}

/* Number of slots in the CONN_ state state. */
u16 nstate(u8 state)
{
    u16 n;
    u8 c;

    n = 0;
    for (c = 0; c < CONN_MAX; c++)
        if (conn_state(c) == state)
            n++;
    return n;
}

/* 1 when every byte of the open port's rings is zero: whatever left
   them was wiped, and nothing waits in them. */
int ringzero(u8 port)
{
    cmem_blk *b;
    char buf[64];
    u16 off;
    u16 i;

    b = fu_ringblk(port);
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

/* ---------------------------------------------------------------------
 * Start-up, the first byte, info and stop.
 * --------------------------------------------------------------------- */

/* tserial_start(ports, baud, 0) refused with want and *bad, leaving
   COM1 closed, COM2 as it was and nothing more held. */
static void refused(u8 ports, u16 baud, int want, u8 bad)
{
    u16 h;
    u8 s2;
    u8 b;

    h = tm_held();
    s2 = fu_isrstate(2);
    b = 9;
    TF_EQ(tserial_start(ports, baud, 0, &b), want);
    TF_EQ(b, bad);
    TF_EQ(fu_isrstate(1), 0);
    TF_EQ(fu_isrstate(2), s2);
    TF_EQ(tserial_poll(g_now), 0);
    TM_HELD(h);
}

static void test_start(void)
{
    uart_info ui;
    u8 port;
    u8 b;

    fresh(0);
    tserial_stop();
    TF_EQ(tserial_poll(g_now), 0);
    TF_EQ(tserial_info(0, &port, &ui), TSER_EARG);
    refused(0, UART_DEFBAUD, TSER_EARG, 0);
    refused(4, UART_DEFBAUD, TSER_EARG, 0);
    refused(BOTH, 9601, TSER_EARG, 1);
    fu_port(2, 0);
    refused(BOTH, UART_DEFBAUD, TSER_ENOPORT, 2);
    fu_port(2, UART_T_8250);
    TF_EQ(uart_open(2, UART_DEFBAUD, 0), UART_OK);
    refused(2, UART_DEFBAUD, TSER_EBUSY, 2);
    uart_close(2);
    fail_alloc(1);
    refused(BOTH, UART_DEFBAUD, TSER_ENOMEM, 0);
    fail_alloc(2);
    refused(BOTH, UART_DEFBAUD, TSER_ENOMEM, 1);
    fail_alloc(3);
    refused(BOTH, UART_DEFBAUD, TSER_ENOMEM, 2);
    fail_alloc(0);
    b = 9;
    TF_EQ(tserial_start(2, 1200, UART_F_RTSCTS, &b), TSER_OK);
    TF_EQ(b, 0);
    TF_EQ(fu_isrstate(1), 0);
    TF_EQ(fu_isrstate(2) & FU_S_OPEN, FU_S_OPEN);
    TF_EQ(tserial_start(1, UART_DEFBAUD, 0, &b), TSER_EARG);
    TF_EQ(fu_isrstate(1), 0);
    TF_EQ(tserial_info(0, &port, &ui), TSER_OK);
    TF_EQ(port, 2);
    TF_EQ(ui.base, 0x2F8);
    TF_EQ(ui.irq, 3);
    TF_EQ(ui.chip, UART_T_8250);
    TF_EQ(tserial_info(1, &port, &ui), TSER_EARG);
    finish();

    begin(BOTH, 0, 0);
    TF_EQ(tserial_info(0, &port, &ui), TSER_OK);
    TF_EQ(port, 1);
    TF_EQ(ui.base, 0x3F8);
    TF_EQ(ui.irq, 4);
    TF_EQ(ui.chip, UART_T_16550A);
    TF_EQ(tserial_info(1, &port, &ui), TSER_OK);
    TF_EQ(port, 2);
    TF_EQ(ui.chip, UART_T_16450);
    TF_EQ(tserial_info(2, &port, &ui), TSER_EARG);
    finish();
}

/* A port listens until its first byte; the session opens with no
   station and HELLO names it. */
static void test_first(void)
{
    proto_peer p;
    char buf[8];

    begin(1, 0, 0);
    TF_EQ(nstate(CONN_LISTEN), 0);
    TF_EQ(pass(), 1);
    TF_EQ(nstate(CONN_LISTEN), 1);
    run(4);
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_EQ(g_opens, 0);
    TF_EQ(fu_peer_recv(1, buf, sizeof buf), 0);
    put(1, "H");
    pass();
    TF_EQ(g_opens, 1);
    TF_STREQ(g_station, "");
    TF_EQ(nstate(CONN_OPEN), 1);
    TF_STREQ(ask(1, "ELLO PC1"), k_ok);
    TF_EQ(conn_info(0, &p), 0);
    TF_STREQ(p.kind, "COM1");
    TF_STREQ(p.station, "PC1");
    login(1);
    TF_STREQ(ask(1, "ADMIN CLIENTS"),
             "+DATA\r\nF SLOT TYPE STATION USER LEVEL STATE IDLE SELF\r\n"
             "R 1 COM1 PC1 ADMIN 3 3 0 1\r\n.\r\n");
    TF_CHECK(ringzero(1));
    finish();
}

/* tserial_stop gives a live session's slot back and closes its port. */
static void test_stop(void)
{
    begin(BOTH, 0, 0);
    pass();
    hello(2, "PC2");
    TF_EQ(nstate(CONN_OPEN), 1);
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_EQ(fu_isrstate(1),
          FU_S_OPEN | FU_S_IER | FU_S_IRQ | FU_S_VECT | FU_S_RTS);
    tserial_stop();
    TF_EQ(g_closes, 1);
    TF_EQ(nstate(CONN_FREE), CONN_MAX);
    finish();
}

/* The caller on COM1 logs in with lines (UART_M_ bits) up, which then
   drop for TSER_HANGUP - 1 ticks and rise again, then drop for good:
   the session ends TSER_HANGUP ticks after the second drop. */
static void hangup(u8 lines)
{
    begin(1, 0, 0);
    pass();
    fu_modem(1, lines);
    hello(1, "PC1");
    login(1);
    fu_modem(1, 0);
    pass();
    g_now += TSER_HANGUP - 1;
    pass();
    fu_modem(1, lines);
    pass();
    fu_modem(1, 0);
    pass();
    g_now += TSER_HANGUP - 1;
    pass();
    TF_EQ(g_closes, 0);
    g_now++;
    pass();
    TF_EQ(g_closes, 1);
    pass();
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_CHECK(ringzero(1));
    TF_CHECK(tf_starts(ask(1, "STATUS"), "-ERR 400 "));
    TF_EQ(g_opens, 2);
    /* The next session starts unarmed: its lines, down, end nothing. */
    g_now += 2 * TSER_HANGUP;
    run(2);
    TF_EQ(g_closes, 1);
    TF_EQ(nstate(CONN_OPEN), 1);
    finish();
}

static void test_hangup(void)
{
    char buf[8];

    hangup(UART_M_DSR | UART_M_DCD);
    hangup(UART_M_DCD);

    /* A response the caller's CTS held back, and the requests CONN
       still held, go with the session: CONN answers one line a pass,
       and the hang-up comes on the third.  The next session reads its
       first request. */
    begin(1, UART_F_RTSCTS, 0);
    pass();
    fu_modem(1, UART_M_DSR | UART_M_DCD);
    hello(1, "PC1");
    fu_peer_rts(1, 0);
    put(1, "PING\r\nPING\r\nPING\r\nPING\r\n");
    pass();
    fu_modem(1, 0);
    pass();
    g_now += TSER_HANGUP;
    pass();
    TF_EQ(g_closes, 1);
    fu_peer_rts(1, 1);
    run(4);
    TF_EQ(fu_peer_recv(1, buf, sizeof buf), 0);
    TF_CHECK(tf_starts(ask(1, "STATUS"), "-ERR 400 "));
    TF_CHECK(ringzero(1));
    finish();

    /* A line of three wires: no hang-up, the session kept. */
    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    login(1);
    g_now += 100 * TSER_HANGUP;
    run(4);
    TF_EQ(g_closes, 0);
    TF_CHECK(tf_starts(ask(1, "STATUS"), "+DATA"));
    finish();
}

int main(int argc, char **argv)
{
    char why[TFS_WHYSZ];

    cmem_init();
    tf_begin("T_TSER");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    g_rep = (char *)malloc(REPSZ + 1);
    why[0] = '\0';
    if (!TF_CHECK(g_rep != NULL) ||
        !TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return tf_done();
    }
    test_start();
    test_first();
    test_stop();
    test_hangup();
    test_tser2();
    eng_close(g_e);
    te_remove(tfs_dir(), NULL);
    free(g_rep);
    return tf_done();
}

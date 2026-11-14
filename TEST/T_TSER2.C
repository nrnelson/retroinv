/*
 * T_TSER2.C - the second half of the T_TSER test (see T_TSER.C):
 * - the ends of a session: after BYE's response has gone into the
 *   transmit ring the receive ring is flushed, so a request sent after
 *   BYE opens nothing, and the port listens again, its next session
 *   new; the third failed LOGIN; the idle timeout, also of a line that
 *   carried only noise; a request after BYE in the same input dropped,
 *   and the next session's input read;
 * - the drain: a session's last output held up by CTS keeps the slot
 *   CONN_SHUTWAIT ticks after the close and no longer, and is then
 *   dropped, the port opened again with its flags, nothing of it sent
 *   once CTS rises;
 *   a port that does not open again stays closed and opens nothing;
 * - flow control: at most TSER_CHUNK bytes handed to CONN per pass,
 *   the next only after rx_done, every pipelined request answered in
 *   order; under UART_F_RTSCTS a sender held back by RTS loses nothing;
 * - pacing: a response longer than the transmit ring sent whole, byte
 *   for byte as without a rate limit, on a line that carries 16 bytes a
 *   step; an idle timeout in the pass that put output into the ring,
 *   CTS having held it up for the idle time, leaves the next session
 *   none of it, nor its count;
 * - the rings: every byte that left them zeroed; overruns, framing
 *   errors and lost bytes counted in tserial_info, and a burst longer
 *   than the receive ring held back, not lost;
 * - receive errors: a request line that arrives while the port's
 *   uart_rxerrs count rises, of any of the four kinds, is not carried
 *   out (USER ADD leaves no user) and is not answered; the session
 *   ends as on a hang-up (CONN_EV_CLOSE, the port listens again, its
 *   rings zeroed) and the next one starts logged out; a clean request
 *   before it is carried out, and so is one in the next session; a
 *   line whose damaged bytes and ending come in different passes is
 *   not carried out either; the port's error counts stay for tserial_info
 *   when the session ends (its rings are flushed, the port not opened
 *   again); the password of a damaged LOGIN, whole or in part, stays
 *   in no block of memory (on the host); output the old session had
 *   put into the transmit ring is not the next session's, nor its
 *   count; a hang-up after such a session, which opens the port again
 *   with its counts at 0, leaves the next session's first request
 *   served;
 * - IMPORT not refused for a port that only listens, refused (402)
 *   once the other port's session is open;
 * - two ports at once, each with its own session, kind and station in
 *   ADMIN CLIENTS;
 * - shutdown: ADMIN SHUTDOWN on one port, 501 to the next request on
 *   the other, both slots given back, no port listening again, and a
 *   LISTEN ended by conn_shutdown.
 * It is a file of its own only to stay within the compiler memory of
 * Watcom C 6.5; T_TSER.H holds what both share.
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
#define TF_PART
#define TF_MALLOC
#define TF_FILE "T_TSER2"
#include "TESTFW.H"
#include "FAKEUART.H"
#include "T_TSER.H"

#define RATE            16      /* bytes a step of a slow line carries  */
#define NPING           100     /* pipelined requests                   */
#define BURST           1500    /* bytes sent at a full receive ring    */
#define NLIST           12      /* items of the long list               */
#define SECRET          "Zq7xKw9"       /* a password sent damaged      */

static const char k_ping[] = "PING\r\n";
static const char k_import[] = "IMPORT ITEMS FILE=NOFILE.CSV";

/* Number of times "+OK" CR LF occurs in the n bytes of s. */
static u16 oks(const char *s, u16 n)
{
    u16 i;
    u16 k;

    k = 0;
    for (i = 0; i + 5 <= n; i++)
        if (memcmp(s + i, k_ok, 5) == 0)
            k++;
    return k;
}

/* Run until the open session ends and the port listens again: 1 then,
   else 0 after LOOPS passes. */
static int relisten(u16 closes)
{
    u16 i;

    for (i = 0; i < LOOPS; i++) {
        pass();
        if (g_closes > closes && nstate(CONN_LISTEN) == 1 &&
            nstate(CONN_OPEN) + nstate(CONN_CLOSING) == 0)
            return 1;
    }
    return 0;
}

static void test_bye(void)
{
    char buf[16];
    u16 n;

    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    put(1, "BYE\r\n");
    pass();
    put(1, k_ping);
    TF_CHECK(relisten(0));
    n = fu_peer_recv(1, buf, sizeof buf - 1);
    buf[n] = '\0';
    TF_STREQ(buf, k_ok);
    run(4);
    TF_EQ(g_opens, 1);
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_CHECK(ringzero(1));
    TF_CHECK(tf_starts(ask(1, "STATUS"), "-ERR 400 "));
    TF_EQ(g_opens, 2);

    /* A request after BYE in the same input: dropped with it, and the
       next session reads its own. */
    put(1, "BYE\r\nPING\r\n");
    TF_CHECK(relisten(1));
    n = fu_peer_recv(1, buf, sizeof buf - 1);
    buf[n] = '\0';
    TF_STREQ(buf, k_ok);
    hello(1, "PC2");
    finish();
}

static void test_fails(void)
{
    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    TF_CHECK(tf_starts(ask(1, "LOGIN ADMIN WRONG1"), "-ERR 401 "));
    TF_CHECK(tf_starts(ask(1, "LOGIN ADMIN WRONG2"), "-ERR 401 "));
    TF_EQ(g_closes, 0);
    TF_CHECK(tf_starts(ask(1, "LOGIN ADMIN WRONG3"), "-ERR 401 "));
    TF_CHECK(relisten(0));
    finish();
}

static void test_idle(void)
{
    begin(1, 0, 10 * TMR_SEC);
    pass();
    put(1, "noise");
    run(2);
    TF_EQ(g_opens, 1);
    g_now += 10 * TMR_SEC - 1;
    run(2);
    TF_EQ(g_closes, 0);
    g_now++;
    TF_CHECK(relisten(0));
    hello(1, "PC1");
    login(1);
    /* The pass that reports the response sent starts the idle time. */
    pass();
    g_now += 10 * TMR_SEC;
    TF_CHECK(relisten(1));
    finish();
}

static void test_drain(void)
{
    char buf[16];
    u16 n;

    begin(1, UART_F_RTSCTS, 0);
    pass();
    hello(1, "PC1");
    fu_peer_rts(1, 0);
    put(1, "BYE\r\n");
    run(4);
    TF_EQ(g_closes, 0);
    TF_EQ(nstate(CONN_CLOSING), 1);
    g_now += CONN_SHUTWAIT - 1;
    run(2);
    TF_EQ(nstate(CONN_CLOSING), 1);
    g_now++;
    pass();
    TF_EQ(g_closes, 1);
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_EQ(fu_isrstate(1) & FU_S_OPEN, FU_S_OPEN);
    fu_peer_rts(1, 1);
    run(2);
    TF_EQ(fu_peer_recv(1, buf, sizeof buf), 0);
    TF_CHECK(ringzero(1));
    hello(1, "PC2");
    /* Opened again with RTS/CTS still. */
    fu_peer_rts(1, 0);
    put(1, "PING\r\n");
    run(4);
    TF_EQ(fu_peer_recv(1, buf, sizeof buf), 0);
    fu_peer_rts(1, 1);
    reply(1);
    TF_STREQ(g_rep, k_ok);
    finish();

    /* A port that does not open again stays closed. */
    begin(1, UART_F_RTSCTS, 0);
    pass();
    hello(1, "PC1");
    fu_peer_rts(1, 0);
    put(1, "BYE\r\n");
    run(4);
    g_now += CONN_SHUTWAIT;
    fail_alloc(1);
    pass();
    fail_alloc(0);
    TF_EQ(g_closes, 1);
    TF_EQ(fu_isrstate(1), 0);
    fu_peer_rts(1, 1);
    put(1, "H");
    run(2);
    TF_EQ(g_opens, 1);
    n = fu_peer_recv(1, buf, sizeof buf);
    TF_EQ(n, 0);
    finish();
}

/* Send NPING pipelined PINGs on port 1, already listening: the first
   pass hands CONN TSER_CHUNK bytes; every PING is answered. */
static void pings(void)
{
    u16 i;

    for (i = 0; i < NPING; i++)
        put(1, k_ping);
    TF_EQ(tserial_poll(g_now), 1 + TSER_CHUNK);
    g_replen = 0;
    for (i = 0; i < LOOPS && g_replen < NPING * 5; i++) {
        pass();
        g_replen = (u16)(g_replen + fu_peer_recv(1, g_rep + g_replen,
                                                 (u16)(REPSZ - g_replen)));
    }
    TF_EQ(g_replen, NPING * 5);
    TF_EQ(oks(g_rep, g_replen), NPING);
}

static void test_flow(void)
{
    uart_info ui;
    u8 port;
    u16 i;

    begin(1, 0, 0);
    pass();
    pings();
    TF_CHECK(ringzero(1));
    finish();

    /* A burst at a full ring under RTS/CTS: held back, none lost. */
    begin(1, UART_F_RTSCTS, 0);
    pass();
    for (i = 0; i < BURST / 6; i++)
        put(1, k_ping);
    TF_EQ(tserial_info(0, &port, &ui), TSER_OK);
    TF_EQ(ui.lost, 0);
    g_replen = 0;
    for (i = 0; i < LOOPS && g_replen < BURST / 6 * 5; i++) {
        pass();
        g_replen = (u16)(g_replen + fu_peer_recv(1, g_rep + g_replen,
                                                 (u16)(REPSZ - g_replen)));
    }
    TF_EQ(oks(g_rep, g_replen), BURST / 6);
    TF_EQ(tserial_info(0, &port, &ui), TSER_OK);
    TF_EQ(ui.lost, 0);
    finish();
}

/* The request whose response holds NLIST long items, into line. */
static void list_req(char *line)
{
    sprintf(line, "ITEM LIST FIELDS=ITEM_ID,MFR,MODEL,DESCR PAGE=%d", NLIST);
}

/* Append " key=" and n copies of c to line. */
static void wide(char *line, const char *key, char c, u16 n)
{
    size_t at;

    strcat(line, key);
    at = strlen(line);
    memset(line + at, c, n);
    line[at + n] = '\0';
}

static void test_pace(void)
{
    char line[PROTO_MAXLINE + 1];
    char list[PROTO_MAXLINE + 1];
    char *fast;
    u16 nfast;
    u16 i;

    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    login(1);
    strcpy(line, "ITEM ADD CATEGORY=SYS");
    wide(line, " MFR=", 'M', 30);
    wide(line, " MODEL=", 'O', 30);
    wide(line, " DESCR=", 'D', 60);
    TF_CHECK(tf_starts(ask(1, line), "+OK ID="));
    for (i = 1; i < NLIST; i++)
        TF_CHECK(tf_starts(ask(1, "ITEM COPY 1"), "+OK ID="));
    list_req(list);
    ask(1, list);
    nfast = g_replen;
    TF_CHECK(nfast > UART_TXSZ);
    fast = (char *)malloc(nfast + 1u);
    if (TF_CHECK(fast != NULL)) {
        memcpy(fast, g_rep, nfast);
        fu_rate(1, RATE);
        ask(1, list);
        TF_EQ(g_replen, nfast);
        TF_CHECK(memcmp(g_rep, fast, nfast) == 0);
        free(fast);
    }
    TF_CHECK(ringzero(1));
    finish();
}

/* An idle timeout in the pass that put output into the ring, after CTS
   held it up for the idle time: that output is the old session's, and
   the next session starts with none. */
static void test_stuck(void)
{
    char list[PROTO_MAXLINE + 1];
    char buf[64];
    u16 n;

    begin(1, UART_F_RTSCTS, 10 * TMR_SEC);
    pass();
    hello(1, "PC1");
    login(1);
    fu_peer_rts(1, 0);
    list_req(list);
    put(1, list);
    put(1, "\r\n");
    run(4);
    g_now += 10 * TMR_SEC;
    fu_peer_rts(1, 1);
    while (fu_peer_recv(1, g_rep, REPSZ) != 0)
        ;
    TF_CHECK(pass() > 0);
    TF_EQ(nstate(CONN_CLOSING), 1);
    run(4);
    while (fu_peer_recv(1, g_rep, REPSZ) != 0)
        ;
    TF_EQ(g_closes, 1);
    TF_EQ(nstate(CONN_LISTEN), 1);
    put(1, "HELLO PC2\r\n");
    TF_EQ(tserial_poll(g_now), 1 + 11);
    reply(1);
    TF_STREQ(g_rep, k_ok);
    n = fu_peer_recv(1, buf, sizeof buf);
    TF_EQ(n, 0);
    TF_STREQ(ask(1, "PING"), k_ok);
    finish();
}

static void test_lost(void)
{
    uart_info ui;
    u8 port;
    u16 i;

    begin(1, 0, 0);
    pass();
    fu_inject(1, FU_OVERRUN, 2);
    put(1, "xx");
    fu_inject(1, FU_FRAMING, 1);
    put(1, "y");
    fu_inject(1, FU_LOST, 3);
    put(1, "zzz");
    for (i = 0; i < BURST / 6; i++)
        put(1, k_ping);
    TF_EQ(tserial_info(0, &port, &ui), TSER_OK);
    TF_EQ(ui.overrun, 2);
    TF_EQ(ui.framing, 1);
    TF_EQ(ui.lost, 3);
    finish();
}

/* 1 when COM1's four error counts together are n. */
static int errs_are(u16 n)
{
    uart_info ui;
    u8 port;

    TF_EQ(tserial_info(0, &port, &ui), TSER_OK);
    return ui.overrun + ui.framing + ui.parity + ui.lost == n;
}

/* The session ends and the port listens again; true when the damaged
   request was neither carried out nor answered, and the rings are
   clean.  closes is g_closes before the damage. */
static int hung_up(u16 closes)
{
    char buf[8];
    int ok;

    ok = relisten(closes);
    ok &= fu_peer_recv(1, buf, sizeof buf) == 0;
    ok &= ringzero(1);
    return ok;
}

/* In a fresh session after the damage: the user EVEn was not added,
   the user BOBn was. */
static void users_are(char n)
{
    char line[16];

    hello(1, "PC2");
    login(1);
    strcpy(line, "USER GET BOBn");
    line[12] = n;
    TF_CHECK(tf_starts(ask(1, line), "+DATA"));
    strcpy(line, "USER GET EVEn");
    line[12] = n;
    TF_CHECK(tf_starts(ask(1, line), "-ERR "));
}

#ifdef COMPAT_HOST
/* In the open session, a LOGIN damaged in its first byte, whole, then in
   a new session in two passes with the password in the second: the
   password is in no block of memory once the session ends, as the search
   finds it in a block that holds it. */
static void wipe_check(void)
{
    cmem_blk *b;
    u16 closes;

    b = cmem_alloc(sizeof SECRET);
    if (TF_CHECK(b != NULL)) {
        cmem_put(b, 0, SECRET, sizeof SECRET);
        TF_CHECK(tm_has(SECRET, sizeof SECRET - 1));
        cmem_set(b, 0, 0, sizeof SECRET);
        TF_CHECK(!tm_has(SECRET, sizeof SECRET - 1));
        cmem_free(b);
    }
    closes = g_closes;
    fu_inject(1, FU_FRAMING, 1);
    put(1, "LOGIN ADMIN " SECRET "\r\n");
    TF_CHECK(hung_up(closes));
    TF_CHECK(!tm_has(SECRET, sizeof SECRET - 1));
    hello(1, "PC4");
    closes = g_closes;
    fu_inject(1, FU_FRAMING, 1);
    put(1, "LOGIN ADMIN ");
    run(2);
    put(1, SECRET "\r\n");
    TF_CHECK(hung_up(closes));
    TF_CHECK(!tm_has(SECRET, sizeof SECRET - 1));
}
#endif

static void test_rxerr(void)
{
    static const u8 kinds[] = { FU_OVERRUN, FU_FRAMING, FU_PARITY, FU_LOST };
    char line[PROTO_MAXLINE + 1];
    u16 closes;
    u16 k;

    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    login(1);
    for (k = 0; k < sizeof kinds; k++) {
        /* A clean request, then the same kind of request marked. */
        strcpy(line, "USER ADD BOBn LEVEL=1 PASSWORD=Bobpw123");
        line[12] = (char)('1' + k);
        TF_CHECK(tf_starts(ask(1, line), "+OK"));
        closes = g_closes;
        strcpy(line, "USER ADD EVEn LEVEL=1 PASSWORD=Bobpw123\r\n");
        line[12] = (char)('1' + k);
        fu_inject(1, kinds[k], 1);
        put(1, line);
        TF_CHECK(hung_up(closes));
        TF_CHECK(errs_are((u16)(k + 1)));
        users_are((char)('1' + k));
    }
#ifdef COMPAT_HOST
    wipe_check();
#endif
    finish();
}

/* The damaged line comes in the pass that puts the old session's output
   into the transmit ring, CTS having held it up: that output is not the
   next session's, and its count is not either. */
static void rxstuck(void)
{
    char list[PROTO_MAXLINE + 1];
    char buf[64];
    u16 closes;

    begin(1, UART_F_RTSCTS, 0);
    pass();
    hello(1, "PC1");
    login(1);
    fu_peer_rts(1, 0);
    list_req(list);
    put(1, list);
    put(1, "\r\n");
    run(4);
    closes = g_closes;
    fu_inject(1, FU_FRAMING, 1);
    put(1, "PING\r\n");
    fu_peer_rts(1, 1);
    while (fu_peer_recv(1, g_rep, REPSZ) != 0)
        ;
    TF_CHECK(pass() > 0);
    TF_CHECK(g_closes > closes);
    while (fu_peer_recv(1, g_rep, REPSZ) != 0)
        ;
    TF_CHECK(relisten(closes));
    while (fu_peer_recv(1, g_rep, REPSZ) != 0)
        ;
    put(1, "HELLO PC2\r\n");
    TF_EQ(tserial_poll(g_now), 1 + 11);
    reply(1);
    TF_STREQ(g_rep, k_ok);
    TF_EQ(fu_peer_recv(1, buf, sizeof buf), 0);
    TF_STREQ(ask(1, "PING"), k_ok);
    finish();
}

/* A session after a damaged line ends by a hang-up, which opens the
   port again with its counts at 0: the session after it is served. */
static void rxhup(void)
{
    u16 closes;

    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    closes = g_closes;
    fu_inject(1, FU_FRAMING, 1);
    put(1, "PING\r\n");
    TF_CHECK(hung_up(closes));
    TF_CHECK(errs_are(1));
    fu_modem(1, UART_M_DSR | UART_M_DCD);
    hello(1, "PC2");
    closes = g_closes;
    fu_modem(1, 0);
    pass();
    g_now += TSER_HANGUP;
    pass();
    TF_CHECK(relisten(closes));
    TF_CHECK(errs_are(0));
    hello(1, "PC3");
    TF_STREQ(ask(1, "PING"), k_ok);
    finish();
}

/* A damaged byte in one pass, the end of its line in a later one; then
   the two sessions that follow a damaged line in other ways. */
static void test_rxsplit(void)
{
    char buf[8];
    u16 closes;

    begin(1, 0, 0);
    pass();
    hello(1, "PC1");
    login(1);
    closes = g_closes;
    fu_inject(1, FU_FRAMING, 1);
    put(1, "USER ADD EVE9 LEVEL=1 ");
    run(2);
    put(1, "PASSWORD=Bobpw123\r\n");
    TF_CHECK(relisten(closes));
    TF_EQ(fu_peer_recv(1, buf, sizeof buf), 0);
    hello(1, "PC2");
    login(1);
    TF_CHECK(tf_starts(ask(1, "USER GET EVE9"), "-ERR "));
    finish();
    rxstuck();
    rxhup();
}

static void test_import(void)
{
    begin(3, 0, 0);
    pass();
    hello(1, "PC1");
    login(1);
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_CHECK(tf_starts(ask(1, k_import), "-ERR "));
    TF_CHECK(!tf_starts(g_rep, "-ERR 402 "));
    put(2, "H");
    pass();
    TF_EQ(nstate(CONN_OPEN), 2);
    TF_CHECK(tf_starts(ask(1, k_import), "-ERR 402 "));
    finish();
}

static void test_two(void)
{
    begin(BOTH, 0, 0);
    pass();
    hello(2, "PC2");
    hello(1, "PC1");
    login(2);
    TF_STREQ(ask(1, "PING"), k_ok);
    TF_CHECK(strstr(ask(2, "ADMIN CLIENTS"),
                    "\r\nR 1 COM1 PC1 ") != NULL);
    TF_CHECK(strstr(g_rep, "\r\nR 2 COM2 PC2 ADMIN 3 3 0 1\r\n") != NULL);
    TF_STREQ(ask(2, "BYE"), k_ok);
    login(1);
    TF_CHECK(tf_starts(ask(1, "STATUS"), "+DATA\r\n"));
    TF_CHECK(strstr(g_rep, "\r\nK SESSIONS=1\r\n") != NULL);
    TF_CHECK(ringzero(1));
    finish();
}

static void test_shutdown(void)
{
    begin(BOTH, 0, 0);
    pass();
    hello(1, "PC1");
    hello(2, "PC2");
    login(1);
    TF_STREQ(ask(1, "ADMIN SHUTDOWN"), k_ok);
    TF_CHECK(tf_starts(ask(2, "PING"), "-ERR 501 "));
    g_now += CONN_SHUTWAIT;
    run(4);
    TF_EQ(g_closes, 2);
    TF_EQ(nstate(CONN_FREE), CONN_MAX);
    TF_EQ(conn_done(), 1);
    put(1, "H");
    run(4);
    TF_EQ(g_opens, 2);
    TF_EQ(fu_isrstate(1) & FU_S_OPEN, FU_S_OPEN);
    finish();

    /* A LISTEN ended by the shutdown. */
    begin(1, 0, 0);
    pass();
    TF_EQ(nstate(CONN_LISTEN), 1);
    conn_shutdown(g_now);
    run(2);
    TF_EQ(conn_done(), 1);
    put(1, "H");
    run(2);
    TF_EQ(g_opens, 0);
    finish();
}

void test_tser2(void)
{
    test_bye();
    test_fails();
    test_idle();
    test_drain();
    test_flow();
    test_pace();
    test_stuck();
    test_lost();
    test_rxerr();
    test_rxsplit();
    test_import();
    test_two();
    test_shutdown();
}

/*
 * T_TNET2.C - the second part of the T_TNETB test (T_TNETB.H): the
 * phases below, which main of T_TNETB.C runs.
 *
 * Checks:
 * - LISTENs: as many as LISTENS= asks and slots are free, posted on the
 *   first pass, never more than SESSIONS= ends; a pass with nothing to
 *   do returns 0;
 * - CONN_NETMAX sessions open, each with its caller's name as station
 *   and the kind "NETB", the next call refused until one hangs up;
 * - a LISTEN, RECEIVE or SEND post refused with NB_RC_TOOMANY or
 *   NB_RC_SESSFULL, and a LISTEN completed with NB_RC_SESSFULL, tried
 *   again on the next pass, at the same time;
 * - the rest after a failed LISTEN: the slots kept, no LISTEN and
 *   nothing done for TMR_SEC ticks, then LISTENs again once the name is
 *   back, across the wrap of the tick count; open sessions served
 *   meanwhile; a start after a stop made while resting posts at once;
 * - requests and responses, at every fnb_latency from 0 to 3: one line,
 *   two lines in one message, a line in two messages, messages of
 *   CONN_RXSZ bytes, one more, and about NB_MSGMAX, which arrive in
 *   pieces, two sessions interleaved; a zero-length message is passed
 *   over; a BYE and a caller's hang-up end the session;
 * - a session's receive buffer is zeroed when it ends, the others'
 *   left as they were;
 * - flow control: while CONN holds a message the next one is not
 *   received (the caller's SEND stays in progress) until it is answered;
 *   a message of CONN_RXSZ bytes is received in one piece;
 * - a response longer than NB_MSGMAX goes in messages of NB_MSGMAX;
 * - a RECEIVE and a SEND completed together are handled one per pass;
 * - the ends of a session: an abort while a SEND is in progress and
 *   no RECEIVE, an abort while a RECEIVE is, a RECEIVE or SEND post
 *   refused (the end hung up behind the transport), an idle timeout
 *   while the caller never reads, its SEND cancelled (the session
 *   aborted); each slot freed only once the provider holds no command
 *   of it;
 * - cancels the provider does not confirm at once: on an ending slot,
 *   whose commands are cancelled again before it is freed, even when a
 *   hang-up would not end them; at a shutdown's LISTENs; and across
 *   tnetb_stop, which frees every slot all the same and returns the
 *   NCBs the provider still owns, the receive block then kept, by a
 *   second tnetb_stop too while the provider owns an NCB, and freed by
 *   a later one once it owns none;
 * - tnetb_stop with sessions open, a SEND in progress among them; a
 *   shutdown: its LISTENs ended, no new one, the next request answered
 *   501 and its session closed.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TNETB.H"
#define TF_PART
#define TF_FILE "T_TNET2"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKENB.H"
#include "T_TNETB.H"

#define LPING           6       /* "PING" and CR LF                     */
#define LOK             5       /* "+OK" and CR LF                      */
#define CLI             "CLIENT"        /* the local caller's name      */
#define IDLE            (3 * TMR_10SEC) /* idle timeout: 30 seconds     */
#define PW              "secret1"       /* ADMIN's password, once set   */
#define NFILL           (FNB_MAXEND / 2) /* sessions using every end    */

static const char k_ping[] = "PING\r\n";
static const char k_ping2[] = "PING\r\nPING\r\n";

/* LPING is a literal, not sizeof k_ping - 1, because TF_EQ keeps the
   text of its first argument in the program's data. */
extern char lping_ok[1];
extern char lping_ok[(LPING == sizeof k_ping - 1) ? 1 : 2];

/* A slot of another transport: CONN never kicks it or gives it input. */
static void other_never(u8 c)
{
    TF_EQ(c, CONN_MAX);
}

static void other_close(u8 c)
{
    conn_closed(c);
}

static const conn_tops g_other = { other_never, other_never, other_close,
                                   "TEST", 0 };

/* A fresh transport of 2 sessions and 1 LISTEN with the idle timeout
   idle, and a session from PEERA on it: the remote end's LSN. */
static u8 one(u32 idle)
{
    u8 l;

    begin(2, 1, idle);
    l = call("PEERA");
    run(2);
    return l;
}

/* Send n copies of "PING" CR LF and then tail from lsn as one message,
   and check that n "+OK" responses come back. */
static void pings(u8 lsn, u16 n, const char *tail)
{
    u16 i;
    u16 len;

    len = 0;
    for (i = 0; i < n; i++) {
        memcpy(g_rep + len, k_ping, LPING);
        len = (u16)(len + LPING);
    }
    strcpy(g_rep + len, tail);
    len = (u16)(len + strlen(tail));
    TF_EQ(fnb_peer_send(lsn, g_rep, len), NB_OK);
    TF_EQ(reply(lsn, (u16)(n * LOK)), n * LOK);
    for (i = 0; i < n; i++)
        TF_CHECK(memcmp(g_rep + i * LOK, k_ok, LOK) == 0);
}

/* The slot of the open session of station, -1 when none. */
static int slot_of(const char *station)
{
    proto_peer p;
    u16 i;

    for (i = 0; conn_info(i, &p) == 0; i++)
        if (strcmp(p.station, station) == 0)
            return p.slot - 1;
    return -1;
}

/* LISTENs only on the slots other transports leave free. */
void test_slots(void)
{
    u8 c[CONN_MAX];
    u8 i;

    begin(3, 3, 0);
    for (i = 0; i < CONN_MAX - 1; i++)
        TF_CHECK(conn_claim(&g_other, &c[i]));
    TF_EQ(pass(), 1);
    TF_EQ(tnetb_listening(), 1);
    TF_EQ(pass(), 0);
    conn_closed(c[0]);
    TF_EQ(pass(), 1);
    TF_EQ(tnetb_listening(), 2);
    for (i = 1; i < CONN_MAX - 1; i++)
        conn_closed(c[i]);
    TF_EQ(pass(), 1);
    TF_EQ(pass(), 0);
    TF_EQ(tnetb_listening(), 3);
    TF_EQ(nstate(CONN_LISTEN), 3);
    TF_EQ(fnb_pending(), 3);
    finish();
}

/* CONN_NETMAX sessions; the next call refused until one ends. */
void test_full(void)
{
    char name[sizeof "PEER0"];
    proto_peer p;
    u8 lsn[CONN_NETMAX];
    int i;
    u8 l;
    u16 closes;

    begin(CONN_NETMAX, 2, 0);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(pass(), 2);
    TF_EQ(pass(), 0);
    TF_EQ(tnetb_listening(), 2);
    TF_EQ(fnb_pending(), 2);
    strcpy(name, "PEER0");
    for (i = 0; i < CONN_NETMAX; i++) {
        name[4] = (char)('A' + i);
        lsn[i] = call(name);
        run(2);
        TF_EQ(conn_count(), i + 1);
        TF_EQ(tnetb_listening(), CONN_NETMAX - i - 1 < 2 ?
                                 CONN_NETMAX - i - 1 : 2);
    }
    TF_EQ(fnb_peer_call("PEERX", SRV, &l), NB_RC_REJECTED);
    run(4);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(nstate(CONN_LISTEN), 0);
    TF_EQ(nstate(CONN_OPEN), CONN_NETMAX);
    TF_EQ(fnb_pending(), CONN_NETMAX);
    for (i = 0; i < CONN_NETMAX; i++)
        TF_STREQ(ask(lsn[i], "PING"), k_ok);
    TF_EQ(conn_info(0, &p), 0);
    TF_STREQ(p.kind, "NETB");
    TF_CHECK(slot_of("PEERC") >= 0);

    /* One caller hangs up: its slot frees and a LISTEN takes it. */
    closes = g_closes;
    TF_EQ(fnb_peer_hangup(lsn[2]), NB_OK);
    run(4);
    TF_EQ(g_closes, closes + 1);
    TF_EQ(slot_of("PEERC"), -1);
    TF_EQ(tnetb_listening(), 1);
    l = call("PEERX");
    TF_STREQ(ask(l, "PING"), k_ok);
    TF_EQ(tnetb_listening(), 0);
    finish();
}

/* Posts refused with NB_RC_TOOMANY or NB_RC_SESSFULL are tried again on
   the next pass, at the same time. */
void test_retry(void)
{
    nba *s;
    u8 ends[NFILL];
    u8 peers[NFILL];
    u16 i;
    u16 n;
    u8 l;
    u8 num;

    begin(4, 2, 0);
    fnb_ncbmax(1);
    pass();
    TF_EQ(tnetb_listening(), 1);
    pass();
    TF_EQ(tnetb_listening(), 1);
    fnb_ncbmax(0);
    pass();
    TF_EQ(tnetb_listening(), 2);
    finish();

    /* A LISTEN that completes with NB_RC_SESSFULL: posted again on the
       next pass, at the same time, with no rest. */
    begin(4, 1, 0);
    fnb_listen_rc(NB_RC_SESSFULL);
    TF_EQ(tnetb_poll(g_now), 2);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(nstate(CONN_LISTEN), 1);
    TF_EQ(tnetb_poll(g_now), 1);
    TF_EQ(tnetb_listening(), 1);
    finish();

    /* A RECEIVE and a SEND that cannot be posted wait for room. */
    begin(4, 1, 0);
    l = call("PEERA");
    run(2);
    TF_EQ(fnb_pending(), 2);
    fnb_ncbmax(1);
    put(l, k_ping);
    run(8);
    TF_EQ(fnb_peer_recv(l, g_rep, REPSZ, &n), NB_RC_PENDING);
    fnb_ncbmax(0);
    reply(l, 0);
    TF_STREQ(g_rep, k_ok);
    TF_STREQ(ask(l, "PING"), k_ok);
    finish();

    /* Every end in use: the LISTEN waits for one. */
    begin(4, 1, 0);
    TF_EQ(nb_add_name("AUX", &num), NB_OK);
    TF_EQ(nba_open(&s, 1), 0);
    for (i = 0; i < NFILL; i++) {
        TF_EQ(nba_listen(s, 0, "AUX", "*", 0, 0), NB_OK);
        TF_EQ(fnb_peer_call("FILL", "AUX", &peers[i]), NB_OK);
        TF_EQ(nba_poll(s, 0), NB_OK);
        ends[i] = nba_lsn(s, 0);
    }
    TF_EQ(fnb_sessions(), FNB_MAXEND);
    pass();
    pass();
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(nb_hangup(ends[0]), NB_OK);
    TF_EQ(fnb_peer_hangup(peers[0]), NB_RC_SESSCLOSED);
    pass();
    TF_EQ(tnetb_listening(), 1);
    l = call("PEERA");
    TF_STREQ(ask(l, "PING"), k_ok);
    for (i = 1; i < NFILL; i++) {
        TF_EQ(nb_hangup(ends[i]), NB_OK);
        TF_EQ(fnb_peer_hangup(peers[i]), NB_RC_SESSCLOSED);
    }
    TF_EQ(nba_close(s), 0);
    TF_EQ(nb_delete_name("AUX"), NB_OK);
    finish();
}

/* After a failed LISTEN the transport rests for TMR_SEC ticks. */
void test_rest(void)
{
    u32 t0;
    u16 k;
    u8 l;
    u8 num;

    /* The name deleted under the server: its LISTENs end, their slots
       stay, and nothing is posted or done for TMR_SEC ticks. */
    begin(4, 2, 0);
    TF_EQ(pass(), 2);
    TF_EQ(nb_delete_name(SRV), NB_OK);
    g_now++;
    TF_EQ(tnetb_poll(g_now), 2);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(nstate(CONN_LISTEN), 2);
    t0 = g_now;
    for (k = 1; k < TMR_SEC; k++) {
        TF_EQ(tnetb_poll(t0 + k), 0);
        TF_EQ(nstate(CONN_LISTEN), 2);
    }
    /* Tried again then, refused (no name): another rest. */
    tnetb_poll(t0 + TMR_SEC);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(nstate(CONN_LISTEN), 2);
    t0 += TMR_SEC;
    for (k = 1; k < TMR_SEC; k++)
        TF_EQ(tnetb_poll(t0 + k), 0);
    /* The name back: LISTENs at the end of the rest, calls taken. */
    TF_EQ(nb_add_name(SRV, &num), NB_OK);
    TF_EQ(tnetb_poll(t0 + TMR_SEC - 1), 0);
    TF_EQ(tnetb_listening(), 0);
    g_now = t0 + TMR_SEC;
    TF_EQ(tnetb_poll(g_now), 2);
    TF_EQ(tnetb_listening(), 2);
    l = call("PEERA");
    TF_STREQ(ask(l, "PING"), k_ok);
    finish();

    /* Across the wrap of the tick count. */
    g_now = 0xFFFFFFF8UL;
    begin(4, 2, 0);
    TF_EQ(pass(), 2);
    TF_EQ(nb_delete_name(SRV), NB_OK);
    TF_EQ(nb_add_name(SRV, &num), NB_OK);
    TF_EQ(tnetb_poll(g_now + 1), 2);
    for (k = 2; k <= TMR_SEC; k++) {
        TF_EQ(tnetb_poll(g_now + k), 0);
        TF_EQ(tnetb_listening(), 0);
    }
    g_now += TMR_SEC + 1;
    TF_EQ(tnetb_poll(g_now), 2);
    TF_EQ(tnetb_listening(), 2);
    finish();
    g_now = 2000;

    /* A resting server still serves its sessions; a start after a stop
       made while resting posts its LISTENs on the first pass. */
    begin(4, 2, 0);
    l = call("PEERA");
    run(2);
    TF_EQ(nb_delete_name(SRV), NB_RC_DELPEND);
    run(2);
    TF_EQ(tnetb_listening(), 0);
    for (k = 0; k < 3; k++)
        TF_STREQ(ask(l, "PING"), k_ok);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(g_closes, 1);
    TF_EQ(fnb_peer_hangup(l), NB_RC_SESSCLOSED);
    TF_EQ(fnb_names(), 0);
    TF_EQ(tnetb_start(SRV, 4, 2, &num), TNETB_OK);
    TF_EQ(tnetb_poll(g_now), 2);
    TF_EQ(tnetb_listening(), 2);
    finish();
}

void test_data(void)
{
    u16 lat;
    u16 closes;
    u16 n;
    u8 a;
    u8 b;

    for (lat = 0; lat <= 3; lat++) {
        begin(CONN_NETMAX, 2, 0);
        fnb_latency(lat);
        a = call("PEERA");
        b = call("PEERB");
        TF_STREQ(ask(a, "PING"), k_ok);
        put(b, k_ping2);
        TF_EQ(reply(b, 2 * LOK), 2 * LOK);
        TF_STREQ(g_rep, "+OK\r\n+OK\r\n");
        TF_EQ(g_msgs, 2);
        put(a, "PI");
        run(4);
        TF_EQ(fnb_peer_recv(a, g_rep, REPSZ, &n), NB_RC_PENDING);
        put(a, "NG\r\n");
        TF_EQ(reply(a, 0), LOK);
        TF_STREQ(g_rep, k_ok);
        TF_EQ(fnb_peer_send(b, g_rep, 0), NB_OK);
        TF_STREQ(ask(b, "PING"), k_ok);
        pings(a, CONN_RXSZ / LPING, "\r\n");
        pings(b, CONN_RXSZ / LPING, " \r\n");
        pings(a, NB_MSGMAX / LPING, "");
        put(a, k_ping);
        put(b, k_ping);
        reply(a, 0);
        TF_STREQ(g_rep, k_ok);
        reply(b, 0);
        TF_STREQ(g_rep, k_ok);

        /* BYE: its response, then the session ends; then the other
           caller hangs up. */
        closes = g_closes;
        TF_STREQ(ask(a, "BYE"), k_ok);
        TF_CHECK(ended(closes, 1));
        TF_EQ(fnb_peer_recv(a, g_rep, REPSZ, &n), NB_RC_SESSCLOSED);
        TF_EQ(fnb_peer_hangup(b), NB_OK);
        TF_CHECK(ended(closes + 1, 0));
        TF_EQ(nstate(CONN_OPEN), 0);
        finish();
    }
}

/* A request that arrives after the end of its session was asked for
   is zeroed with the slot's receive buffer, which is the second slot's,
   not at the start of the block; the first slot's input, taken in the
   same pass, is left for its request to be answered. */
void test_wipe(void)
{
    cmem_blk *rx;
    u16 off;
    u16 len;
    u16 i;
    u8 a;
    u8 b;

    begin(2, 2, IDLE);
    a = call("PEERA");
    b = call("PEERB");
    run(2);
    rx = fnb_lastbuf(0, &off, &len);
    if (!TF_CHECK(rx != NULL)) {
        finish();
        return;
    }
    TF_EQ(off, CONN_RXSZ);
    TF_EQ(len, CONN_RXSZ);
    g_now += IDLE / 2;
    TF_STREQ(ask(a, "PING"), k_ok);
    g_now += IDLE / 2;
    /* PEERB's idle timeout asks for the end, which the next pass does;
       its RECEIVE completes before. */
    pass();
    TF_EQ(g_closes, 0);
    TF_EQ(conn_count(), 1);
    put(b, k_ping);
    cmem_get(rx, off, g_rep, len);
    TF_CHECK(memcmp(g_rep, k_ping, LPING) == 0);
    put(a, k_ping);
    TF_CHECK(ended(0, 0));
    cmem_get(rx, off, g_rep, len);
    for (i = 0; i < len && g_rep[i] == 0; i++)
        ;
    TF_EQ(i, len);
    TF_EQ(reply(a, 0), LOK);
    TF_STREQ(g_rep, k_ok);
    finish();
}

/* Add CLI, open a set s of 3 NCBs and a far block b of CONN_RXSZ
   bytes, and call SRV from CLI: the caller's LSN. */
static u8 client(nba **s, cmem_blk **b)
{
    u8 lsn;

    TF_EQ(nb_add_name(CLI, &lsn), NB_OK);
    TF_EQ(nba_open(s, 3), 0);
    *b = cmem_alloc(CONN_RXSZ);
    TF_CHECK(*b != NULL);
    pass();
    TF_EQ(nb_call(CLI, SRV, &lsn, 0, 0), NB_OK);
    run(2);
    TF_EQ(g_opens, 1);
    TF_STREQ(g_station, CLI);
    return lsn;
}

/* Hang up the caller's end lsn (0 for one the provider has removed) and
   close what client opened. */
static void client_end(nba *s, cmem_blk *b, u8 lsn)
{
    if (lsn != 0)
        TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_close(s), 0);
    cmem_free(b);
    TF_EQ(nb_delete_name(CLI), NB_OK);
}

void test_flow(void)
{
    char got[LOK + 1];
    nba *s;
    cmem_blk *b;
    u16 i;
    u16 k;
    u8 lsn;
    u8 rc;

    begin(2, 1, 0);
    lsn = client(&s, &b);
    cmem_put(b, 0, k_ping, LPING);
    cmem_put(b, 8, k_ping, LPING);
    TF_EQ(nba_send(s, 0, lsn, b, 0, LPING), NB_OK);
    TF_EQ(nba_send(s, 1, lsn, b, 8, LPING), NB_OK);
    for (i = 0; i < 4; i++)
        tnetb_poll(g_now);
    TF_EQ(nba_poll(s, 0), NB_OK);
    TF_EQ(nba_poll(s, 1), NB_RC_PENDING);
    for (i = 0; i < 4; i++)
        tnetb_poll(g_now);
    TF_EQ(nba_poll(s, 1), NB_RC_PENDING);
    TF_EQ(conn_service(g_now), 1);
    for (i = 0; i < 4; i++)
        tnetb_poll(g_now);
    TF_EQ(nba_poll(s, 1), NB_OK);
    for (i = 0; i < 2; i++) {
        TF_EQ(nba_recv(s, 2, lsn, b, 16, 32), NB_OK);
        rc = nba_poll(s, 2);
        for (k = 0; k < LOOPS && rc == NB_RC_PENDING; k++) {
            pass();
            rc = nba_poll(s, 2);
        }
        TF_EQ(rc, NB_OK);
        TF_EQ(nba_len(s, 2), LOK);
        cmem_get(b, 16, got, LOK);
        got[LOK] = '\0';
        TF_STREQ(got, k_ok);
    }

    /* A message of CONN_RXSZ bytes is received whole, at once. */
    for (i = 0; i < CONN_RXSZ / LPING; i++)
        cmem_put(b, (u16)(i * LPING), k_ping, LPING);
    cmem_put(b, (u16)(CONN_RXSZ - 2), "\r\n", 2);
    TF_EQ(nba_send(s, 0, lsn, b, 0, CONN_RXSZ), NB_OK);
    for (i = 0; i < 4; i++)
        tnetb_poll(g_now);
    TF_EQ(nba_poll(s, 0), NB_OK);
    client_end(s, b, lsn);
    TF_CHECK(ended(0, 0));
    finish();

    /* The idle timeout while the caller never reads and its own SEND
       waits: the server's SEND is cancelled, not left to the hang-up
       (which waits for a SEND in progress on a real provider), so the
       session aborts and the caller's SEND ends with NB_RC_ABORTED,
       which removes the caller's end. */
    begin(2, 1, IDLE);
    lsn = client(&s, &b);
    cmem_put(b, 0, k_ping2, 2 * LPING);
    TF_EQ(nba_send(s, 0, lsn, b, 0, 2 * LPING), NB_OK);
    run(8);
    TF_EQ(nba_poll(s, 0), NB_OK);
    TF_EQ(nba_send(s, 1, lsn, b, 0, LPING), NB_OK);
    run(8);
    TF_EQ(nba_poll(s, 1), NB_RC_PENDING);
    g_now += IDLE;
    TF_CHECK(ended(0, 0));
    TF_EQ(nba_poll(s, 1), NB_RC_ABORTED);
    client_end(s, b, 0);
    finish();

    /* The caller hangs up while the server's SEND waits and no RECEIVE
       is posted: the server learns that the session is gone only from
       the SEND it cancels as it stops, and does not hang it up. */
    begin(2, 1, IDLE);
    lsn = client(&s, &b);
    cmem_put(b, 0, k_ping2, 2 * LPING);
    TF_EQ(nba_send(s, 0, lsn, b, 0, 2 * LPING), NB_OK);
    run(8);
    TF_EQ(nba_poll(s, 0), NB_OK);
    TF_EQ(nba_send(s, 1, lsn, b, 0, LPING), NB_OK);
    run(8);
    TF_EQ(nba_poll(s, 1), NB_RC_PENDING);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(fnb_sessions(), 0);
    client_end(s, b, 0);
    finish();

    /* A RECEIVE that fails while its session stays open ends the slot,
       which hangs the session up. */
    begin(2, 1, 0);
    lsn = client(&s, &b);
    fnb_fail(NB_RC_TIMEOUT);
    TF_CHECK(ended(0, 0));
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    client_end(s, b, 0);
    finish();
}

/* Append to line the pair key=, then n copies of ch. */
static void wide(char *line, const char *key, char ch, u16 n)
{
    u16 len;

    strcat(line, key);
    len = (u16)strlen(line);
    memset(line + len, ch, n);
    line[len + n] = '\0';
}

void test_big(void)
{
    char line[PROTO_MAXLINE + 1];
    char list[PROTO_MAXLINE + 1];
    u16 lat;
    u16 i;
    u8 l;

    begin(2, 1, 0);
    l = call("PEERA");
    TF_STREQ(ask(l, "HELLO PEERA"), k_ok);
    TF_CHECK(tf_starts(ask(l, "LOGIN ADMIN admin"), "+OK "));
    TF_STREQ(ask(l, "PASSWD admin " PW), k_ok);
    strcpy(line, "ITEM ADD CATEGORY=SYS");
    wide(line, " MFR=", 'M', 30);
    wide(line, " MODEL=", 'O', 30);
    wide(line, " DESCR=", 'D', 60);
    TF_STREQ(ask(l, line), "+OK ID=1\r\n");
    for (i = 1; i < PROTO_PAGEMAX; i++)
        TF_CHECK(tf_starts(ask(l, "ITEM COPY 1"), "+OK ID="));
    sprintf(list, "ITEM LIST FIELDS=ITEM_ID,MFR,MODEL,DESCR PAGE=%d",
            PROTO_PAGEMAX);
    for (lat = 0; lat <= 3; lat++) {
        fnb_latency(lat);
        ask(l, list);
        TF_CHECK(g_replen > NB_MSGMAX);
        TF_EQ(g_msg1, NB_MSGMAX);
        TF_EQ(g_msgs, (g_replen + NB_MSGMAX - 1) / NB_MSGMAX);
        TF_CHECK(tf_starts(g_rep, "+DATA\r\n"));
    }
    finish();
}

void test_both(void)
{
    u16 n;
    u8 l;

    l = one(0);
    put(l, k_ping);
    run(8);
    TF_EQ(pass(), 0);
    TF_EQ(fnb_pending(), 3);
    put(l, k_ping);
    n = 0;
    TF_EQ(fnb_peer_recv(l, g_rep, REPSZ, &n), NB_OK);
    TF_EQ(n, LOK);
    TF_EQ(tnetb_poll(g_now), 1);
    TF_EQ(tnetb_poll(g_now), 1);
    TF_EQ(tnetb_poll(g_now), 0);
    TF_EQ(reply(l, 0), LOK);
    TF_STREQ(g_rep, k_ok);
    finish();
}

void test_end(void)
{
    u16 i;
    u16 n;
    u8 l;

    /* An abort while a SEND is in progress and CONN holds the rest of
       the input, so that no RECEIVE is. */
    l = one(0);
    put(l, k_ping2);
    run(8);
    TF_EQ(fnb_pending(), 2);
    fnb_abort(l);
    TF_CHECK(ended(0, 0));
    TF_EQ(fnb_peer_recv(l, g_rep, REPSZ, &n), NB_RC_SESSCLOSED);
    finish();

    /* An abort while a RECEIVE is in progress. */
    l = one(0);
    TF_EQ(fnb_pending(), 2);
    fnb_abort(l);
    TF_CHECK(ended(0, 0));
    finish();

    /* A RECEIVE post refused: the server's end (the lower LSN) hung up
       behind the transport while CONN held a blank line. */
    l = one(0);
    TF_EQ(l, 2);
    put(l, "\r\n");
    for (i = 0; i < 4; i++)
        tnetb_poll(g_now);
    TF_EQ(nb_hangup(1), NB_OK);
    TF_CHECK(ended(0, 0));
    finish();

    /* A SEND post refused the same way, the rest of the input held. */
    l = one(0);
    put(l, k_ping2);
    for (i = 0; i < 4; i++)
        tnetb_poll(g_now);
    TF_EQ(nb_hangup(1), NB_OK);
    TF_CHECK(ended(0, 0));
    finish();

    /* The idle timeout while the caller never reads: the SEND ends and
       the response never arrives. */
    l = one(IDLE);
    put(l, k_ping);
    run(8);
    TF_EQ(fnb_pending(), 3);
    g_now += IDLE;
    TF_CHECK(ended(0, 0));
    n = 1;
    TF_EQ(fnb_peer_recv(l, g_rep, REPSZ, &n), NB_RC_SESSCLOSED);
    TF_EQ(n, 0);
    finish();
}

void test_stall(void)
{
    u16 h;
    u16 n;
    u8 l;

    /* An idle session's RECEIVE whose cancel is not confirmed at once,
       held past the hang-up: the slot ends only after a later cancel
       has ended it. */
    one(IDLE);
    fnb_stall(1);
    fnb_hold(1);
    g_now += IDLE;
    TF_CHECK(ended(0, 0));
    finish();

    /* The same for the SEND, then for both commands. */
    for (n = 1; n <= 2; n++) {
        l = one(IDLE);
        put(l, k_ping);
        run(8);
        TF_EQ(fnb_pending(), 3);
        fnb_stall(n);
        fnb_hold(n);
        g_now += IDLE;
        TF_CHECK(ended(0, 0));
        finish();
    }

    /* A shutdown whose LISTEN cancels are not confirmed at once. */
    begin(4, 2, 0);
    TF_EQ(pass(), 2);
    fnb_stall(2);
    conn_shutdown(g_now);
    for (n = 0; n < LOOPS && !conn_done(); n++)
        pass();
    TF_EQ(conn_done(), 1);
    TF_EQ(fnb_pending(), 0);
    finish();

    /* Cancels not confirmed across tnetb_stop: every slot is freed all
       the same.  None is left stuck when the hang-up and DELETE NAME
       end the stalled commands before nba_close. */
    l = one(0);
    put(l, k_ping);
    run(8);
    TF_EQ(fnb_pending(), 3);
    fnb_stall(3);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(nstate(CONN_FREE), CONN_MAX);
    TF_EQ(g_closes, g_opens);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(tnetb_poll(g_now), 0);
    conn_free();
    fnb_peer_hangup(l);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 0);

    /* When they do not, tnetb_stop returns the NCBs still owned (the
       RECEIVE and the LISTEN), and the receive block is kept, also by a
       second tnetb_stop while the provider still owns an NCB (its
       nba_stuck gives 1); once nba_stuck has seen the commands end, a
       third frees it, so that nothing is left (on the host, where the
       blocks are counted). */
    fnb_reset();
    h = tm_held();
    l = one(0);
    TF_EQ(fnb_pending(), 2);
    fnb_stall(5);
    fnb_hold(2);
    TF_EQ(tnetb_stop(), 2);
    TF_EQ(nstate(CONN_FREE), CONN_MAX);
    TF_EQ(g_closes, g_opens);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(fnb_pending(), 0);
    conn_free();
    fnb_peer_hangup(l);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 0);
    fnb_reset();
    TM_HELD(h + 2);
    TF_EQ(tnetb_stop(), 0);
    TM_HELD(h);
}

void test_stop(void)
{
    u16 closes;
    u8 a;

    /* Stop with two sessions open, a SEND in progress on one. */
    begin(4, 2, 0);
    a = call("PEERA");
    call("PEERB");
    put(a, k_ping);
    run(8);
    TF_EQ(fnb_pending(), 5);
    finish();

    /* A shutdown. */
    begin(4, 2, 0);
    a = call("PEERA");
    run(2);
    TF_EQ(tnetb_listening(), 2);
    conn_shutdown(g_now);
    run(4);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(nstate(CONN_LISTEN), 0);
    TF_EQ(fnb_pending(), 1);
    TF_EQ(conn_done(), 0);
    closes = g_closes;
    TF_CHECK(tf_starts(ask(a, "PING"), "-ERR 501 "));
    TF_CHECK(ended(closes, 0));
    TF_EQ(conn_done(), 1);
    run(4);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(fnb_peer_call("PEERB", SRV, &a), NB_RC_REJECTED);
    finish();
}

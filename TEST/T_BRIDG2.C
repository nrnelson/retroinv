/*
 * T_BRIDG2.C - the second half of the T_BRIDGE test (see T_BRIDGE.C):
 * bytes relayed both ways in order at several latencies and rooms,
 * messages of at most NB_MSGMAX bytes taken while no SEND is in
 * progress, posts refused for a while, deliveries in pieces holding the
 * server's next SEND back, the BR_DELIVERWAIT bound, the ends a client
 * or the server makes, lost sessions, and the wipes of the bytes
 * relayed.  It is a file of its own only to stay within the compiler
 * memory of Watcom C 6.5; T_BRIDGE.H holds what both share.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"
#define TF_PART
#define TF_FILE "T_BRIDG2"
#include "TESTFW.H"
#include "FAKENB.H"
#include "TSFAKE.H"
#include "T_BRIDGE.H"

#define UPLEN           5000u   /* bytes the client sends               */
#define DNLEN           7000u   /* bytes the server sends               */
#define CHUNK           300u    /* bytes the client offers at once      */
#define MAXPASS         4000    /* passes a relay may take              */
#define UPSEED          1       /* the streams of the patterns          */
#define DNSEED          2

/* The stream state of test_order. */
typedef struct {
    u8 lsn;                     /* the server's session                 */
    u8 rx;                      /* the server's RECEIVE posted          */
    u8 tx;                      /* the server's SEND posted             */
    u8 msgs;                    /* the server's messages sent           */
    u16 up;                     /* bytes the client sent                */
    u16 upgot;                  /* of them, bytes the server received   */
    u16 dn;                     /* bytes the server sent                */
    u16 dngot;                  /* of them, bytes the client received   */
} relay;

/* The client offers the next bytes of its stream. */
static void cli_put(relay *r)
{
    u16 n;

    n = (u16)(UPLEN - r->up);
    if (n > CHUNK)
        n = CHUNK;
    fill(g_buf, n, r->up, UPSEED);
    r->up = (u16)(r->up + tsf_send(0, g_buf, n));
}

/* The client reads what came and checks it. */
static void cli_get(relay *r)
{
    u16 n;

    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_UP);
    if (n > 0)
        TF_CHECK(same(g_buf, n, r->dngot, DNSEED));
    r->dngot = (u16)(r->dngot + n);
}

/* The server takes a message when one came, else keeps a RECEIVE
   posted. */
static void srv_take(relay *r)
{
    u8 rc;
    u16 n;

    if (!r->rx) {
        srv_recv(0, r->lsn);
        r->rx = 1;
        return;
    }
    rc = nba_poll(g_srv, 0);
    if (rc == NB_RC_PENDING)
        return;
    r->rx = 0;
    TF_EQ(rc, NB_OK);
    n = nba_len(g_srv, 0);
    cmem_get(g_sb, 0, g_buf, n);
    TF_CHECK(same(g_buf, n, r->upgot, UPSEED));
    r->upgot = (u16)(r->upgot + n);
}

/* The server sends its next message once the last has completed. */
static void srv_give(relay *r)
{
    static const u16 sizes[] = { NB_MSGMAX, 1, 700, BR_BOUNCE + 1 };
    u16 n;
    u8 rc;

    if (r->tx) {
        rc = nba_poll(g_srv, 1);
        if (rc == NB_RC_PENDING)
            return;
        TF_EQ(rc, NB_OK);
        r->tx = 0;
    }
    n = sizes[r->msgs % (sizeof sizes / sizeof sizes[0])];
    if (n > DNLEN - r->dn)
        n = (u16)(DNLEN - r->dn);
    if (n == 0)
        return;
    fill(g_buf, n, r->dn, DNSEED);
    srv_send(0, r->lsn, g_buf, n);
    r->tx = 1;
    r->msgs++;
    r->dn = (u16)(r->dn + n);
}

/* Relay both streams at once with latency lat, the client taking at
   most room bytes a pass, then the client leaves. */
static void relay_both(u16 lat, u16 room)
{
    relay r;
    int i;

    setup();
    fnb_latency(lat);
    start_br();
    memset(&r, 0, sizeof r);
    r.lsn = open_slot(0);
    for (i = 0; i < MAXPASS && (r.upgot < UPLEN || r.dngot < DNLEN);
         i++) {
        tsf_room(0, room);
        cli_put(&r);
        srv_take(&r);
        srv_give(&r);
        cli_get(&r);
        run(1);
    }
    TF_EQ(r.upgot, UPLEN);
    TF_EQ(r.dngot, DNLEN);
    if (!r.rx)
        srv_recv(0, r.lsn);
    tsf_fin(0);
    run(2);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE TCP;");
    TF_EQ(fnb_sessions(), 0);
    finish();
}

void test_order(void)
{
    relay_both(0, TSF_NOCAP);
    relay_both(1, 37);
    relay_both(2, 700);
    relay_both(3, BR_BOUNCE + 3);
}

/* The client reads exactly want bytes, from offset at of the server's
   stream. */
static void cli_got(u16 want, u16 at)
{
    u16 n;

    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_UP);
    TF_EQ(n, want);
    TF_CHECK(same(g_buf, n, at, DNSEED));
}

/* The client reads exactly the len bytes of s. */
static void cli_is(const char *s, u16 len)
{
    u16 n;

    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_UP);
    TF_EQ(n, len);
    TF_CHECK(memcmp(g_buf, s, len) == 0);
}

/* The server receives exactly the len bytes of s on slot 0. */
static void srv_is(u8 lsn, const char *s, u16 len)
{
    srv_recv(0, lsn);
    TF_EQ(srv_got(0), len);
    TF_CHECK(memcmp(g_buf, s, len) == 0);
}

/* Up to NB_MSGMAX bytes the client sent go as one message while no
   SEND is in progress; posts refused with NB_RC_TOOMANY are made again
   on a later pass. */
void test_forward(void)
{
    u8 lsn;

    setup();
    start_br();
    srv_listen(0);
    cli_connect(0);
    run(2);
    TF_EQ(srv_wait(0), NB_OK);
    lsn = nba_lsn(g_srv, 0);
    fnb_toomany(1);
    run(2);
    srv_send(0, lsn, "AB", 2);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    cli_is("AB", 2);
    tsf_send(0, "AB", 2);
    fnb_toomany(1);
    run(1);
    tsf_send(0, "CD", 2);
    run(1);
    srv_is(lsn, "ABCD", 4);
    tsf_send(0, "EF", 2);
    run(2);
    fill(g_buf, NB_MSGMAX, 0, UPSEED);
    TF_EQ(tsf_send(0, g_buf, NB_MSGMAX), NB_MSGMAX);
    run(3);
    srv_is(lsn, "EF", 2);
    run(1);
    fill(g_buf, 100, NB_MSGMAX, UPSEED);
    TF_EQ(tsf_send(0, g_buf, 100), 100);
    run(1);
    srv_recv(0, lsn);
    TF_EQ(srv_got(0), NB_MSGMAX);
    TF_CHECK(same(g_buf, NB_MSGMAX, 0, UPSEED));
    run(1);
    srv_recv(0, lsn);
    TF_EQ(srv_got(0), 100);
    TF_CHECK(same(g_buf, 100, NB_MSGMAX, UPSEED));
    run(1);
    fill(g_buf, NB_MSGMAX, 0, UPSEED);
    TF_EQ(tsf_send(0, g_buf, NB_MSGMAX - 1), NB_MSGMAX - 1);
    fnb_toomany(1);
    run(1);
    fill(g_buf, 2, NB_MSGMAX - 1, UPSEED);
    TF_EQ(tsf_send(0, g_buf, 2), 2);
    run(1);
    srv_recv(0, lsn);
    TF_EQ(srv_got(0), NB_MSGMAX);
    TF_CHECK(same(g_buf, NB_MSGMAX, 0, UPSEED));
    run(1);
    srv_recv(0, lsn);
    TF_EQ(srv_got(0), 1);
    TF_CHECK(same(g_buf, 1, NB_MSGMAX, UPSEED));
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;");
    finish();
}

/* A message given in pieces as the client makes room, the server's
   next SEND held back until the last piece has gone, and a RECEIVE
   that completes with NB_RC_INCOMPLETE. */
static void pieces(u8 lsn)
{
    u16 n;

    tsf_room(0, 700);
    fill(g_buf, NB_MSGMAX, 0, DNSEED);
    srv_send(0, lsn, g_buf, NB_MSGMAX);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    cli_got(700, 0);
    fill(g_buf, 10, NB_MSGMAX, DNSEED);
    srv_send(0, lsn, g_buf, 10);
    run(3);
    TF_EQ(nba_poll(g_srv, 1), NB_RC_PENDING);
    tsf_room(0, 1000);
    run(1);
    cli_got(1000, 700);
    TF_EQ(nba_poll(g_srv, 1), NB_RC_PENDING);
    tsf_room(0, TSF_NOCAP);
    run(1);
    cli_got(NB_MSGMAX - 1700, 1700);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    cli_got(10, NB_MSGMAX);
    fnb_fail(NB_RC_INCOMPLETE);
    run(1);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_UP);
    TF_EQ(n, NB_MSGMAX);
    srv_send(0, lsn, "XYZ", 3);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    cli_is("XYZ", 3);
}

/* Deliveries: in pieces; a client with nothing to take waits as long
   as it likes; then a delivery the client stalls, each byte it takes
   giving it BR_DELIVERWAIT ticks more and a request it sends none,
   until it takes none for that long and the slot ends LOST. */
void test_deliver(void)
{
    u8 lsn;
    u16 n;
    u32 t;

    setup();
    start_br();
    lsn = open_slot(0);
    pieces(lsn);
    run_to(g_now + BR_DELIVERWAIT);
    tsf_room(0, 0);
    srv_send(0, lsn, "0123456789", 10);
    t = g_now;
    run_to(t + 1000);
    tsf_room(0, 1);
    run(1);
    cli_is("0", 1);
    tsf_room(0, 0);
    run(5);
    TF_EQ(tsf_send(0, "Q", 1), 1);
    srv_recv(0, lsn);
    run(2);
    TF_EQ(srv_got(0), 1);
    run_to(t + 1000 + BR_DELIVERWAIT);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;");
    run(1);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE LOST;");
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_EOF);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    finish();
}

/* The client leaves with a SEND in progress (the SEND's cancel aborts
   the session), or resets with a RECEIVE in progress (the session is
   hung up), once with the RECEIVE's first cancel not confirmed; a
   connection that ends before the bridge saw it open. */
void test_client_ends(void)
{
    u8 lsn;

    setup();
    start_br();
    lsn = open_slot(0);
    tsf_send(0, "QUIT\r\n", 6);
    run(1);
    tsf_fin(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    lsn = open_slot(0);
    tsf_rst(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    lsn = open_slot(0);
    fnb_stall(1);
    tsf_rst(0);
    run(2);
    TF_EQ(ts_state(0), TS_CLOSED);
    run(1);
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    cli_connect(0);
    tsf_rst(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    cli_connect(0);
    tsf_fin(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE TCP;"
             "0 OPEN 10.0.2.2:1025;0 CALL ;0 CLOSE TCP;"
             "0 OPEN 10.0.2.2:1026;0 CALL ;0 CLOSE TCP;");
    finish();
}

/* A session on slot 0 with a response the client has no room for and
   a request whose SEND is in progress, which the server then hangs up:
   the SEND completes with NB_RC_SESSCLOSED.  With hold the client
   never ends a close (tsf_nofinack).  Returns the tick of the pass that
   ends the session. */
static u32 hung_up(int hold)
{
    u8 lsn;
    u32 t;

    lsn = open_slot(0);
    if (hold)
        tsf_nofinack(0);
    tsf_room(0, 0);
    srv_send(0, lsn, "+OK BYE\r\n", 9);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    tsf_send(0, "QUIT\r\n", 6);
    run(1);
    TF_EQ(nb_hangup(lsn), NB_OK);
    t = g_now;
    run(1);
    TF_EQ(br_sessions(), 0);
    return t;
}

/* Slot 0, draining since tick t, waits BR_CLOSEWAIT ticks, closes the
   connection, waits as long for it to close, then aborts it. */
static void bounded(u32 t)
{
    u16 n;

    run_to(t + BR_CLOSEWAIT);
    TF_EQ(ts_state(0), TS_OPEN);
    run(1);
    TF_EQ(ts_state(0), TS_CLOSING);
    run_to(t + 2 * BR_CLOSEWAIT);
    TF_EQ(ts_state(0), TS_CLOSING);
    run(1);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_GONE);
    TF_EQ(fnb_sessions(), 0);
}

/* The server hangs up: the client gets the bytes received, then the
   end, or leaves first; a client that takes little (a byte taken gives
   no more time), or acknowledges nothing, and never ends the close
   bounds each wait. */
void test_server_ends(void)
{
    u8 lsn;
    u16 n;
    u32 t;

    setup();
    start_br();
    hung_up(0);
    run(5);
    TF_EQ(ts_state(0), TS_OPEN);
    tsf_room(0, TSF_NOCAP);
    run(3);
    cli_is("+OK BYE\r\n", 9);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_EOF);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(fnb_sessions(), 0);
    hung_up(0);
    tsf_rst(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    t = hung_up(1);
    run_to(t + BR_CLOSEWAIT - 5);
    tsf_room(0, 1);
    run(1);
    cli_is("+", 1);
    tsf_room(0, 0);
    bounded(t);
    lsn = open_slot(0);
    tsf_noack(0);
    srv_send(0, lsn, "+", 1);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    cli_is("+", 1);
    TF_EQ(nb_hangup(lsn), NB_OK);
    bounded(g_now);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE NETB;"
             "0 OPEN 10.0.2.2:1025;0 CALL ;0 CLOSE NETB;"
             "0 OPEN 10.0.2.2:1026;0 CALL ;0 CLOSE NETB;"
             "0 OPEN 10.0.2.2:1027;0 CALL ;0 CLOSE NETB;");
    finish();
}

/* Sessions lost: aborted, a SEND failed, a RECEIVE failed with the
   cancel of the SEND in progress not confirmed at once, a SEND refused
   for an LSN the provider removed before the bridge saw it, a RECEIVE
   failed with NB_RC_BADLSN, whose session the bridge never hangs up,
   and a SEND post refused with NB_RC_BADLSN in the pass that reaps a
   RECEIVE, which ends the session once, no RECEIVE posted on LSN 0. */
void test_lost(void)
{
    u8 lsn;
    u16 n;

    setup();
    start_br();
    lsn = open_slot(0);
    fnb_abort(lsn);
    run(3);
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    lsn = open_slot(0);
    tsf_room(0, 0);
    srv_send(0, lsn, "HI", 2);
    TF_EQ(srv_wait(1), NB_OK);
    run(1);
    tsf_send(0, "DATA", 4);
    run(1);
    tsf_send(0, "MORE", 4);
    fnb_fail(NB_RC_TIMEOUT);
    TF_EQ(pass(), 1);
    tsf_room(0, TSF_NOCAP);
    run(3);
    cli_is("HI", 2);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_EOF);
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    lsn = open_slot(0);
    tsf_send(0, "DATA", 4);
    run(1);
    fnb_fail(NB_RC_TIMEOUT);
    fnb_stall(1);
    run(2);
    TF_EQ(ts_state(0), TS_OPEN);
    run(1);
    TF_EQ(ts_state(0), TS_CLOSING);
    run(1);
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE LOST;"
             "0 OPEN 10.0.2.2:1025;0 CALL ;0 CLOSE LOST;"
             "0 OPEN 10.0.2.2:1026;0 CALL ;0 CLOSE LOST;");
    finish();
    setup();
    fnb_latency(2);
    start_br();
    lsn = open_slot(0);
    fnb_abort(lsn);
    tsf_send(0, "DATA", 4);
    run(1);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE LOST;");
    run(4);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE LOST;");
    TF_EQ(ts_state(0), TS_LISTEN);
    finish();
    setup();
    start_br();
    lsn = open_slot(0);
    fnb_fail(NB_RC_BADLSN);
    run(3);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE LOST;");
    TF_EQ(ts_state(0), TS_LISTEN);
    srv_recv(0, lsn);
    TF_EQ(nba_poll(g_srv, 0), NB_RC_PENDING);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    finish();
    setup();
    start_br();
    lsn = open_slot(0);
    srv_send(0, lsn, "HI", 2);
    TF_EQ(srv_wait(1), NB_OK);
    tsf_send(0, "DATA", 4);
    fnb_refuse(1, NB_RC_BADLSN);
    run(3);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;0 CLOSE LOST;");
    cli_is("HI", 2);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(nb_hangup(lsn), NB_OK);
    finish();
}

static const char k_login[] = "LOGIN ADMIN SECRET\r\n";
static const char k_reply[] = "+OK RESPONSE\r\n";

/* No byte relayed stays in the bounce buffer, in the transmit area once
   its SEND has completed, in either area once the connection has ended,
   or in the block once the bridge has stopped. */
void test_wipe(void)
{
    cmem_blk *b;
    const u8 *p;
    u16 off;
    u16 n;
    u8 lsn;
    u8 k;

    setup();
    start_br();
    lsn = open_slot(0);
    b = br_buffers();
    tsf_send(0, k_login, sizeof k_login - 1);
    run(1);
    p = tsf_lastbuf(&n);
    TF_CHECK(p == br_bounce());
    TF_EQ(n, sizeof k_login - 1);
    TF_CHECK(zero_near(br_bounce(), BR_BOUNCE));
    srv_is(lsn, k_login, sizeof k_login - 1);
    run(1);
    TF_CHECK(fnb_lastbuf(1, &off, &n) == b);
    TF_EQ(off, 0);
    TF_EQ(n, sizeof k_login - 1);
    TF_CHECK(zero_far(b, 0, sizeof k_login - 1));
    srv_send(0, lsn, k_reply, sizeof k_reply - 1);
    run(1);
    cli_is(k_reply, sizeof k_reply - 1);
    TF_CHECK(zero_near(br_bounce(), BR_BOUNCE));
    tsf_fin(0);
    run(2);
    TF_CHECK(zero_far(b, 0, 2 * NB_MSGMAX));
    for (k = 0; k < TS_SLOTS; k++)
        srv_send(k, open_slot(k), k_reply, sizeof k_reply - 1);
    run(1);
    TF_EQ(br_stop(), 0);
#ifdef COMPAT_HOST
    /* The host's allocator gives the block just freed to an allocation
       of its size: no slot's receive area may hold the response. */
    b = cmem_alloc((u16)(2 * TS_SLOTS * NB_MSGMAX));
    for (k = 0; k < TS_SLOTS; k++) {
        cmem_get(b, (u16)((2 * k + 1) * NB_MSGMAX), g_buf,
                 sizeof k_reply - 1);
        TF_CHECK(memcmp(g_buf, k_reply, sizeof k_reply - 1) != 0);
    }
    cmem_free(b);
#endif
    finish();
}

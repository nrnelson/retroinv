/*
 * T_FAKEN2.C - the second half of the T_FAKENB test (see T_FAKENB.C):
 * the session table, the NCB limit, refused posts, LISTEN codes and the
 * remote queue's memory, latency, cancels, stalls and holds, wait-form
 * commands and the pump, no-wait timeouts, and the abort on a closed
 * set.  It is a file of its own only to stay within the compiler memory
 * of Watcom C 6.5; T_FAKENB.H holds what both share.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#ifdef COMPAT_HOST
#include <signal.h>
#include <setjmp.h>
#endif
/* Checks keep no expression text, as in T_FAKENB.C: Turbo C stores every
   string literal again. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_PART
#define TF_MALLOC
#define TF_FILE "T_FAKEN2"
#include "TESTFW.H"
#include "FAKENB.H"
#include "T_FAKENB.H"

/* Every far allocation fails. */
static int no_far(u16 size)
{
    return size > 0;
}

/* The session table, the NCB limit, refused posts, LISTEN codes and the
   remote queue's memory. */
void test_limits(void)
{
    u8 r;
    u8 lsn;
    u16 i;
#ifdef COMPAT_HOST
    u16 held;
#endif

    start(2);
    r = 0;
    for (i = 0; i < FNB_MAXEND / 2; i++)
        r = take_call(0, &lsn, 0, 0);
    TF_EQ(fnb_sessions(), FNB_MAXEND);
    snap(0);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_RC_SESSFULL);
    same(0);
    TF_EQ(nb_listen("SRV", "*", &lsn, 0, 0), NB_RC_SESSFULL);
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    TF_EQ(fnb_peer_call("PEER", "SRV", &r), NB_RC_REJECTED);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(fnb_peer_call("PEER", "SRV", &r), NB_RC_SESSFULL);
    TF_EQ(nb_call("CLI", "SRV", &r, 0, 0), NB_RC_SESSFULL);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    for (i = 1; i <= FNB_MAXEND; i++)
        nb_hangup((u8)i);
    for (i = 1; i <= FNB_MAXEND; i++)
        fnb_peer_hangup((u8)i);
    TF_EQ(fnb_sessions(), 0);
    /* The NCB limit, which a completed but unseen command still holds. */
    fnb_ncbmax(1);
    r = take_call(0, &lsn, 0, 0);
    fnb_latency(1);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    snap(0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_RC_TOOMANY);
    same(0);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_RC_TOOMANY);
    TF_EQ(fnb_pending(), 1);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(fnb_pending(), 0);
    fnb_ncbmax(0);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    /* Posts refused for a while, with NB_RC_TOOMANY or the code asked
       for: only those that pass their checks count, then the next is
       taken. */
    fnb_latency(0);
    fnb_toomany(2);
    snap(0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_RC_TOOMANY);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_RC_TOOMANY);
    fnb_refuse(1, NB_RC_BADLSN);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_RC_BADLSN);
    same(0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    /* The next LISTEN posted completes with the code asked for, once; a
       refused one does not use it. */
    fnb_listen_rc(NB_RC_SESSFULL);
    TF_EQ(nba_listen(g_s, 0, "NOBODY", "*", 0, 0), NB_RC_NONAME);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSFULL);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    fnb_listen_rc(NB_RC_NAMEDEL);
    TF_EQ(nb_listen("SRV", "*", &lsn, 0, 0), NB_RC_NAMEDEL);
    TF_EQ(lsn, 0);

    /* No far memory to queue one more remote message: refused, the
       waiting one kept; the drained queue gives its block back. */
    r = take_call(0, &lsn, 0, 0);
#ifdef COMPAT_HOST
    held = tm_held();
#endif
    fill(g_msg, 15, 1);
    TF_EQ(fnb_peer_send(r, g_msg, 10), NB_OK);
    cmem_fault = no_far;
    TF_EQ(fnb_peer_send(r, g_msg + 10, 5), NB_RC_TOOMANY);
    cmem_fault = NULL;
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, BUFSZ), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_len(g_s, 0), 10);
    cmem_get(g_b, 0, g_got, 10);
    TF_CHECK(memcmp(g_got, g_msg, 10) == 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, BUFSZ), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
#ifdef COMPAT_HOST
    TM_HELD(held);
#endif
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    finish();
}

/* Latency, cancels and stalls. */
void test_cancel(void)
{
#ifdef COMPAT_HOST
    u16 held;
#endif
    u8 r;
    u8 lsn;
    u8 cli;
    u8 srv;
    u16 i;

    start(3);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    fnb_latency(3);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    for (i = 0; i < 3; i++) {
        TF_EQ(nba_len(g_s, 0), 0);
        TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    }
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_len(g_s, 0), 4);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    /* A cancel shows its result, or a completion, without latency. */
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_CANCELLED);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    /* A stall is used only by a cancel of a command in progress, which
       it keeps until a later cancel. */
    fnb_latency(0);
    fnb_stall(1);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_RC_PENDING);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(fnb_pending(), 1);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_CANCELLED);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    /* A stalled LISTEN meets no CALL. */
    fnb_stall(1);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_cancel(g_s, 1), NB_RC_PENDING);
    TF_EQ(fnb_peer_call("PEER", "SRV", &cli), NB_RC_REJECTED);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_CANCELLED);
    /* A stalled cancel of a SEND still aborts the session; the hang-up
       of its end completes it.  The other end closed as its SEND
       reported the abort. */
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nb_call("CLI", "SRV", &cli, 0, 0), NB_OK);
    srv = nba_lsn(g_s, 1);
    TF_EQ(nba_send(g_s, 2, srv, g_b, 0, 10), NB_OK);
    TF_EQ(nba_send(g_s, 1, cli, g_b, 0, 10), NB_OK);
    fnb_stall(1);
    TF_EQ(nba_cancel(g_s, 1), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 2), NB_RC_ABORTED);
    TF_EQ(nb_hangup(cli), NB_RC_SESSCLOSED);
    TF_EQ(nba_poll(g_s, 1), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(srv), NB_RC_BADLSN);
    /* So does the closing of its end as another command reports the
       abort, before the cancel returns. */
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nb_call("CLI", "SRV", &cli, 0, 0), NB_OK);
    srv = nba_lsn(g_s, 1);
    TF_EQ(nba_recv(g_s, 2, cli, g_b, 0, 10), NB_OK);
    TF_EQ(nba_send(g_s, 1, cli, g_b, 0, 10), NB_OK);
    fnb_stall(1);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 2), NB_RC_ABORTED);
    TF_EQ(nba_poll(g_s, 1), NB_RC_ABORTED);
    TF_EQ(nb_hangup(cli), NB_RC_BADLSN);
    TF_EQ(nb_hangup(srv), NB_RC_SESSCLOSED);
    /* nba_close counts the commands a stall keeps; a hang-up and the
       deletion of the name end them. */
    fnb_stall(2);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_listen(g_s, 2, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_close(g_s), 2);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(nba_stuck(), 2);
    TF_EQ(fnb_pending(), 2);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_OK);
    TF_EQ(nba_stuck(), 2);
    fnb_latency(2);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_stuck(), 1);
#ifdef COMPAT_HOST
    held = tm_held();
#endif
    TF_EQ(nb_delete_name("SRV"), NB_OK);
    TF_EQ(nba_stuck(), 0);
#ifdef COMPAT_HOST
    TF_CHECK(tm_held() < held);
#endif
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(nba_close(NULL), 0);
}

/* Holds: a hang-up or a name's deletion leaves a stalled command in
   progress, which only a cancel ends, nba_stuck's once its set is
   closed. */
void test_hold(void)
{
    u8 r;
    u8 lsn;
    u8 lsn2;

    start(3);
    r = take_call(0, &lsn, 0, 0);
    fnb_stall(1);
    fnb_hold(1);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_RC_PENDING);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(fnb_pending(), 1);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    /* Its LSN given to a new end, whose messages and hang-up pass it. */
    r = take_call(1, &lsn2, 0, 0);
    TF_EQ(lsn2, lsn);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_OK);
    TF_EQ(nba_recv(g_s, 2, lsn2, g_b, 20, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 2), NB_OK);
    TF_EQ(nba_len(g_s, 2), 4);
    TF_EQ(nb_hangup(lsn2), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_CANCELLED);
    /* n holds keep the first n stalled commands only. */
    r = take_call(0, &lsn, 0, 0);
    fnb_stall(2);
    fnb_hold(1);
    TF_EQ(nba_recv(g_s, 1, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_recv(g_s, 2, lsn, g_b, 10, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 1), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 2), NB_RC_PENDING);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 2), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    /* A LISTEN held by its name's deletion meets no CALL and outlives
       the name added and deleted again. */
    fnb_stall(1);
    fnb_hold(1);
    TF_EQ(nba_listen(g_s, 0, "CLI", "*", 0, 0), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_RC_PENDING);
    TF_EQ(nb_delete_name("CLI"), NB_OK);
    TF_EQ(nb_add_name("CLI", &r), NB_OK);
    TF_EQ(fnb_peer_call("PEER", "CLI", &r), NB_RC_REJECTED);
    TF_EQ(nb_delete_name("CLI"), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    /* Closed: each nba_stuck cancels the held ones again, a stall
       keeping one, until none is left. */
    fnb_stall(3);
    TF_EQ(nba_close(g_s), 2);
    TF_EQ(nba_stuck(), 1);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sessions(), 0);
}

/* A pump that calls the local name arg from PEER at its second call,
   the remote end's LSN in g_peer. */
static void dial(void *arg)
{
    if (++g_pumps == 2)
        fnb_peer_call("PEER", (const char *)arg, &g_peer);
}

/* Wait-form commands and the pump. */
void test_wait(void)
{
    u8 r;
    u8 lsn;
    u8 cli;
    u16 len;
    u16 n;

    start(1);
    fnb_pump(pump, g_s);
    g_pumps = 0;
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(g_pumps, 1);
    /* A CALL refused with no answer or no LISTEN runs it once. */
    TF_EQ(nb_call("CLI", "NOBODY", &lsn, 0, 0), NB_RC_NOANSWER);
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_RC_REJECTED);
    TF_EQ(nb_call("NOBODY", "SRV", &lsn, 0, 0), NB_RC_NONAME);
    TF_EQ(g_pumps, 3);
    /* So does each timed-out CALL, which meets no LISTEN; the CALL
       after them is made as before. */
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    fnb_call_timeouts(2);
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_RC_TIMEOUT);
    TF_EQ(lsn, 0);
    TF_EQ(nb_call("NOBODY", "SRV", &lsn, 0, 0), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, 5);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_s, 0)), NB_RC_SESSCLOSED);
    g_pumps = 0;
    TF_EQ(nb_listen("CLI", "*", &lsn, 0, 0), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, FNB_SPINS);
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 5), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, FNB_SPINS + 5);
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, FNB_SPINS + 1),
          NB_RC_TIMEOUT);
    TF_EQ(g_pumps, 2 * FNB_SPINS + 5);
    TF_EQ(nb_last_cancel_rc(), NB_OK);
    fnb_pump(dial, (void *)"CLI");
    g_pumps = 0;
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 5), NB_OK);
    TF_EQ(lsn, 1);
    TF_EQ(g_peer, 2);
    TF_EQ(g_pumps, 2);
    TF_EQ(fnb_peer_hangup(g_peer), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    fnb_pump(pump, g_s);
    /* A stalled expiry keeps the LISTEN until its name goes. */
    fnb_stall(1);
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 2), NB_RC_PENDING);
    TF_EQ(nb_last_cancel_rc(), NB_RC_NOCANCEL);
    n = g_pumps;
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 2), NB_RC_PENDING);
    TF_EQ(g_pumps, n);
    /* nb_listen has its own LISTEN, and no stall delays its timeout. */
    fnb_stall(1);
    TF_EQ(nb_listen("SRV", "*", &lsn, 0, 0), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, n + FNB_SPINS);
    fnb_stall(0);
    TF_EQ(fnb_peer_call("PEER", "CLI", &r), NB_RC_REJECTED);
    TF_EQ(nb_delete_name("CLI"), NB_OK);
    TF_EQ(nb_add_name("CLI", &r), NB_OK);
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 1), NB_RC_TIMEOUT);
    TF_EQ(nb_last_cancel_rc(), NB_OK);
    /* A RECEIVE waits for a message the pump sends. */
    r = take_call(0, &lsn, 0, 0);
    g_pumps = 0;
    g_sendat = 3;
    g_peer = r;
    fill(g_msg, 10, 9);
    len = 20;
    TF_EQ(nb_recv(lsn, g_got, &len), NB_OK);
    TF_EQ(len, 10);
    TF_EQ(g_pumps, 3);
    TF_CHECK(memcmp(g_got, g_msg, 10) == 0);
    TF_EQ(nb_recv(lsn, g_got, &len), NB_RC_TIMEOUT);
    TF_EQ(len, 0);
    TF_EQ(nb_send(lsn, g_msg, 10), NB_RC_TIMEOUT);
    TF_EQ(fnb_peer_recv(r, g_got, 10, &len), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    /* The session's timeouts bound the tries: the RECEIVE's leaves the
       session open, the SEND's aborts it. */
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 2, 3), NB_OK);
    TF_EQ(nb_call("CLI", "SRV", &cli, 0, 0), NB_OK);
    lsn = nba_lsn(g_s, 0);
    g_pumps = 0;
    len = 20;
    TF_EQ(nb_recv(lsn, g_got, &len), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, 2);
    g_pumps = 0;
    TF_EQ(nb_send(lsn, g_msg, 10), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, 3);
    TF_EQ(nb_send(cli, g_msg, 10), NB_RC_SESSCLOSED);
    TF_EQ(g_nested, 0);
    fnb_pump(NULL, NULL);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(cli), NB_RC_BADLSN);
    finish();
    /* fnb_reset forgets sessions and a stalled LISTEN. */
    start(1);
    fnb_stall(1);
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 1), NB_RC_PENDING);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    TF_EQ(nb_delete_name("SRV"), NB_RC_DELPEND);
    start(1);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(nb_last_cancel_rc(), NB_OK);
    TF_EQ(nb_listen_wait("CLI", "*", &lsn, 0, 0, 1), NB_RC_TIMEOUT);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    finish();
}

/* No-wait commands time out at the session's timeouts, counted in
   polls; 0 never, and a stalled command never. */
void test_timeout(void)
{
    u8 r;
    u8 lsn;
    u16 i;
    u16 n;

    start(2);
    r = take_call(0, &lsn, 3, 2);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 0), NB_RC_TIMEOUT);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    for (i = 0; i < 2; i++)
        TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 0), NB_RC_TIMEOUT);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    fnb_stall(1);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_RC_PENDING);
    for (i = 0; i < 5; i++)
        TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_send(g_s, 1, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 1), NB_RC_TIMEOUT);
    TF_EQ(fnb_peer_send(r, g_msg, 4), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    n = 0;
    for (i = 0; i < 0xFFFFu; i++)
        if (nba_poll(g_s, 0) != NB_RC_PENDING)
            n++;
    TF_EQ(n, 0);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    finish();
}

#ifdef COMPAT_HOST
static jmp_buf g_jmp;

static void on_abort(int sig)
{
    signal(sig, SIG_DFL);
    longjmp(g_jmp, 1);
}

/* nba_open without memory gives CF_ENOMEM and holds nothing; a call
   on a closed set aborts. */
void test_host(void)
{
    volatile int aborted;
    nba *s2;
    u16 held;
    u16 k;

    for (k = 1; k <= 2; k++) {
        held = tm_held();
        tm_fail(k);
        TF_EQ(nba_open(&s2, 1), CF_ENOMEM);
        TF_CHECK(s2 == NULL);
        TM_HELD(held);
    }
    tm_fail(0);

    start(1);
    finish();
    aborted = 0;
    signal(SIGABRT, on_abort);
    if (setjmp(g_jmp) == 0)
        nba_poll(g_s, 0);
    else
        aborted = 1;
    TF_EQ(aborted, 1);
}
#endif

/*
 * T_FAKENB.C - unit test for the simulated NetBIOS provider (FAKENB.C).
 *
 * usage: T_FAKENB
 *
 * Checks (with T_FAKEN2.C):
 * - before nb_present every command and post gives NB_RC_NOT_PRESENT,
 *   and fnb_reset makes it so again; nba_open refuses 0 and more than
 *   NBA_MAX NCBs; a post on a handle out of range, on an NCB in
 *   progress or with a length of 0 or above NB_MSGMAX gives
 *   NB_RC_ILLCMD; every refused post leaves what the NCB gave before;
 * - names: a name added twice gives NB_RC_DUPNAME, a name of another
 *   station NB_RC_NAMEINUSE, one more than FNB_NAMES NB_RC_NAMEFULL,
 *   deleting a name not in the table NB_RC_NONAME; deleting a name
 *   that open ends use gives NB_RC_DELPEND and the name leaves the
 *   table with its last end; the LISTENs of a deleted name complete
 *   with NB_RC_NAMEDEL;
 * - a CALL meets the oldest matching LISTEN ("*" or the caller's name)
 *   and otherwise gets, in order, NB_RC_NONAME, NB_RC_NOANSWER,
 *   NB_RC_REJECTED, NB_RC_SESSFULL; each end takes the lowest free LSN
 *   and the LISTEN gives the caller's name;
 * - a no-wait CALL completes inside its post with the codes of a CALL
 *   or NB_OK, in the same order, NB_RC_TIMEOUT when fnb_call_timeouts
 *   asks, which nb_call and nba_call share; it gives its session in
 *   nba_lsn, and no length or caller's name; a refused or timed-out one
 *   runs the pump once, which may post the LISTEN for the next try; it
 *   meets a no-wait LISTEN and the LISTEN of nb_listen_wait; a cancel
 *   or nba_close in its latency finds it complete, its session open
 *   for the caller to hang up, and no HANG UP is refused; one held by
 *   fnb_call_hold meets nothing until a cancel ends it (a stall keeping
 *   it for nba_close to count), nb_call and the CALLs to time out left
 *   alone, and fnb_reset ends the holds;
 * - messages keep their boundaries both ways between two local ends
 *   and with a remote station, and a SEND completes only once the
 *   other end has taken its last byte; a message longer than the
 *   RECEIVE comes in parts with NB_RC_INCOMPLETE; the remote station's
 *   queue refuses more than FNB_QMAX bytes with NB_RC_TOOMANY and keeps
 *   its bytes as messages pass through it, a message refused for want
 *   of far memory leaves the queue as it was, and zero-length messages
 *   are received whole, with 0 bytes; fnb_lastbuf gives the buffer of
 *   the last RECEIVE and SEND posted and taken, and fnb_late counts the
 *   RECEIVEs posted with a message's start already waiting, from a
 *   local end or a remote station, but not with its rest;
 * - after a hang-up the other end receives what waited, then
 *   NB_RC_SESSCLOSED; an end of the program closes once a command has
 *   reported its session ended, a hang-up before that giving
 *   NB_RC_SESSCLOSED; a command on an LSN no end of its side uses gets
 *   NB_RC_BADLSN, and fnb_badhangups counts the program's HANG UPs so
 *   refused since fnb_reset;
 * - fnb_abort and the cancel of a SEND complete the commands of both
 *   ends with NB_RC_ABORTED and drop the waiting messages; fnb_fail
 *   completes the oldest RECEIVE or SEND with its code, the session
 *   left open;
 * - the limits, refused posts, LISTEN codes, latency, cancels, stalls,
 *   holds, timeouts and the pump (a refused CALL runs it once, as does
 *   each CALL fnb_call_timeouts times out, which meets no LISTEN) of
 *   T_FAKEN2.C; on the host, nba_open without memory gives CF_ENOMEM
 *   and holds nothing, and a call on a closed set aborts the program.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
/* Checks keep no expression text: Turbo C stores every string literal
   again, and the near data of this test would leave DGROUP no room for
   its near heap and stack. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKENB.H"
#include "T_FAKENB.H"

nba *g_s;
cmem_blk *g_b;
u8 g_msg[BUFSZ];
u8 g_got[BUFSZ];
u16 g_pumps;
u16 g_sendat;
u8 g_peer;
u8 g_nested;
static u8 g_inpump;
static u8 g_vrc;                /* what snap noted                      */
static u8 g_vlsn;
static u16 g_vlen;
static char g_vname[NB_NAMELEN];

void fill(u8 *buf, u16 n, u8 seed)
{
    u16 i;

    for (i = 0; i < n; i++)
        buf[i] = (u8)(i * 7u + seed);
}

void pump(void *arg)
{
    if (g_inpump)
        g_nested = 1;
    g_inpump = 1;
    g_pumps++;
    nba_poll((nba *)arg, 0);
    if (g_pumps == g_sendat)
        fnb_peer_send(g_peer, g_msg, 10);
    g_inpump = 0;
}

void start(u16 n)
{
    u8 num;

    fnb_reset();
    TF_EQ(nb_present(), 1);
    TF_EQ(nb_add_name("SRV", &num), NB_OK);
    TF_EQ(nb_add_name("CLI", &num), NB_OK);
    TF_EQ(nba_open(&g_s, n), 0);
}

u8 take_call(u16 h, u8 *lsn, u8 rto, u8 sto)
{
    u8 r;

    TF_EQ(nba_listen(g_s, h, "SRV", "*", rto, sto), NB_OK);
    TF_EQ(fnb_peer_call("PEER", "SRV", &r), NB_OK);
    TF_EQ(nba_poll(g_s, h), NB_OK);
    *lsn = nba_lsn(g_s, h);
    return r;
}

void finish(void)
{
    TF_EQ(nba_close(g_s), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
}

void snap(u16 h)
{
    g_vrc = nba_poll(g_s, h);
    g_vlsn = nba_lsn(g_s, h);
    g_vlen = nba_len(g_s, h);
    nba_callname(g_s, h, g_vname);
}

void same(u16 h)
{
    char name[NB_NAMELEN];

    TF_EQ(nba_poll(g_s, h), g_vrc);
    TF_EQ(nba_lsn(g_s, h), g_vlsn);
    TF_EQ(nba_len(g_s, h), g_vlen);
    nba_callname(g_s, h, name);
    TF_STREQ(name, g_vname);
}

static void test_absent(void)
{
    nba *s2;
    u8 num;
    u8 lsn;
    u16 len;

    fnb_reset();
    TF_EQ(nba_open(&g_s, 0), CF_EINVAL);
    TF_CHECK(g_s == NULL);
    TF_EQ(nba_open(&g_s, NBA_MAX + 1), CF_EINVAL);
    TF_EQ(nba_open(&s2, NBA_MAX), 0);
    TF_EQ(nba_close(s2), 0);
    TF_EQ(nba_open(&g_s, 1), 0);
    TF_EQ(nb_add_name("SRV", &num), NB_RC_NOT_PRESENT);
    TF_EQ(nb_delete_name("SRV"), NB_RC_NOT_PRESENT);
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_RC_NOT_PRESENT);
    TF_EQ(nb_listen("SRV", "*", &lsn, 0, 0), NB_RC_NOT_PRESENT);
    TF_EQ(nb_listen_wait("SRV", "*", &lsn, 0, 0, 1), NB_RC_NOT_PRESENT);
    TF_EQ(nb_send(1, g_msg, 1), NB_RC_NOT_PRESENT);
    len = 1;
    TF_EQ(nb_recv(1, g_got, &len), NB_RC_NOT_PRESENT);
    TF_EQ(len, 0);
    TF_EQ(nb_hangup(1), NB_RC_NOT_PRESENT);
    snap(0);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_RC_NOT_PRESENT);
    TF_EQ(nba_call(g_s, 0, "CLI", "SRV", 0, 0), NB_RC_NOT_PRESENT);
    TF_EQ(nba_recv(g_s, 0, 1, g_b, 0, 1), NB_RC_NOT_PRESENT);
    TF_EQ(nba_send(g_s, 0, 1, g_b, 0, 1), NB_RC_NOT_PRESENT);
    same(0);
    TF_EQ(nb_present(), 1);
    TF_EQ(nb_add_name("SRV", &num), NB_OK);
    TF_EQ(fnb_sets(), 1);
    TF_EQ(fnb_names(), 1);
    fnb_reset();
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(nb_add_name("SRV", &num), NB_RC_NOT_PRESENT);
}

static void test_names(void)
{
    char name[2];
    u8 num;
    u8 r;
    u8 lsn;

    start(2);
    fnb_dupname("OTHER");
    TF_CHECK(nb_add_name("X", &num) == NB_OK && num != 0);
    TF_EQ(nb_add_name("SRV", &num), NB_RC_DUPNAME);
    TF_EQ(nb_add_name("OTHER", &num), NB_RC_NAMEINUSE);
    TF_EQ(num, 0);
    TF_EQ(fnb_names(), 3);
    TF_EQ(nb_delete_name("X"), NB_OK);
    TF_EQ(nb_delete_name("X"), NB_RC_NONAME);
    TF_EQ(nb_add_name("ABCDEFGHIJKLMNOPQ", &num), NB_OK);
    TF_EQ(nb_add_name("ABCDEFGHIJKLMNO", &num), NB_RC_DUPNAME);
    TF_EQ(nb_delete_name("ABCDEFGHIJKLMNO"), NB_OK);
    TF_EQ(fnb_names(), 2);
    /* The LISTENs of a deleted name end; one with open ends lingers,
       unusable, until its last end hangs up. */
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    TF_EQ(nb_delete_name("SRV"), NB_RC_DELPEND);
    TF_EQ(nba_poll(g_s, 1), NB_RC_NAMEDEL);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(fnb_names(), 2);
    TF_EQ(nb_add_name("SRV", &num), NB_RC_DUPNAME);
    TF_EQ(nb_delete_name("SRV"), NB_RC_NONAME);
    TF_EQ(nb_call("CLI", "SRV", &num, 0, 0), NB_RC_NOANSWER);
    TF_EQ(nb_call("SRV", "CLI", &num, 0, 0), NB_RC_NONAME);
    snap(1);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_RC_NONAME);
    same(1);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_names(), 1);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nb_add_name("SRV", &num), NB_OK);
    name[1] = '\0';
    for (name[0] = 'A'; fnb_names() < FNB_NAMES; name[0]++)
        TF_EQ(nb_add_name(name, &num), NB_OK);
    TF_EQ(nb_add_name("X", &num), NB_RC_NAMEFULL);
    finish();
}

static void test_pairing(void)
{
    char name[NB_NAMELEN];
    u16 len;
    u8 lsn;
    u8 r;

    start(3);
    TF_EQ(nb_call("NOBODY", "SRV", &lsn, 0, 0), NB_RC_NONAME);
    TF_EQ(nb_call("NOBODY", "NOBODY2", &lsn, 0, 0), NB_RC_NONAME);
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_RC_REJECTED);
    TF_EQ(nb_call("CLI", "NOBODY", &lsn, 0, 0), NB_RC_NOANSWER);
    TF_EQ(fnb_peer_call("PEER", "NOBODY", &r), NB_RC_NOANSWER);
    TF_EQ(r, 0);
    TF_EQ(nb_listen("NOBODY", "*", &lsn, 0, 0), NB_RC_NONAME);
    TF_EQ(nba_listen(g_s, 0, "SRV", "PEER", 0, 0), NB_OK);
    TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_listen(g_s, 2, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_lsn(g_s, 1), 0);
    nba_callname(g_s, 1, name);
    TF_STREQ(name, "");
    /* CLI passes the LISTEN for PEER and meets the oldest for "*". */
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_OK);
    TF_EQ(lsn, 2);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 2), NB_RC_PENDING);
    TF_EQ(nba_lsn(g_s, 1), 1);
    nba_callname(g_s, 1, name);
    TF_STREQ(name, "CLI");
    TF_EQ(fnb_peer_call("PEER", "SRV", &r), NB_OK);
    TF_EQ(r, 4);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_lsn(g_s, 0), 3);
    nba_callname(g_s, 0, name);
    TF_STREQ(name, "PEER");
    TF_EQ(nba_len(g_s, 0), 0);
    TF_EQ(fnb_sessions(), 4);
    /* Refused posts change nothing. */
    snap(0);
    TF_EQ(nba_recv(g_s, 0, 3, g_b, 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_send(g_s, 0, 3, g_b, 0, NB_MSGMAX + 1), NB_RC_ILLCMD);
    TF_EQ(nba_recv(g_s, 0, 4, g_b, 0, 1), NB_RC_BADLSN);
    TF_EQ(nba_send(g_s, 0, 0, g_b, 0, 1), NB_RC_BADLSN);
    TF_EQ(nba_send(g_s, 0, FNB_MAXEND + 1, g_b, 0, 1), NB_RC_BADLSN);
    TF_EQ(nb_send(0, g_msg, 1), NB_RC_BADLSN);
    len = 1;
    TF_EQ(nb_recv(4, g_got, &len), NB_RC_BADLSN);
    TF_EQ(len, 0);
    same(0);
    TF_EQ(nba_listen(g_s, 2, "SRV", "*", 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_listen(g_s, 3, "SRV", "*", 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_poll(g_s, 3), NB_RC_ILLCMD);
    TF_EQ(nba_lsn(g_s, 3), 0);
    TF_EQ(nba_len(g_s, 3), 0);
    nba_callname(g_s, 3, name);
    TF_STREQ(name, "");
    TF_EQ(nb_hangup(4), NB_RC_BADLSN);
    TF_EQ(fnb_peer_hangup(3), NB_RC_BADLSN);
    TF_EQ(nb_hangup(1), NB_OK);
    TF_EQ(nb_hangup(2), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(2), NB_RC_BADLSN);
    TF_EQ(fnb_peer_hangup(4), NB_OK);
    TF_EQ(nb_hangup(3), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(nba_cancel(g_s, 2), NB_OK);
    TF_EQ(nba_poll(g_s, 2), NB_RC_CANCELLED);
    /* A caller named "" gives an empty name. */
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(fnb_peer_call("", "SRV", &r), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    nba_callname(g_s, 0, name);
    TF_STREQ(name, "");
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_s, 0)), NB_RC_SESSCLOSED);
    finish();
}

/* A pump that at its first call posts on NCB 1 of g_s a CALL from CLI
   to SRV when arg is not NULL, else a LISTEN for SRV. */
static void post1(void *arg)
{
    if (++g_pumps != 1)
        return;
    if (arg != NULL)
        TF_EQ(nba_call(g_s, 1, "CLI", "SRV", 0, 0), NB_OK);
    else
        TF_EQ(nba_listen(g_s, 1, "SRV", "*", 0, 0), NB_OK);
}

/* Post on NCB h a CALL from local to SRV with the receive timeout rto,
   check that the post runs the pump pumps times, and poll it once: the
   code that gives. */
static u8 dial(u16 h, const char *local, u8 rto, u16 pumps)
{
    u16 n;

    n = g_pumps;
    TF_EQ(nba_call(g_s, h, local, "SRV", rto, 0), NB_OK);
    TF_EQ(g_pumps - n, pumps);
    return nba_poll(g_s, h);
}

/* The no-wait CALL. */
static void test_call(void)
{
    char name[NB_NAMELEN];
    u8 r;
    u8 lsn;
    u8 cli;
    u16 i;

    start(3);
    /* Each refusal, in the order of nb_call's, completes in the post,
       the pump run for NB_RC_NOANSWER and NB_RC_REJECTED only. */
    fnb_pump(pump, g_s);
    TF_EQ(nba_call(g_s, 1, "NOBODY", "NOBODY2", 0, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_NONAME);
    g_pumps = 0;
    TF_EQ(nba_call(g_s, 1, "CLI", "NOBODY", 0, 0), NB_OK);
    TF_EQ(g_pumps, 1);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(nba_poll(g_s, 1), NB_RC_NOANSWER);
    TF_EQ(dial(1, "NOBODY", 0, 0), NB_RC_NONAME);
    TF_EQ(dial(1, "CLI", 0, 1), NB_RC_REJECTED);
    TF_EQ(nba_lsn(g_s, 1), 0);
    TF_EQ(nba_listen(g_s, 0, "SRV", "PEER", 0, 0), NB_OK);
    TF_EQ(dial(1, "CLI", 0, 1), NB_RC_REJECTED);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    r = 0;
    for (i = 0; i < FNB_MAXEND / 2; i++)
        r = take_call(0, &lsn, 0, 0);
    TF_EQ(dial(1, "CLI", 0, 1), NB_RC_REJECTED);
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(dial(1, "CLI", 0, 0), NB_RC_SESSFULL);
    TF_EQ(nba_lsn(g_s, 1), 0);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    for (i = 1; i < FNB_MAXEND / 2; i++) {
        TF_EQ(fnb_peer_hangup((u8)(2 * i)), NB_OK);
        TF_EQ(nb_hangup((u8)(2 * i - 1)), NB_RC_SESSCLOSED);
    }
    TF_EQ(fnb_sessions(), 0);
    /* A refused post changes nothing and uses no timed-out CALL. */
    snap(1);
    TF_EQ(nba_call(g_s, 3, "CLI", "SRV", 0, 0), NB_RC_ILLCMD);
    fnb_toomany(1);
    fnb_call_timeouts(1);
    TF_EQ(nba_call(g_s, 1, "CLI", "SRV", 0, 0), NB_RC_TOOMANY);
    same(1);
    /* fnb_call_timeouts times out the next CALLs, nb_call's and
       nba_call's alike, whatever they would have met, each running the
       pump; the CALL after them is made as before.  Its session takes
       the caller's timeouts; it gives no length or caller's name. */
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(dial(1, "CLI", 0, 1), NB_RC_TIMEOUT);
    TF_EQ(nba_lsn(g_s, 1), 0);
    fnb_call_timeouts(2);
    g_pumps = 0;
    TF_EQ(nb_call("NOBODY", "SRV", &cli, 0, 0), NB_RC_TIMEOUT);
    TF_EQ(g_pumps, 1);
    TF_EQ(dial(1, "NOBODY", 0, 1), NB_RC_TIMEOUT);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(dial(1, "CLI", 2, 0), NB_OK);
    cli = nba_lsn(g_s, 1);
    TF_EQ(cli, 2);
    TF_EQ(nba_len(g_s, 1), 0);
    nba_callname(g_s, 1, name);
    TF_STREQ(name, "");
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_lsn(g_s, 0), 1);
    nba_callname(g_s, 0, name);
    TF_STREQ(name, "CLI");
    fnb_pump(NULL, NULL);
    TF_EQ(nba_recv(g_s, 0, cli, g_b, 0, 7), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 0), NB_RC_TIMEOUT);
    TF_EQ(nb_hangup(cli), NB_OK);
    TF_EQ(nb_hangup(1), NB_RC_SESSCLOSED);
    /* A refused CALL's pump posts the LISTEN the next try meets. */
    fnb_pump(post1, NULL);
    g_pumps = 0;
    TF_EQ(dial(0, "CLI", 0, 1), NB_RC_REJECTED);
    TF_EQ(dial(0, "CLI", 0, 0), NB_OK);
    TF_EQ(nba_len(g_s, 0), 0);
    nba_callname(g_s, 0, name);
    TF_STREQ(name, "");
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    cli = nba_lsn(g_s, 0);
    TF_EQ(nba_lsn(g_s, 1), 1);
    TF_EQ(nb_hangup(cli), NB_OK);
    TF_EQ(nb_hangup(1), NB_RC_SESSCLOSED);
    /* It meets the LISTEN of nb_listen_wait, posted from its pump. */
    fnb_pump(post1, g_s);
    g_pumps = 0;
    TF_EQ(nb_listen_wait("SRV", "*", &lsn, 0, 0, 5), NB_OK);
    TF_EQ(g_pumps, 1);
    fnb_pump(NULL, NULL);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    cli = nba_lsn(g_s, 1);
    TF_EQ(lsn, 1);
    TF_EQ(cli, 2);
    fill(g_msg, 7, 5);
    cmem_put(g_b, 0, g_msg, 7);
    TF_EQ(nba_send(g_s, 1, cli, g_b, 0, 7), NB_OK);
    i = 20;
    TF_EQ(nb_recv(lsn, g_got, &i), NB_OK);
    TF_EQ(i, 7);
    TF_CHECK(memcmp(g_got, g_msg, 7) == 0);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nb_hangup(cli), NB_RC_SESSCLOSED);
    /* A cancel in its latency, which uses no stall, finds it complete,
       refused or not, and so does nba_close, the session left open. */
    fnb_latency(2);
    fnb_stall(1);
    TF_EQ(dial(1, "CLI", 0, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_REJECTED);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(dial(1, "CLI", 0, 0), NB_RC_PENDING);
    TF_EQ(fnb_pending(), 2);
    TF_EQ(nba_lsn(g_s, 1), 0);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    cli = nba_lsn(g_s, 1);
    TF_EQ(cli, 2);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_lsn(g_s, 0), 1);
    TF_EQ(nba_recv(g_s, 0, 1, g_b, 0, 7), NB_OK);
    TF_EQ(nba_cancel(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_CANCELLED);
    TF_EQ(nb_hangup(cli), NB_OK);
    TF_EQ(nb_hangup(1), NB_RC_SESSCLOSED);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_call(g_s, 1, "CLI", "SRV", 0, 0), NB_OK);
    finish();
    TF_EQ(fnb_sessions(), 2);
    TF_EQ(nb_hangup(2), NB_OK);
    TF_EQ(nb_hangup(1), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    /* A held CALL meets nothing and ends only by a cancel, before any
       timed-out CALL is used; nb_call is not held. */
    start(3);
    fnb_call_hold(2);
    fnb_call_timeouts(1);
    TF_EQ(nba_call(g_s, 3, "CLI", "SRV", 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_listen(g_s, 0, "SRV", "*", 0, 0), NB_OK);
    fnb_pump(pump, g_s);
    TF_EQ(dial(1, "CLI", 0, 0), NB_RC_PENDING);
    for (i = 0; i < 3; i++)
        TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    TF_EQ(nba_lsn(g_s, 1), 0);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(fnb_pending(), 2);
    TF_EQ(nb_call("CLI", "SRV", &lsn, 0, 0), NB_RC_TIMEOUT);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_CANCELLED);
    TF_EQ(nba_lsn(g_s, 1), 0);
    TF_EQ(fnb_sessions(), 0);
    /* A stall keeps it; nba_close counts it, and only fnb_reset ends
       it, holds and all. */
    fnb_stall(1);
    TF_EQ(nba_call(g_s, 1, "NOBODY", "SRV", 0, 0), NB_OK);
    TF_EQ(nba_cancel(g_s, 1), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    TF_EQ(dial(2, "CLI", 0, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_s, 2)), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_s, 0)), NB_RC_SESSCLOSED);
    fnb_call_hold(1);
    fnb_stall(1);
    TF_EQ(nba_close(g_s), 1);
    TF_EQ(nba_stuck(), 1);
    TF_EQ(fnb_pending(), 1);
    TF_EQ(fnb_badhangups(), 0);
    start(1);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(dial(0, "CLI", 0, 0), NB_RC_REJECTED);
    finish();
}

/* Two local ends: a wait-form client and a no-wait server. */
static void test_local(void)
{
    u8 cli;
    u8 srv;
    u16 len;

    start(2);
    TF_EQ(nba_listen(g_s, 0, "CLI", "*", 0, 0), NB_OK);
    TF_EQ(nb_call("SRV", "CLI", &cli, 0, 0), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    srv = nba_lsn(g_s, 0);
    /* Client to server: one RECEIVE each, oldest first. */
    TF_EQ(nba_recv(g_s, 0, srv, g_b, 0, 100), NB_OK);
    TF_EQ(nba_recv(g_s, 1, srv, g_b, 100, 100), NB_OK);
    fill(g_msg, 30, 1);
    TF_EQ(nb_send(cli, g_msg, 20), NB_OK);
    TF_EQ(nb_send(cli, g_msg + 20, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_len(g_s, 0), 20);
    TF_EQ(nba_lsn(g_s, 0), srv);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    TF_EQ(nba_len(g_s, 1), 10);
    cmem_get(g_b, 0, g_got, 20);
    cmem_get(g_b, 100, g_got + 20, 10);
    TF_CHECK(memcmp(g_got, g_msg, 30) == 0);
    TF_EQ(fnb_late(), 0);
    /* Server to client: two SENDs, the second in two parts, each
       RECEIVE posted late but the one for the second's rest. */
    cmem_put(g_b, 0, g_msg, 30);
    TF_EQ(nba_send(g_s, 0, srv, g_b, 0, 10), NB_OK);
    TF_EQ(nba_send(g_s, 1, srv, g_b, 10, 20), NB_OK);
    len = 100;
    TF_EQ(nb_recv(cli, g_got, &len), NB_OK);
    TF_EQ(len, 10);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_len(g_s, 0), 10);
    len = 15;
    TF_EQ(nb_recv(cli, g_got + 10, &len), NB_RC_INCOMPLETE);
    TF_EQ(len, 15);
    TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    len = 15;
    TF_EQ(nb_recv(cli, g_got + 25, &len), NB_OK);
    TF_EQ(len, 5);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    TF_EQ(nba_len(g_s, 1), 20);
    TF_CHECK(memcmp(g_got, g_msg, 30) == 0);
    TF_EQ(fnb_late(), 2);
    /* The caller's end holds its name too. */
    TF_EQ(nb_delete_name("SRV"), NB_RC_DELPEND);
    /* Cancelling a SEND aborts the session for both ends. */
    TF_EQ(nba_send(g_s, 0, srv, g_b, 0, 10), NB_OK);
    TF_EQ(nba_send(g_s, 1, cli, g_b, 0, 10), NB_OK);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_CANCELLED);
    TF_EQ(nba_poll(g_s, 0), NB_RC_ABORTED);
    TF_EQ(nba_len(g_s, 0), 0);
    /* An end closes once a command has reported its end: the server's
       with its SEND, the client's with the next command. */
    TF_EQ(fnb_sessions(), 1);
    TF_EQ(nb_hangup(srv), NB_RC_BADLSN);
    TF_EQ(nb_send(cli, g_msg, 1), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(nb_hangup(cli), NB_RC_BADLSN);
    TF_EQ(fnb_names(), 1);
    TF_EQ(nb_add_name("NEW", &cli), NB_OK);
    TF_EQ(nba_listen(g_s, 1, "NEW", "*", 0, 0), NB_OK);
    TF_EQ(nba_cancel(g_s, 1), NB_OK);
    finish();
}

/* A remote station: boundaries, parts, the queue, hang-ups, aborts. */
static void test_remote(void)
{
    static const u16 lens[] = { 2, 0, 3 };
    u8 r;
    u8 lsn;
    u16 len;
    u16 off;
    u16 i;

    start(4);
    TF_EQ(fnb_badhangups(), 0);
    TF_EQ(fnb_late(), 0);
    TF_CHECK(fnb_lastbuf(0, &off, &len) == NULL);
    TF_EQ(off + len, 0);
    TF_CHECK(fnb_lastbuf(1, &off, &len) == NULL);
    TF_EQ(off + len, 0);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(lsn, 1);
    TF_EQ(r, 2);
    fill(g_msg, NB_MSGMAX, 3);
    TF_EQ(fnb_peer_send(r, g_msg, 300), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, 5), NB_OK);
    for (i = 0; i < 3; i++) {
        TF_EQ(nba_recv(g_s, 0, lsn, g_b, (u16)(i * 100), 100), NB_OK);
        TF_EQ(nba_poll(g_s, 0), i < 2 ? NB_RC_INCOMPLETE : NB_OK);
        TF_EQ(nba_len(g_s, 0), 100);
    }
    cmem_get(g_b, 0, g_got, 300);
    TF_CHECK(memcmp(g_got, g_msg, 300) == 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 100), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(nba_len(g_s, 0), 5);
    TF_EQ(fnb_late(), 2);
    /* A zero-length message is one, between two others. */
    TF_EQ(fnb_peer_send(r, g_msg, 2), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, 0), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, 3), NB_OK);
    for (i = 0; i < 3; i++) {
        TF_EQ(nba_recv(g_s, 0, lsn, g_b, 7, 10), NB_OK);
        TF_EQ(nba_poll(g_s, 0), NB_OK);
        TF_EQ(nba_len(g_s, 0), lens[i]);
    }
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 7, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 0), NB_OK);
    /* The buffers of the last RECEIVE and SEND taken. */
    TF_CHECK(fnb_lastbuf(0, &off, &len) == g_b);
    TF_EQ(off, 7);
    TF_EQ(len, 10);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 1, 0), NB_RC_ILLCMD);
    TF_EQ(nba_send(g_s, 1, lsn, g_b, 40, 0), NB_RC_ILLCMD);
    TF_CHECK(fnb_lastbuf(1, &off, &len) == NULL);
    TF_CHECK(fnb_lastbuf(0, &off, &len) == g_b);
    TF_EQ(off, 7);
    TF_EQ(nba_send(g_s, 1, lsn, g_b, 40, 3), NB_OK);
    TF_CHECK(fnb_lastbuf(1, &off, &len) == g_b);
    TF_EQ(off, 40);
    TF_EQ(len, 3);
    TF_EQ(fnb_peer_recv(r, g_got, 10, &len), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    TF_EQ(nba_recv(g_s, 0, FNB_MAXEND, g_b, 3, 5), NB_RC_BADLSN);
    TF_EQ(nba_send(g_s, 1, FNB_MAXEND, g_b, 3, 5), NB_RC_BADLSN);
    TF_CHECK(fnb_lastbuf(0, &off, &len) == g_b);
    TF_EQ(off, 7);
    TF_EQ(len, 10);
    TF_CHECK(fnb_lastbuf(1, &off, &len) == g_b);
    TF_EQ(off, 40);
    TF_EQ(len, 3);
    /* The queue holds FNB_QMAX bytes, and keeps its bytes while one
       message waits as others pass through it. */
    for (i = 0; i < FNB_QMAX / NB_MSGMAX; i++)
        TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_RC_TOOMANY);
    TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX + 1), NB_RC_ILLCMD);
    for (i = 0; i < 2 * FNB_QMAX / NB_MSGMAX + 4; i++) {
        TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, NB_MSGMAX), NB_OK);
        TF_EQ(nba_poll(g_s, 0), NB_OK);
        cmem_get(g_b, 0, g_got, NB_MSGMAX);
        fill(g_msg, NB_MSGMAX, (u8)(i < 2 ? 3 : i + 2));
        TF_CHECK(memcmp(g_got, g_msg, NB_MSGMAX) == 0);
        fill(g_msg, NB_MSGMAX, (u8)(i + 4));
        TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX), NB_OK);
    }
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, NB_MSGMAX), NB_OK);
    TF_EQ(nba_recv(g_s, 1, lsn, g_b, 0, NB_MSGMAX), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    /* An end hung up with its queue full reopens empty. */
    TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(lsn, 1);
    TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, NB_MSGMAX), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, NB_MSGMAX), NB_OK);
    TF_EQ(nba_recv(g_s, 1, lsn, g_b, 0, NB_MSGMAX), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    /* Local to remote: the SEND waits for the last byte. */
    fill(g_msg, 50, 7);
    cmem_put(g_b, 0, g_msg, 50);
    TF_EQ(nba_send(g_s, 1, lsn, g_b, 0, 50), NB_OK);
    TF_EQ(fnb_peer_recv(r, g_got, 30, &len), NB_RC_INCOMPLETE);
    TF_EQ(len, 30);
    TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    TF_EQ(fnb_peer_recv(r, g_got + 30, 30, &len), NB_OK);
    TF_EQ(len, 20);
    TF_EQ(nba_poll(g_s, 1), NB_OK);
    TF_EQ(nba_len(g_s, 1), 50);
    TF_CHECK(memcmp(g_got, g_msg, 50) == 0);
    TF_EQ(fnb_peer_recv(r, g_got, 30, &len), NB_RC_PENDING);
    /* fnb_fail completes the oldest RECEIVE or SEND with its code, the
       session left open; a LISTEN and a stalled command posted before
       it stay in progress. */
    TF_EQ(nba_listen(g_s, 2, "SRV", "*", 0, 0), NB_OK);
    TF_EQ(nba_recv(g_s, 3, lsn, g_b, 0, 10), NB_OK);
    fnb_stall(1);
    TF_EQ(nba_cancel(g_s, 3), NB_RC_PENDING);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_send(g_s, 1, lsn, g_b, 0, 10), NB_OK);
    fnb_fail(NB_RC_TIMEOUT);
    TF_EQ(nba_poll(g_s, 0), NB_RC_TIMEOUT);
    TF_EQ(nba_poll(g_s, 1), NB_RC_PENDING);
    fnb_fail(NB_RC_ILLCMD);
    TF_EQ(nba_poll(g_s, 1), NB_RC_ILLCMD);
    fnb_fail(NB_RC_TIMEOUT);
    TF_EQ(nba_poll(g_s, 2), NB_RC_PENDING);
    TF_EQ(nba_poll(g_s, 3), NB_RC_PENDING);
    TF_EQ(nba_cancel(g_s, 2), NB_OK);
    TF_EQ(nba_cancel(g_s, 3), NB_OK);
    TF_EQ(fnb_peer_recv(r, g_got, 30, &len), NB_RC_PENDING);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 10), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    /* The remote end hangs up: what waited still arrives. */
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_BADLSN);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, NB_MSGMAX), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    TF_EQ(fnb_sessions(), 1);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, NB_MSGMAX), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    i = fnb_badhangups();
    TF_EQ(nba_send(g_s, 0, lsn, g_b, 0, 1), NB_RC_BADLSN);
    TF_EQ(nb_hangup(lsn), NB_RC_BADLSN);
    TF_EQ(fnb_badhangups(), i + 1);
    /* A hang-up before the end was reported gives NB_RC_SESSCLOSED. */
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    /* The local end hangs up: its SEND in progress is dropped. */
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_send(g_s, 1, lsn, g_b, 0, 50), NB_OK);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nba_poll(g_s, 1), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_recv(r, g_got, 30, &len), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_recv(r, g_got, 30, &len), NB_RC_BADLSN);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_RC_BADLSN);
    /* A RECEIVE in progress when the other end hangs up. */
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_BADLSN);
    /* fnb_abort ends both ends' commands and drops what waited. */
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    fnb_abort(r);
    TF_EQ(nba_poll(g_s, 0), NB_RC_ABORTED);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_BADLSN);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    fnb_abort(lsn);
    TF_EQ(nba_poll(g_s, 0), NB_RC_ABORTED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_BADLSN);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_OK);
    fnb_abort(r);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_BADLSN);
    r = take_call(0, &lsn, 0, 0);
    TF_EQ(fnb_peer_send(r, g_msg, 1), NB_OK);
    fnb_abort(lsn);
    TF_EQ(nba_recv(g_s, 0, lsn, g_b, 0, 1), NB_OK);
    TF_EQ(nba_poll(g_s, 0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(lsn), NB_RC_BADLSN);
    fnb_abort(lsn);
    /* Zero-length messages fill the queue too, 2 bytes each. */
    r = take_call(0, &lsn, 0, 0);
    for (i = 0; i < 2 * FNB_QMAX && fnb_peer_send(r, g_msg, 0) == NB_OK;
         i++)
        ;
    TF_EQ(i, 3 * FNB_QMAX / 2);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(fnb_peer_hangup(r), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    finish();
}

int main(void)
{
    tf_begin("T_FAKENB");
    cmem_init();
    g_b = cmem_alloc(BUFSZ);
    if (!TF_CHECK(g_b != NULL))
        return tf_done();
    test_absent();
    test_names();
    test_pairing();
    test_call();
    test_local();
    test_remote();
    test_limits();
    test_cancel();
    test_hold();
    test_wait();
    test_timeout();
#ifdef COMPAT_HOST
    test_host();
#endif
    fnb_reset();
    cmem_free(g_b);
    return tf_done();
}

/*
 * T_BRIDGE.C - unit test for the TCP bridge (BRIDGE.C) over the
 * simulated TCP/IP stack (TSFAKE.C) and NetBIOS provider (FAKENB.C).
 *
 * usage: T_BRIDGE
 *
 * Checks (with T_BRIDG2.C):
 * - br_start refuses an empty name and a second start (BR_EARG); a
 *   name given is upper-cased and cut to NB_NAMELEN - 1 characters, one
 *   already in the local name table is used and never deleted; without
 *   one the bridge takes the machine name, else a generated name, tried
 *   again after NB_RC_DUPNAME and NB_RC_NAMEINUSE up to NB_NAMETRIES
 *   times (on the host, whose clock is known); a taken name gives
 *   BR_ENAME, another ADD NAME failure BR_ENB, no far memory BR_ENOMEM,
 *   a slot that cannot listen BR_ELISTEN, each with *nbrc, nothing left
 *   behind (no near block either, on the host) and the slots already
 *   listening stopped;
 * - br_start refuses more than BR_ALLOWMAX networks (BR_EARG); with an
 *   allow-list, a client whose address none of its networks holds is
 *   reported by REFUSE alone, its connection reset with no CALL made,
 *   and the slot listens again; one that a network holds, by its whole
 *   address after two that do not, its first 31 bits or its first 16,
 *   is let in (OPEN, CALL);
 * - TS_SLOTS connections at once, one more refused; br_poll's count; the
 *   slots taken in turn from the one after the last pass's first;
 * - a CALL refused with NB_RC_REJECTED, NB_RC_NOANSWER, NB_RC_NONAME or
 *   NB_RC_SESSFULL, or whose post is refused with a code other than
 *   NB_RC_TOOMANY, gives NOCALL and its code in hex, the client's
 *   connection closed without a byte, and a close the client never
 *   ends aborted after BR_CLOSEWAIT; a CALL timed out is made again
 *   TMR_SEC ticks later, once only; a post refused with NB_RC_TOOMANY
 *   is made again on the next pass; a CALL the client leaves is
 *   cancelled, a cancel not confirmed tried again, and a CALL that
 *   completed as it was cancelled, or stopped, hung up;
 * - br_stop reports CLOSE STOP for the slots calling or relaying only,
 *   aborts every connection, hangs every session up and deletes the
 *   name; with a cancel the provider never confirms it returns the
 *   count and keeps the block as it is, unfreed, the transmit areas of
 *   the other slots zeroed, and a second br_stop while the provider
 *   still owns the NCB keeps it too; once the provider has given the
 *   NCB up and the test has finished, the block is zeroed and freed and
 *   nothing br_start took is left (no near block either, on the host);
 * - while the provider owns an NCB of a stuck stop, br_start is refused
 *   (BR_EARG, NB_OK) taking no set or block, and br_stop returns 0,
 *   neither freeing a block, however often; once it owns none (or after
 *   a reset of the provider), the first br_start, refused for its name
 *   or not, or br_stop zeroes and frees the kept block, once, a start
 *   then taking a new one; stuck stops and starts over and over keep
 *   at most one block, and leave none behind (on the host);
 * - the relaying, ends and wipes of T_BRIDG2.C; no HANG UP is ever
 *   refused (fnb_badhangups) and no NCB is left in progress.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#define COMPAT_FIND
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKENB.H"
#include "TSFAKE.H"
#include "T_BRIDGE.H"

nba *g_srv;
cmem_blk *g_sb;
u32 g_now;
char g_ev[EVSZ];
u8 g_buf[NB_MSGMAX];

/* Append "slot NAME text;" to the log arg. */
static void on_event(void *arg, u8 slot, u8 ev, const char *text)
{
    static const char *const names[] = { "OPEN", "CALL", "NOCALL",
                                         "CLOSE", "REFUSE" };
    char line[40];      /* "255 NOCALL 255.255.255.255:65535;" fits */

    sprintf(line, "%u %s %s;", (unsigned)slot, names[ev], text);
    if (strlen((char *)arg) + strlen(line) < EVSZ)
        strcat((char *)arg, line);
}

void setup(void)
{
    u8 num;

    fnb_reset();
    tsf_reset();
    TF_EQ(nb_present(), 1);
    TF_EQ(ts_init(), TS_OK);
    TF_EQ(nba_open(&g_srv, SRVNCBS), 0);
    g_sb = cmem_alloc((u16)(SRVNCBS * NB_MSGMAX));
    TF_CHECK(g_sb != NULL);
    TF_EQ(nb_add_name("SRV", &num), NB_OK);
    br_events(on_event, g_ev);
    ev_clear();
    g_now = 0;
}

void start_br(void)
{
    u8 rc;

    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_OK);
    TF_EQ(rc, NB_OK);
}

void finish(void)
{
    TF_EQ(br_stop(), 0);
    TF_CHECK(br_buffers() == NULL);
    TF_EQ(nba_close(g_srv), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_badhangups(), 0);
    cmem_free(g_sb);
    ts_term();
}

u16 pass(void)
{
    u16 n;

    ts_poll();
    n = br_poll(g_now);
    g_now++;
    return n;
}

void run(u16 n)
{
    while (n-- > 0)
        pass();
}

void run_to(u32 t)
{
    while (g_now < t)
        pass();
}

void ev_clear(void)
{
    g_ev[0] = '\0';
}

void srv_listen(u8 k)
{
    TF_EQ(nba_listen(g_srv, (u16)(2 * k), "SRV", "*", 0, 0), NB_OK);
}

void cli_connect(u8 k)
{
    u8 s;

    s = TS_SLOTS;
    TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
    TF_EQ(s, k);
}

u8 open_slot(u8 k)
{
    u8 had;
    int i;

    srv_listen(k);
    cli_connect(k);
    had = br_sessions();
    for (i = 0; i < SPINS && br_sessions() == had; i++)
        pass();
    TF_EQ(br_sessions(), had + 1);
    TF_EQ(srv_wait((u16)(2 * k)), NB_OK);
    pass();
    return nba_lsn(g_srv, (u16)(2 * k));
}

u8 srv_wait(u16 h)
{
    u8 rc;
    int i;

    rc = NB_RC_PENDING;
    for (i = 0; i < SPINS && rc == NB_RC_PENDING; i++)
        rc = nba_poll(g_srv, h);
    return rc;
}

void srv_recv(u8 k, u8 lsn)
{
    TF_EQ(nba_recv(g_srv, (u16)(2 * k), lsn, g_sb,
                   (u16)(2 * k * NB_MSGMAX), NB_MSGMAX), NB_OK);
}

void srv_send(u8 k, u8 lsn, const void *src, u16 len)
{
    u16 off;

    off = (u16)((2 * k + 1) * NB_MSGMAX);
    cmem_put(g_sb, off, src, len);
    TF_EQ(nba_send(g_srv, (u16)(2 * k + 1), lsn, g_sb, off, len), NB_OK);
}

u16 srv_got(u8 k)
{
    u16 n;

    TF_EQ(srv_wait((u16)(2 * k)), NB_OK);
    n = nba_len(g_srv, (u16)(2 * k));
    cmem_get(g_sb, (u16)(2 * k * NB_MSGMAX), g_buf, n);
    return n;
}

/* Byte at of the stream seed. */
static u8 pat(u16 at, u8 seed)
{
    return (u8)(at * 7u + (at >> 8) + seed);
}

void fill(u8 *buf, u16 n, u16 at, u8 seed)
{
    u16 i;

    for (i = 0; i < n; i++)
        buf[i] = pat((u16)(at + i), seed);
}

int same(const u8 *buf, u16 n, u16 at, u8 seed)
{
    u16 i;

    for (i = 0; i < n; i++)
        if (buf[i] != pat((u16)(at + i), seed))
            return 0;
    return 1;
}

int zero_far(cmem_blk *b, u16 off, u16 n)
{
    u8 c;

    while (n-- > 0) {
        cmem_get(b, off++, &c, 1);
        if (c != 0)
            return 0;
    }
    return 1;
}

int zero_near(const u8 *p, u16 n)
{
    while (n-- > 0)
        if (*p++ != 0)
            return 0;
    return 1;
}

/* After a failed br_start: no block, no NCBs but the server's, the
   slots in state ts, nothing to poll. */
static void left_none(u8 ts)
{
    u8 k;

    TF_CHECK(br_buffers() == NULL);
    TF_EQ(fnb_sets(), 1);
    TF_EQ(br_poll(g_now), 0);
    for (k = 0; k < TS_SLOTS; k++)
        TF_EQ(ts_state(k), ts);
}

/* Bad arguments, a second start, a start and a stop. */
static void test_args(void)
{
    u8 rc;
    u8 k;

    setup();
    TF_EQ(br_poll(0), 0);
    TF_EQ(br_stop(), 0);
    TF_CHECK(br_buffers() == NULL);
    rc = 0xEE;
    TF_EQ(br_start("", "bridge", NULL, 0, &rc), BR_EARG);
    TF_EQ(rc, NB_OK);
    TF_EQ(br_start("srv", "", NULL, 0, &rc), BR_EARG);
    left_none(TS_FREE);
    start_br();
    TF_CHECK(br_buffers() != NULL);
    TF_EQ(fnb_names(), 2);
    rc = 0xEE;
    TF_EQ(br_start("srv", "other", NULL, 0, &rc), BR_EARG);
    TF_EQ(rc, NB_OK);
    for (k = 0; k < TS_SLOTS; k++)
        TF_EQ(ts_state(k), TS_LISTEN);
    TF_EQ(pass(), 0);
    TF_EQ(br_stop(), 0);
    TF_EQ(fnb_names(), 1);
    TF_EQ(fnb_sets(), 1);
    TF_STREQ(g_ev, "");
    finish();
}

/* Given names: cut and upper-cased, the server's too; one already in
   the table used and kept; one taken; a full table. */
static void test_given(void)
{
    char name[NB_NAMELEN];
    u8 rc;
    u8 num;
    int i;

    setup();
    TF_EQ(br_start("srv", "bridge-station-long", NULL, 0, &rc), BR_OK);
    open_slot(0);
    nba_callname(g_srv, 0, name);
    TF_STREQ(name, "BRIDGE-STATION-");
    TF_EQ(br_stop(), 0);
    TF_EQ(fnb_names(), 1);
    TF_EQ(nb_add_name("BRIDGE", &num), NB_OK);
    start_br();
    TF_EQ(fnb_names(), 2);
    TF_EQ(br_stop(), 0);
    TF_EQ(fnb_names(), 2);
    TF_EQ(nb_delete_name("BRIDGE"), NB_OK);
    fnb_dupname("BRIDGE");
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ENAME);
    TF_EQ(rc, NB_RC_NAMEINUSE);
    left_none(TS_CLOSED);
    finish();
    setup();
    for (i = 1; fnb_names() < FNB_NAMES; i++) {
        sprintf(name, "N%d", i);
        TF_EQ(nb_add_name(name, &num), NB_OK);
    }
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ENB);
    TF_EQ(rc, NB_RC_NAMEFULL);
    left_none(TS_FREE);
    finish();
}

/* Without a name: the machine name, or a generated one, as the
   server's LISTEN sees it. */
static void test_noname(void)
{
    char mach[NB_NAMELEN];
    char name[NB_NAMELEN];
    u8 rc;
    int i;

    setup();
    TF_EQ(br_start("srv", NULL, NULL, 0, &rc), BR_OK);
    open_slot(0);
    nba_callname(g_srv, 0, name);
    if (dos_machine_name(mach) == 0) {
        for (i = 0; mach[i] != '\0'; i++)
            mach[i] = (char)TXT_UPPER(mach[i]);
        TF_STREQ(name, mach);
    } else {
        TF_EQ(strlen(name), 8);
        TF_CHECK(memcmp(name, "RTCP", 4) == 0);
        for (i = 4; i < 8; i++)
            TF_CHECK(strchr("0123456789ABCDEF", name[i]) != NULL);
    }
    TF_EQ(br_stop(), 0);
    TF_EQ(fnb_names(), 1);
    finish();
}

#ifdef COMPAT_HOST
/* The name the bridge generates from tick count v. */
static void gen(char *d, u16 v)
{
    sprintf(d, "RTCP%04X", (unsigned)v);
}

/* Generated names tried again, on the host, whose tmr_ticks counts its
   calls: the bridge's try i reads t + 1 + i and adds i, t being the
   count before it starts. */
static void test_retry(void)
{
    char name[NB_NAMELEN];
    char got[NB_NAMELEN];
    u16 t;
    u8 rc;
    u8 num;
    int i;

    setup();
    t = (u16)tmr_ticks();
    gen(name, (u16)(t + 1));
    TF_EQ(nb_add_name(name, &num), NB_OK);
    gen(name, (u16)(t + 3));
    fnb_dupname(name);
    TF_EQ(br_start("srv", NULL, NULL, 0, &rc), BR_OK);
    TF_EQ(rc, NB_OK);
    open_slot(0);
    nba_callname(g_srv, 0, got);
    gen(name, (u16)(t + 5));
    TF_STREQ(got, name);
    TF_EQ(fnb_names(), 3);
    TF_EQ(br_stop(), 0);
    TF_EQ(fnb_names(), 2);
    finish();
    setup();
    t = (u16)tmr_ticks();
    for (i = 0; i < NB_NAMETRIES; i++) {
        gen(name, (u16)(t + 1 + 2 * i));
        if (i % 2 != 0)
            TF_EQ(nb_add_name(name, &num), NB_OK);
        else
            fnb_dupname(name);
    }
    TF_EQ(br_start("srv", NULL, NULL, 0, &rc), BR_ENAME);
    TF_EQ(rc, NB_RC_DUPNAME);
    TF_EQ(fnb_names(), 1 + NB_NAMETRIES / 2);
    left_none(TS_FREE);
    finish();
}
#endif

/* Every far allocation fails. */
static int no_far(u16 size)
{
    return size > 0;
}

/* BR_ENOMEM (no near memory for the NCBs, on the host, or no far
   memory), BR_ELISTEN (one slot taken, or no stack) and BR_ENB with no
   provider found. */
static void test_fail(void)
{
    u8 rc;
    u8 k;
#ifdef COMPAT_HOST
    u16 held;
#endif

    setup();
#ifdef COMPAT_HOST
    held = tm_held();
    tm_fail(1);
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ENOMEM);
    tm_fail(0);
    TF_EQ(rc, NB_OK);
    TM_HELD(held);
    TF_EQ(fnb_sets(), 1);
#endif
    cmem_fault = no_far;
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ENOMEM);
    cmem_fault = NULL;
    TF_EQ(rc, NB_OK);
    TF_EQ(fnb_names(), 1);
    left_none(TS_FREE);
    TF_EQ(ts_listen(2, 9), TS_OK);
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ELISTEN);
    TF_EQ(rc, NB_OK);
    TF_EQ(fnb_names(), 1);
    TF_EQ(ts_state(0), TS_CLOSED);
    TF_EQ(ts_state(1), TS_CLOSED);
    TF_EQ(ts_state(2), TS_LISTEN);
    TF_EQ(ts_state(3), TS_FREE);
    TF_EQ(tsf_connect(BR_PORT, &k), TSF_REFUSED);
    TF_CHECK(br_buffers() == NULL);
    TF_EQ(fnb_sets(), 1);
    ts_term();
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ELISTEN);
    TF_EQ(fnb_names(), 1);
    finish();
    setup();
    fnb_reset();
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_ENB);
    TF_EQ(rc, NB_RC_NOT_PRESENT);
    TF_EQ(fnb_sets(), 0);
    TF_CHECK(br_buffers() == NULL);
    cmem_free(g_sb);
    ts_term();
}

/* A client connects to slot 0, whose CALL fails as want (OPEN and
   NOCALL) says; the client sees its connection end without a byte, and
   the slot listens again. */
static void nocall(const char *want)
{
    u16 n;

    ev_clear();
    cli_connect(0);
    run(2);
    TF_STREQ(g_ev, want);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_EOF);
    TF_EQ(n, 0);
    run(1);
    TF_EQ(ts_state(0), TS_LISTEN);
}

/* Fill the session table but for one end, then post the server's
   LISTEN. */
static void fill_ends(void)
{
    u8 r;
    int i;

    for (i = 0; i < FNB_MAXEND / 2; i++) {
        srv_listen(0);
        TF_EQ(fnb_peer_call("PEER", "SRV", &r), NB_OK);
        TF_EQ(srv_wait(0), NB_OK);
    }
    TF_EQ(fnb_peer_hangup(r), NB_OK);
    srv_listen(0);
}

/* Refused CALLs, and a close the client never ends. */
static void test_nocall(void)
{
    u8 rc;
    u16 n;
    u32 t;

    setup();
    start_br();
    nocall("0 OPEN 10.0.2.2:1024;0 NOCALL 12;");
    fnb_refuse(1, NB_RC_ILLCMD);
    nocall("0 OPEN 10.0.2.2:1025;0 NOCALL 03;");
    TF_EQ(nb_delete_name("BRIDGE"), NB_OK);
    srv_listen(0);
    nocall("0 OPEN 10.0.2.2:1026;0 NOCALL 15;");
    finish();
    setup();
    TF_EQ(br_start("nosuch", "bridge", NULL, 0, &rc), BR_OK);
    nocall("0 OPEN 10.0.2.2:1024;0 NOCALL 14;");
    ev_clear();
    cli_connect(0);
    tsf_nofinack(0);
    t = g_now;
    run_to(t + 1 + BR_CLOSEWAIT);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1025;0 NOCALL 14;");
    TF_EQ(ts_state(0), TS_CLOSING);
    run(1);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_GONE);
    finish();
    setup();
    start_br();
    fill_ends();
    nocall("0 OPEN 10.0.2.2:1024;0 NOCALL 11;");
    finish();
}

/* The allow-list, on the peers' address 10.0.2.2: refused by the first
   two networks, let in by the third, its whole address, after them, and
   by the last two, its first 31 and 16 bits. */
static void test_allow(void)
{
    static const br_net nets[] = {
        { 0x0A000300UL, 0xFFFFFF00UL }, { 0x0A000203UL, 0xFFFFFFFFUL },
        { 0x0A000202UL, 0xFFFFFFFFUL }, { 0x0A000202UL, 0xFFFFFFFEUL },
        { 0x0A000000UL, 0xFFFF0000UL }
    };
    static const u8 first[] = { 0, 3, 4 };
    u16 n;
    u8 rc;
    int i;

    setup();
    TF_EQ(br_start("srv", "bridge", nets, BR_ALLOWMAX + 1, &rc), BR_EARG);
    left_none(TS_FREE);
    TF_EQ(br_start("srv", "bridge", nets, 2, &rc), BR_OK);
    cli_connect(0);
    run(2);
    TF_STREQ(g_ev, "0 REFUSE 10.0.2.2:1024;");
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_GONE);
    TF_EQ(ts_state(0), TS_LISTEN);
    ev_clear();
    cli_connect(0);
    run(2);
    TF_STREQ(g_ev, "0 REFUSE 10.0.2.2:1025;");
    finish();
    for (i = 0; i < 3; i++) {
        setup();
        TF_EQ(br_start("srv", "bridge", nets + first[i],
                       (u8)(first[i] == 0 ? 3 : 1), &rc), BR_OK);
        open_slot(0);
        TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;");
        finish();
    }
}

/* A CALL post refused within TMR_SEC ticks of the start, made again
   on the next pass; a CALL timed out once, then twice. */
static void test_retried(void)
{
    u32 t;

    setup();
    start_br();
    srv_listen(0);
    fnb_toomany(1);
    cli_connect(0);
    run(1);
    TF_EQ(nba_poll(g_srv, 0), NB_RC_PENDING);
    run(1);
    TF_EQ(nba_poll(g_srv, 0), NB_OK);
    run(1);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;");
    tsf_fin(0);
    run(2);
    ev_clear();
    srv_listen(0);
    fnb_call_timeouts(1);
    cli_connect(0);
    t = g_now;
    run_to(t + 1 + TMR_SEC);
    TF_EQ(nba_poll(g_srv, 0), NB_RC_PENDING);
    run(1);
    TF_EQ(nba_poll(g_srv, 0), NB_OK);
    run(1);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1025;0 CALL ;");
    tsf_fin(0);
    run(2);
    ev_clear();
    srv_listen(0);
    fnb_call_timeouts(2);
    cli_connect(0);
    t = g_now;
    run_to(t + 3 + TMR_SEC);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1026;0 NOCALL 05;");
    run(4 * TMR_SEC);
    TF_EQ(nba_poll(g_srv, 0), NB_RC_PENDING);
    TF_EQ(ts_state(0), TS_LISTEN);
    finish();
}

/* A CALL the client leaves: held, held with its cancel stalled once,
   and completed but not yet seen, which is hung up, as it is when the
   bridge stops. */
static void test_cancel(void)
{
    u8 lsn;

    setup();
    start_br();
    fnb_call_hold(1);
    cli_connect(0);
    run(3);
    tsf_fin(0);
    run(2);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CLOSE TCP;");
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(fnb_pending(), 0);
    fnb_call_hold(1);
    cli_connect(0);
    run(2);
    fnb_stall(1);
    tsf_fin(0);
    run(2);
    TF_EQ(ts_state(0), TS_CLOSED);
    TF_EQ(fnb_pending(), 1);
    run(1);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(fnb_pending(), 0);
    fnb_latency(3);
    srv_listen(0);
    cli_connect(0);
    run(1);
    tsf_fin(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_EQ(srv_wait(0), NB_OK);
    lsn = nba_lsn(g_srv, 0);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    srv_listen(0);
    cli_connect(0);
    run(1);
    ev_clear();
    TF_EQ(br_stop(), 0);
    TF_STREQ(g_ev, "0 CLOSE STOP;");
    TF_EQ(srv_wait(0), NB_OK);
    lsn = nba_lsn(g_srv, 0);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    finish();
}

/* TS_SLOTS sessions at once, each with its own bytes, and one client
   more refused; a slot freed takes the next client. */
static void test_slots(void)
{
    u8 lsn[TS_SLOTS];
    u8 b;
    u8 k;

    setup();
    start_br();
    for (k = 0; k < TS_SLOTS; k++) {
        lsn[k] = open_slot(k);
        TF_EQ(br_sessions(), k + 1);
    }
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;1 OPEN 10.0.2.2:1025;"
             "1 CALL ;2 OPEN 10.0.2.2:1026;2 CALL ;3 OPEN 10.0.2.2:1027;"
             "3 CALL ;");
    TF_EQ(tsf_connect(BR_PORT, &k), TSF_REFUSED);
    TF_EQ(tsf_refused(), 1);
    for (k = 0; k < TS_SLOTS; k++) {
        b = (u8)('A' + k);
        TF_EQ(tsf_send(k, &b, 1), 1);
    }
    run(1);
    for (k = 0; k < TS_SLOTS; k++) {
        srv_recv(k, lsn[k]);
        TF_EQ(srv_got(k), 1);
        TF_EQ(g_buf[0], 'A' + k);
    }
    tsf_rst(2);
    run(2);
    TF_EQ(br_sessions(), 3);
    srv_recv(2, lsn[2]);
    TF_EQ(srv_wait(4), NB_RC_SESSCLOSED);
    open_slot(2);
    TF_EQ(br_sessions(), 4);
    finish();
}

/* br_poll's count of commands and pieces, and no events once the
   receiver is gone. */
static void test_counts(void)
{
    u8 lsn;

    setup();
    start_br();
    TF_EQ(pass(), 0);
    srv_listen(0);
    cli_connect(0);
    TF_EQ(pass(), 1);
    TF_EQ(pass(), 1);
    TF_EQ(pass(), 1);
    TF_EQ(pass(), 0);
    TF_EQ(srv_wait(0), NB_OK);
    lsn = nba_lsn(g_srv, 0);
    TF_EQ(tsf_send(0, "HELLO", 5), 5);
    TF_EQ(pass(), 1);
    srv_recv(0, lsn);
    TF_EQ(pass(), 1);
    TF_EQ(srv_got(0), 5);
    srv_send(0, lsn, "+OK\r\n", 5);
    TF_EQ(pass(), 3);
    TF_EQ(pass(), 0);
    br_events(NULL, NULL);
    tsf_fin(0);
    run(2);
    TF_EQ(ts_state(0), TS_LISTEN);
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1024;0 CALL ;");
    finish();
}

/* Each pass begins with the slot after the last pass's first. */
static void test_rotation(void)
{
    u8 k;

    setup();
    start_br();
    for (k = 0; k < TS_SLOTS; k++)
        cli_connect(k);
    pass();
    TF_STREQ(g_ev, "1 OPEN 10.0.2.2:1025;2 OPEN 10.0.2.2:1026;"
             "3 OPEN 10.0.2.2:1027;0 OPEN 10.0.2.2:1024;");
    ev_clear();
    pass();
    TF_STREQ(g_ev, "2 NOCALL 12;3 NOCALL 12;0 NOCALL 12;1 NOCALL 12;");
    pass();
    for (k = 0; k < TS_SLOTS; k++) {
        TF_EQ(ts_state(k), TS_LISTEN);
        cli_connect(k);
    }
    ev_clear();
    pass();
    TF_STREQ(g_ev, "0 OPEN 10.0.2.2:1028;1 OPEN 10.0.2.2:1029;"
             "2 OPEN 10.0.2.2:1030;3 OPEN 10.0.2.2:1031;");
    finish();
}

/* br_stop with slot 0 relaying, 1 calling, 2 draining, 3 listening;
   then with slot 0 closing. */
static void test_stop(void)
{
    u8 lsn;
    u8 lsn2;
    u8 k;
    u16 n;

    setup();
    start_br();
    lsn = open_slot(0);
    fnb_call_hold(1);
    cli_connect(1);
    run(1);
    lsn2 = open_slot(2);
    tsf_noack(2);
    srv_send(2, lsn2, "BYE", 3);
    TF_EQ(nb_hangup(lsn2), NB_OK);
    run(3);
    TF_EQ(ts_state(2), TS_OPEN);
    ev_clear();
    TF_EQ(br_stop(), 0);
    TF_STREQ(g_ev, "0 CLOSE STOP;1 CLOSE STOP;");
    for (k = 0; k < TS_SLOTS; k++)
        TF_EQ(ts_state(k), TS_CLOSED);
    TF_EQ(tsf_recv(0, g_buf, NB_MSGMAX, &n), TSF_GONE);
    srv_recv(0, lsn);
    TF_EQ(srv_wait(0), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 1);
    finish();
    setup();
    start_br();
    cli_connect(0);
    tsf_nofinack(0);
    run(2);
    TF_EQ(ts_state(0), TS_CLOSING);
    ev_clear();
    TF_EQ(br_stop(), 0);
    TF_STREQ(g_ev, "");
    finish();
}

/* Read n bytes at off of the block b into g_buf and compare them with
   s. */
static int far_is(cmem_blk *b, u16 off, const char *s, u16 n)
{
    cmem_get(b, off, g_buf, n);
    return memcmp(g_buf, s, n) == 0;
}

/* Slots 0, 1 and 2 each hold a message the client has no room for;
   then slot 0 has bytes whose SEND was refused, slot 1 a SEND
   completed but not yet seen, slot 2 a SEND whose cancel the provider
   never confirms. */
static void stuck_slots(void)
{
    u8 lsn[3];
    u8 k;

    for (k = 0; k < 3; k++) {
        lsn[k] = open_slot(k);
        tsf_room(k, 0);
        srv_send(k, lsn[k], "HELLO", 5);
    }
    run(1);
    tsf_send(1, "PASS1", 5);
    tsf_send(2, "PASS2", 5);
    run(1);
    fnb_latency(5);
    srv_recv(1, lsn[1]);
    fnb_toomany(1);
    tsf_send(0, "PASS0", 5);
    run(1);
}

#define KEPTN           3       /* stuck stops and refusals in a row    */

static cmem_blk *g_watch;       /* the block watch_free looks for, until
                                   it is freed                          */
static u16 g_wfrees;            /* its frees since it was set           */
static u16 g_frees;             /* far blocks freed                     */
static u16 g_allocs;            /* far blocks allocated                 */

/* cmem_fault: count the far blocks allocated, failing none. */
static int count_alloc(u16 size)
{
    TF_CHECK(size >= 1);
    g_allocs++;
    return 0;
}

/* cmem_freeing: count the far blocks freed and the frees of g_watch,
   which must be zeroed by then, as it held the relayed text. */
static void watch_free(cmem_blk *b)
{
    g_frees++;
    if (b == g_watch) {
        TF_CHECK(zero_far(b, 0, cmem_size(b)));
        g_wfrees++;
        g_watch = NULL;
    }
}

/* br_stop with a cancel never confirmed. */
static void test_stuck(void)
{
    u16 held;
    u16 left;
    int i;

    fnb_reset();
    held = tm_held();
    setup();
    start_br();
    stuck_slots();
    g_watch = br_buffers();
    g_wfrees = 0;
    cmem_freeing = watch_free;
    fnb_stall(3);
    fnb_hold(1);
    ev_clear();
    left = br_stop();
    TF_EQ(left, 1);
    if (TF_EQ(g_wfrees, 0) && left == 1) {
        TF_CHECK(zero_far(g_watch, 0, 5));
        TF_CHECK(far_is(g_watch, NB_MSGMAX, "HELLO", 5));
        TF_CHECK(zero_far(g_watch, 2 * NB_MSGMAX, 5));
        TF_CHECK(far_is(g_watch, 4 * NB_MSGMAX, "PASS2", 5));
    }
    TF_STREQ(g_ev, "0 CLOSE STOP;1 CLOSE STOP;2 CLOSE STOP;");
    TF_EQ(br_stop(), 0);
    TF_EQ(g_wfrees, 0);
    TF_CHECK(br_buffers() == NULL);
    TF_CHECK(zero_near(br_bounce(), BR_BOUNCE));
    for (i = 0; i < SPINS && nba_stuck() != 0; i++)
        TF_EQ(g_wfrees, 0);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(srv_got(1), 5);
    TF_CHECK(memcmp(g_buf, "PASS1", 5) == 0);
    finish();
    cmem_freeing = NULL;
    TF_EQ(g_wfrees, 1);
    fnb_reset();
    TM_HELD(held);
}

/* Open a session on slot 0 of the started bridge, relay the client's
   PASS0 into a SEND the server never receives, and stop the bridge
   while the provider holds that SEND through every cancel: br_stop
   returns 1 and frees nothing, the block, PASS0 in it, kept in
   g_watch. */
static void stuck_br(void)
{
    u16 h;

    open_slot(0);
    tsf_send(0, "PASS0", 5);
    run(2);
    g_watch = br_buffers();
    g_wfrees = 0;
    TF_EQ(fnb_pending(), 2);
    fnb_stall(SPINS);
    fnb_hold(1);
    h = tm_held();
    g_frees = 0;
    TF_EQ(br_stop(), 1);
    TF_EQ(g_frees, 0);
    TM_HELD(h);
    TF_CHECK(br_buffers() == NULL);
    if (TF_CHECK(g_watch != NULL))
        TF_CHECK(far_is(g_watch, 0, "PASS0", 5));
}

/* While the provider still owns the kept block's NCB, br_start is
   refused with BR_EARG and NB_OK, taking no set or block, and br_stop
   returns 0; neither frees a block, nor touches the kept one, whose
   PASS0 the provider may still be sending. */
static void refused_br(void)
{
    u16 h;
    u8 rc;

    h = tm_held();
    g_frees = 0;
    g_allocs = 0;
    rc = NB_RC_PENDING;
    TF_EQ(br_start("srv", "bridge", NULL, 0, &rc), BR_EARG);
    TF_EQ(rc, NB_OK);
    TF_EQ(fnb_sets(), 1);
    TF_CHECK(br_buffers() == NULL);
    TF_CHECK(g_watch != NULL && far_is(g_watch, 0, "PASS0", 5));
    TF_EQ(br_stop(), 0);
    TF_CHECK(g_watch != NULL && far_is(g_watch, 0, "PASS0", 5));
    TF_EQ(g_allocs, 0);
    TF_EQ(g_frees, 0);
    TM_HELD(h);
    TF_EQ(fnb_pending(), 1);
}

/* br_start succeeds, freeing the kept block, zeroed, once, and taking
   a new one. */
static void start_freed(void)
{
    g_allocs = 0;
    g_frees = 0;
    start_br();
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    TF_EQ(g_allocs, 1);
}

/* br_stop of the started bridge with nothing stuck frees its block
   once; then finish. */
static void stop_clean(void)
{
    g_frees = 0;
    TF_EQ(br_stop(), 0);
    TF_EQ(g_frees, 1);
    finish();
}

/* A stuck stop keeps the block, the provider may still write into it:
   br_start is refused until the provider owns no NCB, and the first
   br_start or br_stop after that frees the block, zeroed, once (a start
   refused for its arguments too), so that at most one block is kept
   however often the bridge is stopped stuck. */
static void test_kept(void)
{
    cmem_blk *b;
    u16 held;
    u16 blk;
    u16 h;
    int i;
    u8 rc;

    /* Heap blocks of one far block: 0 off the host. */
    h = tm_held();
    b = cmem_alloc(1);
    TF_CHECK(b != NULL);
    blk = (u16)(tm_held() - h);
    cmem_free(b);

    fnb_reset();
    held = tm_held();
    setup();
    cmem_fault = count_alloc;
    cmem_freeing = watch_free;
    start_br();
    stuck_br();
    for (i = 0; i < KEPTN; i++)
        refused_br();

    /* Once the provider has given every NCB back, a start refused for
       its name frees the kept block, and the next frees nothing. */
    fnb_stall(0);
    TF_EQ(nba_stuck(), 0);
    h = tm_held();
    g_frees = 0;
    TF_EQ(br_start("", NULL, NULL, 0, &rc), BR_EARG);
    TF_EQ(rc, NB_OK);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    TM_HELD(h - blk);
    TF_EQ(br_start("", NULL, NULL, 0, &rc), BR_EARG);
    TF_EQ(br_stop(), 0);
    TF_EQ(g_frees, 1);
    TM_HELD(h - blk);

    /* Stuck stop, refused start and, once the provider gives the NCB
       back (no stall left), a start freeing the block, over and over:
       never more than the live block and the kept one; then a stop
       freeing it. */
    g_allocs = 0;
    start_br();
    TF_EQ(g_allocs, 1);
    for (i = 0; i < KEPTN; i++) {
        stuck_br();
        refused_br();
        fnb_stall(0);
        start_freed();
    }
    stuck_br();
    fnb_stall(0);
    g_frees = 0;
    TF_EQ(br_stop(), 0);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    finish();
    fnb_reset();
    TM_HELD(held);

    /* A reset of the provider in place of the release: the next stop,
       or the next start, frees the kept block once. */
    setup();
    start_br();
    stuck_br();
    refused_br();
    fnb_reset();
    h = tm_held();
    g_frees = 0;
    TF_EQ(br_stop(), 0);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    TM_HELD(h - blk);
    TF_EQ(br_stop(), 0);
    TF_EQ(g_frees, 1);
    cmem_free(g_sb);
    ts_term();
    setup();
    start_br();
    stuck_br();
    refused_br();
    cmem_free(g_sb);
    ts_term();
    setup();
    start_freed();
    stop_clean();
    cmem_fault = NULL;
    cmem_freeing = NULL;
    fnb_reset();
    TM_HELD(held);
}

int main(void)
{
    tf_begin("T_BRIDGE");
    cmem_init();
    test_args();
    test_given();
    test_noname();
#ifdef COMPAT_HOST
    test_retry();
#endif
    test_fail();
    test_nocall();
    test_allow();
    test_retried();
    test_cancel();
    test_slots();
    test_counts();
    test_rotation();
    test_stop();
    test_stuck();
    test_kept();
    test_order();
    test_forward();
    test_deliver();
    test_client_ends();
    test_server_ends();
    test_lost();
    test_wipe();
    fnb_reset();
    tsf_reset();
    return tf_done();
}

/*
 * T_TNET3.C - the third part of the T_TNETB test (T_TNETB.H): the
 * start-up phase and the phase of the receive block a stuck stop
 * keeps, which main of T_TNETB.C runs.
 *
 * Checks:
 * - before tnetb_start and after tnetb_stop a poll does nothing; the
 *   refusals of tnetb_start, each with its NetBIOS code and nothing
 *   left behind: no provider, every argument out of range, a second
 *   start, the name in the table or at another station, a full name
 *   table, no far memory (and on the host no near memory) at every
 *   allocation in turn;
 * - a tnetb_stop that leaves NCBs with the provider frees no block;
 *   while the provider owns one, tnetb_start is refused (TNETB_EARG,
 *   NB_OK) taking no set or block, and tnetb_stop returns 0, neither
 *   freeing a block, however often; once it owns none (or after a
 *   reset of the provider), the first tnetb_start, refused for its name
 *   or not, or tnetb_stop zeroes and frees the kept block, once, a start
 *   then taking a new one; stuck stops and starts over and over keep
 *   at most one block, and leave none behind (on the host, where the
 *   blocks are counted).
 */
#define COMPAT_NETBIOS
#define COMPAT_FIND
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TNETB.H"
#define TF_PART
#define TF_FILE "T_TNET3"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKENB.H"
#include "T_TNETB.H"

static u16 g_allocs;            /* far allocations since the count began */
static u16 g_failat;            /* the one of them that fails           */

/* Count the far allocations, failing number g_failat (0: none). */
static int fail_nth(u16 size)
{
    TF_CHECK(size >= 1);
    return ++g_allocs == g_failat;
}

/* tnetb_start(name, sessions, listens) gives want with the NetBIOS code
   nbrc and leaves no name, set, LISTEN or slot behind. */
static void refused(const char *name, u8 sessions, u8 listens, int want,
                    u8 nbrc)
{
    u16 names;
    u8 rc;

    names = fnb_names();
    rc = NB_RC_PENDING;
    TF_EQ(tnetb_start(name, sessions, listens, &rc), want);
    TF_EQ(rc, nbrc);
    TF_EQ(fnb_names(), names);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(tnetb_poll(g_now), 0);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(nstate(CONN_FREE), CONN_MAX);
}

void test_start(void)
{
    char name[NB_NAMELEN + 1];
    u16 h;
    int i;
    int r;
    u8 rc;
    u8 num;
    u8 lsn;

    fnb_reset();
    TF_EQ(tnetb_poll(g_now), 0);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(conn_init(g_e, NULL, CONN_MAX, 0, g_now), 0);
    refused(SRV, 2, 1, TNETB_ENONB, NB_RC_NOT_PRESENT);
    TF_EQ(nb_present(), 1);
    memset(name, 'N', NB_NAMELEN);
    name[NB_NAMELEN] = '\0';
    refused(name, 2, 1, TNETB_EARG, NB_OK);
    refused("", 2, 1, TNETB_EARG, NB_OK);
    refused(SRV, 0, 1, TNETB_EARG, NB_OK);
    refused(SRV, CONN_NETMAX + 1, 1, TNETB_EARG, NB_OK);
    refused(SRV, 2, 0, TNETB_EARG, NB_OK);
    refused(SRV, 2, 3, TNETB_EARG, NB_OK);

    /* The name in the table, the table full, the name elsewhere. */
    TF_EQ(nb_add_name(SRV, &num), NB_OK);
    refused(SRV, 2, 1, TNETB_ENAME, NB_RC_DUPNAME);
    TF_EQ(nb_delete_name(SRV), NB_OK);
    strcpy(name, "FILL0");
    for (i = 0; i < FNB_NAMES; i++) {
        name[4] = (char)('A' + i);
        TF_EQ(nb_add_name(name, &num), NB_OK);
    }
    refused(SRV, 2, 1, TNETB_ENB, NB_RC_NAMEFULL);
    for (i = 0; i < FNB_NAMES; i++) {
        name[4] = (char)('A' + i);
        TF_EQ(nb_delete_name(name), NB_OK);
    }
    fnb_dupname(SRV);
    refused(SRV, 2, 1, TNETB_ENAME, NB_RC_NAMEINUSE);
    fresh();

    /* No memory at each allocation in turn, far then near; the fake
       keeps a closed set until its reset, so tm_held is read after
       one. */
    h = tm_held();
    for (i = 1; i < LOOPS; i++) {
        g_allocs = 0;
        g_failat = (u16)i;
        cmem_fault = fail_nth;
        r = tnetb_start(SRV, CONN_NETMAX, 2, &rc);
        cmem_fault = NULL;
        if (r == TNETB_OK)
            break;
        TF_EQ(r, TNETB_ENOMEM);
        TF_EQ(rc, NB_OK);
        TF_EQ(fnb_names(), 0);
        TF_EQ(fnb_sets(), 0);
        fresh();
        TM_HELD(h);
    }
    TF_CHECK(i > 1 && i < LOOPS);
    TF_EQ(tnetb_stop(), 0);
#ifdef COMPAT_HOST
    for (i = 1; i < LOOPS; i++) {
        tm_fail((u16)i);
        r = tnetb_start(SRV, CONN_NETMAX, 2, &rc);
        tm_fail(0);
        if (r == TNETB_OK)
            break;
        TF_EQ(r, TNETB_ENOMEM);
        TF_EQ(rc, NB_OK);
        TF_EQ(fnb_names(), 0);
        TF_EQ(fnb_sets(), 0);
        fresh();
        TM_HELD(h);
    }
    TF_CHECK(i > 1 && i < LOOPS);
    TF_EQ(tnetb_stop(), 0);
#endif
    fresh();
    TM_HELD(h);

    /* The longest name; a second start; nothing posted before a pass; a
       call met before the transport has seen it is hung up at the
       stop. */
    memset(name, 'L', NB_NAMELEN - 1);
    name[NB_NAMELEN - 1] = '\0';
    TF_EQ(tnetb_start(name, 1, 1, &rc), TNETB_OK);
    TF_EQ(rc, NB_OK);
    TF_EQ(tnetb_start(SRV, 1, 1, &rc), TNETB_EARG);
    TF_EQ(fnb_names(), 1);
    TF_EQ(fnb_sets(), 1);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(tnetb_poll(g_now), 1);
    TF_EQ(tnetb_listening(), 1);
    TF_EQ(fnb_peer_call("PEER", name, &lsn), NB_OK);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_peer_hangup(lsn), NB_RC_SESSCLOSED);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(tnetb_poll(g_now), 0);
    TF_EQ(nstate(CONN_FREE), CONN_MAX);
    conn_free();
}

/* ---------------------------------------------------------------------
 * The receive block a stuck stop keeps.
 * --------------------------------------------------------------------- */

#define KEPTN           3       /* stuck stops and refusals in a row    */
#define KEPTFILL        0x5A    /* every byte of a block once kept      */

static cmem_blk *g_watch;       /* the block kept, until it is freed    */
static u16 g_wfrees;            /* frees of g_watch                     */
static u16 g_frees;             /* far blocks freed                     */

/* 1 when every byte of block b is v. */
static int all_of(cmem_blk *b, u8 v)
{
    u8 buf[64];
    u16 off;
    u16 n;
    u16 i;

    for (off = 0; off < cmem_size(b); off = (u16)(off + n)) {
        n = (u16)(cmem_size(b) - off);
        if (n > (u16)sizeof buf)
            n = (u16)sizeof buf;
        cmem_get(b, off, buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] != v)
                return 0;
    }
    return 1;
}

/* cmem_freeing: count the far blocks freed and the frees of g_watch,
   which must be all zero by then, as it held requests. */
static void count_free(cmem_blk *b)
{
    g_frees++;
    if (b != g_watch)
        return;
    TF_CHECK(all_of(b, 0));
    g_wfrees++;
    g_watch = NULL;
}

/* Open a session from station on the started transport, answer a PING
   on it, and stop the transport while the provider holds the session's
   RECEIVE and the LISTEN through every cancel: the stop returns 2 and
   frees nothing, the block kept in g_watch.  Every byte of it is then
   set to KEPTFILL, as the provider may still write into it, so that
   only the transport's zeroing passes count_free's check, whatever the
   block held. */
static void stuck_stop(const char *station)
{
    u16 h;
    u16 off;
    u16 len;
    u8 l;

    l = call(station);
    TF_STREQ(ask(l, "PING"), k_ok);
    g_watch = fnb_lastbuf(0, &off, &len);
    TF_CHECK(g_watch != NULL);
    g_wfrees = 0;
    TF_EQ(fnb_pending(), 2);
    fnb_stall(LOOPS);
    fnb_hold(2);
    h = tm_held();
    g_frees = 0;
    TF_EQ(tnetb_stop(), 2);
    TF_EQ(g_frees, 0);
    TM_HELD(h);
    if (g_watch != NULL)
        cmem_set(g_watch, 0, KEPTFILL, cmem_size(g_watch));
}

/* While the provider still owns the kept block's NCBs, a start is
   refused with TNETB_EARG and NB_OK, taking no set or block, and a stop
   returns 0; neither frees a block, nor touches the kept one, which the
   provider may still be writing into. */
static void refused_kept(void)
{
    u16 h;

    h = tm_held();
    g_frees = 0;
    g_allocs = 0;
    refused(SRV, 2, 1, TNETB_EARG, NB_OK);
    TF_CHECK(g_watch != NULL && all_of(g_watch, KEPTFILL));
    TF_EQ(tnetb_stop(), 0);
    TF_CHECK(g_watch != NULL && all_of(g_watch, KEPTFILL));
    TF_EQ(g_allocs, 0);
    TF_EQ(g_frees, 0);
    TM_HELD(h);
    TF_EQ(fnb_pending(), 2);
}

/* A start that succeeds, freeing the kept block, zeroed, once, and
   taking a new one. */
static void start_freed(void)
{
    u8 rc;

    g_allocs = 0;
    g_frees = 0;
    rc = NB_RC_PENDING;
    TF_EQ(tnetb_start(SRV, 2, 1, &rc), TNETB_OK);
    TF_EQ(rc, NB_OK);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    TF_EQ(g_allocs, 1);
}

/* A stop of the started transport with nothing stuck frees its block
   once; then finish. */
static void stop_clean(void)
{
    g_frees = 0;
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(g_frees, 1);
    finish();
}

/* A stuck stop keeps the receive block, the provider may still write
   into it: a start is refused until the provider owns no NCB, and the
   first start or stop after that frees the block, zeroed, once (a start
   refused for its arguments too), so that at most one block is kept
   however often the transport is stopped stuck. */
void test_kept(void)
{
    cmem_blk *b;
    u16 blk;
    u16 h;
    int i;
    u8 rc;
    static char station[] = "PEER?";   /* the fifth letter set per stop */

    /* Heap blocks of one far block: 0 off the host. */
    h = tm_held();
    b = cmem_alloc(1);
    TF_CHECK(b != NULL);
    blk = (u16)(tm_held() - h);
    cmem_free(b);

    g_failat = 0;
    cmem_fault = fail_nth;
    cmem_freeing = count_free;
    begin(2, 1, 0);
    stuck_stop("PEERA");
    for (i = 0; i < KEPTN; i++)
        refused_kept();

    /* Once the provider has given every NCB back, a start refused for
       its name frees the kept block, and the next frees nothing. */
    fnb_stall(0);
    TF_EQ(nba_stuck(), 0);
    h = tm_held();
    g_frees = 0;
    refused("", 2, 1, TNETB_EARG, NB_OK);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    TM_HELD(h - blk);
    refused("", 2, 1, TNETB_EARG, NB_OK);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(g_frees, 1);
    TM_HELD(h - blk);

    /* Stuck stop, refused start and, once the provider gives the NCBs
       back (no stall left), a start freeing the block, over and over:
       never more than the live block and the kept one; then a stop
       freeing it. */
    g_allocs = 0;
    rc = NB_RC_PENDING;
    TF_EQ(tnetb_start(SRV, 2, 1, &rc), TNETB_OK);
    TF_EQ(g_allocs, 1);
    for (i = 0; i < KEPTN; i++) {
        station[4] = (char)('B' + i);
        stuck_stop(station);
        refused_kept();
        fnb_stall(0);
        start_freed();
    }
    station[4] = (char)('B' + i);
    stuck_stop(station);
    fnb_stall(0);
    g_frees = 0;
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    finish();

    /* A reset of the provider in place of the release: the next stop,
       or the next start, frees the kept block once. */
    begin(2, 1, 0);
    stuck_stop("PEERA");
    refused_kept();
    fresh();
    h = tm_held();
    g_frees = 0;
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(g_wfrees, 1);
    TF_EQ(g_frees, 1);
    TM_HELD(h - blk);
    TF_EQ(tnetb_stop(), 0);
    TF_EQ(g_frees, 1);
    finish();
    begin(2, 1, 0);
    stuck_stop("PEERA");
    refused_kept();
    fresh();
    start_freed();
    stop_clean();
    cmem_fault = NULL;
    cmem_freeing = NULL;
}

/*
 * T_BRIDGE.H - what the two source files of the T_BRIDGE test share:
 * the server's set and buffers, the clock and the event log, the
 * helpers of T_BRIDGE.C, and the tests of T_BRIDG2.C that main runs.
 *
 * The test plays both ends of the bridge: the clients through TSFAKE's
 * peer calls, and the server through a set of no-wait NCBs on the same
 * simulated provider, NCB 2k for the LISTEN and then the RECEIVEs of
 * the session the bridge's slot k calls, NCB 2k + 1 for its SENDs, each
 * NCB with an area of NB_MSGMAX bytes at h * NB_MSGMAX in one far
 * block.  It is split in two compilation units, T_BRIDGE.C (start,
 * names, CALLs, slots, stop, main) and T_BRIDG2.C (relaying, ends and
 * wipes), so that each stays within the compiler memory of Watcom C
 * 6.5.
 */
#ifndef T_BRIDGE_H
#define T_BRIDGE_H

#define SRVNCBS         (2 * TS_SLOTS)  /* NCBs of the server's set     */
#define EVSZ            256     /* bytes of the event log               */
#define SPINS           64      /* tries a wait of the test makes       */

extern nba *g_srv;              /* the server's NCBs                    */
extern cmem_blk *g_sb;          /* their areas                          */
extern u32 g_now;               /* the tick of the next pass            */
extern char g_ev[EVSZ];         /* the events since ev_clear            */
extern u8 g_buf[NB_MSGMAX];     /* scratch of the client and server     */

/* A fresh provider with the name SRV and the server's set, a fresh
   stack started, events logged into g_ev, the clock at 0. */
void setup(void);

/* br_start("srv", "bridge"), which must succeed. */
void start_br(void);

/* Stop the bridge, close the server's set and the stack, and check
   that no NCB is left in progress and no HANG UP was refused. */
void finish(void);

/* One pass at g_now, ts_poll then br_poll, after which the clock moves
   on a tick: br_poll's count. */
u16 pass(void);

/* n passes, one tick apart. */
void run(u16 n);

/* Passes one tick apart until g_now is t. */
void run_to(u32 t);

/* Empty the event log. */
void ev_clear(void);

/* A client connects, taken by slot k. */
void cli_connect(u8 k);

/* Post the server's LISTEN on NCB 2k. */
void srv_listen(u8 k);

/* A session on slot k: the server's LISTEN, a client's connection
   taken by slot k and passes until the bridge holds the session; the
   server's LSN. */
u8 open_slot(u8 k);

/* Poll NCB h of the server until it completes, SPINS times at most:
   its final code, NB_RC_PENDING when it did not complete. */
u8 srv_wait(u16 h);

/* Post the server's RECEIVE of session lsn on NCB 2k, or its SEND of
   the len bytes of src on NCB 2k + 1. */
void srv_recv(u8 k, u8 lsn);
void srv_send(u8 k, u8 lsn, const void *src, u16 len);

/* What the server's RECEIVE on NCB 2k received, into g_buf: its
   length, after checking that it completed with NB_OK. */
u16 srv_got(u8 k);

/* The test pattern: n bytes of the stream seed from offset at into
   buf, and 1 when buf holds them. */
void fill(u8 *buf, u16 n, u16 at, u8 seed);
int same(const u8 *buf, u16 n, u16 at, u8 seed);

/* 1 when the n bytes at off of b are all zero, or those at p. */
int zero_far(cmem_blk *b, u16 off, u16 n);
int zero_near(const u8 *p, u16 n);

/* The tests of T_BRIDG2.C. */
void test_order(void);
void test_forward(void);
void test_deliver(void);
void test_client_ends(void);
void test_server_ends(void);
void test_lost(void);
void test_wipe(void);

#endif

/*
 * T_FAKENB.H - what the two source files of the T_FAKENB test share:
 * the set and buffers every test uses, the helpers of T_FAKENB.C, and
 * the tests of T_FAKEN2.C that main runs.
 *
 * The test is split in two compilation units, T_FAKENB.C (absence,
 * names, pairing, messages, hang-ups, main) and T_FAKEN2.C (limits,
 * latency, cancels, stalls and holds, timeouts, the pump, the host's
 * fault cases), so that each stays within the compiler memory of
 * Watcom C 6.5.
 */
#ifndef T_FAKENB_H
#define T_FAKENB_H

#define BUFSZ           NB_MSGMAX       /* each test buffer             */

extern nba *g_s;                /* the set of every test                */
extern cmem_blk *g_b;           /* RECEIVE and SEND buffers             */
extern u8 g_msg[BUFSZ];
extern u8 g_got[BUFSZ];
extern u16 g_pumps;             /* pump calls                           */
extern u16 g_sendat;            /* the pump call that sends, 0 none     */
extern u8 g_peer;               /* the remote end the pump sends from   */
extern u8 g_nested;             /* the pump ran inside itself           */

/* A pattern that differs at every byte of a message. */
void fill(u8 *buf, u16 n, u8 seed);

/* The pump: counts its calls, polls NCB 0 of the set arg inside (which
   must not run it again), and sends a 10-byte message of g_msg from
   g_peer at call g_sendat. */
void pump(void *arg);

/* A fresh provider with SRV and CLI in the name table and a set of n
   NCBs. */
void start(u16 n);

/* A session from the remote PEER to SRV, by a LISTEN on h with the
   timeouts rto and sto: the remote end's LSN, the local one in *lsn. */
u8 take_call(u16 h, u8 *lsn, u8 rto, u8 sto);

/* Close the set and check that nothing is left of it. */
void finish(void);

/* Note what NCB h gives (nba_poll, nba_lsn, nba_len, nba_callname), then
   check that it still gives the same, as after a refused post. */
void snap(u16 h);
void same(u16 h);

/* The tests of T_FAKEN2.C. */
void test_limits(void);
void test_cancel(void);
void test_hold(void);
void test_wait(void);
void test_timeout(void);
#ifdef COMPAT_HOST
void test_host(void);
#endif

#endif

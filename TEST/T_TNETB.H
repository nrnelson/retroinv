/*
 * T_TNETB.H - what the three source files of the T_TNETB test share:
 * the clock, the remote stations' helpers of T_TNETB.C and the phases
 * of T_TNET3.C and T_TNET2.C.
 *
 * Every phase runs the transport over the simulated provider
 * (FAKENB.H) and the real CONN and engine.  The server's name is SRV;
 * a remote station calls it with fnb_peer_call and is driven by the
 * test, which runs the server's loop (tnetb_poll, conn_service,
 * conn_timeouts at g_now) between its own steps.  begin starts a
 * phase, finish ends it and checks that the transport left nothing
 * behind: no command in progress, no set, name or session end, every
 * slot free, no hang-up of a session the provider had removed, a
 * CONN_EV_CLOSE for every CONN_EV_OPEN, and on the host no more heap
 * blocks than when the phase began.
 *
 * The test is split in three compilation units, T_TNETB.C (the helpers
 * and main), T_TNET3.C (start-up, the block a stuck stop keeps) and
 * T_TNET2.C (LISTENs, CONN_NETMAX sessions, retried posts, the rest
 * after a failed LISTEN, messages, the wipe of the receive buffers,
 * flow control, the ends of a session, stop and shutdown), because
 * Watcom C 6.5 runs out of compiler memory on fewer.
 */
#ifndef T_TNETB_H
#define T_TNETB_H

#define SRV             "INVSRV"        /* the server's name            */
#define LOOPS           64      /* passes a step may take at most       */
#define REPSZ           CONN_OUTSZ      /* bytes of the reply buffer    */

extern const char k_ok[];       /* "+OK" and CR LF                      */
extern u32 g_now;               /* the clock every server call is given */
extern char *g_rep;             /* the bytes reply took, NUL ended      */
extern u16 g_replen;
extern u16 g_msgs;              /* messages reply took                  */
extern u16 g_msg1;              /* the length of the first of them      */
extern u16 g_opens;             /* CONN_EV_OPEN and CONN_EV_CLOSE seen  */
extern u16 g_closes;
extern char g_station[ENG_STALEN + 1];  /* the text of the last OPEN    */
extern eng *g_e;                /* the engine every phase serves        */

/* The helpers of T_TNETB.C, each described where it is defined. */
void fresh(void);
void begin(u8 sessions, u8 listens, u32 idle);
void finish(void);
u16 pass(void);
void run(u16 n);
u8 call(const char *from);
u16 reply(u8 lsn, u16 want);
const char *ask(u8 lsn, const char *line);
void put(u8 lsn, const char *text);
u16 nstate(u8 state);
int ended(u16 closes, u16 others);

/* The phases of T_TNET3.C and T_TNET2.C. */
void test_start(void);
void test_kept(void);
void test_slots(void);
void test_full(void);
void test_retry(void);
void test_rest(void);
void test_data(void);
void test_wipe(void);
void test_flow(void);
void test_big(void);
void test_both(void);
void test_end(void);
void test_stall(void);
void test_stop(void);

#endif

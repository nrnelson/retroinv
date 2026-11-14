/*
 * T_TSER.H - what the two source files of the T_TSER test share: the
 * clock, the remote stations' helpers of T_TSER.C and the call that
 * runs the phases of T_TSER2.C.
 *
 * Every phase runs the transport over the simulated ports (FAKEUART.H)
 * and the real CONN and engine, the test playing the remote stations
 * on COM1 and COM2 with fu_peer_send and fu_peer_recv and running the
 * server's loop (tserial_poll, conn_service, conn_timeouts at g_now)
 * between its own steps.  begin starts a phase, finish ends it and
 * checks that the transport left nothing behind: every port closed
 * with all that uart_open set up undone, every slot free, a
 * CONN_EV_CLOSE for every CONN_EV_OPEN, and on the host no more heap
 * blocks than when the phase began.  The test is split in two
 * compilation units, T_TSER.C (the helpers, start-up, the first byte,
 * info and stop) and T_TSER2.C (the ends of a session, flow control,
 * pacing, the rings, IMPORT, two ports and shutdown), because Watcom C
 * 6.5 runs out of compiler memory on fewer.
 */
#ifndef T_TSER_H
#define T_TSER_H

#define PW              "secret1"       /* ADMIN's password, once set   */
#define LOOPS           256     /* passes a step may take at most       */
#define REPSZ           CONN_OUTSZ      /* bytes of the reply buffer    */
#define BOTH            3       /* tserial_start's bits of COM1, COM2   */

extern const char k_ok[];       /* "+OK" and CR LF                      */
extern u32 g_now;               /* the clock every server call is given */
extern char *g_rep;             /* the bytes reply took, NUL ended      */
extern u16 g_replen;
extern u16 g_opens;             /* CONN_EV_OPEN and CONN_EV_CLOSE seen  */
extern u16 g_closes;
extern char g_station[ENG_STALEN + 1];  /* the text of the last OPEN    */
extern eng *g_e;                /* the engine every phase serves        */

/* The helpers of T_TSER.C, each described where it is defined. */
void begin(u8 ports, u8 flags, u32 idle);
void finish(void);
u16 pass(void);
void run(u16 n);
void put(u8 port, const char *text);
u16 reply(u8 port);
const char *ask(u8 port, const char *line);
void hello(u8 port, const char *station);
void login(u8 port);
u16 nstate(u8 state);
void fail_alloc(u16 nth);
int ringzero(u8 port);

/* Run every phase of T_TSER2.C, one after another. */
void test_tser2(void);

#endif

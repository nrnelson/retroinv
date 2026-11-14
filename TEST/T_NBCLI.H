/*
 * T_NBCLI.H - what the two source files of the T_NBCLI test share: the
 * scripted server, the line collector, and the call that runs the tests
 * of T_NBCL2.C.
 *
 * The server is a local end of the simulated provider: the name SRV
 * with a LISTEN posted on NCB SRX of its own set, a RECEIVE kept posted
 * there once the client has called, and SENDs on the other NCBs, each
 * with a slot of NB_MSGMAX bytes in one far block.  It works only inside
 * the pump (fnb_pump), which the provider runs inside every nba_poll
 * call and refused CALL, so it answers while the client waits: messages
 * queued with srv_queue go out in order as SEND NCBs come free, and the
 * events of srv_at run at the pump call they name, counted from
 * srv_at's last reset.  The test is split in two compilation units,
 * T_NBCLI.C (the harness, names, open failures, main) and T_NBCL2.C
 * (requests, responses, waits, ends, stuck commands), so that each
 * stays within the compiler memory of Watcom C 6.5.
 */
#ifndef T_NBCLI_H
#define T_NBCLI_H

#define SRV             "SRV"           /* the server's name            */
#define SRX             0               /* its LISTEN, then RECEIVE     */
#define SQ              8               /* its SEND NCBs                */
#define RXMAX           3072            /* bytes the server keeps       */
#define LNMAX           3072            /* bytes of the lines collected */
#define OUTMAX          4096            /* bytes queued to send         */

/* Events of srv_at. */
#define EV_RECV         1       /* start receiving                      */
#define EV_HANGUP       2       /* hang up the server's end             */
#define EV_ABORT        3       /* fnb_abort the session                */
#define EV_SEND         4       /* queue the text of the event          */

extern u16 g_pumps;             /* pump calls since the last srv_at(0)  */
extern u8 g_slsn;               /* the server's end, 0 none             */
extern u8 g_clsn;               /* the client's end                     */
extern u8 g_rxend;              /* final code ending the server's
                                   RECEIVEs, 0 none                     */
extern char g_rx[RXMAX];        /* what the server received             */
extern u16 g_nrx;
extern u16 g_nmsg;              /* messages it came in                  */
extern char g_ln[LNMAX];        /* lines collected, each ended by '|'   */
extern u16 g_nln;
extern u16 g_lines;             /* calls of take                        */
extern u16 g_failat;            /* the call of take that fails, 0 none  */
extern u8 g_lbad;               /* take saw a wrong argument            */

/* A fresh provider with SRV in the name table and its LISTEN posted,
   the pump set, and, on the host, the near blocks held noted. */
void fresh(void);

/* Open the client from local (NULL: the default name) to the server,
   check it reached SRV, and start the server receiving. */
nbcli *connect(const char *local);

/* Stop the pump, hang up the server's end, and check that the provider
   and, on the host, the near heap hold nothing more than at fresh. */
void finish(void);

/* Run the server once outside the pump. */
void srv_run(void);

/* Queue len bytes of s as one message to the client. */
void srv_queue(const char *s, u16 len);
void srv_text(const char *s);

/* Start the server receiving (EV_RECV), or stop it: its RECEIVE is
   cancelled. */
void srv_recv(int on);

/* The code of the server's first SEND NCB, which takes the first
   message queued while none is in progress. */
u8 srv_sendrc(void);

/* Hang up the server's end now. */
void srv_hangup(void);

/* Run event ev (EV_) at pump call at, with the text s for EV_SEND; at 0
   forgets every event and resets g_pumps. */
void srv_at(u16 at, u8 ev, const char *s);

/* The nbcli_line_fn of every response: appends the line and '|' to
   g_ln; returns 1 at call g_failat.  arg must be &g_lines. */
int take(void *arg, const char *line, u16 len);

/* Clear the collector and the server's received bytes. */
void clear(void);

/* nbcli_response into the collector, waiting at most w seconds per
   message; g_pumps goes on counting from the last srv_at(0, ...). */
int resp(nbcli *n, u16 w);

/* nbcli_send of the C string s. */
int sendl(nbcli *n, const char *s);

/* Run every test of T_NBCL2.C, one after another. */
void test_nbcl2(void);

#endif

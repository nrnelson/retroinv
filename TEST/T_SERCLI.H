/*
 * T_SERCLI.H - what the two source files of the T_SERCLI test share:
 * the scripted server, the line collector, and the call that runs the
 * tests of T_SERCL2.C.
 *
 * The client opens COM1 of the simulated ports (FAKEUART.H); the test
 * is the remote station on its line.  The server works inside the pump
 * (fu_pump), which the ports run at the start of every uart_ call, so
 * it answers while the client waits: each pump call takes what the
 * client has sent into g_rx, answers a BYE with "+OK" when g_byeok is
 * set, and runs the events of srv_at at the pump call they name,
 * counted from srv_at's last reset.  The test is split in two
 * compilation units, T_SERCLI.C (the harness, opening, requests and
 * the framing, main) and T_SERCL2.C (waits, the room of the transmit
 * ring, the close, the wipes, receive errors), so that each stays within
 * the compiler memory of Watcom C 6.5.
 */
#ifndef T_SERCLI_H
#define T_SERCLI_H

#define PORT            1               /* the client's port            */
#define RXMAX           4096            /* bytes the server keeps       */
#define LNMAX           4096            /* bytes of the lines collected */

/* Events of srv_at. */
#define EV_SEND         1       /* send the text of the event           */
#define EV_RTSOFF       2       /* drop the server's RTS, COM1's CTS    */
#define EV_RTSON        3       /* raise it                             */

extern u16 g_pumps;             /* pump calls since the last srv_at(0)  */
extern char g_rx[RXMAX];        /* what the server received, NUL ended  */
extern u16 g_nrx;
extern char g_ln[LNMAX];        /* lines collected, each ended by '|'   */
extern u16 g_nln;
extern u16 g_lines;             /* calls of take                        */
extern u16 g_failat;            /* the call of take that fails, 0 none  */
extern u8 g_byeok;              /* the server answers BYE with "+OK"    */

/* Fresh ports, COM1 made, the pump set, and, on the host, the heap
   blocks held noted. */
void fresh(void);

/* Open the client on COM1 at UART_DEFBAUD with RTS/CTS. */
sercli *connect(void);

/* Stop the pump, and check that COM1 is closed, its set-up undone, and
   that, on the host, the heap holds no more than at fresh. */
void finish(void);

/* Run the server once outside the pump. */
void srv_run(void);

/* Send s, without its NUL, to the client now. */
void srv_text(const char *s);

/* Run event ev (EV_) at pump call at, with the text s for EV_SEND; at
   0 forgets every event and resets g_pumps. */
void srv_at(u16 at, u8 ev, const char *s);

/* Forget what the server received and the lines collected. */
void clear(void);

/* sercli_line_fn: append the line and '|' to g_ln; fails (1) at the
   g_failat-th call. */
int take(void *arg, const char *line, u16 len);

/* sercli_send of the text s; sercli_response into take. */
int sendl(sercli *s, const char *text);
int resp(sercli *s, u16 waitsecs);

/* 1 when the client's copy of a line of n bytes it sent holds only
   zeros: the first n bytes, at most SC_CHUNK, of its far block
   (SERCLI.C), which is the first member of the session. */
int txwiped(sercli *s, u16 n);

/* Run every test of T_SERCL2.C, one after another. */
void test_sercl2(void);

#endif

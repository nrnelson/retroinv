/*
 * T_CONN.H - what the two source files of the T_CONN test share: the
 * clock, the shared texts, the fake transport of T_CONN.C and its
 * helpers, and the phases of T_CONN2.C.
 *
 * The fake transport keeps one far input buffer per slot and records
 * every callback CONN makes; a callback made against the rules of
 * CONN.H (a kick with no output waiting, an rx_done with no input
 * handed over or with a byte of it left, a second close of a session,
 * any call for a slot whose session has ended, an event text too long
 * or holding the password) is counted, and stop, which ends every
 * phase, checks that none was.
 *
 * The test is split in three compilation units, T_CONN.C (the fake
 * transport, the helpers, the table and session phases, and main),
 * T_CONN2.C (input, fairness, the host's lines, paging, idle time,
 * shutdown and dropped input) and T_CONN3.C (login tickets, the
 * throttle, and memos saved by two sessions at once), because Watcom
 * C 6.5 runs out of compiler memory on fewer.
 */
#ifndef T_CONN_H
#define T_CONN_H

#define NS              4       /* slots of the tables most phases use  */
#define IDLE            (3 * TMR_10SEC) /* idle timeout: 30 seconds     */
#define PW              "secret1"       /* ADMIN's password, once set   */
#define LONGLINE        (PROTO_MAXLINE + 1) /* the shortest over-long   */

extern const char k_ok[];       /* "+OK" and CR LF                      */
extern const char k_kind[];     /* the fake transport's kind            */
extern const char k_login[];    /* LOGIN as ADMIN with PW               */
extern u32 g_now;               /* the clock every CONN call is given   */
extern char *g_out;             /* the output drain took, NUL ended     */
extern u16 g_served;            /* what the last ask's pass returned    */
extern int g_defer;             /* close leaves conn_closed to the phase */
extern u16 g_rxdone[CONN_MAX];
extern u16 g_closes[CONN_MAX];
extern char g_order[CONN_MAX + 1];      /* slots of the REQ events of a
                                           pass, as digits              */

/* The helpers of T_CONN.C, each described where it is defined, and the
   fake transport's ops of a mailbox. */
extern const conn_tops g_mbx;
int claim_as(const conn_tops *ops, u8 *c);
int out_has(u8 c, const char *s);
int slots_have(const char *s);
void start(const char *readonly, u8 nslots, u32 idle);
void stop(void);
int claim(u8 *c);
u8 open_slot(const char *station);
void hang(u8 c);
void put_in(u8 c, const char *bytes, u16 len);
void put_text(u8 c, const char *text);
int rx_clean(u8 c);
u16 drain(u8 c, u16 piece);
const char *ask(u8 c, const char *line);
void login(u8 c, const char *station);
const char *ev(u8 c);

/* The phases of T_CONN2.C. */
void test_input(void);
void test_fair(void);
void test_host(void);
void test_paging(void);
void test_idle(void);
void test_shutdown(void);
void test_drop(void);

/* The phases of T_CONN3.C. */
void test_tickets(void);
void test_throttle(void);
void test_notes(void);

#endif

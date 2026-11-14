/*
 * T_NETE.H - what the three source files of the T_NETE test share: the
 * server run from the provider's pump, the requests over a link, and the
 * phases of T_NETE2.C and T_NETE3.C.
 *
 * One program is both ends: the client's links (LINK.H, over NBCLI.H)
 * and the server (TNETB.H, CONN.H and the engine on the scratch data
 * directory) talk over the simulated provider (FAKENB.H).  The server
 * runs only from the provider's pump, so every pass of its loop happens
 * while a client waits for its call, its SEND or its RECEIVE; a step
 * that waits for the server alone runs the pump function itself.  The
 * clock the server is given never moves, so every IDLE is 0.
 *
 * The test is split in three compilation units, T_NETE.C (the server),
 * T_NETE2.C (the wait for the server's slots, the wipe check, two links
 * at once, the ends of a session, the wipes) and T_NETE3.C (the
 * clients' helpers, the same bytes over NET as over LOCAL, and main),
 * because Watcom C 6.5 runs out of compiler memory on fewer; T_NETE3.C
 * needs no engine, protocol or server header.
 */
#ifndef T_NETE_H
#define T_NETE_H

#define NSLOTS          2       /* the server's slots and sessions      */
#define LOOPS           64      /* server passes a step takes at most   */
#define BADPW           "wrong1"        /* not ADMIN's password         */

/* The buffer of the last SEND or RECEIVE posted, as fnb_lastbuf
   gives it. */
typedef struct {
    cmem_blk *b;
    u16 off;
    u16 len;
} tn_buf;

extern tn_text g_rep;           /* the responses ask or feed took       */
extern char g_req[];            /* the last CONN_EV_REQ text            */
extern u16 g_opens;             /* CONN_EV_OPEN and CONN_EV_CLOSE seen  */
extern u16 g_closes;
extern tn_buf g_cli;            /* the last SEND a client posted        */
extern tn_buf g_srv;            /* the last RECEIVE the server posted   */

/* The helpers of T_NETE.C, T_NETE2.C and T_NETE3.C, each described
   where it is defined. */
int srv_begin(const char *readonly);
void srv_end(void);
void srv_run(void);
void srv_until(u16 n);
void wiped(void);
link *cli_open(void);
int talk(link *l, const char *line, tn_text *t);
const char *ask(link *l, const char *line, int want);
const char *feed(link *l, const char *text, int want);

/* The phases of T_NETE2.C and T_NETE3.C. */
void test_two(void);
void test_shutdown(void);
void test_same(const char *golden);

#endif

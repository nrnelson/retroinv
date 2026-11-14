/*
 * T_TMBX.H - what the three source files of the T_TMBX test share:
 * the clock, the helpers of T_TMBX.C and the phases of T_TMBX.C,
 * T_TMBX2.C and T_TMBX3.C.
 *
 * Every phase runs the mailbox transport on the mailbox of the scratch
 * directory, whose REQUEST and RESPONSE directories main makes, over
 * the real CONN and engine, which keep their data files in the same
 * directory.  The test plays the clients, putting request files in
 * place and reading the responses, and runs the server's loop
 * (tmbox_poll, conn_service, conn_timeouts at g_now) between its own
 * steps.  begin starts a phase, finish ends it and checks that the
 * transport left nothing behind: every slot free, a CONN_EV_CLOSE for
 * every CONN_EV_OPEN, no file handle and, on the host, no heap block
 * more than when the phase began.
 *
 * The test is split in three compilation units, T_TMBX.C (the helpers,
 * main and the phases of start-up and service), T_TMBX2.C (READONLY=,
 * the looks, the failures and the ends of a session) and T_TMBX3.C
 * (the purge of a request as it is read), because Watcom C 6.5 runs out
 * of compiler memory on one.
 */
#ifndef T_TMBX_H
#define T_TMBX_H

#define LOOPS           256     /* passes a step may take at most       */
#define TEXTSZ          2048    /* bytes of a file text reads           */
#define TB_ZBLK        64      /* bytes zeros reads at a time          */
#define PW              "SECRET9"       /* ADMIN's password, once set   */

extern u32 g_now;               /* the clock every server call is given */
extern eng *g_e;                /* the engine every phase serves        */
extern char g_opened[TEXTSZ];   /* the stations opened, each and '|'    */
extern u16 g_opens;             /* CONN_EV_OPEN, CONN_EV_REQ and        */
extern u16 g_reqs;              /*   CONN_EV_CLOSE seen                 */
extern u16 g_closes;
extern u16 g_fails;             /* failures the watch function got      */
extern u8 g_fslot;              /* the last of them                     */
extern char g_fsta[ENG_STALEN + 1];
extern int g_ferr;

/* The helpers of T_TMBX.C, each described where it is defined. */
void begin(const char *readonly);
void finish(void);
u16 pass(void);
void run(u16 n);
void post(const char *sta, const char *text);
const char *text(const char *dir, const char *name);
const char *serve(const char *sta);
int zeros(const char *dir, const char *name, u32 size);
int holds(const char *p, u16 n, const char *s);
void fails_reset(void);

/* The phases of T_TMBX.C, T_TMBX2.C and T_TMBX3.C. */
void test_start(void);
void test_one(void);
void test_lines(void);
void test_order(void);
#ifdef COMPAT_HOST
void test_device(void);
#endif
void test_readonly(void);
void test_look(void);
void test_stale(void);
void test_held(void);
void test_full(void);
void test_rename(void);
void test_zero(void);
void test_badopen(void);
void test_ends(void);
void test_purge(void);

#endif

/*
 * T_INVSRV.H - what the two source files of the T_INVSRV test share:
 * the client and the key source, the settings file and the runs of the
 * server (T_INVSRV.C), used by the phases of T_INVSR2.C.
 *
 * The test is split in three compilation units, T_INVSRV.C (the
 * helpers, the sessions and main), T_INVSR2.C (the start-up failures,
 * the far allocation sweep, the stuck stops, the F3 runs, the burst,
 * midnight and the event log) and T_INVSR3.C (the serial, mailbox and
 * starvation runs), because Watcom C 6.5 runs out of compiler memory on
 * fewer.
 */
#ifndef T_INVSRV_H
#define T_INVSRV_H

#define PATHMAX         96
#define RESPMAX         512     /* bytes of the longest response        */

/* The client: a remote station of the provider. */
typedef struct {
    const char *const *script;  /* NULL: no client                      */
    char resp[RESPMAX];         /* the response being read              */
    u16  rlen;
    u16  step;                  /* requests sent                        */
    u8   lsn;                   /* its end, 0 before the call           */
    u8   wait;                  /* a response is awaited                */
    u8   ended;                 /* the server ended the session         */
    u8   active;                /* a client runs                        */
} peer;

/* The key source's state. */
typedef struct {
    u16  wait;                  /* peeks left before F3 and Y           */
    u16  limit;                 /* peeks after which F3 and Y come
                                   whatever the client, 0 for never     */
    u16  peeks;
    u16  late;                  /* peeks with the screen closed         */
    u8   given;                 /* keys read: 1 F3, 2 F3 and Y          */
    u8   snow;                  /* vid_snow_active() when F3 was read   */
    u8   stuck;                 /* the NCBs are held past the stop      */
    u8   endkey;                /* a key, not the provider, ends the
                                   stop's wait                          */
    u8   xread;                 /* keys read during the stop's wait     */
    u16  no;                    /* the key that answers an early F3, 0
                                   for no such F3                       */
    u8   asked;                 /* of those: 1 F3 read, 2 the answer    */
    u8   up;                    /* the question showed at the answer    */
    u8   hint;                  /* the key hints showed at the last F3  */
    u32  t3;                    /* tmr_ticks() when F3 was read         */
    u32  span;                  /* ticks from then to the stop's wait   */
} keyst;

extern const char k_name[];     /* the server's name                    */
extern const char k_ok[];       /* "+OK" and CR LF, kept once           */
extern const char *const k_mbdirs[];    /* the mailbox's directories    */
#ifdef COMPAT_HOST
extern const char *const s_idle[];  /* the script that only says HELLO  */
#endif
extern char g_mdir[];           /* the message file's directory         */
extern u8 g_held;               /* ports the test holds open, a bit for
                                   each as in SERIAL=                   */
extern peer g_peer;             /* the client of the run                */
extern keyst g_keys;            /* the key source of the run            */
extern u16 g_spin;              /* ticks the key source spins a pass    */
extern u32 g_last;              /* vclock() at the client's last answer */
extern u32 g_closed;            /* vclock() when the session ended      */
extern u16 g_names;             /* names the test itself added          */

/* A tick count that goes on across midnight, as the server's does:
   g_vt, brought up to date from g_vlast (a tmr_ticks value) by vclock,
   which counts the midnights it passes in g_wraps. */
extern u32 g_vt;
extern u32 g_vlast;
extern u16 g_wraps;
u32 vclock(void);

/* 1 when s matches the pattern p, whose '*' matches any run of bytes. */
int glob(const char *s, const char *p);

/* 1 when r holds a whole response: a line, or the lines of a +DATA
   response up to its lone ".". */
int resp_done(const char *r);

/* What the last run wrote on standard error, CRs dropped, in a static
   buffer; "" when it cannot be read (a failed check). */
const char *err_text(void);

/* 1 when err_text() matches the pattern p (glob); else 0, the text
   printed. */
int err_is(const char *p);

/* Write the settings file: DATA=data, then each of the other lines
   given (NULL for none). */
void put_cfg(const char *data, const char *l1, const char *l2);

/* "LOG=" and the path of the event log base.LOG of the scratch
   directory, in a static buffer. */
const char *log_cfg(const char *base);

/* A fresh provider and no serial port (FAKEUART.H): a run that serves
   one makes it with fu_port, whose queues take far memory that the
   other runs need on MS-DOS 3.10.  The client plays script (NULL for
   none), and the key source gives F3 and Y after wait peeks. */
void reset(const char *const *script, u16 wait);

/* Make COM1, on a 16550A, for a run that serves it. */
void make_com1(void);

/* Run the server with the settings put_cfg wrote, the message file from
   mdir and the key source; return its status after checking that the
   server ended the client's session, if there was one, and that the
   provider holds nothing. */
int run(const char *mdir);

/* T_INVSR2.C. */
#ifdef COMPAT_HOST
/* A key of the stop's wait for the NCBs (keys, stuck set): the message
   file is still loaded and CONN's table still started (conn_init
   refuses to start it again), since a SEND the provider owns may be
   sending from a slot's block.  The third peek lets the provider confirm
   the cancels, or with endkey types a key that ends the wait. */
#define NBURST          60      /* PINGs of the burst                   */

u16 stuck_key(keyst *k, int peek);

/* The burst client arg (a peer), run from the provider's pump: call
   the server, send NBURST PINGs in one message, read their answers,
   then BYE and wait for the server to end the session. */
void burster(void *arg);
#endif

/* 1 when the event log base.LOG holds the n lines of want after their
   time stamps, and nothing else, a password nowhere; else 0, the first
   difference failing a check. */
int log_is(const char *base, const char *const *want, u16 n);

/* The checks of the parts, which only main calls: the parts do not
   read them, which keeps them within Watcom C 6.5's memory. */
#ifndef TF_PART
/* T_INVSR2.C. */
#ifdef COMPAT_HOST
void test_stuck(void);
void test_burst(void);
void test_midnight(void);
#endif
void test_refusals(void);
void test_far(void);
void test_f3(void);
void test_newlog(void);
void check_log(void);

/* T_INVSR3.C. */
void test_serial(void);
void test_mbox(void);
void test_mbtkt(void);
void test_throttle(void);
void test_tktrans(void);
void test_racelog(void);
#ifdef COMPAT_HOST
void test_starve(void);
#endif
#endif

#endif

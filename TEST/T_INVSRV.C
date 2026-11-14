/*
 * T_INVSRV.C - the server's run (SRVRUN.H) over the simulated provider
 * (FAKENB.H) and serial ports (FAKEUART.H) and a scratch mailbox, with
 * the real transports, slots, screen and engine.
 *
 * usage: T_INVSRV [-m msgdir] dir
 *
 * dir is a scratch directory prefix ("OBJ/HOST/INVST/", "D:\") naming an
 * existing directory that holds nothing but this test's files, which
 * are removed at the start and, when every check passed, at the end; it
 * is also the data directory and the mailbox.  msgdir holds the
 * server's message file MSG_SRVFILE, by default the program's own
 * directory.  A remote station run from the provider's pump plays a
 * client, and a key source gives F3 and Y once the client is done.
 * After every run nothing is left in the provider: no command, NCB set,
 * name or session, and no serial port is open; what the run wrote on
 * standard error, caught in a scratch file, is checked too.  Checks:
 * - the start-up failures and their exit statuses: a bad settings line,
 *   the name in use, a full name table, a missing data directory, a log
 *   that cannot be created, a message file made for another version;
 *   and every far allocation failed in turn, with COM1 and the mailbox
 *   served too, which stops the start with SRV_EXIT_MSG, SRV_EXIT_OPEN
 *   or SRV_EXIT_NOMEM, in the order the start takes them, until the run
 *   succeeds;
 * - SESSIONS=0 with SERIAL=COM1: no NetBIOS provider looked for, and a
 *   session on COM1 (kind COM1 in ADMIN CLIENTS, its station from HELLO)
 *   logged with an OPEN line without text, the port opened at BAUD= and
 *   with RTSCTS='s flow control; a port of SERIAL= missing (its START and
 *   STOP 10 logged),
 *   the other one closed again, and a port open already stop the start
 *   with SRV_EXIT_PORT; a missing mailbox with SRV_EXIT_MBOX, the port
 *   and the name of a start that got further undone;
 * - SESSIONS=0 with MAILBOX=: a request file served as its station's
 *   session, the request gone and the response in place, the rest after
 *   BYE ignored; a leftover RESPONSE\*.TMP whose rename fails at the
 *   start logged as an MBOX line on slot 0 and put in place later;
 * - a mailbox client's login ticket: asked for with the password, used
 *   in its next file, ended there by BYE TICKET=END and refused in the
 *   file after, which closes at once; the ticket in no line of the log;
 *   and with MAILBOX= no NetBIOS or serial session offered tickets;
 * - the failed-login throttle across sessions: after THR_FAILS failed
 *   logins of ADMIN over two sessions of a serial line, without
 *   MAILBOX=, or in request files of their own, the right password is
 *   refused with 401 too;
 * - a session: HELLO, LOGIN of the seeded ADMIN, its password change,
 *   STATUS with the server's K UPTIME= and K SESSIONS=1 lines, ADMIN
 *   CLIENTS listing it, ITEM LIST, ADMIN SHUTDOWN answered +OK, then a
 *   request refused with 501; the run ends with SRV_EXIT_OK and the
 *   server's message texts are unknown again;
 * - the session run's log commits are fewer than its lines;
 * - READONLY= naming the client's station caps its login at level 1;
 * - on the host, where ticks are virtual: IDLE=1 closes an idle session
 *   after a minute of ticks and before twice that; a LISTEN whose cancel
 *   the provider never confirms ends the loop 2 * CONN_SHUTWAIT ticks
 *   (and less than STOPSLACK more) after F3, then the stop waits, its
 *   message file still loaded and CONN's table not yet freed, until the
 *   provider gives the NCB up, or until a key, which the stop reads,
 *   saying so (MD_STUCK); once the provider gave it up, every far block
 *   the run took is freed; the same with a session open, whose CLOSE
 *   still reaches the event log and whose receive block is zeroed and
 *   freed only after its RECEIVE is over; a
 *   burst of NBURST PINGs, every pass busy, has its log lines committed
 *   at least once and at most once every TMR_SEC ticks; a HELLO on COM1
 *   sent as such a burst begins is answered within STVPASSES passes,
 *   most of the burst still to come; and with IDLE=
 *   a day and the clock spun across midnight, the session closes a day
 *   of ticks after its last answer and the server runs on past a second
 *   midnight;
 * - F3 answered n or Esc with a session open: the requests sent while
 *   the question shows and after the answer are served, the session
 *   ends by its own BYE and the key hints are back at the next F3;
 * - F3 and Y with no session open end a run with SRV_EXIT_OK, the screen
 *   closed (SNOW=1, which the host's colour screen applies, no longer
 *   active); a run with LOG= commits more often than one without, and
 *   an idle run commits its new lines once, not on every idle pass;
 * - SRV_EXIT_IO for the log's close failing, the last data file close
 *   failing, and the log's last write failing;
 * - a missing log created, closed and opened again, so that it is held
 *   denying writes; an existing log opened at once; a failed close or
 *   reopen of the new log stopping the start with SRV_EXIT_IO and its
 *   code; a log another program makes after the open that found it
 *   missing opened with its lines kept, not truncated;
 * - the event log, line by line with the time stamp's form, and the new
 *   password nowhere in it.
 * T_INVSR2.C holds the start-up failures, the F3, burst and midnight
 * runs, the new log and the log check; T_INVSR3.C the serial, mailbox
 * and starvation runs and the log another program makes.
 * A server without a provider is the host run of INVSERV itself, where
 * the provider is absent.  Prints a summary line "T_INVSRV PASS n"; exit
 * status 0 when every check passes, 1 when one fails, 2 for a usage
 * error.
 */
#define COMPAT_NETBIOS
#define COMPAT_UART
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "SRVRUN.H"
#include "TMBOX.H"
#include "TESTFW.H"
#include "FAKENB.H"
#include "FAKEUART.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "T_INVSRV.H"

#define NSESSLOG        13      /* the lines of the session run         */
#define ERRMAX          512     /* bytes of a run's standard error      */
#define ASKSTEPS        3       /* requests answered when the early F3
                                   is answered                          */
#define ASKNEVER        0xFFFF  /* ask_steps: the client never stops    */

/* Where standard error goes between the runs. */
#ifdef COMPAT_HOST
#define NULDEV          "/dev/null"
#else
#define NULDEV          "NUL"
#endif

const char k_name[] = "RETROINV";
const char k_ok[] = "+OK\r\n";

/* How the screen's bottom row starts: the question of F3 with one
   session open, and the key hints. */
static const char k_bask[] = " Sessions open: 1.";
static const char k_bhint[] = " F2=";
const char *const k_mbdirs[] = { TMBOX_REQDIR, TMBOX_RSPDIR, NULL };
static const char *const g_extra[] = {
    "SRV.CFG", "EV.LOG", "BU.LOG", "GU.LOG", "NL.LOG", "SE.LOG", "MB.LOG",
    "PF.LOG", "TK.LOG",
    "ERR.TXT", "INVSERV.MSG",
    NULL
};

/* Scripts: a request, then the response it must get ('*' matches any
   run of bytes); a NULL request ends the script, after which the client
   waits for the server to end the session. */
static const char *const s_session[] = {
    "HELLO PEER", k_ok,
    "LOGIN ADMIN admin", "+OK LEVEL=3 MUSTCHG=1\r\n",
    "PASSWD admin secret01", k_ok,
    "STATUS", "+DATA\r\n*\r\nK UPTIME=*\r\nK SESSIONS=1\r\n.\r\n",
    "ADMIN CLIENTS", "+DATA\r\nF SLOT TYPE STATION USER LEVEL STATE IDLE "
    "SELF\r\nR 1 NETB PEER ADMIN 3 3 0 1\r\n.\r\n",
    "ITEM LIST", "+DATA\r\nF ITEM_ID *\r\n.\r\n",
    "ADMIN SHUTDOWN", k_ok,
    "PING", "-ERR 501 *\r\n",
    NULL
};
static const char *const s_readonly[] = {
    "HELLO PEER", k_ok,
    "LOGIN ADMIN secret01", "+OK LEVEL=1 *\r\n",
    "ADMIN SHUTDOWN", "-ERR 203 *\r\n",
    "BYE", k_ok,
    NULL
};
#ifdef COMPAT_HOST
const char *const s_idle[] = {
    "HELLO PEER", k_ok,
    NULL
};
#endif

char g_mdir[PATHMAX];
u8 g_held;
peer g_peer;
keyst g_keys;
u16 g_names;
u16 g_spin;
u32 g_last;
u32 g_closed;

u32 g_vt;
u32 g_vlast;
u16 g_wraps;

u32 vclock(void)
{
    u32 d;

    d = tmr_since(g_vlast);
    g_vlast += d;
    if (g_vlast >= (u32)TMR_DAY) {
        g_vlast -= (u32)TMR_DAY;
        g_wraps++;
    }
    g_vt += d;
    return g_vt;
}

/* Spin the host's virtual clock on by g_spin ticks. */
static void spin(void)
{
    u16 i;

    for (i = 0; i < g_spin; i++)
        tmr_ticks();
    vclock();
}

int glob(const char *s, const char *p)
{
    if (*p == '\0')
        return *s == '\0';
    if (*p == '*')
        return glob(s, p + 1) || (*s != '\0' && glob(s + 1, p));
    return *s == *p && glob(s + 1, p + 1);
}

/* 1 when s ends with e. */
static int ends(const char *s, const char *e)
{
    size_t n;
    size_t m;

    n = strlen(s);
    m = strlen(e);
    return n >= m && strcmp(s + n - m, e) == 0;
}

int resp_done(const char *r)
{
    if (strncmp(r, "+DATA\r\n", 7) == 0)
        return ends(r, "\r\n.\r\n");
    return ends(r, "\r\n");
}

/* The requests the client stops at, until they are answered and the
   key source's early F3, or its answer, is read: none without one. */
static u16 ask_steps(void)
{
    if (g_keys.no == 0 || g_keys.asked == 2)
        return ASKNEVER;
    return g_keys.asked ? ASKSTEPS : 1;
}

/* The client arg (a peer), run from the provider's pump: call the
   server, send each request of its script and check its response, then
   wait for the server to end the session. */
static void client(void *arg)
{
    peer *c;
    const char *want;
    char line[48];
    u16 n;
    u8 rc;

    c = (peer *)arg;
    if (c->ended)
        return;
    if (c->lsn == 0) {
        if (fnb_peer_call("PEER", k_name, &c->lsn) != NB_OK)
            c->lsn = 0;
        return;
    }
    if (c->wait) {
        rc = fnb_peer_recv(c->lsn, c->resp + c->rlen,
                           (u16)(RESPMAX - 1 - c->rlen), &n);
        if (rc == NB_RC_PENDING)
            return;
        if (!TF_CHECK(rc == NB_OK || rc == NB_RC_INCOMPLETE)) {
            c->ended = 1;
            return;
        }
        c->rlen = (u16)(c->rlen + n);
        c->resp[c->rlen] = '\0';
        if (resp_done(c->resp)) {
            g_last = vclock();
            want = c->script[2 * c->step - 1];
            if (!TF_CHECK(glob(c->resp, want)))
                printf("  got \"%s\"\n", c->resp);
            c->wait = 0;
        }
        return;
    }
    if (c->step >= ask_steps())
        return;
    if (c->script[2 * c->step] != NULL) {
        strcpy(line, c->script[2 * c->step]);
        strcat(line, "\r\n");
        if (!TF_EQ(fnb_peer_send(c->lsn, line, (u16)strlen(line)), NB_OK)) {
            c->ended = 1;
            return;
        }
        c->step++;
        c->rlen = 0;
        c->resp[0] = '\0';
        c->wait = 1;
        return;
    }
    rc = fnb_peer_recv(c->lsn, c->resp, RESPMAX - 1, &n);
    if (rc != NB_RC_PENDING) {
        TF_EQ(rc, NB_RC_SESSCLOSED);
        c->ended = 1;
        g_closed = vclock();
    }
}

/* 1 when the screen's bottom row starts with s. */
static int bar_starts(const char *s)
{
    u8 cells[VID_ROWBYTES];
    u16 i;

    vid_getrow(VID_ROWS - 1, cells);
    for (i = 0; s[i] != '\0'; i++)
        if (cells[2 * i] != (u8)s[i])
            return 0;
    return 1;
}

/* Key source arg (a keyst): F3 and then Y, typed ahead once the
   client, if any, is done and wait peeks have passed, or once limit
   peeks have passed, whatever the client, until they are read; then
   nothing.
   With no set, first an F3 once the client's first request is
   answered, and no once ASKSTEPS are, the question on the screen
   meanwhile; the client waits for each of the two keys.
   With stuck set, a key after the screen has closed (the stop's wait
   for the NCBs, the snow avoidance of SNOW=1 then no longer active)
   goes to stuck_key.  With g_spin set, each peek, one a pass, spins the
   clock on by g_spin ticks until the client is done and a second
   midnight has passed; F3 comes only then. */
static u16 keys(void *arg, int peek)
{
    keyst *k;
    int forced;

    k = (keyst *)arg;
    if (peek)
        k->peeks++;
#ifdef COMPAT_HOST
    if (k->stuck && k->given && !vid_snow_active())
        return stuck_key(k, peek);
#endif
    if (g_spin != 0 && peek && (!g_peer.ended || g_wraps < 2)) {
        spin();
        return KEY_NONE;
    }
    if (ask_steps() != ASKNEVER) {
        if (g_peer.wait || g_peer.step < ask_steps())
            return KEY_NONE;
        if (k->asked == 0) {
            if (!peek)
                k->asked = 1;
            return KEY_F3;
        }
        if (!peek) {
            k->asked = 2;
            k->up = (u8)bar_starts(k_bask);
        }
        return k->no;
    }
    forced = k->limit != 0 && k->peeks > k->limit;
    if (k->given == 0 &&
        (forced || !g_peer.active || g_peer.ended)) {
        if (!forced && k->wait > 0) {
            k->wait--;
            return KEY_NONE;
        }
        if (!peek) {
            k->given = 1;
            k->snow = (u8)vid_snow_active();
            k->hint = (u8)bar_starts(k_bhint);
            k->t3 = tmr_ticks();
        }
        return KEY_F3;
    }
    if (k->given == 1) {
        if (!peek)
            k->given = 2;
        return 'Y';
    }
    return peek ? KEY_NONE : KEY_EOS;
}

void put_cfg(const char *data, const char *l1, const char *l2)
{
    char t[3 * PATHMAX];

    sprintf(t, "DATA=%s\r\n%s%s%s%s", data, l1 != NULL ? l1 : "",
            l1 != NULL ? "\r\n" : "", l2 != NULL ? l2 : "",
            l2 != NULL ? "\r\n" : "");
    TF_EQ(tfs_write_all(tfs_path("SRV", ".CFG"), t, (u16)strlen(t)), 0);
}

const char *log_cfg(const char *base)
{
    static char t[PATHMAX + 4];

    sprintf(t, "LOG=%s", tfs_path(base, ".LOG"));
    return t;
}

void reset(const char *const *script, u16 wait)
{
    fnb_reset();
    fu_reset();
    memset(&g_peer, 0, sizeof g_peer);
    g_peer.script = script;
    g_peer.active = (u8)(script != NULL);
    fnb_pump(script != NULL ? client : NULL, &g_peer);
    memset(&g_keys, 0, sizeof g_keys);
    g_keys.wait = wait;
}

void make_com1(void)
{
    fu_port(1, UART_T_16550A);
}

/* Check that the provider holds nothing but the test's own g_names
   names, and that no port but those of g_held is open. */
static void clean(void)
{
    u8 p;

    TF_EQ(nba_stuck(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_names(), g_names);
    TF_EQ(fnb_sessions(), 0);
    for (p = 1; p <= UART_MAXPORT; p++)
        if (!(g_held & (1 << (p - 1))))
            TF_EQ(fu_isrstate(p), 0);
}

const char *err_text(void)
{
    static char t[ERRMAX];
    u16 got;
    u16 i;
    u16 n;

    got = 0;
    TF_EQ(tfs_read_all(tfs_path("ERR", ".TXT"), t, ERRMAX - 1, &got), 0);
    for (i = 0, n = 0; i < got; i++)
        if (t[i] != '\r')
            t[n++] = t[i];
    t[n] = '\0';
    return t;
}

int err_is(const char *p)
{
    const char *t;

    t = err_text();
    if (glob(t, p))
        return 1;
    printf("  standard error: \"%s\"\n", t);
    return 0;
}

int run(const char *mdir)
{
    char cfg[PATHMAX];
    char err[PATHMAX];
    int st;

    strcpy(cfg, tfs_path("SRV", ".CFG"));
    strcpy(err, tfs_path("ERR", ".TXT"));
    TF_CHECK(freopen(err, "w", stderr) != NULL);
    key_set_source(keys, &g_keys);
    st = srv_run(cfg, mdir);
    key_set_source(NULL, NULL);
    /* Closed, so that DOS writes the file's size for err_text. */
    TF_CHECK(freopen(NULDEV, "w", stderr) != NULL);
    fnb_pump(NULL, NULL);
    fu_pump(NULL, NULL);
    if (g_peer.lsn != 0)
        TF_EQ(fnb_peer_hangup(g_peer.lsn), NB_RC_SESSCLOSED);
    clean();
    return st;
}

/* A run whose F3, typed with the session open, is answered no: the
   requests sent while the question shows and after the answer are
   served, the session ends by its own BYE, and the second F3 and Y
   shut down. */
static void asked_run(u16 no)
{
    reset(s_readonly, 0);
    g_keys.no = no;
    put_cfg(tfs_dir(), "READONLY=PEER", NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_peer.step, 4);
    TF_EQ(g_peer.wait, 0);
    TF_EQ(g_peer.ended, 1);
    TF_EQ(g_keys.asked, 2);
    TF_EQ(g_keys.up, 1);
    TF_EQ(g_keys.hint, 1);
    TF_EQ(g_keys.given, 2);
    TF_CHECK(err_is(""));
}

static void test_session(void)
{
    u16 commits[2];
    int logged;
#ifdef COMPAT_HOST
    u32 t0;
    u32 t1;
#endif

    /* The log's commits are fewer than its lines: they wait for a pass
       with nothing to do.  The session on new data without a log gives
       the commits of the data files alone. */
    for (logged = 0; logged < 2; logged++) {
        te_remove(tfs_dir(), g_extra);
        reset(s_session, 0);
        put_cfg(tfs_dir(), logged ? log_cfg("EV") : NULL, NULL);
        tfs_fault(TFS_OP(CF_OP_COMMIT), 0, 0, 0);
        TF_EQ(run(g_mdir), SRV_EXIT_OK);
        tfs_fault_off();
        commits[logged] = tfs_count(CF_OP_COMMIT);
    }
    TF_CHECK(commits[1] >= commits[0] &&
             commits[1] - commits[0] < NSESSLOG);
    TF_EQ(g_peer.step, 8);
    TF_EQ(g_peer.wait, 0);
    TF_CHECK(err_is(""));
    TF_EQ(msg(MSG_R_SRV)[0], '?');

    reset(s_readonly, 0);
    put_cfg(tfs_dir(), "READONLY=PEER", NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_peer.step, 4);
    TF_EQ(g_keys.given, 2);
    asked_run('n');
    asked_run(KEY_ESC);
#ifdef COMPAT_HOST
    /* IDLE=1 closes the session a minute of ticks (6 * TMR_10SEC, one
       a pass on the host) after its last request, well before the key
       source would type F3 and Y. */
    reset(s_idle, 0);
    g_keys.limit = 3 * 6 * TMR_10SEC;
    put_cfg(tfs_dir(), "IDLE=1", NULL);
    t0 = tmr_ticks();
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    t1 = tmr_since(t0);
    TF_CHECK(t1 >= 6 * TMR_10SEC && t1 < 2 * 6 * TMR_10SEC);
    TF_EQ(g_peer.step, 1);
    TF_EQ(g_peer.ended, 1);
    TF_EQ(g_keys.given, 2);
#endif
}

int main(int argc, char **argv)
{
    int a;

    cmem_init();
    tf_begin("T_INVSRV");
    tf_progdir(g_mdir, (u16)sizeof g_mdir, argv[0]);
    a = 1;
    if (argc > 2 && strcmp(argv[1], "-m") == 0 &&
        strlen(argv[2]) < PATHMAX) {
        strcpy(g_mdir, argv[2]);
        a = 3;
    }
    if (a + 1 != argc) {
        printf("usage: T_INVSRV [-m msgdir] dir\n");
        return 2;
    }
    tfs_set_dir(argv[a]);
    te_remove(tfs_dir(), g_extra);
    tfs_dirs(k_mbdirs, 0);
    if (!TF_EQ(tfs_dirs(k_mbdirs, 1), 0))
        return tf_done();
    test_refusals();
    test_far();
    test_session();
    test_serial();
    test_mbox();
    test_mbtkt();
    test_throttle();
    test_tktrans();
#ifdef COMPAT_HOST
    test_stuck();
    test_burst();
    test_starve();
    test_midnight();
#endif
    test_f3();
    test_newlog();
    test_racelog();
    check_log();
    if (tf_fails == 0) {
        te_remove(tfs_dir(), g_extra);
        tfs_dirs(k_mbdirs, 0);
    }
    return tf_done();
}

/*
 * T_SRVSCR.C - the server's status screen (SRC/SERVER/SRVSCR.C).
 *
 * usage: T_SRVSCR [-u] [-g golddir] [-m msgdir] dir
 *
 * dir is a scratch directory prefix ("OBJ/HOST/SRVT/", "D:\"); golddir
 * holds the goldens NAME.SNP and msgdir the server's message file
 * MSG_SRVFILE, both by default the program's own directory.  CONN is
 * replaced here by a stub of the calls the screen makes (conn_count,
 * conn_info, conn_state, conn_uptime, conn_stopping), whose sessions,
 * LISTENs and uptime each check sets, and the serial transport by one
 * of tserial_info, whose ports each check sets, so a screen shows
 * exactly the facts given.  Keys come from a key source that has each
 * key typed ahead, as the screen reads only keys that are waiting.
 * T_SRVSC2.C (T_SRVSCR.H) has the snapshots and the checks of FULL,
 * slot 5, PORTS, MDA and the long uptime.
 * Checks:
 * - closed, before the first open and after a close, srvscr_poll
 *   returns SRVSCR_NONE without asking CONN, and srvscr_trace and
 *   srvscr_close do nothing;
 * - IDLE: the frame, the settings with no read-only station, an empty
 *   table, no serial port and no mailbox, and the key hints;
 * - PORTS: COM2 alone, on an 8250, its name, baud rate and chip drawn at
 *   open and its error counts redrawn with the uptime, and a mailbox;
 * - TWO: two sessions, one logged in and one not, two LISTENs, an
 *   uptime past a day; a poll less than TMR_SEC ticks after the last
 *   redraw changes nothing, and F5 redraws at once, every cell;
 * - TRACE: F2 turns the trace on, and the events of CONN_EV_OPEN ..
 *   CONN_EV_CLOSE show on the trace line, another event number
 *   changing nothing; F2 again turns it off, and an event then changes
 *   nothing;
 * - CONFIRM: F3 asks before it returns SRVSCR_QUIT; MD_YESNO's second
 *   letter in either case, or Esc, cancels; another letter, an extended
 *   key (one whose low byte is an answer too), F3 or F2 leaves the
 *   question, as do F5 and Ctrl-Break, which rewrite every cell; the
 *   question's count follows the sessions open, a second apart;
 *   MD_YESNO's first letter in either case confirms; SRVSCR_QUIT then
 *   comes once, the shutdown row shows and F3 does nothing; a shutdown
 *   begun elsewhere drops the question and makes F3 ask nothing, the
 *   letter after it answering nothing, while a letter typed before the
 *   screen has dropped the question still answers it; with no session
 *   open F3 asks all the same, a cancel leaving the key
 *   hints and the first letter confirming; F10 neither asks nor quits,
 *   with sessions open or none;
 * - a new srvscr_open starts with the trace off and no question;
 * - one session, in slot 5 with slot 1 free: its row first in the
 *   table and no "No session is open." anywhere;
 * - FULL: every slot open, every session state, the kinds NETB, COM1,
 *   COM2 and MBOX, the longest server name, station and user, a data
 *   directory and a mailbox of ENG_MAXDIR and a READONLY list of
 *   SRVCFG_ROMAX names cut at the right edge, idle times of a day and
 *   past it, both ports on the fastest rate with every count at its
 *   largest; drawn with VID_F_BIOS and VID_F_SNOW, snow avoidance then
 *   off;
 * - MDA: the TWO screen in the monochrome palette, on an adapter that
 *   shows mode 7 (VID_F_MDA on the host);
 * - an uptime text longer than its field, from a message file written
 *   in dir, is cut to the field at the right edge, the title beside it
 *   untouched.
 * Every snapshot also checks that the adapter shows the shadow screen
 * (vid_verify).  A colour snapshot is compared only on a colour screen
 * and the MDA one only on a monochrome one, so a DOS run checks the ones
 * its adapter shows; the last check is that the run compared exactly
 * those (every one on the host, where VID_F_MDA gives the monochrome
 * screen), or none with -u.
 *
 * Snapshot format, every line ending CR LF: "=SNAP name", then each row
 * as "|" + VID_COLS characters + "|" (00h, 0Ah, 0Dh and 1Ah written as
 * '?'), "=ATTR" with " L=hh" for each attribute letter, A.. in the order
 * of first use, and each row of letters likewise.  A snapshot that
 * agrees with its golden (or, with -u, has become it) is removed; one
 * that differs is kept in dir.  Prints each failure and a summary line;
 * exit status 0 when every check passes, 1 when one fails, 2 for a
 * usage error.
 */
/* tf_cmpfile, for snap of T_SRVSC2.C. */
#define TF_CMPFILE
#define COMPAT_UART
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TSERIAL.H"
#include "SRVCFG.H"
#include "MSG.H"
#include "SRVSCR.H"
#include "TESTFW.H"
#include "T_SRVSCR.H"

#define MAXKEYS         8

char g_dir[PATHMAX];
char g_gdir[PATHMAX];
char g_mdir[PATHMAX];
int g_update;

/* ---------------------------------------------------------------------
 * The stub of CONN.
 * --------------------------------------------------------------------- */
proto_peer g_peers[CONN_MAX];
u16 g_npeers;
static u8 g_states[CONN_MAX];
u32 g_uptime;
static u32 g_now;               /* the now conn_uptime was last given    */
static int g_stopping;
static u8 g_nports;             /* ports the serial transport serves    */

u16 conn_count(void)
{
    return g_npeers;
}

int conn_info(u16 i, proto_peer *p)
{
    if (i >= g_npeers)
        return 1;
    *p = g_peers[i];
    return 0;
}

u8 conn_state(u8 c)
{
    return c < CONN_MAX ? g_states[c] : (u8)CONN_FREE;
}

u32 conn_uptime(u32 now)
{
    g_now = now;
    return g_uptime;
}

int conn_stopping(void)
{
    return g_stopping;
}

void set_peer(u16 i, u8 slot, const char *sta, const char *user,
              u8 level, u8 state, u32 idle)
{
    proto_peer *p;

    p = &g_peers[i];
    memset(p, 0, sizeof *p);
    p->slot = slot;
    p->level = level;
    p->state = state;
    p->idle = idle;
    strcpy(p->kind, "NETB");
    strcpy(p->station, sta);
    strcpy(p->user, user);
    g_states[slot - 1] = CONN_OPEN;
}

void clear_conn(void)
{
    memset(g_states, CONN_FREE, sizeof g_states);
    g_npeers = 0;
    g_uptime = 0;
    g_stopping = 0;
    g_nports = 0;
}

/* ---------------------------------------------------------------------
 * The stub of the serial transport.
 * --------------------------------------------------------------------- */
static u8 g_ports[UART_MAXPORT];
uart_info g_uinfo[UART_MAXPORT];

int tserial_info(u8 i, u8 *port, uart_info *ui)
{
    if (i >= g_nports)
        return TSER_EARG;
    *port = g_ports[i];
    *ui = g_uinfo[i];
    return TSER_OK;
}

void set_port(u8 i, u8 port, u8 chip, u16 n)
{
    memset(&g_uinfo[i], 0, sizeof g_uinfo[i]);
    g_ports[i] = port;
    g_uinfo[i].chip = chip;
    g_uinfo[i].overrun = n;
    g_uinfo[i].framing = n;
    g_uinfo[i].parity = n;
    g_uinfo[i].lost = n;
    g_nports = (u8)(i + 1);
}

/* ---------------------------------------------------------------------
 * Keys, every one typed ahead.
 * --------------------------------------------------------------------- */
static u16 g_keys[MAXKEYS];
static u16 g_nkeys;
static u16 g_kpos;

static u16 key_src(void *arg, int peek)
{
    TF_CHECK(arg == (void *)g_keys);
    if (g_kpos >= g_nkeys)
        return peek ? KEY_NONE : KEY_EOS;
    return peek ? g_keys[g_kpos] : g_keys[g_kpos++];
}

/* Type the keys k (a list ended by KEY_NONE) and poll once at now. */
static int keys_poll(const u16 *k, u32 now)
{
    int rc;

    g_nkeys = 0;
    while (g_nkeys < MAXKEYS && k[g_nkeys] != KEY_NONE) {
        g_keys[g_nkeys] = k[g_nkeys];
        g_nkeys++;
    }
    g_kpos = 0;
    key_set_source(key_src, g_keys);
    rc = srvscr_poll(now);
    TF_EQ(g_kpos, g_nkeys);
    key_set_source(NULL, NULL);
    return rc;
}

/* ---------------------------------------------------------------------
 * The checks.
 * --------------------------------------------------------------------- */
srvcfg g_cfg;

static const u16 k_f2[] = { KEY_F2, KEY_NONE };
static const u16 k_f5[] = { KEY_F5, KEY_NONE };
static const u16 k_f3[] = { KEY_F3, KEY_NONE };

/* The bottom row's texts as drawn, the margin cell first. */
static const char k_hint[] = " F2=Trace  F3=Shut down  F5=Refresh";
static const char k_ask[] =
    " Sessions open: 2.  Shut the server down?  Y=Yes N=No";
static const char k_ask1[] =
    " Sessions open: 1.  Shut the server down?  Y=Yes N=No";
static const char k_ask0[] =
    " Sessions open: 0.  Shut the server down?  Y=Yes N=No";
static const char k_stop[] =
    " Shutting down: waiting for the sessions to end";

/* 1 when the bottom row reads s. */
static int bar_is(const char *s)
{
    char t[VID_COLS + 1];

    return strcmp(row_text(VID_ROWS - 1, t), s) == 0;
}

/* Calls on a closed screen: nothing asks CONN or draws. */
static void closed_calls(void)
{
    g_now = 0;
    TF_EQ(srvscr_poll(1), SRVSCR_NONE);
    TF_EQ(g_now, 0);
    srvscr_trace(0, CONN_EV_OPEN, "LAB1");
    srvscr_close();
}

void two_sessions(void)
{
    clear_conn();
    set_peer(0, 1, "LAB1", "ADMIN", ENG_LVL_ADMIN, PROTO_S_READY, 75);
    set_peer(1, 2, "BENCH-PC", "", 0, PROTO_S_HELLO, 3);
    g_npeers = 2;
    g_states[4] = CONN_LISTEN;
    g_states[5] = CONN_LISTEN;
    g_uptime = 90061UL;
}

static void test_idle(void)
{
    clear_conn();
    g_uptime = 5;
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(srvscr_poll(100), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    snap("IDLE", 0);
}

static void test_two(void)
{
    char t[VID_COLS + 1];

    two_sessions();
    g_uptime = 7;
    TF_EQ(srvscr_poll(100 + TMR_SEC - 1), SRVSCR_NONE);
    TF_CHECK(strstr(row_text(0, t), "0:00:05") != NULL);
    TF_EQ(vid_flushed(), 0);
    g_uptime = 90061UL;
    TF_EQ(srvscr_poll(100 + TMR_SEC), SRVSCR_NONE);
    TF_EQ(g_now, 100 + TMR_SEC);
    snap("TWO", 0);
    TF_EQ(keys_poll(k_f5, 100 + TMR_SEC + 1), SRVSCR_NONE);
    TF_EQ(vid_flushed(), VID_CELLS);
    snap("TWO", 0);
}

static void test_trace(void)
{
    char t[VID_COLS + 1];
    char was[VID_COLS + 1];

    srvscr_trace(0, CONN_EV_OPEN, "LAB1");
    TF_STREQ(row_text(VID_ROWS - 3, t), " Trace off");
    TF_EQ(keys_poll(k_f2, 200), SRVSCR_NONE);
    TF_STREQ(row_text(VID_ROWS - 3, t), " Trace on");
    srvscr_trace(9, CONN_EV_OPEN, "BENCH-PC");
    TF_STREQ(row_text(VID_ROWS - 3, t), " Slot 10 opened from BENCH-PC");
    srvscr_trace(0, CONN_EV_LOGIN, "ADMIN");
    TF_STREQ(row_text(VID_ROWS - 3, t), " Slot 1 logged in as ADMIN");
    srvscr_trace(0, CONN_EV_CLOSE, "");
    TF_STREQ(row_text(VID_ROWS - 3, was), " Slot 1 closed");
    srvscr_trace(0, (u8)(CONN_EV_CLOSE + 1), "X");
    srvscr_trace(0, (u8)(CONN_EV_OPEN - 1), "X");
    TF_STREQ(row_text(VID_ROWS - 3, t), was);
    srvscr_trace(1, CONN_EV_REQ, "ITEM LIST");
    TF_EQ(srvscr_poll(201), SRVSCR_NONE);
    snap("TRACE", 0);
    TF_EQ(keys_poll(k_f2, 202), SRVSCR_NONE);
    srvscr_trace(1, CONN_EV_REQ, "LOGIN");
    TF_STREQ(row_text(VID_ROWS - 3, t), " Trace off");
}

static void test_confirm(void)
{
    static const u16 k_no[] = { KEY_F3, 'N', KEY_NONE };
    static const u16 k_low[] = { KEY_F3, 'n', KEY_NONE };
    static const u16 k_esc[] = { KEY_F3, KEY_ESC, KEY_NONE };
    static const u16 k_not[] = { KEY_F3, KEY_F1, KEY_EXT + 'y',
                                 KEY_EXT + 'n', KEY_EXT + KEY_ESC, 'a',
                                 KEY_F3, KEY_F2, KEY_NONE };
    static const u16 k_brk[] = { KEY_BREAK, KEY_NONE };
    static const u16 k_old[] = { KEY_F10, 'y', KEY_NONE };
    static const u16 k_yes[] = { 'y', KEY_NONE };
    static const u16 k_f3y[] = { KEY_F3, 'y', KEY_NONE };
    static const u16 k_big[] = { 'Y', KEY_NONE };
    char t[VID_COLS + 1];

    TF_EQ(keys_poll(k_no, 300), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    TF_EQ(keys_poll(k_low, 300), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    TF_EQ(keys_poll(k_esc, 300), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    /* No answer: an extended key, also one whose low byte is an
       answer's (Alt-2 has MD_YESNO's y), another letter, F3 again, and
       F2, which leaves the trace off. */
    TF_EQ(keys_poll(k_not, 301), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    TF_STREQ(row_text(VID_ROWS - 3, t), " Trace off");
    /* Ctrl-Break and F5 rewrite the whole screen, the question
       included. */
    TF_EQ(keys_poll(k_brk, 302), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    TF_EQ(vid_flushed(), VID_CELLS);
    TF_EQ(keys_poll(k_f5, 302), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    TF_EQ(vid_flushed(), VID_CELLS);
    /* The question's count follows the sessions, redrawn with the
       rest once TMR_SEC has passed. */
    g_npeers = 1;
    TF_EQ(srvscr_poll(302 + TMR_SEC), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask1));
    g_npeers = 2;
    TF_EQ(srvscr_poll(302 + 2 * TMR_SEC), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    TF_EQ(keys_poll(k_no + 1, 302), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    /* F10 asks nothing, so the letter after it confirms nothing. */
    TF_EQ(keys_poll(k_old, 302), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    TF_EQ(keys_poll(k_f3, 303), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    snap("CONFIRM", 0);
    TF_EQ(keys_poll(k_yes, 304), SRVSCR_QUIT);
    TF_CHECK(bar_is(k_stop));
    TF_EQ(srvscr_poll(305), SRVSCR_NONE);
    TF_EQ(keys_poll(k_f3, 306), SRVSCR_NONE);
    TF_CHECK(bar_is(k_stop));
    srvscr_close();

    /* A shutdown begun elsewhere drops the question; F3 then asks
       nothing, so the y after it answers nothing, even with no session
       open. */
    two_sessions();
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(keys_poll(k_f3, 400), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    g_stopping = 1;
    TF_EQ(srvscr_poll(401), SRVSCR_NONE);
    TF_CHECK(bar_is(k_stop));
    g_npeers = 0;
    TF_EQ(keys_poll(k_f3y, 402), SRVSCR_NONE);
    TF_CHECK(bar_is(k_stop));
    TF_EQ(keys_poll(k_yes, 403), SRVSCR_NONE);
    srvscr_close();

    /* A y typed while the question still shows, the shutdown begun
       elsewhere since the screen's last poll, answers it: the question
       on the screen is the one the operator read, and SRVSCR_QUIT then
       asks for a shutdown already under way, which changes nothing
       (conn_shutdown). */
    two_sessions();
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(keys_poll(k_f3, 450), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    g_stopping = 1;
    TF_EQ(keys_poll(k_yes, 451), SRVSCR_QUIT);
    TF_CHECK(bar_is(k_stop));
    TF_EQ(keys_poll(k_f3y, 452), SRVSCR_NONE);
    TF_CHECK(bar_is(k_stop));
    srvscr_close();

    /* With no session open F3 asks all the same, and F10 does
       nothing. */
    clear_conn();
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(keys_poll(k_old, 500), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    TF_EQ(keys_poll(k_f3, 500), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask0));
    TF_EQ(keys_poll(k_esc + 1, 501), SRVSCR_NONE);
    TF_CHECK(bar_is(k_hint));
    TF_EQ(keys_poll(k_f3, 502), SRVSCR_NONE);
    TF_EQ(keys_poll(k_big, 503), SRVSCR_QUIT);
    TF_CHECK(bar_is(k_stop));
    srvscr_close();
}

/* srvscr_open starts again: the trace off and no question, whatever
   the last screen had; once closed, the screen ignores every call. */
static void test_reopen(void)
{
    static const u16 k_on[] = { KEY_F2, KEY_F3, KEY_NONE };
    char t[VID_COLS + 1];

    two_sessions();
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(keys_poll(k_on, 600), SRVSCR_NONE);
    TF_CHECK(bar_is(k_ask));
    srvscr_close();
    closed_calls();
    if (!TF_EQ(srvscr_open(0, &g_cfg), 0))
        return;
    TF_EQ(srvscr_poll(601), SRVSCR_NONE);
    TF_STREQ(row_text(VID_ROWS - 3, t), " Trace off");
    srvscr_trace(0, CONN_EV_OPEN, "LAB1");
    TF_STREQ(row_text(VID_ROWS - 3, t), " Trace off");
    TF_CHECK(bar_is(k_hint));
    srvscr_close();
}

/* Copy the directory argument a into out, or the program's directory
   when a is NULL; 0 when it does not fit. */
static int set_dir(char *out, const char *a, const char *prog)
{
    if (a == NULL)
        a = prog;
    if (strlen(a) >= PATHMAX)
        return 0;
    strcpy(out, a);
    return 1;
}

int main(int argc, char **argv)
{
    char prog[PATHMAX];
    const char *g;
    const char *m;
    int a;

    g = NULL;
    m = NULL;
    tf_begin("T_SRVSCR");
    tf_progdir(prog, (u16)sizeof prog, argv[0]);
    for (a = 1; a < argc && argv[a][0] == '-'; a++) {
        if (strcmp(argv[a], "-u") == 0)
            g_update = 1;
        else if (strcmp(argv[a], "-g") == 0 && a + 1 < argc)
            g = argv[++a];
        else if (strcmp(argv[a], "-m") == 0 && a + 1 < argc)
            m = argv[++a];
        else
            break;
    }
    if (a + 1 != argc || !set_dir(g_dir, argv[a], prog) ||
        !set_dir(g_gdir, g, prog) || !set_dir(g_mdir, m, prog)) {
        printf("usage: T_SRVSCR [-u] [-g golddir] [-m msgdir] dir\n");
        return 2;
    }
    if (!TF_EQ(msg_load(g_mdir, MSG_SRVFILE), 0)) {
        printf("  %s\n", msg_why());
        return tf_done();
    }
    srvcfg_default(&g_cfg);
    strcpy(g_cfg.name, "RISRV");
    strcpy(g_cfg.data, "D:\\INV\\");
    closed_calls();
    test_idle();
    test_two();
    test_trace();
    test_confirm();
    test_reopen();
    test_sc2();
    msg_free();
    return tf_done();
}

/*
 * T_INVTCP.C - INVTCP's run (invtcp_main, INVTCP.H): its command line,
 * start, loop and stop over the simulated TCP/IP stack (TSFAKE.C) and
 * NetBIOS provider (FAKENB.C).
 *
 * usage: T_INVTCP [dir] [BREAK]
 *
 * dir is a scratch directory prefix ("OBJ/HOST/INVTT/", "D:\") for the
 * files that catch the run's standard output and standard error.
 * BREAK, the last argument, says that BREAK is on when the program
 * starts, as the MS-DOS 3.10 runs set it (T_INVTCP_DOSPRE of SRCS.MK),
 * so that a stop that does not put it back shows.
 *
 * A key source plays the operator: each loop pass asks it for a key,
 * which is when the test acts as a client (tsf_connect) or a server (a
 * set of its own and the name RISRV) and finally types F3, which the
 * source, when that asks, follows with the answer Y, typed ahead with
 * it, unless the scene chose another key; a key it gives once the loop
 * is over is one typed ahead during the stop's wait.  Standard output
 * is caught in a scratch file for each run, its handle pointed there
 * with dos_dup2 and back after, whatever the log is; each line must end
 * as a text file's line does, and the lines are checked without that
 * end and an event line's time stamp.  Standard error is caught in a
 * scratch file too and checked, its carriage returns removed.  Checks:
 * - the usage: /? and -? end at once with TCP_EXIT_OK, the usage on
 *   standard output and nothing on standard error; no server, a bad
 *   server name, a second server, an unknown switch, a /NAME: name
 *   refused, a repeated /NAME: (its switch matched in either case), an
 *   /ALLOW: network that is not one (an octet missing, over 255, of
 *   more than three digits or with a leading zero, bits 0, over 32,
 *   missing or with a leading zero, a character after it) and one
 *   /ALLOW: more than BR_ALLOWMAX end with TCP_EXIT_USAGE and the usage
 *   on standard error, after "bad or repeated argument" for an argument
 *   refused; none of them writes an event line or starts the stack or
 *   the bridge;
 * - /ALLOW: networks that do not hold the client's address: its REFUSE
 *   line alone, its connection reset and the server never called; one
 *   that holds it by its first 31 bits, its switch in lower case and
 *   after another: the client let in, its OPEN, CALL and CLOSE lines;
 * - a run stopped by F3 and Y: TCP_EXIT_OK, the START line naming the
 *   server (given in lower case) in upper case and BR_PORT, the STOP
 *   line the status, and on standard error the question alone, asked
 *   with no session open too; a key other than F3 is read and the loop
 *   goes on, in the same pass when F3 is typed ahead after it; BREAK off
 *   during the loop; the bridge's far block is zeroed and freed inside
 *   the first br_stop, before ts_term and before the STOP line, the
 *   stack stopped once, no name, set or NCB left;
 * - the question: F10 neither asks nor stops, nor does a y with no
 *   question asked; N, n or Esc after F3 answers no, and a Y after that
 *   is not an answer; F3 then asks again; another key leaves the
 *   question waiting, and F3 asks it once more; an answer typed a pass
 *   later counts, y as Y; the question counts the sessions open, one or
 *   two, and after a no the session is still open and relays a client's
 *   bytes to the server, no CLOSE line written;
 * - F3 and Y with a slot relaying and one calling: their OPEN lines,
 *   the CALL line of the first, a CLOSE STOP line for each, in the
 *   order they came, both connections aborted, the server's session
 *   hung up, the CALL cancelled;
 * - a stop whose RECEIVE the provider gives up only in the wait: the
 *   waiting message with the count, after the question, a wait made
 *   after ts_term with the bridge stopped, which ends by itself without
 *   reading a key, and the block zeroed and freed after that wait,
 *   before the STOP line and before invtcp_main returns;
 * - a stop whose CALL the provider never gives up: the wait ended by a
 *   key typed ahead, read once; the block still allocated when
 *   invtcp_main returns, and kept by br_stop until nba_stuck reports no
 *   NCB owned, then zeroed and freed;
 * - each start failure the fakes can make (no packet driver, no
 *   settings, the stack refusing; the local name taken, a full name
 *   table; no far memory; a slot that cannot listen): its status and
 *   message, no loop pass and no event line, nothing left behind, and a
 *   run after it that succeeds;
 * - BREAK as it was before each run, once the run is over; under DOS,
 *   INT 24h pointed elsewhere during the loop and back at the handler
 *   found before the run once it is over;
 * - at exit, the emergency stops of the runs: none waits, reads a key or
 *   writes anything; the newest keeps a block whose CALL the provider
 *   still owns; once it no longer does, an earlier one frees it, zeroed.
 *   A failure there ends the program with abort, as the summary line is
 *   printed by then.
 *
 * Every run that starts the stack registers INVTCP's emergency stop with
 * atexit, and main registers two checks of its own: 21 functions of the
 * 32 that C89 promises a program.  Under a runtime that gives only those
 * 32 (Turbo C, Watcom), a registration past them fails: a run then fails
 * to start with TCP_EXIT_STACK, "the emergency stop could not be set
 * up", or main's own check fails.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#define COMPAT_FIND
#define COMPAT_VECT
#define COMPAT_DUP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"
#include "INVTCP.H"
#include "TESTFW.H"
#include "FAKENB.H"
#include "TSFAKE.H"
#include "T_FILES.H"

#ifdef COMPAT_HOST
#define NULDEV          "/dev/null"
#else
#define NULDEV          "NUL"
#endif

#define ERRSZ           256     /* bytes of standard error kept         */
#define OUTSZ           512     /* bytes of standard output kept        */
#define MAXARGS         (BR_ALLOWMAX + 2) /* arguments a run is given  */
#define SPINS           64      /* loop passes a scene waits at most    */
#define DRAINKEY        'q'     /* the key typed ahead during the wait  */
#ifdef COMPAT_HOST
#define NLW             1       /* bytes of a newline in a text file    */
#else
#define NLW             2
#endif

/* The event lines as read_out leaves them. */
#define EV_START        "0 START RISRV 7000\n"
#define EV_STOP         "0 STOP 0\n"
#define EV_OPEN1        "1 OPEN 10.0.2.2:1024\n"
#define EV_OPEN2        "2 OPEN 10.0.2.2:1025\n"
#define EV_CALL1        "1 CALL\n"
#define EV_CALL2        "2 CALL\n"
#define EV_CLOSE1       "1 CLOSE STOP\n"
#define EV_CLOSE2       "2 CLOSE STOP\n"

/* Where the key source is: in the loop, in the stop after F3 and Y, or
   in the emergency stops after main. */
#define PH_LOOP         0
#define PH_STOP         1
#define PH_EXIT         2

/* What the far allocation hook does to the bridge's block, the second
   far block of a run (the stack's is the first). */
#define FM_NONE         0
#define FM_FAIL         1       /* refuse it                            */
#define FM_LISTEN       2       /* take slot 2 first: it cannot listen  */

static const char k_usage[] = "usage: INVTCP [/NAME:local] [/ALLOW:net]... "
                              "server\n"
                              "  /NAME:  the bridge's NetBIOS name "
                              "(default the machine name)\n"
                              "  /ALLOW: let in only a.b.c.d, or the network "
                              "a.b.c.d/bits (4 at most)\n";

/* Standard error's lines: the question of F3 with n sessions open, and
   the stop's wait. */
#define ER_ASK(n)       "INVTCP: sessions open: " n ".  Stop the bridge?" \
                        "  Y=Yes N=No\n"
#define ER_ASK0         ER_ASK("0")
#define ER_ASK1         ER_ASK("1")
#define ER_ASK2         ER_ASK("2")
#define ER_WAIT         "INVTCP: 1 NCBs still owned by NetBIOS: " \
                        "waiting (a key ends the wait)\n"

/* The scene of a run: called for every key the loop asks for, n
   counting the earlier ones, it acts and returns the key typed ahead,
   KEY_NONE for none. */
typedef u16 (*scene_fn)(u16 n);

static scene_fn g_scene;
static int g_ph;                /* PH_ */
static u16 g_ahead;             /* the loop's key typed ahead           */
static u16 g_ans;               /* the key typed ahead with a scene's
                                   F3, KEY_NONE for none                */
static int g_up;                /* F3's question waits for its answer   */
static u16 g_asks;              /* the loop's asks since the run began  */
static u16 g_polls;             /* the loop's ts_poll calls             */
static u16 g_dkey;              /* the key typed ahead after the loop   */
static u16 g_dpeeks;            /* asks after the loop                  */
static u16 g_dreads;            /* keys read after the loop             */
static u16 g_dbad;              /* asks with the bridge or stack up     */

static cmem_blk *g_blk;         /* the bridge's far block in the loop   */
static u16 g_frees;             /* its frees                            */
static u16 g_fterms;            /* tsf_terms() at its last free         */
static int g_fph;               /* g_ph at its last free                */
static int g_fzero;             /* it was zeroed at its last free       */

static int g_fmode;             /* FM_                                  */
static u16 g_allocs;            /* far allocations since the run began  */

static nba *g_srv;              /* the server's set: one LISTEN         */
static u8 g_lsn0;               /* pair: the server's first session, 0
                                   before it has opened                 */
static int g_brk;               /* BREAK at the start                   */
static char g_errf[DOS_PATHMAX];        /* standard error's file        */
static char g_err[ERRSZ];       /* what the last run said there, its
                                   carriage returns removed             */
static char g_outf[DOS_PATHMAX];        /* standard output's file       */
static char g_out[OUTSZ];       /* what the last run wrote there, as
                                   read_out leaves it                   */
static u16 g_last;              /* where in the file its last line is   */
static int g_cap;               /* handle 1 is on g_outf                */
static int g_save;              /* handle 1's own file meanwhile        */
static u32 g_fout;              /* bytes of g_outf at the block's last
                                   free while caught                    */
static u16 g_terms;             /* tsf_terms() when main returns        */
#ifndef COMPAT_HOST
static u16 g_v24s;              /* INT 24h's vector before the run      */
static u16 g_v24o;
#endif

/* The key source. */
static u16 keys(void *arg, int peek)
{
    u16 k;
#ifndef COMPAT_HOST
    u16 seg;
    u16 off;
#endif

    if (arg != (void *)&g_ph)
        abort();
    if (g_ph == PH_LOOP) {
        TF_EQ(dos_break_get(), 0);
#ifndef COMPAT_HOST
        dos_getvect(0x24, &seg, &off);
        TF_CHECK(seg != g_v24s || off != g_v24o);
#endif
        if (br_buffers() != NULL)
            g_blk = br_buffers();
        if (peek) {
            if (g_ahead == KEY_NONE)
                g_ahead = (*g_scene)(g_asks++);
            return g_ahead;
        }
        k = g_ahead;
        g_ahead = KEY_NONE;
        if (k == KEY_F3) {
            g_up = 1;
            g_ahead = g_ans;
        } else if (g_up) {
            if (k == 'Y' || k == 'y')
                g_ph = PH_STOP;
            if (k == 'N' || k == 'n' || k == KEY_ESC)
                g_up = 0;
        }
        return k;
    }
    if (!peek) {
        g_dreads++;
        return g_dkey;
    }
    g_dpeeks++;
    if (tsf_terms() == 0 || br_buffers() != NULL)
        g_dbad++;
    return g_dkey;
}

/* tsf_pump: count the loop's passes into *arg. */
static void count(void *arg)
{
    (*(u16 *)arg)++;
}

/* cmem_freeing: note the frees of the bridge's block. */
static void freeing(cmem_blk *b)
{
    u8 c[16];
    u16 off;
    u16 n;
    u16 i;

    if (b != g_blk)
        return;
    g_frees++;
    if (g_cap)
        TF_EQ(cf_size(1, &g_fout), 0);
    g_fterms = tsf_terms();
    g_fph = g_ph;
    g_fzero = 1;
    for (off = 0; off < cmem_size(b); off = (u16)(off + n)) {
        n = (u16)(cmem_size(b) - off);
        if (n > sizeof c)
            n = (u16)sizeof c;
        cmem_get(b, off, c, n);
        for (i = 0; i < n; i++)
            if (c[i] != 0)
                g_fzero = 0;
    }
}

/* cmem_fault: g_fmode's work on the bridge's block. */
static int far_hook(u16 size)
{
    if (++g_allocs != 2 || g_fmode == FM_NONE)
        return 0;
    TF_EQ(size, 2 * TS_SLOTS * NB_MSGMAX);
    if (g_fmode == FM_FAIL)
        return 1;
    TF_EQ(ts_listen(2, 9), TS_OK);
    return 0;
}

/* Point handle 1 at g_outf, emptied; 1 when that worked.  cap_off puts
   it back in either case. */
static int cap_on(void)
{
    int h;
    int ok;

    fflush(stdout);
    if (dos_dup(1, &g_save) != 0)
        return 0;
    g_cap = 1;
    ok = cf_create(g_outf, &h) == 0;
    if (ok) {
        ok = dos_dup2(h, 1) == 0;
        cf_close(h);
    }
    return ok;
}

/* Put handle 1 back on its own file, if cap_on moved it; 1 when that
   worked. */
static int cap_off(void)
{
    int ok;

    if (!g_cap)
        return 1;
    g_cap = 0;
    fflush(stdout);
    ok = dos_dup2(g_save, 1) == 0;
    return cf_close(g_save) == 0 && ok;
}

/* Read g_outf into g_out: each line must end in a text file's newline,
   which becomes a bare one, and a time stamp and the blank after it
   that start a line are removed; g_last is where its last line is. */
static void read_out(void)
{
    static const char shape[] = "0000-00-00 00:00:00 ";
    char c;
    u16 got;
    u16 i;
    u16 j;
    u16 k;
    u16 m;
    u16 s;

    if (!TF_EQ(tfs_read_all(g_outf, g_out, OUTSZ - 1, &got), 0))
        got = 0;
    g_last = 0;
    for (i = 0, k = 0; i < got; i = (u16)(j + 1)) {
        g_last = i;
        for (j = i; j < got && g_out[j] != '\n'; j++)
            ;
        m = (u16)(j + 1 - NLW);
        if (!TF_CHECK(j < got && j >= i + NLW - 1 &&
                      (NLW == 1 || g_out[m] == '\r')))
            m = j;
        for (s = 0; shape[s] != '\0' && i + s < m; s++) {
            c = g_out[i + s];
            if (shape[s] == '0' ? !TXT_ISDIGIT(c) : c != shape[s])
                break;
        }
        if (shape[s] == '\0')
            i = (u16)(i + s);
        while (i < m)
            g_out[k++] = g_out[i++];
        g_out[k++] = '\n';
    }
    g_out[k] = '\0';
}

/* Fresh fakes, no fault. */
static void fresh(void)
{
    fnb_reset();
    tsf_reset();
    g_fmode = FM_NONE;
}

/* Run INVTCP with the n arguments args after the program's name, scene
   playing the operator and dkey typed ahead once the loop is over;
   its status, with standard error in g_err. */
static int inv(int n, char **args, scene_fn scene, u16 dkey)
{
    char *av[MAXARGS + 2];
#ifndef COMPAT_HOST
    u16 seg;
    u16 off;
#endif
    u16 got;
    u16 j;
    int st;
    int i;

    av[0] = "INVTCP";
    for (i = 0; i < n; i++)
        av[i + 1] = args[i];
    av[n + 1] = NULL;
    g_scene = scene;
    g_ph = PH_LOOP;
    g_ahead = KEY_NONE;
    g_ans = 'Y';
    g_up = 0;
    g_asks = 0;
    g_polls = 0;
    tsf_pump(count, &g_polls);
    g_dkey = dkey;
    g_dpeeks = 0;
    g_dreads = 0;
    g_dbad = 0;
    g_blk = NULL;
    g_frees = 0;
    g_allocs = 0;
    TF_CHECK(freopen(g_errf, "w", stderr) != NULL);
#ifndef COMPAT_HOST
    dos_getvect(0x24, &g_v24s, &g_v24o);
#endif
    TF_CHECK(cap_on());
    st = invtcp_main(n + 1, av);
    TF_CHECK(cap_off());
    read_out();
#ifndef COMPAT_HOST
    dos_getvect(0x24, &seg, &off);
    TF_EQ(seg, g_v24s);
    TF_EQ(off, g_v24o);
#endif
    /* Closed, so that DOS writes the file's size. */
    TF_CHECK(freopen(NULDEV, "w", stderr) != NULL);
    if (!TF_EQ(tfs_read_all(g_errf, g_err, ERRSZ - 1, &got), 0))
        got = 0;
    for (i = 0, j = 0; j < got; j++)
        if (g_err[j] != '\r')
            g_err[i++] = g_err[j];
    g_err[i] = '\0';
    TF_EQ(dos_break_get(), g_brk);
    return st;
}

/* The block was last freed with every line of the last run out but
   the STOP line, which comes once the stop is over. */
static void freed_before_stop(void)
{
    TF_EQ(g_fout, g_last);
}

/* F3 at once. */
static u16 quit(u16 n)
{
    return n == 0 ? KEY_F3 : KEY_NONE;
}

/* A key other than F3 and F3 typed ahead together. */
static u16 two(u16 n)
{
    return n == 0 ? 'a' : KEY_F3;
}

/* A key other than F3, then F3 a pass later. */
static u16 other(u16 n)
{
    if (n == 0)
        return 'a';
    return n >= 2 ? KEY_F3 : KEY_NONE;
}

/* F10 and a y; F3 answered N, then a Y; F3 answered Esc; F3 followed
   by a letter that is no answer, a second F3, which asks again, and,
   a pass later, y.  Past that, which a loop still running reaches, F3
   and Y. */
static u16 asks(u16 n)
{
    static const u16 k[] = { KEY_F10, 'y', KEY_F3, 'Y', KEY_F3, KEY_F3,
                             KEY_F3, KEY_NONE, 'y' };
    static const u16 ans[] = { 0, 0, 'n', 0, KEY_ESC, 'a', KEY_NONE };

    if (n < sizeof ans / sizeof ans[0])
        g_ans = ans[n];
    if (n < sizeof k / sizeof k[0])
        return k[n];
    g_ans = 'Y';
    return KEY_F3;
}

/* The server's set listening as RISRV and a client taken by slot k. */
static void serve(u8 k)
{
    u8 num;
    u8 s;

    TF_EQ(nba_open(&g_srv, 1), 0);
    TF_EQ(nb_add_name("RISRV", &num), NB_OK);
    TF_EQ(nba_listen(g_srv, 0, "RISRV", "*", 0, 0), NB_OK);
    s = TS_SLOTS;
    TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
    TF_EQ(s, k);
}

static u16 g_at;                /* the ask a scene's last step came at  */

/* A session on slot 0, then, two passes after it opened, the step
   (relay or stuck), then F3 two passes later. */
static u16 session(u16 n, void (*step)(void))
{
    if (n == 0) {
        serve(0);
        g_at = 0;
        return KEY_NONE;
    }
    if (g_at == 0) {
        if (br_sessions() != 0 || n >= SPINS)
            g_at = n;
        return KEY_NONE;
    }
    if (n == g_at + 2)
        (*step)();
    return n >= g_at + 4 ? KEY_F3 : KEY_NONE;
}

/* Slot 1 calling: its CALL held, so that it is in progress at F3. */
static void call1(void)
{
    u8 s;

    fnb_call_hold(1);
    s = TS_SLOTS;
    TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
    TF_EQ(s, 1);
}

/* Slot 0's RECEIVE given up by the provider only on the cancels the
   wait makes. */
static void stuck(void)
{
    fnb_stall(5);
    fnb_hold(1);
}

static u16 relay(u16 n)
{
    return session(n, call1);
}

static u16 drained(u16 n)
{
    return session(n, stuck);
}

/* A session on slot 0; two passes after it opened, F3 answered N; a
   pass later the session still open and the client sending; three
   passes later the server receiving that, then F3 and Y. */
static u16 keeps(u16 n)
{
    char b[4];
    u16 len;

    if (n == 0 || g_at == 0)
        return session(n, NULL);
    if (n == g_at + 2) {
        g_ans = 'N';
        return KEY_F3;
    }
    if (n == g_at + 3) {
        TF_EQ(br_sessions(), 1);
        TF_EQ(tsf_send(0, "PING", 4), 4);
    }
    if (n < g_at + 6)
        return KEY_NONE;
    if (n == g_at + 6) {
        TF_EQ(br_sessions(), 1);
        len = (u16)sizeof b;
        TF_EQ(nb_recv(nba_lsn(g_srv, 0), b, &len), NB_OK);
        TF_CHECK(len == sizeof b && memcmp(b, "PING", sizeof b) == 0);
    }
    g_ans = 'Y';
    return KEY_F3;
}

/* A session on slot 0, then the server listening again and a client
   taken by slot 1, then F3 once both sessions are open or after SPINS
   asks. */
static u16 pair(u16 n)
{
    u8 s;

    if (n == 0) {
        serve(0);
        g_lsn0 = 0;
        return KEY_NONE;
    }
    if (g_lsn0 == 0 && br_sessions() == 1) {
        TF_EQ(nba_poll(g_srv, 0), NB_OK);
        g_lsn0 = nba_lsn(g_srv, 0);
        TF_EQ(nba_listen(g_srv, 0, "RISRV", "*", 0, 0), NB_OK);
        s = TS_SLOTS;
        TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
        TF_EQ(s, 1);
    }
    return br_sessions() == 2 || n >= SPINS ? KEY_F3 : KEY_NONE;
}

/* A client taken by slot 0, then F3 once its session has opened or
   after SPINS asks. */
static u16 client(u16 n)
{
    if (n == 0) {
        serve(0);
        return KEY_NONE;
    }
    return br_sessions() != 0 || n >= SPINS ? KEY_F3 : KEY_NONE;
}

/* A CALL the provider never gives up: held, its cancels stalled. */
static u16 held(u16 n)
{
    u8 s;

    if (n == 0) {
        fnb_call_hold(1);
        s = TS_SLOTS;
        TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
        TF_EQ(s, 0);
        return KEY_NONE;
    }
    if (n == 2)
        fnb_stall(4);
    return n >= 3 ? KEY_F3 : KEY_NONE;
}

static char *a_srv[] = { "risrv" };     /* upper-cased by INVTCP */

/* A run stopped by F3 and Y after a key it ignores, from the provider
   as it is and a fresh stack: status 0, the question alone on standard
   error, the block freed in the first br_stop, the stack stopped
   once, the bridge's name gone and its NCBs closed. */
static void good_run(void)
{
    u16 terms;
    u16 names;
    u16 sets;

    terms = tsf_terms();
    names = fnb_names();
    sets = fnb_sets();
    TF_EQ(inv(1, a_srv, other, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK0);
    TF_STREQ(g_out, EV_START EV_STOP);
    freed_before_stop();
    TF_EQ(g_asks, 3);
    TF_EQ(g_polls, 2);
    TF_CHECK(g_blk != NULL);
    TF_EQ(g_frees, 1);
    TF_EQ(g_fterms, terms);
    TF_EQ(g_fph, PH_STOP);
    TF_CHECK(g_fzero);
    TF_EQ(g_dpeeks + g_dreads, 0);
    TF_EQ(tsf_terms(), terms + 1);
    TF_CHECK(br_buffers() == NULL);
    TF_EQ(fnb_names(), names);
    TF_EQ(fnb_sets(), sets);
    TF_EQ(fnb_pending(), 0);
}

/* The usage, and arguments refused. */
static void test_usage(void)
{
    static char *a_help[] = { "/?" };
    static char *a_dhelp[] = { "-?" };
    static char *a_bad[] = { "*RISRV" };
    static char *a_two[] = { "RISRV", "OTHER" };
    static char *a_sw[] = { "/X", "RISRV" };
    static char *a_names[] = { "/NAME:A", "/name:b", "RISRV" };
    static char *a_badname[] = { "/NAME:*X", "RISRV" };
    static char *a_long[] = { "RISRV-SERVER-NAM" };
    static char *a_nets[] = { "/ALLOW:10.0.2", "/ALLOW:10.0.2.256",
                              "/ALLOW:10.0.2.2/0", "/ALLOW:10.0.2.2/33",
                              "/ALLOW:10.0.2.2/", "/ALLOW:10.0.2.2x",
                              "/ALLOW:10..2.2", "/ALLOW:1000.0.2.2",
                              "/ALLOW:010.0.2.2", "/ALLOW:10.0.2.02",
                              "/ALLOW:10.0.2.2/024" };
    static char *a_five[] = { "/ALLOW:1.0.0.1", "/ALLOW:1.0.0.2",
                              "/ALLOW:1.0.0.3", "/ALLOW:1.0.0.4",
                              "/ALLOW:1.0.0.5", "RISRV" };
    char *a_net[2];
    char want[80];
    int i;

    fresh();
    TF_EQ(inv(1, a_help, quit, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, "");
    TF_STREQ(g_out, k_usage);
    TF_EQ(inv(1, a_dhelp, quit, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, "");
    TF_STREQ(g_out, k_usage);
    TF_EQ(inv(0, a_srv, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_STREQ(g_err, k_usage);
    TF_STREQ(g_out, "");
    TF_EQ(inv(1, a_bad, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, k_usage));
    TF_EQ(inv(1, a_long, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, k_usage));
    TF_EQ(inv(2, a_two, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, "INVTCP: bad or repeated argument: OTHER\n"
                       "usage: INVTCP "));
    TF_EQ(inv(2, a_sw, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, "INVTCP: bad or repeated argument: /X\n"
                       "usage: INVTCP "));
    TF_EQ(inv(3, a_names, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, "INVTCP: bad or repeated argument: "
                       "/name:b\nusage: INVTCP "));
    TF_STREQ(g_out, "");
    TF_EQ(inv(2, a_badname, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, "INVTCP: bad or repeated argument: "
                       "/NAME:*X\nusage: INVTCP "));
    a_net[1] = "RISRV";
    for (i = 0; i < (int)(sizeof a_nets / sizeof a_nets[0]); i++) {
        a_net[0] = a_nets[i];
        TF_EQ(inv(2, a_net, quit, KEY_NONE), TCP_EXIT_USAGE);
        sprintf(want, "INVTCP: bad or repeated argument: %s\nusage: ",
                a_nets[i]);
        TF_CHECK(tf_starts(g_err, want));
    }
    TF_EQ(inv(BR_ALLOWMAX + 2, a_five, quit, KEY_NONE), TCP_EXIT_USAGE);
    TF_CHECK(tf_starts(g_err, "INVTCP: bad or repeated argument: "
                       "/ALLOW:1.0.0.5\nusage: INVTCP "));
    TF_EQ(g_asks, 0);
    TF_EQ(tsf_terms(), 0);
    TF_EQ(fnb_sets(), 0);
}

/* F3 and Y with nothing to stop, and with a slot relaying and one
   calling. */
static void test_stop(void)
{
    u8 lsn;
    u16 n;

    fresh();
    good_run();
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_sets(), 0);
    fresh();
    TF_EQ(inv(1, a_srv, two, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_out, EV_START EV_STOP);
    TF_EQ(g_asks, 2);
    TF_EQ(g_polls, 1);
    TF_EQ(g_frees, 1);
    fresh();
    TF_EQ(inv(1, a_srv, relay, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK1);
    TF_STREQ(g_out, EV_START EV_OPEN1 EV_CALL1 EV_OPEN2 EV_CLOSE1 EV_CLOSE2
             EV_STOP);
    freed_before_stop();
    TF_EQ(g_frees, 1);
    TF_EQ(g_fterms, 0);
    TF_EQ(tsf_recv(0, g_err, ERRSZ, &n), TSF_GONE);
    TF_EQ(tsf_recv(1, g_err, ERRSZ, &n), TSF_GONE);
    TF_EQ(nba_poll(g_srv, 0), NB_OK);
    lsn = nba_lsn(g_srv, 0);
    TF_EQ(nb_hangup(lsn), NB_RC_SESSCLOSED);
    TF_EQ(nba_close(g_srv), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_badhangups(), 0);
    TF_EQ(fnb_names(), 1);
}

/* The question: what stops and what does not, without a session and
   with one, which a no leaves relaying, and the count of two. */
static void test_ask(void)
{
    fresh();
    TF_EQ(inv(1, a_srv, asks, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK0 ER_ASK0 ER_ASK0 ER_ASK0);
    TF_STREQ(g_out, EV_START EV_STOP);
    TF_EQ(g_asks, 9);
    TF_EQ(g_polls, 2);
    TF_EQ(g_ph, PH_STOP);
    fresh();
    TF_EQ(inv(1, a_srv, keeps, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK1 ER_ASK1);
    TF_STREQ(g_out, EV_START EV_OPEN1 EV_CALL1 EV_CLOSE1 EV_STOP);
    TF_EQ(g_asks, g_at + 7);
    TF_EQ(nba_poll(g_srv, 0), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_srv, 0)), NB_RC_SESSCLOSED);
    TF_EQ(nba_close(g_srv), 0);
    TF_EQ(fnb_pending(), 0);
    fresh();
    TF_EQ(inv(1, a_srv, pair, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK2);
    TF_STREQ(g_out, EV_START EV_OPEN1 EV_CALL1 EV_OPEN2 EV_CALL2 EV_CLOSE1
             EV_CLOSE2 EV_STOP);
    TF_EQ(nba_poll(g_srv, 0), NB_OK);
    TF_EQ(nb_hangup(g_lsn0), NB_RC_SESSCLOSED);
    TF_EQ(nb_hangup(nba_lsn(g_srv, 0)), NB_RC_SESSCLOSED);
    TF_EQ(nba_close(g_srv), 0);
    TF_EQ(fnb_pending(), 0);
}

/* The allow-list: the peers' address 10.0.2.2 refused, reset and never
   called, by /ALLOW: networks that hold other addresses only; let in,
   its session opened, by a switch in lower case that names it among
   others, by its first 31 bits. */
static void test_allow(void)
{
    static char *a_out[] = { "/ALLOW:10.0.3.0/24", "/ALLOW:10.0.2.3",
                             "RISRV" };
    static char *a_in[] = { "/ALLOW:10.0.3.0/24", "/allow:10.0.2.3/31",
                            "RISRV" };
    char buf[ERRSZ];
    u16 n;

    fresh();
    TF_EQ(inv(3, a_out, client, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK0);
    TF_STREQ(g_out, EV_START "1 REFUSE 10.0.2.2:1024\n" EV_STOP);
    TF_EQ(tsf_recv(0, buf, ERRSZ, &n), TSF_GONE);
    TF_EQ(nba_poll(g_srv, 0), NB_RC_PENDING);
    TF_EQ(nba_close(g_srv), 0);
    fresh();
    TF_EQ(inv(3, a_in, client, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK1);
    TF_STREQ(g_out, EV_START EV_OPEN1 EV_CALL1 EV_CLOSE1 EV_STOP);
    TF_EQ(nba_poll(g_srv, 0), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_srv, 0)), NB_RC_SESSCLOSED);
    TF_EQ(nba_close(g_srv), 0);
    TF_EQ(fnb_pending(), 0);
}

/* A stop that waits for the provider: ended by the provider, then by a
   key. */
static void test_wait(void)
{
    fresh();
    TF_EQ(inv(1, a_srv, drained, KEY_NONE), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK1 ER_WAIT);
    TF_STREQ(g_out, EV_START EV_OPEN1 EV_CALL1 EV_CLOSE1 EV_STOP);
    freed_before_stop();
    TF_CHECK(g_dpeeks != 0);
    TF_EQ(g_dbad, 0);
    TF_EQ(g_dreads, 0);
    TF_EQ(g_frees, 1);
    TF_EQ(g_fterms, 1);
    TF_EQ(g_fph, PH_STOP);
    TF_CHECK(g_fzero);
    TF_EQ(nba_stuck(), 0);
    TF_EQ(fnb_names(), 1);
    TF_EQ(nba_close(g_srv), 0);
    fresh();
    TF_EQ(inv(1, a_srv, held, DRAINKEY), TCP_EXIT_OK);
    TF_STREQ(g_err, ER_ASK0 ER_WAIT);
    TF_STREQ(g_out, EV_START EV_OPEN1 EV_CLOSE1 EV_STOP);
    TF_CHECK(g_dpeeks != 0);
    TF_EQ(g_dbad, 0);
    TF_EQ(g_dreads, 1);
    TF_EQ(g_frees, 0);
    TF_CHECK(br_buffers() == NULL);
    TF_CHECK(nba_stuck() != 0);
    TF_EQ(br_stop(), 0);
    TF_EQ(g_frees, 0);
    fnb_reset();
    TF_EQ(br_stop(), 0);
    TF_EQ(g_frees, 1);
    TF_CHECK(g_fzero);
    TF_EQ(br_stop(), 0);
    TF_EQ(g_frees, 1);
}

/* A start that fails with st and the message text, from fakes set up
   for it, then a run that succeeds with the provider as the failure
   left it; started is 1 when the stack was. */
static void fails(int n, char **args, int st, const char *text,
                  int started)
{
    char want[80];

    TF_EQ(inv(n, args, quit, KEY_NONE), st);
    sprintf(want, "INVTCP: %s\n", text);
    TF_STREQ(g_err, want);
    TF_STREQ(g_out, "");
    TF_EQ(g_asks, 0);
    TF_EQ(tsf_terms(), started);
    TF_CHECK(br_buffers() == NULL);
    TF_EQ(fnb_pending(), 0);
    g_fmode = FM_NONE;
    tsf_reset();
    good_run();
}

/* Every start failure the fakes can make. */
static void test_fail(void)
{
    static char *a_name[] = { "/name:BRIDGE", "RISRV" };
    char name[NB_NAMELEN];
    u8 num;
    int i;

    fresh();
    tsf_init_rc(TS_ENOPKT);
    fails(1, a_srv, TCP_EXIT_NOPKT, "no packet driver is loaded", 0);
    fresh();
    tsf_init_rc(TS_ECFG);
    fails(1, a_srv, TCP_EXIT_STACK,
          "WATTCP.CFG is missing or gives no address", 0);
    fresh();
    tsf_init_rc(TS_EINIT);
    fails(1, a_srv, TCP_EXIT_STACK, "the TCP stack did not start", 0);
    fresh();
    fnb_dupname("BRIDGE");
    fails(2, a_name, TCP_EXIT_NAME,
          "the local name could not be added (NetBIOS error 16)", 1);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_sets(), 0);
    fresh();
    TF_EQ(nb_present(), 1);
    for (i = 1; fnb_names() < FNB_NAMES; i++) {
        sprintf(name, "N%d", i);
        TF_EQ(nb_add_name(name, &num), NB_OK);
    }
    TF_EQ(inv(1, a_srv, quit, KEY_NONE), TCP_EXIT_NAME);
    TF_STREQ(g_err, "INVTCP: the local name could not be added "
             "(NetBIOS error 0E)\n");
    TF_STREQ(g_out, "");
    TF_EQ(g_asks, 0);
    TF_EQ(tsf_terms(), 1);
    TF_EQ(fnb_names(), FNB_NAMES);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(nb_delete_name("N1"), NB_OK);
    tsf_reset();
    good_run();
    fresh();
    g_fmode = FM_FAIL;
    fails(1, a_srv, TCP_EXIT_NOMEM, "not enough memory", 1);
    TF_EQ(fnb_sets(), 0);
    fresh();
    g_fmode = FM_LISTEN;
    fails(1, a_srv, TCP_EXIT_STACK, "a slot could not listen", 1);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_sets(), 0);
}

/* At exit, report a failed check of line, with handle 1 back on its
   own file, and end the program: the summary line is out and the exit
   status set by now. */
static void exit_check(int ok, int line)
{
    if (!ok) {
        cap_off();
        printf("T_INVTCP:%d: check at exit failed\n", line);
        fflush(stdout);
        abort();
    }
}

/* The checks every emergency stop must pass: no key asked for or read,
   nothing written, the stack left alone. */
static void exit_quiet(void)
{
    u32 n;

    exit_check(g_dpeeks == 0 && g_dreads == 0, __LINE__);
    fflush(stdout);
    exit_check(ftell(stderr) == 0 && cf_size(1, &n) == 0 && n == 0,
               __LINE__);
    exit_check(tsf_terms() == g_terms, __LINE__);
}

/* Runs after the emergency stop of the last run only: it kept the
   block, whose CALL the provider still owns; then the provider gives
   every NCB up. */
static void exit_newest(void)
{
    exit_quiet();
    exit_check(g_frees == 0, __LINE__);
    fnb_reset();
}

/* Runs last: an earlier emergency stop freed the block, zeroed. */
static void exit_last(void)
{
    exit_quiet();
    exit_check(cap_off(), __LINE__);
    exit_check(g_frees == 1 && g_fzero && g_fph == PH_EXIT, __LINE__);
}

int main(int argc, char **argv)
{
    int st;

    tf_begin("T_INVTCP");
    g_brk = dos_break_get();
    if (argc > 1 && strcmp(argv[argc - 1], "BREAK") == 0) {
        TF_EQ(g_brk, 1);
        argc--;
    }
    tfs_set_dir(argc > 1 ? argv[1] : "");
    strcpy(g_errf, tfs_path("ERR", ".TXT"));
    strcpy(g_outf, tfs_path("OUT", ".TXT"));
    key_set_source(keys, &g_ph);
    cmem_freeing = freeing;
    cmem_fault = far_hook;
    TF_EQ(atexit(exit_last), 0);
    test_usage();
    test_stop();
    test_ask();
    test_allow();
    test_wait();
    test_fail();
    fresh();
    TF_EQ(atexit(exit_newest), 0);
    TF_EQ(inv(1, a_srv, held, DRAINKEY), TCP_EXIT_OK);
    TF_EQ(g_frees, 0);
    TF_CHECK(nba_stuck() != 0);
    g_ph = PH_EXIT;
    g_dkey = DRAINKEY;
    g_dpeeks = 0;
    g_dreads = 0;
    g_terms = tsf_terms();
    TF_CHECK(freopen(g_errf, "w", stderr) != NULL);
    st = tf_done();
    exit_check(cap_on(), __LINE__);
    return st;
}

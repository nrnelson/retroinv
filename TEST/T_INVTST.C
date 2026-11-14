/*
 * T_INVTST.C - unit test for the request script interpreter of INVTEST
 * (INVSCR.C), run over the simulated NetBIOS provider (FAKENB.C) and
 * serial ports (FAKEUART.C) and a scratch mailbox.
 *
 * usage: T_INVTST dir
 *
 * A canned server in the test answers the interpreter's requests
 * through no-wait NCBs of its own, driven by the FAKENB pump from the
 * interpreter's waits; over the serial lines as their remote station,
 * driven by the FAKEUART pump; and in the mailbox, which is dir, from
 * the read hook (cf_fault_rd) as the interpreter looks for a response
 * file.  Each run reads its script from dir's S.SCR and writes its
 * transcript to dir's T.TXT, which the checks then read; the transcript
 * of the last run is kept.  T_INVTS2.C holds the serial and mailbox
 * checks (see its header).
 *
 * Checks:
 * - a script over two sessions passes: comments and blank lines, CR LF
 *   line ends, "> " and ">& " requests, @S, @WAIT, @SLEEP, @HANGUP,
 *   exact and glob checks, a dot-stuffed data response and one of more
 *   than NB_MSGMAX bytes; the session is closed after its BYE, not after
 *   BYEX, and is called again; a comment too long for the interpreter
 *   is skipped;
 * - the transcript masks the arguments of LOGIN and PASSWD in any case,
 *   after blanks or tabs, and the value of PASSWORD= in any case, bare,
 *   starting with '=' or quoted with an escaped quote inside; LOGINX is
 *   shown; the transcript holds no password;
 * - a differing response line (exact, a prefix, a check longer than the
 *   line, a glob, a glob that must backtrack), two differing lines (the
 *   first is named), a check with no line left, a request on a closed
 *   or never opened session, a blank request, an @WAIT with no response
 *   pending (also after the session was hung up and called again), a
 *   session the server hangs up before its response (read at once or
 *   at the next request), bad directives and a request line too long
 *   each fail the run at their line with INVSCR_FAIL, saying why;
 * - a local name another station holds fails with INVSCR_CALL at once,
 *   a missing script with INVSCR_FAIL and no line; a call refused while
 *   the server has no LISTEN posted, calls that timed out, and calls
 *   aborted beyond those one open makes again, are made again and pass,
 *   under DOS a second apart; on the host, whose clock
 *   is virtual, a call to a name no station has, and a call that times
 *   out every time, fail with INVSCR_CALL after a minute of calls and
 *   no more (on real DOS, where each open makes NBCLI_CALLTRIES CALLs
 *   of up to about half a minute, one more open of up to about 61
 *   seconds may run past it), and @SLEEP 65535 passes;
 * - after every run no NCB is in progress, no set, name or session end
 *   is left, and no session the provider had removed was hung up.
 *
 * Prints each failure and a summary line; exit status 0 when every
 * check passes, 1 when one fails, 2 for a usage error.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "FAKENB.H"
#include "NBCLI.H"
#include "INVSCR.H"
#include "T_INVTST.H"

#define SRV_NAME        "RISRV"
#define RX_LEN          128     /* each RECEIVE, so lines come in parts */
#define TX_OFF          (NSESS * RX_LEN)
#define BUF_LEN         (TX_OFF + NSESS * NB_MSGMAX)
#define LONG_LINE       600     /* a script line too long for INVSCR.C  */
#define SV_SETTLE       4       /* server steps run after a script      */
/* CALLs timed out before one passes: every CALL of one nbcli_open, so
   that the open fails once and the @CALL opens again a second later. */
#define CALL_TOUTS      NBCLI_CALLTRIES

/* The BIG response is longer than one message (the build fails
   otherwise), so the server sends it as two. */
extern char t_invtst_big[1];
extern char t_invtst_big[(BIG_LEN > NB_MSGMAX) ? 1 : 2];

static nba *g_set;
static cmem_blk *g_buf;
srv g_srv[NSESS];
char g_scr[DOS_PATHMAX];
char g_out[DOS_PATHMAX];

const char k_calla[] = "@CALL 1 RINVA";
const char k_badline[] = "bad line";

static const char *const k_pass[] = {
    "# two sessions, the second with its own name",
    k_calla,
    "> HELLO A",
    "< +OK",
    "",
    "> HELLO B",
    "<* +OK*",
    "> LOGIN ADMIN SECRET1",
    "<* +O*",
    "   ",
    "@CALL 2 RINVB",
    ">& PASSWD SECRET1 SECRET2",
    ">& USER ADD BOB LEVEL=1 PASSWORD=SECRET3",
    "@S 1",
    "> LIST",
    "< +DATA",
    "< F NAME",
    "< ..DOT",
    "<* *",
    "< .",
    "@WAIT 2",
    "<",
    "@WAIT 2",
    "<* -ERR 100 *",
    ">   login  admin  SECRET4",
    "> LOGIN\tadmin\tSECRET5",
    "> Login admin SECRET6",
    "> passwd SECRET7 SECRET8",
    "> user reset bob password=SECRET9",
    "> USER SET\tPassWord=SECRETA\tLEVEL=1",
    "> USER RESET BOB PASSWORD==SECRETB",
    "> USER RESET BOB PASSWORD=\"SECRET C\\\" SECRET\" LEVEL=1",
    "> LOGINX a b",
    "> BYEX",
    "> ECHO still open",
    "< still open",
    "> BIG",
    "< +DATA",
    "<* R 00 x*x",
    "@SLEEP 2",
    "> BYE",
    "< +OK BYE",
    "@CALL 1 RINVC",
    "> HELLO C",
    "< +OK",
    "> ECHO again",
    "< again",
    "@HANGUP 2",
    NULL
};

/* What the transcript of k_pass shows of its masked requests. */
static const char *const k_masked[] = {
    "1> LOGIN ***",
    "2>& PASSWD ***",
    "2>& USER ADD BOB LEVEL=1 PASSWORD=***",
    "1>   login ***",
    "1> Login ***",
    "1> passwd ***",
    "1> user reset bob password=***",
    "1> USER SET\tPassWord=***\tLEVEL=1",
    "1> USER RESET BOB PASSWORD=***",
    "1> USER RESET BOB PASSWORD=*** LEVEL=1",
    "1> LOGINX a b",
    NULL
};

/* Scripts that fail, each at its line given in k_fails. */
static const char *const k_differs[] = {
    k_calla, "> LIST", "< +DATA", "< F WRONG", NULL
};
static const char *const k_two[] = {
    k_calla, "> LIST", "< +DATA", "< WRONG1", "< WRONG2", NULL
};
static const char *const k_glob[] = {
    k_calla, "> HELLO", "<* +X*", NULL
};
static const char *const k_prefix[] = {
    k_calla, "> HELLO", "< +O", NULL
};
static const char *const k_longer[] = {
    k_calla, "> HELLO", "< +OK EXTRA", NULL
};
static const char *const k_back[] = {
    k_calla, "> ECHO abc", "<* ab*bc", NULL
};
static const char *const k_short[] = {
    k_calla, "> HELLO", "< +OK", "< EXTRA", NULL
};
static const char *const k_afterbye[] = {
    k_calla, "> BYE", "> HELLO", NULL
};
static const char *const k_noopen[] = { "> HELLO", NULL };
static const char *const k_blank[] = { k_calla, ">   ", NULL };
static const char *const k_nowait[] = { k_calla, "@WAIT 1", NULL };
static const char *const k_rehang[] = {
    k_calla, ">& HELLO", "@HANGUP 1", k_calla, "@WAIT 1", NULL
};
static const char *const k_twice[] = {
    k_calla, "@CALL 1 RINVB", NULL
};
static const char *const k_range[] = { "@CALL 5 RINVA", NULL };
static const char *const k_noname[] = { "@CALL 1", NULL };
static const char *const k_extra[] = { "@CALL 1 RINVA X", NULL };
static const char *const k_sargs[] = { k_calla, "@S 1 2", NULL };
static const char *const k_word[] = { "@PING 1", NULL };
static const char *const k_sleep[] = { "@SLEEP 0", NULL };
static const char *const k_sigil[] = { ">HELLO", NULL };
static const char *const k_twosp[] = { "@CALL 1  RINVA", NULL };
static const char *const k_tail[] = { "@CALL 1 RINVA ", NULL };
static const char *const k_stray[] = { "<* +OK", NULL };

/* A failing script, the line it fails at and the transcript line that
   says why. */
typedef struct {
    const char *const *lines;
    u16 line;
    const char *why;
} failcase;

static const failcase k_fails[] = {
    { k_differs, 4, "response line differs" },
    { k_two, 4, "response line differs" },
    { k_glob, 3, "response line differs" },
    { k_prefix, 3, "response line differs" },
    { k_longer, 3, "response line differs" },
    { k_back, 3, "response line differs" },
    { k_short, 4, "no response line left to check" },
    { k_afterbye, 3, "session not open" },
    { k_noopen, 1, "session not open" },
    { k_blank, 2, "blank request" },
    { k_nowait, 2, "no response pending" },
    { k_rehang, 5, "no response pending" },
    { k_twice, 2, "session already open" },
    { k_range, 1, k_badline },
    { k_noname, 1, k_badline },
    { k_extra, 1, k_badline },
    { k_sargs, 2, k_badline },
    { k_word, 1, k_badline },
    { k_sleep, 1, k_badline },
    { k_sigil, 1, k_badline },
    { k_twosp, 1, k_badline },
    { k_tail, 1, k_badline },
    { k_stray, 1, "no response line left to check" }
};

/* A script that passes after a comment line too long for the
   interpreter, or after a call refused; the one line put after a
   request line too long, or run with its local name taken. */
static const char *const k_aftercomment[] = {
    k_calla, "> HELLO", "< +OK", NULL
};
static const char *const k_call1[] = { k_calla, NULL };

/* Scripts whose session the server hangs up, the one before its
   response is read, the other before the next request is sent; each
   fails at its last line. */
static const char *const k_drop[] = { k_calla, "> DROP", NULL };
static const char *const k_dropnext[] = {
    k_calla, ">& DROP", "> HELLO", NULL
};

#ifdef COMPAT_HOST
/* An hour of sleep, on the host's virtual clock only. */
static const char *const k_sleepmax[] = {
    k_calla, "@SLEEP 65535", "> HELLO", "< +OK", NULL
};
#endif

/* Queue text as the server's output on session v. */
static void put(srv *v, const char *text)
{
    u16 n;

    if (v->opos == v->olen) {
        v->opos = 0;
        v->olen = 0;
    }
    n = (u16)strlen(text);
    if (TF_CHECK(v->olen + n <= SV_OUTSZ)) {
        memcpy(v->out + v->olen, text, n);
        v->olen = (u16)(v->olen + n);
    }
}

void answer(srv *v, const char *line)
{
    char row[BIG_ROW + 1];
    int i;

    if (strcmp(line, "DROP") == 0) {
        v->bye = 1;
    } else if (strcmp(line, "BYE") == 0) {
        put(v, "+OK BYE\r\n");
        v->bye = 1;
    } else if (strcmp(line, "LIST") == 0) {
        put(v, "+DATA\r\nF NAME\r\n..DOT\r\nR A\r\n.\r\n");
    } else if (strcmp(line, "BIG") == 0) {
        put(v, "+DATA\r\n");
        for (i = 0; i < BIG_LINES; i++) {
            sprintf(row, "R %02d ", i);
            memset(row + BIG_HEAD, 'x', BIG_XS);
            strcpy(row + BIG_HEAD + BIG_XS, "\r\n");
            put(v, row);
        }
        put(v, ".\r\n");
    } else if (strncmp(line, "HELLO", 5) == 0 ||
               strncmp(line, "LOGIN ", 6) == 0) {
        put(v, "+OK\r\n");
    } else if (strncmp(line, "ECHO ", 5) == 0) {
        put(v, line + 5);
        put(v, "\r\n");
    } else if (strncmp(line, "PASSWD ", 7) == 0) {
        put(v, "\r\n");
    } else {
        put(v, "-ERR 100 unknown command\r\n");
    }
}

/* Post the RECEIVE of session index i. */
static void post_recv(srv *v, u16 i)
{
    v->rx = (u8)TF_EQ(nba_recv(g_set, i, v->lsn, g_buf,
                               (u16)(i * RX_LEN), RX_LEN), NB_OK);
}

void take(srv *v, const char *piece, u16 n)
{
    u16 k;

    for (k = 0; k < n; k++) {
        if (piece[k] != '\n') {
            if (TF_CHECK(v->llen < SV_LINESZ - 1))
                v->line[v->llen++] = piece[k];
            continue;
        }
        if (v->llen > 0 && v->line[v->llen - 1] == '\r')
            v->llen--;
        v->line[v->llen] = '\0';
        v->llen = 0;
        answer(v, v->line);
    }
}

/* Move session index i of the server on by one step. */
static void serve_one(srv *v, u16 i)
{
    char piece[RX_LEN];
    u16 h;
    u16 n;
    u8 rc;

    if (v->state == SV_LISTEN) {
        rc = nba_poll(g_set, i);
        if (rc == NB_RC_PENDING)
            return;
        v->state = SV_DONE;
        if (rc != NB_OK)
            return;
        v->state = SV_OPEN;
        v->lsn = nba_lsn(g_set, i);
        post_recv(v, i);
    }
    if (v->state != SV_OPEN)
        return;
    if (v->rx && (rc = nba_poll(g_set, i)) != NB_RC_PENDING) {
        v->rx = 0;
        if (rc != NB_OK && rc != NB_RC_INCOMPLETE) {
            /* The RECEIVE failed: the session has ended. */
            v->state = SV_DONE;
            return;
        }
        n = nba_len(g_set, i);
        cmem_get(g_buf, (u16)(i * RX_LEN), piece, n);
        take(v, piece, n);
        if (!v->bye)
            post_recv(v, i);
    }
    h = (u16)(NSESS + i);
    if (nba_poll(g_set, h) == NB_RC_PENDING)
        return;
    if (v->opos < v->olen) {
        n = (u16)(v->olen - v->opos);
        if (n > NB_MSGMAX)
            n = NB_MSGMAX;
        cmem_put(g_buf, (u16)(TX_OFF + i * NB_MSGMAX), v->out + v->opos, n);
        if (nba_send(g_set, h, v->lsn, g_buf, (u16)(TX_OFF + i * NB_MSGMAX),
                     n) == NB_OK)
            v->opos = (u16)(v->opos + n);
    } else if (v->bye) {
        nb_hangup(v->lsn);
        v->state = SV_DONE;
    }
}

/* The pump: one step of every server session (arg is g_srv). */
static void serve(void *arg)
{
    u16 i;

    for (i = 0; i < NSESS; i++)
        serve_one((srv *)arg + i, i);
}

/* The pump of a server that posts its LISTENs late: at its first run,
   which a refused CALL makes, it posts them, and serve takes over. */
static void late(void *arg)
{
    u16 i;

    for (i = 0; i < NSESS; i++)
        TF_EQ(nba_listen(g_set, i, SRV_NAME, "*", 0, 0), NB_OK);
    fnb_pump(serve, arg);
}

/* Reset the provider and start the server, a LISTEN on each session. */
static void srv_start(void)
{
    u16 i;
    u8 num;

    fnb_reset();
    memset(g_srv, 0, sizeof g_srv);
    TF_EQ(nb_present(), 1);
    TF_EQ(nb_add_name(SRV_NAME, &num), NB_OK);
    TF_EQ(nba_open(&g_set, 2 * NSESS), 0);
    g_buf = cmem_alloc(BUF_LEN);
    TF_CHECK(g_buf != NULL);
    for (i = 0; i < NSESS; i++)
        TF_EQ(nba_listen(g_set, i, SRV_NAME, "*", 0, 0), NB_OK);
    fnb_pump(serve, g_srv);
}

/* Let the server see the ends of its sessions, stop it and check that
   nothing is left and that no session the provider had removed was hung
   up. */
static void srv_stop(void)
{
    int k;
    int i;

    fnb_pump(NULL, NULL);
    for (k = 0; k < SV_SETTLE; k++)
        serve(g_srv);
    for (i = 0; i < NSESS; i++)
        TF_CHECK(g_srv[i].state != SV_OPEN);
    TF_EQ(nba_close(g_set), 0);
    TF_EQ(nb_delete_name(SRV_NAME), NB_OK);
    cmem_free(g_buf);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_badhangups(), 0);
}

void write_script(const char *longp, const char *const *lines)
{
    FILE *f;
    int i;

    f = fopen(g_scr, "wb");
    if (!TF_CHECK(f != NULL))
        return;
    if (longp != NULL) {
        fputs(longp, f);
        for (i = 0; i < LONG_LINE; i++)
            putc('x', f);
        fputs("\r\n", f);
    }
    for (; *lines != NULL; lines++)
        fprintf(f, "%s\r\n", *lines);
    fclose(f);
}

int drive(const char *server, u16 *line)
{
    FILE *out;
    int rc;

    out = fopen(g_out, "w");
    if (!TF_CHECK(out != NULL))
        return -1;
    rc = invscr_run(out, server, g_scr, line);
    fclose(out);
    return rc;
}

int seen(const char *text, int whole)
{
    char buf[SV_LINESZ];
    FILE *f;
    size_t n;
    int found;

    f = fopen(g_out, "r");
    if (f == NULL)
        return 0;
    found = 0;
    while (!found && fgets(buf, (int)sizeof buf, f) != NULL) {
        n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n')
            buf[--n] = '\0';
        found = whole ? strcmp(buf, text) == 0 : strstr(buf, text) != NULL;
    }
    fclose(f);
    return found;
}

/* Run the script against the server and check the result and the line
   it gives. */
static void run(int rc, u16 at)
{
    u16 line;

    srv_start();
    TF_EQ(drive(SRV_NAME, &line), rc);
    TF_EQ(line, at);
    srv_stop();
}

static void test_pass(void)
{
    char last[BIG_ROW + 3];
    const char *const *m;
    int n;

    write_script(NULL, k_pass);
    run(INVSCR_PASS, 0);
    TF_CHECK(seen("@CALL 2 RINVB", 1));
    for (m = k_masked; *m != NULL; m++)
        TF_CHECK(seen(*m, 1));
    TF_CHECK(seen("1< ..DOT", 1));
    n = sprintf(last, "1< R %02d ", BIG_LINES - 1);
    memset(last + n, 'x', BIG_XS);
    last[n + BIG_XS] = '\0';
    TF_CHECK(seen(last, 1));
    TF_CHECK(seen("2<", 1));
    TF_CHECK(seen("2< -ERR 100 unknown command", 1));
    TF_CHECK(seen("1< again", 1));
    TF_CHECK(!seen("SECRET", 0));

    write_script("# ", k_aftercomment);
    run(INVSCR_PASS, 0);
}

static void test_failures(void)
{
    const failcase *c;

    for (c = k_fails; c < k_fails + sizeof k_fails / sizeof k_fails[0];
         c++) {
        write_script(NULL, c->lines);
        run(INVSCR_FAIL, c->line);
        TF_CHECK(seen(c->why, 1));
    }
    write_script("> ", k_call1);
    run(INVSCR_FAIL, 1);
    TF_CHECK(seen(k_badline, 1));
}

static void test_dropped(void)
{
    static const char *const *const scripts[] = { k_drop, k_dropnext };
    u16 line;
    u16 k;

    for (k = 0; k < 2; k++) {
        write_script(NULL, scripts[k]);
        srv_start();
        TF_EQ(drive(SRV_NAME, &line), INVSCR_FAIL);
        TF_EQ(line, (u16)(k + 2));
        srv_stop();
        TF_CHECK(seen("session lost", 1));
    }
}

static void test_call_and_open(void)
{
    u32 start;
    u32 ticks;
    u16 line;
    u16 i;

    /* A name another station holds is refused at once. */
    write_script(NULL, k_call1);
    srv_start();
    fnb_dupname("RINVA");
    TF_EQ(drive(SRV_NAME, &line), INVSCR_CALL);
    TF_EQ(line, 1);
    srv_stop();
    TF_CHECK(seen("local name not added, NetBIOS rc=16", 1));

    /* No LISTEN at the first call: it is refused, and made again. */
    write_script(NULL, k_aftercomment);
    srv_start();
    for (i = 0; i < NSESS; i++)
        TF_EQ(nba_cancel(g_set, i), NB_OK);
    fnb_pump(late, g_srv);
    TF_EQ(drive(SRV_NAME, &line), INVSCR_PASS);
    TF_EQ(line, 0);
    srv_stop();

    /* Calls that timed out, as ones the server dropped as they opened,
       are made again, a second apart: under DOS, whose clock is real,
       the retries take CALL_TOUTS seconds and well under twice that. */
    srv_start();
    fnb_call_timeouts(CALL_TOUTS);
    start = tmr_ticks();
    TF_EQ(drive(SRV_NAME, &line), INVSCR_PASS);
    ticks = tmr_since(start);
    TF_EQ(line, 0);
    srv_stop();
#ifndef COMPAT_HOST
    TF_CHECK(ticks >= CALL_TOUTS * TMR_SEC &&
             ticks < 2 * CALL_TOUTS * TMR_SEC);
#endif

    /* Calls aborted, more than one open makes: the open that fails is
       made again. */
    srv_start();
    fnb_call_fail(NBCLI_CALLTRIES + 1, NB_RC_ABORTED);
    TF_EQ(drive(SRV_NAME, &line), INVSCR_PASS);
    TF_EQ(line, 0);
    srv_stop();

#ifdef COMPAT_HOST
    /* A minute of calls, on the host's virtual clock only. */
    srv_start();
    start = tmr_ticks();
    TF_EQ(drive("NOSRV", &line), INVSCR_CALL);
    TF_CHECK(tmr_since(start) >= INVSCR_CALLTIME);
    TF_EQ(line, 1);
    srv_stop();
    TF_CHECK(seen("call failed, NetBIOS rc=14", 1));
    /* A minute of calls that all time out, and no more. */
    srv_start();
    fnb_call_timeouts(0xFFFFu);
    start = tmr_ticks();
    TF_EQ(drive(SRV_NAME, &line), INVSCR_CALL);
    ticks = tmr_since(start);
    TF_CHECK(ticks >= INVSCR_CALLTIME && ticks < INVSCR_CALLTIME + TMR_10SEC);
    TF_EQ(line, 1);
    srv_stop();
    TF_CHECK(seen("call failed, NetBIOS rc=05", 1));
    write_script(NULL, k_sleepmax);
    run(INVSCR_PASS, 0);
#endif

    TF_EQ(cf_remove(g_scr), 0);
    run(INVSCR_FAIL, 0);
    TF_CHECK(seen("cannot open", 0));
}

int main(int argc, char **argv)
{
    static const char *const files[] = { "S.SCR", NULL };

    tf_begin("T_INVTST");
    if (argc < 2) {
        printf("usage: T_INVTST dir\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    strcpy(g_scr, tfs_path("S", ".SCR"));
    strcpy(g_out, tfs_path("T", ".TXT"));
    tfs_wipe(files);
    test_pass();
    test_failures();
    test_dropped();
    test_call_and_open();
    test_serial();
    test_mbox();
    tfs_wipe(files);
    return tf_done();
}

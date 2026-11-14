/*
 * T_LINKN.C - unit test for the NET backend of the client's link
 * (LINK.C over NBCLI.C) on the simulated provider (FAKENB.C).
 *
 * usage: T_LINKN
 *
 * A scripted server, run from the provider's pump, answers each request
 * line with the next reply of the test's table and hangs up only a
 * session it has aborted, so every other end of a session the test sees
 * is the link's own.  Checks:
 * - BYE: an answer other than +OK leaves the session open; +OK to a
 *   lower-case BYE after spaces ends it (LINK_CLOSED), the link hangs
 *   up at once, and later requests, empty lines included, are not sent
 *   and give LINK_CLOSED;
 * - LOGIN: -ERR 401, 403, 3xx and 500 count, 100, 102 and 400 do not,
 *   nor a 401 to another command; +OK resets the count; the
 *   LINK_MAXFAIL-th failure ends the session; LOGIN is read as the
 *   tokenizer reads it (after a tab, in any case, in quotes); no byte
 *   of the link holds the password of a LOGIN or PASSWD once it has
 *   been answered, those bytes including the buffer the link delivers
 *   its answer to an over-long line from;
 * - -ERR 501 (the server shutting down) to any request ends it;
 * - a session lost while a request waits gives LINK_ELOST, then
 *   LINK_ELOST at once without sending, and rq_send RQ_ELOST with
 *   res.closed set;
 * - a failing line callback gives LINK_EOUTPUT and the next request
 *   still gets its own response;
 * - a line over LINK_MAXLINE is answered with 103 without being sent,
 *   a line of LINK_MAXLINE bytes is sent, and so is a line of pairs
 *   only, but neither an empty line nor one of spaces and TABs;
 * - link_feed stops at the response that ends the session and sends
 *   nothing after it;
 * - link_netname names the calling name;
 * - a CALL the server's name refuses (no LISTEN posted) is
 *   LINK_EREFUSED, one that no station answers LINK_ECALL, each with
 *   its NetBIOS code and no link;
 * - after each test no NCB is in progress and no name or session end is
 *   left, and no hang-up named a session end the provider had removed.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "FAKENB.H"
#include "LINK.H"
#include "RQ.H"

#define SRVNAME         "SRV"
#define PW              "SECRET1"       /* the password requests carry  */
#define OUTMAX          512     /* response text a test keeps           */
#define QMAX            512     /* reply bytes queued for sending       */

static nba *g_srv;              /* NCB 0 LISTEN then RECEIVE, 1 SEND    */
static cmem_blk *g_sb;          /* received message, then message sent  */
static u8 g_slsn;
static char g_who[NB_NAMELEN];  /* the name the link called from        */
static u8 g_inpump;
static u8 g_abort;              /* the session was aborted              */
static const char *const *g_rep;    /* replies, NULL: abort instead     */
static u16 g_nreq;              /* request lines received               */
static char g_q[QMAX];
static u16 g_qn;
static char g_out[OUTMAX];      /* response lines, each ended by LF     */
static u16 g_on;
static u16 g_failat;            /* the line take refuses, from 1; 0 none */
static u16 g_lines;
static const char *g_lp;        /* the last line take was handed, and   */
static u16 g_ll;                /*   its length                         */

/* Queue reply text, its LF line ends sent as CR LF; a reply that does
   not fit fails the test and is not queued. */
static void queue(const char *text)
{
    const char *p;
    u16 n;

    n = 0;
    for (p = text; *p != '\0'; p++)
        n = (u16)(n + (*p == '\n' ? 2 : 1));
    if (!TF_CHECK(g_qn + n <= QMAX))
        return;
    for (; *text != '\0'; text++) {
        if (*text == '\n')
            g_q[g_qn++] = '\r';
        g_q[g_qn++] = *text;
    }
}

/* The scripted server on the set arg: accept the call, answer each
   request line with the next reply of g_rep; at a NULL reply, abort
   the session and hang up its own end. */
static void pump(void *arg)
{
    nba *s;
    char buf[NB_MSGMAX];
    u16 len;
    u16 i;
    u8 rc;

    s = (nba *)arg;
    if (g_inpump || g_abort)
        return;
    g_inpump = 1;
    rc = nba_poll(s, 0);
    if (g_slsn == 0 && rc == NB_OK) {
        g_slsn = nba_lsn(s, 0);
        nba_callname(s, 0, g_who);
        rc = NB_RC_CANCELLED;
    } else if (g_slsn != 0 && rc == NB_OK) {
        len = nba_len(s, 0);
        cmem_get(g_sb, 0, buf, len);
        for (i = 0; i < len && !g_abort; i++) {
            if (buf[i] != '\n')
                continue;
            if (g_rep[g_nreq] == NULL) {
                fnb_abort(g_slsn);
                nb_hangup(g_slsn);
                g_abort = 1;
            } else {
                queue(g_rep[g_nreq]);
            }
            g_nreq++;
        }
    }
    if (g_slsn != 0 && !g_abort && rc != NB_RC_PENDING &&
        rc != NB_RC_SESSCLOSED)
        nba_recv(s, 0, g_slsn, g_sb, 0, NB_MSGMAX);
    if (g_qn > 0 && !g_abort && nba_poll(s, 1) != NB_RC_PENDING) {
        cmem_put(g_sb, NB_MSGMAX, g_q, g_qn);
        nba_send(s, 1, g_slsn, g_sb, NB_MSGMAX, g_qn);
        g_qn = 0;
    }
    g_inpump = 0;
}

/* Start the server with the replies rep and open the link to it. */
static link *start(const char *const *rep)
{
    link *l;
    u8 num;
    u8 nbrc;

    fnb_reset();
    TF_EQ(nb_present(), 1);
    TF_EQ(nb_add_name(SRVNAME, &num), NB_OK);
    TF_EQ(nba_open(&g_srv, 2), 0);
    g_slsn = 0;
    g_who[0] = '\0';
    g_abort = 0;
    g_rep = rep;
    g_nreq = 0;
    g_qn = 0;
    g_on = 0;
    g_failat = 0;
    TF_EQ(nba_listen(g_srv, 0, SRVNAME, "*", 0, 0), NB_OK);
    fnb_pump(pump, g_srv);
    l = NULL;
    TF_EQ(link_open_net(&l, "srv", &nbrc), 0);
    TF_EQ(fnb_sessions(), 2);
    return l;
}

/* Close the link and the server, whose end has gone with the link's,
   and check that nothing is left. */
static void stop(link *l)
{
    TF_EQ(link_close(l), 0);
    fnb_pump(NULL, NULL);
    TF_EQ(nba_close(g_srv), 0);
    nb_delete_name(SRVNAME);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_badhangups(), 0);
}

/* link_line_fn: count the line in the u16 at arg, note where it is
   and keep it in g_out; refuse line g_failat. */
static int take(void *arg, const char *line, u16 len)
{
    u16 *n;

    n = (u16 *)arg;
    g_lp = line;
    g_ll = len;
    ++*n;
    if (*n == g_failat)
        return 1;
    if (g_on + len + 1 < OUTMAX) {
        memcpy(g_out + g_on, line, len);
        g_on = (u16)(g_on + len);
        g_out[g_on++] = '\n';
    }
    g_out[g_on] = '\0';
    return 0;
}

/* 1 when s is anywhere in the bytes of l itself. */
static int link_has(link *l, const char *s)
{
    const char *b;
    u16 size, len, i;

    b = (const char *)link_bytes(l, &size);
    len = (u16)strlen(s);
    for (i = 0; i + len <= size; i++)
        if (memcmp(b + i, s, len) == 0)
            return 1;
    return 0;
}

/* 1 when the len bytes at p lie inside the bytes of l itself. */
static int in_link(link *l, const char *p, u16 len)
{
    const char *b;
    u16 size;
    u16 i;

    b = (const char *)link_bytes(l, &size);
    for (i = 0; i + len <= size; i++)
        if (b + i == p)
            return 1;
    return 0;
}

/* Send line on l; its result. */
static int ask(link *l, const char *line)
{
    g_on = 0;
    g_out[0] = '\0';
    g_lines = 0;
    return link_send(l, line, (u16)strlen(line), take, &g_lines);
}

static void test_bye(void)
{
    static const char *const rep[] = {
        "-ERR 102 bad argument FIELD=X\n", "+OK\n", "+OK\n"
    };
    link *l;

    l = start(rep);
    TF_EQ(ask(l, "BYE X"), LINK_OK);
    TF_CHECK(g_who[0] != '\0');
    TF_STREQ(link_netname(l), g_who);
    TF_STREQ(g_out, "-ERR 102 bad argument FIELD=X\n");
    TF_EQ(fnb_sessions(), 2);
    TF_EQ(ask(l, "  bye"), LINK_CLOSED);
    TF_STREQ(g_out, "+OK\n");
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(ask(l, "PING"), LINK_CLOSED);
    TF_EQ(ask(l, ""), LINK_CLOSED);
    TF_STREQ(g_out, "");
    TF_EQ(g_nreq, 2);
    stop(l);
}

static void test_logins(void)
{
    static const char *const rep[] = {
        "-ERR 401 bad user name or password\n",
        "-ERR 403 account disabled\n",
        "-ERR 100 syntax error\n",
        "-ERR 102 missing argument FIELD=PASSWORD\n",
        "-ERR 400 HELLO or LOGIN required\n",
        "-ERR 401 bad password\n",
        "+OK LEVEL=3 NAME=\"A\"\n",
        "-ERR 403 account disabled\n",
        "-ERR 300 database I/O error\n",
        "-ERR 500 internal error\n"
    };
    static const char k_login[] = "LOGIN A " PW;
    static const char k_passwd[] = "PASSWD A " PW;
    link *l;
    u16 i;

    l = start(rep);
    for (i = 0; i < 9; i++) {
        TF_EQ(ask(l, i == 5 ? k_passwd : k_login), LINK_OK);
        TF_EQ(fnb_sessions(), 2);
        TF_EQ(link_has(l, PW), 0);
    }
    TF_EQ(ask(l, k_login), LINK_CLOSED);
    TF_STREQ(g_out, "-ERR 500 internal error\n");
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(ask(l, "PING"), LINK_CLOSED);
    TF_EQ(g_nreq, 10);
    stop(l);
}

/* LOGIN as the tokenizer reads it: after a tab, in lower case, quoted. */
static void test_login_words(void)
{
    static const char *const rep[] = {
        "-ERR 401 bad user name or password\n",
        "-ERR 401 bad user name or password\n",
        "-ERR 401 bad user name or password\n"
    };
    link *l;

    l = start(rep);
    TF_EQ(ask(l, "\tlogin A B"), LINK_OK);
    TF_EQ(ask(l, "\"LOGIN\" A B"), LINK_OK);
    TF_EQ(ask(l, "\"Login\"\tA B"), LINK_CLOSED);
    TF_EQ(fnb_sessions(), 0);
    stop(l);
}

static void test_shutdown(void)
{
    static const char *const rep[] = {
        "+OK PONG\n", "-ERR 501 server shutting down\n", "+OK\n"
    };
    link *l;

    l = start(rep);
    TF_EQ(ask(l, "PING"), LINK_OK);
    TF_EQ(ask(l, "ITEM LIST"), LINK_CLOSED);
    TF_STREQ(g_out, "-ERR 501 server shutting down\n");
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(ask(l, "PING"), LINK_CLOSED);
    TF_EQ(g_nreq, 2);
    stop(l);
}

static void test_lost(void)
{
    static const char *const rep[] = { "+OK PONG\n", NULL, "+OK\n" };
    rq q;
    rq_res res;
    link *l;

    l = start(rep);
    TF_EQ(ask(l, "PING"), LINK_OK);
    TF_EQ(ask(l, "PING"), LINK_ELOST);
    TF_STREQ(g_out, "");
    TF_EQ(ask(l, "PING"), LINK_ELOST);
    TF_EQ(ask(l, ""), LINK_ELOST);
    TF_EQ(g_nreq, 2);
    rq_begin(&q, "PING");
    TF_EQ(rq_send(l, &q, NULL, NULL, &res), RQ_ELOST);
    TF_EQ(res.code, RQ_ELOST);
    TF_EQ(res.closed, 1);
    TF_EQ(g_nreq, 2);
    stop(l);
}

static void test_output(void)
{
    static const char *const rep[] = {
        "+DATA\nF ID\nR 1\n.\n", "+OK PONG\n"
    };
    link *l;

    l = start(rep);
    g_failat = 2;
    g_lines = 0;
    TF_EQ(link_send(l, "ITEM LIST", 9, take, &g_lines), LINK_EOUTPUT);
    TF_EQ(ask(l, "PING"), LINK_OK);
    TF_STREQ(g_out, "+OK PONG\n");
    stop(l);
}

static void test_lines(void)
{
    static const char *const rep[] = {
        "-ERR 100 no command\n", "-ERR 101 unknown command\n"
    };
    char line[LINK_MAXLINE + 2];
    link *l;

    l = start(rep);
    memset(line, 'X', LINK_MAXLINE + 1);
    line[LINK_MAXLINE + 1] = '\0';
    TF_EQ(ask(l, line), LINK_OK);
    TF_CHECK(strncmp(g_out, "-ERR 103 ", 9) == 0);
    TF_EQ(in_link(l, g_lp, g_ll), 1);
    TF_EQ(g_nreq, 0);
    TF_EQ(ask(l, ""), LINK_OK);
    TF_STREQ(g_out, "");
    TF_EQ(g_nreq, 0);
    TF_EQ(ask(l, " \t "), LINK_OK);
    TF_STREQ(g_out, "");
    TF_EQ(g_nreq, 0);
    TF_EQ(ask(l, "A=1"), LINK_OK);
    TF_STREQ(g_out, "-ERR 100 no command\n");
    TF_EQ(g_nreq, 1);
    line[LINK_MAXLINE] = '\0';
    TF_EQ(ask(l, line), LINK_OK);
    TF_STREQ(g_out, "-ERR 101 unknown command\n");
    TF_EQ(g_nreq, 2);
    stop(l);
}

static void test_feed(void)
{
    static const char k_in[] = "PING\r\nBYE\r\nPING\r\n";
    static const char *const rep[] = { "+OK PONG\n", "+OK\n", "+OK\n" };
    link *l;

    l = start(rep);
    g_lines = 0;
    TF_EQ(link_feed(l, k_in, (u16)(sizeof k_in - 1), take, &g_lines),
          LINK_CLOSED);
    TF_STREQ(g_out, "+OK PONG\n+OK\n");
    TF_EQ(g_nreq, 2);
    TF_EQ(fnb_sessions(), 0);
    stop(l);
}

/* The CALL refused by the server's name, and the CALL unanswered. */
static void test_call(void)
{
    link *l;
    u8 num;
    u8 nbrc;

    fnb_reset();
    TF_EQ(nb_present(), 1);
    TF_EQ(nb_add_name(SRVNAME, &num), NB_OK);
    TF_EQ(link_open_net(&l, "srv", &nbrc), LINK_EREFUSED);
    TF_EQ(nbrc, NB_RC_REJECTED);
    TF_CHECK(l == NULL);
    TF_EQ(link_open_net(&l, "nosuch", &nbrc), LINK_ECALL);
    TF_EQ(nbrc, NB_RC_NOANSWER);
    TF_CHECK(l == NULL);
    nb_delete_name(SRVNAME);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 0);
}

int main(void)
{
    tf_begin("T_LINKN");
    cmem_init();
    g_sb = cmem_alloc(2 * NB_MSGMAX);
    if (!TF_CHECK(g_sb != NULL))
        return tf_done();
    test_bye();
    test_logins();
    test_login_words();
    test_shutdown();
    test_lost();
    test_output();
    test_lines();
    test_feed();
    test_call();
    cmem_free(g_sb);
    return tf_done();
}

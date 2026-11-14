/*
 * T_NBCL2.C - the second half of the T_NBCLI test (see T_NBCLI.C):
 * requests and responses over an open session (the send copy wiped
 * after each SEND, a RECEIVE posted before each response comes), the
 * framing, over-long and blank lines, a failing line callback, the
 * waits of RECEIVE and SEND, sessions that end (never hung up once the
 * provider has removed them), a command stuck at the close, and the
 * NCB limit, posts refused for a while and then taken.  It is a file
 * of its own only to stay within the compiler memory of Watcom C 6.5;
 * T_NBCLI.H holds what both share.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#define TF_PART
#define TF_MALLOC
#define TF_FILE "T_NBCL2"
#include "TESTFW.H"
#include "FAKENB.H"
#include "NBCLI.H"
#include "T_NBCLI.H"

#define BIGMAX          3072    /* the longest line built, NUL included */
#define W               2       /* seconds of the short waits timed     */
#define WAIT            NBCLI_SENDWAIT  /* seconds of the others        */
#define ALLSTALL        0xFFFF  /* fnb_stall: every cancel of the test  */
#define HIDDEN          0xFFFF  /* fnb_latency: polls past every wait   */
#define SLACK           4       /* ticks the polls around a wait read   */

static char g_big[BIGMAX];
static char g_exp[LNMAX];

/* Ticks a wait of secs seconds takes at least and, with SLACK for the
   polls around it, at most; the host's timer counts one tick per call,
   so a client that reads it once per poll waits that many pump
   calls. */
#define TICKS_MIN(secs) ((u16)((secs) * TMR_SEC))
#define TICKS_MAX(secs) ((u16)((secs) * (TMR_SEC + 1) + SLACK))

/* g_big: the text head, then n copies of c, then tail. */
static u16 build(const char *head, char c, u16 n, const char *tail)
{
    u16 len;

    len = (u16)strlen(head);
    memcpy(g_big, head, len);
    memset(g_big + len, c, n);
    len = (u16)(len + n);
    strcpy(g_big + len, tail);
    return (u16)(len + strlen(tail));
}

/* Append to g_exp the pieces of at most NBCLI_MAXRESP bytes that the
   len bytes at s are delivered in, each followed by '|'. */
static void pieces(const char *s, u16 len)
{
    u16 at;
    u16 n;

    at = (u16)strlen(g_exp);
    do {
        n = len > NBCLI_MAXRESP ? NBCLI_MAXRESP : len;
        memcpy(g_exp + at, s, n);
        at = (u16)(at + n);
        g_exp[at++] = '|';
        s += n;
        len = (u16)(len - n);
    } while (len > 0);
    g_exp[at] = '\0';
}

/* 1 when the buffer of the last no-wait SEND posted was len bytes and
   holds only zeros: the client's copy of what it sent, wiped. */
static int wiped(u16 len)
{
    cmem_blk *b;
    u16 off;
    u16 n;
    u16 i;
    char got[64];

    b = fnb_lastbuf(1, &off, &n);
    if (b == NULL || n != len)
        return 0;
    while (n > 0) {
        i = n < sizeof got ? n : (u16)sizeof got;
        cmem_get(b, off, got, i);
        off = (u16)(off + i);
        n = (u16)(n - i);
        while (i > 0)
            if (got[--i] != 0)
                return 0;
    }
    return 1;
}

/* resp, with the line callback failing at its call at. */
static int resp_fail(nbcli *n, u16 at)
{
    clear();
    g_failat = at;
    return nbcli_response(n, take, &g_lines, WAIT);
}

/* Requests in one message each, responses one, several or a part to a
   message, with each latency; a RECEIVE is posted before each response
   comes, from the open on, so that none is late. */
static void test_request(void)
{
    nbcli *n;
    u16 lat;

    for (lat = 0; lat <= 3; lat += 3) {
        fresh();
        fnb_latency(lat);
        n = connect("cli");
        srv_at(0, 0, NULL);
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_EQ(g_lines, 0);
        TF_EQ(g_pumps, 0);
        TF_EQ(sendl(n, "HELLO X"), NBCLI_OK);
        TF_CHECK(wiped(9));
        srv_run();
        TF_STREQ(g_rx, "HELLO X\r\n");
        TF_EQ(g_nmsg, 1);
        srv_text("+OK hi\r\n");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+OK hi|");
        /* Three requests, their responses in one message. */
        TF_EQ(sendl(n, "A"), NBCLI_OK);
        TF_EQ(sendl(n, "B c"), NBCLI_OK);
        TF_EQ(sendl(n, "C"), NBCLI_OK);
        srv_run();
        TF_STREQ(g_rx, "A\r\nB c\r\nC\r\n");
        TF_EQ(g_nmsg, 3);
        srv_text("+OK 1\r\n-ERR 100 bad\r\n+DATA\r\nK A=1\r\n..x\r\n.\r\n");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+OK 1|");
        srv_at(0, 0, NULL);
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "-ERR 100 bad|");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+DATA|K A=1|..x|.|");
        TF_EQ(g_pumps, 0);
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_EQ(g_lines, 0);
        /* "+DATA" and more is a single line; so is a data line alone. */
        TF_EQ(sendl(n, "D"), NBCLI_OK);
        TF_EQ(sendl(n, "E"), NBCLI_OK);
        srv_text("+DATAx\r\n.\r\n");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+DATAx|");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, ".|");
        /* Two requests, the second response sent only once the first
           is read: the RECEIVE posted after the read takes it. */
        TF_EQ(sendl(n, "F"), NBCLI_OK);
        TF_EQ(sendl(n, "G"), NBCLI_OK);
        srv_text("+OK f\r\n");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+OK f|");
        srv_text("+OK g\r\n");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+OK g|");
        /* Empty data lines are lines, not terminators. */
        TF_EQ(sendl(n, "H"), NBCLI_OK);
        srv_text("+DATA\r\n\r\nL\r\n\r\n.\r\n");
        TF_EQ(resp(n, WAIT), NBCLI_OK);
        TF_STREQ(g_ln, "+DATA||L||.|");
        TF_EQ(fnb_late(), 0);
        TF_EQ(nbcli_closed(n), 0);
        nbcli_close(n);
        TF_EQ(nb_hangup(g_clsn), NB_RC_BADLSN);
        finish();
    }
}

/* Two responses split into two messages at every byte, and sent a
   byte to a message (split at 0). */
static void test_frame(void)
{
    static const char text[] =
        "+DATA\r\nK A=1\r\n..x\r\n.\r\n+OK next\r\n";
    nbcli *n;
    u16 len;
    u16 k;
    u16 i;
    int ok;

    fresh();
    n = connect("cli");
    len = (u16)strlen(text);
    for (k = 0; k < len; k++) {
        TF_EQ(sendl(n, "A"), NBCLI_OK);
        TF_EQ(sendl(n, "B"), NBCLI_OK);
        if (k == 0) {
            for (i = 0; i < len; i++)
                srv_queue(text + i, 1);
        } else {
            srv_queue(text, k);
            srv_queue(text + k, (u16)(len - k));
        }
        ok = resp(n, WAIT) == NBCLI_OK &&
             strcmp(g_ln, "+DATA|K A=1|..x|.|") == 0;
        ok = ok && resp(n, WAIT) == NBCLI_OK && strcmp(g_ln, "+OK next|") == 0;
        if (!TF_CHECK(ok))
            printf("  split at %u\n", (unsigned)k);
    }
    nbcli_close(n);
    finish();
}

/* Response lines longer than NBCLI_MAXRESP. */
static void test_long(void)
{
    nbcli *n;
    u16 len;

    fresh();
    n = connect("cli");
    /* A single line in three pieces, one in exactly two. */
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    TF_EQ(sendl(n, "B"), NBCLI_OK);
    len = build("+OK ", 'a', 1096, "\r\n");
    srv_queue(g_big, len);
    g_exp[0] = '\0';
    pieces(g_big, (u16)(len - 2));
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_EQ(g_lines, 3);
    TF_STREQ(g_ln, g_exp);
    len = build("+OK ", 'b', 2 * NBCLI_MAXRESP - 4, "\r\n");
    srv_queue(g_big, len);
    g_exp[0] = '\0';
    pieces(g_big, (u16)(len - 2));
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_EQ(g_lines, 2);
    TF_STREQ(g_ln, g_exp);
    /* A data line whose last piece is ".", then the terminator. */
    TF_EQ(sendl(n, "C"), NBCLI_OK);
    TF_EQ(sendl(n, "D"), NBCLI_OK);
    len = build("+DATA\r\nL ", 'c', NBCLI_MAXRESP - 2, ".\r\n.\r\n+OK z\r\n");
    srv_queue(g_big, len);
    strcpy(g_exp, "+DATA|");
    pieces(g_big + 7, NBCLI_MAXRESP + 1);
    strcat(g_exp, ".|");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, g_exp);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK z|");
    /* A long first line that starts "+DATA" is a single line. */
    TF_EQ(sendl(n, "E"), NBCLI_OK);
    TF_EQ(sendl(n, "F"), NBCLI_OK);
    len = build("+DATA", 'd', 600, "\r\n+OK y\r\n");
    srv_queue(g_big, len);
    g_exp[0] = '\0';
    pieces(g_big, 605);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, g_exp);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK y|");
    /* A line over two messages, split as long as they can be. */
    TF_EQ(sendl(n, "G"), NBCLI_OK);
    len = build("+OK ", 'e', 2996, "\r\n");
    srv_queue(g_big, NB_MSGMAX);
    srv_queue(g_big + NB_MSGMAX, (u16)(len - NB_MSGMAX));
    g_exp[0] = '\0';
    pieces(g_big, (u16)(len - 2));
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_EQ(g_lines, 6);
    TF_STREQ(g_ln, g_exp);
    /* An over-long request goes out whole, in as few messages as it
       takes. */
    clear();
    len = build("", 'f', BIGMAX - 202, "");
    TF_EQ(nbcli_send(n, g_big, len), NBCLI_OK);
    srv_run();
    TF_EQ(g_nrx, len + 2);
    TF_EQ(g_nmsg, (len + 2 + NB_MSGMAX - 1) / NB_MSGMAX);
    TF_CHECK(memcmp(g_rx, g_big, len) == 0);
    TF_CHECK(memcmp(g_rx + len, "\r\n", 2) == 0);
    clear();
    TF_EQ(nbcli_send(n, g_big, NB_MSGMAX - 2), NBCLI_OK);
    srv_run();
    TF_EQ(g_nrx, NB_MSGMAX);
    TF_EQ(g_nmsg, 1);
    clear();
    TF_EQ(nbcli_send(n, g_big, NB_MSGMAX - 1), NBCLI_OK);
    srv_run();
    TF_EQ(g_nrx, NB_MSGMAX + 1);
    TF_EQ(g_nmsg, 2);
    TF_CHECK(memcmp(g_rx + NB_MSGMAX - 1, "\r\n", 2) == 0);
    srv_text("-ERR 103 a\r\n-ERR 103 b\r\n-ERR 103 c\r\n");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "-ERR 103 a|");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "-ERR 103 c|");
    nbcli_close(n);
    finish();
}

/* Blank lines are sent but get no response unless too long. */
static void test_blank(void)
{
    nbcli *n;
    u16 len;

    fresh();
    n = connect("cli");
    clear();
    TF_EQ(sendl(n, ""), NBCLI_OK);
    TF_EQ(sendl(n, " \t "), NBCLI_OK);
    len = build("\t", ' ', NBCLI_MAXLINE - 2, "\t");
    TF_EQ(nbcli_send(n, g_big, len), NBCLI_OK);
    srv_run();
    TF_EQ(g_nmsg, 3);
    TF_EQ(g_nrx, 2 + 5 + NBCLI_MAXLINE + 2);
    TF_CHECK(memcmp(g_rx, "\r\n \t \r\n\t ", 9) == 0);
    srv_at(0, 0, NULL);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_EQ(g_lines, 0);
    TF_EQ(g_pumps, 0);
    /* One more blank is a request (error 103); a blank between two
       requests changes nothing. */
    len = build("", ' ', NBCLI_MAXLINE + 1, "");
    TF_EQ(nbcli_send(n, g_big, len), NBCLI_OK);
    len = build("", '\t', NBCLI_MAXLINE + 1, "");
    TF_EQ(nbcli_send(n, g_big, len), NBCLI_OK);
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    TF_EQ(sendl(n, ""), NBCLI_OK);
    TF_EQ(sendl(n, " x "), NBCLI_OK);
    srv_text("-ERR 103 long\r\n-ERR 103 tabs\r\n+OK a\r\n-ERR 101 x\r\n");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "-ERR 103 long|");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "-ERR 103 tabs|");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK a|");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "-ERR 101 x|");
    srv_at(0, 0, NULL);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_EQ(g_lines, 0);
    TF_EQ(g_pumps, 0);
    nbcli_close(n);
    finish();
}

/* A line callback that fails: the response is read to its end. */
static void test_output(void)
{
    nbcli *n;
    u16 len;

    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    TF_EQ(sendl(n, "B"), NBCLI_OK);
    srv_text("+DATA\r\nK 1\r\nK 2\r\nK 3\r\n.\r\n+OK b\r\n");
    TF_EQ(resp_fail(n, 2), NBCLI_EOUTPUT);
    TF_EQ(g_lines, 2);
    TF_STREQ(g_ln, "+DATA|K 1|");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK b|");
    /* The rest of the response comes later. */
    TF_EQ(sendl(n, "C"), NBCLI_OK);
    TF_EQ(sendl(n, "D"), NBCLI_OK);
    srv_text("+DATA\r\nK 1");
    srv_at(0, 0, NULL);
    srv_at(3, EV_SEND, "\r\nK 2\r");
    srv_at(6, EV_SEND, "\n.\r\n+OK d\r\n");
    TF_EQ(resp_fail(n, 1), NBCLI_EOUTPUT);
    TF_EQ(g_lines, 1);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK d|");
    /* A single line, and the first piece of a long one. */
    TF_EQ(sendl(n, "E"), NBCLI_OK);
    TF_EQ(sendl(n, "F"), NBCLI_OK);
    TF_EQ(sendl(n, "G"), NBCLI_OK);
    len = build("+OK e\r\n+OK ", 'f', 1096, "\r\n+OK g\r\n");
    srv_queue(g_big, len);
    TF_EQ(resp_fail(n, 1), NBCLI_EOUTPUT);
    TF_STREQ(g_ln, "+OK e|");
    TF_EQ(resp_fail(n, 1), NBCLI_EOUTPUT);
    TF_EQ(g_lines, 1);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK g|");
    TF_EQ(nbcli_closed(n), 0);
    nbcli_close(n);
    finish();
}

/* The waits of RECEIVE and SEND, each command's own.  A wait that runs
   out cancels the command and hangs up; with the cancel stalled (stall
   1) the hang-up still ends the command, so the close keeps nothing. */
static void test_wait(void)
{
    nbcli *n;
    int stall;

    for (stall = 0; stall <= 1; stall++) {
        /* No response: the RECEIVE is cancelled, the session hung up. */
        fresh();
        n = connect("cli");
        TF_EQ(sendl(n, "A"), NBCLI_OK);
        if (stall)
            fnb_stall(ALLSTALL);
        srv_at(0, 0, NULL);
        TF_EQ(resp(n, W), NBCLI_ELOST);
#ifdef COMPAT_HOST
        TF_CHECK(g_pumps >= TICKS_MIN(W) && g_pumps <= TICKS_MAX(W));
#endif
        TF_EQ(nbcli_closed(n), 1);
        TF_EQ(nb_hangup(g_clsn), NB_RC_BADLSN);
        if (!stall) {
            srv_run();
            TF_EQ(g_rxend, NB_RC_SESSCLOSED);
            /* An ended session neither sends nor reads. */
            clear();
            srv_at(0, 0, NULL);
            TF_EQ(sendl(n, "B"), NBCLI_ELOST);
            TF_EQ(resp(n, WAIT), NBCLI_ELOST);
            TF_EQ(g_pumps, 0);
            srv_run();
            TF_EQ(g_nrx, 0);
        }
        nbcli_close(n);
        TF_EQ(nba_stuck(), 0);
        finish();
#ifdef COMPAT_HOST
        /* No SEND taken: cancelled, which aborts the session (the
           server's own SEND ends so), and hung up.  The first message
           fills the client's RECEIVE, the second waits. */
        fresh();
        n = connect("cli");
        srv_recv(0);
        srv_text("+OK w\r\n");
        srv_text("+OK x\r\n");
        if (stall)
            fnb_stall(ALLSTALL);
        srv_at(0, 0, NULL);
        TF_EQ(sendl(n, "A"), NBCLI_ELOST);
        TF_CHECK(g_pumps >= TICKS_MIN(NBCLI_SENDWAIT) &&
                 g_pumps <= TICKS_MAX(NBCLI_SENDWAIT));
        TF_EQ(srv_sendrc(), NB_RC_ABORTED);
        TF_EQ(nbcli_closed(n), 1);
        TF_EQ(fnb_badhangups(), 0);
        TF_EQ(nb_hangup(g_clsn), NB_RC_BADLSN);
        nbcli_close(n);
        TF_EQ(nba_stuck(), 0);
        finish();
#endif
    }
#ifdef COMPAT_HOST
    /* No SEND taken, with the RECEIVE posted: the cancel of the SEND
       aborts the session, the RECEIVE says so, and it is not hung
       up. */
    fresh();
    n = connect("cli");
    srv_recv(0);
    srv_at(0, 0, NULL);
    TF_EQ(sendl(n, "A"), NBCLI_ELOST);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    TF_EQ(nba_stuck(), 0);
    finish();
    /* A RECEIVE that ended with the session just as its wait ran out,
       its end not yet shown: the cancel finds it ended, so the session
       is not hung up. */
    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    fnb_latency(HIDDEN);
    srv_hangup();
    fnb_latency(0);
    TF_EQ(resp(n, W), NBCLI_ELOST);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    finish();
    /* Each message has its own wait. */
    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    srv_at(0, 0, NULL);
    srv_at(TICKS_MIN(W) * 3 / 4, EV_SEND, "+DATA\r\n");
    srv_at(TICKS_MIN(W) * 6 / 4, EV_SEND, "K 1\r\n");
    srv_at(TICKS_MIN(W) * 9 / 4, EV_SEND, ".\r\n");
    TF_EQ(resp(n, W), NBCLI_OK);
    TF_STREQ(g_ln, "+DATA|K 1|.|");
    /* A SEND taken late still carries the request whole. */
    clear();
    srv_recv(0);
    srv_at(0, 0, NULL);
    srv_at(TICKS_MIN(NBCLI_SENDWAIT) * 3 / 4, EV_RECV, NULL);
    TF_EQ(sendl(n, "B"), NBCLI_OK);
    srv_run();
    TF_STREQ(g_rx, "B\r\n");
    srv_text("+OK b\r\n");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK b|");
    nbcli_close(n);
    finish();
#endif
}

/* A RECEIVE (send 0) or SEND whose cancel the provider does not
   confirm, not even when the session ends: the close keeps the
   session's block, its send copy wiped, until the provider lets the
   command end and nba_stuck sees it; the test then frees the block,
   which the client has let go for the rest of the run. */
static void stuck(int send)
{
    nbcli *n;
    cmem_blk *b;
    u16 off;
    u16 len;
    int kept;
#ifdef COMPAT_HOST
    u16 h;
#endif

    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    srv_recv(0);
    fnb_stall(ALLSTALL);
    fnb_hold(1);
    if (send) {
        TF_EQ(sendl(n, "SECRET"), NBCLI_ELOST);
        TF_CHECK(wiped(8));
    } else {
        TF_EQ(resp(n, W), NBCLI_ELOST);
    }
    TF_EQ(nbcli_closed(n), 1);
    /* The client's last SEND was the last posted: its block is the
       session's. */
    b = fnb_lastbuf(1, &off, &len);
#ifdef COMPAT_HOST
    h = tm_held();
#endif
    nbcli_close(n);
    kept = 1;
#ifdef COMPAT_HOST
    kept = TM_HELD(h - 1);
#endif
    TF_EQ(nba_stuck(), 1);
    fnb_stall(0);
    TF_EQ(nba_stuck(), 0);
    if (kept)
        cmem_free(b);
    finish();
}

/* Commands stuck at the close; the SEND's wait, NBCLI_SENDWAIT, only on
   the host's virtual clock. */
static void test_stuck(void)
{
    stuck(0);
#ifdef COMPAT_HOST
    stuck(1);
#endif
}

/* Sessions the server ends: no hang-up follows, and nothing more is
   sent or read. */
static void test_ended(void)
{
    nbcli *n;
    u16 ev;

    for (ev = EV_HANGUP; ev <= EV_ABORT; ev++) {
        /* In a response, the lines already delivered staying so. */
        fresh();
        n = connect("cli");
        TF_EQ(sendl(n, "A"), NBCLI_OK);
        srv_text("+DATA\r\nK A=1\r\n");
        srv_at(0, 0, NULL);
        srv_at(3, (u8)ev, NULL);
        TF_EQ(resp(n, WAIT), NBCLI_ELOST);
        TF_STREQ(g_ln, "+DATA|K A=1|");
        TF_EQ(nbcli_closed(n), 1);
        nbcli_close(n);
        TF_EQ(fnb_badhangups(), 0);
        finish();
        /* While a SEND waits. */
        fresh();
        n = connect("cli");
        srv_recv(0);
        srv_at(0, 0, NULL);
        srv_at(3, (u8)ev, NULL);
        TF_EQ(sendl(n, "A"), NBCLI_ELOST);
        TF_EQ(nbcli_closed(n), 1);
        nbcli_close(n);
        TF_EQ(fnb_badhangups(), 0);
        finish();
    }
    /* Bytes kept from before the end are not read after it. */
    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    TF_EQ(sendl(n, "B"), NBCLI_OK);
    srv_text("+OK 1\r\n+OK 2\r\n");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK 1|");
    srv_hangup();
    TF_EQ(nbcli_closed(n), 0);
    TF_EQ(sendl(n, "C"), NBCLI_ELOST);
    TF_EQ(nbcli_closed(n), 1);
    srv_at(0, 0, NULL);
    TF_EQ(resp(n, WAIT), NBCLI_ELOST);
    TF_EQ(g_lines, 0);
    TF_EQ(sendl(n, "D"), NBCLI_ELOST);
    TF_EQ(g_pumps, 0);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    finish();
    /* The response to BYE, read before the server hangs up: the session
       is not known to have ended, and the close, which finds the
       RECEIVE posted after the read ended by the hang-up, does not hang
       it up. */
    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "BYE"), NBCLI_OK);
    srv_text("+OK bye\r\n");
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK bye|");
    srv_hangup();
    TF_EQ(nbcli_closed(n), 0);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    TF_EQ(nb_hangup(g_clsn), NB_RC_BADLSN);
    finish();
    /* A session hung up behind the client's back: the provider refuses
       the next post for its LSN, and the client does not hang that LSN
       up. */
    fresh();
    n = connect("cli");
    TF_EQ(nb_hangup(g_clsn), NB_OK);
    TF_EQ(sendl(n, "A"), NBCLI_ELOST);
    TF_EQ(nbcli_closed(n), 1);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    finish();
    /* The same with no RECEIVE posted to report it: the refused post
       alone says so. */
    fresh();
    fnb_toomany(1);
    n = connect("cli");
    TF_EQ(nb_hangup(g_clsn), NB_OK);
    TF_EQ(sendl(n, "A"), NBCLI_ELOST);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    finish();
    /* A response waiting unread in the RECEIVE when the server hangs
       up: the next SEND's code says the session has ended, whatever
       the RECEIVE's. */
    fresh();
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    srv_text("+OK a\r\n");
    srv_hangup();
    TF_EQ(sendl(n, "B"), NBCLI_ELOST);
    nbcli_close(n);
    TF_EQ(fnb_badhangups(), 0);
    finish();
}

/* Posts refused for too many commands in progress.  A RECEIVE refused
   at the open or after a read is posted again before the next SEND, or
   by the wait for the next message; a post that waits is made again
   until one is taken, or until the wait runs out.  The provider frees
   no NCB while a client only posts, so the limit stays. */
static void test_toomany(void)
{
    nbcli *n;
    u32 t;

    /* The RECEIVE refused at the open, posted before the SEND, so not
       late, and refused again after the read. */
    fresh();
    fnb_toomany(1);
    n = connect("cli");
    TF_EQ(sendl(n, "A"), NBCLI_OK);
    srv_run();
    TF_STREQ(g_rx, "A\r\n");
    srv_text("+OK a\r\n");
    fnb_toomany(1);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK a|");
    TF_EQ(fnb_late(), 0);
    /* The RECEIVE refused before the SEND too, then no room for it for
       the whole wait. */
    clear();
    fnb_toomany(1);
    TF_EQ(sendl(n, "C"), NBCLI_OK);
    srv_text("+OK c\r\n");
    fnb_ncbmax(2);
    t = tmr_ticks();
    TF_EQ(resp(n, W), NBCLI_ELOST);
    t = tmr_since(t);
    TF_CHECK(t >= TICKS_MIN(W));
#ifdef COMPAT_HOST
    TF_CHECK(t <= TICKS_MAX(W));
#endif
    TF_EQ(g_lines, 0);
    TF_EQ(nbcli_closed(n), 1);
    fnb_ncbmax(0);
    TF_EQ(nb_hangup(g_clsn), NB_RC_BADLSN);
    nbcli_close(n);
    finish();
    /* The RECEIVE refused at the open and before the SEND, the SEND
       twice, the RECEIVE three times in the wait. */
    fresh();
    fnb_toomany(1);
    n = connect("cli");
    fnb_toomany(3);
    TF_EQ(sendl(n, "B"), NBCLI_OK);
    srv_run();
    TF_STREQ(g_rx, "B\r\n");
    srv_text("+OK b\r\n");
    fnb_toomany(3);
    TF_EQ(resp(n, WAIT), NBCLI_OK);
    TF_STREQ(g_ln, "+OK b|");
    TF_EQ(fnb_late(), 1);
    nbcli_close(n);
    finish();
#ifdef COMPAT_HOST
    fresh();
    n = connect("cli");
    fnb_ncbmax(1);
    t = tmr_ticks();
    TF_EQ(sendl(n, "A"), NBCLI_ELOST);
    t = tmr_since(t);
    TF_CHECK(t >= TICKS_MIN(NBCLI_SENDWAIT) &&
             t <= TICKS_MAX(NBCLI_SENDWAIT));
    fnb_ncbmax(0);
    TF_EQ(nb_hangup(g_clsn), NB_RC_BADLSN);
    srv_run();
    TF_EQ(g_nrx, 0);
    nbcli_close(n);
    finish();
#endif
}

void test_nbcl2(void)
{
    test_request();
    test_frame();
    test_long();
    test_blank();
    test_output();
    test_wait();
    test_ended();
    test_stuck();
    test_toomany();
}

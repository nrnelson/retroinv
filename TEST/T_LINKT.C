/*
 * T_LINKT.C - unit test for the TCP backend of the client's link
 * (LINK.C over LINKTCP.C) on a simulated connection, the sk_ calls of
 * COMPAT.H defined here.
 *
 * usage: T_LINKT
 *
 * The simulated peer answers each request line it receives with the
 * next reply of the test's table, or, at a NULL reply, falls silent,
 * and hands the bytes over in pieces of the test's size; once they are
 * out, sk_recv gives 0, as CWSOCK.C does for a connection the peer has
 * closed and for a wait that has run out.  Checks:
 * - LINK_TCPPORT is the bridge's BR_PORT (at compile time);
 * - the open passes the address, the port (LINK_TCPPORT for 0) and
 *   LINK_NETWAIT seconds to sk_open; each failure of sk_open is its
 *   LINK_E code with *out NULL, the Winsock error passed on and no
 *   block left; a session or link the heap refuses is LINK_ENOMEM, the
 *   connection then closed and no block left;
 * - a request goes out as its line and CR LF in one sk_send, and the
 *   bytes it was sent from hold no password once link_send has
 *   returned, nor do the link's own; a line of LINK_MAXLINE bytes is
 *   sent whole, a longer one is answered with 103 unsent;
 * - responses, one line or +DATA, are delivered whole in any pieces,
 *   one byte at a time too, the bytes after one kept for the next;
 * - +OK to BYE, and the LINK_MAXFAIL-th failed LOGIN, whose answer
 *   comes with the peer's close, end the session (LINK_CLOSED): the
 *   connection is closed at once and later requests are not sent;
 * - a peer that falls silent or closes early, or a send that fails,
 *   gives LINK_ELOST, the connection closed, then LINK_ELOST at once;
 * - link_close closes a live connection once, and none an ended one,
 *   wiping the session before it is freed;
 * - link_netname is NULL.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_SOCKET
#define LINK_TCP
#define TF_MALLOC
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "LINK.H"
#include "BRIDGE.H"

#define PW              "SECRET1"       /* the password requests carry  */
#define TXMAX           1024    /* request bytes the peer keeps         */
#define RXMAX           1024    /* reply bytes queued for the link      */
#define OUTMAX          1024    /* response text a test keeps           */

extern char t_linkt_port_ok[1];
extern char t_linkt_port_ok[(LINK_TCPPORT == BR_PORT) ? 1 : 2];

struct sk {
    int open;                   /* 1 between sk_open and sk_close       */
};

static sk g_k;
static int g_openrc;            /* what sk_open returns                 */
static u16 g_openerr;           /* and gives in *err                    */
static u8 g_ip[4];              /* what sk_open was given               */
static u16 g_port;
static u16 g_secs;
static int g_opens;
static int g_closes;
static int g_sendrc;            /* what sk_send returns                 */
static int g_sends;
static const char *g_sentfrom;  /* the bytes the last sk_send was given */
static u16 g_sentlen;
static char g_tx[TXMAX];        /* every byte sent                      */
static u16 g_txn;
static char g_rx[RXMAX];        /* bytes queued for the link            */
static u16 g_rxn;
static u16 g_rxpos;
static u16 g_piece;             /* the most sk_recv hands over at once  */
static const char *const *g_rep;    /* replies, NULL: silence           */
static u16 g_nreq;
static int g_silent;            /* a NULL reply came: no more replies   */
static char g_out[OUTMAX];      /* response lines, each ended by LF     */
static u16 g_on;
static const char *g_lp;        /* the last line take was handed, and   */
static u16 g_ll;                /*   its length                         */

int sk_open(sk **out, const u8 *ip, u16 port, u16 secs, u16 *err)
{
    g_opens++;
    memcpy(g_ip, ip, sizeof g_ip);
    g_port = port;
    g_secs = secs;
    *err = g_openerr;
    *out = NULL;
    if (g_openrc != SK_OK)
        return g_openrc;
    TF_CHECK(!g_k.open);
    g_k.open = 1;
    *out = &g_k;
    return SK_OK;
}

/* Queue reply text, its LF line ends sent as CR LF. */
static void queue(const char *text)
{
    for (; *text != '\0'; text++) {
        if (!TF_CHECK(g_rxn + 2 <= RXMAX))
            return;
        if (*text == '\n')
            g_rx[g_rxn++] = '\r';
        g_rx[g_rxn++] = *text;
    }
}

int sk_send(sk *k, const char *buf, u16 n, u16 secs)
{
    u16 i;

    TF_CHECK(k == &g_k && k->open);
    TF_EQ(secs, LINK_NETWAIT);
    g_sends++;
    g_sentfrom = buf;
    g_sentlen = n;
    if (g_sendrc != SK_OK)
        return g_sendrc;
    for (i = 0; i < n; i++) {
        if (TF_CHECK(g_txn < TXMAX))
            g_tx[g_txn++] = buf[i];
        if (buf[i] != '\n' || g_silent)
            continue;
        if (g_rep[g_nreq] == NULL)
            g_silent = 1;
        else
            queue(g_rep[g_nreq++]);
    }
    return SK_OK;
}

u16 sk_recv(sk *k, char *buf, u16 room, u16 secs)
{
    u16 n;

    TF_CHECK(k == &g_k && k->open);
    TF_CHECK(room > 0);
    TF_EQ(secs, LINK_NETWAIT);
    n = (u16)(g_rxn - g_rxpos);
    if (n > room)
        n = room;
    if (n > g_piece)
        n = g_piece;
    memcpy(buf, g_rx + g_rxpos, n);
    g_rxpos = (u16)(g_rxpos + n);
    /* Without bytes: the wait ran out, or the peer closed. */
    return n;
}

/* Check that the bytes the last sk_send was given are zero by now. */
static void sent_wiped(void)
{
    u16 i;

    for (i = 0; g_sentfrom != NULL && i < g_sentlen; i++)
        if (!TF_EQ(g_sentfrom[i], 0))
            break;
}

/* The session frees the bytes sent from once the connection closes, so
   they are checked here and forgotten. */
void sk_close(sk *k)
{
    if (k == NULL)
        return;
    TF_CHECK(k == &g_k && k->open);
    sent_wiped();
    g_sentfrom = NULL;
    k->open = 0;
    g_closes++;
}

/* Set the peer up with the replies rep, handed over piece bytes at a
   time; sk_open is to return rc with err. */
static void peer(const char *const *rep, u16 piece, int rc, u16 err)
{
    g_openrc = rc;
    g_openerr = err;
    memset(g_ip, 0, sizeof g_ip);
    g_port = 0;
    g_secs = 0;
    g_opens = 0;
    g_closes = 0;
    g_sendrc = SK_OK;
    g_sends = 0;
    g_sentfrom = NULL;
    g_txn = 0;
    g_rxn = 0;
    g_rxpos = 0;
    g_piece = piece;
    g_rep = rep;
    g_nreq = 0;
    g_silent = 0;
    g_on = 0;
    g_out[0] = '\0';
}

/* Open the link to 10.0.2.15 at the default port. */
static link *start(void)
{
    static const u8 ip[4] = { 10, 0, 2, 15 };
    link *l;
    u16 err;

    l = NULL;
    err = 1;
    TF_EQ(link_open_tcp(&l, ip, 0, &err), 0);
    TF_EQ(err, 0);
    TF_CHECK(l != NULL);
    return l;
}

/* link_line_fn: keep the line in g_out and note where it is. */
static int take(void *arg, const char *line, u16 len)
{
    g_lp = line;
    g_ll = len;
    TF_CHECK(arg == g_out);
    TF_EQ(strlen(line), len);
    if (TF_CHECK(g_on + len + 1 < OUTMAX)) {
        memcpy(g_out + g_on, line, len);
        g_on = (u16)(g_on + len);
        g_out[g_on++] = '\n';
    }
    g_out[g_on] = '\0';
    return 0;
}

/* link_send of the text line, the response kept in g_out afresh. */
static int send_line(link *l, const char *line)
{
    g_on = 0;
    g_out[0] = '\0';
    return link_send(l, line, (u16)strlen(line), take, g_out);
}

/* 1 when the n bytes at p hold s. */
static int holds(const void *p, u16 n, const char *s)
{
    const char *b;
    size_t m;
    u16 i;

    b = (const char *)p;
    m = strlen(s);
    for (i = 0; i + m <= n; i++)
        if (memcmp(b + i, s, m) == 0)
            return 1;
    return 0;
}

static void test_open(void)
{
    static const u8 ip[4] = { 192, 168, 1, 2 };
    static const int sks[] = {
        SK_ENOSTACK, SK_ENOMEM, SK_EREFUSED, SK_ECONN, SK_ECANCEL
    };
    static const int links[] = {
        LINK_ENOTCP, LINK_ENOMEM, LINK_EREFUSED, LINK_ECALL, LINK_ECANCEL
    };
    static const char *const none[] = { NULL };
    link *l;
    u16 err;
    int i;

    peer(none, 1, SK_OK, 0);
    l = start();
    TF_EQ(g_opens, 1);
    TF_EQ(g_port, LINK_TCPPORT);
    TF_EQ(g_secs, LINK_NETWAIT);
    TF_EQ(g_ip[0], 10);
    TF_EQ(g_ip[3], 15);
    TF_CHECK(link_netname(l) == NULL);
    TF_EQ(link_close(l), 0);
    TF_EQ(g_closes, 1);
    TF_EQ(g_sends, 0);
    TM_HELD(0);
    peer(none, 1, SK_OK, 0);
    TF_EQ(link_open_tcp(&l, ip, 7001, &err), 0);
    TF_EQ(g_port, 7001);
    TF_EQ(g_ip[0], 192);
    TF_EQ(g_ip[1], 168);
    TF_EQ(g_ip[2], 1);
    TF_EQ(g_ip[3], 2);
    TF_EQ(link_close(l), 0);
    for (i = 0; i < (int)(sizeof sks / sizeof sks[0]); i++) {
        peer(none, 1, sks[i], (u16)(10060 + i));
        l = (link *)g_out;      /* any address but NULL */
        err = 0;
        TF_EQ(link_open_tcp(&l, ip, 0, &err), links[i]);
        TF_CHECK(l == NULL);
        TF_EQ(err, 10060 + i);
        TF_EQ(g_closes, 0);
        TM_HELD(0);
    }
    /* No room for the session: no connection is tried. */
    peer(none, 1, SK_OK, 0);
    tm_fail(1);
    err = 1;
    TF_EQ(link_open_tcp(&l, ip, 0, &err), LINK_ENOMEM);
    tm_fail(0);
    TF_EQ(err, 0);
    TF_CHECK(l == NULL);
    TF_EQ(g_opens, 0);
    TM_HELD(0);
    /* No room for the link: the connection is closed again. */
    peer(none, 1, SK_OK, 0);
    tm_fail(2);
    TF_EQ(link_open_tcp(&l, ip, 0, &err), LINK_ENOMEM);
    tm_fail(0);
    TF_CHECK(l == NULL);
    TF_EQ(g_opens, 1);
    TF_EQ(g_closes, 1);
    TM_HELD(0);
}

static void test_request(void)
{
    static const char *const rep[] = {
        "+OK hello\n", "+OK ADMIN 3\n", "-ERR 103 line too long\n", NULL
    };
    char line[LINK_MAXLINE + 2];
    const char *resp;
    const void *p;
    u16 rlen;
    u16 n;
    link *l;

    peer(rep, RXMAX, SK_OK, 0);
    l = start();
    TF_EQ(send_line(l, "HELLO T"), LINK_OK);
    TF_STREQ(g_out, "+OK hello\n");
    TF_EQ(g_sends, 1);
    TF_EQ(g_txn, 9);
    TF_CHECK(memcmp(g_tx, "HELLO T\r\n", 9) == 0);
    TF_EQ(send_line(l, "LOGIN ADMIN " PW), LINK_OK);
    TF_STREQ(g_out, "+OK ADMIN 3\n");
    TF_EQ(g_sends, 2);
    TF_CHECK(holds(g_tx, g_txn, "LOGIN ADMIN " PW "\r\n"));
    /* What the line was sent from no longer holds it, nor does the
       link. */
    TF_EQ(g_sentlen, sizeof "LOGIN ADMIN " PW "\r\n" - 1);
    sent_wiped();
    p = link_bytes(l, &n);
    TF_CHECK(!holds(p, n, PW));
    /* A line of LINK_MAXLINE bytes is sent whole; one more is answered
       here. */
    memset(line, 'X', sizeof line);
    memcpy(line, "PING ", 5);
    g_txn = 0;
    TF_EQ(link_send(l, line, LINK_MAXLINE, take, g_out), LINK_OK);
    TF_EQ(g_sends, 3);
    TF_EQ(g_txn, LINK_MAXLINE + 2);
    TF_CHECK(memcmp(g_tx, line, LINK_MAXLINE) == 0);
    TF_CHECK(g_tx[LINK_MAXLINE] == '\r' && g_tx[LINK_MAXLINE + 1] == '\n');
    /* That answer was delivered from the session's own buffer. */
    resp = g_lp;
    rlen = g_ll;
    g_on = 0;
    TF_EQ(link_send(l, line, LINK_MAXLINE + 1, take, g_out), LINK_OK);
    TF_EQ(g_sends, 3);
    TF_EQ(strncmp(g_out, "-ERR 103 ", 9), 0);
    /* An empty line is not sent. */
    TF_EQ(send_line(l, " \t "), LINK_OK);
    TF_EQ(g_sends, 3);
    TF_EQ(g_on, 0);
    /* The session is wiped at the close, its response buffer too. */
    tm_expect_wiped(resp, rlen);
    TF_EQ(link_close(l), 0);
    TF_CHECK(tm_wiped());
    TF_EQ(g_closes, 1);
    TM_HELD(0);
}

static void test_pieces(void)
{
    static const char *const rep[] = {
        "+DATA\nK ID=1\n..dotted\n.\n",
        "+OK one\n",
        "+OK two\n+OK three\n",
        "",
        NULL
    };
    static const u16 pieces[] = { 1, 2, 7, RXMAX };
    link *l;
    int i;

    for (i = 0; i < (int)(sizeof pieces / sizeof pieces[0]); i++) {
        peer(rep, pieces[i], SK_OK, 0);
        l = start();
        TF_EQ(send_line(l, "ITEM LIST"), LINK_OK);
        TF_STREQ(g_out, "+DATA\nK ID=1\n..dotted\n.\n");
        TF_EQ(send_line(l, "PING"), LINK_OK);
        TF_STREQ(g_out, "+OK one\n");
        TF_EQ(send_line(l, "PING"), LINK_OK);
        TF_STREQ(g_out, "+OK two\n");
        /* The second reply came early and was kept. */
        TF_EQ(send_line(l, "PING"), LINK_OK);
        TF_STREQ(g_out, "+OK three\n");
        TF_EQ(link_close(l), 0);
        TF_EQ(g_closes, 1);
        TM_HELD(0);
    }
}

static void test_end(void)
{
    static const char *const bye[] = { "-ERR 102 nope\n", "+OK\n", NULL };
    static const char *const fails[] = {
        "-ERR 401 bad\n", "-ERR 401 bad\n", "-ERR 401 bad\n", NULL
    };
    link *l;

    peer(bye, 3, SK_OK, 0);
    l = start();
    TF_EQ(send_line(l, "BYE"), LINK_OK);
    TF_EQ(g_closes, 0);
    TF_EQ(send_line(l, "bye"), LINK_CLOSED);
    TF_EQ(g_closes, 1);
    TF_EQ(send_line(l, "PING"), LINK_CLOSED);
    TF_EQ(g_sends, 2);
    TF_EQ(link_close(l), 0);
    TF_EQ(g_closes, 1);
    TM_HELD(0);
    /* The last failure's answer comes with the peer's close. */
    peer(fails, 5, SK_OK, 0);
    l = start();
    TF_EQ(send_line(l, "LOGIN A x1"), LINK_OK);
    TF_EQ(send_line(l, "LOGIN A x2"), LINK_OK);
    TF_EQ(send_line(l, "LOGIN A x3"), LINK_CLOSED);
    TF_STREQ(g_out, "-ERR 401 bad\n");
    TF_EQ(g_closes, 1);
    TF_EQ(link_close(l), 0);
    TF_EQ(g_closes, 1);
    TM_HELD(0);
}

static void test_lost(void)
{
    static const char *const silent[] = { "+OK\n", NULL };
    static const char *const cut[] = { "+DATA\nK ID=1\n", "", NULL };
    link *l;

    /* No answer: the wait runs out. */
    peer(silent, RXMAX, SK_OK, 0);
    l = start();
    TF_EQ(send_line(l, "PING"), LINK_OK);
    TF_EQ(send_line(l, "PING"), LINK_ELOST);
    TF_EQ(g_closes, 1);
    TF_EQ(send_line(l, "PING"), LINK_ELOST);
    TF_EQ(g_sends, 2);
    TF_EQ(link_close(l), 0);
    TF_EQ(g_closes, 1);
    TM_HELD(0);
    /* The peer closes within a response, its lines delivered. */
    peer(cut, 4, SK_OK, 0);
    l = start();
    TF_EQ(send_line(l, "ITEM LIST"), LINK_ELOST);
    TF_STREQ(g_out, "+DATA\nK ID=1\n");
    TF_EQ(g_closes, 1);
    TF_EQ(link_close(l), 0);
    TM_HELD(0);
    /* A send that fails; its bytes are wiped all the same, as sk_close
       checks. */
    peer(silent, RXMAX, SK_OK, 0);
    l = start();
    g_sendrc = SK_ELOST;
    TF_EQ(send_line(l, "LOGIN ADMIN " PW), LINK_ELOST);
    TF_EQ(g_closes, 1);
    TF_EQ(send_line(l, "PING"), LINK_ELOST);
    TF_EQ(g_sends, 1);
    TF_EQ(link_close(l), 0);
    TF_EQ(g_closes, 1);
    TM_HELD(0);
}

int main(void)
{
    tf_begin("T_LINKT");
    test_open();
    test_request();
    test_pieces();
    test_end();
    test_lost();
    return tf_done();
}

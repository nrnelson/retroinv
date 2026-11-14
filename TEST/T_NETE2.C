/*
 * T_NETE2.C - the second part of the T_NETE test (T_NETE.H): two links
 * at once and the ends of a session, which main of T_NETE.C runs.
 *
 * Checks:
 * - two links open at once, their requests interleaved, one line fed in
 *   two pieces around a request of the other: a login on one leaves the
 *   other logged out; each session has its own login, station and level
 *   (WHOAMI), the second one's station named READONLY= so that it logs
 *   in at level 1 and is refused a change and ADMIN CLIENTS (203);
 *   IMPORT is refused with 402 while both are open; ADMIN CLIENTS lists
 *   both, SELF set for the requester only.  The links call from names
 *   of their own, distinct, unless DOS has a machine name (DOSBox-X):
 *   both then call from it (NBCLI.H), the sessions share that station,
 *   READONLY= stays empty, and the second session, at level 3, is
 *   listed by its own ADMIN CLIENTS with SELF set on its row instead;
 * - BYE, and the LINK_MAXFAIL-th failed LOGIN, end the session on both
 *   sides: LINK_CLOSED, later requests neither sent nor answered, the
 *   server's slot freed, both ends of the session gone;
 * - ADMIN SHUTDOWN: +OK to the requester, then -ERR 501 and LINK_CLOSED
 *   to the next request of each session; the server then done, with no
 *   session end, name, command or set left once stopped;
 * - after each LOGIN and PASSWD, no byte of the request is left in the
 *   copy the client sent it from or in the server's receive buffer, and
 *   CONN's event names the request without its arguments.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "LINK.H"
#define TF_PART
#define TF_FILE "T_NETE2"
#include "TESTFW.H"
#include "FAKENB.H"
#include "TSCRIPT.H"
#include "T_NETE.H"

#define WANTSZ          192     /* bytes of an expected response        */
#define ZEROSZ          64      /* bytes zero reads at a time           */

static const char k_ok[] = "+OK\r\n";
static const char k_e203[] = "-ERR 203 ";
static const char k_e401[] = "-ERR 401 ";
static const char k_e501[] = "-ERR 501 ";
static const char k_name[] = " NAME=\"System Administrator\"\r\n";

static char g_ro[NB_NAMELEN];   /* the server's READONLY= list          */

/* Slots open or closing. */
static u16 in_use(void)
{
    u16 n;
    u8 c;
    u8 s;

    n = 0;
    for (c = 0; c < NSLOTS; c++) {
        s = conn_state(c);
        if (s == CONN_OPEN || s == CONN_CLOSING)
            n++;
    }
    return n;
}

/* Run the server until n of its slots are open or closing. */
void srv_until(u16 n)
{
    u16 i;

    for (i = 0; i < LOOPS && in_use() != n; i++)
        srv_run();
    TF_EQ(in_use(), n);
}

/* 1 when the bytes of p are all 0. */
static int zero(const tn_buf *p)
{
    char buf[ZEROSZ];
    u16 at;
    u16 n;
    u16 i;

    for (at = 0; at < p->len; at = (u16)(at + n)) {
        n = (u16)(p->len - at < ZEROSZ ? p->len - at : ZEROSZ);
        cmem_get(p->b, (u16)(p->off + at), buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

/* No byte of the last request is left where it travelled: the copy the
   client sent it from, and as many bytes at the start of the buffer the
   server received it in, posted again since, are all 0.  Each is
   checked once, and only after a request answered as expected: a failed
   one may have closed the link and freed both blocks. */
void wiped(void)
{
    tn_buf rx;

    rx = g_srv;
    rx.len = g_cli.len;
    TF_CHECK(g_cli.b != NULL && zero(&g_cli));
    TF_CHECK(g_srv.b != NULL && g_srv.len == CONN_RXSZ &&
             rx.len <= CONN_RXSZ && zero(&rx));
    g_cli.b = NULL;
    g_srv.b = NULL;
}

/* The response to LOGIN of ADMIN at level, in want. */
static const char *logged(char *want, int level)
{
    sprintf(want, "+OK LEVEL=%d%s", level, k_name);
    return want;
}

/* The response to WHOAMI of ADMIN at level on station, in want. */
static const char *whoami(char *want, int level, const char *station)
{
    sprintf(want, "+DATA\r\nK USER=ADMIN\r\nK LEVEL=%d\r\nK STATION=%s\r\n"
            ".\r\n", level, station);
    return want;
}

/* Log ADMIN in on l from the fresh data directory: HELLO, LOGIN with
   the first password, PASSWD, LOGIN with the new one; no request left
   behind after LOGIN or PASSWD. */
static void admin(link *l)
{
    char want[WANTSZ];

    TF_STREQ(ask(l, "HELLO X", LINK_OK), k_ok);
    if (TF_STREQ(ask(l, "LOGIN ADMIN admin", LINK_OK),
                 "+OK LEVEL=3 MUSTCHG=1\r\n"))
        wiped();
    TF_STREQ(g_req, "LOGIN");
    if (TF_STREQ(ask(l, "PASSWD admin " TSC_PW, LINK_OK), k_ok))
        wiped();
    TF_STREQ(g_req, "PASSWD");
    if (TF_STREQ(ask(l, "LOGIN ADMIN " TSC_PW, LINK_OK), logged(want, 3)))
        wiped();
    TF_STREQ(g_req, "LOGIN");
}

/* ADMIN CLIENTS as asked on the session self (1 or 2) of the two, A at
   level 3 on station sa and B at level lb on station sb, in want. */
static const char *clients(char *want, int self, const char *sa, int lb,
                           const char *sb)
{
    sprintf(want, "+DATA\r\nF SLOT TYPE STATION USER LEVEL STATE IDLE SELF"
            "\r\nR 1 NETB %s ADMIN 3 3 0 %d\r\nR 2 NETB %s ADMIN %d 3 0 %d"
            "\r\n.\r\n", sa, self == 1, sb, lb, self == 2);
    return want;
}

/* Two sessions at once on a and b, B's at level lb: 1 when its station
   is capped, else 3.  Each keeps its own login, level and station. */
static void sessions(link *a, link *b, int lb)
{
    char want[WANTSZ];
    const char *sa;
    const char *sb;

    sa = link_netname(a);
    sb = link_netname(b);
    /* A logs in; B, in its own session, is still logged out. */
    admin(a);
    TF_STREQ(ask(b, "HELLO X", LINK_OK), k_ok);
    TF_CHECK(tf_starts(ask(b, "WHOAMI", LINK_OK), "-ERR 400 "));
    if (TF_STREQ(ask(b, "LOGIN ADMIN " TSC_PW, LINK_OK), logged(want, lb)))
        wiped();

    /* A's WHOAMI in two pieces around B's. */
    TF_STREQ(feed(a, "WHO", LINK_OK), "");
    TF_STREQ(ask(b, "WHOAMI", LINK_OK), whoami(want, lb, sb));
    TF_STREQ(feed(a, "AMI\r\n", LINK_OK), whoami(want, 3, sa));

    TF_CHECK(tf_starts(ask(a, "IMPORT ITEMS FILE=NONE.CSV", LINK_OK),
                       "-ERR 402 "));
    TF_STREQ(ask(a, "ADMIN CLIENTS", LINK_OK), clients(want, 1, sa, lb, sb));
    if (lb == 3) {
        TF_STREQ(ask(b, "ADMIN CLIENTS", LINK_OK),
                 clients(want, 2, sa, lb, sb));
    } else {
        TF_CHECK(tf_starts(ask(b, "ADMIN CLIENTS", LINK_OK), k_e203));
        TF_CHECK(tf_starts(ask(b, "ITEM ADD CATEGORY=SYS MFR=IBM", LINK_OK),
                           k_e203));
    }
}

/* BYE ends A's session on both sides, and so does B's last failed
   LOGIN, after its first ones. */
static void ends(link *a, link *b)
{
    u16 i;

    TF_STREQ(ask(a, "BYE", LINK_CLOSED), k_ok);
    srv_until(1);
    TF_EQ(conn_count(), 1);
    TF_EQ(g_closes, 1);
    TF_EQ(fnb_sessions(), 2);
    TF_STREQ(ask(a, "PING", LINK_CLOSED), "");
    for (i = 1; i < LINK_MAXFAIL; i++) {
        if (TF_CHECK(tf_starts(ask(b, "LOGIN ADMIN " BADPW, LINK_OK), k_e401)))
            wiped();
    }
    TF_CHECK(tf_starts(ask(b, "LOGIN ADMIN " BADPW, LINK_CLOSED), k_e401));
    srv_until(0);
    TF_EQ(conn_count(), 0);
    TF_EQ(g_closes, 2);
    TF_EQ(fnb_sessions(), 0);
    TF_STREQ(ask(b, "PING", LINK_CLOSED), "");
}

void test_two(void)
{
    char mach[DOS_MACHLEN + 1];
    link *a;
    link *b;
    int named;

    g_ro[0] = '\0';
    if (!srv_begin(g_ro))
        return;
    a = cli_open();
    b = cli_open();
    if (!TF_CHECK(a != NULL && b != NULL))
        return;
    named = dos_machine_name(mach) == 0;
    TF_EQ(strcmp(link_netname(a), link_netname(b)) == 0, named);
    if (!named)
        strcpy(g_ro, link_netname(b));
    sessions(a, b, named ? 3 : 1);
    ends(a, b);
    TF_EQ(link_close(a), 0);
    TF_EQ(link_close(b), 0);
    srv_end();
}

void test_shutdown(void)
{
    link *c;
    link *d;
    u16 i;

    if (!srv_begin(""))
        return;
    c = cli_open();
    d = cli_open();
    if (!TF_CHECK(c != NULL && d != NULL))
        return;
    admin(c);
    TF_STREQ(ask(d, "HELLO X", LINK_OK), k_ok);
    TF_STREQ(ask(c, "ADMIN SHUTDOWN", LINK_OK), k_ok);
    TF_EQ(conn_stopping(), 1);
    TF_CHECK(tf_starts(ask(d, "PING", LINK_CLOSED), k_e501));
    TF_CHECK(tf_starts(ask(c, "PING", LINK_CLOSED), k_e501));
    for (i = 0; i < LOOPS && !conn_done(); i++)
        srv_run();
    TF_EQ(conn_done(), 1);
    TF_EQ(g_closes, 2);
    TF_EQ(fnb_sessions(), 0);
    TF_STREQ(ask(d, "PING", LINK_CLOSED), "");
    TF_EQ(link_close(c), 0);
    TF_EQ(link_close(d), 0);
    srv_end();
}

/*
 * T_CONN2.C - the second half of the T_CONN test (see T_CONN.C):
 * input (pipelined lines in one input answered one per pass, the input
 * handed back only once all of it is read, a line and an over-long
 * line split over several inputs), fairness (one request of every slot
 * with work per pass, in slot order from one slot further each pass, a
 * slot whose output is still being sent passed over), the host's
 * STATUS lines, the count of other sessions that IMPORT checks, the
 * read-only cap, conn_info's rows, paging in the room of the output
 * block, idle time, shutdown, and what a slot drops when it ends.  It
 * is a file of its own only to stay within the compiler memory of
 * Watcom C 6.5; T_CONN.H holds what both share.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#define TF_PART
#define TF_FILE "T_CONN2"
#include "TESTFW.H"
#include "T_CONN.H"

#define BIGSPAN         0x70000000UL    /* ticks whose tenfold overflows */

static const char k_import[] = "IMPORT ITEMS FILE=NOFILE.CSV";
static const char k_status[] = "STATUS";
static const char k_e402[] = "-ERR 402 ";
static const char k_e501[] = "-ERR 501 ";
static const char k_ses1[] = "\r\nK SESSIONS=1\r\n";

/* conn_info's row of slot c into *p: 1, or 0 when c is not open. */
static int info_of(u8 c, proto_peer *p)
{
    u16 i;

    for (i = 0; conn_info(i, p) == 0; i++)
        if (p->slot == c + 1)
            return 1;
    return 0;
}

/* Whole seconds in t ticks, as CONN.H says CONN computes them. */
static u32 secs(u32 t)
{
    return t / TMR_10SEC * 10 + t % TMR_10SEC * 10 / TMR_10SEC;
}

/* ---------------------------------------------------------------------
 * Input.
 * --------------------------------------------------------------------- */

void test_input(void)
{
    char line[LONGLINE];
    u8 c;

    start(NULL, NS, 0);
    c = open_slot("STA1");
    login(c, "STA1");
    /* Three lines in one input: one answered per pass, none while the
       output of the last is waiting, the input given back at the end. */
    g_rxdone[c] = 0;
    put_text(c, "PING\r\nVER\r\nWHOAMI\r\n");
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(conn_service(g_now), 0);
    drain(c, 0);
    TF_STREQ(g_out, k_ok);
    TF_EQ(g_rxdone[c], 0);
    TF_EQ(conn_service(g_now), 1);
    drain(c, 0);
    TF_CHECK(tf_starts(g_out, "+OK RETROINV "));
    TF_EQ(conn_service(g_now), 1);
    drain(c, 0);
    TF_CHECK(tf_starts(g_out, "+DATA\r\n"));
    TF_EQ(conn_service(g_now), 0);
    TF_EQ(g_rxdone[c], 1);
    TF_CHECK(rx_clean(c));
    TF_STREQ(ev(c), "R:PING|R:VER|R:WHOAMI|");
    /* A line over three inputs, CR and LF apart. */
    put_text(c, "PI");
    TF_EQ(conn_service(g_now), 0);
    TF_EQ(g_rxdone[c], 2);
    put_text(c, "NG\r");
    TF_EQ(conn_service(g_now), 0);
    put_text(c, "\n");
    TF_EQ(conn_service(g_now), 1);
    drain(c, 0);
    TF_STREQ(g_out, k_ok);
    TF_STREQ(ev(c), "R:PING|");
    /* An over-long line over two inputs. */
    memset(line, 'A', sizeof line);
    put_in(c, line, LONGLINE - 2);
    TF_EQ(conn_service(g_now), 0);
    put_in(c, line, 2);
    TF_EQ(conn_service(g_now), 0);
    put_text(c, "\r\n");
    TF_EQ(conn_service(g_now), 1);
    drain(c, 0);
    TF_CHECK(tf_starts(g_out, "-ERR 103 "));
    TF_STREQ(ev(c), "R:|");
    stop();
}

/* ---------------------------------------------------------------------
 * Fairness.
 * --------------------------------------------------------------------- */

/* Check that g_order is one request of each of n slots in turn from its
   first, and that this first is one further than *first (none before:
   *first is CONN_MAX); keep it in *first and forget the order. */
static void check_turn(u8 n, u8 *first)
{
    u8 i, s;

    TF_EQ(strlen(g_order), n);
    s = (u8)(g_order[0] - '0');
    for (i = 1; i < n; i++)
        TF_EQ(g_order[i] - '0', (s + i) % n);
    if (*first < CONN_MAX)
        TF_EQ(s, (*first + 1) % n);
    *first = s;
    g_order[0] = '\0';
}

void test_fair(void)
{
    static const char *const stations[] = { "F0", "F1", "F2" };
    u8 c, k, first, skip;

    start(NULL, 3, 0);
    for (c = 0; c < 3; c++)
        TF_EQ(open_slot(stations[c]), c);
    for (c = 0; c < 3; c++)
        login(c, stations[c]);
    for (c = 0; c < 3; c++)
        put_text(c, "PING\r\nPING\r\nPING\r\nPING\r\nPING\r\n");
    g_order[0] = '\0';
    first = CONN_MAX;
    for (k = 0; k < 3; k++) {
        TF_EQ(conn_service(g_now), 3);
        check_turn(3, &first);
        for (c = 0; c < 3; c++)
            TF_EQ(drain(c, 0), 5);
    }
    /* The fourth pass; the output of its first slot is held back. */
    TF_EQ(conn_service(g_now), 3);
    skip = (u8)(g_order[0] - '0');
    check_turn(3, &first);
    for (c = 0; c < 3; c++)
        if (c != skip)
            drain(c, 0);
    TF_EQ(conn_service(g_now), 2);
    TF_CHECK(strchr(g_order, '0' + skip) == NULL);
    g_order[0] = '\0';
    for (c = 0; c < 3; c++)
        drain(c, 0);
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(g_order[0] - '0', skip);
    drain(skip, 0);
    TF_EQ(conn_service(g_now), 0);
    for (c = 0; c < 3; c++) {
        TF_EQ(g_rxdone[c], 3);
        TF_STREQ(ev(c), "R:PING|R:PING|R:PING|R:PING|R:PING|");
    }
    stop();
}

/* ---------------------------------------------------------------------
 * The host.
 * --------------------------------------------------------------------- */

void test_host(void)
{
    proto_peer p, q;
    u32 t;
    u8 a, b, l, d;

    start("RO1,RO2", NS, 0);
    t = g_now;
    a = open_slot("PC1");
    login(a, "PC1");
    g_now += 7 * TMR_10SEC;
    TF_CHECK(strstr(ask(a, k_status), "\r\nK UPTIME=70\r\n") != NULL);
    TF_CHECK(strstr(g_out, k_ses1) != NULL);
    /* The station of the transport is the one capped. */
    b = open_slot("RO1");
    TF_STREQ(ask(b, "HELLO X1"), k_ok);
    TF_CHECK(tf_starts(ask(b, k_login), "+OK LEVEL=1 "));
    d = open_slot("PC2");
    TF_STREQ(ask(d, "HELLO RO2"), k_ok);
    TF_CHECK(tf_starts(ask(d, k_login), "+OK LEVEL=3 "));
    TF_CHECK(claim(&l));
    TF_EQ(conn_count(), 3);
    TF_CHECK(strstr(ask(a, k_status), "\r\nK SESSIONS=3\r\n") != NULL);
    TF_CHECK(tf_starts(ask(a, k_import), k_e402));
    /* The rows, in slot order, idle since each slot's last request. */
    g_now += TMR_10SEC;
    TF_STREQ(ask(b, "PING"), k_ok);
    g_now += TMR_10SEC;
    conn_timeouts(g_now);
    TF_EQ(conn_info(0, &p), 0);
    TF_EQ(conn_info(1, &q), 0);
    TF_CHECK(p.slot < q.slot);
    TF_EQ(conn_info(2, &p), 0);
    TF_CHECK(q.slot < p.slot);
    memset(&p, 0x55, sizeof p);
    memset(&q, 0x55, sizeof q);
    TF_EQ(conn_info(3, &p), 1);
    TF_CHECK(memcmp(&p, &q, sizeof p) == 0);
    TF_CHECK(info_of(a, &p));
    TF_STREQ(p.kind, k_kind);
    TF_STREQ(p.station, "PC1");
    TF_STREQ(p.user, "ADMIN");
    TF_EQ(p.level, 3);
    TF_EQ(p.state, PROTO_S_READY);
    TF_EQ(p.self, 0);
    TF_EQ(p.idle, 20);
    TF_CHECK(info_of(b, &p));
    TF_STREQ(p.station, "RO1");
    TF_EQ(p.level, 1);
    TF_EQ(p.idle, 10);
    TF_CHECK(info_of(d, &p));
    TF_STREQ(p.station, "PC2");
    TF_EQ(info_of(l, &p), 0);
    conn_timeouts(g_now + BIGSPAN);
    TF_CHECK(info_of(d, &p));
    TF_EQ(p.idle, 20 + secs(BIGSPAN));
    g_now += BIGSPAN;
    TF_EQ(conn_uptime(g_now), secs(g_now - t));
    /* With one other session open, then none (a LISTEN is none). */
    hang(b);
    TF_CHECK(strstr(ask(a, k_status), "\r\nK SESSIONS=2\r\n") != NULL);
    TF_CHECK(tf_starts(ask(a, k_import), k_e402));
    hang(d);
    TF_CHECK(tf_starts(ask(a, k_import), "-ERR "));
    TF_EQ(tf_starts(g_out, k_e402), 0);
    TF_CHECK(strstr(ask(a, k_status), k_ses1) != NULL);
    stop();
}

/* ---------------------------------------------------------------------
 * Paging.
 * --------------------------------------------------------------------- */

/* Append " key=" and a quoted value of n backslashes, escaped, to
   line: each takes two characters in a response too. */
static void kv(char *line, const char *key, u16 n)
{
    char *p;

    p = line + strlen(line);
    *p++ = ' ';
    strcpy(p, key);
    p += strlen(key);
    *p++ = '=';
    *p++ = '"';
    for (; n > 0; n--) {
        *p++ = '\\';
        *p++ = '\\';
    }
    *p++ = '"';
    *p = '\0';
}

void test_paging(void)
{
    char line[PROTO_MAXLINE + 1];
    char id[ENG_IDLEN + 1];
    const char *s;
    u16 i, rows;
    u8 c;

    start(NULL, NS, 0);
    c = open_slot("PG");
    login(c, "PG");
    strcpy(line, "ITEM ADD CATEGORY=SYS");
    kv(line, "DESCR", 60);
    kv(line, "MFR", 30);
    TF_CHECK(tf_starts(ask(c, line), "+OK ID="));
    id[0] = '\0';
    strncat(id, g_out + 7, ENG_IDLEN);
    id[strcspn(id, "\r")] = '\0';
    strcpy(line, "ITEM SET ");
    strcat(line, id);
    kv(line, "MODEL", 30);
    kv(line, "ACQ_SRC", 30);
    TF_STREQ(ask(c, line), k_ok);
    strcpy(line, "ITEM COPY ");
    strcat(line, id);
    for (i = 1; i < PROTO_PAGEMAX; i++) {
        TF_CHECK(tf_starts(ask(c, line), "+OK ID="));
        ev(c);
    }
    /* A full page would pass the output block's room: it stops early,
       whole, with its continuation. */
    sprintf(line, "ITEM LIST PAGE=%d FIELDS=ITEM_ID,MFR,DESCR,MODEL,ACQ_SRC",
            PROTO_PAGEMAX);
    ask(c, line);
    TF_CHECK(tf_starts(g_out,
                       "+DATA\r\nF ITEM_ID MFR DESCR MODEL ACQ_SRC\r\n"));
    rows = 0;
    for (s = g_out; (s = strstr(s, "\r\nR ")) != NULL; s += 2)
        rows++;
    TF_CHECK(rows > 0 && rows < PROTO_PAGEMAX);
    s = strstr(g_out, "\r\nC ");
    TF_CHECK(s != NULL && strcmp(strstr(s + 2, "\r\n"), "\r\n.\r\n") == 0);
    stop();
}

/* ---------------------------------------------------------------------
 * Idle time.
 * --------------------------------------------------------------------- */

void test_idle(void)
{
    proto_peer p;
    cmem_blk *b;
    u16 off;
    u32 t;
    u8 c, d, l;

    start(NULL, NS, IDLE);
    g_defer = 1;
    t = g_now;
    TF_CHECK(claim(&l));
    c = open_slot("ID1");
    login(c, "ID1");
    /* A blank line is no activity. */
    g_now = t + 100;
    put_text(c, "\r\n");
    TF_EQ(conn_service(g_now), 0);
    g_now = t + 2 * TMR_10SEC;
    conn_timeouts(g_now);
    TF_CHECK(info_of(c, &p));
    TF_EQ(p.idle, 20);
    conn_timeouts(t + IDLE - 1);
    TF_EQ(g_closes[c], 0);
    conn_timeouts(t + IDLE);
    TF_EQ(g_closes[c], 1);
    TF_EQ(conn_state(c), CONN_CLOSING);
    TF_EQ(g_closes[l], 0);
    TF_EQ(conn_state(l), CONN_LISTEN);
    conn_timeouts(t + IDLE + 1);
    TF_EQ(g_closes[c], 1);
    hang(c);
    TF_STREQ(ev(c), "C:|");

    /* A request answered starts the time again. */
    g_now = t = t + IDLE + 1;
    c = open_slot("ID2");
    g_now = t + 100;
    TF_STREQ(ask(c, "PING"), k_ok);
    conn_timeouts(t + 100 + IDLE - 1);
    TF_EQ(g_closes[c], 0);
    conn_timeouts(t + 100 + IDLE);
    TF_EQ(g_closes[c], 1);
    hang(c);

    /* A request whose work outlasts the idle time (a PACK, an IMPORT):
       conn_served, given the tick at which the work ended, restarts the
       idle time of only the slot it answered; the other slot closes at
       its own time.  A later conn_served, with no request answered,
       restarts nothing. */
    g_now = t = t + 100 + IDLE;
    c = open_slot("ID6");
    d = open_slot("ID7");
    TF_STREQ(ask(c, "PING"), k_ok);
    conn_served(t + 2 * IDLE);
    conn_timeouts(t + 2 * IDLE);
    TF_EQ(g_closes[c], 0);
    TF_EQ(g_closes[d], 1);
    hang(d);
    conn_served(t + 2 * IDLE + 100);
    conn_timeouts(t + 3 * IDLE - 1);
    TF_EQ(g_closes[c], 0);
    conn_timeouts(t + 3 * IDLE);
    TF_EQ(g_closes[c], 1);
    hang(c);

    /* A send that never completes, each piece sent restarting it. */
    g_now = t = t + 3 * IDLE;
    c = open_slot("ID3");
    put_text(c, "PING\r\n");
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(conn_out_peek(c, &b, &off), 5);
    conn_out_done(c, t + 50, 1);
    conn_timeouts(t + 50 + IDLE - 1);
    TF_EQ(g_closes[c], 0);
    conn_timeouts(t + 50 + IDLE);
    TF_EQ(g_closes[c], 1);
    hang(c);

    /* A CLOSING slot whose output is never sent. */
    g_now = t = t + 50 + IDLE;
    c = open_slot("ID4");
    put_text(c, "BYE\r\n");
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(conn_state(c), CONN_CLOSING);
    conn_timeouts(t + IDLE - 1);
    TF_EQ(g_closes[c], 0);
    conn_timeouts(t + IDLE);
    TF_EQ(g_closes[c], 1);
    /* A send that completes before the hang-up closes nothing again. */
    g_now = t + IDLE;
    TF_EQ(drain(c, 0), 5);
    TF_EQ(g_closes[c], 1);
    hang(c);
    stop();

    /* No timeout at all. */
    start(NULL, NS, 0);
    c = open_slot("ID5");
    g_now += BIGSPAN;
    conn_timeouts(g_now);
    TF_EQ(g_closes[c], 0);
    TF_EQ(conn_state(c), CONN_OPEN);
    stop();
}

/* ---------------------------------------------------------------------
 * Shutdown.
 * --------------------------------------------------------------------- */

void test_shutdown(void)
{
    char line[LONGLINE];
    u32 t;
    u8 a, b, d, e, l, x, y;

    start(NULL, 6, 0);
    a = open_slot("S1");
    b = open_slot("S2");
    d = open_slot("S3");
    e = open_slot("S4");
    TF_CHECK(claim(&l));
    TF_CHECK(claim(&x));
    login(a, "S1");
    login(b, "S2");
    login(d, "S3");
    put_text(a, "PING\r\n");
    TF_EQ(conn_service(g_now), 1);
    put_text(d, "PING\r\nPING\r\n");
    t = g_now;
    g_defer = 1;
    conn_shutdown(t);
    TF_EQ(conn_stopping(), 1);
    TF_EQ(conn_done(), 0);
    TF_EQ(g_closes[l], 1);
    TF_EQ(g_closes[x], 1);
    TF_EQ(conn_state(l), CONN_CLOSING);
    hang(l);
    TF_EQ(claim(&y), 0);
    /* A call that came in before the LISTEN was ended opens nothing. */
    conn_open(x, t, "S6");
    TF_EQ(conn_state(x), CONN_CLOSING);
    TF_EQ(g_closes[x], 1);
    TF_EQ(conn_count(), 4);
    hang(x);
    TF_STREQ(ev(l), "");
    TF_STREQ(ev(x), "");
    g_defer = 0;
    /* A second shutdown changes nothing: the wait still runs from t. */
    conn_shutdown(t + 1);
    TF_EQ(g_closes[a], 0);

    /* The response being sent is sent first. */
    drain(a, 0);
    TF_STREQ(g_out, k_ok);
    TF_EQ(conn_state(a), CONN_OPEN);
    /* The next request of each is refused, then the slot closes. */
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(conn_state(d), CONN_CLOSING);
    drain(d, 0);
    TF_CHECK(tf_starts(g_out, k_e501));
    TF_EQ(g_closes[d], 1);
    TF_CHECK(rx_clean(d));
    TF_STREQ(ev(d), "R:PING|C:|");
    TF_CHECK(tf_starts(ask(a, "PING"), k_e501));
    TF_EQ(conn_state(a), CONN_FREE);
    memset(line, 'A', sizeof line);
    put_in(e, line, sizeof line);
    TF_EQ(conn_service(g_now), 0);
    put_text(e, "\n");
    TF_EQ(conn_service(g_now), 1);
    drain(e, 0);
    TF_CHECK(tf_starts(g_out, k_e501));
    TF_EQ(conn_state(e), CONN_FREE);
    /* A blank line, tabs included, is still no request. */
    TF_STREQ(ask(b, ""), "");
    TF_EQ(g_served, 0);
    TF_STREQ(ask(b, " \t"), "");
    TF_EQ(g_served, 0);
    TF_EQ(conn_state(b), CONN_OPEN);
    /* The wait ends with every slot still in use closed once. */
    g_defer = 1;
    conn_timeouts(t + CONN_SHUTWAIT - 1);
    TF_EQ(g_closes[b], 0);
    TF_EQ(conn_done(), 0);
    conn_timeouts(t + CONN_SHUTWAIT);
    TF_EQ(g_closes[b], 1);
    TF_EQ(conn_done(), 0);
    conn_timeouts(t + CONN_SHUTWAIT + 1);
    TF_EQ(g_closes[b], 1);
    hang(b);
    TF_STREQ(ev(b), "C:|");
    TF_EQ(conn_done(), 1);
    g_now = t + CONN_SHUTWAIT + 1;
    stop();
}

/* ---------------------------------------------------------------------
 * What an ended slot drops.
 * --------------------------------------------------------------------- */

void test_drop(void)
{
    proto_peer p;
    u8 c, d;

    start(NULL, 1, 0);
    TF_EQ(conn_stopping(), 0);
    TF_CHECK(claim(&c));
    /* Input on a slot not open is zeroed at once and not given back. */
    put_text(c, "LOGIN ADMIN " PW "\r\n");
    TF_CHECK(rx_clean(c));
    TF_EQ(g_rxdone[c], 0);
    conn_open(c, g_now, "DR1");
    login(c, "DR1");
    put_text(c, "PIN");
    TF_EQ(conn_service(g_now), 0);
    hang(c);
    TF_STREQ(ev(c), "C:|");
    /* A new session starts afresh: no user, no partial line. */
    d = open_slot("DR2");
    TF_EQ(d, c);
    TF_CHECK(info_of(c, &p));
    TF_STREQ(p.station, "DR2");
    TF_STREQ(p.user, "");
    TF_EQ(p.level, 0);
    TF_EQ(p.state, PROTO_S_NEW);
    TF_CHECK(tf_starts(ask(c, "G"), "-ERR "));
    TF_STREQ(ev(c), "O:DR2|R:G|");
    /* Input not yet read when the session ends. */
    login(c, "DR2");
    put_text(c, "PASSWD ADMIN " PW " x\r\n");
    hang(c);
    TF_CHECK(rx_clean(c));
    TF_EQ(g_rxdone[c], 3);
    /* The line after BYE is zeroed by the time the slot is closed. */
    c = open_slot("DR3");
    g_defer = 1;
    put_text(c, "BYE\r\nPASSWD ADMIN " PW " y\r\n");
    TF_EQ(conn_service(g_now), 1);
    drain(c, 0);
    TF_EQ(g_closes[c], 1);
    TF_CHECK(rx_clean(c));
    TF_EQ(conn_service(g_now), 0);
    hang(c);
    TF_STREQ(ev(c), "O:DR3|R:BYE|C:|");
    stop();
}

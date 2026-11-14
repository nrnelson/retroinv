/*
 * T_CONN.C - the server's connection slots (CONN.H) over a fake
 * transport and the real engine.
 *
 * usage: T_CONN dir
 *
 * dir is a data directory prefix ("OBJ/HOST/CONNT/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files there are removed at the start and at the end.
 * Checks, in this file:
 * - conn_init's refusals (slot count, a second start, no near memory
 *   on the host, no far memory), no memory held after them, the
 *   states, counts and uptime of a fresh table, and claims up to its
 *   size;
 * - one session: the OPEN, REQ, LOGIN and CLOSE events and the REQ
 *   texts (first word, second word after any blanks and tabs,
 *   upper-cased, cut, never an argument, "" for a blank first byte and
 *   an over-long line),
 *   conn_info before and after LOGIN, blank lines (no response, no
 *   event), 103 for an over-long line, a line of PROTO_MAXLINE bytes
 *   answered, the input zeroed before rx_done, no password in the
 *   output block, nor in the slots once its request has been answered
 *   (where a second slot's station is found), BYE and the
 *   PROTO_MAXFAIL-th failed LOGIN closing the slot only once its output
 *   has drained, the line after BYE never answered and zeroed; a
 *   session with no watcher.
 * T_CONN2.C and T_CONN3.C check the rest (see their headers).  Every
 * phase checks that no callback was made against CONN.H.  Prints a
 * summary line "T_CONN PASS n"; exit status 0 when every check passes,
 * else 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "T_CONN.H"

#define RXOFF           5       /* where the fake puts input in its
                                   buffer, so an offset is honoured     */
#define EVMAX           200     /* event text kept per slot             */
#define WIN             64      /* bytes of a block read at a time      */

const char k_ok[] = "+OK\r\n";
const char k_kind[] = "TEST";
const char k_login[] = "LOGIN ADMIN " PW;
u32 g_now = 1000;
char *g_out;
u16 g_served;
int g_defer;
u16 g_rxdone[CONN_MAX];
u16 g_closes[CONN_MAX];
char g_order[CONN_MAX + 1];

static const char k_e401[] = "-ERR 401 ";
static eng *g_e;
static u16 g_bad;                       /* callbacks against CONN.H     */
static u16 g_kicks[CONN_MAX];

static const eng_time g_t0 = { 2026, 10, 8, 12, 0, 0 };
static cmem_blk *g_rx[CONN_MAX];        /* the fake's input buffers     */
static u16 g_rxlen[CONN_MAX];           /* bytes of the input handed    */
static u8 g_rxwait[CONN_MAX];           /* input handed, not given back */
static u8 g_live[CONN_MAX];             /* claimed, conn_closed not yet */
static cmem_blk *g_blk[CONN_MAX];       /* the output block kick saw    */
static char g_ev[CONN_MAX][EVMAX];      /* "R:PING|L:ADMIN|" per slot   */
static char g_evout[EVMAX];
static u16 g_allocs;

/* ---------------------------------------------------------------------
 * The fake transport.
 * --------------------------------------------------------------------- */

/* 1 when the n bytes at off of b are all zero. */
static int blk_zero(cmem_blk *b, u16 off, u16 n)
{
    u8 buf[WIN];
    u16 k, i;

    while (n > 0) {
        k = n < sizeof buf ? n : (u16)sizeof buf;
        cmem_get(b, off, buf, k);
        for (i = 0; i < k; i++)
            if (buf[i] != 0)
                return 0;
        off = (u16)(off + k);
        n = (u16)(n - k);
    }
    return 1;
}

static void fk_kick(u8 c)
{
    cmem_blk *b;
    u16 off;

    g_kicks[c]++;
    if (!g_live[c] || conn_out_peek(c, &b, &off) == 0)
        g_bad++;
    else
        g_blk[c] = b;
}

static void fk_rxdone(u8 c)
{
    g_rxdone[c]++;
    if (!g_live[c] || !g_rxwait[c] || !blk_zero(g_rx[c], RXOFF, g_rxlen[c]))
        g_bad++;
    g_rxwait[c] = 0;
}

static void fk_close(u8 c)
{
    g_closes[c]++;
    if (!g_live[c] || g_closes[c] > 1)
        g_bad++;
    else if (!g_defer) {
        conn_closed(c);
        g_live[c] = 0;
    }
}

static const conn_tops g_ops = { fk_kick, fk_rxdone, fk_close, k_kind, 0 };
const conn_tops g_mbx = { fk_kick, fk_rxdone, fk_close, "MBOX", 1 };

static void fk_event(u8 c, u8 e, const char *text)
{
    static const char letters[] = "?OLRC";
    char *p;
    u16 n;

    if (c >= CONN_MAX || !g_live[c] || strlen(text) > CONN_TRACELEN ||
        strstr(text, PW) != NULL) {
        g_bad++;
        return;
    }
    if (e == CONN_EV_REQ) {
        n = (u16)strlen(g_order);
        if (n < CONN_MAX) {
            g_order[n] = (char)('0' + c);
            g_order[n + 1] = '\0';
        }
    }
    n = (u16)strlen(g_ev[c]);
    if (n + strlen(text) + 4 > EVMAX) {
        g_bad++;
        return;
    }
    p = g_ev[c] + n;
    *p++ = letters[e <= CONN_EV_CLOSE ? e : 0];
    *p++ = ':';
    strcpy(p, text);
    strcat(p, "|");
}

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

/* A table of nslots slots on the engine, capping readonly, with the
   idle timeout idle, started at g_now, events going to the fake. */
void start(const char *readonly, u8 nslots, u32 idle)
{
    TF_EQ(conn_init(g_e, readonly, nslots, idle, g_now), 0);
    conn_watch(fk_event);
    g_bad = 0;
    g_defer = 0;
}

/* Hang up every slot still in use, check the fake's record and free
   the table. */
void stop(void)
{
    u8 c;

    for (c = 0; c < CONN_MAX; c++)
        if (conn_state(c) != CONN_FREE)
            hang(c);
    TF_EQ(g_bad, 0);
    conn_free();
}

/* conn_claim for the fake as the transport ops (g_ops or g_mbx), its
   record of the slot started afresh. */
int claim_as(const conn_tops *ops, u8 *c)
{
    if (!conn_claim(ops, c))
        return 0;
    g_kicks[*c] = 0;
    g_rxdone[*c] = 0;
    g_closes[*c] = 0;
    g_rxwait[*c] = 0;
    g_blk[*c] = NULL;
    g_ev[*c][0] = '\0';
    g_live[*c] = 1;
    return 1;
}

/* conn_claim for the fake. */
int claim(u8 *c)
{
    return claim_as(&g_ops, c);
}

/* A claimed slot opened from station at g_now. */
u8 open_slot(const char *station)
{
    u8 c;

    c = 0;
    TF_CHECK(claim(&c));
    conn_open(c, g_now, station);
    TF_EQ(conn_state(c), CONN_OPEN);
    return c;
}

/* The transport ends slot c's session or LISTEN. */
void hang(u8 c)
{
    conn_closed(c);
    g_live[c] = 0;
    TF_EQ(conn_state(c), CONN_FREE);
}

/* Hand len bytes to slot c, at RXOFF of its buffer. */
void put_in(u8 c, const char *bytes, u16 len)
{
    cmem_put(g_rx[c], RXOFF, bytes, len);
    g_rxlen[c] = len;
    g_rxwait[c] = 1;
    conn_input(c, g_rx[c], RXOFF, len);
}

/* Hand text, without its NUL, to slot c. */
void put_text(u8 c, const char *text)
{
    put_in(c, text, (u16)strlen(text));
}

/* 1 when every byte of the input last handed to slot c is zero. */
int rx_clean(u8 c)
{
    return blk_zero(g_rx[c], RXOFF, g_rxlen[c]);
}

/* Send slot c's output in pieces of at most piece bytes (0: as much as
   is waiting) into g_out until none waits; returns the byte count.  No
   piece sent may kick. */
u16 drain(u8 c, u16 piece)
{
    cmem_blk *b;
    u16 off, n, got;
    u16 kicks;

    got = 0;
    kicks = g_kicks[c];
    for (;;) {
        n = conn_out_peek(c, &b, &off);
        if (n == 0)
            break;
        if (piece != 0 && n > piece)
            n = piece;
        if (n > CONN_OUTSZ - got) {
            g_bad++;
            break;
        }
        cmem_get(b, off, g_out + got, n);
        got = (u16)(got + n);
        conn_out_done(c, g_now, n);
    }
    g_out[got] = '\0';
    if (g_kicks[c] != kicks)
        g_bad++;
    return got;
}

/* Send line and CR LF to slot c in one input, make one pass (its result
   in g_served) and drain the slot; returns the output.  A pass that
   answers kicks exactly once, one that answers nothing never. */
const char *ask(u8 c, const char *line)
{
    char buf[CONN_RXSZ];
    u16 n, kicks;

    n = (u16)strlen(line);
    memcpy(buf, line, n);
    buf[n] = '\r';
    buf[n + 1] = '\n';
    put_in(c, buf, (u16)(n + 2));
    kicks = g_kicks[c];
    g_served = conn_service(g_now);
    if (g_kicks[c] - kicks != (g_served != 0))
        g_bad++;
    drain(c, 0);
    return g_out;
}

/* HELLO station and LOGIN as ADMIN on slot c, its events dropped. */
void login(u8 c, const char *station)
{
    char line[sizeof "HELLO " + ENG_STALEN];

    strcpy(line, "HELLO ");
    strcat(line, station);
    TF_STREQ(ask(c, line), k_ok);
    TF_CHECK(tf_starts(ask(c, k_login), "+OK LEVEL="));
    ev(c);
}

/* Slot c's events since the last call, then forgotten. */
const char *ev(u8 c)
{
    strcpy(g_evout, g_ev[c]);
    g_ev[c][0] = '\0';
    return g_evout;
}

/* 1 when the len bytes of s are anywhere in the n bytes at b. */
static int has(const u8 *b, u16 n, const char *s, u16 len)
{
    u16 i;

    for (i = 0; i + len <= n; i++)
        if (memcmp(b + i, s, len) == 0)
            return 1;
    return 0;
}

/* 1 when s (shorter than WIN) is anywhere in the whole output block
   of slot c; the windows read overlap by its length less one. */
int out_has(u8 c, const char *s)
{
    u8 buf[WIN];
    u16 size, off, n, len;

    if (g_blk[c] == NULL)
        return 0;
    len = (u16)strlen(s);
    size = cmem_size(g_blk[c]);
    for (off = 0; size - off >= len; off = (u16)(off + n - len + 1)) {
        n = (u16)(size - off < WIN ? size - off : WIN);
        cmem_get(g_blk[c], off, buf, n);
        if (has(buf, n, s, len))
            return 1;
        if (off + n == size)
            break;
    }
    return 0;
}

/* 1 when s is anywhere in the slots' own bytes. */
int slots_have(const char *s)
{
    const u8 *b;
    u16 size;

    b = (const u8 *)conn_bytes(&size);
    return has(b, size, s, (u16)strlen(s));
}

/* ---------------------------------------------------------------------
 * The table.
 * --------------------------------------------------------------------- */

/* Fail the second far allocation. */
static int fail_second(u16 size)
{
    return size > 0 && ++g_allocs == 2;
}

static void test_init(void)
{
    u16 n;
    u8 c, i;

    TF_EQ(conn_init(g_e, NULL, 0, 0, g_now), CF_EINVAL);
    TF_EQ(conn_init(g_e, NULL, CONN_MAX + 1, 0, g_now), CF_EINVAL);
    n = tm_held();
#ifdef COMPAT_HOST
    tm_fail(1);
    TF_EQ(conn_init(g_e, NULL, CONN_MAX, 0, g_now), CF_ENOMEM);
    tm_fail(0);
    TM_HELD(n);
#endif
    g_allocs = 0;
    cmem_fault = fail_second;
    TF_EQ(conn_init(g_e, NULL, CONN_MAX, 0, g_now), CF_ENOMEM);
    cmem_fault = NULL;
    TF_CHECK(g_allocs >= 2);
    TM_HELD(n);
    start(NULL, CONN_MAX, 0);
    TF_EQ(conn_init(g_e, NULL, 1, 0, g_now), CF_EINVAL);
    TF_EQ(conn_state(0), CONN_FREE);
    TF_EQ(conn_state(CONN_MAX), CONN_FREE);
    TF_EQ(conn_count(), 0);
    TF_EQ(conn_stopping(), 0);
    TF_EQ(conn_done(), 0);
    TF_EQ(conn_uptime(g_now + 2 * TMR_10SEC), 20);
    TF_EQ(conn_uptime(g_now + TMR_10SEC - 1), 9);
    for (i = 0; i < CONN_MAX; i++) {
        TF_CHECK(claim(&c));
        TF_EQ(conn_state(c), CONN_LISTEN);
    }
    TF_EQ(claim(&c), 0);
    TF_EQ(conn_count(), 0);
    stop();
    for (c = 0; c < CONN_MAX; c++)
        TF_STREQ(ev(c), "");
    conn_free();
}

/* ---------------------------------------------------------------------
 * One session.
 * --------------------------------------------------------------------- */

static void test_session(void)
{
    proto_peer p, q;
    char line[LONGLINE + 1];
    u8 c;
    u8 d;

    start(NULL, NS, 0);
    c = open_slot("STA1");
    TF_EQ(conn_count(), 1);
    TF_STREQ(ev(c), "O:STA1|");
    TF_EQ(conn_info(0, &p), 0);
    TF_EQ(p.slot, c + 1);
    TF_STREQ(p.kind, k_kind);
    TF_STREQ(p.station, "STA1");
    TF_STREQ(p.user, "");
    TF_EQ(p.level, 0);
    TF_EQ(p.state, PROTO_S_NEW);
    TF_EQ(p.self, 0);
    TF_EQ(p.idle, 0);
    memset(&p, 0x55, sizeof p);
    memset(&q, 0x55, sizeof q);
    TF_EQ(conn_info(1, &p), 1);
    TF_CHECK(memcmp(&p, &q, sizeof p) == 0);

    TF_STREQ(ask(c, "PING"), k_ok);
    TF_EQ(g_served, 1);
    TF_EQ(g_kicks[c], 1);
    TF_EQ(g_rxdone[c], 1);
    TF_STREQ(ev(c), "R:PING|");
    /* Blank lines: consumed, no response, no event. */
    TF_STREQ(ask(c, ""), "");
    TF_EQ(g_served, 0);
    TF_STREQ(ask(c, "   "), "");
    TF_EQ(g_served, 0);
    TF_EQ(g_kicks[c], 1);
    TF_EQ(g_rxdone[c], 3);
    TF_STREQ(ev(c), "");

    TF_STREQ(ask(c, "HELLO STA1"), k_ok);
    TF_STREQ(ask(c, "LOGIN ADMIN admin"), "+OK LEVEL=3 MUSTCHG=1\r\n");
    TF_CHECK(rx_clean(c));
    TF_STREQ(ask(c, "PASSWD admin " PW), k_ok);
    TF_CHECK(rx_clean(c));
    TF_EQ(slots_have(PW), 0);
    /* Every slot's bytes are searched: a second slot's station shows. */
    d = open_slot("STA9");
    TF_EQ(slots_have("STA9"), 1);
    hang(d);
    TF_STREQ(ev(c), "R:HELLO|R:LOGIN|L:ADMIN|R:PASSWD|");
    TF_CHECK(tf_starts(ask(c, "LOGIN ADMIN bad"), k_e401));
    TF_STREQ(ev(c), "R:LOGIN|");
    TF_CHECK(tf_starts(ask(c, k_login), "+OK LEVEL=3"));
    TF_CHECK(rx_clean(c));
    TF_STREQ(ev(c), "R:LOGIN|L:ADMIN|");
    TF_EQ(out_has(c, "ADMIN admin"), 0);
    TF_EQ(out_has(c, PW), 0);
    TF_EQ(conn_info(0, &p), 0);
    TF_STREQ(p.user, "ADMIN");
    TF_EQ(p.level, 3);
    TF_EQ(p.state, PROTO_S_READY);

    /* The words of the REQ texts. */
    TF_CHECK(tf_starts(ask(c, "item list page=1 fields=item_id"), "+DATA"));
    TF_CHECK(tf_starts(ask(c, "ITEM\tLIST PAGE=1"), "+DATA"));
    TF_CHECK(tf_starts(ask(c, "ITEM   LIST PAGE=1"), "+DATA"));
    TF_CHECK(tf_starts(ask(c, "ITEM 5"), "-ERR "));
    TF_CHECK(tf_starts(ask(c, "ABCDEFGHIJ KLMNOPQRST"), "-ERR "));
    TF_CHECK(tf_starts(ask(c, "PING2"), "-ERR "));
    TF_STREQ(ask(c, " PING"), k_ok);
    TF_STREQ(ev(c), "R:ITEM LIST|R:ITEM LIST|R:ITEM LIST|R:ITEM|"
                    "R:ABCDEFGH KLMNOPQR|R:|R:|");
    /* A line of PROTO_MAXLINE bytes is answered, one more is 103. */
    memset(line, ' ', sizeof line);
    memcpy(line, "PING", 4);
    line[PROTO_MAXLINE] = '\0';
    TF_STREQ(ask(c, line), k_ok);
    memset(line, 'A', LONGLINE);
    line[LONGLINE] = '\0';
    TF_CHECK(tf_starts(ask(c, line), "-ERR 103 "));
    TF_EQ(g_served, 1);
    TF_STREQ(ev(c), "R:PING|R:|");

    /* BYE: the slot closes once its output has gone, and the line
       after it is never answered but zeroed. */
    put_text(c, "BYE\r\nPASSWD admin " PW " x\r\n");
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(conn_state(c), CONN_CLOSING);
    TF_EQ(conn_count(), 0);
    TF_EQ(drain(c, 2), 5);
    TF_STREQ(g_out, k_ok);
    TF_EQ(g_closes[c], 1);
    TF_EQ(conn_state(c), CONN_FREE);
    TF_CHECK(rx_clean(c));
    TF_STREQ(ev(c), "R:BYE|C:|");

    /* The PROTO_MAXFAIL-th failed LOGIN, the close left to the phase. */
    g_defer = 1;
    c = open_slot("STA2");
    TF_STREQ(ask(c, "HELLO STA2"), k_ok);
    TF_CHECK(tf_starts(ask(c, "LOGIN ADMIN bad1"), k_e401));
    TF_CHECK(tf_starts(ask(c, "LOGIN ADMIN bad2"), k_e401));
    TF_EQ(conn_state(c), CONN_OPEN);
    put_text(c, "LOGIN ADMIN bad3\r\n");
    TF_EQ(conn_service(g_now), 1);
    TF_EQ(conn_state(c), CONN_CLOSING);
    TF_EQ(g_closes[c], 0);
    drain(c, 0);
    TF_CHECK(tf_starts(g_out, k_e401));
    TF_EQ(g_closes[c], 1);
    TF_EQ(conn_state(c), CONN_CLOSING);
    TF_EQ(out_has(c, "bad3"), 0);
    TF_CHECK(rx_clean(c));
    hang(c);
    TF_STREQ(ev(c), "O:STA2|R:HELLO|R:LOGIN|R:LOGIN|R:LOGIN|C:|");

    /* No watcher: no event, and the slot still works and closes. */
    g_defer = 0;
    conn_watch(NULL);
    c = open_slot("STA3");
    TF_STREQ(ask(c, "PING"), k_ok);
    put_text(c, "BYE\r\n");
    TF_EQ(conn_service(g_now), 1);
    drain(c, 0);
    TF_EQ(g_closes[c], 1);
    TF_EQ(conn_state(c), CONN_FREE);
    TF_STREQ(ev(c), "");
    conn_watch(fk_event);
    stop();
}

int main(int argc, char **argv)
{
    char why[TFS_WHYSZ];
    u8 c;

    cmem_init();
    tf_begin("T_CONN");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    g_out = (char *)malloc(CONN_OUTSZ + 1);
    for (c = 0; c < CONN_MAX; c++)
        g_rx[c] = cmem_alloc(RXOFF + CONN_RXSZ);
    why[0] = '\0';
    if (!TF_CHECK(g_out != NULL && g_rx[CONN_MAX - 1] != NULL) ||
        !TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return tf_done();
    }
    test_init();
    test_session();
    test_tickets();
    test_throttle();
    test_input();
    test_fair();
    test_host();
    test_paging();
    test_idle();
    test_shutdown();
    test_drop();
    test_notes();
    eng_close(g_e);
    te_remove(tfs_dir(), NULL);
    for (c = 0; c < CONN_MAX; c++)
        cmem_free(g_rx[c]);
    free(g_out);
    return tf_done();
}

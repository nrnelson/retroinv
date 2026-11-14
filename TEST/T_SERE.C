/*
 * T_SERE.C - the client's SERIAL link and the server end to end over a
 * pair of simulated ports (FAKEUART.H): the same protocol bytes as over
 * the LOCAL link, and the hang-up of a client that goes without BYE.
 *
 * usage: T_SERE [dir [golden]]
 *
 * dir is a data directory prefix ("OBJ/HOST/SERET/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files there are removed at the start and at the end.  golden
 * is TEST/GOLDEN/INVCMD.TXT, what INV /CMD /LOCAL writes for the request
 * script of TEST/T_INV.SH (default: INVCMD.TXT beside the program).
 *
 * One program is both ends: the client on COM1 (LINK.H over SERCLI.H,
 * or SERCLI.H alone) and the server on COM2 (TSERIAL.H, CONN.H and the
 * engine on dir, its clock fixed), the two ports paired, as by a full
 * null-modem cable, and paced; the server runs from the ports' pump, so
 * every pass of its loop happens while the client waits.  Checks:
 * - the request script, fed in pieces with link_feed as INV /CMD feeds
 *   its input, gives over SERIAL exactly the bytes of the golden file,
 *   and over LOCAL, on a fresh data directory with the same fixed clock,
 *   the same bytes; the session ends at BYE with LINK_CLOSED, and the
 *   request after it is not answered;
 * - a client that logs in and closes its port without BYE leaves the
 *   session open until its DTR has been down TSER_HANGUP ticks, then
 *   the server's slot listens again;
 * - noise before the first request, as a line gets when one end starts
 *   while the other's port opens: a byte with a framing error while the
 *   server listens opens no session, and a clean byte that leads the
 *   HELLO line has it refused as unknown with the session in step, so
 *   that the HELLO sent again (as sn_hello of SESS.H does) and the
 *   requests after it are served;
 * - each run's server session opened and closed once, and no CONN
 *   event text holds the password; once stopped, no port is open.
 * Prints a summary line "T_SERE PASS n"; exit status 0 when every check
 * passes, else 1.
 */
#define COMPAT_UART
#define LINK_SERIAL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TSERIAL.H"
#include "LINK.H"
#include "SERCLI.H"
#include "TESTFW.H"
#include "FAKEUART.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"

#define CPORT           1       /* the client's port                    */
#define SPORT           2       /* the server's port                    */
#define RATE            64      /* bytes a line moves each way per step */
#define LOOPS           64      /* server passes the end may take       */
#define WAIT            5       /* seconds the client waits a response  */
#define STATION         "SERE"  /* HELLO's station, over both links     */

extern char sere_script_fits[1];
extern char sere_script_fits[(TSC_OUTSZ >= TSC_SIZE) ? 1 : 2];

static const eng_time g_t0 = { TSC_CLOCK };

static eng *g_e;
static u32 g_now = 1000;        /* the server's clock                   */
static u16 g_opens;             /* CONN_EV_OPEN and CONN_EV_CLOSE seen  */
static u16 g_closes;

static void on_event(u8 c, u8 ev, const char *text)
{
    TF_EQ(c, 0);
    TF_CHECK(strstr(text, TSC_PW) == NULL);
    if (ev == CONN_EV_OPEN)
        g_opens++;
    else if (ev == CONN_EV_CLOSE)
        g_closes++;
}

/* fu_pump_fn: one pass of the server's loop at the tick count *arg. */
static void serve(void *arg)
{
    u32 now;

    now = *(u32 *)arg;
    tserial_poll(now);
    conn_service(now);
    conn_timeouts(now);
}

/* Start the server on a fresh data directory and the paired ports: 1,
   else 0 with nothing left started. */
static int srv_begin(void)
{
    char why[TFS_WHYSZ];
    u8 bad;

    te_remove(tfs_dir(), NULL);
    why[0] = '\0';
    if (!TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return 0;
    }
    if (TF_EQ(conn_init(g_e, NULL, 1, 0, g_now), 0)) {
        conn_watch(on_event);
        g_opens = 0;
        g_closes = 0;
        fu_reset();
        fu_port(CPORT, UART_T_16550A);
        fu_port(SPORT, UART_T_16550A);
        fu_pair(CPORT, SPORT);
        fu_rate(CPORT, RATE);
        fu_rate(SPORT, RATE);
        if (TF_EQ(tserial_start(1 << (SPORT - 1), UART_DEFBAUD, 0, &bad),
                  TSER_OK)) {
            fu_pump(serve, &g_now);
            return 1;
        }
        conn_free();
        conn_watch(NULL);
    }
    TF_EQ(eng_close(g_e), ENG_OK);
    return 0;
}

/* Run the server until its port listens again, stop it, and check that
   its one session opened and closed and that no port is open. */
static void srv_end(void)
{
    u16 i;

    fu_pump(NULL, NULL);
    for (i = 0; i < LOOPS && conn_state(0) != CONN_LISTEN; i++)
        serve(&g_now);
    TF_EQ(conn_state(0), CONN_LISTEN);
    tserial_stop();
    conn_free();
    conn_watch(NULL);
    TF_EQ(eng_close(g_e), ENG_OK);
    TF_EQ(g_opens, 1);
    TF_EQ(g_closes, 1);
    TF_EQ(fu_isrstate(CPORT), 0);
    TF_EQ(fu_isrstate(SPORT), 0);
}

/* The script over SERIAL into t. */
static void serial_run(const char *script, u16 n, tn_text *t)
{
    link *l;

    if (!srv_begin())
        return;
    l = NULL;
    if (TF_EQ(link_open_serial(&l, CPORT, UART_DEFBAUD, 0), 0)) {
        tsc_run(l, script, n, t);
        TF_EQ(link_close(l), 0);
    }
    srv_end();
}

/* Send line on s and read its response into t. */
static void talk(sercli *s, const char *line, tn_text *t)
{
    TF_EQ(sercli_send(s, line, (u16)strlen(line)), SERCLI_OK);
    TF_EQ(sercli_response(s, tsc_take, t, WAIT), SERCLI_OK);
}

/* A client logs in and closes its port without BYE: the session stays
   open, logged in, for TSER_HANGUP - 1 ticks and then ends. */
static void hangup_run(tn_text *t)
{
    sercli *s;

    if (!srv_begin())
        return;
    s = NULL;
    if (TF_EQ(sercli_open(&s, CPORT, UART_DEFBAUD, 0), SERCLI_OK)) {
        talk(s, "HELLO " STATION, t);
        talk(s, "LOGIN ADMIN admin", t);
        TF_STREQ(t->b, "+OK\r\n+OK LEVEL=3 MUSTCHG=1\r\n");
        sercli_close(s, 0);
    }
    fu_pump(NULL, NULL);
    serve(&g_now);
    g_now += TSER_HANGUP - 1;
    serve(&g_now);
    TF_EQ(conn_state(0), CONN_OPEN);
    TF_EQ(g_closes, 0);
    g_now++;
    serve(&g_now);
    TF_EQ(g_closes, 1);
    srv_end();
}

/* Write the byte c into the client's port, a framing error marked on
   it at the server's when bad is 1, and run the server a few passes. */
static void noise(cmem_blk *b, char c, int bad)
{
    u16 i;

    if (bad)
        fu_inject(SPORT, FU_FRAMING, 1);
    cmem_put(b, 0, &c, 1);
    TF_EQ(uart_write(CPORT, b, 0, 1), 1);
    for (i = 0; i < 4; i++)
        serve(&g_now);
}

/* Noise reaches the listening server ahead of the first request: a
   byte with a framing error, which opens no session, then a clean one,
   which leads the HELLO line; HELLO, sent again, and PING are then
   served, each answered in turn, into t. */
static void noise_run(tn_text *t)
{
    static const char hello[] = "HELLO " STATION;
    cmem_blk *b;
    link *l;

    if (!srv_begin())
        return;
    b = cmem_alloc(1);
    l = NULL;
    if (TF_CHECK(b != NULL) &&
        TF_EQ(link_open_serial(&l, CPORT, UART_DEFBAUD, 0), 0)) {
        noise(b, '\377', 1);
        TF_EQ(conn_state(0), CONN_LISTEN);
        TF_EQ(g_opens, 0);
        noise(b, 'x', 0);
        TF_EQ(g_opens, 1);
        TF_EQ(link_send(l, hello, sizeof hello - 1, tsc_take, t), LINK_OK);
        TF_EQ(link_send(l, hello, sizeof hello - 1, tsc_take, t), LINK_OK);
        TF_EQ(link_send(l, "PING", 4, tsc_take, t), LINK_OK);
        TF_STREQ(t->b, "-ERR 101 unknown command\r\n+OK\r\n+OK\r\n");
        TF_EQ(link_close(l), 0);
    }
    cmem_free(b);
    srv_end();
}

int main(int argc, char **argv)
{
    char golden[DOS_PATHMAX];
    tn_text t[3];
    char *pool;
    char *script;
    u16 n;
    int i;

    cmem_init();
    tf_begin("T_SERE");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    tsc_golden(golden, argc, argv);
    pool = (char *)malloc(4 * TSC_OUTSZ);
    if (!TF_CHECK(pool != NULL))
        return tf_done();
    for (i = 0; i < 3; i++) {
        t[i].b = pool + i * TSC_OUTSZ;
        t[i].n = 0;
        t[i].b[0] = '\0';
    }
    script = pool + 3 * TSC_OUTSZ;
    if (!TF_EQ(tfs_read_all(golden, t[2].b, TSC_OUTSZ - 1, &t[2].n), 0) ||
        !TF_EQ(tfs_size(golden), t[2].n))
        printf("cannot read %s\n", golden);
    n = tsc_script(script, STATION);
    serial_run(script, n, &t[0]);
    tsc_local(script, n, &t[1]);
    tsc_same(&t[0], &t[2], "SERIAL script and golden");
    tsc_same(&t[1], &t[2], "LOCAL script and golden");
    t[0].n = 0;
    t[0].b[0] = '\0';
    hangup_run(&t[0]);
    t[0].n = 0;
    t[0].b[0] = '\0';
    noise_run(&t[0]);
    te_remove(tfs_dir(), NULL);
    free(pool);
    return tf_done();
}

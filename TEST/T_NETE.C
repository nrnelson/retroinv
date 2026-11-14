/*
 * T_NETE.C - the client's NET link and the server end to end over the
 * simulated provider (T_NETE.H): the same protocol bytes as over the
 * LOCAL link, and the sessions' state, ends and wipes.
 *
 * usage: T_NETE [dir [golden]]
 *
 * dir is a data directory prefix ("OBJ/HOST/NETET/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files and the exports there are removed at the start and at
 * the end.  golden is TEST/GOLDEN/INVCMD.TXT, what INV /CMD /LOCAL
 * writes for the request script of TEST/T_INV.SH (default: INVCMD.TXT
 * beside the program).  Checks, in this file, in every phase:
 * - no CONN event text holds the password or BADPW, the wrong one
 *   T_NETE2.C tries;
 * - once the server is stopped, every session that opened has closed,
 *   the provider holds no command, set, name or session end, and no
 *   hang-up named a session end the provider had removed.
 * T_NETE3.C (the same bytes) and T_NETE2.C (two links, the ends of a
 * session, the wipes) check the rest; see their headers.  Prints a
 * summary line "T_NETE PASS n"; exit status 0 when every check passes,
 * else 1.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TNETB.H"
#include "LINK.H"
#define TF_PART
#define TF_FILE "T_NETE"
#include "TESTFW.H"
#include "FAKENB.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"
#include "T_NETE.H"

tn_text g_rep;
char g_req[CONN_TRACELEN + 1];
u16 g_opens;
u16 g_closes;

static const eng_time g_t0 = { TSC_CLOCK };

static u32 g_now = 1000;        /* the server's clock, never moved      */
static eng *g_e;
static u8 g_inserve;
static tn_buf g_tx;             /* the last SEND and RECEIVE buffers    */
static tn_buf g_rx;
tn_buf g_cli;
tn_buf g_srv;

/* ---------------------------------------------------------------------
 * The server.
 * --------------------------------------------------------------------- */

static void on_event(u8 c, u8 ev, const char *text)
{
    TF_CHECK(c < NSLOTS);
    TF_CHECK(strstr(text, TSC_PW) == NULL);
    TF_CHECK(strstr(text, BADPW) == NULL);
    if (ev == CONN_EV_OPEN)
        g_opens++;
    else if (ev == CONN_EV_CLOSE)
        g_closes++;
    else if (ev == CONN_EV_REQ)
        strcpy(g_req, text);
}

/* 1 when the buffer of the last SEND (send 1) or RECEIVE (send 0)
   posted differs from *seen, which then takes it; else 0. */
static int took(tn_buf *seen, int send)
{
    tn_buf now;

    now.b = fnb_lastbuf(send, &now.off, &now.len);
    if (now.b == seen->b && now.off == seen->off && now.len == seen->len)
        return 0;
    *seen = now;
    return 1;
}

/* fnb_pump_fn: one pass of the server's loop at the tick count *arg,
   never inside itself.  Only the clients post between passes and only
   the server during one, so a SEND posted since the last pass is a
   client's, and a RECEIVE posted during the pass the server's. */
static void serve(void *arg)
{
    u32 now;

    if (g_inserve)
        return;
    g_inserve = 1;
    now = *(u32 *)arg;
    if (took(&g_tx, 1))
        g_cli = g_tx;
    took(&g_rx, 0);
    tnetb_poll(now);
    conn_service(now);
    conn_timeouts(now);
    if (took(&g_rx, 0))
        g_srv = g_rx;
    took(&g_tx, 1);
    g_inserve = 0;
}

/* Start the server on a fresh data directory, the stations of the
   comma list readonly (kept by pointer) capped: 1, else 0 with nothing
   started. */
int srv_begin(const char *readonly)
{
    char why[TFS_WHYSZ];
    u8 rc;

    fnb_reset();
    TF_EQ(nb_present(), 1);
    te_remove(tfs_dir(), NULL);
    why[0] = '\0';
    if (!TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return 0;
    }
    if (!TF_EQ(conn_init(g_e, readonly, NSLOTS, 0, g_now), 0)) {
        eng_close(g_e);
        return 0;
    }
    conn_watch(on_event);
    g_opens = 0;
    g_closes = 0;
    g_tx.b = NULL;
    g_rx.b = NULL;
    g_cli.b = NULL;
    g_srv.b = NULL;
    rc = NB_RC_PENDING;
    TF_EQ(tnetb_start(TSC_SRV, NSLOTS, NSLOTS, &rc), TNETB_OK);
    TF_EQ(rc, NB_OK);
    fnb_pump(serve, &g_now);
    return 1;
}

/* Stop the server, its clients' links closed, and check that nothing
   is left: every session that opened closed, no command, set, name or
   session end, no hang-up of a session the provider had removed. */
void srv_end(void)
{
    fnb_pump(NULL, NULL);
    TF_EQ(tnetb_stop(), 0);
    conn_free();
    TF_EQ(eng_close(g_e), ENG_OK);
    TF_EQ(g_closes, g_opens);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_badhangups(), 0);
}

void srv_run(void)
{
    serve(&g_now);
}

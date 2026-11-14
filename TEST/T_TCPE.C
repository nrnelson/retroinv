/*
 * T_TCPE.C - TCP clients end to end through the bridge to the server
 * over the simulated stack and provider (T_TCPE.H): the same protocol
 * bytes as over the LOCAL link, sessions at once, their stations and
 * levels, and their ends.
 *
 * usage: T_TCPE [dir [golden]]
 *
 * dir is a data directory prefix ("OBJ/HOST/TCPET/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files there are removed before the phases that need fresh
 * data and at the end.  golden is TEST/GOLDEN/INVCMD.TXT, the output of
 * INV /CMD /LOCAL for the request script of TEST/T_INV.SH (default:
 * INVCMD.TXT beside the program).  Checks, in this file, in every
 * phase:
 * - every session's station is the bridge's name;
 * - no event text of CONN holds the password;
 * - once the server is stopped, every session that opened has closed.
 * T_TCPE3.C (the same bytes) and T_TCPE2.C (sessions at once, their
 * cap, their ends) check the rest; see their headers.  Prints a
 * summary line "T_TCPE PASS n"; exit status 0 when every check passes,
 * else 1.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TNETB.H"
#include "TSOCK.H"
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"
#include "T_TCPE.H"

char *g_rx[TS_SLOTS];
char *g_want[TS_SLOTS];
char *g_script;
char g_ev[EVSZ];

static const eng_time g_t0 = { TSC_CLOCK };

static eng *g_e;
static u8 g_up;                 /* 1 while the server runs              */
static u16 g_opens;             /* CONN_EV_OPEN and CONN_EV_CLOSE seen  */
static u16 g_closes;

/* ---------------------------------------------------------------------
 * The server.
 * --------------------------------------------------------------------- */

/* conn_event_fn: count the sessions that open, each from the bridge's
   station, and those that close. */
static void on_conn(u8 c, u8 ev, const char *text)
{
    TF_CHECK(c < TS_SLOTS);
    TF_CHECK(strstr(text, TSC_PW) == NULL);
    if (ev == CONN_EV_OPEN) {
        TF_STREQ(text, BRNAME);
        g_opens++;
    } else if (ev == CONN_EV_CLOSE) {
        g_closes++;
    }
}

int srv_begin(const char *readonly)
{
    char why[TFS_WHYSZ];
    u8 rc;

    why[0] = '\0';
    if (!TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return 0;
    }
    if (!TF_EQ(conn_init(g_e, readonly, TS_SLOTS, 0, NOW), 0)) {
        eng_close(g_e);
        return 0;
    }
    conn_watch(on_conn);
    g_opens = 0;
    g_closes = 0;
    rc = NB_RC_PENDING;
    TF_EQ(tnetb_start(TSC_SRV, TS_SLOTS, TS_SLOTS, &rc), TNETB_OK);
    TF_EQ(rc, NB_OK);
    g_up = 1;
    return 1;
}

void srv_run(void)
{
    if (g_up) {
        tnetb_poll(NOW);
        conn_service(NOW);
        conn_timeouts(NOW);
    }
}

void srv_stop(void)
{
    if (!g_up)
        return;
    TF_EQ(tnetb_stop(), 0);
    conn_free();
    TF_EQ(eng_close(g_e), ENG_OK);
    TF_EQ(g_closes, g_opens);
    g_up = 0;
}

int main(int argc, char **argv)
{
    char golden[DOS_PATHMAX];
    char *pool;
    u8 k;

    cmem_init();
    tf_begin("T_TCPE");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    tsc_golden(golden, argc, argv);
    pool = (char *)malloc((2 * TS_SLOTS + 1) * OUTSZ);
    if (!TF_CHECK(pool != NULL))
        return tf_done();
    for (k = 0; k < TS_SLOTS; k++) {
        g_rx[k] = pool + k * OUTSZ;
        g_want[k] = pool + (TS_SLOTS + k) * OUTSZ;
    }
    g_script = pool + 2 * TS_SLOTS * OUTSZ;
    test_same(golden);
    test_four();
    test_shutdown();
    te_remove(tfs_dir(), NULL);
    free(pool);
    return tf_done();
}

/*
 * T_TCPE2.C - the second part of the T_TCPE test (T_TCPE.H): sessions
 * at once, their cap, ADMIN SHUTDOWN and the server stopped, which main
 * of T_TCPE.C runs.
 *
 * Checks:
 * - TS_SLOTS clients at once, a connection more refused; their
 *   requests interleaved, each in its own session: after one has set
 *   ADMIN's password, each logs in, WHOAMI names the bridge as its
 *   station, ADMIN CLIENTS lists every session with SELF set on the
 *   asker's row, and BYE ends each (CLOSE NETB) with the client seeing
 *   the connection's end;
 * - the same, with READONLY= naming the bridge: every session logs in
 *   at level 1 and is refused a change and ADMIN CLIENTS (203);
 * - ADMIN SHUTDOWN: +OK to the requester, then -ERR 501 to the next
 *   request of each session and the end of its connection, the server
 *   then done;
 * - with the server stopped, the bridge still accepts a connection and
 *   closes it without a byte: its CALL fails with NB_RC_NOANSWER
 *   (NOCALL).
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TSOCK.H"
#include "BRIDGE.H"
#define TF_PART
#define TF_FILE "T_TCPE2"
#include "TESTFW.H"
#include "TSFAKE.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"
#include "T_TCPE.H"

static const char k_ok[] = "+OK\r\n";
static const char k_name[] = " NAME=\"System Administrator\"\r\n";
static const char k_setpw[] = "HELLO X\r\nLOGIN ADMIN admin\r\n"
    "PASSWD admin " TSC_PW "\r\n";
static const char k_pwset[] = "+OK\r\n+OK LEVEL=3 MUSTCHG=1\r\n+OK\r\n";
static const char k_e203[] = "-ERR 203 permission denied\r\n";
static const char k_e501[] = "-ERR 501 server shutting down\r\n";
/* Requests after a shutdown: the first is answered 501 and ends the
   session; the rest, in that piece or a later one, go unanswered. */
static const char k_pings[] = "PING\r\nPING\r\nPING\r\n";

/* Append to want the response to LOGIN of ADMIN at level. */
static void login(char *want, int level)
{
    sprintf(want + strlen(want), "+OK LEVEL=%d%s", level, k_name);
}

/* Append to want the responses to HELLO, LOGIN and WHOAMI of ADMIN at
   level. */
static void logged(char *want, int level)
{
    strcat(want, k_ok);
    login(want, level);
    sprintf(want + strlen(want), "+DATA\r\nK USER=ADMIN\r\nK LEVEL=%d\r\n"
            "K STATION=" BRNAME "\r\n.\r\n", level);
}

/* Append to want the response to ADMIN CLIENTS asked by client self,
   every client logged in as ADMIN at level 3; client k, having called
   in turn, holds the server's slot k + 1. */
static void clients(char *want, u8 self)
{
    u8 k;

    strcat(want, "+DATA\r\nF SLOT TYPE STATION USER LEVEL STATE IDLE SELF"
           "\r\n");
    for (k = 0; k < TS_SLOTS; k++)
        sprintf(want + strlen(want), "R %u NETB " BRNAME " ADMIN 3 3 0 %d"
                "\r\n", (unsigned)(k + 1), k == self);
    strcat(want, ".\r\n");
}

/* Check the bridge's log: each client's OPEN and CALL in turn, then
   each one's CLOSE NETB, in the order the bridge's passes met the ends,
   then tail. */
static void logged_ends(const char *tail)
{
    char ev[EVSZ];
    size_t n;
    size_t m;
    u8 k;

    ev[0] = '\0';
    for (k = 0; k < TS_SLOTS; k++)
        sprintf(ev + strlen(ev), "%u OPEN;%u CALL;", (unsigned)k,
                (unsigned)k);
    n = strlen(ev);
    TF_CHECK(strncmp(g_ev, ev, n) == 0);
    m = n + strlen(tail);
    for (k = 0; k < TS_SLOTS; k++) {
        sprintf(ev, "%u CLOSE NETB;", (unsigned)k);
        TF_CHECK(strstr(g_ev + n, ev) != NULL);
        m += strlen(ev);
    }
    if (TF_EQ(strlen(g_ev), m))
        TF_STREQ(g_ev + m - strlen(tail), tail);
}

/* TS_SLOTS sessions at once on the data as they are, the bridge's
   station capped or not, each sending req; uncapped, client 0 first
   sets ADMIN's password. */
static void four(int capped, const char *req)
{
    const char *r[TS_SLOTS];
    u8 k;
    u8 s;

    if (!begin(capped ? BRNAME : ""))
        return;
    for (k = 0; k < TS_SLOTS; k++) {
        cli_open(k);
        r[k] = req;
        g_want[k][0] = '\0';
        logged(g_want[k], capped ? 1 : 3);
        if (capped) {
            strcat(g_want[k], k_e203);
            strcat(g_want[k], k_e203);
        } else {
            clients(g_want[k], k);
        }
        strcat(g_want[k], k_ok);
    }
    s = TS_SLOTS;
    TF_EQ(tsf_connect(BR_PORT, &s), TSF_REFUSED);
    TF_EQ(s, TS_SLOTS);
    if (!capped)
        ask(0, k_setpw, k_pwset, 0);
    talk(r, (const char *const *)g_want, 1);
    logged_ends("");
    finish();
}

void test_four(void)
{
    te_remove(tfs_dir(), NULL);
    four(0, "HELLO X\r\nLOGIN ADMIN " TSC_PW "\r\nWHOAMI\r\nADMIN CLIENTS\r\n"
         "BYE\r\n");
    four(1, "HELLO X\r\nLOGIN ADMIN " TSC_PW "\r\nWHOAMI\r\n"
         "ITEM ADD CATEGORY=SYS MFR=IBM\r\nADMIN CLIENTS\r\nBYE\r\n");
}

void test_shutdown(void)
{
    const char *r[TS_SLOTS];
    char ev[EVSZ];
    u8 k;
    u8 s;

    te_remove(tfs_dir(), NULL);
    if (!begin(""))
        return;
    for (k = 0; k < TS_SLOTS; k++)
        cli_open(k);
    /* The far block was not zeroed when it was allocated: clear slot 0's
       transmit area, unused so far, so that only what the requests
       below leave there can be found in it. */
    cmem_set(br_buffers(), 0, 0, NB_MSGMAX);
    ask(0, k_setpw, k_pwset, 0);
    TF_CHECK(wiped(0, NB_MSGMAX));
    g_want[0][0] = '\0';
    login(g_want[0], 3);
    strcat(g_want[0], k_ok);
    ask(0, "LOGIN ADMIN " TSC_PW "\r\nADMIN SHUTDOWN\r\n", g_want[0], 0);
    TF_EQ(conn_stopping(), 1);
    for (k = 0; k < TS_SLOTS; k++) {
        r[k] = k_pings;
        strcpy(g_want[k], k_e501);
    }
    talk(r, (const char *const *)g_want, 1);
    TF_EQ(conn_done(), 1);
    srv_stop();

    s = TS_SLOTS;
    TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
    if (TF_CHECK(s < TS_SLOTS))
        ask(s, NULL, "", 1);
    sprintf(ev, "%u OPEN;%u NOCALL %02X;", (unsigned)s, (unsigned)s,
            NB_RC_NOANSWER);
    logged_ends(ev);
    finish();
}

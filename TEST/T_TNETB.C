/*
 * T_TNETB.C - the server's NetBIOS transport (TNETB.H) over the
 * simulated provider (FAKENB.H), the real CONN and the engine.
 *
 * usage: T_TNETB dir
 *
 * dir is a data directory prefix ("OBJ/HOST/TNETBT/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files there are removed at the start and at the end.  This
 * file holds the helpers and main; the phases, and what they check, are
 * in T_TNET3.C and T_TNET2.C (see their headers).  Prints a summary
 * line "T_TNETB PASS n"; exit status 0 when every check passes, else 1.
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
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKENB.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "T_TNETB.H"

#define NPEER           16      /* remote ends a phase records          */

const char k_ok[] = "+OK\r\n";
u32 g_now = 1000;
char *g_rep;
u16 g_replen;
u16 g_msgs;
u16 g_msg1;
u16 g_opens;
u16 g_closes;
char g_station[ENG_STALEN + 1];

static const eng_time g_t0 = { 2026, 10, 8, 12, 0, 0 };
eng *g_e;
static u8 g_peers[NPEER];       /* the remote ends call opened          */
static u16 g_npeers;
static u16 g_held;              /* heap blocks held when a phase began:
                                   on the host, far blocks too          */

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

static void on_event(u8 c, u8 ev, const char *text)
{
    TF_CHECK(c < CONN_MAX);
    if (ev == CONN_EV_OPEN) {
        g_opens++;
        strncpy(g_station, text, ENG_STALEN);
        g_station[ENG_STALEN] = '\0';
    } else if (ev == CONN_EV_CLOSE) {
        g_closes++;
    }
}

/* A fresh provider, found. */
void fresh(void)
{
    fnb_reset();
    TF_EQ(nb_present(), 1);
}

/* A fresh provider, a table of CONN_MAX slots with the idle timeout
   idle at g_now, and the transport started on SRV. */
void begin(u8 sessions, u8 listens, u32 idle)
{
    u8 rc;

    fresh();
    g_held = tm_held();
    TF_EQ(conn_init(g_e, NULL, CONN_MAX, idle, g_now), 0);
    conn_watch(on_event);
    g_npeers = 0;
    g_opens = 0;
    g_closes = 0;
    rc = NB_RC_PENDING;
    TF_EQ(tnetb_start(SRV, sessions, listens, &rc), TNETB_OK);
    TF_EQ(rc, NB_OK);
}

/* Stop the transport and check that it left nothing behind, then hang
   up the remote ends, free the table and check that the heap holds
   what it held when begin was called. */
void finish(void)
{
    int i;
    u8 c;

    TF_EQ(tnetb_stop(), 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(tnetb_listening(), 0);
    TF_EQ(tnetb_poll(g_now), 0);
    TF_EQ(tnetb_stop(), 0);
    for (c = 0; c < CONN_MAX; c++)
        TF_EQ(conn_state(c), CONN_FREE);
    TF_EQ(g_closes, g_opens);
    for (i = 0; i < g_npeers; i++)
        fnb_peer_hangup(g_peers[i]);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_badhangups(), 0);
    conn_free();
    fnb_reset();
    TM_HELD(g_held);
}

/* One pass of the server's loop at g_now; what tnetb_poll returned. */
u16 pass(void)
{
    u16 n;

    n = tnetb_poll(g_now);
    conn_service(g_now);
    conn_timeouts(g_now);
    return n;
}

void run(u16 n)
{
    while (n-- > 0)
        pass();
}

/* Run a pass, call SRV from the remote name from and run the server
   until the session opens with from as its station: the remote end's
   LSN. */
u8 call(const char *from)
{
    u16 opens;
    int i;
    u8 lsn;

    pass();
    opens = g_opens;
    lsn = 0;
    TF_EQ(fnb_peer_call(from, SRV, &lsn), NB_OK);
    if (g_npeers < NPEER)
        g_peers[g_npeers++] = lsn;
    for (i = 0; i < LOOPS && g_opens == opens; i++)
        pass();
    TF_EQ(g_opens, opens + 1);
    TF_STREQ(g_station, from);
    return lsn;
}

/* 1 when s is one whole response: a line, or "+DATA" up to its ".". */
static int whole(const char *s, u16 n)
{
    if (n < 2 || s[n - 2] != '\r' || s[n - 1] != '\n')
        return 0;
    if (strncmp(s, "+DATA", 5) != 0)
        return 1;
    return n >= 5 && strcmp(s + n - 5, "\r\n.\r\n") == 0;
}

/* Run the server and take the messages at the remote end lsn into g_rep
   until it holds want bytes, or, for want 0, one whole response; or
   until LOOPS + want passes have gone (a response takes two), REPSZ
   bytes have come, or fnb_peer_recv gives a code other than NB_OK or
   NB_RC_PENDING.  Returns g_replen. */
u16 reply(u8 lsn, u16 want)
{
    int i;
    u16 n;
    u8 rc;

    g_replen = 0;
    g_msgs = 0;
    g_msg1 = 0;
    g_rep[0] = '\0';
    rc = NB_RC_PENDING;
    for (i = 0; i < LOOPS + want && rc == NB_RC_PENDING; i++) {
        pass();
        for (;;) {
            n = 0;
            rc = fnb_peer_recv(lsn, g_rep + g_replen,
                               (u16)(REPSZ - g_replen), &n);
            if (rc != NB_OK)
                break;
            if (g_msgs++ == 0)
                g_msg1 = n;
            g_replen = (u16)(g_replen + n);
            if (g_replen == REPSZ)
                break;
        }
        g_rep[g_replen] = '\0';
        if (want != 0 ? g_replen >= want : whole(g_rep, g_replen))
            break;
    }
    return g_replen;
}

/* Send text, without its NUL, from the remote end lsn. */
void put(u8 lsn, const char *text)
{
    TF_EQ(fnb_peer_send(lsn, text, (u16)strlen(text)), NB_OK);
}

/* Send line and CR LF from the remote end lsn: the response. */
const char *ask(u8 lsn, const char *line)
{
    char buf[PROTO_MAXLINE + 3];

    strcpy(buf, line);
    strcat(buf, "\r\n");
    put(lsn, buf);
    reply(lsn, 0);
    return g_rep;
}

/* Number of slots in the CONN_ state state. */
u16 nstate(u8 state)
{
    u16 n;
    u8 c;

    n = 0;
    for (c = 0; c < CONN_MAX; c++)
        if (conn_state(c) == state)
            n++;
    return n;
}

/* Run the server until an open session ends (g_closes passes closes):
   1 then, else 0 after LOOPS passes.  The pass that ends it has done
   something (its tnetb_poll is not 0), and the provider then holds no
   command of its slot: only the LISTENs and others in progress for the
   other sessions or callers. */
int ended(u16 closes, u16 others)
{
    int i;
    u16 n;

    n = 0;
    for (i = 0; i < LOOPS && g_closes == closes; i++)
        n = pass();
    if (g_closes == closes)
        return 0;
    if (i > 0)
        TF_CHECK(n > 0);
    TF_EQ(fnb_pending(), tnetb_listening() + others);
    return 1;
}

int main(int argc, char **argv)
{
    char why[TFS_WHYSZ];

    cmem_init();
    tf_begin("T_TNETB");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    g_rep = (char *)malloc(REPSZ + 1);
    why[0] = '\0';
    if (!TF_CHECK(g_rep != NULL) ||
        !TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return tf_done();
    }
    test_start();
    test_kept();
    test_slots();
    test_full();
    test_retry();
    test_rest();
    test_data();
    test_wipe();
    test_flow();
    test_big();
    test_both();
    test_end();
    test_stall();
    test_stop();
    eng_close(g_e);
    te_remove(tfs_dir(), NULL);
    free(g_rep);
    return tf_done();
}

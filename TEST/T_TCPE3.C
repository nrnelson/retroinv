/*
 * T_TCPE3.C - the third part of the T_TCPE test (T_TCPE.H): the stack
 * and the bridge, the TCP clients, and the same bytes over the bridge
 * as over LOCAL, which main of T_TCPE.C runs.
 *
 * Checks:
 * - the request script of TEST/T_INV.SH, sent by one client in pieces,
 *   gets exactly the bytes of that script's golden file: the session
 *   ends at BYE (CLOSE NETB), the request after BYE unanswered, and the
 *   client then sees the connection's end; both areas of the slot and
 *   the bounce buffer then hold nothing but zeros;
 * - in every phase: no event text of the bridge holds the password;
 *   every client receives exactly the bytes it must, no byte more;
 *   after each talk that ends the clients' connections, their slots
 *   listen again; once the bridge and the server are stopped the
 *   provider holds no command, set, name or session end and has refused
 *   no hang-up.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"
#define TF_PART
#define TF_FILE "T_TCPE3"
#include "TESTFW.H"
#include "FAKENB.H"
#include "TSFAKE.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"
#include "T_TCPE.H"

#define LOOPS           256     /* passes a talk takes at most          */
#define ZEROSZ          64      /* bytes zero reads at a time           */

/* A client: the requests it has still to send and what it received, in
   g_rx. */
typedef struct {
    const char *tx;             /* the bytes not yet sent               */
    u16 txn;
    u16 rxn;                    /* bytes received                       */
    u8 on;                      /* 1 while it takes part in a talk      */
    int end;                    /* tsf_recv's last result               */
} te_cli;

extern char tcpe_script_fits[1];
extern char tcpe_script_fits[(OUTSZ >= TSC_SIZE) ? 1 : 2];

static te_cli g_cli[TS_SLOTS];

/* ---------------------------------------------------------------------
 * The stack and the bridge.
 * --------------------------------------------------------------------- */

/* br_event_fn: append "slot EVENT text;" to the log arg, without the
   text when it is empty or the client's address (OPEN). */
static void on_br(void *arg, u8 slot, u8 ev, const char *text)
{
    static const char *const names[] = { "OPEN", "CALL", "NOCALL",
                                         "CLOSE" };
    char line[EVSZ];

    TF_CHECK(strstr(text, TSC_PW) == NULL);
    sprintf(line, "%u %s", (unsigned)slot, names[ev]);
    if (ev != BR_EV_OPEN && text[0] != '\0') {
        strcat(line, " ");
        strcat(line, text);
    }
    strcat(line, ";");
    if (TF_CHECK(strlen((char *)arg) + strlen(line) < EVSZ))
        strcat((char *)arg, line);
}

/* One pass: the stack, the bridge, and the server if it runs. */
static void run(void)
{
    ts_poll();
    br_poll(NOW);
    srv_run();
}

int begin(const char *readonly)
{
    u8 rc;

    fnb_reset();
    tsf_reset();
    TF_EQ(nb_present(), 1);
    TF_EQ(ts_init(), TS_OK);
    if (!srv_begin(readonly))
        return 0;
    g_ev[0] = '\0';
    br_events(on_br, g_ev);
    rc = NB_RC_PENDING;
    TF_EQ(br_start(TSC_SRV, BRNAME, NULL, 0, &rc), BR_OK);
    TF_EQ(rc, NB_OK);
    run();
    return 1;
}

void finish(void)
{
    srv_stop();
    TF_EQ(br_stop(), 0);
    ts_term();
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_sessions(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_badhangups(), 0);
}

/* ---------------------------------------------------------------------
 * The clients.
 * --------------------------------------------------------------------- */

/* Each client sends its next piece and lets the bridge write FEEDSZ
   bytes to it; run; each client in a talk reads. */
static void pass(void)
{
    te_cli *c;
    u16 n;
    u8 k;

    for (k = 0; k < TS_SLOTS; k++) {
        c = &g_cli[k];
        if (c->txn > 0) {
            n = tsf_send(k, c->tx, (u16)(c->txn < FEEDSZ ? c->txn : FEEDSZ));
            c->tx += n;
            c->txn = (u16)(c->txn - n);
        }
        if (c->on)
            tsf_room(k, FEEDSZ);
    }
    run();
    for (k = 0; k < TS_SLOTS; k++) {
        c = &g_cli[k];
        if (c->on) {
            c->end = tsf_recv(k, g_rx[k] + c->rxn,
                              (u16)(OUTSZ - 1 - c->rxn), &n);
            c->rxn = (u16)(c->rxn + n);
            g_rx[k][c->rxn] = '\0';
        }
    }
}

void cli_open(u8 k)
{
    u16 i;
    u8 had;
    u8 s;

    had = br_sessions();
    s = TS_SLOTS;
    TF_EQ(tsf_connect(BR_PORT, &s), TSF_OK);
    TF_EQ(s, k);
    for (i = 0; i < LOOPS && br_sessions() == had; i++)
        pass();
    TF_EQ(br_sessions(), had + 1);
}

void talk(const char *const *req, const char *const *want, int end)
{
    te_cli *c;
    u16 i;
    u8 k;
    int busy;

    for (k = 0; k < TS_SLOTS; k++) {
        c = &g_cli[k];
        c->on = (u8)(want[k] != NULL);
        c->tx = req[k];
        c->txn = (u16)(req[k] != NULL ? strlen(req[k]) : 0);
        c->rxn = 0;
        g_rx[k][0] = '\0';
        c->end = TSF_UP;
    }
    busy = 1;
    for (i = 0; i < LOOPS && busy; i++) {
        pass();
        busy = 0;
        for (k = 0; k < TS_SLOTS; k++) {
            c = &g_cli[k];
            if (c->on && (c->rxn < strlen(want[k]) ||
                          (end && (c->end == TSF_UP ||
                                   ts_state(k) != TS_LISTEN))))
                busy = 1;
        }
    }
    for (k = 0; k < TS_SLOTS; k++) {
        if (want[k] != NULL) {
            TF_STREQ(g_rx[k], want[k]);
            TF_EQ(g_cli[k].rxn, strlen(want[k]));
            TF_EQ(g_cli[k].end, end ? TSF_EOF : TSF_UP);
            TF_EQ(ts_state(k), end ? TS_LISTEN : TS_OPEN);
        }
    }
}

void ask(u8 k, const char *req, const char *want, int end)
{
    const char *r[TS_SLOTS];
    const char *w[TS_SLOTS];
    u8 i;

    for (i = 0; i < TS_SLOTS; i++) {
        r[i] = NULL;
        w[i] = NULL;
    }
    r[k] = req;
    w[k] = want;
    talk(r, w, end);
}

/* ---------------------------------------------------------------------
 * The same bytes as over LOCAL.
 * --------------------------------------------------------------------- */

/* 1 when the n bytes of p are all 0. */
static int zero(const u8 *p, u16 n)
{
    while (n > 0)
        if (p[--n] != 0)
            return 0;
    return 1;
}

int wiped(u8 k, u16 len)
{
    u8 buf[ZEROSZ];
    u16 off;
    u16 n;

    for (off = 0; off < len; off = (u16)(off + n)) {
        n = (u16)(len - off < ZEROSZ ? len - off : ZEROSZ);
        cmem_get(br_buffers(), (u16)(2 * k * NB_MSGMAX + off), buf, n);
        if (!zero(buf, n))
            return 0;
    }
    return zero(br_bounce(), BR_BOUNCE);
}

void test_same(const char *golden)
{
    char *gold;
    u16 n;

    gold = g_want[0];
    if (!TF_EQ(tfs_read_all(golden, gold, OUTSZ - 1, &n), 0) ||
        !TF_EQ(tfs_size(golden), n)) {
        printf("cannot read %s\n", golden);
        return;
    }
    gold[n] = '\0';
    tsc_script(g_script, "INVTEST");
    te_remove(tfs_dir(), NULL);
    if (!begin(""))
        return;
    cli_open(0);
    ask(0, g_script, gold, 1);
    TF_STREQ(g_ev, "0 OPEN;0 CALL;0 CLOSE NETB;");
    TF_CHECK(wiped(0, 2 * NB_MSGMAX));
    finish();
}

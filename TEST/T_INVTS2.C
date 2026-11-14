/*
 * T_INVTS2.C - the second part of T_INVTST (T_INVTST.H): the scripts
 * over serial lines and the mailbox, run with no NetBIOS server named.
 *
 * Checks:
 * - a script over two serial lines passes: @COM with and without a
 *   rate, in any case, ">" and ">&" requests, @WAIT, a data response
 *   longer than a transmit ring, each port opened at the rate @COM gave
 *   (UART_DEFBAUD when none); the line is closed after the response
 *   to its BYE, with no BYE of its own, and opened again; @HANGUP sends
 *   no BYE, and a line the script leaves open is closed with one; the
 *   transcript masks LOGIN's arguments;
 * - a script over a mailbox session passes: its ">&" lines go out as
 *   one request file at @POST, whose responses the @WAITs then read in
 *   order, a data response included; the session is closed after the
 *   response to its BYE and opened again; @HANGUP drops the lines
 *   queued, which are never posted; the transcript holds no password;
 * - with no server named NetBIOS is never looked for (the provider's
 *   commands still refuse to run) and @CALL fails with INVSCR_CALL;
 * - a bad @COM (another port, a rate not of UART_BAUDOK, a word too
 *   many or too few), a bad @MBOX (a station that is not one, a missing
 *   directory), @COM or @MBOX on a session open already, @POST on a
 *   serial line or a closed session, ">" and an early @WAIT on a
 *   mailbox session fail the run with INVSCR_FAIL at their line, saying
 *   why; a missing port and one open already fail it with INVSCR_PORT;
 *   on the host, whose clock is virtual, a serial response that never
 *   comes and a response file that never comes fail it too;
 * - a mailbox post that fails ends the transcript with the line after
 *   its echo: on the host, a response file whose every read fails
 *   (DOS error 55) until the wait runs out with "mailbox failed:
 *   READ-RSP, DOS error 55", the file kept; a request file of the
 *   station still there with "mailbox failed" alone; the run fails
 *   with INVSCR_FAIL at the @POST; a response file whose first read
 *   fails is read again and the script passes.
 */
#define COMPAT_NETBIOS
#define COMPAT_UART
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TMBOX.H"
#include "MBXCLI.H"
#define TF_PART
#define TF_FILE "T_INVTS2"
#include "TESTFW.H"
#include "T_FILES.H"
#include "FAKENB.H"
#include "FAKEUART.H"
#include "INVSCR.H"
#include "T_INVTST.H"

#define MBSTA           "MB1"   /* the mailbox session's station        */
#define MBREQMAX        512     /* bytes of a request file it reads     */
#define LINEMAX         (DOS_PATHMAX + 16)  /* an @MBOX line            */
#define EDEV            55      /* DOS error 55, a read on a handle of a
                                   network session that is gone         */
#define SER_RATE        64      /* bytes a line moves each way per step,
                                   so that no receive ring overflows    */

static const char *const k_mbdirs[] = { TMBOX_REQDIR, TMBOX_RSPDIR, NULL };

/* The serial server, a session of g_srv per port, from the first, and
   the BYEs each answered. */
static u16 g_byes[UART_MAXPORT];

/* The mailbox server: its session of g_srv, the last, beside the
   serial ones; the request it read; its guard against the read hook's
   own calls; the request files served; the reads of the response it
   fails once it has served. */
#define MBSESS          (NSESS - 1)
static char g_req[MBREQMAX];
static u8 g_inmb;
static u16 g_served;
static u16 g_rdfails;

extern char invts2_srv_fits[1];
extern char invts2_srv_fits[(UART_MAXPORT <= MBSESS) ? 1 : 2];

/* The @MBOX line of session 3 for MBSTA in the scratch mailbox. */
static char g_mbox[LINEMAX];

/* ---------------------------------------------------------------------
 * The serial server.
 * --------------------------------------------------------------------- */

/* fu_pump_fn: take what each port sent and send its output; a session
   whose BYE has been answered starts afresh (arg is g_srv). */
static void ser_serve(void *arg)
{
    char piece[SV_LINESZ];
    srv *v;
    u16 n;
    u8 p;

    v = (srv *)arg;
    for (p = 1; p <= UART_MAXPORT; p++, v++) {
        if (v->state != SV_OPEN)
            continue;
        n = fu_peer_recv(p, piece, (u16)sizeof piece);
        take(v, piece, n);
        if (v->opos < v->olen)
            v->opos = (u16)(v->opos + fu_peer_send(p, v->out + v->opos,
                                                   (u16)(v->olen - v->opos)));
        if (v->bye && v->opos == v->olen) {
            v->bye = 0;
            v->llen = 0;
            g_byes[p - 1]++;
        }
    }
}

/* Fresh ports and provider: COM1 and COM2 made, paced at SER_RATE,
   each with a server session, but those not in made (a bit for each
   port, as SERIAL=). */
static void ser_start(u8 made)
{
    u8 p;

    fnb_reset();
    fu_reset();
    memset(g_srv, 0, UART_MAXPORT * sizeof g_srv[0]);
    memset(g_byes, 0, sizeof g_byes);
    for (p = 1; p <= UART_MAXPORT; p++) {
        if (made & (1 << (p - 1))) {
            fu_port(p, UART_T_16550A);
            fu_rate(p, SER_RATE);
            g_srv[p - 1].state = SV_OPEN;
        }
    }
    fu_pump(ser_serve, g_srv);
}

/* Run the script with no server named and check its result and line,
   then that NetBIOS was never looked for and every port is closed. */
static void run_none(int rc, u16 at)
{
    u16 line;
    u8 num;
    u8 p;

    TF_EQ(drive(NULL, &line), rc);
    TF_EQ(line, at);
    TF_EQ(nb_add_name("PROBE", &num), NB_RC_NOT_PRESENT);
    for (p = 1; p <= UART_MAXPORT; p++)
        TF_EQ(fu_isrstate(p), 0);
}

static const char *const k_spass[] = {
    "@COM 1 COM1",
    "> HELLO S",
    "< +OK",
    ">& LOGIN ADMIN SECRET1",
    "@COM 2 com2 38400",
    "> ECHO two",
    "< two",
    "@WAIT 1",
    "<* +O*",
    "@S 1",
    "> BIG",
    "< +DATA",
    "<* R 00 x*x",
    "> BYE",
    "< +OK BYE",
    "@COM 1 COM1",
    "> ECHO again",
    "< again",
    "@HANGUP 1",
    NULL
};

/* Failing serial scripts, each at its line, with its result and why. */
static const char *const k_badport[] = { "@COM 1 COM3", NULL };
static const char *const k_badbaud[] = { "@COM 1 COM1 9601", NULL };
static const char *const k_zbaud[] = { "@COM 1 COM1 0", NULL };
static const char *const k_comwords[] = { "@COM 1 COM1 9600 X", NULL };
static const char *const k_comshort[] = { "@COM 1", NULL };
static const char *const k_comtwice[] = { "@COM 1 COM1", "@COM 1 COM2",
                                          NULL };
static const char *const k_compost[] = { "@COM 1 COM1", "@POST 1", NULL };
static const char *const k_nocall[] = { k_calla, NULL };
static const char *const k_com2[] = { "@COM 1 COM2", NULL };
static const char *const k_com1[] = { "@COM 1 COM1", NULL };
#ifdef COMPAT_HOST
static const char *const k_mute[] = { "@COM 1 COM1", "> DROP", NULL };
#endif

typedef struct {
    const char *const *lines;
    u16 line;
    int rc;
    const char *why;
} sercase;

static const sercase k_sfails[] = {
    { k_badport, 1, INVSCR_FAIL, k_badline },
    { k_badbaud, 1, INVSCR_FAIL, k_badline },
    { k_zbaud, 1, INVSCR_FAIL, k_badline },
    { k_comwords, 1, INVSCR_FAIL, k_badline },
    { k_comshort, 1, INVSCR_FAIL, k_badline },
    { k_comtwice, 2, INVSCR_FAIL, "session already open" },
    { k_compost, 2, INVSCR_FAIL, "not a mailbox session" },
    { k_nocall, 1, INVSCR_CALL, "no server named" }
};

void test_serial(void)
{
    const sercase *c;
    u16 line;
    u8 flags;

    write_script(NULL, k_spass);
    ser_start(3);
    run_none(INVSCR_PASS, 0);
    TF_CHECK(seen("1>& LOGIN ***", 1));
    TF_CHECK(seen("2< two", 1));
    TF_CHECK(!seen("SECRET", 0));
    TF_EQ(g_byes[0], 1);
    TF_EQ(g_byes[1], 1);
    TF_EQ(fu_opened(1, &flags), UART_DEFBAUD);
    TF_EQ(flags, 0);
    TF_EQ(fu_opened(2, &flags), 38400u);

    for (c = k_sfails; c < k_sfails + sizeof k_sfails / sizeof k_sfails[0];
         c++) {
        write_script(NULL, c->lines);
        ser_start(3);
        run_none(c->rc, c->line);
        TF_CHECK(seen(c->why, 1));
    }

    write_script(NULL, k_com2);
    ser_start(1);
    run_none(INVSCR_PORT, 1);
    TF_CHECK(seen("no such port", 1));

    write_script(NULL, k_com1);
    ser_start(3);
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(drive(NULL, &line), INVSCR_PORT);
    TF_EQ(line, 1);
    uart_close(1);
    TF_CHECK(seen("port in use", 1));

#ifdef COMPAT_HOST
    /* DROP gets no answer: the wait runs out, on the virtual clock. */
    write_script(NULL, k_mute);
    ser_start(1);
    run_none(INVSCR_FAIL, 2);
    TF_CHECK(seen("session lost", 1));
#endif
}

/* ---------------------------------------------------------------------
 * The mailbox server.
 * --------------------------------------------------------------------- */

/* The read hook: before each open the interpreter makes, serve the
   request file of MBSTA, if there is one, into its response file; once
   one is served, fail the next g_rdfails reads. */
static int mb_hook(int op)
{
    const char *req;
    u16 got;

    if (!g_inmb && op == CF_OP_READ && g_served != 0 && g_rdfails != 0) {
        g_rdfails--;
        return EDEV;
    }
    if (g_inmb || op != CF_OP_OPEN)
        return 0;
    g_inmb = 1;
    req = tfs_subpath(TMBOX_REQDIR, MBSTA ".REQ");
    if (tfs_exists(req)) {
        got = 0;
        TF_EQ(tfs_read_all(req, g_req, (u16)sizeof g_req, &got), 0);
        TF_EQ(cf_remove(req), 0);
        memset(&g_srv[MBSESS], 0, sizeof g_srv[MBSESS]);
        take(&g_srv[MBSESS], g_req, got);
        TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, MBSTA ".RSP"),
                            g_srv[MBSESS].out, g_srv[MBSESS].olen), 0);
        g_served++;
    }
    g_inmb = 0;
    return 0;
}

static const char *const k_mpass[] = {
    g_mbox,
    ">& HELLO M",
    ">& LOGIN ADMIN SECRET1",
    ">& LIST",
    ">& BYE",
    "@POST 3",
    "@WAIT 3",
    "< +OK",
    "@S 3",
    "@WAIT 3",
    "< +OK",
    "@WAIT 3",
    "< +DATA",
    "< F NAME",
    "< ..DOT",
    "<* *",
    "< .",
    "@WAIT 3",
    "< +OK BYE",
    g_mbox,
    ">& ECHO dropped",
    "@HANGUP 3",
    g_mbox,
    "@POST 3",
    NULL
};

/* Failing mailbox scripts, each at its line, with why. */
static const char *const k_mgt[] = { g_mbox, "> HELLO", NULL };
static const char *const k_mwait[] = { g_mbox, ">& HELLO", "@WAIT 3",
                                       NULL };
static const char *const k_mtwice[] = { g_mbox, g_mbox, NULL };
static const char *const k_msta[] = { "@MBOX 3 B*D .", NULL };
static const char *const k_mshort[] = { "@MBOX 3 MB1", NULL };
static const char *const k_mpost[] = { "@POST 3", NULL };
static const char *const k_mnone[] = { g_mbox, ">& HELLO", "@POST 3",
                                       NULL };
static const char *const k_mread[] = { g_mbox, ">& HELLO M", "@POST 3",
                                       "@WAIT 3", "< +OK", NULL };

/* 1 when the transcript's last two lines are a and b. */
static int ends(const char *a, const char *b)
{
    char last[2][SV_LINESZ];
    FILE *f;
    size_t n;
    int i;

    f = fopen(g_out, "r");
    if (f == NULL)
        return 0;
    last[0][0] = '\0';
    last[1][0] = '\0';
    for (i = 0; fgets(last[i], (int)sizeof last[i], f) != NULL; i ^= 1) {
        n = strlen(last[i]);
        if (n > 0 && last[i][n - 1] == '\n')
            last[i][n - 1] = '\0';
    }
    fclose(f);
    return strcmp(last[i], a) == 0 && strcmp(last[i ^ 1], b) == 0;
}

/* Mailbox posts that fail, and one whose first read fails. */
static void test_mfailed(void)
{
    char rsp[DOS_PATHMAX];

    strcpy(rsp, tfs_subpath(TMBOX_RSPDIR, MBSTA ".RSP"));
    /* The request file the run with no server left. */
    cf_remove(tfs_subpath(TMBOX_REQDIR, MBSTA ".REQ"));
    cf_fault_rd = mb_hook;
#ifdef COMPAT_HOST
    write_script(NULL, k_mread);
    ser_start(0);
    g_served = 0;
    g_rdfails = 0xFFFF;
    run_none(INVSCR_FAIL, 3);
    TF_EQ(g_served, 1);
    TF_CHECK(ends("@POST 3", "mailbox failed: READ-RSP, DOS error 55"));
    TF_CHECK(tfs_exists(rsp));
    TF_EQ(cf_remove(rsp), 0);
#endif
    write_script(NULL, k_mread);
    ser_start(0);
    g_served = 0;
    g_rdfails = 1;
    run_none(INVSCR_PASS, 0);
    TF_EQ(g_served, 1);
    TF_EQ(g_rdfails, 0);
    TF_CHECK(!seen("mailbox failed", 0));
    TF_CHECK(!tfs_exists(rsp));
    cf_fault_rd = NULL;

    write_script(NULL, k_mnone);
    ser_start(0);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, MBSTA ".REQ"), "X\r\n",
                        3), 0);
    run_none(INVSCR_FAIL, 3);
    TF_CHECK(ends("@POST 3", "mailbox failed"));
}

static const sercase k_mfails[] = {
    { k_mgt, 2, INVSCR_FAIL, "> on a mailbox session" },
    { k_mwait, 3, INVSCR_FAIL, "no response pending" },
    { k_mtwice, 2, INVSCR_FAIL, "session already open" },
    { k_msta, 1, INVSCR_FAIL, k_badline },
    { k_mshort, 1, INVSCR_FAIL, k_badline },
    { k_mpost, 1, INVSCR_FAIL, "session not open" }
};

void test_mbox(void)
{
    const sercase *c;

    sprintf(g_mbox, "@MBOX 3 %s %s", MBSTA, tfs_dir());
    tfs_dirs(k_mbdirs, 0);
    if (!TF_EQ(tfs_dirs(k_mbdirs, 1), 0))
        return;
    ser_start(0);
    g_served = 0;
    cf_fault_rd = mb_hook;
    write_script(NULL, k_mpass);
    run_none(INVSCR_PASS, 0);
    TF_EQ(g_served, 1);
    TF_CHECK(seen("3>& LOGIN ***", 1));
    TF_CHECK(seen("3< ..DOT", 1));
    TF_CHECK(seen("3< +OK BYE", 1));
    TF_CHECK(!seen("SECRET", 0));
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_REQDIR, MBSTA ".REQ")));
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_RSPDIR, MBSTA ".RSP")));

    for (c = k_mfails; c < k_mfails + sizeof k_mfails / sizeof k_mfails[0];
         c++) {
        write_script(NULL, c->lines);
        ser_start(0);
        run_none(c->rc, c->line);
        TF_CHECK(seen(c->why, 1));
    }
    cf_fault_rd = NULL;
    TF_EQ(g_served, 1);

#ifdef COMPAT_HOST
    /* No server: the response file never comes, on the virtual clock. */
    write_script(NULL, k_mnone);
    ser_start(0);
    run_none(INVSCR_FAIL, 3);
    TF_CHECK(seen("no response file", 1));
#endif
    test_mfailed();
    tfs_dirs(k_mbdirs, 0);
}

/*
 * T_INVSR3.C - the third part of T_INVSRV (T_INVSRV.C has the usage and
 * the list of checks): the runs with serial ports and the mailbox, the
 * mailbox's login tickets, the failed-login throttle, a log another
 * program makes, and on the host the serial port served during a
 * NetBIOS burst.
 */
#define COMPAT_NETBIOS
#define COMPAT_UART
#define PROTO_TICKETS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "SRVRUN.H"
#include "TMBOX.H"
#include "THROTL.H"
#define TF_PART
#define TF_FILE "T_INVSR3"
#include "TESTFW.H"
#include "FAKENB.H"
#include "FAKEUART.H"
#include "T_FILES.H"
#include "T_INVSRV.H"

#define MBWAIT          64      /* peeks before F3 in a mailbox run     */
#define SERPORT         1       /* the client's port, COM1 (make_com1)   */
#define SERLIMIT        4096    /* peeks before F3 and Y end a serial
                                   run whose client is not done         */
#define STVPASSES       4       /* passes in which a serial request must
                                   be answered during the burst         */
#define STVLIMIT        1024    /* peeks before F3 and Y end the
                                   starvation run, its serial session
                                   still open                           */

/* The serial client's script, as the NetBIOS client's (T_INVSRV.C);
   the session the server opened for it is the first of the run. */
static const char *const s_serial[] = {
    "HELLO LAB1", k_ok,
    "LOGIN ADMIN secret01", "+OK LEVEL=3 *\r\n",
    "ADMIN CLIENTS", "+DATA\r\nF SLOT TYPE STATION USER LEVEL STATE IDLE "
    "SELF\r\nR 1 COM1 LAB1 ADMIN 3 3 0 1\r\n.\r\n",
    "BYE", "+OK*\r\n",
    NULL
};

/* The serial run's event log: its OPEN has no station, HELLO names it
   later. */
static const char *const k_selog[] = {
    "0 START RETROINV", "1 OPEN", "1 REQ HELLO", "1 REQ LOGIN",
    "1 LOGIN ADMIN", "1 REQ ADMIN CLIENTS", "1 REQ BYE", "1 CLOSE",
    "0 STOP 0"
};

/* The mailbox run: the request, the response it must get, and the
   event log, whose MBOX line is the failed rename of OLD.TMP. */
static const char k_req[] = "HELLO MB\r\nLOGIN ADMIN secret01\r\nPING\r\n"
    "BYE\r\nPING\r\n";
static const char k_rsp[] = "+OK\r\n+OK LEVEL=3 *\r\n+OK*\r\n+OK*\r\n";
static const char k_old[] = "+OK\r\n";
static const char *const k_mblog[] = {
    "0 START RETROINV", "0 MBOX 5 OLD", "1 OPEN ADMIN", "1 REQ HELLO",
    "1 REQ LOGIN", "1 LOGIN ADMIN", "1 REQ PING", "1 REQ BYE", "1 CLOSE",
    "0 STOP 0"
};

/* The ticket run: the mailbox client's requests, the responses each
   must get, and the event log; the mailbox's slot follows COM1's. */
static const char k_tkreq1[] = "HELLO TK\r\n"
    "LOGIN ADMIN secret01 TICKET=NEW\r\nBYE\r\n";
static const char k_tkrsp1[] = "+OK\r\n+OK LEVEL=3 * TICKET=*\r\n+OK\r\n";
static const char k_tkrsp2[] = "+OK\r\n+OK LEVEL=3 *\r\n+OK\r\n+OK\r\n";
static const char k_tkrsp3[] = "+OK\r\n-ERR 401 *\r\n";
static const char *const k_tklog[] = {
    "0 START RETROINV",
    "2 OPEN TK", "2 REQ HELLO", "2 REQ LOGIN", "2 LOGIN ADMIN", "2 REQ BYE",
    "2 CLOSE",
    "2 OPEN TK", "2 REQ HELLO", "2 REQ LOGIN", "2 LOGIN ADMIN", "2 REQ PING",
    "2 REQ BYE", "2 CLOSE",
    "2 OPEN TK", "2 REQ HELLO", "2 REQ LOGIN", "2 CLOSE",
    "0 STOP 0"
};

/* The throttle runs.  A serial client's script without MAILBOX=: two
   sessions, the first closed by its third failed login, failing as
   ADMIN THR_FAILS times in all, then the right password refused too. */
extern char t_invsr3_fails_ok[1];
extern char t_invsr3_fails_ok[(THR_FAILS == 5) ? 1 : 2];
static const char *const s_serthr[] = {
    "HELLO LAB1", k_ok,
    "LOGIN ADMIN wrong01", "-ERR 401 *\r\n",
    "LOGIN ADMIN wrong01", "-ERR 401 *\r\n",
    "LOGIN ADMIN wrong01", "-ERR 401 *\r\n",
    "HELLO LAB1", k_ok,
    "LOGIN ADMIN wrong01", "-ERR 401 *\r\n",
    "LOGIN ADMIN wrong01", "-ERR 401 *\r\n",
    "LOGIN ADMIN secret01", "-ERR 401 *\r\n",
    NULL
};

/* A mailbox client's request files, a wrong password and then the
   right one, and the response each gets. */
static const char k_threq[] = "HELLO TH\r\nLOGIN ADMIN wrong01\r\n";
static const char k_thok[] = "HELLO TH\r\nLOGIN ADMIN secret01\r\n";
static const char k_thrsp[] = "+OK\r\n-ERR 401 *\r\n";

/* A NetBIOS and a serial session of a server that keeps tickets, as it
   does with MAILBOX=: neither is offered one. */
static const char *const s_nbtkt[] = {
    "HELLO PEER", k_ok,
    "LOGIN ADMIN secret01 TICKET=NEW",
    "-ERR 102 unexpected key FIELD=TICKET\r\n",
    "BYE", k_ok,
    NULL
};
static const char *const s_sertkt[] = {
    "HELLO LAB1", k_ok,
    "LOGIN ADMIN secret01 TICKET=NEW",
    "-ERR 102 unexpected key FIELD=TICKET\r\n",
    "BYE", k_ok,
    NULL
};

/* The log of a start that found no COM2. */
static const char *const k_pflog[] = { "0 START RETROINV", "0 STOP 10" };

/* NL.LOG as another program makes it, and after the run that finds it
   so. */
static const char k_other[] = "2000-01-01 00:00:00 0 OTHER\r\n";
static const char *const k_nllog[] = {
    "0 OTHER", "0 START RETROINV", "0 STOP 0"
};

#define NSELOG          (sizeof k_selog / sizeof k_selog[0])
#define NTKLOG          (sizeof k_tklog / sizeof k_tklog[0])
#define NPFLOG          (sizeof k_pflog / sizeof k_pflog[0])
#define NMBLOG          (sizeof k_mblog / sizeof k_mblog[0])
#define NNLLOG          (sizeof k_nllog / sizeof k_nllog[0])

/* The serial client arg (a peer) on SERPORT, run from the ports' pump:
   send each request of its script and check its response; the session
   is over once BYE is answered. */
static void ser_client(void *arg)
{
    peer *c;
    char line[48];
    u16 n;

    c = (peer *)arg;
    if (c->ended)
        return;
    if (c->wait) {
        n = fu_peer_recv(SERPORT, c->resp + c->rlen,
                         (u16)(RESPMAX - 1 - c->rlen));
        c->rlen = (u16)(c->rlen + n);
        c->resp[c->rlen] = '\0';
        if (!resp_done(c->resp))
            return;
        if (!TF_CHECK(glob(c->resp, c->script[2 * c->step - 1])))
            printf("  got \"%s\"\n", c->resp);
        c->wait = 0;
        c->ended = (u8)(c->script[2 * c->step] == NULL);
        return;
    }
    strcpy(line, c->script[2 * c->step]);
    strcat(line, "\r\n");
    n = (u16)strlen(line);
    TF_EQ(fu_peer_send(SERPORT, line, n), n);
    c->step++;
    c->rlen = 0;
    c->resp[0] = '\0';
    c->wait = 1;
}

/* The settings line "MAILBOX=" and the scratch directory, or another
   directory dir of it, in a static buffer. */
static const char *mbox_cfg(const char *dir)
{
    static char t[PATHMAX + 16];

    sprintf(t, "MAILBOX=%s%s", tfs_dir(), dir);
    return t;
}

void test_serial(void)
{
    u8 flags;
    u8 num;

    /* A session on COM1 with no NetBIOS: the provider was never asked
       whether it is there, so its commands still fail. */
    reset(s_serial, 0);
    make_com1();
    g_keys.limit = SERLIMIT;
    fu_pump(ser_client, &g_peer);
    put_cfg(tfs_dir(), "SESSIONS=0\r\nSERIAL=COM1\r\nBAUD=19200\r\nRTSCTS=1",
            log_cfg("SE"));
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(fu_opened(1, &flags), 19200u);
    TF_EQ(flags, UART_F_RTSCTS);
    TF_EQ(g_peer.ended, 1);
    TF_EQ(g_peer.step, 4);
    TF_EQ(g_keys.given, 2);
    TF_CHECK(err_is(""));
    TF_EQ(nb_add_name("PROBE", &num), NB_RC_NOT_PRESENT);
    log_is("SE", k_selog, NSELOG);

    /* COM2 missing: COM1, opened first, is closed again, and the log
       has the run's START and STOP. */
    reset(NULL, 0);
    make_com1();
    put_cfg(tfs_dir(), "SESSIONS=0\r\nSERIAL=COM2,COM1", log_cfg("PF"));
    TF_EQ(run(g_mdir), SRV_EXIT_PORT);
    TF_CHECK(err_is("INVSERV: there is no serial port COM2\n"));
    log_is("PF", k_pflog, NPFLOG);

    /* COM1 open already, beside a NetBIOS session. */
    reset(NULL, 0);
    make_com1();
    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_OK);
    g_held = 1;
    put_cfg(tfs_dir(), "SESSIONS=1", "SERIAL=COM1");
    TF_EQ(run(g_mdir), SRV_EXIT_PORT);
    g_held = 0;
    uart_close(1);
    TF_CHECK(err_is("INVSERV: the serial port COM1 is in use\n"));

    /* No mailbox: the name and the port started before it are undone
       (run checks both). */
    reset(NULL, 0);
    make_com1();
    put_cfg(tfs_dir(), "SESSIONS=1\r\nSERIAL=COM1", mbox_cfg("NOBOX"));
    TF_EQ(run(g_mdir), SRV_EXIT_MBOX);
    TF_CHECK(err_is("INVSERV: cannot find the mailbox *NOBOX* or its "
                    "REQUEST and RESPONSE directories\n"));
}

void test_mbox(void)
{
    char t[RESPMAX];
    u16 got;

    /* A request served as its station's session, after the first rename
       of the run, that of a session's leftover OLD.TMP at the start,
       failed; the rename is tried again, and the .RSP left in place. */
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, "ADMIN.REQ"), k_req,
                        (u16)strlen(k_req)), 0);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "OLD.TMP"), k_old,
                        (u16)strlen(k_old)), 0);
    reset(NULL, MBWAIT);
    g_keys.limit = 4 * MBWAIT;
    sprintf(t, "SESSIONS=0\r\n%s", log_cfg("MB"));
    put_cfg(tfs_dir(), t, mbox_cfg(""));
    tfs_fault(TFS_OP(CF_OP_RENAME), 1, 1, CF_EACCES);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    tfs_fault_off();
    TF_EQ(g_keys.given, 2);
    TF_CHECK(err_is(""));
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_REQDIR, "ADMIN.REQ")));
    got = 0;
    TF_EQ(tfs_read_all(tfs_subpath(TMBOX_RSPDIR, "ADMIN.RSP"), t,
                       (u16)(sizeof t - 1), &got), 0);
    t[got] = '\0';
    if (!TF_CHECK(glob(t, k_rsp)))
        printf("  ADMIN.RSP: \"%s\"\n", t);
    TF_EQ(tfs_size(tfs_subpath(TMBOX_RSPDIR, "OLD.RSP")), strlen(k_old));
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_RSPDIR, "OLD.TMP")));
    log_is("MB", k_mblog, NMBLOG);
}

/* The ticket run's client: the ticket its first file was given and the
   files answered so far. */
static struct {
    char t[PROTO_TKTLEN + 1];
    u8   step;
} g_tk;

/* The mailbox client arg (a peer), run from the ports' pump: post a
   file asking for a ticket, then, each time the last one is answered,
   check and remove its response and post the next: the ticket's login
   with BYE TICKET=END, and that login again, refused.  The client is
   done once the third is answered. */
static void tk_client(void *arg)
{
    static const char *const rsps[] = { k_tkrsp1, k_tkrsp2, k_tkrsp3 };
    char r[RESPMAX];
    char q[RESPMAX];
    const char *p;
    peer *c;
    u16 got;

    c = (peer *)arg;
    if (c->ended)
        return;
    if (g_tk.step == 0) {
        TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, "TK.REQ"), k_tkreq1,
                            (u16)strlen(k_tkreq1)), 0);
        g_tk.step = 1;
        return;
    }
    if (!tfs_exists(tfs_subpath(TMBOX_RSPDIR, "TK.RSP")))
        return;
    got = 0;
    TF_EQ(tfs_read_all(tfs_subpath(TMBOX_RSPDIR, "TK.RSP"), r,
                       (u16)(sizeof r - 1), &got), 0);
    r[got] = '\0';
    TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, "TK.RSP")), 0);
    if (!TF_CHECK(glob(r, rsps[g_tk.step - 1])))
        printf("  TK.RSP %u: \"%s\"\n", (unsigned)g_tk.step, r);
    if (g_tk.step == 1) {
        p = strstr(r, "TICKET=");
        if (TF_CHECK(p != NULL && strlen(p) >= 7 + PROTO_TKTLEN))
            txt_copy(g_tk.t, p + 7, (u16)sizeof g_tk.t);
    }
    if (g_tk.step == 3) {
        c->ended = 1;
        return;
    }
    sprintf(q, "HELLO TK\r\nLOGIN ADMIN TICKET=%s\r\nPING\r\n"
            "BYE TICKET=END\r\n", g_tk.t);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, "TK.REQ"), q,
                        (u16)strlen(q)), 0);
    g_tk.step++;
}

/* A mailbox client logs in with its password once, asking for a ticket,
   then with the ticket, which BYE TICKET=END ends, so that it is refused
   the next time, closing the session at once.  The ticket is in no
   line of the event log, which the run's own stop leaves, nor in the
   mailbox once its responses are removed. */
void test_mbtkt(void)
{
    char t[RESPMAX];
    u16 got;

    reset(NULL, 0);
    make_com1();
    g_peer.active = 1;
    g_keys.limit = SERLIMIT;
    memset(&g_tk, 0, sizeof g_tk);
    fu_pump(tk_client, &g_peer);
    sprintf(t, "SESSIONS=0\r\nSERIAL=COM1\r\n%s", log_cfg("TK"));
    put_cfg(tfs_dir(), t, mbox_cfg(""));
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_tk.step, 3);
    TF_EQ(g_peer.ended, 1);
    TF_EQ(g_keys.given, 2);
    TF_CHECK(err_is(""));
    TF_EQ(strlen(g_tk.t), PROTO_TKTLEN);
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_REQDIR, "TK.REQ")));
    log_is("TK", k_tklog, NTKLOG);
    got = 0;
    if (TF_EQ(tfs_read_all(tfs_path("TK", ".LOG"), t, (u16)(sizeof t - 1),
                           &got), 0)) {
        t[got] = '\0';
        TF_CHECK(strstr(t, g_tk.t) == NULL);
    }
}

/* The throttle run's client arg (a peer), run from the ports' pump:
   post THR_FAILS request files with a wrong password, then one with the
   right password, each once the last is answered, its response checked
   and removed.  The client is done once the last is answered. */
static void th_client(void *arg)
{
    char r[RESPMAX];
    const char *q;
    peer *c;
    u16 got;

    c = (peer *)arg;
    if (c->ended)
        return;
    if (g_tk.step > 0) {
        if (!tfs_exists(tfs_subpath(TMBOX_RSPDIR, "TH.RSP")))
            return;
        got = 0;
        TF_EQ(tfs_read_all(tfs_subpath(TMBOX_RSPDIR, "TH.RSP"), r,
                           (u16)(sizeof r - 1), &got), 0);
        r[got] = '\0';
        TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, "TH.RSP")), 0);
        if (!TF_CHECK(glob(r, k_thrsp)))
            printf("  TH.RSP %u: \"%s\"\n", (unsigned)g_tk.step, r);
    }
    if (g_tk.step > THR_FAILS) {
        c->ended = 1;
        return;
    }
    q = g_tk.step < THR_FAILS ? k_threq : k_thok;
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, "TH.REQ"), q,
                        (u16)strlen(q)), 0);
    g_tk.step++;
}

/* THR_FAILS failed logins of ADMIN over sessions, and the server
   refuses the right password after them: on a serial line of a server
   without MAILBOX=, and in request files of their own. */
void test_throttle(void)
{
    reset(s_serthr, 0);
    make_com1();
    g_keys.limit = SERLIMIT;
    fu_pump(ser_client, &g_peer);
    put_cfg(tfs_dir(), "SESSIONS=0", "SERIAL=COM1");
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_peer.ended, 1);
    TF_CHECK(err_is(""));

    reset(NULL, 0);
    make_com1();
    g_peer.active = 1;
    g_keys.limit = SERLIMIT;
    memset(&g_tk, 0, sizeof g_tk);
    fu_pump(th_client, &g_peer);
    put_cfg(tfs_dir(), "SESSIONS=0\r\nSERIAL=COM1", mbox_cfg(""));
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_tk.step, THR_FAILS + 1);
    TF_EQ(g_peer.ended, 1);
    TF_CHECK(err_is(""));
}

/* A server with MAILBOX= keeps tickets, but offers them to no NetBIOS
   or serial session. */
void test_tktrans(void)
{
    reset(s_nbtkt, 0);
    put_cfg(tfs_dir(), mbox_cfg(""), NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_peer.step, 3);
    TF_CHECK(err_is(""));

    reset(s_sertkt, 0);
    make_com1();
    g_keys.limit = SERLIMIT;
    fu_pump(ser_client, &g_peer);
    put_cfg(tfs_dir(), "SESSIONS=0\r\nSERIAL=COM1", mbox_cfg(""));
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_peer.ended, 1);
    TF_EQ(g_peer.step, 3);
    TF_CHECK(err_is(""));
}

#ifdef COMPAT_HOST
/* The starvation run: the serial request sent during the burst, the
   pass it was sent in and the passes until its answer, and the burst's
   answers read by then. */
static struct {
    char resp[RESPMAX];
    u16  rlen;
    u16  at;
    u16  passes;
    u16  burstdone;
    u8   sent;
    u8   answered;
} g_stv;

/* The starvation client arg (the burst's peer), run from the provider's
   pump, so that it acts on every pass whether or not the server polls
   its serial port: the burst, and once the burst is sent, a HELLO on
   COM1 from the line's far end, whose answer it then waits for. */
static void starver(void *arg)
{
    u16 n;

    burster(arg);
    if (g_peer.step == 0)
        return;
    if (g_stv.answered)
        return;
    if (!g_stv.sent) {
        TF_EQ(fu_peer_send(1, "HELLO STV\r\n", 11), 11);
        g_stv.at = g_keys.peeks;
        g_stv.sent = 1;
        return;
    }
    n = fu_peer_recv(1, g_stv.resp + g_stv.rlen,
                     (u16)(RESPMAX - 1 - g_stv.rlen));
    g_stv.rlen = (u16)(g_stv.rlen + n);
    g_stv.resp[g_stv.rlen] = '\0';
    if (resp_done(g_stv.resp)) {
        g_stv.answered = 1;
        g_stv.passes = (u16)(g_keys.peeks - g_stv.at);
        g_stv.burstdone = g_peer.rlen;
    }
}

/* The serial port served on passes busy with NetBIOS: a HELLO on COM1,
   sent as the burst begins, is answered within STVPASSES passes, while
   most of the burst is still to come.  The serial session stays open
   at F3 and Y. */
void test_starve(void)
{
    reset(NULL, 0);
    make_com1();
    g_keys.limit = STVLIMIT;
    g_peer.active = 1;
    fnb_pump(starver, &g_peer);
    memset(&g_stv, 0, sizeof g_stv);
    put_cfg(tfs_dir(), "SERIAL=COM1", NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_EQ(g_peer.step, 2);
    TF_EQ(g_keys.given, 2);
    TF_EQ(g_stv.answered, 1);
    if (!TF_CHECK(glob(g_stv.resp, k_ok)))
        printf("  got \"%s\"\n", g_stv.resp);
    if (!TF_CHECK(g_stv.passes <= STVPASSES))
        printf("  answered after %u passes\n", (unsigned)g_stv.passes);
    TF_CHECK(g_stv.burstdone < NBURST / 2);
}
#endif

/* cf_fault hook, once: at the server's create of the log, which it
   found missing, another program makes the log first. */
static int other_log(int op)
{
    if (op == CF_OP_CREATE) {
        cf_fault = NULL;
        tfs_write_all(tfs_path("NL", ".LOG"), k_other,
                      (u16)(sizeof k_other - 1));
    }
    return 0;
}

/* A log another program makes between the open that finds it missing
   and the create is opened as it is, its lines kept. */
void test_racelog(void)
{
    put_cfg(tfs_dir(), log_cfg("NL"), NULL);
    cf_remove(tfs_path("NL", ".LOG"));
    reset(NULL, 0);
    cf_fault = other_log;
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    TF_CHECK(cf_fault == NULL);
    log_is("NL", k_nllog, NNLLOG);
}

/*
 * T_NBCLI.C - unit test for the client's NetBIOS session (NBCLI.C) over
 * the simulated provider (FAKENB.C).
 *
 * usage: T_NBCLI
 *
 * Checks (with T_NBCL2.C):
 * - names: a name given is upper-cased and cut to NB_NAMELEN - 1
 *   characters, added, and deleted again by nbcli_close; one in the
 *   table already (NB_RC_DUPNAME) is used and kept; one of another
 *   station gives NBCLI_ENAME with NB_RC_NAMEINUSE, a full table
 *   NBCLI_ENAME with NB_RC_NAMEFULL; without a name, the DOS machine
 *   name when there is one, else "RINV" and four hex digits; where
 *   there is no machine name, an open whose first names (those of the
 *   ticks around it) are taken still finds a free one; on the host (its
 *   timer counts calls) each try's name is known, and a taken one is
 *   tried again with a different name, NB_NAMETRIES times in all,
 *   the last try's code given;
 * - a server name of NB_NAMELEN - 1 characters is called whole, a
 *   longer one cut to it;
 * - open failures: no station with the server's name (NB_RC_NOANSWER),
 *   no LISTEN (NB_RC_REJECTED), an unusable local name (NB_RC_NONAME)
 *   and a CALL timing out or aborted NBCLI_CALLTRIES times
 *   (NB_RC_TIMEOUT, NB_RC_ABORTED) give NBCLI_ECALL, and memory running
 *   out at any allocation, nb_present's NB_PRES_NOMEM included,
 *   NBCLI_ENOMEM, nb_present's 0 NBCLI_ENONB; each leaves *out NULL, the
 *   name it added deleted, no session, set or memory held; a CALL that
 *   timed out or was aborted fewer times is made again and opens the
 *   session, a refused one is not made again;
 * - requests, responses, framing, over-long lines, blank lines, a
 *   failing line callback, a RECEIVE posted before each response comes
 *   (none late, fnb_late), the waits of RECEIVE and SEND, ended sessions
 *   hung up only while the provider still holds them, commands stuck
 *   at the close and the NCB limit of T_NBCL2.C.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKENB.H"
#include "NBCLI.H"
#include "T_NBCLI.H"

#define EVMAX           8       /* events of srv_at                     */
#define OUTQ            128     /* messages queued                      */
#define FIRSTMAX        63      /* most ticks an open reads before its
                                   first generated name                 */
#define STEPMAX         8       /* most ticks between two tries' names  */
#define LONGSRV         "LONGSERVERNAME1"  /* NB_NAMELEN - 1 characters */

u16 g_pumps;
u8 g_slsn;
u8 g_clsn;
u8 g_rxend;
char g_rx[RXMAX];
u16 g_nrx;
u16 g_nmsg;
char g_ln[LNMAX];
u16 g_nln;
u16 g_lines;
u16 g_failat;
u8 g_lbad;
static nba *g_srv;
static cmem_blk *g_sb;          /* SRX's slot, then the SEND slots      */
static u8 g_inpump;
static u8 g_rxon;               /* the server receives                  */
static u8 g_rxpost;             /* its RECEIVE is posted                */
static u8 g_rxbig;              /* a message did not fit NB_MSGMAX      */
static u8 g_shung;              /* the server hung up                   */
static cmem_blk *g_ob;          /* messages queued, oldest first        */
static u16 g_qlen[OUTQ];
static u16 g_qn;
static u16 g_qhead;
static u16 g_qoff;
static u16 g_qend;
static u16 g_ev_at[EVMAX];
static u8 g_ev[EVMAX];
static const char *g_ev_s[EVMAX];
static u16 g_nev;
static u16 g_fnth;              /* the cmem_alloc call that fails       */
static u16 g_fcount;
#ifdef COMPAT_HOST
static u16 g_held;              /* near blocks held at fresh            */
#endif

/* Post the server's SENDs while a slot is free. */
static void flush(void)
{
    u16 h;
    u16 len;

    while (g_qn > 0 && g_slsn != 0) {
        for (h = 1; h <= SQ && nba_poll(g_srv, h) == NB_RC_PENDING; h++)
            ;
        if (h > SQ)
            return;
        len = g_qlen[g_qhead];
        cmem_copy(g_sb, (u16)(h * NB_MSGMAX), g_ob, g_qoff, len);
        /* A SEND refused drops every message queued: g_qn reaches 0. */
        if (nba_send(g_srv, h, g_slsn, g_sb, (u16)(h * NB_MSGMAX), len) !=
            NB_OK)
            g_qn = 1;
        g_qoff = (u16)(g_qoff + len);
        g_qhead++;
        if (--g_qn == 0)
            g_qhead = g_qoff = g_qend = 0;
    }
}

/* Take what the server's RECEIVE got and post it again. */
static void receive(void)
{
    u8 rc;
    u16 len;

    if (g_rxpost) {
        rc = nba_poll(g_srv, SRX);
        if (rc == NB_RC_PENDING)
            return;
        g_rxpost = 0;
        if (rc == NB_OK) {
            len = nba_len(g_srv, SRX);
            if (len > RXMAX - 1 - g_nrx)
                len = (u16)(RXMAX - 1 - g_nrx);
            cmem_get(g_sb, 0, g_rx + g_nrx, len);
            g_nrx = (u16)(g_nrx + len);
            g_rx[g_nrx] = '\0';
            g_nmsg++;
        } else if (rc == NB_RC_INCOMPLETE) {
            g_rxbig = 1;
        } else {
            g_rxend = rc;
        }
    }
    if (g_rxon && g_rxend == 0 && g_slsn != 0 &&
        nba_recv(g_srv, SRX, g_slsn, g_sb, 0, NB_MSGMAX) == NB_OK)
        g_rxpost = 1;
}

static void event(u16 i)
{
    switch (g_ev[i]) {
    case EV_RECV:
        g_rxon = 1;
        break;
    case EV_HANGUP:
        srv_hangup();
        break;
    case EV_ABORT:
        fnb_abort(g_slsn);
        break;
    default:
        srv_text(g_ev_s[i]);
        break;
    }
}

/* The pump, given &g_pumps to count its calls in. */
static void pump(void *arg)
{
    u16 i;

    if (g_inpump)
        return;
    g_inpump = 1;
    (*(u16 *)arg)++;
    for (i = 0; i < g_nev; i++)
        if (g_ev_at[i] == g_pumps)
            event(i);
    receive();
    flush();
    g_inpump = 0;
}

void srv_run(void)
{
    if (g_inpump)
        return;
    g_inpump = 1;
    receive();
    flush();
    g_inpump = 0;
}

void srv_queue(const char *s, u16 len)
{
    if (!TF_CHECK(g_qhead + g_qn < OUTQ && len <= OUTMAX - g_qend))
        return;
    cmem_put(g_ob, g_qend, s, len);
    g_qend = (u16)(g_qend + len);
    g_qlen[g_qhead + g_qn] = len;
    g_qn++;
    srv_run();
}

void srv_text(const char *s)
{
    srv_queue(s, (u16)strlen(s));
}

void srv_recv(int on)
{
    g_rxon = (u8)on;
    if (!on && g_rxpost) {
        TF_EQ(nba_cancel(g_srv, SRX), NB_OK);
        g_rxpost = 0;
    }
    srv_run();
}

u8 srv_sendrc(void)
{
    u8 rc;

    g_inpump = 1;
    rc = nba_poll(g_srv, 1);
    g_inpump = 0;
    return rc;
}

void srv_hangup(void)
{
    u8 in;

    in = g_inpump;
    g_inpump = 1;
    nb_hangup(g_slsn);
    g_shung = 1;
    g_inpump = in;
}

void srv_at(u16 at, u8 ev, const char *s)
{
    if (at == 0) {
        g_nev = 0;
        g_pumps = 0;
        return;
    }
    if (!TF_CHECK(g_nev < EVMAX))
        return;
    g_ev_at[g_nev] = at;
    g_ev[g_nev] = ev;
    g_ev_s[g_nev] = s;
    g_nev++;
}

int take(void *arg, const char *line, u16 len)
{
    if (arg != (void *)&g_lines || len > NBCLI_MAXRESP || line[len] != '\0')
        g_lbad = 1;
    if (g_nln + len + 1 < LNMAX) {
        memcpy(g_ln + g_nln, line, len);
        g_nln = (u16)(g_nln + len);
        g_ln[g_nln++] = '|';
        g_ln[g_nln] = '\0';
    } else {
        g_lbad = 1;
    }
    g_lines++;
    return g_lines == g_failat;
}

void clear(void)
{
    g_nln = 0;
    g_ln[0] = '\0';
    g_lines = 0;
    g_failat = 0;
    g_nrx = 0;
    g_rx[0] = '\0';
    g_nmsg = 0;
}

int resp(nbcli *n, u16 w)
{
    clear();
    return nbcli_response(n, take, &g_lines, w);
}

int sendl(nbcli *n, const char *s)
{
    return nbcli_send(n, s, (u16)strlen(s));
}

void fresh(void)
{
    u8 num;

    fnb_reset();
    TF_EQ(nb_present(), 1);
#ifdef COMPAT_HOST
    g_held = tm_held();
#endif
    TF_EQ(nb_add_name(SRV, &num), NB_OK);
    TF_EQ(nba_open(&g_srv, SQ + 1), 0);
    TF_EQ(nba_listen(g_srv, SRX, SRV, "*", 0, 0), NB_OK);
    g_slsn = 0;
    g_clsn = 0;
    g_rxon = 0;
    g_rxpost = 0;
    g_rxend = 0;
    g_rxbig = 0;
    g_shung = 0;
    g_qn = g_qhead = g_qoff = g_qend = 0;
    g_lbad = 0;
    srv_at(0, 0, NULL);
    clear();
    fnb_pump(pump, &g_pumps);
}

nbcli *connect(const char *local)
{
    nbcli *n;
    u8 nbrc;
    u8 rc;
    char who[NB_NAMELEN];

    TF_EQ(nbcli_open(&n, "srv", local, &nbrc), NBCLI_OK);
    TF_EQ(nbrc, NB_OK);
    if (n == NULL) {
        printf("T_NBCLI: no session, run stopped\n");
        exit(tf_done() + 1);
    }
    g_inpump = 1;
    while ((rc = nba_poll(g_srv, SRX)) == NB_RC_PENDING)
        ;
    g_inpump = 0;
    TF_EQ(rc, NB_OK);
    g_slsn = nba_lsn(g_srv, SRX);
    g_clsn = (u8)(g_slsn + 1);
    nba_callname(g_srv, SRX, who);
    TF_STREQ(who, nbcli_name(n));
    TF_EQ(nbcli_closed(n), 0);
    g_rxon = 1;
    srv_run();
    return n;
}

void finish(void)
{
    u16 h;

    fnb_pump(NULL, NULL);
    if (g_slsn != 0 && !g_shung)
        nb_hangup(g_slsn);
    for (h = 0; h <= SQ; h++)
        TF_EQ(nba_cancel(g_srv, h), NB_OK);
    TF_EQ(nba_close(g_srv), 0);
    TF_EQ(nb_delete_name(SRV), NB_OK);
    TF_EQ(g_rxbig, 0);
    TF_EQ(g_lbad, 0);
    TF_EQ(fnb_pending(), 0);
    TF_EQ(fnb_sets(), 0);
    TF_EQ(fnb_names(), 0);
    TF_EQ(fnb_sessions(), 0);
    fnb_reset();
#ifdef COMPAT_HOST
    TM_HELD(g_held);
#endif
}

/* An open that must fail with rc and the NetBIOS code nbrc, holding no
   name beyond the names noted before it, nor a set of its own. */
static void refused(const char *server, const char *local, int rc, u8 nbrc)
{
    nbcli *n;
    u8 got;
    u16 names;
    u16 sets;

    names = fnb_names();
    sets = fnb_sets();
    TF_EQ(nbcli_open(&n, server, local, &got), rc);
    TF_CHECK(n == NULL);
    TF_EQ(got, nbrc);
    TF_EQ(fnb_names(), names);
    TF_EQ(fnb_sets(), sets);
}

/* The name n with every ASCII letter upper-cased, cut to NB_NAMELEN - 1
   characters, into out (NB_NAMELEN bytes). */
static void upname(char *out, const char *n)
{
    u16 i;
    char c;

    for (i = 0; i < NB_NAMELEN - 1 && n[i] != '\0'; i++) {
        c = n[i];
        out[i] = (char)TXT_UPPER(c);
    }
    out[i] = '\0';
}

/* Names given, and names taken. */
static void test_names(void)
{
    nbcli *n;
    u8 num;
    u16 i;
    char nm[NB_NAMELEN];

    fresh();
    n = connect("cli");
    TF_STREQ(nbcli_name(n), "CLI");
    TF_EQ(fnb_names(), 2);
    TF_EQ(nb_add_name("CLI", &num), NB_RC_DUPNAME);
    nbcli_close(n);
    TF_EQ(fnb_names(), 1);
    finish();
    fresh();
    n = connect("abcdefghijklmnopqrstuvwxyz");
    TF_STREQ(nbcli_name(n), "ABCDEFGHIJKLMNO");
    nbcli_close(n);
    finish();
    /* A name in the local name table already is used and kept. */
    fresh();
    TF_EQ(nb_add_name("CLI", &num), NB_OK);
    n = connect("Cli");
    TF_STREQ(nbcli_name(n), "CLI");
    nbcli_close(n);
    TF_EQ(fnb_names(), 2);
    TF_EQ(nb_delete_name("CLI"), NB_OK);
    finish();
    /* Another station's name is not replaced by another. */
    fresh();
    fnb_dupname("CLI");
    refused("srv", "cli", NBCLI_ENAME, NB_RC_NAMEINUSE);
    TF_EQ(nba_poll(g_srv, SRX), NB_RC_PENDING);
    finish();
    /* A full table, for a name given and for the default one. */
    fresh();
    for (i = 1; i < FNB_NAMES; i++) {
        sprintf(nm, "N%u", (unsigned)i);
        TF_EQ(nb_add_name(nm, &num), NB_OK);
    }
    refused("srv", "cli", NBCLI_ENAME, NB_RC_NAMEFULL);
    refused("srv", NULL, NBCLI_ENAME, NB_RC_NAMEFULL);
    for (i = 1; i < FNB_NAMES; i++) {
        sprintf(nm, "N%u", (unsigned)i);
        TF_EQ(nb_delete_name(nm), NB_OK);
    }
    finish();
}

/* The value of the four hex digits after "RINV" in s. */
static u16 hexof(const char *s)
{
    u16 v;
    u16 i;
    char c;

    v = 0;
    for (i = 4; i < 8; i++) {
        c = s[i];
        v = (u16)(v * 16 + (TXT_ISDIGIT(c) ? c - '0' : c - 'A' + 10));
    }
    return v;
}

/* The generated name whose digits are v, into out (NB_NAMELEN bytes). */
static void gname(char *out, u16 v)
{
    sprintf(out, "RINV%04X", (unsigned)v);
}

/* The default name: the machine name, else RINV and four hex digits. */
static void test_default(void)
{
    nbcli *n;
    u8 num;
    u16 i;
    u16 t;
    const char *s;
    char mach[DOS_MACHLEN + 1];
    char want[NB_NAMELEN];

    fresh();
    if (dos_machine_name(mach) == 0) {
        upname(want, mach);
        n = connect(NULL);
        TF_STREQ(nbcli_name(n), want);
        TF_EQ(fnb_names(), 2);
        nbcli_close(n);
        TF_EQ(fnb_names(), 1);
        finish();
        fresh();
        TF_EQ(nb_add_name(want, &num), NB_OK);
        n = connect(NULL);
        TF_STREQ(nbcli_name(n), want);
        nbcli_close(n);
        TF_EQ(nb_delete_name(want), NB_OK);
    } else {
        n = connect(NULL);
        s = nbcli_name(n);
        TF_EQ(strlen(s), 8);
        TF_CHECK(strncmp(s, "RINV", 4) == 0);
        for (i = 4; i < 8 && s[i] != '\0'; i++)
            TF_CHECK(TXT_ISDIGIT(s[i]) || (s[i] >= 'A' && s[i] <= 'F'));
        TF_EQ(fnb_names(), 2);
        nbcli_close(n);
        TF_EQ(fnb_names(), 1);
        /* Names taken for the ticks around the open: another is tried. */
        finish();
        fresh();
        t = (u16)tmr_ticks();
        for (i = 0; i < NB_NAMETRIES - 1; i++) {
            gname(want, (u16)(t + i));
            fnb_dupname(want);
        }
        n = connect(NULL);
        TF_CHECK((u16)(hexof(nbcli_name(n)) - t) >= NB_NAMETRIES - 1);
        nbcli_close(n);
    }
    finish();
}

#ifdef COMPAT_HOST
/* Take the generated names of tries 0 .. tries - 1 (try i: first +
   i * step), those of the tries in mask by another station, the rest
   into the local name table. */
static void takes(u16 first, u16 step, u16 tries, u16 mask)
{
    u16 i;
    u8 num;
    char nm[NB_NAMELEN];

    for (i = 0; i < tries; i++) {
        gname(nm, (u16)(first + i * step));
        if (mask & (1u << i))
            fnb_dupname(nm);
        else
            TF_EQ(nb_add_name(nm, &num), NB_OK);
    }
}

/* Delete the names takes put into the local name table. */
static void untake(u16 first, u16 step, u16 tries, u16 mask)
{
    u16 i;
    char nm[NB_NAMELEN];

    for (i = 0; i < tries; i++)
        if (!(mask & (1u << i))) {
            gname(nm, (u16)(first + i * step));
            TF_EQ(nb_delete_name(nm), NB_OK);
        }
}

/* Generated names tried again while taken.  The host's timer counts
   its calls, so the open repeats the ticks of a probe made the same
   way: the probe gives the first name's offset from the tick read just
   before the open, and a second open whose first name is taken gives
   the step from one try to the next. */
static void test_retry(void)
{
    nbcli *n;
    u16 t;
    u16 d;
    u16 k;
    u16 all;
    char nm[NB_NAMELEN];

    all = (u16)((1u << NB_NAMETRIES) - 1);
    fresh();
    t = (u16)tmr_ticks();
    n = connect(NULL);
    d = (u16)(hexof(nbcli_name(n)) - t);
    TF_CHECK(d >= 1 && d <= FIRSTMAX);
    nbcli_close(n);
    finish();
    /* The first name in the local name table: the next try. */
    fresh();
    t = (u16)(tmr_ticks() + d);
    takes(t, 1, 1, 0);
    n = connect(NULL);
    k = (u16)(hexof(nbcli_name(n)) - t);
    TF_CHECK(k >= 1 && k <= STEPMAX);
    TF_EQ(fnb_names(), 3);
    nbcli_close(n);
    untake(t, 1, 1, 0);
    finish();
    /* The first name another station's. */
    fresh();
    t = (u16)(tmr_ticks() + d);
    takes(t, k, 1, 1);
    n = connect(NULL);
    gname(nm, (u16)(t + k));
    TF_STREQ(nbcli_name(n), nm);
    nbcli_close(n);
    finish();
    /* Every try but the last taken: the last succeeds. */
    fresh();
    t = (u16)(tmr_ticks() + d);
    takes(t, k, NB_NAMETRIES - 1, 0x55);
    n = connect(NULL);
    gname(nm, (u16)(t + k * (NB_NAMETRIES - 1)));
    TF_STREQ(nbcli_name(n), nm);
    nbcli_close(n);
    untake(t, k, NB_NAMETRIES - 1, 0x55);
    finish();
    /* Every try taken: the last try's code. */
    fresh();
    t = (u16)(tmr_ticks() + d);
    takes(t, k, NB_NAMETRIES, all >> 1);
    refused("srv", NULL, NBCLI_ENAME, NB_RC_DUPNAME);
    untake(t, k, NB_NAMETRIES, all >> 1);
    finish();
    fresh();
    t = (u16)(tmr_ticks() + d);
    takes(t, k, NB_NAMETRIES, (u16)(1u << (NB_NAMETRIES - 1)));
    refused("srv", NULL, NBCLI_ENAME, NB_RC_NAMEINUSE);
    untake(t, k, NB_NAMETRIES, (u16)(1u << (NB_NAMETRIES - 1)));
    finish();
}
#endif

/* A pump that posts the server's LISTEN at its first call, as a server
   that listens again between a client's tries; it counts its calls in
   the u16 at arg. */
static void relisten(void *arg)
{
    if ((*(u16 *)arg)++ == 0)
        TF_EQ(nba_listen(g_srv, SRX, SRV, "*", 0, 0), NB_OK);
}

/* The CALL refused, or timed out or aborted every time; a CALL that
   timed out or was aborted made again. */
static void test_call(void)
{
    nbcli *n;
    u8 num;
    u8 lsn;

    fresh();
    fnb_call_timeouts(NBCLI_CALLTRIES - 1);
    n = connect("cli");
    nbcli_close(n);
    finish();
    fresh();
    fnb_call_timeouts(NBCLI_CALLTRIES);
    refused("srv", "cli", NBCLI_ECALL, NB_RC_TIMEOUT);
    TF_EQ(fnb_sessions(), 0);
    finish();
    fresh();
    fnb_call_fail(NBCLI_CALLTRIES - 1, NB_RC_ABORTED);
    n = connect("cli");
    nbcli_close(n);
    finish();
    fresh();
    fnb_call_fail(NBCLI_CALLTRIES, NB_RC_ABORTED);
    refused("srv", "cli", NBCLI_ECALL, NB_RC_ABORTED);
    TF_EQ(fnb_sessions(), 0);
    finish();
    /* nb_present finding no memory, then no provider. */
    fresh();
    fnb_present(NB_PRES_NOMEM);
    refused("srv", "cli", NBCLI_ENOMEM, NB_OK);
    fnb_present(0);
    refused("srv", "cli", NBCLI_ENONB, NB_OK);
    fnb_present(1);
    TF_EQ(nb_present(), 1);
    finish();
    fresh();
    refused("nosuch", "cli", NBCLI_ECALL, NB_RC_NOANSWER);
    TF_EQ(fnb_sessions(), 0);
    finish();
    /* A CALL refused is not made again, though the server listens
       again before a second try could be made. */
    fresh();
    TF_EQ(nba_cancel(g_srv, SRX), NB_OK);
    g_pumps = 0;
    fnb_pump(relisten, &g_pumps);
    refused("srv", "cli", NBCLI_ECALL, NB_RC_REJECTED);
    fnb_pump(NULL, NULL);
    TF_EQ(g_pumps, 1);
    TF_EQ(nba_poll(g_srv, SRX), NB_RC_PENDING);
    TF_EQ(fnb_sessions(), 0);
    finish();
    /* A name that open ends use, deleted: in the table, unusable. */
    fresh();
    TF_EQ(nb_add_name("CLI", &num), NB_OK);
    TF_EQ(nb_call("CLI", SRV, &lsn, 0, 0), NB_OK);
    TF_EQ(nb_delete_name("CLI"), NB_RC_DELPEND);
    refused("srv", "cli", NBCLI_ECALL, NB_RC_NONAME);
    TF_EQ(fnb_sessions(), 2);
    TF_EQ(nb_hangup(lsn), NB_OK);
    TF_EQ(nb_hangup(nba_lsn(g_srv, SRX)), NB_RC_SESSCLOSED);
    TF_EQ(fnb_names(), 1);
    finish();
}

/* A server name of NB_NAMELEN - 1 characters, upper-cased, is called
   whole, and a longer one is cut to it. */
static void test_server_name(void)
{
    static const char *const names[] = {
        "longservername1", "longservername1x"
    };
    nbcli *n;
    nba *set;
    u8 num;
    u8 nbrc;
    int i;

    fresh();
    TF_EQ(nb_add_name(LONGSRV, &num), NB_OK);
    TF_EQ(nba_open(&set, 1), 0);
    for (i = 0; i < 2; i++) {
        TF_EQ(nba_listen(set, 0, LONGSRV, "*", 0, 0), NB_OK);
        TF_EQ(nbcli_open(&n, names[i], "cli", &nbrc), NBCLI_OK);
        TF_EQ(nbrc, NB_OK);
        TF_EQ(nba_poll(set, 0), NB_OK);
        nbcli_close(n);
        TF_EQ(nb_hangup(nba_lsn(set, 0)), NB_RC_SESSCLOSED);
    }
    TF_EQ(nba_close(set), 0);
    TF_EQ(nb_delete_name(LONGSRV), NB_OK);
    finish();
}

static int fault(u16 size)
{
    TF_CHECK(size > 0);
    return ++g_fcount == g_fnth;
}

/* One open with the nth allocation failing (the nth malloc on the host
   when heap is 1, else the nth cmem_alloc); 1 when it succeeded. */
static int nomem(u16 nth, int heap)
{
    nbcli *n;
    u8 nbrc;
    int rc;
    int called;

    fresh();
    g_fcount = 0;
    g_fnth = heap ? 0 : nth;
    cmem_fault = fault;
#ifdef COMPAT_HOST
    tm_fail(heap ? nth : 0);
#endif
    rc = nbcli_open(&n, "srv", "cli", &nbrc);
    cmem_fault = NULL;
#ifdef COMPAT_HOST
    tm_fail(0);
#endif
    if (rc == NBCLI_OK) {
        g_inpump = 1;
        TF_EQ(nba_poll(g_srv, SRX), NB_OK);
        g_inpump = 0;
        g_slsn = nba_lsn(g_srv, SRX);
        nbcli_close(n);
        finish();
        return 1;
    }
    TF_EQ(rc, NBCLI_ENOMEM);
    TF_CHECK(n == NULL);
    TF_EQ(nbrc, NB_OK);
    TF_EQ(fnb_names(), 1);
    TF_EQ(fnb_sets(), 1);
    g_inpump = 1;
    called = nba_poll(g_srv, SRX) == NB_OK;
    g_inpump = 0;
    if (called)
        g_slsn = nba_lsn(g_srv, SRX);
    TF_EQ(fnb_sessions(), called ? 1 : 0);
    finish();
    return 0;
}

/* Memory running out at each allocation of the open in turn. */
static void test_nomem(void)
{
    u16 nth;

    for (nth = 1; nth < 20 && !nomem(nth, 0); nth++)
        ;
    TF_CHECK(nth > 1 && nth < 20);
#ifdef COMPAT_HOST
    for (nth = 1; nth < 20 && !nomem(nth, 1); nth++)
        ;
    TF_CHECK(nth > 1 && nth < 20);
#endif
}

int main(void)
{
    tf_begin("T_NBCLI");
    cmem_init();
    g_sb = cmem_alloc((SQ + 1) * NB_MSGMAX);
    g_ob = cmem_alloc(OUTMAX);
    if (!TF_CHECK(g_sb != NULL && g_ob != NULL))
        return tf_done();
    test_names();
    test_default();
#ifdef COMPAT_HOST
    test_retry();
#endif
    test_call();
    test_server_name();
    test_nomem();
    test_nbcl2();
    fnb_reset();
    cmem_free(g_sb);
    cmem_free(g_ob);
    return tf_done();
}

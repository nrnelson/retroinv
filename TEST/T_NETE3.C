/*
 * T_NETE3.C - the third part of the T_NETE test (T_NETE.H): the
 * clients' helpers, the same bytes over NET as over LOCAL, and main,
 * which runs the phases.
 *
 * Checks:
 * - the request script of TEST/T_INV.SH, fed in pieces with link_feed
 *   as INV /CMD feeds its input, gives over NET exactly the bytes of
 *   that script's golden file, and over LOCAL, on a fresh data
 *   directory with the same fixed clock, the same bytes; each session
 *   ends at BYE with LINK_CLOSED, and the request after it is not
 *   answered;
 * - a second session of each exports AUDIT, with the same responses,
 *   into two files that hold the same bytes, so every change was
 *   audited alike, the station included.  Over NET the station is the
 *   name the link called from, over LOCAL what HELLO says, so each
 *   LOCAL session says HELLO with the name of its NET counterpart.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"
#include "T_NETE.H"

#define NTR             4       /* transcripts: NET then LOCAL, each a
                                   script and an export                 */
#define NTEXT           (NTR + 2) /* TSC_OUTSZ buffers: g_tr, golden,
                                   script                               */
#define AUDNET          "NETAUD"        /* the stems of the exports     */
#define AUDLOC          "LOCAUD"

static const link_clock g_clk = { TSC_CLOCK };
static const char *const k_csvs[] = { AUDNET ".CSV", AUDLOC ".CSV", NULL };

extern char nete_script_fits[1];
extern char nete_script_fits[(TSC_OUTSZ >= TSC_SIZE) ? 1 : 2];

static tn_text g_tr[NTR];
static tn_text g_gold;
static char *g_script;

/* A NET link to the server, its LISTENs posted first; NULL when the
   open failed. */
link *cli_open(void)
{
    link *l;
    u8 rc;

    srv_run();
    l = NULL;
    rc = NB_RC_PENDING;
    TF_EQ(link_open_net(&l, TSC_SRV, &rc), 0);
    TF_EQ(rc, NB_OK);
    return l;
}

/* Send line on l, its response appended to t: link_send's result. */
int talk(link *l, const char *line, tn_text *t)
{
    return link_send(l, line, (u16)strlen(line), tsc_take, t);
}

/* Send line on l and check that the result is want: the response, in
   g_rep. */
const char *ask(link *l, const char *line, int want)
{
    g_rep.n = 0;
    g_rep.b[0] = '\0';
    TF_EQ(talk(l, line, &g_rep), want);
    return g_rep.b;
}

/* Feed text to l with link_feed and check that the result is want: the
   responses, in g_rep. */
const char *feed(link *l, const char *text, int want)
{
    g_rep.n = 0;
    g_rep.b[0] = '\0';
    TF_EQ(link_feed(l, text, (u16)strlen(text), tsc_take, &g_rep), want);
    return g_rep.b;
}

/* A second session on l, as the station station: LOGIN, EXPORT AUDIT
   to the scratch file stem.CSV and BYE, the responses appended to t.
   The audit holds fewer rows than one EXPORT request examines, so that
   request writes the whole file. */
static void export_audit(link *l, const char *station, const char *stem,
                         tn_text *t)
{
    char line[LINK_MAXLINE + 1];

    strcpy(line, "HELLO ");
    strcat(line, station);
    TF_EQ(talk(l, line, t), LINK_OK);
    TF_EQ(talk(l, "LOGIN ADMIN " TSC_PW, t), LINK_OK);
    strcpy(line, "EXPORT AUDIT FILE=");
    strcat(line, tfs_path(stem, ".CSV"));
    TF_EQ(talk(l, line, t), LINK_OK);
    TF_EQ(talk(l, "BYE", t), LINK_CLOSED);
    TF_CHECK(strstr(t->b, "-ERR") == NULL);
}

/* The NET run, then the LOCAL one: the responses of each session into
   g_tr, its export into the .CSV file of stem AUDNET or AUDLOC. */
static void runs(void)
{
    char name[2][LINK_STALEN + 1];
    char why[TFS_WHYSZ];
    link *l;
    u16 n;
    int i;

    if (!srv_begin(""))
        return;
    for (i = 0; i < 2; i++) {
        l = cli_open();
        if (l == NULL)
            return;
        strcpy(name[i], link_netname(l));
        if (i == 0) {
            n = tsc_script(g_script, name[0]);
            tsc_run(l, g_script, n, &g_tr[0]);
        } else {
            export_audit(l, name[1], AUDNET, &g_tr[1]);
        }
        TF_EQ(link_close(l), 0);
        srv_until(0);
    }
    TF_EQ(g_opens, 2);
    srv_end();

    te_remove(tfs_dir(), NULL);
    for (i = 0; i < 2; i++) {
        why[0] = '\0';
        if (!TF_EQ(link_open_local(&l, tfs_dir(), &g_clk, why,
                                   (u16)sizeof why), 0)) {
            printf("link_open_local: %s\n", why);
            return;
        }
        if (i == 0)
            tsc_run(l, g_script, n, &g_tr[2]);
        else
            export_audit(l, name[1], AUDLOC, &g_tr[3]);
        TF_EQ(link_close(l), 0);
    }
}

void test_same(const char *golden)
{
    char *pool;
    u16 i;

    pool = (char *)malloc(NTEXT * TSC_OUTSZ);
    if (!TF_CHECK(pool != NULL))
        return;
    for (i = 0; i < NTR; i++) {
        g_tr[i].b = pool + i * TSC_OUTSZ;
        g_tr[i].n = 0;
        g_tr[i].b[0] = '\0';
    }
    g_gold.b = pool + NTR * TSC_OUTSZ;
    g_gold.n = 0;
    g_script = pool + (NTR + 1) * TSC_OUTSZ;
    if (!TF_EQ(tfs_read_all(golden, g_gold.b, TSC_OUTSZ - 1, &g_gold.n),
               0) ||
        !TF_EQ(tfs_size(golden), g_gold.n))
        printf("cannot read %s\n", golden);
    te_remove(tfs_dir(), k_csvs);
    runs();
    tsc_same(&g_tr[0], &g_gold, "NET script and golden");
    tsc_same(&g_tr[2], &g_gold, "LOCAL script and golden");
    tsc_same(&g_tr[0], &g_tr[2], "NET and LOCAL script");
    tsc_same(&g_tr[1], &g_tr[3], "NET and LOCAL export");
    TF_CHECK(tfs_same(tfs_path(AUDNET, ".CSV"), tfs_path(AUDLOC, ".CSV")));
    te_remove(tfs_dir(), k_csvs);
    free(pool);
}

int main(int argc, char **argv)
{
    char golden[DOS_PATHMAX];

    cmem_init();
    tf_begin("T_NETE");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    tsc_golden(golden, argc, argv);
    g_rep.b = (char *)malloc(TSC_OUTSZ);
    if (!TF_CHECK(g_rep.b != NULL))
        return tf_done();
    test_same(golden);
    test_two();
    test_shutdown();
    te_remove(tfs_dir(), NULL);
    free(g_rep.b);
    return tf_done();
}

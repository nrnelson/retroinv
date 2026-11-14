/*
 * T_MBXE.C - the mailbox client and the server's mailbox end to end on
 * a scratch mailbox: the same protocol bytes as over the LOCAL link.
 *
 * usage: T_MBXE [dir [golden]]
 *
 * dir is a data directory prefix ("OBJ/HOST/MBXET/", "D:\") naming an
 * existing directory that holds nothing but this test's files; it is
 * also the mailbox, whose REQUEST and RESPONSE directories the test
 * makes.  The engine's files and the mailbox are removed at the start
 * and at the end.  golden is TEST/GOLDEN/INVCMD.TXT, what INV /CMD /LOCAL
 * writes for the request script of TEST/T_INV.SH (default: INVCMD.TXT
 * beside the program).
 *
 * One program is both ends: the client (MBXCLI.H) posts the script's
 * lines as one request file, and the server (TMBOX.H, CONN.H and the
 * engine on dir, its clock fixed) runs pass by pass until the client
 * finds the response file.  Checks:
 * - the names and the longest station MBXCLI.H mirrors are TMBOX.H's
 *   (the station at compile time);
 * - the request script gives exactly the bytes of the golden file, and
 *   over LOCAL, on a fresh data directory with the same fixed clock,
 *   the same bytes; the request after BYE is not answered, and the
 *   response file ends with its last response, not inside one;
 * - the server's session opened and closed once, from the request
 *   file's station, and no CONN event text holds the password; the
 *   request file is gone and no response file is left once read.
 * Prints a summary line "T_MBXE PASS n"; exit status 0 when every check
 * passes, else 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TMBOX.H"
#include "MBXCLI.H"
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"

#define LOOPS           64      /* server passes a session may take     */
#define STATION         "MBXE"  /* the request file's station           */

extern char mbxe_script_fits[1];
extern char mbxe_script_fits[(TSC_OUTSZ >= TSC_SIZE) ? 1 : 2];
extern char mbxe_stalen_ok[1];
extern char mbxe_stalen_ok[(MBXCLI_STALEN == TMBOX_STALEN) ? 1 : 2];

static const char *const k_dirs[] = { TMBOX_REQDIR, TMBOX_RSPDIR, NULL };
static const eng_time g_t0 = { TSC_CLOCK };

static u16 g_opens;             /* CONN_EV_OPEN and CONN_EV_CLOSE seen  */
static u16 g_closes;

static void on_event(u8 c, u8 ev, const char *text)
{
    TF_EQ(c, 0);
    TF_CHECK(strstr(text, TSC_PW) == NULL);
    if (ev == CONN_EV_OPEN) {
        TF_STREQ(text, STATION);
        g_opens++;
    } else if (ev == CONN_EV_CLOSE) {
        g_closes++;
    }
}

/* Queue the n bytes of script on m, line by line, each line's CR LF or
   LF dropped. */
static void queue(mbxcli *m, const char *script, u16 n)
{
    u16 at;
    u16 k;

    for (at = 0; at < n; at = (u16)(at + k + 1)) {
        for (k = 0; at + k < n && script[at + k] != '\n'; k++)
            ;
        TF_EQ(mbxcli_queue(m, script + at,
                           (u16)(k > 0 && script[at + k - 1] == '\r' ?
                                 k - 1 : k)), MBXCLI_OK);
    }
}

/* The script through the mailbox into t, the server on a fresh data
   directory; then the server stopped and checked. */
static void mbox_run(const char *script, u16 n, tn_text *t)
{
    char why[TFS_WHYSZ];
    mbxcli *m;
    eng *e;
    u32 now;
    u16 i;
    int rc;

    te_remove(tfs_dir(), NULL);
    why[0] = '\0';
    if (!TF_EQ(eng_open(&e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return;
    }
    now = 1000;
    if (TF_EQ(conn_init(e, NULL, 1, 0, now), 0)) {
        conn_watch(on_event);
        if (TF_EQ(mbxcli_open(&m, tfs_dir(), STATION), MBXCLI_OK)) {
            queue(m, script, n);
            TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
            if (TF_EQ(tmbox_start(tfs_dir()), TMBOX_OK)) {
                rc = MBXCLI_EWAIT;
                for (i = 0; i < LOOPS && rc == MBXCLI_EWAIT; i++) {
                    tmbox_poll(now);
                    conn_service(now);
                    conn_timeouts(now);
                    rc = mbxcli_post(m, 0);
                }
                TF_EQ(rc, MBXCLI_OK);
                while ((rc = mbxcli_response(m, tsc_take, t)) ==
                       MBXCLI_OK)
                    ;
                TF_EQ(rc, MBXCLI_ENONE);
                tmbox_stop();
            }
            mbxcli_close(m);
        }
        conn_free();
        conn_watch(NULL);
    }
    TF_EQ(eng_close(e), ENG_OK);
    TF_EQ(g_opens, 1);
    TF_EQ(g_closes, 1);
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_REQDIR, STATION ".REQ")));
    TF_CHECK(!tfs_exists(tfs_subpath(TMBOX_RSPDIR, STATION ".RSP")));
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
    tf_begin("T_MBXE");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    TF_STREQ(MBXCLI_REQDIR, TMBOX_REQDIR);
    TF_STREQ(MBXCLI_RSPDIR, TMBOX_RSPDIR);
    TF_STREQ(MBXCLI_REQEXT, TMBOX_REQEXT);
    TF_STREQ(MBXCLI_TMPEXT, TMBOX_TMPEXT);
    TF_STREQ(MBXCLI_RSPEXT, TMBOX_RSPEXT);
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
    tfs_dirs(k_dirs, 0);
    if (TF_EQ(tfs_dirs(k_dirs, 1), 0)) {
        n = tsc_script(script, STATION);
        mbox_run(script, n, &t[0]);
        tsc_local(script, n, &t[1]);
        tsc_same(&t[0], &t[2], "mailbox script and golden");
        tsc_same(&t[1], &t[2], "LOCAL script and golden");
    }
    te_remove(tfs_dir(), NULL);
    tfs_dirs(k_dirs, 0);
    free(pool);
    return tf_done();
}

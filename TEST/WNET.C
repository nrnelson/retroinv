/*
 * WNET.C - the NET link of the Windows programs (LINKNET.C, NBCLI.C and
 * the Windows variant of CNETB.C), with no NetBIOS provider or against a
 * server on another machine.
 *
 * usage: WIN WNET /NONB
 *        WIN WNET server script seconds    (seconds 1..WN_MAXSECS)
 *
 * Built as a Windows program with the WinMain of TEST/WTEST.C.  With
 * /NONB, on a machine without NetBIOS, the link must refuse to open at
 * once.  Otherwise the program opens the link to server, calling again
 * once a second for up to seconds while the server's name does not
 * answer or refuses the call (its machine may still be starting), and
 * then plays script, in the form of TEST/SCRIPTS/: lines "> request",
 * each sent as it is, "< line", the next line of the response, and
 * comments ("#") and blank lines between.  While the link waits,
 * Windows runs the other programs, as INVWIN lets it (dos_yield).
 *
 * Checks:
 * - /NONB: link_open_net gives LINK_ENONB, no link and the code NB_OK
 *   within WN_NONBTICKS;
 * - else the link opens before the time is up, each request is
 *   answered, every line of its response equals the script's next "<"
 *   line, no more and no fewer lines, and the link closes;
 * - then a no-wait LISTEN of the name WN_NAME, posted and cancelled
 *   (the CANCEL names an NCB that Windows maps), the CANCEL giving NB_OK
 *   or NB_RC_PENDING, ends cancelled within WN_GRACE, and the name is
 *   deleted;
 * - no NetBIOS command is left with the provider at the end.
 *
 * Prints how the open went ("WNET OPEN rc nbrc tries ticks") and the
 * cancel ("WNET CANCEL rc ticks", the ticks until the LISTEN's end
 * showed), each line that differs, and a summary line; exit status
 * (the WNET EXIT line of WTEST.C) 0 when every check passes, 1 when one
 * fails.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "TESTFW.H"
#include "LINK.H"

#define WN_SCRMAX       4096    /* bytes of the script                  */
#define WN_MAXSECS      600     /* the longest wait for the server      */
#define WN_NONBTICKS    TMR_SEC /* the longest refusal without NetBIOS  */
#define WN_GRACE        (5 * TMR_SEC)  /* the longest wait for a cancel  */
#define WN_NAME         "WNETCAN"  /* the name the cancelled LISTEN has */

/* The script and the line its reader is at, between the requests and
   the lines of their responses. */
static char g_scr[WN_SCRMAX + 1];
static char *g_at;

/* The script's next line, its end made a NUL, skipping comments and
   blank lines; NULL at the end. */
static char *next_line(void)
{
    char *line;
    char *e;

    while (*g_at != '\0') {
        line = g_at;
        e = strchr(line, '\n');
        g_at = e != NULL ? e + 1 : line + strlen(line);
        if (e != NULL)
            *e = '\0';
        if (e != NULL && e > line && e[-1] == '\r')
            e[-1] = '\0';
        if (*line != '\0' && *line != '#')
            return line;
    }
    return NULL;
}

/* link_line_fn: the line must be the script's next "< " line; *arg
   counts the lines. */
static int check_line(void *arg, const char *line, u16 len)
{
    char *want;

    want = next_line();
    if (want == NULL || strncmp(want, "< ", 2) != 0) {
        printf("WNET extra: %s\n", line);
        TF_CHECK(0);
    } else if (strcmp(want + 2, line) != 0 || strlen(line) != len) {
        printf("WNET want: %s\nWNET got:  %s\n", want + 2, line);
        TF_CHECK(0);
    }
    *(int *)arg += 1;
    return 0;
}

/* A plain dispatch of the messages waiting, like INVWIN's yield but
   without its WM_QUIT repost, its filter of input while closing and its
   modeless dialogs. */
static void yield(void)
{
    MSG m;

    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

/* /NONB: the link finds no NetBIOS and refuses at once. */
static void test_nonb(void)
{
    link *l;
    u32 start;
    u8 nbrc;

    nbrc = NB_RC_PENDING;
    start = tmr_ticks();
    TF_EQ(link_open_net(&l, "RISRV", &nbrc), LINK_ENONB);
    TF_CHECK(tmr_since(start) <= WN_NONBTICKS);
    TF_CHECK(l == NULL);
    TF_EQ(nbrc, NB_OK);
}

/* Read the script file path into g_scr. */
static void load_script(const char *path)
{
    u16 n;
    int fh;

    n = 0;
    TF_EQ(cf_open(path, CF_READ, &fh), 0);
    if (fh >= 0) {
        cf_read(fh, g_scr, WN_SCRMAX, &n);
        cf_close(fh);
    }
    g_scr[n] = '\0';
    g_at = g_scr;
}

/* Open the link to server for up to secs seconds and play the script;
   a "<" line found between requests is one that the response before it
   lacked. */
static void test_server(const char *server, u16 secs)
{
    link *l;
    char *line;
    u32 start;
    u8 nbrc;
    int tries;
    int rc;
    int got;

    start = tmr_ticks();
    tries = 0;
    do {
        if (tries++ != 0)
            tmr_delay(TMR_SEC);
        rc = link_open_net(&l, server, &nbrc);
    } while ((rc == LINK_ECALL || rc == LINK_EREFUSED) &&
             tmr_since(start) < (u32)secs * TMR_SEC);
    printf("WNET OPEN %d %u %d %lu\n", rc, (unsigned)nbrc, tries,
           (unsigned long)tmr_since(start));
    TF_EQ(rc, LINK_OK);
    while (rc == LINK_OK && (line = next_line()) != NULL) {
        if (strncmp(line, "> ", 2) != 0) {
            printf("WNET missing: %s\n", line);
            TF_CHECK(0);
            continue;
        }
        got = 0;
        TF_EQ(link_send(l, line + 2, (u16)strlen(line + 2), check_line,
                        &got), LINK_OK);
        TF_CHECK(got != 0);
    }
    TF_EQ(link_close(l), 0);
}

/* A no-wait LISTEN of WN_NAME, posted and cancelled at once. */
static void test_cancel(void)
{
    nba *s;
    char name[NB_NAMELEN];
    u32 start;
    u8 added;
    u8 rc;

    TF_EQ(nb_local_name(name, WN_NAME, WN_NAME, &added), NB_OK);
    TF_EQ(nba_open(&s, 1), 0);
    if (s != NULL) {
        TF_EQ(nba_listen(s, 0, name, "*", 0, 0), NB_OK);
        TF_EQ(nba_poll(s, 0), NB_RC_PENDING);
        start = tmr_ticks();
        rc = nba_cancel(s, 0);
        while (nba_poll(s, 0) == NB_RC_PENDING &&
               tmr_since(start) < WN_GRACE)
            dos_idle();
        printf("WNET CANCEL %u %lu\n", (unsigned)rc,
               (unsigned long)tmr_since(start));
        TF_CHECK(rc == NB_OK || rc == NB_RC_PENDING);
        TF_EQ(nba_poll(s, 0), NB_RC_CANCELLED);
    }
    TF_EQ(nba_close(s), 0);
    if (added)
        TF_EQ(nb_delete_name(name), NB_OK);
}

int main(int argc, char **argv)
{
    u16 secs;

    tf_begin("WNET");
    secs = argc == 4 ? txt_num(argv[3], WN_MAXSECS) : 0;
    if (!(argc == 2 && strcmp(argv[1], "/NONB") == 0) && secs == 0) {
        printf("usage: WIN WNET /NONB\n"
               "       WIN WNET server script seconds\n");
        return 1;
    }
    cmem_init();
    dos_yield = yield;
    if (argc == 2) {
        test_nonb();
    } else {
        load_script(argv[2]);
        test_server(argv[1], secs);
        test_cancel();
    }
    TF_EQ(nba_stuck(), 0);
    dos_yield = NULL;
    return tf_done();
}

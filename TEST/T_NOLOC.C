/*
 * T_NOLOC.C - unit test for the link of a program built without the
 * engine: LINK.C with LINKNOL.C in place of LINKLOC.C, and of the
 * protocol layer TOKEN.C alone, no response writer and no engine text,
 * as that program links them.
 *
 * usage: T_NOLOC
 *
 * Checks:
 * - link_open_local opens nothing: RQ_ENOTAVAIL with *out NULL and a
 *   reason, also with no why buffer;
 * - on a remote link (a stand-in backend), a line of LINK_MAXLINE bytes
 *   is sent, and one byte more is answered "-ERR 103 line too long"
 *   without being sent, by link_send and by link_feed alike, as a server
 *   answers it;
 * - link_close closes the backend once.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "ENGINE.H"
#include "LINK.H"
#include "LINKREM.H"
#include "RQ.H"

#define OUTMAX          128     /* response text a test keeps           */

static int g_sent;              /* lines the backend was sent           */
static int g_closed;            /* closes of the backend                */
static char g_out[OUTMAX];      /* the response lines, LF ended         */

static int be_send(void *s, const char *line, u16 len)
{
    g_sent++;
    return s != NULL && line != NULL && len > 0 ? 0 : LR_ELOST;
}

static int be_response(void *s, link_line_fn fn, void *arg)
{
    return s != NULL && fn(arg, "+OK", 3) != 0 ? LR_EOUTPUT : 0;
}

static void be_ended(void *s)
{
    g_closed += s != NULL;
}

static void be_close(void *s)
{
    g_closed += s != NULL;
}

static const link_rops be_ops = { be_send, be_response, be_ended,
                                  be_close };

/* link_line_fn: the line and LF onto g_out. */
static int keep(void *arg, const char *line, u16 len)
{
    size_t n;

    n = strlen(g_out);
    if (arg != NULL || n + len + 2 > OUTMAX)
        return 1;
    memcpy(g_out + n, line, len);
    g_out[n + len] = '\n';
    g_out[n + len + 1] = '\0';
    return 0;
}

static void test_local(void)
{
    char why[LINK_WHYSZ];
    link *l;

    l = (link *)g_out;
    why[0] = '\0';
    TF_EQ(link_open_local(&l, "DATA\\", NULL, why, (u16)sizeof why),
          RQ_ENOTAVAIL);
    TF_CHECK(l == NULL);
    TF_CHECK(why[0] != '\0');
    l = (link *)g_out;
    TF_EQ(link_open_local(&l, "", NULL, NULL, 0), RQ_ENOTAVAIL);
    TF_CHECK(l == NULL);
}

static void test_toolong(void)
{
    static char be;             /* the stand-in session                 */
    char line[LINK_MAXLINE + 2];
    link *l;

    if (!TF_EQ(link_open_remote(&l, &be, &be_ops, NULL), 0))
        return;
    memset(line, 'X', sizeof line);
    g_out[0] = '\0';
    TF_EQ(link_send(l, line, LINK_MAXLINE, keep, NULL), LINK_OK);
    TF_EQ(g_sent, 1);
    TF_STREQ(g_out, "+OK\n");
    g_out[0] = '\0';
    TF_EQ(link_send(l, line, LINK_MAXLINE + 1, keep, NULL), LINK_OK);
    TF_EQ(g_sent, 1);
    TF_STREQ(g_out, "-ERR 103 line too long\n");
    g_out[0] = '\0';
    line[LINK_MAXLINE + 1] = '\n';
    TF_EQ(link_feed(l, line, LINK_MAXLINE + 2, keep, NULL), LINK_OK);
    TF_EQ(g_sent, 1);
    TF_STREQ(g_out, "-ERR 103 line too long\n");
    TF_EQ(link_close(l), 0);
    TF_EQ(g_closed, 1);
}

int main(void)
{
    tf_begin("T_NOLOC");
    test_local();
    test_toolong();
    return tf_done();
}

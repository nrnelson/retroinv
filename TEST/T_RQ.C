/*
 * T_RQ.C - request building and response decoding (SRC/CLIENT/RQ.C)
 * over a LOCAL link.
 *
 * usage: T_RQ dir [NOKERR]
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/RDATA/", "D:\")
 * holding nothing but this test's files; the engine's files there are
 * removed first and last, so the session starts from new, empty tables
 * with the initial ADMIN user, the clock fixed by tc_clock.
 *
 * Covered:
 * - quoting and rq_cost, overflow and the rq_try forms;
 * - every response kind the server sends (+OK with values, -ERR with
 *   FIELD=, +DATA with K, F, R, L and C lines), escapes, the spacing of L
 *   text and memo lines that begin with dots;
 * - a callback stopping a response or trying to send one of its own;
 * - a data block ended by K ERROR=;
 * - a request too long to send, the wiping of the line sent;
 * - a data directory name too long once its separator is added;
 * - a closed session.
 *
 * The K ERROR= case cuts the item table short through a second handle
 * while the engine holds it open.  Under MS-DOS the engine's handle then
 * still has the old size and DOS reads clusters that no longer belong
 * to the file, which a real DOS may report as a disk error; NOKERR
 * leaves that case out for such runs.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "ENGINE.H"
#include "LINK.H"
#include "RQ.H"
#include "TEWIPE.H"
#define TF_CLOCK
#include "TESTFW.H"

#define PATHMAX         96

static char g_ddir[PATHMAX];
static link *g_lk;

/* Lines collected by the callback, kind letter first, values separated
   by '|'. */
#define COLMAX          2048
static char g_col[COLMAX];
static u16 g_stop_after;        /* stop at this line (1-based), or 0    */
static u16 g_nlines;

static int collect(void *arg, const rq_line *ln)
{
    size_t n;
    u16 i;
    char k[3];

    n = strlen(g_col);
    TF_CHECK(arg == (void *)g_col);
    g_nlines++;
    k[0] = ln->kind;
    k[1] = '\0';
    if (n + 2 < COLMAX)
        strcat(g_col, k);
    for (i = 0; i < ln->n; i++) {
        if (strlen(g_col) + strlen(ln->v[i]) + 2 < COLMAX) {
            strcat(g_col, "|");
            strcat(g_col, ln->v[i]);
        }
    }
    if (ln->over > 0 && strlen(g_col) + 2 < COLMAX)
        strcat(g_col, "+");
    if (strlen(g_col) + 1 < COLMAX)
        strcat(g_col, "\n");
    return g_stop_after != 0 && g_nlines >= g_stop_after;
}

/* Send line as words (no quoting) and collect the response. */
static int send(const char *words, rq_res *res)
{
    rq q;

    g_col[0] = '\0';
    g_nlines = 0;
    rq_begin(&q, words);
    return rq_send(g_lk, &q, collect, g_col, res);
}

/* A callback that tries to send a request of its own. */
static int g_reenter_rc;

static int reenter(void *arg, const rq_line *ln)
{
    rq q;
    rq_res res;

    rq_begin(&q, "PING");
    g_reenter_rc = rq_send(g_lk, &q, NULL, NULL, &res);
    return arg != NULL || ln->kind == 'X';
}

static void test_building(void)
{
    rq q;
    char big[LINK_MAXLINE + 2];
    u16 room;

    TF_EQ(rq_cost("", 0), 2);
    TF_EQ(rq_cost("abc", 3), 3);
    TF_EQ(rq_cost("a b", 3), 5);
    TF_EQ(rq_cost("\"", 1), 4);
    TF_EQ(rq_cost("\\", 1), 4);
    TF_EQ(rq_cost("\n", 1), 4);
    TF_EQ(rq_cost("\t", 1), 6);
    TF_EQ(rq_cost("\177", 1), 6);
    TF_EQ(rq_cost("\304", 1), 1);
    TF_EQ(rq_cost("=", 1), 3);
    TF_EQ(rq_cost("ab", 1), 1);

    rq_begin(&q, "ITEM LIST");
    TF_STREQ(q.line, "ITEM LIST");
    TF_EQ(q.len, 9);
    rq_word(&q, "a b");
    rq_word(&q, "x=y");
    rq_word(&q, "plain");
    rq_pair(&q, "E", "");
    rq_pair(&q, "Q", "say \"hi\"\\");
    rq_pairn(&q, "M", "l1\nl2\tz\177\304", 9);
    rq_pairu(&q, "PAGE", 0);
    rq_pairu(&q, "N", 4294967295UL);
    TF_STREQ(q.line, "ITEM LIST \"a b\" \"x=y\" plain E=\"\" "
                     "Q=\"say \\\"hi\\\"\\\\\" M=\"l1\\nl2\\x09z\\x7F\304\" "
                     "PAGE=0 N=4294967295");
    TF_EQ(q.len, strlen(q.line));
    TF_EQ(q.over, 0);
    TF_EQ(rq_room(&q), LINK_MAXLINE - q.len);

    /* Fill to exactly LINK_MAXLINE bytes: K= takes 3 plus the value. */
    rq_begin(&q, "X");
    room = rq_room(&q);
    memset(big, 'v', sizeof big);
    big[room - 3] = '\0';
    TF_EQ(rq_trypair(&q, "K", big), 1);
    TF_EQ(q.len, LINK_MAXLINE);
    TF_EQ(rq_room(&q), 0);
    TF_EQ(rq_trypair(&q, "A", ""), 0);
    TF_EQ(q.len, LINK_MAXLINE);
    TF_EQ(q.over, 0);
    rq_word(&q, "w");
    TF_EQ(q.over, 1);
    TF_EQ(q.len, LINK_MAXLINE);

    rq_begin(&q, "X");
    big[room - 3] = 'v';
    big[room - 2] = '\0';
    TF_EQ(rq_trypairn(&q, "K", big, (u16)(room - 2)), 0);
    TF_EQ(q.len, 1);
    TF_STREQ(q.line, "X");
    rq_pair(&q, "K", big);
    TF_EQ(q.over, 1);
    TF_STREQ(q.line, "X");
    /* A later part that fits is still added, but the request stays
       refused. */
    rq_pair(&q, "B", "1");
    TF_STREQ(q.line, "X B=1");
    TF_EQ(q.over, 1);

    memset(big, 'w', sizeof big);
    big[LINK_MAXLINE + 1] = '\0';
    rq_begin(&q, big);
    TF_EQ(q.over, 1);
    TF_EQ(q.len, 0);
}

static void test_session(void)
{
    rq_res res;
    rq q;
    char v[RQ_TEXTLEN + 1];
    u16 i;

    TF_EQ(send("PING", &res), RQ_OK);
    TF_EQ(res.data, 0);
    TF_STREQ(res.text, "");
    TF_EQ(send("VER", &res), RQ_OK);
    TF_CHECK(strncmp(res.text, "RETROINV ", 9) == 0);
    TF_EQ(send("HELLO", &res), RQ_EARG);
    TF_STREQ(res.text, "missing argument");
    TF_STREQ(res.field, "STATION");
    TF_EQ(res.closed, 0);
    TF_EQ(send("WHOAMI", &res), RQ_ENOLOGIN);
    TF_STREQ(res.text, "HELLO or LOGIN required");
    TF_STREQ(res.field, "");
    TF_EQ(send("BOGUS", &res), RQ_EUNKNOWN);

    rq_begin(&q, "HELLO");
    rq_word(&q, "T1");
    TF_EQ(rq_send(g_lk, &q, NULL, NULL, &res), RQ_OK);
    rq_begin(&q, "LOGIN");
    rq_word(&q, "ADMIN");
    rq_word(&q, "admin");
    TF_EQ(rq_send(g_lk, &q, NULL, NULL, &res), RQ_OK);
    /* The line sent is wiped. */
    TF_EQ(q.len, 0);
    for (i = 0; i < sizeof q.line && q.line[i] == '\0'; i++)
        ;
    TF_EQ(i, sizeof q.line);
    TF_EQ(rq_okval(&res, "LEVEL", v, sizeof v), 1);
    TF_STREQ(v, "3");
    TF_EQ(rq_okval(&res, "MUSTCHG", v, sizeof v), 1);
    TF_STREQ(v, "1");
    TF_EQ(rq_okval(&res, "NAME", v, sizeof v), 0);
    TF_STREQ(v, "");
    TF_EQ(send("STATUS", &res), RQ_EMUSTCHG);
    TF_EQ(send("PASSWD admin secret1", &res), RQ_OK);
    TF_EQ(send("LOGIN ADMIN secret1", &res), RQ_OK);
    TF_EQ(rq_okval(&res, "NAME", v, sizeof v), 1);
    TF_CHECK(v[0] != '\0');
    TF_EQ(rq_okval(&res, "LEVEL", v, 2), 1);
    TF_STREQ(v, "3");
    TF_EQ(rq_okval(&res, "LEVE", v, sizeof v), 0);

    /* A value at the end of a text cut at RQ_TEXTLEN may be cut too. */
    memset(res.text, 'x', RQ_TEXTLEN);
    strcpy(res.text + RQ_TEXTLEN - 6, "ID=123");
    res.text[RQ_TEXTLEN - 7] = ' ';
    TF_EQ(rq_okval(&res, "ID", v, sizeof v), 0);
    res.text[RQ_TEXTLEN - 1] = '\0';
    TF_EQ(rq_okval(&res, "ID", v, sizeof v), 1);
    TF_STREQ(v, "12");
}

/* An item whose values need every kind of quoting. */
static void test_items(void)
{
    rq_res res;
    rq q;
    char id[16];
    char cur[LINK_MAXCURSOR + 1];
    int i;

    for (i = 0; i < 3; i++) {
        rq_begin(&q, "ITEM ADD");
        rq_pair(&q, "CATEGORY", "CARD");
        rq_pair(&q, "MFR", i == 0 ? "A \"q\" b\\c" : "IBM");
        rq_pair(&q, "MODEL", i == 0 ? "\304x=y" : "5150");
        rq_pair(&q, "DESCR", "");
        TF_EQ(rq_send(g_lk, &q, NULL, NULL, &res), RQ_OK);
    }
    TF_EQ(rq_okval(&res, "ID", id, sizeof id), 1);
    TF_STREQ(id, "3");

    TF_EQ(send("ITEM GET 1", &res), RQ_OK);
    TF_EQ(res.data, 1);
    TF_CHECK(strstr(g_col, "K|MFR|A \"q\" b\\c\n") != NULL);
    TF_CHECK(strstr(g_col, "K|MODEL|\304x=y\n") != NULL);
    TF_CHECK(strstr(g_col, "K|DESCR|\n") != NULL);
    TF_CHECK(strstr(g_col, "K|ITEM_ID|1\n") == g_col);

    TF_EQ(send("ITEM LIST FIELDS=ITEM_ID,MFR,MODEL,DESCR PAGE=2", &res),
          RQ_OK);
    TF_STREQ(g_col, "F|ITEM_ID|MFR|MODEL|DESCR\n"
                  "R|1|A \"q\" b\\c|\304x=y|\n"
                  "R|2|IBM|5150|\n");
    TF_CHECK(res.more[0] != '\0');
    strcpy(cur, res.more);
    rq_begin(&q, "ITEM LIST");
    rq_pair(&q, "FIELDS", "ITEM_ID");
    rq_pair(&q, "AFTER", cur);
    g_col[0] = '\0';
    TF_EQ(rq_send(g_lk, &q, collect, g_col, &res), RQ_OK);
    TF_STREQ(g_col, "F|ITEM_ID\nR|3\n");
    TF_STREQ(res.more, "");

    /* A callback stops the response; the link stays in step. */
    g_stop_after = 2;
    TF_EQ(send("ITEM LIST FIELDS=ITEM_ID", &res), RQ_ESTOP);
    g_stop_after = 0;
    TF_EQ(g_nlines, 2);
    TF_STREQ(g_col, "F|ITEM_ID\nR|1\n");
    TF_EQ(send("PING", &res), RQ_OK);

    /* A callback cannot send a request of its own. */
    rq_begin(&q, "ITEM GET 1");
    g_reenter_rc = -1;
    TF_EQ(rq_send(g_lk, &q, reenter, NULL, &res), RQ_OK);
    TF_EQ(g_reenter_rc, RQ_EINTERNAL);
    TF_EQ(send("PING", &res), RQ_OK);

    TF_EQ(send("ITEM ADD MFR=X", &res), RQ_EARG);
    TF_STREQ(res.field, "CATEGORY");
    TF_CHECK(res.text[0] != '\0' && strstr(res.text, "FIELD") == NULL);
    TF_EQ(send("ITEM GET 99", &res), RQ_ENOTFOUND);
    TF_EQ(res.data, 0);
}

static const char notes_want[] =
    "L|  two spaces\n"
    "L|.dot\n"
    "L|\n"
    "L|..two\n"
    "L|back\\slash tab\t \\x41 \304 end \n";

/* Memo text comes back as L lines: leading spaces and dots kept,
   escapes decoded, an empty line as "L" alone. */
static void test_notes(void)
{
    rq_res res;
    rq q;

    rq_begin(&q, "ITEM NOTES 2");
    rq_pair(&q, "ADD", "  two spaces");
    TF_EQ(rq_send(g_lk, &q, NULL, NULL, &res), RQ_OK);
    rq_begin(&q, "ITEM NOTES 2");
    rq_pair(&q, "ADD", ".dot\n\n..two");
    TF_EQ(rq_send(g_lk, &q, NULL, NULL, &res), RQ_OK);
    rq_begin(&q, "ITEM NOTES 2");
    rq_pair(&q, "ADD", "back\\slash tab\t \\x41 \304 end ");
    TF_EQ(rq_send(g_lk, &q, NULL, NULL, &res), RQ_OK);
    TF_EQ(send("ITEM NOTES 2", &res), RQ_OK);
    TF_STREQ(g_col, notes_want);
}

/* A failure after +DATA has begun ends the block with K ERROR=n: the
   item table is cut short behind the engine's back, so reading the
   records past the cut fails. */
static void test_data_error(void)
{
    char path[PATHMAX + 16];
    rq_res res;
    u8 hdr[12];
    u16 got;
    u16 hlen;
    u16 rlen;
    int h;

    got = 0;
    strcpy(path, g_ddir);
    strcat(path, "ITEMS.DBF");
    if (!TF_EQ(cf_open(path, CF_RDWR | CF_DENYNONE, &h), 0))
        return;
    TF_EQ(cf_read(h, hdr, sizeof hdr, &got), 0);
    hlen = get_le16(hdr + 8);
    rlen = get_le16(hdr + 10);
    TF_EQ(cf_truncate(h, (u32)hlen + rlen), 0);
    cf_close(h);
    TF_EQ(send("ITEM LIST FIELDS=ITEM_ID ORDER=MFR", &res), RQ_EIO);
    TF_EQ(res.data, 1);
    TF_STREQ(g_col, "F|ITEM_ID\nR|1\n");
}

static void test_closing(void)
{
    rq_res res;
    int i;

    TF_EQ(send("HELLO T2", &res), RQ_OK);
    for (i = 0; i < 2; i++) {
        TF_EQ(send("LOGIN ADMIN wrong", &res), RQ_EBADPW);
        TF_EQ(res.closed, 0);
    }
    TF_EQ(send("LOGIN ADMIN wrong", &res), RQ_EBADPW);
    TF_EQ(res.closed, 1);
    TF_EQ(send("PING", &res), RQ_ECLOSED);
    TF_EQ(res.closed, 1);
    TF_STREQ(res.text, "");
}

static void test_too_long(void)
{
    rq_res res;
    rq q;
    char big[LINK_MAXLINE + 1];

    memset(big, 'v', LINK_MAXLINE);
    big[LINK_MAXLINE] = '\0';
    rq_begin(&q, "PING");
    rq_pair(&q, "K", big);
    TF_EQ(q.over, 1);
    TF_EQ(rq_send(g_lk, &q, collect, g_col, &res), RQ_ETOOLONG);
    TF_EQ(q.len, 0);
    TF_EQ(send("PING", &res), RQ_OK);
}

/* A directory name of LINK_MAXDIR characters without a separator is too
   long once link_open_local adds one. */
static void test_long_dir(void)
{
    static char notlink;        /* its address: no link, but not NULL   */
    char dir[LINK_MAXDIR + 1];
    char why[LINK_WHYSZ];
    link *l;

    l = (link *)&notlink;
    memset(dir, 'd', LINK_MAXDIR);
    dir[LINK_MAXDIR] = '\0';
    TF_EQ(link_open_local(&l, dir, NULL, why, sizeof why), RQ_EARG);
    TF_CHECK(l == NULL);
    TF_STREQ(why, "data directory name too long");
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    int rc;

    tf_begin("T_RQ");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_RQ dir [NOKERR]\n");
        return 2;
    }
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    test_building();
    test_long_dir();
    rc = link_open_local(&g_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_RQ: %s\n", why);
        return tf_done();
    }
    test_session();
    test_items();
    test_notes();
    test_too_long();
    if (argc < 3 || strcmp(argv[2], "NOKERR") != 0)
        test_data_error();
    test_closing();
    TF_EQ(link_close(g_lk), 0);
    te_remove(g_ddir, NULL);
    return tf_done();
}

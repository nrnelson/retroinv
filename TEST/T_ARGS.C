/*
 * T_ARGS.C - unit test for the connection switches of the client
 * programs (SRC/CLIENT/ARGS.C), written from the contract of ARGS.H.
 *
 * usage: T_ARGS   (the runners' scratch directory argument is unused)
 *
 * Built with ARGS_WINSW against ARGSW.C it is T_ARGST (TEST/T_ARGST.C),
 * the test of /TCP: and /MBOX: as well.  Checks, with the words built
 * into an argv:
 * - no words, or only the program name: kind ARGS_LOCAL, every other
 *   field empty, *word NULL;
 * - each switch (/LOCAL, /NET, /COM1, /COM2, /CFG:) is taken whole and
 *   without regard to case, with '/' and with '-'; a longer or shorter
 *   word, one with no colon (/CFG) or another port (/COM0, /COM3) is
 *   no switch and goes to the function;
 * - /LOCAL with and without its dir, /NET with its name (and the missing
 *   name, ARGS_ENONAME, at the end or before a switch), /COMn with
 *   comname upper-cased, /CFG:file with the text after the colon as
 *   typed, and an empty /CFG:; dir, net and cfg point into argv;
 * - the repeats and /COM2 after /COM1 (ARGS_EBAD), the order of the
 *   results (the first error in the words wins, then ARGS_ECLASH naming
 *   the second connection, then ARGS_EBADNAME), the longest /NET name
 *   taken, and *word as the element of argv itself, empty words too;
 * - the function: given the words it does not take, with the prefix
 *   removed and "" for a plain word, in order and with arg (the word
 *   after a port too); never given a word that is taken, a repeated
 *   /LOCAL's or /NET's word included; skipped when NULL; its error is
 *   returned with *word the word, and the scan goes on to the end of the
 *   words;
 * - without ARGS_WINSW, /TCP: and /MBOX: are words like any other,
 *   given to the function and naming no connection;
 * - with it, /TCP: is taken whole up to its colon and without regard to
 *   case, alone (port 0) and with a port, each number at its bounds, tcp
 *   pointing after the colon into argv; every malformed address or port
 *   is refused (ARGS_EBAD): a number missing, over 255 or of four
 *   digits, three or five numbers, an empty, zero or too large port, a
 *   stray character, an empty value; /TCP without its colon goes to the
 *   function; the words after it are not its own; it repeats, clashes
 *   and orders its errors as the other connections do;
 * - with it, /MBOX: is taken whole up to its colon and without regard
 *   to case, mbox pointing after the colon into argv, the text as typed;
 *   an empty dir is refused (ARGS_EBAD); /MBOX without its colon goes to
 *   the function; it repeats and clashes as the other connections do.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "ARGS.H"
#include "TESTFW.H"

#ifdef ARGS_WINSW
#define TA_NAME         "T_ARGST"
#else
#define TA_NAME         "T_ARGS"
#endif

#define MAXW    24              /* words of a line, program name included */
#define SEEN    12              /* words a recorder keeps                 */

static char g_buf[512];
static char *g_v[MAXW];
static int g_vc;
static args g_a;
static const char *g_w;

/* The recorder of the function's words, and what it answers. */
typedef struct {
    int n;                      /* words given, kept or not              */
    char seen[SEEN][32];
    const char *stop;           /* the word to answer code to, or NULL   */
    int code;
} rec;

static int cb(void *arg, const char *s)
{
    rec *r;

    r = (rec *)arg;
    if (r->n < SEEN)
        strcpy(r->seen[r->n], s);
    r->n++;
    if (r->stop != NULL && strcmp(s, r->stop) == 0)
        return r->code;
    return ARGS_OK;
}

static void rec_init(rec *r)
{
    memset(r, 0, sizeof *r);
}

/* Parse the words of line (separated by single spaces) after a program
   name, into a and w, first filling both with junk. */
static int run(const char *line, args_fn fn, void *arg)
{
    char *p;
    static const char junk = 'j';

    strcpy(g_buf, line);
    g_vc = 0;
    g_v[g_vc++] = "PROG";
    for (p = g_buf; *p != '\0' && g_vc < MAXW; ) {
        g_v[g_vc++] = p;
        while (*p != '\0' && *p != ' ')
            p++;
        if (*p == ' ')
            *p++ = '\0';
    }
    memset(&g_a, 0x55, sizeof g_a);
    g_w = &junk;
    return args_parse(&g_a, g_vc, g_v, fn, arg, &g_w);
}

/* line parses with no function to ARGS_OK, *word NULL. */
static int ok(const char *line)
{
    int r;

    r = run(line, NULL, NULL);
    TF_EQ(r, ARGS_OK);
    TF_CHECK(g_w == NULL);
    return r == ARGS_OK;
}

/* line parses with no function to result, *word the text word. */
static void fails(const char *line, int result, const char *word)
{
    TF_EQ(run(line, NULL, NULL), result);
    if (TF_CHECK(g_w != NULL))
        TF_STREQ(g_w, word);
}

/* The fields a connection leaves empty. */
static void no_dir(void) { TF_CHECK(g_a.dir == NULL); }
static void no_net(void) { TF_CHECK(g_a.net == NULL); }
static void no_cfg(void) { TF_CHECK(g_a.cfg == NULL); }
static void no_com(void)
{
    TF_EQ(g_a.com, 0);
    TF_STREQ(g_a.comname, "");
}

static void test_none(void)
{
    char *one[1];

    one[0] = "PROG";
    memset(&g_a, 0x55, sizeof g_a);
    g_w = (const char *)&g_a;
    TF_EQ(args_parse(&g_a, 1, one, NULL, NULL, &g_w), ARGS_OK);
    TF_CHECK(g_w == NULL);
    TF_EQ(g_a.kind, ARGS_LOCAL);
    no_dir();
    no_net();
    no_cfg();
    no_com();
    memset(&g_a, 0x55, sizeof g_a);
    g_w = (const char *)&g_a;
    TF_EQ(args_parse(&g_a, 0, one, NULL, NULL, &g_w), ARGS_OK);
    TF_CHECK(g_w == NULL);
    TF_EQ(g_a.kind, ARGS_LOCAL);
    no_dir();
    no_net();
    no_cfg();
    no_com();
}

static void test_local(void)
{
    static const char *const sw[] = { "/LOCAL", "/local", "-LOCAL", "-Local",
                                      "/lOcAl" };
    int i;
    char line[32];

    for (i = 0; i < 5; i++) {
        if (!ok(sw[i]))
            continue;
        TF_EQ(g_a.kind, ARGS_LOCAL);
        no_dir();
        no_net();
        no_cfg();
        no_com();
        sprintf(line, "%s DATA", sw[i]);
        if (!ok(line))
            continue;
        TF_EQ(g_a.kind, ARGS_LOCAL);
        TF_CHECK(g_a.dir == g_v[2]);
        TF_STREQ(g_a.dir, "DATA");
        no_net();
        no_cfg();
        no_com();
    }
    /* The dir is kept as typed, and is the next word only. */
    if (ok("/LOCAL c:\\Data\\"))
        TF_STREQ(g_a.dir, "c:\\Data\\");
    if (ok("/LOCAL /CFG:x"))
        no_dir();
    if (ok("/LOCAL -CFG:x d"))
        no_dir();
}

static void test_net(void)
{
    static const char *const sw[] = { "/NET", "/net", "-NET", "-Net" };
    int i;
    char line[32];
    char name[NB_NAMELEN + 2];

    for (i = 0; i < 4; i++) {
        sprintf(line, "%s SRV", sw[i]);
        if (!ok(line))
            continue;
        TF_EQ(g_a.kind, ARGS_NET);
        TF_CHECK(g_a.net == g_v[2]);
        TF_STREQ(g_a.net, "SRV");
        no_dir();
        no_cfg();
        no_com();
        fails(sw[i], ARGS_ENONAME, sw[i]);
    }
    if (ok("/NET Mixed.Case"))
        TF_STREQ(g_a.net, "Mixed.Case");
    if (ok("/NET a*b"))
        TF_STREQ(g_a.net, "a*b");
    /* No name: at the end, or a switch follows. */
    fails("/NET /CFG:x", ARGS_ENONAME, "/NET");
    fails("/NET -x", ARGS_ENONAME, "/NET");
    fails("/NET /LOCAL", ARGS_ENONAME, "/NET");
    fails("/CFG:x -net", ARGS_ENONAME, "-net");
    /* The name's length: 1 .. NB_NAMELEN - 1 characters. */
    memset(name, 'N', sizeof name);
    name[1] = '\0';
    sprintf(line, "/NET %s", name);
    if (ok(line))
        TF_STREQ(g_a.net, "N");
    memset(name, 'N', sizeof name);
    name[NB_NAMELEN - 1] = '\0';
    sprintf(line, "/NET %s", name);
    if (ok(line)) {
        TF_EQ(g_a.kind, ARGS_NET);
        TF_EQ(strlen(g_a.net), NB_NAMELEN - 1);
    }
    memset(name, 'N', sizeof name);
    name[NB_NAMELEN] = '\0';
    sprintf(line, "/NET %s", name);
    fails(line, ARGS_EBADNAME, name);
    fails("/NET *WILD", ARGS_EBADNAME, "*WILD");
    fails("-net *", ARGS_EBADNAME, "*");
}

static void test_com(void)
{
    static const char *const sw1[] = { "/COM1", "/com1", "-COM1", "-Com1" };
    static const char *const sw2[] = { "/COM2", "/com2", "-COM2", "-cOm2" };
    int i;

    for (i = 0; i < 4; i++) {
        if (ok(sw1[i])) {
            TF_EQ(g_a.kind, ARGS_COM);
            TF_EQ(g_a.com, 1);
            TF_STREQ(g_a.comname, "COM1");
            no_dir();
            no_net();
            no_cfg();
        }
        if (ok(sw2[i])) {
            TF_EQ(g_a.kind, ARGS_COM);
            TF_EQ(g_a.com, 2);
            TF_STREQ(g_a.comname, "COM2");
            no_dir();
            no_net();
            no_cfg();
        }
    }
    /* A word after a port is no part of it. */
    if (ok("/COM1 /CFG:a"))
        TF_EQ(g_a.com, 1);
}

#ifdef ARGS_WINSW
static void no_tcp(void) { TF_CHECK(g_a.tcp == NULL); }

/* line parses to ARGS_TCP with the address a.b.c.d and port. */
static void tcp_ok(const char *line, int a, int b, int c, int d, u16 port)
{
    if (!ok(line))
        return;
    TF_EQ(g_a.kind, ARGS_TCP);
    TF_CHECK(g_a.tcp == g_v[1] + 5);
    TF_EQ(g_a.ip[0], a);
    TF_EQ(g_a.ip[1], b);
    TF_EQ(g_a.ip[2], c);
    TF_EQ(g_a.ip[3], d);
    TF_EQ(g_a.port, port);
    no_dir();
    no_net();
    no_cfg();
    no_com();
}

static void test_tcp(void)
{
    static const char *const bad[] = {
        "/TCP:", "/TCP:1.2.3", "/TCP:1.2.3.4.5", "/TCP:1.2.3.4.",
        "/TCP:1..3.4", "/TCP:.1.2.3", "/TCP:1.2.3.256", "/TCP:256.0.0.0",
        "/TCP:1.2.3.0004", "/TCP:0001.2.3.4", "/TCP:a.b.c.d",
        "/TCP:1.2.3.4x", "/TCP:1.2.3.4:", "/TCP:1.2.3.4:0",
        "/TCP:1.2.3.4:65536", "/TCP:1.2.3.4:99999", "/TCP:1.2.3.4:7x",
        "/TCP:1.2.3.4:-1", "/TCP:1.2.3.4::7", "/TCP:1.2.3.4/"
    };
    unsigned i;
    rec r;

    tcp_ok("/TCP:10.0.2.15", 10, 0, 2, 15, 0);
    if (ok("/TCP:10.0.2.15"))
        TF_STREQ(g_a.tcp, "10.0.2.15");
    tcp_ok("/tcp:1.2.3.4:7000", 1, 2, 3, 4, 7000);
    if (ok("-Tcp:1.2.3.4:7000"))
        TF_STREQ(g_a.tcp, "1.2.3.4:7000");
    tcp_ok("-TCP:0.0.0.0:1", 0, 0, 0, 0, 1);
    tcp_ok("/TCP:255.255.255.255:65535", 255, 255, 255, 255, 65535u);
    tcp_ok("/TCP:010.000.002.015", 10, 0, 2, 15, 0);
    tcp_ok("/TCP:1.2.3.4:00080", 1, 2, 3, 4, 80);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
        fails(bad[i], ARGS_EBAD, bad[i]);
    /* Without its colon it is no switch; the other connections leave
       tcp empty. */
    rec_init(&r);
    if (TF_EQ(run("/TCP -TCPX:1.2.3.4", cb, &r), ARGS_OK)) {
        TF_EQ(r.n, 2);
        TF_STREQ(r.seen[0], "TCP");
        TF_STREQ(r.seen[1], "TCPX:1.2.3.4");
        TF_EQ(g_a.kind, ARGS_LOCAL);
        no_tcp();
    }
    if (ok("/NET SRV"))
        no_tcp();
    if (ok("/COM1"))
        no_tcp();
    if (ok("/LOCAL d"))
        no_tcp();
    /* A word after it is no part of it. */
    if (ok("/TCP:1.2.3.4 /CFG:a")) {
        TF_EQ(g_a.kind, ARGS_TCP);
        TF_STREQ(g_a.cfg, "a");
    }
    rec_init(&r);
    if (TF_EQ(run("/TCP:1.2.3.4 d /X", cb, &r), ARGS_OK)) {
        TF_STREQ(g_a.tcp, "1.2.3.4");
        TF_EQ(r.n, 2);
        TF_STREQ(r.seen[0], "");
        TF_STREQ(r.seen[1], "X");
    }
    /* Repeats, clashes and their order, as for the other connections. */
    fails("/TCP:1.2.3.4 /TCP:1.2.3.4", ARGS_EBAD, "/TCP:1.2.3.4");
    fails("/TCP:1.2.3.4 -tcp:5.6.7.8:9", ARGS_EBAD, "-tcp:5.6.7.8:9");
    fails("/LOCAL /TCP:1.2.3.4", ARGS_ECLASH, "/TCP:1.2.3.4");
    fails("/TCP:1.2.3.4 /LOCAL d", ARGS_ECLASH, "/LOCAL");
    fails("/NET s -tcp:1.2.3.4", ARGS_ECLASH, "-tcp:1.2.3.4");
    fails("/TCP:1.2.3.4 /NET s", ARGS_ECLASH, "/NET");
    fails("/COM1 /TCP:1.2.3.4", ARGS_ECLASH, "/TCP:1.2.3.4");
    fails("/TCP:1.2.3.4 /COM2", ARGS_ECLASH, "/COM2");
    fails("/LOCAL /TCP:1.2.3", ARGS_EBAD, "/TCP:1.2.3");
    fails("/TCP:1.2.3 /NET *bad", ARGS_EBAD, "/TCP:1.2.3");
}

static void test_mbox(void)
{
    rec r;

    if (ok("/MBOX:E:\\MBOX")) {
        TF_EQ(g_a.kind, ARGS_MBOX);
        TF_CHECK(g_a.mbox == g_v[1] + 6);
        TF_STREQ(g_a.mbox, "E:\\MBOX");
        no_tcp();
        no_dir();
        no_net();
        no_cfg();
        no_com();
    }
    if (ok("-mbox:mb/x"))
        TF_STREQ(g_a.mbox, "mb/x");
    if (ok("/Mbox:: /CFG:a")) {
        TF_STREQ(g_a.mbox, ":");
        TF_STREQ(g_a.cfg, "a");
    }
    fails("/MBOX:", ARGS_EBAD, "/MBOX:");
    rec_init(&r);
    if (TF_EQ(run("/MBOX -MBOXX:d", cb, &r), ARGS_OK)) {
        TF_EQ(r.n, 2);
        TF_STREQ(r.seen[0], "MBOX");
        TF_STREQ(r.seen[1], "MBOXX:d");
        TF_EQ(g_a.kind, ARGS_LOCAL);
        TF_CHECK(g_a.mbox == NULL);
    }
    if (ok("/TCP:1.2.3.4"))
        TF_CHECK(g_a.mbox == NULL);
    fails("/MBOX:a /MBOX:b", ARGS_EBAD, "/MBOX:b");
    fails("/MBOX:a /TCP:1.2.3.4", ARGS_ECLASH, "/TCP:1.2.3.4");
    fails("/NET s /mbox:a", ARGS_ECLASH, "/mbox:a");
    fails("/MBOX:a /LOCAL d", ARGS_ECLASH, "/LOCAL");
    fails("/COM1 /MBOX:", ARGS_EBAD, "/MBOX:");
}
#else
/* Without ARGS_WINSW, /TCP: and /MBOX: are words like any other. */
static void test_tcp(void)
{
    rec r;

    rec_init(&r);
    if (TF_EQ(run("/TCP:10.0.2.15 -tcp:1.2.3.4:7000", cb, &r), ARGS_OK)) {
        TF_EQ(r.n, 2);
        TF_STREQ(r.seen[0], "TCP:10.0.2.15");
        TF_STREQ(r.seen[1], "tcp:1.2.3.4:7000");
        TF_EQ(g_a.kind, ARGS_LOCAL);
    }
    if (ok("/NET s /TCP:1.2.3.4"))
        TF_EQ(g_a.kind, ARGS_NET);
}

static void test_mbox(void)
{
    rec r;

    rec_init(&r);
    if (TF_EQ(run("/MBOX:E:\\MBOX", cb, &r), ARGS_OK)) {
        TF_EQ(r.n, 1);
        TF_STREQ(r.seen[0], "MBOX:E:\\MBOX");
        TF_EQ(g_a.kind, ARGS_LOCAL);
    }
}
#endif

static void test_cfg(void)
{
    static const char *const sw[] = { "/CFG:", "/cfg:", "-CFG:", "-Cfg:" };
    int i;
    char line[32];

    for (i = 0; i < 4; i++) {
        sprintf(line, "%sFile.Cfg", sw[i]);
        if (!ok(line))
            continue;
        TF_EQ(g_a.kind, ARGS_LOCAL);
        TF_CHECK(g_a.cfg == g_v[1] + 5);
        TF_STREQ(g_a.cfg, "File.Cfg");
        no_dir();
        no_net();
        no_com();
        fails(sw[i], ARGS_EBAD, sw[i]);
    }
    if (ok("/CFG:a:b\\c.d"))
        TF_STREQ(g_a.cfg, "a:b\\c.d");
    if (ok("/CFG:/x"))
        TF_STREQ(g_a.cfg, "/x");
    fails("/CFG:", ARGS_EBAD, "/CFG:");
    fails("/NET s -cfg:", ARGS_EBAD, "-cfg:");
}

/* Every switch combined with every other in both orders. */
static void test_combine(void)
{
    if (ok("/CFG:c.cfg /LOCAL d")) {
        TF_EQ(g_a.kind, ARGS_LOCAL);
        TF_STREQ(g_a.dir, "d");
        TF_STREQ(g_a.cfg, "c.cfg");
    }
    if (ok("/LOCAL d /CFG:c.cfg")) {
        TF_EQ(g_a.kind, ARGS_LOCAL);
        TF_STREQ(g_a.dir, "d");
        TF_STREQ(g_a.cfg, "c.cfg");
    }
    if (ok("-cfg:c.cfg -net S")) {
        TF_EQ(g_a.kind, ARGS_NET);
        TF_STREQ(g_a.net, "S");
        TF_STREQ(g_a.cfg, "c.cfg");
        TF_CHECK(g_a.cfg == g_v[1] + 5);
        TF_CHECK(g_a.net == g_v[3]);
    }
    if (ok("/NET S /CFG:c.cfg")) {
        TF_EQ(g_a.kind, ARGS_NET);
        TF_STREQ(g_a.net, "S");
        TF_STREQ(g_a.cfg, "c.cfg");
    }
    if (ok("/CFG:c.cfg /COM2")) {
        TF_EQ(g_a.kind, ARGS_COM);
        TF_STREQ(g_a.comname, "COM2");
        TF_STREQ(g_a.cfg, "c.cfg");
    }
    if (ok("-com1 -cfg:c.cfg")) {
        TF_EQ(g_a.kind, ARGS_COM);
        TF_STREQ(g_a.comname, "COM1");
        TF_STREQ(g_a.cfg, "c.cfg");
    }
}

static void test_repeat(void)
{
    fails("/LOCAL /LOCAL", ARGS_EBAD, "/LOCAL");
    fails("/LOCAL d -local", ARGS_EBAD, "-local");
    fails("/NET a /NET a", ARGS_EBAD, "/NET");
    fails("/NET a -NET b", ARGS_EBAD, "-NET");
    fails("/COM1 /COM1", ARGS_EBAD, "/COM1");
    fails("/COM2 -com2", ARGS_EBAD, "-com2");
    fails("/COM1 /COM2", ARGS_EBAD, "/COM2");
    fails("/COM2 /com1", ARGS_EBAD, "/com1");
    fails("/CFG:a /CFG:b", ARGS_EBAD, "/CFG:b");
    fails("/CFG:a -cfg:a", ARGS_EBAD, "-cfg:a");
    fails("/CFG:a /LOCAL /CFG:", ARGS_EBAD, "/CFG:");
}

static void test_clash(void)
{
    fails("/LOCAL /NET s", ARGS_ECLASH, "/NET");
    fails("/NET s /LOCAL", ARGS_ECLASH, "/LOCAL");
    fails("/LOCAL d /COM1", ARGS_ECLASH, "/COM1");
    fails("/COM2 -local", ARGS_ECLASH, "-local");
    fails("/NET s /COM1", ARGS_ECLASH, "/COM1");
    fails("/COM1 -net s", ARGS_ECLASH, "-net");
    /* The first second connection, in the words' order. */
    fails("/LOCAL /NET s /COM1", ARGS_ECLASH, "/NET");
    fails("/COM1 /CFG:a /LOCAL /NET s", ARGS_ECLASH, "/LOCAL");
    fails("/NET s /CFG:a /COM2 /LOCAL", ARGS_ECLASH, "/COM2");
}

static void test_order(void)
{
    rec r;

    /* A scan error comes before any clash. */
    fails("/LOCAL /NET s /CFG:", ARGS_EBAD, "/CFG:");
    fails("/LOCAL /NET s /LOCAL", ARGS_EBAD, "/LOCAL");
    fails("/LOCAL /COM1 /COM1", ARGS_EBAD, "/COM1");
    fails("/LOCAL /COM1 /NET", ARGS_ENONAME, "/NET");
    fails("/COM1 /NET /CFG:a", ARGS_ENONAME, "/NET");
    /* The first scan error of several. */
    fails("/CFG:a /CFG:b /NET", ARGS_EBAD, "/CFG:b");
    fails("/NET /CFG:", ARGS_ENONAME, "/NET");
    fails("/CFG: /NET", ARGS_EBAD, "/CFG:");
    /* A clash is reported before a bad name, whatever the order. */
    fails("/LOCAL /NET *bad", ARGS_ECLASH, "/NET");
    fails("/NET *bad /LOCAL", ARGS_ECLASH, "/LOCAL");
    fails("/COM1 /NET *bad", ARGS_ECLASH, "/NET");
    fails("/NET *bad /COM2", ARGS_ECLASH, "/COM2");
    /* A bad name alone, and after the whole scan. */
    fails("/NET *bad /CFG:a", ARGS_EBADNAME, "*bad");
    /* A refusal by the function is a scan error too. */
    rec_init(&r);
    r.stop = "ZZ";
    r.code = ARGS_EBAD;
    TF_EQ(run("/LOCAL /NET s /ZZ", cb, &r), ARGS_EBAD);
    if (TF_CHECK(g_w != NULL))
        TF_STREQ(g_w, "/ZZ");
    rec_init(&r);
    r.stop = "ZZ";
    r.code = ARGS_EBAD;
    TF_EQ(run("/NET *bad -ZZ", cb, &r), ARGS_EBAD);
    if (TF_CHECK(g_w != NULL))
        TF_STREQ(g_w, "-ZZ");
}

static void test_fn(void)
{
    rec r;

    /* Words not taken go to the function, in order, without the prefix. */
    rec_init(&r);
    if (TF_EQ(run("/Q -r plain /CMD", cb, &r), ARGS_OK)) {
        TF_CHECK(g_w == NULL);
        TF_EQ(r.n, 4);
        TF_STREQ(r.seen[0], "Q");
        TF_STREQ(r.seen[1], "r");
        TF_STREQ(r.seen[2], "");
        TF_STREQ(r.seen[3], "CMD");
    }
    TF_EQ(g_a.kind, ARGS_LOCAL);
    /* A plain word after a switch's own words goes there too. */
    rec_init(&r);
    TF_EQ(run("/LOCAL d e /CFG:c g", cb, &r), ARGS_OK);
    TF_EQ(r.n, 2);
    TF_STREQ(r.seen[0], "");
    TF_STREQ(r.seen[1], "");
    TF_STREQ(g_a.dir, "d");
    TF_STREQ(g_a.cfg, "c");
    rec_init(&r);
    TF_EQ(run("/NET s f", cb, &r), ARGS_OK);
    TF_EQ(r.n, 1);
    TF_STREQ(r.seen[0], "");
    TF_STREQ(g_a.net, "s");
    /* The words taken are never given. */
    rec_init(&r);
    TF_EQ(run("/LOCAL d /CFG:c", cb, &r), ARGS_OK);
    TF_EQ(r.n, 0);
    rec_init(&r);
    TF_EQ(run("/NET s -cfg:c", cb, &r), ARGS_OK);
    TF_EQ(r.n, 0);
    rec_init(&r);
    TF_EQ(run("/com2", cb, &r), ARGS_OK);
    TF_EQ(r.n, 0);
    /* A port takes no word after it; a repeated /LOCAL or /NET takes
       its word as the first does. */
    rec_init(&r);
    TF_EQ(run("/COM1 p", cb, &r), ARGS_OK);
    TF_EQ(r.n, 1);
    TF_STREQ(r.seen[0], "");
    TF_EQ(g_a.com, 1);
    rec_init(&r);
    TF_EQ(run("/LOCAL d /LOCAL e", cb, &r), ARGS_EBAD);
    TF_CHECK(g_w == g_v[3]);
    TF_EQ(r.n, 0);
    rec_init(&r);
    TF_EQ(run("/NET s -net t", cb, &r), ARGS_EBAD);
    TF_CHECK(g_w == g_v[3]);
    TF_EQ(r.n, 0);
    /* A switch after /LOCAL is no dir and goes to the function. */
    rec_init(&r);
    TF_EQ(run("/LOCAL /X", cb, &r), ARGS_OK);
    TF_EQ(r.n, 1);
    TF_STREQ(r.seen[0], "X");
    no_dir();
    /* Near misses are no switches. */
    rec_init(&r);
    TF_EQ(run("/LOCALS /LOC /NETS /NE /COM /COM3 /COM0 /COM12 /CFG /CFGS:a",
              cb, &r), ARGS_OK);
    TF_EQ(r.n, 10);
    TF_STREQ(r.seen[0], "LOCALS");
    TF_STREQ(r.seen[1], "LOC");
    TF_STREQ(r.seen[2], "NETS");
    TF_STREQ(r.seen[3], "NE");
    TF_STREQ(r.seen[4], "COM");
    TF_STREQ(r.seen[5], "COM3");
    TF_STREQ(r.seen[6], "COM0");
    TF_STREQ(r.seen[7], "COM12");
    TF_STREQ(r.seen[8], "CFG");
    TF_STREQ(r.seen[9], "CFGS:a");
    TF_EQ(g_a.kind, ARGS_LOCAL);
    no_cfg();
    no_com();
    /* With no function, the same words are skipped. */
    TF_EQ(run("/LOCALS /COM3 plain /CFG /CFGS:a", NULL, NULL), ARGS_OK);
    TF_CHECK(g_w == NULL);
    TF_EQ(g_a.kind, ARGS_LOCAL);
    no_dir();
    no_cfg();
    no_com();
    /* It is given the program's own switches in any order with ours. */
    rec_init(&r);
    if (TF_EQ(run("/T /NET s /CMD:x -cfg:c /U", cb, &r), ARGS_OK)) {
        TF_EQ(r.n, 3);
        TF_STREQ(r.seen[0], "T");
        TF_STREQ(r.seen[1], "CMD:x");
        TF_STREQ(r.seen[2], "U");
        TF_EQ(g_a.kind, ARGS_NET);
        TF_STREQ(g_a.cfg, "c");
    }
}

/* Keeps the s and arg it is given. */
typedef struct {
    const char *s;
    void *arg;
} ptr;

static int cbp(void *arg, const char *s)
{
    ptr *p;

    p = (ptr *)arg;
    p->s = s;
    p->arg = arg;
    return ARGS_OK;
}

static void test_fnptr(void)
{
    ptr p;

    p.s = NULL;
    p.arg = NULL;
    TF_EQ(run("/Q /LOCAL", cbp, &p), ARGS_OK);
    TF_CHECK(p.arg == (void *)&p);
    TF_CHECK(p.s == g_v[1] + 1);
    TF_STREQ(p.s, "Q");
}

/* line with fn answering code to the word stop: result, *word text
   word, and the callback given count words. */
static void stops(const char *line, const char *stop, int code, int result,
                  const char *word, int count)
{
    rec r;

    rec_init(&r);
    r.stop = stop;
    r.code = code;
    TF_EQ(run(line, cb, &r), result);
    if (TF_CHECK(g_w != NULL))
        TF_STREQ(g_w, word);
    TF_EQ(r.n, count);
}

static void test_stop(void)
{
    rec r;

    /* The function's error is the result, and the scan goes on: it sees
       every later word. */
    stops("/A /B /C", "B", ARGS_EBAD, ARGS_EBAD, "/B", 3);
    stops("/A -B /C", "B", ARGS_EOWN, ARGS_EOWN, "-B", 3);
    stops("/B", "B", ARGS_EOWN + 7, ARGS_EOWN + 7, "/B", 1);
    stops("/LOCAL d word /C", "", ARGS_EBAD, ARGS_EBAD, "word", 2);
    /* The first error in the words wins, ours or the function's. */
    stops("/B /D", "D", ARGS_EOWN, ARGS_EOWN, "/D", 2);
    stops("/B /D", "B", ARGS_EBAD, ARGS_EBAD, "/B", 2);
    stops("/NET /B", "B", ARGS_EOWN, ARGS_ENONAME, "/NET", 1);
    stops("/CFG: /B", "B", ARGS_EOWN, ARGS_EBAD, "/CFG:", 1);
    stops("/B /CFG:", "B", ARGS_EOWN, ARGS_EOWN, "/B", 1);
    stops("/B /NET", "B", ARGS_EBAD, ARGS_EBAD, "/B", 1);
    stops("/CFG:a /CFG:b /B", "B", ARGS_EOWN, ARGS_EBAD, "/CFG:b", 1);
    /* The function's error comes before a clash and a bad name. */
    stops("/LOCAL /NET *x /B", "B", ARGS_EOWN, ARGS_EOWN, "/B", 1);
    stops("/B /LOCAL /NET *x", "B", ARGS_EOWN, ARGS_EOWN, "/B", 1);
    /* Once the function's error is found, a clash or a bad name is not
       reported, and the function still sees every later word. */
    rec_init(&r);
    r.stop = "B";
    r.code = ARGS_EOWN;
    TF_EQ(run("/COM1 /B /LOCAL /X /Y", cb, &r), ARGS_EOWN);
    TF_EQ(r.n, 3);
    TF_STREQ(r.seen[2], "Y");
    /* Codes: the library's own are below the caller's. */
    TF_CHECK(ARGS_OK < ARGS_EBAD);
    TF_CHECK(ARGS_EBAD < ARGS_ENONAME);
    TF_CHECK(ARGS_ENONAME < ARGS_ECLASH);
    TF_CHECK(ARGS_ECLASH < ARGS_EBADNAME);
    TF_CHECK(ARGS_EBADNAME < ARGS_EOWN);
    /* The connection kinds are distinct bits, and a port's name fits. */
    TF_CHECK(ARGS_LOCAL != 0 && ARGS_NET != 0 && ARGS_COM != 0);
    TF_CHECK((ARGS_LOCAL & ARGS_NET) == 0);
    TF_CHECK((ARGS_LOCAL & ARGS_COM) == 0);
    TF_CHECK((ARGS_NET & ARGS_COM) == 0);
    TF_CHECK(ARGS_COMSZ >= (int)sizeof "COM1");
}

/* Words taken directly from an argv, empty ones too; *word is the
   element of argv itself. */
static void test_argv(void)
{
    char *p1[3], *p2[2], *p3[3], *p4[2], *p5[3], *p6[3];

    p1[0] = "P"; p1[1] = "/LOCAL"; p1[2] = "";
    memset(&g_a, 0x55, sizeof g_a);
    g_w = NULL;
    TF_EQ(args_parse(&g_a, 3, p1, NULL, NULL, &g_w), ARGS_OK);
    TF_CHECK(g_w == NULL);
    TF_EQ(g_a.kind, ARGS_LOCAL);
    TF_CHECK(g_a.dir == p1[2]);
    p2[0] = "P"; p2[1] = "";
    g_w = NULL;
    TF_EQ(args_parse(&g_a, 2, p2, NULL, NULL, &g_w), ARGS_OK);
    TF_EQ(g_a.kind, ARGS_LOCAL);
    p3[0] = "P"; p3[1] = "/NET"; p3[2] = "";
    g_w = NULL;
    TF_EQ(args_parse(&g_a, 3, p3, NULL, NULL, &g_w), ARGS_EBADNAME);
    TF_CHECK(g_w == p3[2]);
    p4[0] = "P"; p4[1] = "/";
    g_w = NULL;
    TF_EQ(args_parse(&g_a, 2, p4, NULL, NULL, &g_w), ARGS_OK);
    TF_EQ(g_a.kind, ARGS_LOCAL);
    p5[0] = "P"; p5[1] = "/NET"; p5[2] = "-";
    g_w = NULL;
    TF_EQ(args_parse(&g_a, 3, p5, NULL, NULL, &g_w), ARGS_ENONAME);
    TF_CHECK(g_w == p5[1]);
    p6[0] = "P"; p6[1] = "/CFG:"; p6[2] = "/NET";
    g_w = NULL;
    TF_EQ(args_parse(&g_a, 3, p6, NULL, NULL, &g_w), ARGS_EBAD);
    TF_CHECK(g_w == p6[1]);
}

int main(void)
{
    tf_begin(TA_NAME);
    test_none();
    test_local();
    test_net();
    test_com();
    test_tcp();
    test_mbox();
    test_cfg();
    test_combine();
    test_repeat();
    test_clash();
    test_order();
    test_fn();
    test_fnptr();
    test_stop();
    test_argv();
    return tf_done();
}

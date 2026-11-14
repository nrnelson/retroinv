/*
 * T_TOKEN.C - unit tests for the protocol's line assembly, tokenizer,
 * response writer and comma-list matcher (TOKEN.C) and for the session
 * state changes (SESSION.C).
 *
 * usage: T_TOKEN
 *
 * No files are read or written.  The writer is checked byte for byte
 * through a sink that collects everything it is given, can report a
 * chosen amount of room, and can be told to fail from a chosen write on.
 * Checks:
 *   - line assembly: CR LF and LF ends, a lone CR as data, lines of
 *     PROTO_MAXLINE bytes and longer ones, every byte value, and the
 *     wiping of consumed lines;
 *   - the tokenizer: positional tokens and pairs, quoting and escapes,
 *     malformed input, and the PROTO_MAXARGS, PROTO_MAXPAIRS and
 *     PROTO_MAXLINE limits;
 *   - the writer: +OK and -ERR lines, the error texts, +DATA blocks with
 *     every kind of line and value, dot-stuffing, lines cut at
 *     PROTO_MAXRESP, and sink failures;
 *   - the comma-list matcher proto_in_list;
 *   - sessions: HELLO, read-only stations, LOGIN, PASSWD, failed logins,
 *     BYE and a station fixed by the transport.
 * Exit status 0 when every check passes, 1 when one fails.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "TESTFW.H"

/* ---------------------------------------------------------------------
 * The test sink.
 * --------------------------------------------------------------------- */
#define SINK_MAX        2048

typedef struct {
    char buf[SINK_MAX + 1];
    u16  len;
    u16  writes;                /* calls of the write function          */
    u16  failat;                /* write number that fails, 0 for none  */
    u16  room;
} sink;

static sink g_sink;
static proto_out g_out;
static proto_acc g_acc;
static proto_req g_req;
static proto_session g_sess;
static char g_line[PROTO_MAXLINE + 2];  /* requests, one byte too long  */
static char g_big[2 * PROTO_MAXRESP];   /* values longer than a response
                                           line, for the cut tests      */

static int sink_write(void *arg, const char *buf, u16 len)
{
    sink *k;

    k = (sink *)arg;
    k->writes++;
    if (k->failat != 0 && k->writes >= k->failat)
        return 1;
    if ((u32)k->len + len > SINK_MAX)
        return 1;
    memcpy(k->buf + k->len, buf, len);
    k->len = (u16)(k->len + len);
    k->buf[k->len] = '\0';
    return 0;
}

static u16 sink_room(void *arg)
{
    return ((sink *)arg)->room;
}

static void sink_reset(void)
{
    g_sink.len = 0;
    g_sink.buf[0] = '\0';
    g_sink.writes = 0;
    g_sink.failat = 0;
    g_sink.room = 0;
    proto_out_init(&g_out, sink_write, NULL, &g_sink);
}

/* ---------------------------------------------------------------------
 * Line assembly.
 * --------------------------------------------------------------------- */

/* Feed n bytes of s; returns the result of the last feed and checks
   that every earlier one returned PROTO_ACC_NONE. */
static int feed(const char *s, u16 n)
{
    u16 i;
    int rc;
    int early;

    rc = PROTO_ACC_NONE;
    early = 0;
    for (i = 0; i < n; i++) {
        rc = proto_acc_feed(&g_acc, (u8)s[i]);
        if (i + 1 < n && rc != PROTO_ACC_NONE)
            early = 1;
    }
    TF_EQ(early, 0);
    return rc;
}

/* Feed n copies of byte c. */
static void feed_fill(int c, u16 n)
{
    u16 i;
    int bad;

    bad = 0;
    for (i = 0; i < n; i++)
        if (proto_acc_feed(&g_acc, c) != PROTO_ACC_NONE)
            bad = 1;
    TF_EQ(bad, 0);
}

static void test_acc_ends(void)
{
    proto_acc_init(&g_acc);
    TF_EQ(feed("PING\r\n", 6), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 4);
    TF_STREQ(g_acc.buf, "PING");

    /* The line stays until the next feed, which starts a new one. */
    TF_STREQ(g_acc.buf, "PING");
    TF_EQ(feed("VER\n", 4), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 3);
    TF_STREQ(g_acc.buf, "VER");

    /* A lone CR is data; only the CR before LF is dropped. */
    TF_EQ(feed("A\rB\r\n", 5), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 3);
    TF_STREQ(g_acc.buf, "A\rB");
    TF_EQ(feed("\r\r\n", 3), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 1);
    TF_STREQ(g_acc.buf, "\r");
    TF_EQ(feed("X\r", 2), PROTO_ACC_NONE);
    TF_EQ(feed("\n", 1), PROTO_ACC_LINE);
    TF_STREQ(g_acc.buf, "X");
}

static void test_acc_maxline(void)
{
    /* Empty lines. */
    TF_EQ(feed("\r\n", 2), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 0);
    TF_EQ(feed("\n", 1), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 0);

    /* PROTO_MAXLINE bytes fit, with CR LF and with LF alone. */
    feed_fill('A', PROTO_MAXLINE);
    TF_EQ(feed("\r\n", 2), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, PROTO_MAXLINE);
    TF_EQ(g_acc.buf[PROTO_MAXLINE - 1], 'A');
    TF_EQ(g_acc.buf[PROTO_MAXLINE], 0);
    feed_fill('B', PROTO_MAXLINE);
    TF_EQ(feed("\n", 1), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, PROTO_MAXLINE);
}

static void test_acc_overlong(void)
{
    /* One byte more is reported once, at its LF, then all is in step. */
    feed_fill('C', PROTO_MAXLINE + 1);
    TF_EQ(feed("\r\n", 2), PROTO_ACC_LONG);
    TF_EQ(feed("PING\r\n", 6), PROTO_ACC_LINE);
    TF_STREQ(g_acc.buf, "PING");
    feed_fill('D', PROTO_MAXLINE + 1);
    TF_EQ(feed("\n", 1), PROTO_ACC_LONG);
    TF_EQ(feed("BYE\n", 4), PROTO_ACC_LINE);
    TF_STREQ(g_acc.buf, "BYE");
    feed_fill('E', PROTO_MAXLINE);
    TF_EQ(feed("\r\r\n", 3), PROTO_ACC_LONG);
    feed_fill('F', 1000);
    feed_fill('\r', 3);
    TF_EQ(feed("\n", 1), PROTO_ACC_LONG);
    TF_EQ(feed("OK\n", 3), PROTO_ACC_LINE);
    TF_STREQ(g_acc.buf, "OK");

    /* NUL bytes are data and counted. */
    TF_EQ(feed("A\0B\n", 4), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 3);
    TF_EQ(g_acc.buf[1], 0);
    TF_EQ(g_acc.buf[2], 'B');
}

static void test_acc_bytes(void)
{
    u16 i;
    int ok;
    int c;

    /* Bytes 0..127 except LF, then the CP437 high bytes 80..FF. */
    for (c = 0; c < 128; c++)
        if (c != '\n')
            proto_acc_feed(&g_acc, c);
    TF_EQ(proto_acc_feed(&g_acc, '\n'), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 127);
    ok = 1;
    for (i = 0; i < 127; i++)
        if ((u8)g_acc.buf[i] != (u8)(i < 10 ? i : i + 1))
            ok = 0;
    TF_EQ(ok, 1);
    for (c = 128; c < 256; c++)
        proto_acc_feed(&g_acc, c);
    TF_EQ(proto_acc_feed(&g_acc, '\n'), PROTO_ACC_LINE);
    TF_EQ(g_acc.len, 128);
    ok = 1;
    for (i = 0; i < 128; i++)
        if ((u8)g_acc.buf[i] != (u8)(128 + i))
            ok = 0;
    TF_EQ(ok, 1);
}

static void test_acc_wipe(void)
{
    u16 i;
    int ok;

    /* A consumed line leaves nothing behind: a shorter next line finds
       the rest of the buffer zeroed, so no password lingers. */
    TF_EQ(feed("LOGIN ADMIN secret1\r\n", 21), PROTO_ACC_LINE);
    TF_EQ(feed("PING\r\n", 6), PROTO_ACC_LINE);
    ok = 1;
    for (i = 4; i < sizeof g_acc.buf; i++)
        if (g_acc.buf[i] != 0)
            ok = 0;
    TF_EQ(ok, 1);
    /* So does a discarded over-long line. */
    feed_fill('S', 300);
    TF_EQ(feed("\n", 1), PROTO_ACC_LONG);
    ok = 1;
    for (i = 0; i < sizeof g_acc.buf; i++)
        if (g_acc.buf[i] != 0)
            ok = 0;
    TF_EQ(ok, 1);

    /* proto_acc_wipe wipes a complete line at once, and leaves a
       partial one, and the assembler's state, as they are. */
    TF_EQ(feed("LOGIN ADMIN secret1\n", 20), PROTO_ACC_LINE);
    proto_acc_wipe(&g_acc);
    ok = 1;
    for (i = 0; i < sizeof g_acc.buf; i++)
        if (g_acc.buf[i] != 0)
            ok = 0;
    TF_EQ(ok, 1);
    TF_EQ(feed("PI", 2), PROTO_ACC_NONE);
    proto_acc_wipe(&g_acc);
    TF_EQ(feed("NG\n", 3), PROTO_ACC_LINE);
    TF_STREQ(g_acc.buf, "PING");

    /* init wipes the whole buffer, whatever it held. */
    memset(g_acc.buf, 'X', sizeof g_acc.buf);
    proto_acc_init(&g_acc);
    ok = 1;
    for (i = 0; i < sizeof g_acc.buf; i++)
        if (g_acc.buf[i] != 0)
            ok = 0;
    TF_EQ(ok, 1);
    TF_EQ(g_acc.len, 0);

    /* init drops a partial line and an over-long state. */
    feed_fill('G', 300);
    proto_acc_init(&g_acc);
    TF_EQ(feed("HELLO PC\n", 9), PROTO_ACC_LINE);
    TF_STREQ(g_acc.buf, "HELLO PC");
}

static void test_acc(void)
{
    test_acc_ends();
    test_acc_maxline();
    test_acc_overlong();
    test_acc_bytes();
    test_acc_wipe();
}

/* ---------------------------------------------------------------------
 * Tokenizer.
 * --------------------------------------------------------------------- */

static int parse(const char *s)
{
    return proto_parse(&g_req, s, (u16)strlen(s));
}

static void test_parse_basic(void)
{
    TF_EQ(parse("ITEM GET 12"), ENG_OK);
    TF_EQ(g_req.nargs, 3);
    TF_EQ(g_req.npairs, 0);
    TF_STREQ(g_req.arg[0], "ITEM");
    TF_STREQ(g_req.arg[1], "GET");
    TF_STREQ(g_req.arg[2], "12");

    /* Spaces and tabs separate; positional tokens keep their case. */
    TF_EQ(parse("  item \t get\t\t0012  "), ENG_OK);
    TF_EQ(g_req.nargs, 3);
    TF_STREQ(g_req.arg[0], "item");
    TF_STREQ(g_req.arg[1], "get");
    TF_STREQ(g_req.arg[2], "0012");

    TF_EQ(parse(""), ENG_OK);
    TF_EQ(g_req.nargs, 0);
    TF_EQ(g_req.npairs, 0);
    TF_EQ(parse("    \t "), ENG_OK);
    TF_EQ(g_req.nargs, 0);
    TF_EQ(g_req.npairs, 0);

    /* Keys are upper-cased, values keep their case. */
    TF_EQ(parse("ITEM LIST cat=Cpu Loc=a3 _x9=Y"), ENG_OK);
    TF_EQ(g_req.nargs, 2);
    TF_EQ(g_req.npairs, 3);
    TF_STREQ(g_req.pair[0].key, "CAT");
    TF_STREQ(g_req.pair[0].val, "Cpu");
    TF_STREQ(g_req.pair[1].key, "LOC");
    TF_STREQ(g_req.pair[1].val, "a3");
    TF_STREQ(g_req.pair[2].key, "_X9");
    TF_STREQ(g_req.pair[2].val, "Y");
    TF_STREQ(proto_get(&g_req, "LOC"), "a3");
    TF_CHECK(proto_get(&g_req, "ZONE") == NULL);
    TF_CHECK(proto_get(&g_req, "loc") == NULL);

    /* Only pairs: no command word, which the dispatcher refuses. */
    TF_EQ(parse("A=1"), ENG_OK);
    TF_EQ(g_req.nargs, 0);
    TF_EQ(g_req.npairs, 1);

    /* Tokens live in the request, not in the line. */
    strcpy(g_line, "LOC GET A3 X=yz");
    TF_EQ(parse(g_line), ENG_OK);
    memset(g_line, '#', strlen(g_line));
    TF_STREQ(g_req.arg[2], "A3");
    TF_STREQ(g_req.pair[0].val, "yz");

    /* A line given by length need not end in a NUL. */
    TF_EQ(proto_parse(&g_req, "PING JUNK", 4), ENG_OK);
    TF_EQ(g_req.nargs, 1);
    TF_STREQ(g_req.arg[0], "PING");
}

static void test_parse_quotes(void)
{
    TF_EQ(parse("ITEM FIND \"8-bit ISA\" PAGE=5"), ENG_OK);
    TF_EQ(g_req.nargs, 3);
    TF_STREQ(g_req.arg[2], "8-bit ISA");
    TF_STREQ(proto_get(&g_req, "PAGE"), "5");

    /* Quotes keep spaces, tabs and '='. */
    TF_EQ(parse("X \"a=b\" D=\"two  words\tand=more\""), ENG_OK);
    TF_EQ(g_req.nargs, 2);
    TF_STREQ(g_req.arg[1], "a=b");
    TF_STREQ(proto_get(&g_req, "D"), "two  words\tand=more");

    /* Every escape, hex digits in either case. */
    TF_EQ(parse("X V=\"q\\\"b\\\\s\\nl\\x41\\xff\\x7F\\xAb\""), ENG_OK);
    TF_STREQ(proto_get(&g_req, "V"), "q\"b\\s\nlA\377\177\253");
    TF_EQ(parse("X \"\\x01\\x1f\\x20\""), ENG_OK);
    TF_STREQ(g_req.arg[1], "\001\037 ");

    /* Outside quotes a backslash is ordinary. */
    TF_EQ(parse("EXPORT ITEMS FILE=C:\\DATA\\X.CSV C:\\n"), ENG_ESYNTAX);
    TF_EQ(parse("X C:\\n FILE=C:\\DATA\\X.CSV"), ENG_OK);
    TF_STREQ(g_req.arg[1], "C:\\n");
    TF_STREQ(proto_get(&g_req, "FILE"), "C:\\DATA\\X.CSV");

    /* Empty values. */
    TF_EQ(parse("X \"\" A= B=\"\""), ENG_OK);
    TF_EQ(g_req.nargs, 2);
    TF_STREQ(g_req.arg[1], "");
    TF_STREQ(proto_get(&g_req, "A"), "");
    TF_STREQ(proto_get(&g_req, "B"), "");

    /* The value of a bare pair is everything after the first '='. */
    TF_EQ(parse("X A=b=c=="), ENG_OK);
    TF_STREQ(proto_get(&g_req, "A"), "b=c==");

    /* CP437 bytes pass unchanged, bare and quoted. */
    TF_EQ(parse("X \202\377 V=\"\341 \260\""), ENG_OK);
    TF_STREQ(g_req.arg[1], "\202\377");
    TF_STREQ(proto_get(&g_req, "V"), "\341 \260");

    /* Malformed quoting and escapes. */
    TF_EQ(parse("X \"\\q\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\t\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\x4\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\x4"), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\x4g\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\xg4\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\x00\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"abc"), ENG_ESYNTAX);
    TF_EQ(parse("X V=\"abc"), ENG_ESYNTAX);
    TF_EQ(parse("X \"abc\\"), ENG_ESYNTAX);
    TF_EQ(parse("X \"abc\\\""), ENG_ESYNTAX);
    TF_EQ(parse("X \"a\"b"), ENG_ESYNTAX);
    TF_EQ(parse("X V=\"a\"b"), ENG_ESYNTAX);
    TF_EQ(parse("X V=\"a\"\"b\""), ENG_ESYNTAX);
    TF_EQ(parse("X a\"b"), ENG_ESYNTAX);
    TF_EQ(parse("X V=a\"b"), ENG_ESYNTAX);
    TF_EQ(parse("X \"a\"=b"), ENG_ESYNTAX);
    TF_EQ(parse("X \"\\\""), ENG_ESYNTAX);
    TF_EQ(proto_parse(&g_req, "X A\0B", 5), ENG_ESYNTAX);
    TF_EQ(proto_parse(&g_req, "X \"\0\"", 5), ENG_ESYNTAX);

    /* A quote closed at the end of the line, and before a tab. */
    TF_EQ(parse("X \"end\""), ENG_OK);
    TF_STREQ(g_req.arg[1], "end");
    TF_EQ(parse("X V=\"a\"\tW=1"), ENG_OK);
    TF_STREQ(proto_get(&g_req, "V"), "a");
    TF_STREQ(proto_get(&g_req, "W"), "1");
}

static void test_parse_limits(void)
{
    char key[16];
    u16 i;
    u16 n;
    u16 head;

    /* Keys. */
    TF_EQ(parse("X =v"), ENG_ESYNTAX);
    TF_EQ(parse("X ="), ENG_ESYNTAX);
    TF_EQ(parse("X 1A=v"), ENG_ESYNTAX);
    TF_EQ(parse("X A-B=v"), ENG_ESYNTAX);
    TF_EQ(parse("X A\377=v"), ENG_ESYNTAX);
    TF_EQ(parse("X ABCDEFGHIJK=v"), ENG_ESYNTAX);
    TF_EQ(parse("X abcdefghij=v"), ENG_OK);
    TF_STREQ(g_req.pair[0].key, "ABCDEFGHIJ");

    /* Positional tokens come first. */
    TF_EQ(parse("X A=1 B"), ENG_ESYNTAX);
    TF_EQ(parse("X A=1 \"B\""), ENG_ESYNTAX);

    /* Repeated keys, compared after upper-casing. */
    TF_EQ(parse("X a=1 B=2 A=3"), ENG_EARG);
    TF_STREQ(g_req.bad, "A");
    TF_EQ(parse("X A=1 A=1"), ENG_EARG);
    TF_STREQ(g_req.bad, "A");
    TF_EQ(parse("X A=1 A=2 \"oops"), ENG_ESYNTAX);
    TF_EQ(parse("X A=1"), ENG_OK);
    TF_STREQ(g_req.bad, "");

    /* PROTO_MAXARGS positional tokens, and one more. */
    TF_EQ(parse("1 2 3 4 5 6 7 8"), ENG_OK);
    TF_EQ(g_req.nargs, PROTO_MAXARGS);
    TF_STREQ(g_req.arg[7], "8");
    TF_EQ(parse("1 2 3 4 5 6 7 8 9"), ENG_ESYNTAX);
    TF_EQ(parse("1 2 3 4 5 6 7 8 \"9\""), ENG_ESYNTAX);

    /* PROTO_MAXPAIRS pairs, and one more. */
    strcpy(g_line, "X");
    for (i = 0; i < PROTO_MAXPAIRS; i++) {
        sprintf(key, " K%u=v", (unsigned)i);
        strcat(g_line, key);
    }
    TF_EQ(parse(g_line), ENG_OK);
    TF_EQ(g_req.npairs, PROTO_MAXPAIRS);
    TF_STREQ(proto_get(&g_req, "K23"), "v");
    strcat(g_line, " K99=v");
    TF_EQ(parse(g_line), ENG_ESYNTAX);

    /* PROTO_MAXLINE bytes, and one more. */
    memset(g_line, 'A', PROTO_MAXLINE + 1);
    g_line[PROTO_MAXLINE] = '\0';
    TF_EQ(parse(g_line), ENG_OK);
    TF_EQ(strlen(g_req.arg[0]), PROTO_MAXLINE);
    g_line[PROTO_MAXLINE] = 'A';
    g_line[PROTO_MAXLINE + 1] = '\0';
    TF_EQ(parse(g_line), ENG_ETOOLONG);

    /* The fullest line the buffer must hold: every token one byte. */
    strcpy(g_line, "1 2 3 4 5 6 7 8");
    head = (u16)strlen(g_line);
    for (i = 0; i < PROTO_MAXPAIRS; i++) {
        sprintf(key, " %c=", (char)('A' + i));
        strcat(g_line, key);
    }
    n = (u16)strlen(g_line);
    while (n < PROTO_MAXLINE)
        g_line[n++] = 'z';
    g_line[n] = '\0';
    TF_EQ(parse(g_line), ENG_OK);
    TF_EQ(g_req.npairs, PROTO_MAXPAIRS);
    TF_EQ(strlen(proto_get(&g_req, "X")),
          PROTO_MAXLINE - head - 3 * PROTO_MAXPAIRS);
}

/* ---------------------------------------------------------------------
 * Response writer.
 * --------------------------------------------------------------------- */

static void test_out_simple(void)
{
    /* Every error code the engine defines. */
    static const int codes[] = {
        ENG_ESYNTAX, ENG_EUNKNOWN, ENG_EARG, ENG_ETOOLONG, ENG_ENOTAVAIL,
        ENG_ENOTFOUND, ENG_EDUP, ENG_ECONSTR, ENG_EPERM, ENG_ECURSOR,
        ENG_EIO, ENG_EINDEX, ENG_EFULL, ENG_EHANDLES, ENG_EINUSE,
        ENG_ENOLOGIN, ENG_EBADPW, ENG_EBUSY, ENG_EDISABLED, ENG_EMUSTCHG,
        ENG_EINTERNAL, ENG_ESHUTDOWN
    };
    u16 i;

    sink_reset();
    TF_EQ(proto_out_room(&g_out), 0xFFFFu);
    proto_ok(&g_out, NULL);
    proto_ok(&g_out, "");
    proto_ok(&g_out, "LEVEL=3 NAME=\"Full Name\"");
    TF_STREQ(g_sink.buf,
             "+OK\r\n+OK\r\n+OK LEVEL=3 NAME=\"Full Name\"\r\n");
    TF_EQ(g_sink.writes, 3);

    sink_reset();
    proto_err(&g_out, ENG_ENOTFOUND, NULL, NULL);
    proto_err(&g_out, ENG_EARG, NULL, "ID");
    proto_err(&g_out, ENG_EARG, "bad id", "");
    proto_err(&g_out, ENG_ECONSTR, "location occupied", NULL);
    proto_err(&g_out, ENG_EINTERNAL, "", NULL);
    TF_STREQ(g_sink.buf,
             "-ERR 200 not found\r\n"
             "-ERR 102 bad or missing argument FIELD=ID\r\n"
             "-ERR 102 bad id\r\n"
             "-ERR 202 location occupied\r\n"
             "-ERR 500\r\n");

    TF_STREQ(proto_errtext(ENG_ETOOLONG), "line too long");
    TF_STREQ(proto_errtext(ENG_EBADPW), "bad user name or password");
    TF_STREQ(proto_errtext(999), "error");
    TF_STREQ(proto_errtext(0), "error");
    for (i = 0; i < sizeof codes / sizeof codes[0]; i++)
        TF_CHECK(strcmp(proto_errtext(codes[i]), "error") != 0);

    /* The room callback. */
    proto_out_init(&g_out, sink_write, sink_room, &g_sink);
    g_sink.room = 1234;
    TF_EQ(proto_out_room(&g_out), 1234);
    g_sink.room = 0;
    TF_EQ(proto_out_room(&g_out), 0);
}

static void test_out_data(void)
{
    sink_reset();
    proto_data_begin(&g_out);
    TF_EQ(g_out.indata, 1);
    proto_line_begin(&g_out, "F");
    proto_line_word(&g_out, "ITEM_ID");
    proto_line_word(&g_out, "DESCR");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "R");
    proto_line_value(&g_out, "abc");
    proto_line_value(&g_out, "");
    proto_line_value(&g_out, "a b");
    proto_line_value(&g_out, "q\"");
    proto_line_value(&g_out, "back\\");
    proto_line_value(&g_out, "x=y");
    proto_line_value(&g_out, "l1\nl2");
    proto_line_value(&g_out, "\001\037\177\t");
    proto_line_value(&g_out, "\202\351\377");
    proto_line_value(&g_out, ".5");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "K");
    proto_line_kv(&g_out, "MODEL", "PC/XT 5160");
    proto_line_kv(&g_out, "QTY", "1");
    proto_line_kv(&g_out, "NOTE", "");
    proto_line_num(&g_out, "N", 0);
    proto_line_num(&g_out, "MAX", 4294967295UL);
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "H");
    proto_line_word(&g_out, "Bin  A3");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "L");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "P");
    proto_line_end(&g_out);
    /* Lines beginning with '.' are dot-stuffed inside +DATA. */
    proto_line_begin(&g_out, ".");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "..x");
    proto_line_word(&g_out, "y");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "C");
    proto_line_word(&g_out, "ITEMS1:41:12");
    proto_line_end(&g_out);
    proto_data_end(&g_out);
    TF_EQ(g_out.indata, 0);
    TF_STREQ(g_sink.buf,
             "+DATA\r\n"
             "F ITEM_ID DESCR\r\n"
             "R abc \"\" \"a b\" \"q\\\"\" \"back\\\\\" \"x=y\" "
             "\"l1\\nl2\" \"\\x01\\x1F\\x7F\\x09\" \202\351\377 .5\r\n"
             "K MODEL=\"PC/XT 5160\" QTY=1 NOTE=\"\" N=0 MAX=4294967295\r\n"
             "H Bin  A3\r\n"
             "L\r\n"
             "P\r\n"
             "..\r\n"
             "...x y\r\n"
             "C ITEMS1:41:12\r\n"
             ".\r\n");
    TF_EQ(g_sink.writes, 11);

    /* Outside +DATA nothing is stuffed. */
    sink_reset();
    proto_line_begin(&g_out, ".x");
    proto_line_end(&g_out);
    TF_STREQ(g_sink.buf, ".x\r\n");

    /* An empty data block. */
    sink_reset();
    proto_data_begin(&g_out);
    proto_data_end(&g_out);
    TF_STREQ(g_sink.buf, "+DATA\r\n.\r\n");
}

static void test_cut_fill(void)
{
    /* "R" and one value of PROTO_MAXRESP - 2 bytes fill the line. */
    sink_reset();
    memset(g_big, 'v', PROTO_MAXRESP - 2);
    g_big[PROTO_MAXRESP - 2] = '\0';
    proto_line_begin(&g_out, "R");
    proto_line_value(&g_out, g_big);
    TF_EQ(g_out.cut, 0);
    TF_EQ(g_out.len, PROTO_MAXRESP);
    proto_line_value(&g_out, "x");
    TF_EQ(g_out.cut, 1);
    proto_line_word(&g_out, "y");
    proto_line_kv(&g_out, "K", "");
    proto_line_num(&g_out, "N", 1);
    proto_line_end(&g_out);
    TF_EQ(g_sink.len, PROTO_MAXRESP + 2);
    TF_EQ(g_sink.buf[0], 'R');
    TF_EQ(g_sink.buf[1], ' ');
    TF_EQ(g_sink.buf[PROTO_MAXRESP - 1], 'v');
    TF_EQ(g_sink.buf[PROTO_MAXRESP], '\r');
    TF_EQ(g_sink.buf[PROTO_MAXRESP + 1], '\n');

    /* The flag starts clear on every line. */
    proto_line_begin(&g_out, "R");
    TF_EQ(g_out.cut, 0);
    proto_line_end(&g_out);
}

static void test_cut_parts(void)
{
    u16 n;

    /* A quoted value is measured with its escapes and left out whole,
       while a later, shorter part still fits. */
    sink_reset();
    memset(g_big, '"', 200);
    g_big[200] = '\0';
    proto_line_begin(&g_out, "R");
    proto_line_value(&g_out, g_big);
    TF_EQ(g_out.cut, 0);
    TF_EQ(g_out.len, 1 + 1 + 2 + 400);
    proto_line_value(&g_out, g_big);
    TF_EQ(g_out.cut, 1);
    TF_EQ(g_out.len, 404);
    proto_line_value(&g_out, "tail");
    proto_line_end(&g_out);
    n = g_sink.len;
    TF_EQ(n, 404 + 5 + 2);
    TF_CHECK(memcmp(g_sink.buf + 404, " tail\r\n", 7) == 0);

    /* A key and value that would end one byte past the limit. */
    sink_reset();
    memset(g_big, 'k', PROTO_MAXRESP - 3);
    g_big[PROTO_MAXRESP - 3] = '\0';
    proto_line_begin(&g_out, "K");
    proto_line_kv(&g_out, "A", g_big);
    TF_EQ(g_out.cut, 1);
    TF_EQ(g_out.len, 1);
    g_big[PROTO_MAXRESP - 4] = '\0';
    proto_line_kv(&g_out, "A", g_big);
    TF_EQ(g_out.cut, 1);
    TF_EQ(g_out.len, PROTO_MAXRESP);
    proto_line_end(&g_out);
    TF_EQ(g_sink.len, PROTO_MAXRESP + 2);
}

static void test_cut_dot(void)
{
    /* A line that starts with '.' inside +DATA keeps a byte for the
       stuffing: one part too many is cut, and the stuffed line sent is
       PROTO_MAXRESP bytes and CR LF. */
    sink_reset();
    proto_data_begin(&g_out);
    memset(g_big, 't', PROTO_MAXRESP - 2);
    g_big[PROTO_MAXRESP - 2] = '\0';
    proto_line_begin(&g_out, ".");
    proto_line_word(&g_out, g_big);
    TF_EQ(g_out.cut, 1);
    TF_EQ(g_out.len, 1);
    g_big[PROTO_MAXRESP - 3] = '\0';
    proto_line_word(&g_out, g_big);
    TF_EQ(g_out.len, PROTO_MAXRESP - 1);
    proto_line_end(&g_out);
    proto_data_end(&g_out);
    TF_EQ(g_sink.len, 7 + PROTO_MAXRESP + 2 + 3);
    TF_CHECK(memcmp(g_sink.buf + 7, ".. t", 4) == 0);
    TF_CHECK(memcmp(g_sink.buf + 7 + PROTO_MAXRESP, "\r\n.\r\n", 5)
             == 0);

    /* A first word that begins with '.', inside and outside +DATA. */
    sink_reset();
    proto_data_begin(&g_out);
    memset(g_big, '.', PROTO_MAXRESP);
    g_big[PROTO_MAXRESP] = '\0';
    proto_line_begin(&g_out, g_big);
    TF_EQ(g_out.cut, 1);
    TF_EQ(g_out.len, 0);
    proto_data_end(&g_out);
    proto_line_begin(&g_out, g_big);
    TF_EQ(g_out.cut, 0);
    TF_EQ(g_out.len, PROTO_MAXRESP);

    /* Parts far longer than a line are measured without overflow. */
    sink_reset();
    memset(g_big, '\001', 2 * PROTO_MAXRESP - 1);
    g_big[2 * PROTO_MAXRESP - 1] = '\0';
    proto_line_begin(&g_out, "R");
    proto_line_value(&g_out, g_big);
    TF_EQ(g_out.cut, 1);
    proto_line_kv(&g_out, g_big, g_big);
    proto_line_word(&g_out, g_big);
    TF_EQ(g_out.len, 1);
    memset(g_big, 'w', 2 * PROTO_MAXRESP - 1);
    proto_line_value(&g_out, g_big);
    proto_line_kv(&g_out, "K", g_big);
    proto_line_kv(&g_out, g_big, "v");
    TF_EQ(g_out.len, 1);
    proto_line_end(&g_out);
    TF_STREQ(g_sink.buf, "R\r\n");
}

static void test_out_cut(void)
{
    test_cut_fill();
    test_cut_parts();
    test_cut_dot();
}

static void test_out_errors(void)
{
    /* A failure inside +DATA ends the block with K ERROR. */
    sink_reset();
    proto_data_begin(&g_out);
    proto_line_begin(&g_out, "F");
    proto_line_word(&g_out, "ITEM_ID");
    proto_line_end(&g_out);
    proto_line_begin(&g_out, "R");
    proto_line_value(&g_out, "1");
    proto_line_end(&g_out);
    proto_err(&g_out, ENG_EIO, NULL, "ITEMS");
    TF_EQ(g_out.indata, 0);
    TF_STREQ(g_sink.buf,
             "+DATA\r\nF ITEM_ID\r\nR 1\r\nK ERROR=300\r\n.\r\n");

    /* The same written by a handler itself. */
    sink_reset();
    proto_data_begin(&g_out);
    proto_line_begin(&g_out, "K");
    proto_line_num(&g_out, "ERROR", ENG_EINDEX);
    proto_line_end(&g_out);
    proto_data_end(&g_out);
    TF_STREQ(g_sink.buf, "+DATA\r\nK ERROR=301\r\n.\r\n");

    /* A sink error is remembered and later output dropped. */
    sink_reset();
    g_sink.failat = 2;
    TF_EQ(proto_out_failed(&g_out), 0);
    proto_ok(&g_out, "one");
    TF_EQ(proto_out_failed(&g_out), 0);
    proto_ok(&g_out, "two");
    TF_EQ(proto_out_failed(&g_out), 1);
    proto_ok(&g_out, "three");
    proto_data_begin(&g_out);
    proto_data_end(&g_out);
    TF_EQ(proto_out_failed(&g_out), 1);
    TF_EQ(g_sink.writes, 2);
    TF_STREQ(g_sink.buf, "+OK one\r\n");
}

/* ---------------------------------------------------------------------
 * Sessions.
 * --------------------------------------------------------------------- */

static void login(u8 level, int mustchg)
{
    eng_login l;

    memset(&l, 0, sizeof l);
    l.level = level;
    l.mustchg = mustchg;
    strcpy(l.user, "NORMAN");
    strcpy(l.fullname, "Norman Fisher");
    sess_login_ok(&g_sess, &l);
}

static void test_sess_hello(void)
{
    proto_session *s;

    s = &g_sess;
    sess_init(s, NULL);
    TF_EQ(s->state, PROTO_S_NEW);
    TF_EQ(s->fixsta, 0);
    TF_STREQ(s->station, "");
    TF_STREQ(s->user, "");
    TF_EQ(s->level, 0);
    TF_EQ(s->fails, 0);
    TF_EQ(sess_level_ok(s, 0), 1);
    TF_EQ(sess_level_ok(s, ENG_LVL_READ), 0);

    /* Station checks leave the session unchanged. */
    TF_EQ(sess_hello(s, "", NULL), ENG_EARG);
    TF_EQ(sess_hello(s, "ABCDEFGHIJKLMNOPQ", NULL), ENG_EARG);
    TF_EQ(sess_hello(s, "PC\001", NULL), ENG_EARG);
    TF_EQ(sess_hello(s, "PC\177", NULL), ENG_EARG);
    TF_EQ(sess_hello(s, "STA1 ", "STA1"), ENG_EARG);
    TF_EQ(sess_hello(s, " STA1", "STA1"), ENG_EARG);
    TF_EQ(sess_hello(s, "   ", NULL), ENG_EARG);
    TF_EQ(sess_hello(s, " ", NULL), ENG_EARG);
    TF_EQ(s->state, PROTO_S_NEW);
    TF_EQ(sess_hello(s, "PC 2", NULL), ENG_OK);
    TF_STREQ(s->station, "PC 2");
    TF_EQ(sess_hello(s, "ABCDEFGHIJKLMNOP", NULL), ENG_OK);
    TF_STREQ(s->station, "ABCDEFGHIJKLMNOP");
    TF_EQ(sess_hello(s, "pc1", NULL), ENG_OK);
    TF_EQ(s->state, PROTO_S_HELLO);
    TF_STREQ(s->station, "pc1");
    TF_EQ(s->readonly, 0);

    login(ENG_LVL_ADMIN, 0);
    TF_EQ(s->state, PROTO_S_READY);
    TF_STREQ(s->user, "NORMAN");
    TF_EQ(s->userlevel, 3);
    TF_EQ(s->level, 3);
    TF_EQ(sess_level_ok(s, ENG_LVL_ADMIN), 1);

    /* A failed HELLO leaves the user logged in. */
    TF_EQ(sess_hello(s, "", NULL), ENG_EARG);
    TF_EQ(s->state, PROTO_S_READY);
    TF_EQ(s->level, 3);
}

static void test_sess_readonly(void)
{
    proto_session *s;

    s = &g_sess;
    /* HELLO logs out and sets the read-only cap. */
    TF_EQ(sess_hello(s, "Pc1", "STA1, pc1 ,X"), ENG_OK);
    TF_EQ(s->state, PROTO_S_HELLO);
    TF_STREQ(s->user, "");
    TF_EQ(s->level, 0);
    TF_EQ(s->userlevel, 0);
    TF_EQ(s->readonly, 1);
    login(ENG_LVL_ADMIN, 0);
    TF_EQ(s->userlevel, 3);
    TF_EQ(s->level, ENG_LVL_READ);
    TF_EQ(sess_level_ok(s, ENG_LVL_READ), 1);
    TF_EQ(sess_level_ok(s, ENG_LVL_OPER), 0);
    TF_EQ(sess_hello(s, "PC10", "STA1,PC1,X"), ENG_OK);
    TF_EQ(s->readonly, 0);
    TF_EQ(sess_hello(s, "PC", "STA1,PC1,X"), ENG_OK);
    TF_EQ(s->readonly, 0);
    TF_EQ(sess_hello(s, "X", "STA1,PC1,X"), ENG_OK);
    TF_EQ(s->readonly, 1);
    TF_EQ(sess_hello(s, "STA1", "sta1"), ENG_OK);
    TF_EQ(s->readonly, 1);
    TF_EQ(sess_hello(s, "STA1", ""), ENG_OK);
    TF_EQ(s->readonly, 0);
    TF_EQ(sess_hello(s, "STA1", ",,"), ENG_OK);
    TF_EQ(s->readonly, 0);
    TF_EQ(sess_hello(s, "pc1", NULL), ENG_OK);
    TF_EQ(s->readonly, 0);
}

static void test_sess_login(void)
{
    proto_session *s;

    s = &g_sess;
    /* MUSTCHG, then PASSWD. */
    login(ENG_LVL_OPER, 1);
    TF_EQ(s->state, PROTO_S_MUSTCHG);
    TF_EQ(s->level, ENG_LVL_OPER);
    sess_passwd_ok(s);
    TF_EQ(s->state, PROTO_S_READY);
    sess_passwd_ok(s);
    TF_EQ(s->state, PROTO_S_READY);

    /* A LOGIN replaces the user. */
    login(ENG_LVL_READ, 0);
    TF_EQ(s->level, ENG_LVL_READ);
    TF_EQ(s->userlevel, ENG_LVL_READ);

    /* Failures log out and count; the third closes. */
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(s->state, PROTO_S_HELLO);
    TF_STREQ(s->user, "");
    TF_EQ(s->level, 0);
    TF_EQ(s->fails, 1);
    sess_passwd_ok(s);
    TF_EQ(s->state, PROTO_S_HELLO);
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(s->fails, 2);
    /* HELLO does not reset the count. */
    TF_EQ(sess_hello(s, "PC2", NULL), ENG_OK);
    TF_EQ(s->fails, 2);
    TF_EQ(sess_login_failed(s), 1);
    TF_EQ(s->state, PROTO_S_CLOSED);
    TF_EQ(s->fails, PROTO_MAXFAIL);
    TF_EQ(sess_hello(s, "PC2", NULL), ENG_EINTERNAL);
    TF_EQ(s->state, PROTO_S_CLOSED);
}

static void test_sess_restart(void)
{
    proto_session *s;

    s = &g_sess;
    /* sess_init starts again (a serial line); a success resets. */
    sess_init(s, NULL);
    TF_EQ(s->fails, 0);
    TF_EQ(s->state, PROTO_S_NEW);
    TF_STREQ(s->station, "");
    TF_EQ(sess_hello(s, "PC3", NULL), ENG_OK);
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(sess_login_failed(s), 0);
    login(ENG_LVL_OPER, 0);
    TF_EQ(s->fails, 0);
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(s->state, PROTO_S_HELLO);
    login(ENG_LVL_OPER, 1);
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(sess_login_failed(s), 0);
    TF_EQ(sess_login_failed(s), 1);
    TF_EQ(s->state, PROTO_S_CLOSED);

    /* BYE closes and logs out. */
    sess_init(s, NULL);
    TF_EQ(sess_hello(s, "PC4", NULL), ENG_OK);
    login(ENG_LVL_ADMIN, 0);
    sess_bye(s);
    TF_EQ(s->state, PROTO_S_CLOSED);
    TF_EQ(s->level, 0);
    TF_STREQ(s->user, "");
    sess_init(s, NULL);
    sess_bye(s);
    TF_EQ(s->state, PROTO_S_CLOSED);
}

static void test_sess_fixed(void)
{
    proto_session *s;

    s = &g_sess;
    /* A station fixed by the transport. */
    sess_init(s, "MAILBOX-STATION-NAME");
    TF_EQ(s->fixsta, 1);
    TF_STREQ(s->station, "MAILBOX-STATION-");
    TF_EQ(s->state, PROTO_S_NEW);
    /* Transport stations are trimmed; an empty one is not fixed. */
    sess_init(s, "  NB-PC1        ");
    TF_EQ(s->fixsta, 1);
    TF_STREQ(s->station, "NB-PC1");
    TF_EQ(sess_hello(s, "OTHER", "nb-pc1"), ENG_OK);
    TF_STREQ(s->station, "NB-PC1");
    TF_EQ(s->readonly, 1);
    sess_init(s, "ABCDEFGHIJKLMNO Q");
    TF_STREQ(s->station, "ABCDEFGHIJKLMNO");
    sess_init(s, "               ");
    TF_EQ(s->fixsta, 0);
    TF_STREQ(s->station, "");
    TF_EQ(sess_hello(s, "PC5", NULL), ENG_OK);
    TF_STREQ(s->station, "PC5");
    sess_init(s, "");
    TF_EQ(s->fixsta, 0);
    sess_init(s, "MBOX1");
    TF_EQ(sess_hello(s, "", NULL), ENG_EARG);
    TF_EQ(s->state, PROTO_S_NEW);
    TF_EQ(sess_hello(s, "OTHER", "OTHER"), ENG_OK);
    TF_EQ(s->state, PROTO_S_HELLO);
    TF_STREQ(s->station, "MBOX1");
    TF_EQ(s->readonly, 0);
    TF_EQ(sess_hello(s, "OTHER", "mbox1"), ENG_OK);
    TF_EQ(s->readonly, 1);
    login(ENG_LVL_OPER, 0);
    TF_EQ(s->level, ENG_LVL_READ);
}

/* proto_in_list: whole names of a comma list, compared without regard
   to the case of ASCII letters; spaces around each name are ignored,
   other bytes (tabs, inner spaces, those of word) count. */
static void test_in_list(void)
{
    /* Only, first, middle and last name. */
    TF_EQ(proto_in_list("STA1", "STA1"), 1);
    TF_EQ(proto_in_list("STA1,STA2,STA3", "STA1"), 1);
    TF_EQ(proto_in_list("STA1,STA2,STA3", "STA2"), 1);
    TF_EQ(proto_in_list("STA1,STA2,STA3", "STA3"), 1);
    TF_EQ(proto_in_list("STA1,STA2,STA3", "STA4"), 0);
    /* Case, ASCII letters only. */
    TF_EQ(proto_in_list("STA1,PC2", "sta1"), 1);
    TF_EQ(proto_in_list("sTa1,PC2", "StA1"), 1);
    TF_EQ(proto_in_list("STA1,pc2", "PC2"), 1);
    TF_EQ(proto_in_list("\xC9TA", "\xE9TA"), 0);
    TF_EQ(proto_in_list("\xE9TA", "\xE9TA"), 1);
    /* Whole names: a prefix either way is no match. */
    TF_EQ(proto_in_list("STA10", "STA1"), 0);
    TF_EQ(proto_in_list("STA1", "STA10"), 0);
    TF_EQ(proto_in_list("STA10,STA1", "STA1"), 1);
    TF_EQ(proto_in_list("STA,STA12", "STA1"), 0);
    TF_EQ(proto_in_list("A", "AB"), 0);
    TF_EQ(proto_in_list("AB", "A"), 0);
    /* Spaces around a name are ignored, inner ones are part of it. */
    TF_EQ(proto_in_list(" STA1 , STA2 ,STA3  ", "STA1"), 1);
    TF_EQ(proto_in_list(" STA1 , STA2 ,STA3  ", "STA2"), 1);
    TF_EQ(proto_in_list(" STA1 , STA2 ,STA3  ", "STA3"), 1);
    TF_EQ(proto_in_list("   STA1", "STA1"), 1);
    TF_EQ(proto_in_list("STA1   ", "STA1"), 1);
    TF_EQ(proto_in_list("ST A1", "STA1"), 0);
    TF_EQ(proto_in_list("ST A1", "ST"), 0);
    TF_EQ(proto_in_list("ST A1", "ST A1"), 1);
    TF_EQ(proto_in_list(" ST A1 ,B", "st a1"), 1);
    /* Only the space is trimmed, and only from the list's names. */
    TF_EQ(proto_in_list("\tSTA1", "STA1"), 0);
    TF_EQ(proto_in_list("STA1\t", "STA1"), 0);
    TF_EQ(proto_in_list("STA1", " STA1"), 0);
    TF_EQ(proto_in_list("STA1", "STA1 "), 0);
    TF_EQ(proto_in_list(" STA1 ", "STA1 "), 0);
    /* The comma is the only separator. */
    TF_EQ(proto_in_list("STA1;STA2", "STA2"), 0);
    TF_EQ(proto_in_list("STA1;STA2", "STA1;STA2"), 1);
    TF_EQ(proto_in_list("STA1 STA2", "STA2"), 0);
    /* Empty names are skipped and never match. */
    TF_EQ(proto_in_list("", "STA1"), 0);
    TF_EQ(proto_in_list("", ""), 0);
    TF_EQ(proto_in_list(",", ""), 0);
    TF_EQ(proto_in_list("  ", ""), 0);
    TF_EQ(proto_in_list(" , ,", ""), 0);
    TF_EQ(proto_in_list("STA1,,STA2", ""), 0);
    TF_EQ(proto_in_list("STA1", ""), 0);
    TF_EQ(proto_in_list(",STA1", "STA1"), 1);
    TF_EQ(proto_in_list("STA1,", "STA1"), 1);
    TF_EQ(proto_in_list("STA1,,STA2", "STA2"), 1);
    TF_EQ(proto_in_list(" , ,STA2, ", "STA2"), 1);
    /* The longest station, ENG_STALEN characters. */
    TF_EQ(proto_in_list("PC1,ABCDEFGHIJKLMNOP", "abcdefghijklmnop"), 1);
    TF_EQ(proto_in_list("PC1,ABCDEFGHIJKLMNOP", "ABCDEFGHIJKLMNO"), 0);
}

static void test_session(void)
{
    test_sess_hello();
    test_sess_readonly();
    test_sess_login();
    test_sess_restart();
    test_sess_fixed();
}

int main(void)
{
    tf_begin("T_TOKEN");
    test_acc();
    test_parse_basic();
    test_parse_quotes();
    test_parse_limits();
    test_out_simple();
    test_out_data();
    test_out_cut();
    test_out_errors();
    test_in_list();
    test_session();
    return tf_done();
}

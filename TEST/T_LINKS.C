/*
 * T_LINKS.C - unit test for the SERIAL backend of the client's link
 * (LINKSER.C over SERCLI.C), INV's serial switches (INVCOM.C) and
 * COMTEST's loopback check (PORTCHK.C) on the simulated ports
 * (FAKEUART.C).
 *
 * usage: T_LINKS dir
 *
 * dir is a scratch directory prefix ("OBJ/HOST/LINKST/", "D:") for the
 * report file of the loopback check, removed first and last.
 *
 * A scripted server, run from the ports' pump, answers each request
 * line the link sends on COM1 by its command word (see answer).  The
 * judging of a session's end is LINK.C's, shared with NET and tested
 * in full by T_LINKN; this test checks what is the serial backend's:
 * - open: a missing port (LINK_ENOPORT), a port, baud rate or flag
 *   uart_open refuses (LINK_ESETUP), a port open already (LINK_EBUSY),
 *   no memory at any allocation (LINK_ENOMEM), each with *out NULL, the
 *   port as it was and nothing held; link_netname NULL; nothing sent;
 * - requests and responses through link_send and link_feed, a line
 *   over LINK_MAXLINE answered with 103 without being sent, a failing
 *   line callback (LINK_EOUTPUT, the session going on), no byte of the
 *   link holding the password of a LOGIN once answered, nor any far
 *   block it frees at the close;
 * - a session that ends (+OK to BYE, the LINK_MAXFAIL-th failed LOGIN,
 *   -ERR 501) closes the port at once without a BYE, and nothing more
 *   is sent; link_close on a live session sends BYE, reads its response
 *   and closes the port;
 * - a response that does not come within LINK_NETWAIT seconds, on the
 *   host's virtual clock: LINK_ELOST, the port closed after a last
 *   CR LF and BYE that no response is waited for, nothing more sent;
 * - invcom_open's results, as link_open_serial's, each failure's reason
 *   on standard error; pchk_run's results and reports, each line
 *   starting "COMTEST ", the last its verdict: "COMTEST PASS" for a
 *   loopback with every byte back (also under RTS/CTS, which it names,
 *   and on a line of a byte a step, slower than TMR_SEC in all),
 *   "COMTEST FAIL" and a reason (lower-case words, after an optional
 *   device name in capitals and a colon) after the report of one that
 *   lost a byte and of one that got every byte back but one changed,
 *   and alone, its reason ending "COM2: no such port" or "COM1: no
 *   memory", for a port missing or out of memory; each port closed
 *   again and nothing held.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_UART
#define COMPAT_FIND
#define LINK_SERIAL
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "FAKEUART.H"
#include "LINK.H"
#include "CFG.H"
#include "INVCOM.H"
#include "PORTCHK.H"
#include "T_FILES.H"

#define PORT            1
#define PW              "SECRET1"       /* the password requests carry  */
#define RXMAX           1024    /* bytes the server keeps               */
#define OUTMAX          512     /* response text a test keeps           */
#define PATHMAX         64      /* bytes of a directory argument        */
#define BADAT           200     /* the pattern byte corrupt changes     */
#define NBLK            2       /* far blocks a serial link holds       */

static char g_rx[RXMAX];        /* what the server received             */
static u16 g_nrx;
static u16 g_done;              /* bytes of g_rx answered               */
static char g_out[OUTMAX];      /* the lines delivered, each ended '|'  */
static u16 g_fail;              /* the line callback fails from this
                                   line on, 0 never                     */
static u16 g_lines;
static u16 g_fnth;              /* the cmem_alloc call that fails       */
static u16 g_fcount;
static u16 g_nfreed;            /* far blocks no_pw has read            */
#ifdef COMPAT_HOST
static u16 g_held;
#endif

/* The server's answer to the request line s: by its first word, HELLO
   and BYE "+OK", LOGIN "+OK" for the password PW and 401 otherwise,
   LIST a "+DATA" response, STOP 501, SILENT none, anything else "+OK"
   and the line. */
static void answer(const char *s)
{
    char buf[64];

    if (strncmp(s, "LOGIN ", 6) == 0)
        strcpy(buf, strstr(s, PW) != NULL ? "+OK LEVEL=3\r\n" :
                                            "-ERR 401 bad password\r\n");
    else if (strcmp(s, "LIST") == 0)
        strcpy(buf, "+DATA\r\nK 1\r\nK 2\r\n.\r\n");
    else if (strcmp(s, "STOP") == 0)
        strcpy(buf, "-ERR 501 shutting down\r\n");
    else if (strcmp(s, "SILENT") == 0)
        return;
    else if (strncmp(s, "HELLO", 5) == 0 || strcmp(s, "BYE") == 0)
        strcpy(buf, "+OK\r\n");
    else
        sprintf(buf, "+OK %.40s\r\n", s);
    fu_peer_send(PORT, buf, (u16)strlen(buf));
}

/* Take what the link sent and answer every whole line. */
static void srv_run(void)
{
    char *e;

    g_nrx = (u16)(g_nrx + fu_peer_recv(PORT, g_rx + g_nrx,
                                       (u16)(RXMAX - 1 - g_nrx)));
    g_rx[g_nrx] = '\0';
    while ((e = strstr(g_rx + g_done, "\r\n")) != NULL) {
        *e = '\0';
        answer(g_rx + g_done);
        *e = '\r';
        g_done = (u16)(e + 2 - g_rx);
    }
}

static void pump(void *arg)
{
    TF_CHECK(arg == NULL);
    srv_run();
}

static int collect(void *arg, const char *line, u16 len)
{
    size_t n;

    n = strlen(g_out);
    TF_CHECK(arg == NULL && line[len] == '\0');
    if (n + len + 2 <= OUTMAX) {
        memcpy(g_out + n, line, len);
        strcpy(g_out + n + len, "|");
    }
    return ++g_lines >= g_fail && g_fail != 0;
}

/* Send s over l: the result; the lines delivered are in g_out. */
static int req(link *l, const char *s)
{
    g_out[0] = '\0';
    g_lines = 0;
    return link_send(l, s, (u16)strlen(s), collect, NULL);
}

/* Forget what the server received. */
static void clear(void)
{
    g_nrx = 0;
    g_done = 0;
    g_rx[0] = '\0';
}

static void fresh(void)
{
    fu_reset();
    fu_port(PORT, UART_T_16450);
    fu_pump(pump, NULL);
    clear();
    g_fail = 0;
#ifdef COMPAT_HOST
    g_held = tm_held();
#endif
}

static link *open_link(void)
{
    link *l;

    l = NULL;
    TF_EQ(link_open_serial(&l, PORT, UART_DEFBAUD, 0), 0);
    TF_CHECK(l != NULL);
    return l;
}

/* The link is closed: the port too, and the heap as at fresh. */
static void finish(void)
{
    fu_pump(NULL, NULL);
    TF_EQ(fu_isrstate(PORT), 0);
#ifdef COMPAT_HOST
    TM_HELD(g_held);
#endif
    fu_reset();
}

static int fail_nth(u16 size)
{
    TF_CHECK(size > 0);
    return ++g_fcount == g_fnth;
}

/* cmem_freeing: count the block b and check that no run of its bytes
   is PW. */
static void no_pw(cmem_blk *b)
{
    char w[sizeof PW - 1];
    u16 off;
    int seen;

    g_nfreed++;
    seen = 0;
    for (off = 0; off + sizeof w <= cmem_size(b); off++) {
        cmem_get(b, off, w, sizeof w);
        seen |= memcmp(w, PW, sizeof w) == 0;
    }
    TF_CHECK(!seen);
}

/* link_open_serial(port, baud, flags) refused with want, nothing held
   and COM1 as it was. */
static void refused(u8 port, u16 baud, u8 flags, int want)
{
    link *l;
#ifdef COMPAT_HOST
    u16 h;
#endif
    u8 st;

#ifdef COMPAT_HOST
    h = tm_held();
#endif
    st = fu_isrstate(PORT);
    TF_EQ(link_open_serial(&l, port, baud, flags), want);
    TF_CHECK(l == NULL);
    TF_EQ(fu_isrstate(PORT), st);
#ifdef COMPAT_HOST
    TM_HELD(h);
#endif
}

static void test_open(void)
{
    link *l;
    int rc;
    u16 n;

    fresh();
    refused(2, UART_DEFBAUD, 0, LINK_ENOPORT);
    refused(PORT, 9601, 0, LINK_ESETUP);
    refused(PORT, UART_DEFBAUD, UART_F_LOOP, LINK_ESETUP);
    refused(0, UART_DEFBAUD, 0, LINK_ESETUP);
    refused(PORT, UART_DEFBAUD, UART_F_NOFIFO, LINK_ESETUP);
    TF_EQ(uart_open(PORT, UART_DEFBAUD, 0), UART_OK);
    refused(PORT, UART_DEFBAUD, 0, LINK_EBUSY);
    uart_close(PORT);
    cmem_fault = fail_nth;
    for (n = 1; n <= NBLK; n++) {
        g_fnth = n;
        g_fcount = 0;
        refused(PORT, UART_DEFBAUD, 0, LINK_ENOMEM);
    }
    cmem_fault = NULL;
#ifdef COMPAT_HOST
    for (n = 1;; n++) {
        tm_fail(n);
        rc = link_open_serial(&l, PORT, UART_DEFBAUD, 0);
        tm_fail(0);
        if (rc == 0)
            break;
        TF_EQ(rc, LINK_ENOMEM);
        TF_CHECK(l == NULL);
        TF_EQ(fu_isrstate(PORT), 0);
        TM_HELD(g_held);
    }
    TF_CHECK(n > 2);
    link_close(l);
    clear();
#endif
    rc = link_open_serial(&l, PORT, UART_DEFBAUD, UART_F_RTSCTS);
    TF_EQ(rc, 0);
    TF_CHECK(link_netname(l) == NULL);
    TF_EQ(g_nrx, 0);
    TF_EQ(link_close(l), 0);
    TF_STREQ(g_rx, "BYE\r\n");
    finish();
}

static void test_requests(void)
{
    char big[LINK_MAXLINE + 2];
    const char *b;
    link *l;
    u16 size;
    u16 i;

    fresh();
    l = open_link();
    TF_EQ(req(l, "HELLO PC"), LINK_OK);
    TF_STREQ(g_out, "+OK|");
    TF_EQ(req(l, "LOGIN ADMIN " PW), LINK_OK);
    TF_STREQ(g_out, "+OK LEVEL=3|");
    b = (const char *)link_bytes(l, &size);
    for (i = 0; i + sizeof PW - 1 <= size; i++)
        TF_CHECK(memcmp(b + i, PW, sizeof PW - 1) != 0);
    TF_EQ(req(l, "LIST"), LINK_OK);
    TF_STREQ(g_out, "+DATA|K 1|K 2|.|");
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    i = g_nrx;
    TF_EQ(req(l, big), LINK_OK);
    TF_CHECK(strncmp(g_out, "-ERR 103 ", 9) == 0);
    TF_EQ(g_nrx, i);
    g_fail = 2;
    TF_EQ(req(l, "LIST"), LINK_EOUTPUT);
    TF_STREQ(g_out, "+DATA|K 1|");
    g_fail = 0;
    TF_EQ(req(l, "PING"), LINK_OK);
    TF_STREQ(g_out, "+OK PING|");
    g_out[0] = '\0';
    TF_EQ(link_feed(l, "A\r\nB\nC", 6, collect, NULL), LINK_OK);
    TF_STREQ(g_out, "+OK A|+OK B|");
    TF_EQ(link_feed(l, "\n", 1, collect, NULL), LINK_OK);
    TF_STREQ(g_out, "+OK A|+OK B|+OK C|");
    i = g_nrx;
    g_nfreed = 0;
    cmem_freeing = no_pw;
    TF_EQ(link_close(l), 0);
    cmem_freeing = NULL;
    TF_EQ(g_nfreed, NBLK);
    TF_STREQ(g_rx + i, "BYE\r\n");
    finish();
}

/* The session ended by the response to last: the port closes at once,
   no BYE is sent, and nothing more. */
static void ended(const char *const *reqs, const char *last)
{
    link *l;
    u16 n;

    fresh();
    l = open_link();
    for (; *reqs != NULL; reqs++)
        TF_EQ(req(l, *reqs), LINK_OK);
    n = g_nrx;
    TF_EQ(req(l, last), LINK_CLOSED);
    TF_EQ(fu_isrstate(PORT), 0);
    srv_run();
    TF_EQ(g_nrx, n + strlen(last) + 2);
    n = g_nrx;
    TF_EQ(req(l, "PING"), LINK_CLOSED);
    TF_EQ(link_close(l), 0);
    srv_run();
    TF_EQ(g_nrx, n);
    finish();
}

static void test_ends(void)
{
    static const char *const none[] = { NULL };
    static const char *const fails[] = {
        "HELLO PC", "LOGIN ADMIN X", "LOGIN ADMIN Y", NULL
    };

    ended(none, "BYE");
    ended(none, "STOP");
    ended(fails, "LOGIN ADMIN Z");

#ifdef COMPAT_HOST
    {
        link *l;

        /* No response: lost, the port closed with a last BYE, not
           waited for, and nothing more sent. */
        fresh();
        l = open_link();
        TF_EQ(req(l, "SILENT"), LINK_ELOST);
        TF_EQ(fu_isrstate(PORT), 0);
        TF_EQ(req(l, "PING"), LINK_ELOST);
        TF_EQ(link_close(l), 0);
        TF_STREQ(g_rx, "SILENT\r\n\r\nBYE\r\n");
        finish();
    }
#endif
}

/* The report of a loopback on COM1, a 16450, at 9600 baud: its first
   two lines, and the whole of one with every byte back. */
#define REP_PORT        "COMTEST COM1: 16450\nCOMTEST I/O base 3F8h, IRQ 4\n"
#define REP_OK          REP_PORT \
                        "COMTEST 9600 baud: 256 bytes back in order\n" \
                        "COMTEST PASS\n"
#define REP_FAIL        "COMTEST FAIL "

/* Where standard error goes once its file has been written. */
#ifdef COMPAT_HOST
#define NULDEV          "/dev/null"
#else
#define NULDEV          "NUL"
#endif

/* Read the scratch file name into got, of OUTMAX bytes, as a C string;
   returns 0 when it cannot be opened. */
static int file_get(const char *name, char *got)
{
    FILE *f;
    u16 n;

    f = fopen(tfs_path(name, ""), "r");
    if (!TF_CHECK(f != NULL))
        return 0;
    n = (u16)fread(got, 1, OUTMAX - 1, f);
    got[n] = '\0';
    fclose(f);
    return 1;
}

/* Check that the scratch file name holds want and nothing more. */
static void file_is(const char *name, const char *want)
{
    char got[OUTMAX];

    if (file_get(name, got))
        TF_STREQ(got, want);
}

/* 1 when the reason r of a FAIL verdict is lower-case words, after an
   optional device name in capitals and digits and ": ". */
static int reason_ok(const char *r)
{
    const char *s;

    for (s = r; *s >= 'A' && *s <= 'Z'; s++)
        ;
    if (s != r) {
        while (TXT_ISDIGIT(*s))
            s++;
        if (s[0] == ':' && s[1] == ' ')
            r = s + 2;
    }
    return *r >= 'a' && *r <= 'z';
}

/* Check that the scratch file name holds n failed reports and nothing
   more: for each, the text want[2i], then a line REP_FAIL and a reason
   that reason_ok passes and that ends with want[2i + 1]. */
static void fails_are(const char *name, const char *const *want, int n)
{
    char got[OUTMAX];
    const char *p;
    const char *eol;
    size_t head;
    size_t tail;
    int i;

    if (!file_get(name, got))
        return;
    p = got;
    for (i = 0; i < n; i++) {
        head = strlen(want[2 * i]);
        tail = strlen(want[2 * i + 1]);
        if (!TF_CHECK(strncmp(p, want[2 * i], head) == 0))
            return;
        p += head;
        eol = strchr(p, '\n');
        if (!TF_CHECK(eol != NULL &&
                      strncmp(p, REP_FAIL, sizeof REP_FAIL - 1) == 0 &&
                      reason_ok(p + sizeof REP_FAIL - 1) &&
                      (size_t)(eol - p) >= tail &&
                      strncmp(eol - tail, want[2 * i + 1], tail) == 0))
            return;
        p = eol + 1;
    }
    TF_STREQ(p, "");
}

/* fu_pump_fn: once byte BADAT of the pattern has come back into COM1's
   receive ring, which a fresh open fills from its start, change it
   there, so that every byte comes back but one is wrong. */
static void corrupt(void *arg)
{
    cmem_blk *b;
    char c;

    b = fu_ringblk(PORT);
    if (arg != NULL || b == NULL)
        return;
    cmem_get(b, BADAT, &c, 1);
    if ((u8)c == BADAT) {
        c = 0x55;
        cmem_put(b, BADAT, &c, 1);
    }
}

/* invcom_open's results, each failure's reason on standard error,
   which the test keeps in ERR.TXT, and pchk_run's reports, those of a
   pass in COMTEST.OUT and those of a failure in COMFAIL.OUT. */
static void test_invcom(void)
{
    static const char *const files[] = {
        "COMTEST.OUT", "COMFAIL.OUT", "ERR.TXT", NULL
    };
    static const char *const fails[] = {
        REP_PORT "COMTEST 9600 baud: 0 bytes back in order\n", "",
        REP_PORT "COMTEST 9600 baud: 200 bytes back in order\n", "",
        "", "COM2: no such port",
        "", "COM1: no memory"
    };
    static const char k_invfail[] = "INV: cannot open COM2: no such port\n"
                                    "INV: cannot open COM1: port in use\n"
                                    "INV: cannot open COM1: no memory\n";
    FILE *f;
    link *l;
    cfg c;

    memset(&c, 0, sizeof c);
    c.baud = UART_DEFBAUD;
    fresh();
    tfs_wipe(files);
    if (!TF_CHECK(freopen(tfs_path(files[2], ""), "w", stderr) != NULL))
        return;
    TF_EQ(invcom_open(&l, 2, &c), LINK_ENOPORT);
    TF_EQ(uart_open(PORT, UART_DEFBAUD, 0), UART_OK);
    TF_EQ(invcom_open(&l, PORT, &c), LINK_EBUSY);
    uart_close(PORT);
    cmem_fault = fail_nth;
    g_fnth = 1;
    g_fcount = 0;
    TF_EQ(invcom_open(&l, PORT, &c), LINK_ENOMEM);
    cmem_fault = NULL;
    c.rtscts = 1;
    TF_EQ(invcom_open(&l, PORT, &c), 0);
    TF_EQ(fu_isrstate(PORT) & FU_S_OPEN, FU_S_OPEN);
    TF_EQ(link_close(l), 0);

    f = fopen(tfs_path(files[0], ""), "w");
    if (!TF_CHECK(f != NULL))
        return;
    TF_EQ(pchk_run(PORT, UART_DEFBAUD, UART_F_RTSCTS, f), PCHK_OK);
    TF_EQ(pchk_run(PORT, UART_DEFBAUD, 0, f), PCHK_OK);
    /* A byte a step: the check outlasts TMR_SEC ticks, each byte that
       moves starting its wait again. */
    fu_rate(PORT, 1);
    TF_EQ(pchk_run(PORT, UART_DEFBAUD, 0, f), PCHK_OK);
    fu_rate(PORT, 0);
    TF_EQ(fclose(f), 0);
    f = fopen(tfs_path(files[1], ""), "w");
    if (!TF_CHECK(f != NULL))
        return;
    fu_inject(PORT, FU_LOST, 1);
    TF_EQ(pchk_run(PORT, UART_DEFBAUD, 0, f), PCHK_EFAIL);
    fu_pump(corrupt, NULL);
    TF_EQ(pchk_run(PORT, UART_DEFBAUD, 0, f), PCHK_EFAIL);
    fu_pump(pump, NULL);
    TF_EQ(pchk_run(2, UART_DEFBAUD, 0, f), PCHK_EOPEN);
    cmem_fault = fail_nth;
    g_fnth = 1;
    g_fcount = 0;
    TF_EQ(pchk_run(PORT, UART_DEFBAUD, 0, f), PCHK_EOPEN);
    cmem_fault = NULL;
    TF_EQ(fu_isrstate(PORT), 0);
    TF_EQ(fclose(f), 0);
    /* Closed, so that DOS writes the file's size. */
    TF_CHECK(freopen(NULDEV, "w", stderr) != NULL);
    file_is(files[0], REP_PORT
                      "COMTEST 9600 baud, RTS/CTS: 256 bytes back in order\n"
                      "COMTEST PASS\n" REP_OK REP_OK);
    fails_are(files[1], fails, 4);
    file_is(files[2], k_invfail);
    tfs_wipe(files);
    finish();
}

int main(int argc, char **argv)
{
    cmem_init();
    tf_begin("T_LINKS");
    if (argc != 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_LINKS dir\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    test_open();
    test_requests();
    test_ends();
    test_invcom();
    return tf_done();
}

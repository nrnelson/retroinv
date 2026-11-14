/*
 * TCPTEST.C - TCP probe on the bridge's sockets (TSOCK.H): the TCP stack
 * and the packet driver tested without NetBIOS.
 *
 *   TCPTEST [/PORT:n]
 *
 * Every one of the TS_SLOTS slots listens on port n (1..65535, BR_PORT
 * without /PORT:) and echoes each line its client sends, LF included,
 * once the line is complete; a line longer than TT_LINE bytes is echoed
 * in pieces of TT_LINE bytes.  A connection that finds every slot taken
 * is refused by the stack.  A slot whose connection has ended listens
 * again.  The line QUIT (ended by LF or CR LF) from any client is not
 * echoed: every connection is closed, the program waits up to
 * TT_CLOSEWAIT ticks for them to end, then aborts what is left, releases
 * the packet driver and ends.
 *
 * The switch matches in any case and may begin with '-' for '/'.  /?
 * alone writes the usage text to standard output and exits 0; any other
 * bad command line writes it to standard error and nothing to standard
 * output.  Otherwise each event is one line on standard output,
 * the last the verdict:
 *
 *   TCPTEST LISTEN <port>
 *   TCPTEST <slot> OPEN <a.b.c.d>:<port>
 *   TCPTEST <slot> CLOSE
 *   TCPTEST QUIT
 *   TCPTEST PASS
 *   TCPTEST FAIL no packet driver | stack does not start | stack does
 *                not listen
 *
 * Exit status: 0 after QUIT, 1 for a usage error, 2 when there is no
 * packet driver, 3 for a stack that does not start or listen, 4 under
 * DOS older than 3.1.
 */
#define COMPAT_HEAP
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"

#define TT_LINE         128     /* bytes of a slot's line buffer        */
#define TT_CLOSEWAIT    (5 * TMR_SEC) /* the wait for QUIT's closes     */

#define TT_EXIT_OK      0
#define TT_EXIT_USAGE   1
#define TT_EXIT_NOPKT   2
#define TT_EXIT_FAIL    3
#define TT_EXIT_DOSVER  4

typedef struct {
    u16 have;                   /* bytes in buf                         */
    u16 sent;                   /* of its first line, bytes echoed      */
    u8 open;                    /* 1 once OPEN is reported              */
    char buf[TT_LINE];
} tt_slot;

static tt_slot tt_s[TS_SLOTS];
static u16 tt_port;
static u8 tt_quit;              /* 1 once a client sent QUIT            */

static const char k_quit[] = "QUIT";
static const char k_usage[] = "usage: TCPTEST [/PORT:n]\n";

/* The length of the first line in t's buffer, its LF included: the
   whole buffer when it is full without one, 0 when no line is
   complete. */
static u16 line_len(const tt_slot *t)
{
    u16 i;

    for (i = 0; i < t->have; i++)
        if (t->buf[i] == '\n')
            return (u16)(i + 1);
    return t->have == TT_LINE ? (u16)TT_LINE : 0;
}

/* 1 when the n bytes at s are the line QUIT. */
static int is_quit(const char *s, u16 n)
{
    if (n > 1 && s[n - 2] == '\r')
        n--;
    return n == sizeof k_quit && memcmp(s, k_quit, sizeof k_quit - 1) == 0;
}

/* Read and echo for slot k, TS_OPEN; returns 1 when bytes moved. */
static int serve(u8 k)
{
    tt_slot *t;
    u16 n;
    u16 w;

    t = &tt_s[k];
    n = line_len(t);
    if (n == 0) {
        w = ts_read(k, t->buf + t->have, (u16)(TT_LINE - t->have));
        t->have = (u16)(t->have + w);
        n = line_len(t);
        if (n == 0)
            return w != 0;
    }
    if (t->sent == 0 && is_quit(t->buf, n)) {
        tt_quit = 1;
        return 1;
    }
    w = ts_write(k, t->buf + t->sent, (u16)(n - t->sent));
    t->sent = (u16)(t->sent + w);
    if (t->sent == n) {
        t->have = (u16)(t->have - n);
        memmove(t->buf, t->buf + n, t->have);
        t->sent = 0;
    }
    return w != 0;
}

/* Report slot k's connection. */
static void opened(u8 k)
{
    u8 ip[TS_IPLEN];
    u16 port;

    ts_peer(k, ip, &port);
    printf("TCPTEST %u OPEN %u.%u.%u.%u:%u\n", (unsigned)k, (unsigned)ip[0],
           (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3], (unsigned)port);
}

/* Run slot k for a pass; returns 1 when it did something. */
static int step(u8 k)
{
    tt_slot *t;

    t = &tt_s[k];
    switch (ts_state(k)) {
    case TS_OPEN:
        if (!t->open) {
            t->open = 1;
            opened(k);
        }
        return serve(k);
    case TS_CLOSED:
        if (t->open)
            printf("TCPTEST %u CLOSE\n", (unsigned)k);
        memset(t, 0, sizeof *t);
        ts_listen(k, tt_port);
        return 1;
    default:
        return 0;
    }
}

/* Close every connection and wait up to TT_CLOSEWAIT for them to end. */
static void close_all(void)
{
    u32 start;
    u8 st;
    u8 k;
    int busy;

    for (k = 0; k < TS_SLOTS; k++)
        ts_close(k);
    start = tmr_ticks();
    do {
        ts_poll();
        busy = 0;
        for (k = 0; k < TS_SLOTS; k++) {
            st = ts_state(k);
            busy |= st == TS_OPEN || st == TS_CLOSING;
        }
    } while (busy && tmr_since(start) < TT_CLOSEWAIT);
}

int main(int argc, char **argv)
{
    const char *a;
    const char *why;
    int rc;
    int busy;
    int i;
    u8 k;
    u8 major;
    u8 minor;

    dos_heap_top();
    if (!dos_version(&major, &minor)) {
        fprintf(stderr, "TCPTEST: requires MS-DOS 3.1 or later\n");
        return TT_EXIT_DOSVER;
    }
    a = argv[1];
    if (argc == 2 && (a[0] == '/' || a[0] == '-') && txt_ieq(a + 1, "?")) {
        printf(k_usage);
        return TT_EXIT_OK;
    }
    for (i = 1; i < argc; i++) {
        a = argv[i];
        if ((a[0] != '/' && a[0] != '-') || !txt_ieqn(a + 1, "PORT:", 5) ||
            tt_port != 0 || (tt_port = txt_num(a + 6, 0xFFFF)) == 0) {
            fprintf(stderr, k_usage);
            return TT_EXIT_USAGE;
        }
    }
    if (tt_port == 0)
        tt_port = BR_PORT;
    /* Each event line reaches a redirected file as it happens. */
    setvbuf(stdout, NULL, _IONBF, 0);
    rc = ts_init();
    why = rc == TS_OK ? "stack does not listen" :
          rc == TS_ENOPKT ? "no packet driver" : "stack does not start";
    for (k = 0; rc == TS_OK && k < TS_SLOTS; k++)
        rc = ts_listen(k, tt_port);
    if (rc != TS_OK) {
        printf("TCPTEST FAIL %s\n", why);
        return rc == TS_ENOPKT ? TT_EXIT_NOPKT : TT_EXIT_FAIL;
    }
    printf("TCPTEST LISTEN %u\n", (unsigned)tt_port);
    while (!tt_quit) {
        ts_poll();
        busy = 0;
        for (k = 0; k < TS_SLOTS && !tt_quit; k++)
            busy |= step(k);
        if (!busy)
            dos_idle();
    }
    close_all();
    ts_term();
    printf("TCPTEST QUIT\nTCPTEST PASS\n");
    return TT_EXIT_OK;
}

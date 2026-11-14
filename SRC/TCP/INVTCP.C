/*
 * INVTCP.C - INVTCP.EXE, the TCP bridge program (INVTCP.H), apart from
 * its main (TCPMAIN.C).
 */
#define COMPAT_NETBIOS
#define COMPAT_FIND
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "TSOCK.H"
#include "BRIDGE.H"
#include "INVTCP.H"

#define TC_WORD         6       /* characters of the longest event word */

static const char k_prog[] = "INVTCP: ";

/* The words of the BR_EV_ events, in their order: on_event's arg. */
static char ev_words[] = "OPEN CALL NOCALL CLOSE REFUSE";

/* The loop's tick count. */
static tmr_clock tc_clock;

static void usage(FILE *f)
{
    fputs("usage: INVTCP [/NAME:local] [/ALLOW:net]... server\n", f);
    fputs("  /NAME:  the bridge's NetBIOS name (default the machine "
          "name)\n", f);
    fprintf(f, "  /ALLOW: let in only a.b.c.d, or the network a.b.c.d/bits "
            "(%u at most)\n", (unsigned)BR_ALLOWMAX);
}

/* Read the decimal number at *s, 1 to 3 digits without a leading zero
   (which other tools read as octal), into *v and move *s past it; 1
   when there is one and it is at most max. */
static int num(const char **s, u16 max, u16 *v)
{
    const char *p;
    u16 n;
    char ch;

    p = *s;
    n = 0;
    ch = *p;
    while (TXT_ISDIGIT(ch) && p - *s < 3) {
        n = (u16)(n * 10 + (ch - '0'));
        ch = *++p;
    }
    if (p == *s || n > max || (**s == '0' && p - *s > 1))
        return 0;
    *s = p;
    *v = n;
    return 1;
}

/* The network s, "a.b.c.d/bits" (bits 1..32) or "a.b.c.d" (the address
   alone), into *n; 1 when s is one. */
static int net(const char *s, br_net *n)
{
    u16 v;
    u16 bits;
    int i;

    n->addr = 0;
    for (i = 0; i < TS_IPLEN; i++) {
        if ((i > 0 && *s++ != '.') || !num(&s, 255, &v))
            return 0;
        n->addr = (n->addr << 8) | v;
    }
    bits = 8 * TS_IPLEN;
    if (*s == '/') {
        s++;
        if (!num(&s, bits, &bits) || bits == 0)
            return 0;
    }
    if (*s != '\0')
        return 0;
    n->mask = (u32)0xFFFFFFFFUL << (8 * TS_IPLEN - bits);
    return 1;
}

/* Say text on standard error after the program's name; returns st. */
static int fail(int st, const char *text)
{
    fprintf(stderr, "%s%s\n", k_prog, text);
    return st;
}

/* Write the event line of slot with word and text on standard output,
   at once. */
static void event(u8 slot, const char *word, const char *text)
{
    char stamp[DOS_STAMPLEN + 1];

    dos_stamp(stamp);
    printf("%s %u %s%s%s\n", stamp, (unsigned)slot, word,
           text[0] != '\0' ? " " : "", text);
    fflush(stdout);
}

/* br_events receiver: the event of slot k, its word from the list
   arg. */
static void on_event(void *arg, u8 k, u8 ev, const char *text)
{
    char word[TC_WORD + 1];

    txt_word_at((const char *)arg, ev, word, (u16)sizeof word);
    event((u8)(k + 1), word, text);
}

/* Stop the bridge and the stack; while the provider owns NCBs, wait,
   then stop the bridge again, which frees the block it kept once the
   provider has given every NCB back; put the BREAK setting and the
   critical-error handler back.  invtcp_main calls it once the run is
   over; as the atexit emergency stop it runs again, with at most the
   kept block left to free, or first when the library ends the
   program. */
static void halt(void)
{
    u16 kept;

    kept = br_stop();
    ts_term();
    if (kept != 0) {
        fprintf(stderr, "%s%u NCBs still owned by NetBIOS: waiting "
                "(a key ends the wait)\n", k_prog, kept);
        nba_drain();
        br_stop();
    }
    dos_break_restore();
    dos_crit_remove();
}

/* Start the provider check, the stack, the emergency stop and the
   bridge, with the nallow networks of allow, in the order of INVTCP.H;
   TCP_EXIT_OK, or the status of the failure, said on standard error,
   with what was started left for halt. */
static int start(const char *server, const char *local, const br_net *allow,
                 u8 nallow)
{
    u8 nbrc;

    if (!nb_present())
        return fail(TCP_EXIT_NONB, "no NetBIOS provider is loaded");
    switch (ts_init()) {
    case TS_OK:
        break;
    case TS_ENOPKT:
        return fail(TCP_EXIT_NOPKT, "no packet driver is loaded");
    case TS_ECFG:
        return fail(TCP_EXIT_STACK,
                    "WATTCP.CFG is missing or gives no address");
    default:
        return fail(TCP_EXIT_STACK, "the TCP stack did not start");
    }
    if (atexit(halt) != 0)
        return fail(TCP_EXIT_STACK, "the emergency stop could not be set up");
    switch (br_start(server, local, allow, nallow, &nbrc)) {
    case BR_OK:
        return TCP_EXIT_OK;
    case BR_ENOMEM:
        return fail(TCP_EXIT_NOMEM, "not enough memory");
    case BR_ENAME:
    case BR_ENB:
        fprintf(stderr, "%sthe local name could not be added (NetBIOS "
                "error %02X)\n", k_prog, (unsigned)nbrc);
        return TCP_EXIT_NAME;
    default:            /* BR_ELISTEN; invtcp_main rules BR_EARG out */
        return fail(TCP_EXIT_STACK, "a slot could not listen");
    }
}

/* Relay until F3 and then Y: F3 asks on standard error, again each
   time, as the event lines scroll the question away; Y, N or Esc
   answers, N and Esc for going on, and no other key counts. */
static void run(void)
{
    u16 work;
    u16 k;
    int ask;

    ask = 0;
    tmr_clock_start(&tc_clock);
    for (;;) {
        ts_poll();
        work = br_poll(tmr_clock_now(&tc_clock));
        while (key_ready()) {
            k = key_read();
            if (k == KEY_F3) {
                ask = 1;
                fprintf(stderr, "%ssessions open: %u.  Stop the bridge?  "
                        "Y=Yes N=No\n", k_prog, (unsigned)br_sessions());
            } else if (ask) {
                if (k == 'Y' || k == 'y')
                    return;
                if (k == 'N' || k == 'n' || k == KEY_ESC)
                    ask = 0;
            }
        }
        if (work == 0)
            dos_idle();
    }
}

int invtcp_main(int argc, char **argv)
{
    char t[NB_NAMELEN + TXT_U32LEN + 1];
    br_net allow[BR_ALLOWMAX];
    const char *server;
    const char *local;
    const char *a;
    int i;
    int sw;
    int st;
    u8 nallow;
    u8 major;
    u8 minor;

    cmem_init();
    if (!dos_version(&major, &minor))
        return fail(TCP_EXIT_DOSVER, "requires MS-DOS 3.1 or later");
    server = NULL;
    local = NULL;
    nallow = 0;
    for (i = 1; i < argc; i++) {
        a = argv[i];
        sw = a[0] == '/' || a[0] == '-';
        if (sw && txt_ieq(a + 1, "?")) {
            usage(stdout);
            return TCP_EXIT_OK;
        }
        if (!sw && server == NULL) {
            server = a;
        } else if (sw && txt_ieqn(a + 1, "NAME:", 5) &&
                   txt_nbname_ok(a + 6) && local == NULL) {
            local = a + 6;
        } else if (sw && txt_ieqn(a + 1, "ALLOW:", 6) &&
                   nallow < BR_ALLOWMAX && net(a + 7, &allow[nallow])) {
            nallow++;
        } else {
            fprintf(stderr, "%sbad or repeated argument: %s\n", k_prog, a);
            usage(stderr);
            return TCP_EXIT_USAGE;
        }
    }
    if (server == NULL || !txt_nbname_ok(server)) {
        usage(stderr);
        return TCP_EXIT_USAGE;
    }
    dos_crit_install();
    dos_break_off();
    st = start(server, local, allow, nallow);
    if (st != TCP_EXIT_OK) {
        halt();
        return st;
    }
    br_events(on_event, ev_words);
    /* The server's name as the bridge calls it, and the port. */
    txt_ucopy(t, server, NB_NAMELEN);
    i = (int)strlen(t);
    t[i++] = ' ';
    txt_u32(t + i, BR_PORT);
    event(0, "START", t);
    run();
    halt();
    txt_u32(t, (u32)st);
    event(0, "STOP", t);
    return st;
}

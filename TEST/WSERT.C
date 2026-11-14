/*
 * WSERT.C - test of the serial ports of a Windows program (CCOMM.C)
 * under the link's SERIAL backend (LINKSER.C over SERCLI.C).
 *
 * usage: WIN WSERT [port]
 *
 * Without port the machine has no serial port, as in "make wintest".
 * With it, that port (1..UART_MAXPORT) leads to an INVSERV serving the
 * data TEST/KEYS/SEED.REQ seeds and no other port is there, as
 * TEST/WIN/winser.sh runs the program.
 *
 * Checks:
 * - uart_open refuses a port out of range, a rate UART_BAUDOK refuses
 *   and every flag but UART_F_RTSCTS (UART_EARG), and a missing port
 *   (UART_ENOPORT), and link_open_serial a missing port (LINK_ENOPORT);
 * - a port not open reads and writes nothing, its transmitter is idle
 *   and closing it does nothing;
 * - with port: the port opens with and without UART_F_RTSCTS, and not
 *   while another program has it (OpenComm's IE_OPEN, UART_EBUSY) or a
 *   link of this one (UART_EBUSY); with the transmitter held (SETXOFF),
 *   a LOGIN closed before it goes is dropped and wiped, and requests
 *   stay queued, unwiped, through a read and a check of the transmitter,
 *   then go and are answered once it is let go (SETXON); a link carries
 *   a session of HELLO, VER, LOGIN as ADMIN, PING, a line of
 *   LINK_MAXLINE bytes adding a note to item 2, the item's notes read
 *   back, and ITEM LIST, each request answered (LINK_OK), its waits
 *   calling dos_yield; link_close, which sends BYE, frees the port;
 * - with port, the relay answers a line FLOOD, which it does not pass on,
 *   with more bytes than the driver's receive queue (CC_RXQ of
 *   CCOMM.C) holds, while the program reads none: uart_rxerrs rises
 *   (the queue overflowed, CE_RXOVER), within LINK_NETWAIT seconds; the
 *   program then reads on until the line is quiet for WS_QUIET seconds,
 *   and the sessions after it are served as before;
 * - with port, a link logged in afresh and lost to that flood (LINK_ELOST)
 *   sends a last BYE as it closes, which ends the session: a link opened
 *   once the flood is read away is refused ITEM LIST as not logged in;
 *   then one more link sends PING and closes.
 *
 * Each PING is where winser.sh's relay searches the machine's memory
 * for the password, which must be nowhere: after the held LOGIN closed
 * and the session's sent and answered, and after the lost link's
 * LOGIN and close.  The program therefore keeps the password with its
 * bits flipped (WS_FLIP), and wipes the line it builds from it once it
 * is sent.
 *
 * Writes each failure, the session (each request after "> ", LOGIN's
 * password as "***", and each line of its response after "< "), which
 * winser.sh compares with TEST/GOLDEN/WIN/WSERT.TXT, and a summary line
 * into WSERT.OUT (TEST/WTEST.C); status 0 when every check passes, 1
 * when one fails.
 */
#define COMPAT_UART
#define LINK_SERIAL
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "TESTFW.H"

#define WS_BUFSZ        16      /* bytes of the block of the closed-port
                                   reads and writes                     */
#define WS_QUEUE        64      /* bytes of each queue of the port
                                   OpenComm holds                       */
#define WS_QUIET        2       /* seconds without a byte that end the
                                   read of the flood                    */
#define WS_FLIP         0x55    /* the bits flipped in k_pw             */
#define WS_ANSWER       64      /* bytes of the block of the held
                                   requests' answers                    */

static const char k_ping[] = "PING";
static const char k_login[] = "LOGIN ADMIN ";
static const char k_note[] = "ITEM NOTES 2 ADD=\"";
/* The requests sent with the transmitter held, two lines that end the
   session they open. */
static const char k_held[] = "VER\r\nBYE\r\n";
/* The line the relay answers with its flood, on no server's account. */
static const char k_flood[] = "FLOOD\r\n";
/* SEED.REQ's last password for ADMIN. */
static const char k_pw[] = {
    'R' ^ WS_FLIP, 'e' ^ WS_FLIP, 't' ^ WS_FLIP, 'r' ^ WS_FLIP,
    'o' ^ WS_FLIP, '1' ^ WS_FLIP, '9' ^ WS_FLIP, '8' ^ WS_FLIP,
    '6' ^ WS_FLIP
};
static u16 g_yields;

/* link_line_fn: the response line into WSERT.OUT. */
static int show(void *arg, const char *line, u16 len)
{
    TF_CHECK(arg == NULL && line[len] == '\0');
    printf("< %s\n", line);
    return 0;
}

/* link_line_fn: a line of the flood, which is not written, as its
   pieces may be cut anywhere. */
static int drop(void *arg, const char *line, u16 len)
{
    return !TF_CHECK(arg == NULL && line[len] == '\0');
}

/* dos_yield: counted, then the messages waiting dispatched, so that
   Windows runs the other programs while this one polls. */
static void yield(void)
{
    MSG m;

    g_yields++;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

/* The LOGIN request of ADMIN into line (LINK_MAXLINE + 1 bytes). */
static void login_line(char *line)
{
    u16 n;
    u16 i;

    strcpy(line, k_login);
    n = (u16)strlen(line);
    for (i = 0; i < sizeof k_pw; i++)
        line[n + i] = (char)(k_pw[i] ^ WS_FLIP);
    line[n + i] = '\0';
}

/* Send line on l, shown in WSERT.OUT as shown, or as itself when shown
   is NULL. */
static void ask(link *l, const char *line, const char *shown)
{
    printf("> %s\n", shown != NULL ? shown : line);
    TF_EQ(link_send(l, line, (u16)strlen(line), show, NULL), LINK_OK);
}

/* The refusals that need no port, and those of each missing port: all
   but have, 0 for none. */
static void test_refused(u8 have)
{
    link *l;
    u8 p;

    TF_EQ(uart_open(0, UART_DEFBAUD, 0), UART_EARG);
    TF_EQ(uart_open(UART_MAXPORT + 1, UART_DEFBAUD, 0), UART_EARG);
    TF_EQ(uart_open(1, UART_MINBAUD / 2, 0), UART_EARG);
    TF_EQ(uart_open(1, UART_MAXBAUD + UART_MINBAUD, 0), UART_EARG);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_LOOP), UART_EARG);
    TF_EQ(uart_open(1, UART_DEFBAUD, UART_F_NOFIFO), UART_EARG);
    for (p = 1; p <= UART_MAXPORT; p++)
        if (p != have) {
            TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_ENOPORT);
            TF_EQ(link_open_serial(&l, p, UART_DEFBAUD, 0), LINK_ENOPORT);
            TF_CHECK(l == NULL);
        }
}

/* The calls on p, a port not open. */
static void test_closed(u8 p)
{
    cmem_blk *b;

    b = cmem_alloc(WS_BUFSZ);
    if (!TF_CHECK(b != NULL))
        return;
    cmem_set(b, 0, 0, WS_BUFSZ);
    TF_EQ(uart_read(p, b, 0, WS_BUFSZ), 0);
    TF_EQ(uart_write(p, b, 0, WS_BUFSZ), 0);
    TF_EQ(uart_txidle(p), 1);
    uart_close(p);
    cmem_free(b);
}

/* The opens of port p, with it free and with OpenComm holding it. */
static void test_open(u8 p)
{
    char name[sizeof "COM1"];
    int id;

    TF_EQ(uart_open(p, UART_DEFBAUD, UART_F_RTSCTS), UART_OK);
    uart_close(p);
    TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_OK);
    uart_close(p);
    strcpy(name, "COM1");
    name[sizeof name - 2] = (char)('0' + p);
    id = OpenComm(name, WS_QUEUE, WS_QUEUE);
    if (TF_CHECK(id >= 0)) {
        TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_EBUSY);
        CloseComm(id);
    }
}

/* The bytes in the transmit queue of COMn, whose comm ID OpenComm makes
   n - 1. */
static u16 queued(u8 n)
{
    COMSTAT st;

    GetCommError(n - 1, &st);
    return (u16)st.cbOutQue;
}

/* Put the n bytes of src into p's transmit queue, opened afresh with
   the transmitter held as if an XOFF had come, all of them waiting
   there; then a read and a check of the transmitter, which must leave
   them so.  1 when they are all queued. */
static int put_held(u8 p, cmem_blk *b, const char *src, u16 n)
{
    if (!TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_OK))
        return 0;
    EscapeCommFunction(p - 1, SETXOFF);
    cmem_put(b, 0, src, n);
    TF_EQ(uart_write(p, b, 0, n), n);
    cmem_set(b, 0, 0, n);
    TF_EQ(uart_read(p, b, 0, WS_ANSWER), 0);
    TF_EQ(uart_txidle(p), 0);
    return TF_EQ(queued(p), n);
}

/* Port p with its transmitter held: a LOGIN closed before it goes,
   which must reach neither the line nor, as the next PING's search
   finds, memory; and requests still all queued after a read and a
   check of the transmitter, which must not wipe them before they
   go, sent once the transmitter goes again and answered. */
static void test_held(u8 p)
{
    char line[LINK_MAXLINE + 1];
    cmem_blk *b;
    char *s;
    char *e;
    u32 start;
    u16 got;
    u16 n;
    int lfs;

    b = cmem_alloc(WS_ANSWER);
    if (!TF_CHECK(b != NULL))
        return;
    login_line(line);
    n = (u16)strlen(line);
    put_held(p, b, line, n);
    txt_wipe(line, (u16)sizeof line);
    uart_close(p);
    if (put_held(p, b, k_held, (u16)(sizeof k_held - 1))) {
        printf("> VER\n> BYE\n");
        EscapeCommFunction(p - 1, SETXON);
        start = tmr_ticks();
        for (got = 0, lfs = 0; lfs < 2 && got < WS_ANSWER - 1 &&
             tmr_since(start) < (u32)LINK_NETWAIT * TMR_10SEC / 10; ) {
            dos_idle();
            n = uart_read(p, b, got, (u16)(WS_ANSWER - 1 - got));
            cmem_get(b, got, line + got, n);
            for (; n > 0; n--)
                lfs += line[got++] == '\n';
        }
        line[got] = '\0';
        TF_EQ(lfs, 2);
        for (s = line; (e = strchr(s, '\n')) != NULL; s = e + 1) {
            *e = '\0';
            if (e > s && e[-1] == '\r')
                e[-1] = '\0';
            printf("< %s\n", s);
        }
    }
    uart_close(p);
    cmem_free(b);
}

/* Read port p, open, until the line is quiet for WS_QUIET seconds. */
static void quiet(u8 p, cmem_blk *b)
{
    u32 start;

    for (start = tmr_ticks(); tmr_since(start) < WS_QUIET * TMR_SEC; ) {
        dos_idle();
        if (uart_read(p, b, 0, WS_ANSWER) != 0)
            start = tmr_ticks();
    }
}

/* Port p opened, the relay's flood asked for and left unread: the
   receive error count rises; then the flood read down to the last byte. */
static void test_flood(u8 p)
{
    cmem_blk *b;
    u32 start;
    u16 errs;

    b = cmem_alloc(WS_ANSWER);
    if (!TF_CHECK(b != NULL))
        return;
    if (TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_OK)) {
        errs = uart_rxerrs(p);
        cmem_put(b, 0, k_flood, sizeof k_flood - 1);
        TF_EQ(uart_write(p, b, 0, sizeof k_flood - 1), sizeof k_flood - 1);
        start = tmr_ticks();
        while (uart_rxerrs(p) == errs &&
               tmr_since(start) < (u32)LINK_NETWAIT * TMR_10SEC / 10)
            dos_idle();
        TF_CHECK(uart_rxerrs(p) != errs);
        quiet(p, b);
        uart_close(p);
    }
    cmem_free(b);
}

/* A link on port p logged in, then lost to the relay's flood: its close
   sends a last BYE, which must end the session, so that the next link
   on the port, once the flood is read away, is not logged in. */
static void test_lost(u8 p)
{
    char line[LINK_MAXLINE + 1];
    cmem_blk *b;
    link *l;

    if (!TF_EQ(link_open_serial(&l, p, UART_DEFBAUD, 0), 0))
        return;
    ask(l, "HELLO WSERT", NULL);
    login_line(line);
    ask(l, line, "LOGIN ***");
    txt_wipe(line, (u16)sizeof line);
    printf("> FLOOD\n");
    /* The line without its CR LF, which the link adds. */
    TF_EQ(link_send(l, k_flood, (u16)(sizeof k_flood - 3), drop, NULL),
          LINK_ELOST);
    TF_EQ(link_close(l), 0);
    b = cmem_alloc(WS_ANSWER);
    if (TF_CHECK(b != NULL) &&
        TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_OK)) {
        quiet(p, b);
        uart_close(p);
    }
    cmem_free(b);
    if (TF_EQ(link_open_serial(&l, p, UART_DEFBAUD, 0), 0)) {
        ask(l, "ITEM LIST", NULL);
        TF_EQ(link_close(l), 0);
    }
}

/* The session on port p. */
static void test_session(u8 p)
{
    char line[LINK_MAXLINE + 1];
    link *l;
    u16 n;
    u16 i;

    if (!TF_EQ(link_open_serial(&l, p, UART_DEFBAUD, 0), 0))
        return;
    TF_EQ(uart_open(p, UART_DEFBAUD, 0), UART_EBUSY);
    g_yields = 0;
    ask(l, "HELLO WSERT", NULL);
    ask(l, "VER", NULL);
    login_line(line);
    ask(l, line, "LOGIN ***");
    txt_wipe(line, (u16)sizeof line);
    ask(l, k_ping, NULL);
    /* A letter counting the tens, then digits, so that every byte of
       the line, which uart_write sends in several pieces, is told
       apart in the note read back. */
    strcpy(line, k_note);
    for (i = (u16)strlen(line); i < LINK_MAXLINE - 1; i++) {
        n = (u16)(i - (sizeof k_note - 1));
        line[i] = (char)(n % 10 == 0 ? 'A' + n / 10 % 26 : '0' + n % 10);
    }
    line[i++] = '"';
    line[i] = '\0';
    ask(l, line, NULL);
    ask(l, "ITEM NOTES 2", NULL);
    ask(l, "ITEM LIST", NULL);
    TF_EQ(link_close(l), 0);
    TF_CHECK(g_yields > 0);
    test_lost(p);
    if (TF_EQ(link_open_serial(&l, p, UART_DEFBAUD, 0), 0)) {
        ask(l, k_ping, NULL);
        TF_EQ(link_close(l), 0);
    }
    test_closed(p);
}

int main(int argc, char **argv)
{
    u8 have;

    tf_begin("WSERT");
    dos_yield = yield;
    have = (u8)(argc > 1 ? atoi(argv[1]) : 0);
    test_refused(have);
    test_closed(1);
    if (have != 0) {
        test_open(have);
        test_held(have);
        test_flood(have);
        test_session(have);
    }
    dos_yield = NULL;
    return tf_done();
}

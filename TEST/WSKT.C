/*
 * WSKT.C - test of the TCP connection of a Windows program (CWSOCK.C).
 *
 * usage: WIN WSKT [/TCP:bridge /TCP:silent]
 *
 * Without the switches the machine has no Winsock, as in "make
 * wintest".  With them, the TCP bridge INVTCP listens at the address
 * bridge, at LINK_TCPPORT when it names no port, and nothing at the
 * port after that one, and no station answers at the address silent, as
 * TEST/INTEG/wintcp.sh runs the program.
 *
 * Checks:
 * - without Winsock, sk_open refuses with SK_ENOSTACK, *err 0 and *out
 *   NULL, and so it does again;
 * - with it: a connection to the port after the bridge's is refused,
 *   SK_EREFUSED with *err WSAECONNREFUSED; one to silent runs out its
 *   WK_SECS, SK_ECONN with *err 0, no sooner;
 * - one to the bridge opens and sends HELLO and BYE in one write; the
 *   program then idles WK_IDLE seconds, dispatching the messages, so
 *   that the server's two answers and the close that follows BYE have
 *   reached the stack before the first read; sk_recv still gives every
 *   byte of the answers, "+OK" twice, and then 0, as often as it is
 *   asked; sk_close.  Winsock 1.1 lets a stack post FD_CLOSE while bytes
 *   are still unread, which the reads then drain; TCP/IP-32 posts it
 *   only once they have been read.
 *
 * Writes each failure and a summary line into WSKT.OUT (TEST/WTEST.C);
 * status 0 when every check passes, 1 when one fails.
 */
#define COMPAT_SOCKET
#define LINK_TCP
#define ARGS_WINSW
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include <winsock.h>
#include "ARGS.H"
#include "LINK.H"
#include "TESTFW.H"

#define WK_SECS         3       /* the wait for silent, seconds         */
#define WK_IDLE         2       /* the idle before the read, seconds    */
#define WK_WAIT         10      /* every other wait, seconds            */
#define WK_RECVMAX      64      /* bytes of the answers kept            */

static const char k_request[] = "HELLO WSKT\r\nBYE\r\n";
static const char k_answers[] = "+OK\r\n+OK\r\n";

/* The yield hook: dispatch the messages waiting, the connection's
   window's among them. */
static void pump(void)
{
    MSG m;

    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

/* /TCP:'s address and port in s into *a (ARGS.H); 1 when it reads. */
static int address(args *a, char *s)
{
    char *argv[2];
    const char *w;

    argv[0] = NULL;
    argv[1] = s;
    return TF_EQ(args_parse(a, 2, argv, NULL, NULL, &w), ARGS_OK) &&
           TF_EQ(a->kind, ARGS_TCP);
}

static void test_none(void)
{
    static const u8 ip[4] = { 10, 0, 2, 15 };
    sk *k;
    u16 err;
    int i;

    for (i = 0; i < 2; i++) {
        k = (sk *)&err;         /* any address but NULL */
        err = 1;
        TF_EQ(sk_open(&k, ip, LINK_TCPPORT, WK_WAIT, &err), SK_ENOSTACK);
        TF_CHECK(k == NULL);
        TF_EQ(err, 0);
    }
}

static void test_refusals(const args *bridge, const args *silent)
{
    u32 start;
    u16 port;
    u16 err;
    sk *k;

    port = bridge->port != 0 ? bridge->port : LINK_TCPPORT;
    TF_EQ(sk_open(&k, bridge->ip, (u16)(port + 1), WK_WAIT, &err),
          SK_EREFUSED);
    TF_CHECK(k == NULL);
    TF_EQ(err, WSAECONNREFUSED);
    start = tmr_ticks();
    TF_EQ(sk_open(&k, silent->ip, LINK_TCPPORT, WK_SECS, &err), SK_ECONN);
    TF_CHECK(tmr_since(start) >= (u32)WK_SECS * TMR_10SEC / 10);
    TF_CHECK(k == NULL);
    TF_EQ(err, 0);
}

static void test_close(const args *bridge)
{
    char got[WK_RECVMAX];
    u16 port;
    u16 err;
    u16 n;
    u16 m;
    sk *k;

    port = bridge->port != 0 ? bridge->port : LINK_TCPPORT;
    if (!TF_EQ(sk_open(&k, bridge->ip, port, WK_WAIT, &err), SK_OK))
        return;
    TF_EQ(err, 0);
    TF_EQ(sk_send(k, k_request, (u16)(sizeof k_request - 1), WK_WAIT),
          SK_OK);
    tmr_delay((u16)(WK_IDLE * TMR_10SEC / 10));
    for (n = 0; n < sizeof got; n = (u16)(n + m)) {
        m = sk_recv(k, got + n, (u16)(sizeof got - n), WK_WAIT);
        if (m == 0)
            break;
    }
    TF_EQ(n, sizeof k_answers - 1);
    TF_CHECK(memcmp(got, k_answers, sizeof k_answers - 1) == 0);
    TF_EQ(sk_recv(k, got, (u16)sizeof got, WK_WAIT), 0);
    sk_close(k);
}

int main(int argc, char **argv)
{
    args bridge;
    args silent;

    tf_begin("WSKT");
    dos_yield = pump;
    if (argc < 3) {
        test_none();
    } else if (address(&bridge, argv[1]) && address(&silent, argv[2])) {
        test_refusals(&bridge, &silent);
        test_close(&bridge);
    }
    dos_yield = NULL;
    return tf_done();
}

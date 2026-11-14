/*
 * T_TSFAKE.C - unit test for the simulated TCP/IP stack (TSFAKE.C).
 *
 * usage: T_TSFAKE
 *
 * Checks:
 * - ts_init gives TS_OK, or the code of tsf_init_rc, or TS_EINIT
 *   without far memory, starting nothing, and TS_EARG on every later
 *   call; ts_term counts in tsf_terms only when the layer runs;
 *   before ts_init and after ts_term every slot is TS_FREE, ts_listen
 *   gives TS_EARG and ts_poll does nothing; tsf_reset starts again;
 * - ts_listen refuses a slot out of range, port 0 and a slot neither
 *   TS_FREE nor TS_CLOSED; a connection goes to the lowest slot
 *   listening on its port, one that finds none is refused and counted,
 *   and each connection taken has the peer TSF_PEERIP and the port
 *   TSF_PORT0 plus the connections taken before it; ts_peer gives
 *   zeros before a connection and once the slot listens again;
 * - bytes keep their order both ways through the rings; tsf_send takes
 *   at most TSF_QMAX bytes waiting, ts_room is TSF_QMAX less the bytes
 *   waiting or not acknowledged, whichever are more, or less under
 *   tsf_room's cap, so a read by the peer alone frees no room, and
 *   ts_write takes at most ts_room, past 64 KB too; tsf_lastbuf names
 *   the buffer of the last ts_read that read bytes;
 * - ts_unsent counts every byte written until the next ts_poll, read by
 *   the peer or not, and under tsf_noack, holding their room, until the
 *   connection ends; tsf_noack also holds a close, as tsf_nofinack
 *   does;
 * - ts_close drops the bytes received and refuses more, the peer reads
 *   what was written and then TSF_EOF, and the next ts_poll ends the
 *   close unless tsf_nofinack holds it; a slot names its last
 *   connection to the peer until it takes another;
 * - tsf_fin drops both ways at once, the slot TS_CLOSING until the next
 *   ts_poll; tsf_rst, ts_abort and ts_term drop both ways, the slot
 *   TS_CLOSED (TS_FREE after ts_term); the settings of tsf_room,
 *   tsf_noack and tsf_nofinack end with the connection;
 * - a slot out of range is TS_FREE to every call;
 * - the pump runs once at the end of every ts_poll, with its argument,
 *   never inside itself, and no other call runs it: no call waits.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "TSOCK.H"
#include "TSFAKE.H"

#define BUFSZ           (TSF_QMAX + 16)     /* each test buffer         */
#define PORT            7000    /* the port most slots listen on        */
#define OTHER           7001    /* another port                         */
#define NOSLOT          TS_SLOTS    /* the pump's slot: none            */
#define STREAM          (3 * TSF_QMAX + 77)    /* bytes of a stream     */
#define SENDK           700     /* bytes a stream sends at once         */
#define READK           500     /* bytes a stream reads at once         */
#define LONGK           (TSF_QMAX - 16)     /* bytes of one long write  */
#define LONGN           32      /* long writes, 63.5 KB in all: enough
                                   to use up TSF_NOCAP were it counted
                                   down                                 */

static u8 g_out[BUFSZ];
static u8 g_got[BUFSZ];
static u16 g_pumps;             /* pump calls                           */
static u8 g_inpump;
static u8 g_nested;             /* the pump ran inside itself           */
static void *g_arg;             /* the pump's last argument             */
static u8 g_finslot;            /* the slot the pump closes (tsf_fin)   */
static u8 g_sendslot;           /* the slot the pump sends 2 bytes on   */

/* Byte i of the test pattern. */
static u8 pat(u16 i)
{
    return (u8)(i * 7u + (i >> 8));
}

/* Bytes from to from + n - 1 of the pattern into buf. */
static void fill(u8 *buf, u16 n, u16 from)
{
    u16 i;

    for (i = 0; i < n; i++)
        buf[i] = pat((u16)(from + i));
}

/* Counts its calls, notes its argument, polls inside itself (which must
   not run it again), then closes g_finslot or sends on g_sendslot. */
static void pump(void *arg)
{
    if (g_inpump)
        g_nested = 1;
    g_inpump = 1;
    g_pumps++;
    g_arg = arg;
    ts_poll();
    tsf_fin(g_finslot);
    tsf_send(g_sendslot, "hi", 2);
    g_inpump = 0;
}

/* Every far allocation fails. */
static int no_far(u16 size)
{
    return size > 0;
}

/* A fresh layer, started, the pump counting and doing nothing else. */
static void start(void)
{
    tsf_reset();
    TF_EQ(ts_init(), TS_OK);
    g_pumps = 0;
    g_nested = 0;
    g_finslot = NOSLOT;
    g_sendslot = NOSLOT;
    tsf_pump(pump, NULL);
}

/* Slot slot listens on PORT and takes a connection: the peer's port. */
static u16 take(u8 slot)
{
    u8 got;
    u8 ip[TS_IPLEN];
    u16 port;

    TF_EQ(ts_listen(slot, PORT), TS_OK);
    got = NOSLOT;
    TF_EQ(tsf_connect(PORT, &got), TSF_OK);
    TF_EQ(got, slot);
    TF_EQ(ts_state(slot), TS_OPEN);
    ts_peer(slot, ip, &port);
    return port;
}

/* ts_peer of slot gives zeros. */
static void no_peer(u8 slot)
{
    u8 ip[TS_IPLEN];
    u16 port;

    memset(ip, 0xFF, sizeof ip);
    port = 0xFFFFu;
    ts_peer(slot, ip, &port);
    TF_EQ(ip[0] | ip[1] | ip[2] | ip[3], 0);
    TF_EQ(port, 0);
}

/* The peer reads all that waits on slot (at most BUFSZ): the count,
   which must come with TSF_UP. */
static u16 drain(u8 slot)
{
    u16 n;

    n = 0xFFFFu;
    TF_EQ(tsf_recv(slot, g_got, BUFSZ, &n), TSF_UP);
    return n;
}

/* tsf_lastbuf names p and n bytes. */
static void last(const u8 *p, u16 n)
{
    u16 len;

    TF_CHECK(tsf_lastbuf(&len) == p);
    TF_EQ(len, n);
}

/* tsf_recv on slot gives rc and no byte. */
static void ends(u8 slot, int rc)
{
    u16 n;

    n = 0xFFFFu;
    TF_EQ(tsf_recv(slot, g_got, BUFSZ, &n), rc);
    TF_EQ(n, 0);
}

/* STREAM bytes through the ring of slot, from the peer (out 0) or to
   it, SENDK bytes offered and READK read at a time. */
static void stream(u8 slot, int out)
{
    u16 sent;
    u16 got;
    u16 k;
    u16 n;
    u16 i;
    int ok;

    sent = 0;
    got = 0;
    ok = 1;
    while (got < STREAM && ok) {
        k = (u16)(STREAM - sent < SENDK ? STREAM - sent : SENDK);
        fill(g_out, k, sent);
        if (out) {
            sent = (u16)(sent + ts_write(slot, g_out, k));
            ok = TF_EQ(tsf_recv(slot, g_got, READK, &n), TSF_UP);
            ts_poll();
        } else {
            sent = (u16)(sent + tsf_send(slot, g_out, k));
            n = ts_read(slot, g_got, READK);
        }
        for (i = 0; i < n; i++)
            if (g_got[i] != pat((u16)(got + i)))
                ok = 0;
        got = (u16)(got + n);
        if (n == 0)
            ok = 0;
    }
    TF_CHECK(ok);
    TF_EQ(sent, STREAM);
}

static void test_init(void)
{
    static const int codes[] = { TS_ENOPKT, TS_ECFG, TS_EINIT };
    int i;

    tsf_reset();
    g_pumps = 0;
    tsf_pump(pump, NULL);
    TF_EQ(ts_state(0), TS_FREE);
    TF_EQ(ts_listen(0, PORT), TS_EARG);
    ts_poll();
    ts_term();
    TF_EQ(tsf_terms(), 0);
    for (i = 0; i < (int)(sizeof codes / sizeof codes[0]); i++) {
        tsf_reset();
        tsf_pump(pump, NULL);
        tsf_init_rc(codes[i]);
        TF_EQ(ts_init(), codes[i]);
        TF_EQ(ts_listen(0, PORT), TS_EARG);
        ts_poll();
        ts_term();
        TF_EQ(tsf_terms(), 0);
        TF_EQ(ts_init(), TS_EARG);
    }
    TF_EQ(g_pumps, 0);
    tsf_reset();
    tsf_pump(pump, NULL);
    TF_EQ(ts_init(), TS_OK);
    TF_EQ(ts_init(), TS_EARG);
    ts_poll();
    TF_EQ(g_pumps, 1);
    TF_EQ(ts_listen(0, PORT), TS_OK);
    TF_EQ(ts_state(0), TS_LISTEN);
    ts_term();
    TF_EQ(tsf_terms(), 1);
    TF_EQ(ts_state(0), TS_FREE);
    ts_term();
    TF_EQ(tsf_terms(), 1);
    ts_poll();
    TF_EQ(g_pumps, 1);
    TF_EQ(ts_init(), TS_EARG);
    TF_EQ(ts_listen(0, PORT), TS_EARG);
    tsf_init_rc(TS_EINIT);
    tsf_reset();
    TF_EQ(tsf_terms(), 0);
    TF_EQ(ts_init(), TS_OK);
    ts_poll();
    TF_EQ(g_pumps, 1);
    tsf_reset();
    cmem_fault = no_far;
    TF_EQ(ts_init(), TS_EINIT);
    cmem_fault = NULL;
    TF_EQ(ts_listen(0, PORT), TS_EARG);
    TF_EQ(ts_init(), TS_EARG);
}

static void test_assign(void)
{
    u8 s;
    u8 ip[TS_IPLEN];
    u16 port;

    start();
    TF_EQ(ts_listen(TS_SLOTS, PORT), TS_EARG);
    TF_EQ(ts_listen(0xFF, PORT), TS_EARG);
    TF_EQ(ts_listen(0, 0), TS_EARG);
    TF_EQ(ts_state(0), TS_FREE);
    TF_EQ(ts_listen(3, PORT), TS_OK);
    TF_EQ(ts_listen(1, PORT), TS_OK);
    TF_EQ(ts_listen(2, PORT), TS_OK);
    TF_EQ(ts_listen(0, OTHER), TS_OK);
    TF_EQ(ts_listen(1, PORT), TS_EARG);
    TF_EQ(ts_listen(1, 1), TS_EARG);
    no_peer(1);
    TF_EQ(tsf_connect(PORT, &s), TSF_OK);
    TF_EQ(s, 1);
    ts_peer(1, ip, &port);
    TF_EQ(ip[0], 10);
    TF_EQ(ip[1], 0);
    TF_EQ(ip[2], 2);
    TF_EQ(ip[3], 2);
    TF_EQ(port, TSF_PORT0);
    TF_EQ(ts_listen(1, PORT), TS_EARG);
    TF_EQ(tsf_connect(PORT, &s), TSF_OK);
    TF_EQ(s, 2);
    TF_EQ(tsf_connect(PORT, &s), TSF_OK);
    TF_EQ(s, 3);
    TF_EQ(tsf_connect(PORT, &s), TSF_REFUSED);
    TF_EQ(tsf_refused(), 1);
    TF_EQ(tsf_connect(OTHER, &s), TSF_OK);
    TF_EQ(s, 0);
    TF_EQ(tsf_connect(OTHER, &s), TSF_REFUSED);
    TF_EQ(tsf_connect(1, &s), TSF_REFUSED);
    TF_EQ(tsf_refused(), 3);
    ts_peer(2, ip, &port);
    TF_EQ(port, TSF_PORT0 + 1);
    ts_peer(3, ip, &port);
    TF_EQ(port, TSF_PORT0 + 2);
    ts_peer(0, ip, &port);
    TF_EQ(port, TSF_PORT0 + 3);
    for (s = 0; s < TS_SLOTS; s++) {
        TF_EQ(ts_state(s), TS_OPEN);
        ts_abort(s);
        TF_EQ(ts_state(s), TS_CLOSED);
        ts_peer(s, ip, &port);
        TF_CHECK(port != 0);
        TF_EQ(ts_listen(s, 0), TS_EARG);
    }
    for (s = 0; s < TS_SLOTS; s++) {
        TF_EQ(take(s), TSF_PORT0 + 4 + s);
        TF_EQ(tsf_refused(), 3);
    }
    TF_EQ(tsf_connect(PORT, &s), TSF_REFUSED);
    TF_EQ(tsf_refused(), 4);
    ts_abort(2);
    TF_EQ(ts_listen(2, PORT), TS_OK);
    no_peer(2);
    TF_EQ(tsf_connect(PORT, &s), TSF_OK);
    TF_EQ(s, 2);
    ts_peer(2, ip, &port);
    TF_EQ(port, TSF_PORT0 + 8);
    ts_term();
    for (s = 0; s < TS_SLOTS; s++)
        no_peer(s);
}

static void test_bytes(void)
{
    u8 s;
    int i;

    start();
    s = 1;
    TF_EQ(take(s), TSF_PORT0);
    last(NULL, 0);
    TF_EQ(ts_read(s, g_got, 10), 0);
    fill(g_out, BUFSZ, 0);
    TF_EQ(tsf_send(s, g_out, 100), 100);
    TF_EQ(ts_read(s, g_got, 0), 0);
    TF_EQ(ts_read(s, g_got, 40), 40);
    last(g_got, 40);
    TF_EQ(ts_read(s, g_got + 40, BUFSZ), 60);
    TF_CHECK(memcmp(g_got, g_out, 100) == 0);
    last(g_got + 40, 60);
    TF_EQ(ts_read(s, g_out, BUFSZ), 0);
    last(g_got + 40, 60);
    fill(g_out, BUFSZ, 0);
    TF_EQ(tsf_send(s, g_out, BUFSZ), TSF_QMAX);
    TF_EQ(tsf_send(s, g_out, 1), 0);
    TF_EQ(ts_read(s, g_got, 10), 10);
    TF_EQ(tsf_send(s, g_out, 20), 10);
    TF_EQ(ts_read(s, g_got, BUFSZ), TSF_QMAX);
    TF_CHECK(memcmp(g_got, g_out + 10, TSF_QMAX - 10) == 0);
    TF_CHECK(memcmp(g_got + TSF_QMAX - 10, g_out, 10) == 0);
    stream(s, 0);

    TF_EQ(ts_room(s), TSF_QMAX);
    TF_EQ(drain(s), 0);
    TF_EQ(ts_write(s, g_out, 100), 100);
    TF_EQ(ts_room(s), TSF_QMAX - 100);
    tsf_room(s, 50);
    TF_EQ(ts_room(s), 50);
    TF_EQ(ts_write(s, g_out + 100, 80), 50);
    TF_EQ(ts_room(s), 0);
    TF_EQ(ts_write(s, g_out, 1), 0);
    TF_EQ(drain(s), 150);
    TF_CHECK(memcmp(g_got, g_out, 150) == 0);
    TF_EQ(ts_room(s), 0);
    tsf_room(s, 3000);
    TF_EQ(ts_room(s), TSF_QMAX - 150);
    ts_poll();
    TF_EQ(ts_room(s), TSF_QMAX);
    TF_EQ(ts_write(s, g_out, 10), 10);
    TF_EQ(ts_room(s), TSF_QMAX - 10);
    tsf_room(s, 30);
    TF_EQ(ts_room(s), 30);
    tsf_room(s, TSF_NOCAP);
    TF_EQ(ts_write(s, g_out, BUFSZ), TSF_QMAX - 10);
    TF_EQ(ts_room(s), 0);
    TF_EQ(drain(s), TSF_QMAX);
    TF_EQ(ts_room(s), 0);
    ts_poll();
    TF_EQ(ts_room(s), TSF_QMAX);
    stream(s, 1);

    tsf_room(s, 5);
    ts_abort(s);
    TF_EQ(take(s), TSF_PORT0 + 1);
    TF_EQ(ts_room(s), TSF_QMAX);
    for (i = 0; i < LONGN; i++) {
        TF_EQ(ts_write(s, g_out, LONGK), LONGK);
        TF_EQ(drain(s), LONGK);
        ts_poll();
    }
    TF_EQ(ts_room(s), TSF_QMAX);
    TF_EQ(ts_write(s, g_out, BUFSZ), TSF_QMAX);
}

static void test_unsent(void)
{
    u8 s;

    start();
    s = 0;
    take(s);
    TF_EQ(ts_unsent(s), 0);
    TF_EQ(ts_write(s, g_out, 10), 10);
    TF_EQ(ts_unsent(s), 10);
    TF_EQ(ts_write(s, g_out, 5), 5);
    TF_EQ(drain(s), 15);
    TF_EQ(ts_unsent(s), 15);
    ts_poll();
    TF_EQ(ts_unsent(s), 0);
    tsf_noack(s);
    TF_EQ(ts_write(s, g_out, 4), 4);
    ts_poll();
    TF_EQ(drain(s), 4);
    ts_poll();
    TF_EQ(ts_unsent(s), 4);
    TF_EQ(ts_room(s), TSF_QMAX - 4);
    TF_EQ(ts_write(s, g_out, 2), 2);
    TF_EQ(ts_unsent(s), 6);
    TF_EQ(ts_room(s), TSF_QMAX - 6);
    TF_EQ(ts_listen(1, PORT), TS_OK);
    TF_EQ(ts_unsent(1), 0);
    TF_EQ(ts_unsent(TS_SLOTS), 0);
    tsf_nofinack(s);
    ts_close(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    TF_EQ(ts_unsent(s), 6);
    ts_poll();
    TF_EQ(ts_unsent(s), 6);
    ts_abort(s);
    TF_EQ(ts_unsent(s), 0);
    take(s);
    TF_EQ(ts_write(s, g_out, 3), 3);
    ts_poll();
    TF_EQ(ts_unsent(s), 0);
    tsf_noack(s);
    TF_EQ(ts_write(s, g_out, 3), 3);
    ts_close(s);
    ts_poll();
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSING);
    TF_EQ(ts_unsent(s), 3);
    tsf_fin(s);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(ts_unsent(s), 0);
    take(s);
    tsf_noack(s);
    ts_close(s);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSING);
    ts_abort(s);
    TF_EQ(ts_state(s), TS_CLOSED);
}

static void test_close(void)
{
    u8 s;
    u16 n;

    start();
    s = 2;
    take(s);
    fill(g_out, BUFSZ, 0);
    TF_EQ(tsf_send(s, "xyz", 3), 3);
    TF_EQ(ts_write(s, g_out, 20), 20);
    ts_close(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    TF_EQ(ts_read(s, g_got, BUFSZ), 0);
    TF_EQ(ts_room(s), 0);
    TF_EQ(ts_write(s, g_out, 1), 0);
    TF_EQ(tsf_send(s, "xyz", 3), 0);
    TF_EQ(tsf_recv(s, g_got, 8, &n), TSF_UP);
    TF_EQ(n, 8);
    TF_EQ(drain(s), 12);
    TF_CHECK(memcmp(g_got, g_out + 8, 12) == 0);
    ends(s, TSF_EOF);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(ts_unsent(s), 0);
    ts_close(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    ends(s, TSF_EOF);

    take(s);
    TF_EQ(ts_write(s, g_out, 30), 30);
    ts_close(s);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(ts_listen(s, PORT), TS_OK);
    no_peer(s);
    TF_EQ(drain(s), 30);
    TF_CHECK(memcmp(g_got, g_out, 30) == 0);
    ends(s, TSF_EOF);
    TF_EQ(tsf_connect(PORT, &s), TSF_OK);
    TF_EQ(s, 2);
    TF_EQ(ts_write(s, g_out, 5), 5);
    ts_close(s);
    ts_poll();
    take(s);
    TF_EQ(drain(s), 0);

    TF_EQ(ts_listen(0, PORT), TS_OK);
    ts_close(0);
    TF_EQ(ts_state(0), TS_LISTEN);
    ts_abort(0);
    ts_close(3);
    TF_EQ(ts_state(3), TS_FREE);
    tsf_nofinack(s);
    ts_close(s);
    ts_close(s);
    ts_poll();
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSING);
    ends(s, TSF_EOF);
    ts_abort(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    ends(s, TSF_GONE);
    take(s);
    ts_close(s);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    take(s);
    tsf_nofinack(s);
    ts_close(s);
    ts_poll();
    tsf_fin(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    take(s);
    tsf_nofinack(s);
    ts_close(s);
    ts_term();
    TF_EQ(ts_state(s), TS_FREE);
    TF_EQ(tsf_terms(), 1);
}

static void test_fin(void)
{
    u8 s;

    start();
    s = 1;
    take(s);
    TF_EQ(tsf_send(s, g_out, 10), 10);
    TF_EQ(ts_write(s, g_out, 10), 10);
    tsf_fin(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    TF_EQ(ts_read(s, g_got, BUFSZ), 0);
    TF_EQ(ts_room(s), 0);
    TF_EQ(ts_write(s, g_out, 1), 0);
    TF_EQ(ts_unsent(s), 0);
    TF_EQ(tsf_send(s, g_out, 1), 0);
    tsf_fin(s);
    ts_close(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    ends(s, TSF_GONE);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    tsf_fin(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(ts_listen(s, PORT), TS_OK);
    tsf_fin(s);
    tsf_rst(s);
    TF_EQ(ts_state(s), TS_LISTEN);
    tsf_fin(3);
    tsf_rst(3);
    TF_EQ(ts_state(3), TS_FREE);
    ts_abort(s);
    take(s);
    TF_EQ(ts_write(s, g_out, 10), 10);
    ts_close(s);
    tsf_fin(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    ends(s, TSF_GONE);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);

    take(s);
    TF_EQ(tsf_send(s, g_out, 10), 10);
    TF_EQ(ts_write(s, g_out, 10), 10);
    tsf_rst(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(ts_read(s, g_got, BUFSZ), 0);
    TF_EQ(ts_unsent(s), 0);
    ends(s, TSF_GONE);
    take(s);
    tsf_nofinack(s);
    ts_close(s);
    ts_poll();
    tsf_rst(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    take(s);
    tsf_fin(s);
    tsf_rst(s);
    TF_EQ(ts_state(s), TS_CLOSED);
}

static void test_abort(void)
{
    u8 s;
    u8 slot;

    start();
    s = 0;
    take(s);
    TF_EQ(tsf_send(s, g_out, 10), 10);
    TF_EQ(ts_write(s, g_out, 10), 10);
    ts_abort(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(ts_read(s, g_got, BUFSZ), 0);
    ends(s, TSF_GONE);
    TF_EQ(ts_listen(s, PORT), TS_OK);
    ts_abort(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(tsf_connect(PORT, &slot), TSF_REFUSED);
    ts_abort(3);
    TF_EQ(ts_state(3), TS_FREE);
    take(s);
    TF_EQ(ts_write(s, g_out, 7), 7);
    ts_close(s);
    ts_poll();
    ts_abort(s);
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(drain(s), 7);

    take(s);
    TF_EQ(ts_write(s, g_out, 7), 7);
    ts_close(s);
    ts_poll();
    take(1);
    TF_EQ(tsf_send(1, g_out, 10), 10);
    TF_EQ(ts_write(1, g_out, 10), 10);
    take(2);
    tsf_nofinack(2);
    ts_close(2);
    ts_term();
    for (slot = 0; slot < TS_SLOTS; slot++) {
        TF_EQ(ts_state(slot), TS_FREE);
        ends(slot, TSF_GONE);
        no_peer(slot);
        TF_EQ(ts_read(slot, g_got, BUFSZ), 0);
        TF_EQ(ts_room(slot), 0);
        TF_EQ(ts_unsent(slot), 0);
    }
    TF_EQ(tsf_connect(PORT, &slot), TSF_REFUSED);
}

/* Every call takes slots out of range, and every slot before ts_init,
   for slots TS_FREE. */
static void test_range(void)
{
    static const u8 bad[] = { TS_SLOTS, 0xFF };
    int i;
    u8 s;

    tsf_reset();
    last(NULL, 0);
    for (s = 0; s < TS_SLOTS; s++) {
        TF_EQ(ts_read(s, g_got, BUFSZ), 0);
        TF_EQ(ts_room(s), 0);
        TF_EQ(ts_write(s, g_out, 1), 0);
        TF_EQ(tsf_send(s, g_out, 1), 0);
        ends(s, TSF_GONE);
        no_peer(s);
    }
    start();
    for (s = 0; s < TS_SLOTS; s++)
        take(s);
    for (i = 0; i < (int)sizeof bad; i++) {
        s = bad[i];
        TF_EQ(ts_state(s), TS_FREE);
        TF_EQ(ts_read(s, g_got, BUFSZ), 0);
        TF_EQ(ts_room(s), 0);
        TF_EQ(ts_write(s, g_out, 1), 0);
        TF_EQ(ts_unsent(s), 0);
        ts_close(s);
        ts_abort(s);
        no_peer(s);
        TF_EQ(tsf_send(s, g_out, 1), 0);
        ends(s, TSF_GONE);
        tsf_fin(s);
        tsf_rst(s);
        tsf_room(s, 0);
        tsf_noack(s);
        tsf_nofinack(s);
    }
    for (s = 0; s < TS_SLOTS; s++) {
        TF_EQ(ts_state(s), TS_OPEN);
        TF_EQ(ts_room(s), TSF_QMAX);
        TF_EQ(ts_write(s, g_out, 1), 1);
        ts_poll();
        TF_EQ(ts_unsent(s), 0);
        ts_close(s);
        ts_poll();
        TF_EQ(ts_state(s), TS_CLOSED);
    }
}

static void test_pump(void)
{
    u8 s;
    int x;

    start();
    s = 3;
    take(s);
    tsf_pump(pump, &x);
    ts_poll();
    TF_EQ(g_pumps, 1);
    TF_CHECK(g_arg == &x);
    TF_EQ(g_nested, 0);
    g_sendslot = s;
    ts_poll();
    TF_EQ(g_pumps, 2);
    TF_EQ(ts_read(s, g_got, BUFSZ), 2);
    g_sendslot = NOSLOT;
    g_finslot = s;
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSING);
    ts_poll();
    TF_EQ(ts_state(s), TS_CLOSED);
    TF_EQ(g_pumps, 4);
    TF_EQ(g_nested, 0);
    tsf_pump(NULL, NULL);
    ts_poll();
    TF_EQ(g_pumps, 4);
}

/* No call but ts_poll runs the pump, so none waits for the stack. */
static void test_nowait(void)
{
    u8 s;
    u8 ip[TS_IPLEN];
    u16 n;

    start();
    TF_EQ(ts_listen(0, PORT), TS_OK);
    TF_EQ(tsf_connect(PORT, &s), TSF_OK);
    tsf_room(s, 4);
    tsf_noack(s);
    tsf_nofinack(s);
    TF_EQ(tsf_send(s, g_out, 3), 3);
    TF_EQ(ts_read(s, g_got, BUFSZ), 3);
    TF_EQ(ts_write(s, g_out, 9), 4);
    TF_EQ(ts_write(s, g_out, 9), 0);
    TF_EQ(ts_room(s), 0);
    TF_EQ(ts_unsent(s), 4);
    ts_peer(s, ip, &n);
    TF_EQ(tsf_recv(s, g_got, BUFSZ, &n), TSF_UP);
    ts_close(s);
    TF_EQ(ts_state(s), TS_CLOSING);
    tsf_fin(s);
    tsf_rst(s);
    ts_abort(s);
    TF_EQ(tsf_connect(PORT, &s), TSF_REFUSED);
    tsf_init_rc(TS_OK);
    tsf_lastbuf(&n);
    tsf_refused();
    tsf_terms();
    ts_term();
    TF_EQ(g_pumps, 0);
}

int main(void)
{
    tf_begin("T_TSFAKE");
    cmem_init();
    test_init();
    test_assign();
    test_bytes();
    test_unsent();
    test_close();
    test_fin();
    test_abort();
    test_range();
    test_pump();
    test_nowait();
    tsf_reset();
    return tf_done();
}

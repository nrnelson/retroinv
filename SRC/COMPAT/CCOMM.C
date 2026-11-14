/*
 * CCOMM.C - the serial ports COM1 and COM2 of a Windows program, through
 * the comm driver of Windows 3.0.
 *
 * The driver (COMM.DRV) owns the chip and its interrupt: OpenComm gives
 * the program a port with a receive queue of CC_RXQ bytes and a
 * transmit queue of CC_TXQ, which the driver's handler fills and
 * empties, and ReadComm and WriteComm move bytes between the queues and
 * the program.  This module serves through them the calls of COMPAT.H's
 * serial section that a client makes, so that SERCLI.C and LINKSER.C run
 * on it unchanged.  Windows 3.0 has no WM_COMMNOTIFY, so nothing tells
 * the program that bytes have come: they wait in the receive queue until
 * uart_read takes them, which SERCLI's waits do between their yields
 * (dos_idle), and the server sends nothing unasked.  The queue is large
 * so that a response of a few kilobytes keeps coming while another
 * program has the processor.
 *
 * Bytes cross between a block and the driver through a buffer on the
 * stack, as the driver takes a far address and a block has none between
 * two cmem_ calls (COMPAT.H).  The driver keeps every byte of its
 * transmit queue until it is written over, so the queue is wiped (wipe)
 * once it has drained after a write, at the next uart_read or
 * uart_txidle, which every wait of SERCLI's makes, and again as
 * uart_close closes the port: a request's password stays in the queue
 * only while it is being sent.
 */
#define COMPAT_UART
#include "COMPAT.H"
#include <string.h>
#include "CWIN.H"

#define CC_RXQ          4096    /* bytes of the driver's receive queue  */
#define CC_TXQ          1024    /* bytes of its transmit queue          */
#define CC_CHUNK        128     /* bytes through the stack at a time    */
/* The errors of GetCommError that uart_rxerrs counts. */
#define CC_RXERR        (CE_OVERRUN | CE_RXOVER | CE_FRAME)
/* RTS/CTS: the room left in the receive queue that drops RTS, and the
   bytes left in it that raise it again, as CUART.C's UART_RXHIGH and
   UART_RXLOW are to its ring. */
#define CC_FLOWLIM      (CC_RXQ / 4)

static const char k_com[] = "COM1";

typedef struct {
    int cid;                    /* OpenComm's ID plus 1, 0 while closed */
    u16 errs;                   /* receive errors since uart_open       */
    u8 sent;                    /* flag: bytes queued since last wipe   */
} cc_port;

static cc_port cc_ports[UART_MAXPORT];

/* The open port numbered port, or NULL. */
static cc_port *port_of(u8 port)
{
    if (port < 1 || port > UART_MAXPORT || cc_ports[port - 1].cid == 0)
        return NULL;
    return &cc_ports[port - 1];
}

/* Fill st with p's queues, and count one receive error when the
   driver latched any since the last call; GetCommError also clears the
   latch, which would refuse every later call. */
static void comm_error(cc_port *p, COMSTAT *st)
{
    if (GetCommError(p->cid - 1, st) & CC_RXERR)
        p->errs++;
}

/* The bytes p's transmit queue has room for, at most CC_CHUNK. */
static int tx_room(cc_port *p)
{
    COMSTAT st;
    UINT room;

    comm_error(p, &st);
    room = CC_TXQ - st.cbOutQue;
    return room > CC_CHUNK ? CC_CHUNK : (int)room;
}

/* Drop the bytes p's transmit queue holds, as CUART.C's close drops
   them, and write zeros over the whole queue, holding the transmitter
   as if an XOFF had come so that none of them is sent; then drop them
   and let the transmitter go. */
static void wipe(cc_port *p)
{
    char zero[CC_CHUNK];
    int id;
    int m;

    id = p->cid - 1;
    EscapeCommFunction(id, SETXOFF);
    FlushComm(id, 0);
    memset(zero, 0, sizeof zero);
    while ((m = tx_room(p)) > 0 && WriteComm(id, zero, m) == m)
        ;
    FlushComm(id, 0);
    EscapeCommFunction(id, SETXON);
}

/* 1 when the transmit queue of p is empty, after wiping it when bytes
   have gone into it since the last wipe; else 0. */
static int drained(cc_port *p)
{
    COMSTAT st;

    comm_error(p, &st);
    if (st.cbOutQue != 0)
        return 0;
    if (p->sent) {
        wipe(p);
        p->sent = 0;
    }
    return 1;
}

int uart_open(u8 port, u16 baud, u8 flags)
{
    char name[sizeof k_com];
    DCB dcb;
    int id;
    UINT rtscts;

    if (port < 1 || port > UART_MAXPORT || !UART_BAUDOK(baud) ||
        (flags & ~UART_F_RTSCTS) != 0)
        return UART_EARG;
    if (cc_ports[port - 1].cid != 0)
        return UART_EBUSY;
    memcpy(name, k_com, sizeof k_com);
    name[sizeof k_com - 2] = (char)('0' + port);
    /* A port the machine lacks gives IE_HARDWARE. */
    id = OpenComm(name, CC_RXQ, CC_TXQ);
    if (id == IE_OPEN)
        return UART_EBUSY;
    if (id == IE_MEMORY)
        return UART_ENOMEM;
    if (id < 0)
        return UART_ENOPORT;
    /* The settings of WIN.INI's [ports], which may name XON/XOFF, are
       replaced but for the timeouts and characters, which nothing
       here uses. */
    GetCommState(id, &dcb);
    rtscts = (flags & UART_F_RTSCTS) != 0;
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = 1;
    dcb.fRtsDisable = 0;
    dcb.fParity = 0;
    dcb.fOutxCtsFlow = rtscts;
    dcb.fOutxDsrFlow = 0;
    dcb.fDtrDisable = 0;
    dcb.fOutX = 0;
    dcb.fInX = 0;
    dcb.fPeChar = 0;
    dcb.fNull = 0;
    dcb.fChEvt = 0;
    dcb.fDtrflow = 0;
    dcb.fRtsflow = rtscts;
    dcb.XonLim = CC_FLOWLIM;
    dcb.XoffLim = CC_FLOWLIM;
    if (SetCommState(&dcb) < 0) {
        CloseComm(id);
        return UART_EARG;
    }
    cc_ports[port - 1].cid = id + 1;
    return UART_OK;
}

void uart_close(u8 port)
{
    cc_port *p;

    p = port_of(port);
    if (p == NULL)
        return;
    wipe(p);
    CloseComm(p->cid - 1);
    memset(p, 0, sizeof *p);
}

u16 uart_read(u8 port, cmem_blk *b, u16 off, u16 n)
{
    char buf[CC_CHUNK];
    COMSTAT st;
    cc_port *p;
    u16 done;
    int id;
    int m;
    int got;

    p = port_of(port);
    if (p == NULL)
        return 0;
    /* Every wait for a response polls here, so the request is wiped
       from the queue once it is out. */
    drained(p);
    id = p->cid - 1;
    for (done = 0; done < n; ) {
        m = n - done > CC_CHUNK ? CC_CHUNK : (int)(n - done);
        /* A result below 1 may be an error, latched until GetCommError
           clears it, its size then the bytes read before it. */
        got = ReadComm(id, buf, m);
        if (got <= 0) {
            comm_error(p, &st);
            got = -got;
        }
        cmem_put(b, (u16)(off + done), buf, (u16)got);
        done = (u16)(done + got);
        if (got < m)
            break;
    }
    return done;
}

u16 uart_write(u8 port, cmem_blk *b, u16 off, u16 n)
{
    char buf[CC_CHUNK];
    cc_port *p;
    u16 done;
    int id;
    int m;
    int put;

    p = port_of(port);
    if (p == NULL)
        return 0;
    id = p->cid - 1;
    for (done = 0; done < n; ) {
        m = tx_room(p);
        if ((u16)m > n - done)
            m = (int)(n - done);
        if (m == 0)
            break;
        cmem_get(b, (u16)(off + done), buf, (u16)m);
        /* A negative result is an error after -put bytes, which the
           next tx_room clears. */
        put = WriteComm(id, buf, m);
        if (put < 0)
            put = -put;
        done = (u16)(done + put);
        if (put < m)
            break;
    }
    if (done != 0)
        p->sent = 1;
    txt_wipe(buf, (u16)sizeof buf);
    return done;
}

int uart_txidle(u8 port)
{
    cc_port *p;

    p = port_of(port);
    return p == NULL || drained(p);
}

u16 uart_rxerrs(u8 port)
{
    COMSTAT st;
    cc_port *p;

    p = port_of(port);
    if (p == NULL)
        return 0;
    comm_error(p, &st);
    return p->errs;
}

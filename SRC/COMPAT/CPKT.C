/*
 * CPKT.C - the packet-driver calls of WATTCP's kernel, on a class 1 (DIX
 * Ethernet) driver of the Packet Driver Specification 1.09.
 *
 * A packet driver hooks one software interrupt of 60h..80h; its handler
 * starts with three bytes of code and the text "PKT DRVR" with its NUL.
 * _pkt_eth_init looks for that text through the interrupt vector table,
 * from 60h up, and opens the first driver whose driver_info reports
 * class 1 and whose access_type grants a handle for IP frames (type
 * 0800h) and one for ARP frames (0806h); get_address then gives the
 * station's address.  A driver that refuses leaves nothing held, and the
 * search goes on.  The kernel cannot learn of a failure (its tcp_init
 * returns nothing), so pkt_error records it for the kernel's caller.  The
 * first opening registers pkt_release with atexit, so that the driver
 * stops calling into the program however it ends.
 *
 * The driver calls the receive upcall, cpkt_up, twice for every frame:
 * first for a buffer, then to say the frame is in it.  The upcall is
 * machine code in the data segment, the bytes of CPKTUP.ASM (make lint
 * checks them against it), with the address of cpkt_head written into it
 * when the driver is opened.  It fills the CPKT_NBUF buffers of cpkt_bufs
 * as a ring, in order: a frame that finds the next buffer not free, or
 * that is longer than CPKT_FRAME, is dropped.  pkt_received takes the
 * frames in the same order, and pkt_buf_release zeroes the bytes of a
 * frame as it frees its buffer, since frames carry passwords; pkt_release
 * zeroes every frame once the driver has let go of the buffers.  The upcall
 * moves a buffer from CB_FREE to CB_BUSY and on to CB_FULL, the program
 * from CB_FULL to CB_BUSY, while the kernel holds the frame, and back to
 * CB_FREE; only the upcall writes cpkt_head and only the program
 * cpkt_tail.  Each of these is a word written in one instruction, so the
 * two sides need no lock.
 *
 * The driver's calls take their buffers in DS and ES, both set to the
 * data segment: the access_type type is a local, which relies on SS being
 * the data segment, as in the small and medium models.  IP fragments are
 * not reassembled: one passes as a whole datagram and fails its TCP,
 * UDP or ICMP checksum.
 *
 * Like every file that includes WATTCP's headers, this one is built with
 * WATTCP's byte packing and signed char (-zp1 -j for Open Watcom, /j for
 * Watcom C 6.5; Turbo C's defaults).
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include <dos.h>
#include <stdlib.h>
#include <string.h>
#include "WATTCP.H"

#define CPKT_NBUF       5       /* receive buffers                      */
#define CPKT_NTYPE      2       /* frame types taken: IP and ARP        */
#define CPKT_FRAME      1514    /* longest Ethernet frame, FCS excluded */
#define CPKT_INTLO      0x60    /* software interrupts a driver may use */
#define CPKT_INTHI      0x80
#define CPKT_SIGAT      3       /* the text's offset from the handler   */
#define CPKT_UP_HEAD    12      /* offset of &cpkt_head in cpkt_up      */

/* Driver functions, AX or AH. */
#define PD_CLASS        1       /* DIX Ethernet                         */
#define PD_INFO         0x01FF  /* driver_info                          */
#define PD_ACCESS       (0x0200 | PD_CLASS) /* access_type, class in AL */
#define PD_RELEASE      3       /* release_type                         */
#define PD_SEND         4       /* send_pkt                             */
#define PD_ADDRESS      6       /* get_address                          */
#define PD_ANYTYPE      0xFFFF  /* access_type: any interface type      */

/* A buffer's states, and the offsets of its fields, as in CPKTUP.ASM. */
#define CB_FREE         0
#define CB_BUSY         1       /* filling, or held by the kernel       */
#define CB_FULL         2       /* received, not yet taken              */
#define CB_LEN          2
#define CB_NEXT         4
#define CB_FRAME        6

typedef struct cpkt_buf {
    volatile u16 state;         /* CB_FREE, CB_BUSY or CB_FULL          */
    u16 len;                    /* bytes in frame                       */
    struct cpkt_buf *next;      /* the next buffer of the ring          */
    u8 frame[CPKT_FRAME];
} cpkt_buf;

/* The header is three words, next a near pointer, and the buffer has no
   padding (the build fails otherwise), so the upcall finds the fields
   at the offsets above. */
extern char cpkt_buf_check[1];
extern char cpkt_buf_check[(sizeof(cpkt_buf *) == 2 && CB_FRAME == 3 * 2 &&
                            sizeof(cpkt_buf) == CB_FRAME + CPKT_FRAME) ?
                           1 : 2];

/* The bytes of CPKTUP.ASM, one instruction to a row. */
static u8 cpkt_up[] = {
    0x9C,
    0x85, 0xC0,
    0x75, 0x2F,
    0x81, 0xF9, CPKT_FRAME & 0xFF, CPKT_FRAME >> 8,
    0x77, 0x23,
    0xBB, 0x00, 0x00,
    0x2E, 0x8B, 0x3F,
    0x2E, 0x83, 0x3D, CB_FREE,
    0x75, 0x17,
    0x2E, 0xC7, 0x05, CB_BUSY, 0x00,
    0x2E, 0x89, 0x4D, CB_LEN,
    0x2E, 0x8B, 0x45, CB_NEXT,
    0x2E, 0x89, 0x07,
    0x83, 0xC7, CB_FRAME,
    0x0E,
    0x07,
    0x9D,
    0xCB,
    0x31, 0xFF,
    0x8E, 0xC7,
    0x9D,
    0xCB,
    0xC7, 0x44, -CB_FRAME & 0xFF, CB_FULL, 0x00,
    0x9D,
    0xCB
};

static cpkt_buf cpkt_bufs[CPKT_NBUF];
static cpkt_buf *cpkt_head;     /* the next buffer the upcall fills     */
static cpkt_buf *cpkt_tail = cpkt_bufs; /* the next one taken           */
static u16 cpkt_h[CPKT_NTYPE];  /* the handles, IP first                */
static int cpkt_nh;             /* handles held                         */
static u8 cpkt_int;             /* the driver's interrupt               */
static eth_address cpkt_mac;    /* the station's Ethernet address       */
static int cpkt_err = PKT_ENONE;
static int cpkt_exitset;        /* 1 once pkt_release is registered     */

/* The offset of the IP header in a received frame.  The kernel takes a
   connecting peer's Ethernet address from the frame only when it is not
   0. */
word _pktipofs = sizeof(eth_Header);

/* Call the driver with r, DS and ES the data segment: 0, or 1 when it
   reports an error (carry). */
static int pd(union REGS *r)
{
    struct SREGS s;

    segread(&s);
    s.es = s.ds;
    int86x(cpkt_int, r, r, &s);
    return r->x.cflag != 0;
}

/* 1 when the handler of interrupt n starts as a packet driver's does. */
static int is_driver(int n)
{
    static const char sig[] = "PKT DRVR";
    FARP(u16) vec;
    FARP(u8) p;
    int i;

    vec = CFP(u16, 0, n * 4);
    p = CFP(u8, vec[1], vec[0] + CPKT_SIGAT);
    for (i = 0; i < (int)sizeof sig; i++)
        if (p[i] != (u8)sig[i])
            return 0;
    return 1;
}

/* Take a handle for the frames of Ethernet type etype, through the
   upcall: 0, or 1 when the driver refuses. */
static int take_type(u16 etype)
{
    union REGS r;
    u8 type[2];

    type[0] = (u8)(etype >> 8);
    type[1] = (u8)(etype & 0xFFu);
    r.x.ax = PD_ACCESS;
    r.x.bx = PD_ANYTYPE;
    r.h.dl = 0;
    r.x.si = (unsigned)type;
    r.x.cx = sizeof type;
    r.x.di = (unsigned)cpkt_up;
    if (pd(&r) != 0)
        return 1;
    cpkt_h[cpkt_nh++] = (u16)r.x.ax;
    return 0;
}

/* Open the driver of interrupt n, as the file's header comment says: 0,
   or 1 with nothing held. */
static int open_driver(int n)
{
    static const u16 etypes[CPKT_NTYPE] = { 0x0800, 0x0806 };
    union REGS r;
    cpkt_buf *b;
    int rc;
    int i;

    cpkt_int = (u8)n;
    r.x.ax = PD_INFO;
    if (pd(&r) != 0 || r.h.ch != PD_CLASS)
        return 1;
    for (b = cpkt_bufs; b < cpkt_bufs + CPKT_NBUF - 1; b++)
        b->next = b + 1;
    b->next = cpkt_bufs;
    cpkt_head = cpkt_bufs;
    cpkt_tail = cpkt_bufs;
    put_le16(cpkt_up + CPKT_UP_HEAD, (unsigned)&cpkt_head);
    rc = 0;
    for (i = 0; rc == 0 && i < CPKT_NTYPE; i++)
        rc = take_type(etypes[i]);
    if (rc == 0) {
        r.h.ah = PD_ADDRESS;
        r.x.bx = cpkt_h[0];
        r.x.di = (unsigned)cpkt_mac;
        r.x.cx = sizeof cpkt_mac;
        rc = pd(&r);
    }
    if (rc != 0)
        pkt_release();
    return rc;
}

void *_pkt_eth_init(void)
{
    int n;

    cpkt_err = PKT_ENONE;
    for (n = CPKT_INTLO; n <= CPKT_INTHI; n++) {
        if (!is_driver(n))
            continue;
        if (open_driver(n) == 0) {
            cpkt_err = PKT_OK;
            break;
        }
        cpkt_err = PKT_EREFUSED;
    }
    if (cpkt_err == PKT_OK && !cpkt_exitset) {
        atexit(pkt_release);
        cpkt_exitset = 1;
    }
    return cpkt_mac;
}

int pkt_error(void)
{
    return cpkt_err;
}

/* The ring's links stay as they are, so that a driver that refused
   release_type still finds a valid ring. */
void pkt_release(void)
{
    union REGS r;

    cpkt_buf *b;

    while (cpkt_nh > 0) {
        cpkt_nh--;
        r.h.ah = PD_RELEASE;
        r.x.bx = cpkt_h[cpkt_nh];
        pd(&r);
    }
    for (b = cpkt_bufs; b < cpkt_bufs + CPKT_NBUF; b++)
        memset(b->frame, 0, sizeof b->frame);
}

int pkt_send(char *buffer, int length)
{
    union REGS r;

    if (cpkt_nh == 0)
        return 1;
    r.h.ah = PD_SEND;
    r.x.si = (unsigned)buffer;
    r.x.cx = (unsigned)length;
    return pd(&r);
}

/* The buffer of the next frame received, now CB_BUSY, or NULL when none
   is waiting. */
static cpkt_buf *take(void)
{
    cpkt_buf *b;

    b = cpkt_tail;
    if (b->state != CB_FULL)
        return NULL;
    b->state = CB_BUSY;
    cpkt_tail = b->next;
    return b;
}

/* Zero the frame in b and free the buffer. */
static void give_back(cpkt_buf *b)
{
    memset(b->frame, 0, b->len);
    b->state = CB_FREE;
}

void *pkt_received(void)
{
    cpkt_buf *b;

    b = take();
    return b != NULL ? b->frame : NULL;
}

/* ptr is the frame's IP or ARP header, sizeof(eth_Header) bytes in. */
void pkt_buf_release(char *ptr)
{
    give_back((cpkt_buf *)(ptr - sizeof(eth_Header) - CB_FRAME));
}

/* The kernel calls this once the driver is open, to drop the frames
   that came before it was ready: those waiting are released. */
void pkt_buf_wipe(void)
{
    cpkt_buf *b;
    int i;

    for (i = 0; i < CPKT_NBUF && (b = take()) != NULL; i++)
        give_back(b);
}

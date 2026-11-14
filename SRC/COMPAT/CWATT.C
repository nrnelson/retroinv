/*
 * CWATT.C - WATTCP's assembly helpers in C, the few names of the WATTCP
 * modules the bridge leaves out, and the kernel's initial sequence
 * numbers.
 *
 * WATTCP's kernel calls a small library of assembly routines for its
 * timers, the Internet checksum, byte order and console text.  This file
 * gives the routines it references, with the prototypes of its ELIB.H,
 * so that no assembler is needed and every build compiles them.  The
 * bridge configures a static address, so the BOOTP and name-server
 * modules are not linked: their variables, which the configuration
 * reader still sets, are defined here, and resolve takes only dotted
 * addresses.
 *
 * The file includes WATTCP's headers and is built, as the kernel is, with
 * byte packing and signed char; the checks below refuse a build whose
 * structs or char differ from the kernel's.
 */
#define COMPAT_INTERNAL
#define COMPAT_FINE
#include "COMPAT.H"
#include "WATTCP.H"

#define WT_STDERR       2       /* DOS handle of standard error         */
#define WT_OUTBUF       32      /* bytes outs passes to one DOS write   */
#define WT_ROUNDS       4       /* rounds of wt_isn's mix               */

/* Seconds in which a count of TMR_10SEC / 10 ticks a second gains one
   over TMR_SEC a second; set_timeout is exact only when that is a whole
   number (the build fails otherwise). */
#define WT_SECX         (10 / (TMR_10SEC - 10 * TMR_SEC))
extern char wt_secx_whole[1];
extern char wt_secx_whole[(10 % (TMR_10SEC - 10 * TMR_SEC) == 0) ? 1 : 2];

/* The kernel's wire and socket layouts, as WATTCP's own build of 1994
   lays them out: a header of another size, or a socket that is not
   byte-packed, would corrupt frames or the data beside a socket.  The
   IP header's other fields take 19 bytes, so it is 20 only when its two
   4-bit fields share one byte, with tos at offset 1.  The socket size
   is that of the medium model, whose function pointers take 4 bytes.
   char is signed, as WATTCP's code takes it to be. */
extern char wt_in_header_20[1];
extern char wt_in_header_20[(sizeof(in_Header) == 20) ? 1 : 2];
extern char wt_tcp_header_20[1];
extern char wt_tcp_header_20[(sizeof(tcp_Header) == 20) ? 1 : 2];
extern char wt_arp_header_28[1];
extern char wt_arp_header_28[(sizeof(arp_Header) == 28) ? 1 : 2];
extern char wt_tcp_socket_4218[1];
extern char wt_tcp_socket_4218[(sizeof(tcp_Socket) == 4218) ? 1 : 2];
extern char wt_signed_char[1];
extern char wt_signed_char[((char)-1 < 0) ? 1 : 2];

/* ---------------------------------------------------------------------
 * Timers.
 *
 * A timeout is a tick count extended past midnight: the BIOS count of
 * the day plus TMR_DAY for each midnight seen since the program
 * started, so that a later time is always the larger number.  The count
 * goes back only at midnight, so a midnight is missed only when the
 * clock is not read for a whole day.
 * --------------------------------------------------------------------- */
static u32 wt_day;
static u32 wt_last;

/* The extended tick count now. */
static u32 now(void)
{
    u32 t;

    t = tmr_ticks();
    if (t < wt_last)
        wt_day += TMR_DAY;
    wt_last = t;
    return wt_day + t;
}

unsigned long set_ttimeout(unsigned int ticks)
{
    return now() + ticks;
}

/* seconds * TMR_10SEC / 10 ticks from now, without a long division. */
unsigned long set_timeout(unsigned int seconds)
{
    return (u32)seconds * TMR_SEC + seconds / WT_SECX + now();
}

int chk_timeout(unsigned long timeout)
{
    return timeout < now();
}

/* ---------------------------------------------------------------------
 * Initial sequence numbers.
 *
 * WATTCP starts every connection of a listening socket at a number made
 * from the socket's address, the same each time, so that a host that
 * cannot see the replies could still complete a connection it forges
 * from another address.  WATTCP.DIF has the kernel take wt_isn's number
 * instead when a SYN arrives.  The numbers come from a secret pool of
 * two words: wt_stir, which ts_poll calls on every pass of the
 * program's loop, turns one of them and folds the timer chip's count
 * (tmr_fine) into both its halves, so that the pool takes in the
 * chip's phase at every pass, whenever a frame arrives.  A number mixes
 * the pool, a count of the numbers made and the tick in WT_ROUNDS rounds
 * of add, rotate and exclusive or, and gives out 32 of its 64 bits, the
 * two words folded together: never the pool, from which the next number
 * could be worked out.  It is not cryptography: it keeps a host beyond
 * a router, which never sees the replies, from predicting the number a
 * forged connection needs.
 * --------------------------------------------------------------------- */
static u32 wt_pool[2];
static u32 wt_isns;             /* numbers made                         */
static u8 wt_at;                /* the pool word the next stir turns    */

/* x turned left by n bits, 1..31. */
static u32 rotl(u32 x, int n)
{
    return (x << n) | (x >> (32 - n));
}

void wt_stir(void)
{
    u16 f;

    f = tmr_fine();
    wt_at ^= 1;
    wt_pool[wt_at] = rotl(wt_pool[wt_at], 7) ^ (((u32)f << 16) | f);
}

unsigned long wt_isn(void)
{
    u32 a;
    u32 b;
    int i;

    wt_stir();
    a = wt_pool[0] + ++wt_isns;
    b = wt_pool[1] ^ now();
    for (i = 0; i < WT_ROUNDS; i++) {
        a += b;
        b = rotl(b, 13) ^ a;
        a = rotl(a, 16);
    }
    return a ^ b;
}

/* ---------------------------------------------------------------------
 * Checksum and byte order.
 * --------------------------------------------------------------------- */

/* The 1's complement sum of the little-endian words at ptr, an odd last
   byte taken as the low byte of a word.  That is the sum of the
   network-order words with its bytes swapped, the same two bytes in
   memory, so the kernel stores it, or its complement, as it is.  The
   carries out of the low word gather in the high word and are added
   back at the end, which gives the same sum as adding each at once;
   len is an int, so the high word cannot overflow. */
unsigned inchksum(FARP(void) ptr, int len)
{
    FARP(const u16) w;
    u32 sum;
    int n;

    w = (FARP(const u16))ptr;
    sum = 0;
    for (n = len >> 1; n > 0; n--)
        sum += *w++;
    if (len & 1)
        sum += *(FARP(const u8))w;
    sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)(sum + (sum >> 16));
}

unsigned intel16(unsigned x)
{
    return (x << 8) | (x >> 8);
}

unsigned long intel(unsigned long x)
{
    return ((u32)intel16((unsigned)x) << 16) | intel16((unsigned)(x >> 16));
}

/* ---------------------------------------------------------------------
 * Console text.
 *
 * The kernel's messages (a configuration file not found, a frame with a
 * bad checksum) go to standard error as the kernel passes them, CR and
 * LF included.
 * --------------------------------------------------------------------- */
void outs(FARP(char) s)
{
    char buf[WT_OUTBUF];
    int n;

    n = 0;
    while (*s != '\0') {
        buf[n++] = *s++;
        if (n == WT_OUTBUF) {
            cf_write(WT_STDERR, buf, WT_OUTBUF);
            n = 0;
        }
    }
    if (n != 0)
        cf_write(WT_STDERR, buf, (u16)n);
}

/* ---------------------------------------------------------------------
 * Names of the modules left out.
 *
 * The configuration reader stores its BOOTP, name-server and domain
 * settings in these, and the ARP wait saves and restores the Ctrl-Break
 * flags.  None of the values is acted on: the modules that would, and
 * the Ctrl-Break handler that would set watcbroke, are not linked.
 * --------------------------------------------------------------------- */
word _bootpon;
word _bootptimeout;
longword _bootphost;
char *def_domain;
longword def_nameservers[MAX_NAMESERVERS];
int _last_nameserver;
int _domaintimeout;
word wathndlcbrk;
word watcbroke;

longword resolve(char *name)
{
    return isaddr(name) ? aton(name) : 0;
}

/* ---------------------------------------------------------------------
 * The program's name, Watcom C 6.5 only.
 *
 * Watcom C 6.5's library keeps no copy of argv.  tcp_config copies
 * _argv[0] to find WATTCP.CFG beside the program when it is given no
 * path; the empty name makes it look in the current directory.
 * --------------------------------------------------------------------- */
#ifdef COMPAT_WC6
char *wt_argv[] = { "" };
#endif

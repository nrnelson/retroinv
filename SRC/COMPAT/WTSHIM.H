/*
 * WTSHIM.H - the Turbo C library names the WATTCP sources use, for the
 * compilers that lack them.
 *
 * WATTCP was written for Turbo C.  The prepared tree includes this header
 * at the top of WATTCP.H and in place of every <mem.h> and <values.h>, so
 * every WATTCP module reads it before WATTCP's own declarations, of which
 * it uses none.  It also declares the one call of ours that WATTCP.DIF adds
 * to the kernel, and the stir that feeds it.  Under Turbo C it includes the
 * two headers it stands for and maps the interrupt-flag calls; under
 * Watcom it defines the Borland names over the standard library.
 * <stdlib.h> declares atoi, which UDP_NDS.C calls without including it,
 * and under Turbo C and Open Watcom also min and max.
 *
 * WT_BF4 is the type of the IP header's two 4-bit fields: the header is
 * 20 bytes, with tos at offset 1, only when the two share one byte.
 * Watcom gives unsigned int bit fields a two-byte unit; Turbo C 2.01
 * takes no other type and packs them into one byte.
 */
#ifndef WTSHIM_H
#define WTSHIM_H

#include "COMPAT.H"
#include <stdlib.h>

#ifdef COMPAT_TC
#include <mem.h>
#include <values.h>
#define WT_BF4          unsigned
#define _disable        disable
#define _enable         enable
#else
#include <string.h>
#include <limits.h>
#define WT_BF4          unsigned char
#define MAXINT          INT_MAX
#define movmem(s, d, n) memmove((d), (s), (n))
#ifndef min
#define min(a, b)       ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)       ((a) > (b) ? (a) : (b))
#endif
#endif

/* tcp_config reads argv[0] for the directory of WATTCP.CFG when it is
   given no path and the WATTCP.CFG variable is not set.  Open Watcom
   names it __argv; Watcom C 6.5's library keeps no copy, so CWATT.C
   defines wt_argv in its place. */
#ifdef COMPAT_OW
#define _argv           __argv
#endif
#ifdef COMPAT_WC6
#define _argv           wt_argv
extern char *wt_argv[];
#endif

/* The initial sequence number of the connection a listening socket
   takes, which WATTCP.DIF has the kernel ask for when a SYN arrives;
   wt_stir stirs the pool the numbers come from, on every pass of the
   program's loop (CWATT.C). */
unsigned long wt_isn(void);
void wt_stir(void);

/* Watcom C 6.5's library has open, read and close but not the
   underscored names; its open always takes the permission argument. */
#ifdef COMPAT_WC6
#define _open(p, f)     open((p), (f), 0)
#define _read           read
#define _close          close
#endif

#endif

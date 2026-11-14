/*
 * PORTCHK.H - COMTEST's loopback check of a serial port (PORTCHK.C).
 *
 * The check is a module of its own, apart from the program COMTEST.C,
 * so that the unit test runs it on the simulated ports.
 */
#ifndef PORTCHK_H
#define PORTCHK_H

#include <stdio.h>
#include "COMPAT.H"

/* Results of pchk_run, which are COMTEST's exit statuses. */
#define PCHK_OK         0       /* every byte came back in order        */
#define PCHK_EOPEN      2       /* the port could not be opened         */
#define PCHK_EFAIL      3       /* a byte was lost or changed           */

/* Send every byte value, 0 to 255 in order, through port
   (1..UART_MAXPORT) in the chip's own loopback at baud, with the
   uart_open flags (UART_F_RTSCTS or 0), reading them back until all
   are in or none has moved for TMR_SEC ticks; then write to out, a
   line each and each line starting "COMTEST ", the port and chip, the
   I/O base and IRQ, the speed (and "RTS/CTS" under that flow control)
   with the bytes that came back in order, and the verdict "COMTEST
   PASS" or "COMTEST FAIL byte lost or changed".  Returns PCHK_OK,
   PCHK_EFAIL, or PCHK_EOPEN with the verdict alone written, "COMTEST
   FAIL COMn: " and the reason "no such port", "port in use" or "no
   memory". */
int pchk_run(u8 port, u16 baud, u8 flags, FILE *out);

#endif

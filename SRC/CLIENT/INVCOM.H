/*
 * INVCOM.H - INV's serial switches (INVCOM.C): opening the SERIAL link
 * of /COM1 and /COM2.
 *
 * The open takes the port's speed and flow control from the settings
 * (BAUD= and RTSCTS= of CFG.H) and says on standard error why a port
 * could not be opened: "no such port", "port in use" or "no memory".
 * It is a module of its own so that INV.C compiles without the serial
 * section of COMPAT.H, within Watcom C 6.5's compiler memory.
 */
#ifndef INVCOM_H
#define INVCOM_H

#include "COMPAT.H"
#include "LINK.H"
#include "CFG.H"

/* Open the SERIAL link over port (1..UART_MAXPORT) into *l at c's
   settings.  Returns 0, or a LINK_E code of link_open_serial, said on
   standard error. */
int invcom_open(link **l, u8 port, const cfg *c);

#endif

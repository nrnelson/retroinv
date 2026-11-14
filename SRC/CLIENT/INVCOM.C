/*
 * INVCOM.C - INV's serial switches (INVCOM.H).
 *
 * The reasons a port could not be opened are built in, as they are
 * written before INV.MSG is loaded, or in /CMD mode, which does not
 * load it.
 */
#define COMPAT_UART
#define LINK_SERIAL
#include <stdio.h>
#include "COMPAT.H"
#include "LINK.H"
#include "CFG.H"
#include "INVCOM.H"
#include "INVCONN.H"

#define IC_WHYMEM       3       /* the reason of LINK_ENOMEM in why[]   */

/* The failure is named from a table indexed from LINK_ENOPORT, with
   LINK_ENOMEM at IC_WHYMEM.  INV opens only COM1 or COM2 (ARGS.C) at a
   rate of UART_BAUDOK (CFG.C), so link_open_serial never gives it
   LINK_ESETUP: its row repeats LINK_ENOPORT's text rather than take
   INV's near data with one of its own.  The pair declares one array
   twice, the second time with size 2 when that fails: the declarations
   then disagree, which every compiler refuses. */
extern char invcom_codes_ok[1];
extern char invcom_codes_ok[(LINK_EBUSY == LINK_ENOPORT + 1 &&
                             LINK_ESETUP == LINK_ENOPORT + 2) ? 1 : 2];

static const char k_noport[] = "no such port";

int invcom_open(link **l, u8 port, const cfg *c)
{
    static const char *const why[] = {
        k_noport, "port in use", k_noport, "no memory"
    };
    int rc;

    rc = link_open_serial(l, port, c->baud,
                          (u8)(c->rtscts ? UART_F_RTSCTS : 0));
    if (rc != 0) {
        fputs(inv_tag, stderr);
        fputs("cannot open COM", stderr);
        fputc('0' + port, stderr);
        fputs(": ", stderr);
        fputs(why[rc == LINK_ENOMEM ? IC_WHYMEM : rc - LINK_ENOPORT],
              stderr);
        fputc('\n', stderr);
    }
    return rc;
}

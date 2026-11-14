/*
 * PORTCHK.C - COMTEST's loopback check of a serial port (PORTCHK.H).
 *
 * The check copies its pattern into the first half of a far block and
 * reads what comes back into the second half; in loopback the port has
 * no interrupt, so each uart_ call polls the chip.
 */
#define COMPAT_UART
#include <stddef.h>
#include <stdio.h>
#include "COMPAT.H"
#include "PORTCHK.H"

#define PC_PATN         256     /* bytes of the pattern, one per value  */

/* The tables of pchk_run are indexed by the open failure, a UART_E
   code from UART_ENOPORT on (UART_EARG cannot come: COMTEST checks the
   speed, and the flags are fixed), and by the chip, a UART_T_ code
   from UART_T_8250 on.  The pair declares one array twice, the second
   time with size 2 when that fails: the declarations then disagree,
   which every compiler refuses. */
extern char pchk_codes_ok[1];
extern char pchk_codes_ok[(UART_EBUSY == UART_ENOPORT + 1 &&
                           UART_ENOMEM == UART_ENOPORT + 2 &&
                           UART_T_16450 == UART_T_8250 + 1 &&
                           UART_T_16550 == UART_T_8250 + 2 &&
                           UART_T_16550A == UART_T_8250 + 3) ? 1 : 2];

/* Write the number v to out, in hex digits without leading zeros when
   hex is nonzero, else in decimal. */
static void num(FILE *out, u16 v, int hex)
{
    char b[TXT_U32LEN + 1];
    int sh;
    int n;

    if (hex) {
        for (sh = 12; sh > 0 && (v >> sh) == 0; sh -= 4)
            ;
        for (n = 0; sh >= 0; sh -= 4)
            b[n++] = txt_hexdig[(v >> sh) & 0xF];
        b[n] = '\0';
    } else {
        txt_u32(b, v);
    }
    fputs(b, out);
}

int pchk_run(u8 port, u16 baud, u8 flags, FILE *out)
{
    static const char *const why[] = {
        "no such port", "port in use", "no memory"
    };
    static const char *const chips[] = {
        "8250", "16450", "16550", "16550A"
    };
    u8 pat[PC_PATN];
    uart_info ui;
    cmem_blk *blk;
    u32 start;
    u16 sent;
    u16 got;
    u16 n;
    u16 m;
    int rc;

    blk = cmem_alloc(2 * PC_PATN);
    rc = blk == NULL ? UART_ENOMEM :
         uart_open(port, baud, (u8)(flags | UART_F_LOOP));
    if (rc != UART_OK) {
        cmem_free(blk);
        fputs("COMTEST FAIL COM", out);
        num(out, port, 0);
        fputs(": ", out);
        fputs(why[rc - UART_ENOPORT], out);
        fputc('\n', out);
        return PCHK_EOPEN;
    }
    uart_getinfo(port, &ui);
    for (n = 0; n < PC_PATN; n++)
        pat[n] = (u8)n;
    cmem_put(blk, 0, pat, PC_PATN);
    sent = 0;
    got = 0;
    start = tmr_ticks();
    while (got < PC_PATN && tmr_since(start) < TMR_SEC) {
        n = uart_write(port, blk, sent, (u16)(PC_PATN - sent));
        sent = (u16)(sent + n);
        m = uart_read(port, blk, (u16)(PC_PATN + got),
                      (u16)(PC_PATN - got));
        got = (u16)(got + m);
        if (n != 0 || m != 0)
            start = tmr_ticks();
        else
            dos_idle();
    }
    uart_close(port);
    cmem_get(blk, PC_PATN, pat, got);
    cmem_free(blk);
    for (n = 0; n < got && pat[n] == (u8)n; n++)
        ;
    fputs("COMTEST COM", out);
    num(out, port, 0);
    fputs(": ", out);
    fputs(chips[ui.chip - UART_T_8250], out);
    fputs("\nCOMTEST I/O base ", out);
    num(out, ui.base, 1);
    fputs("h, IRQ ", out);
    num(out, ui.irq, 0);
    fputs("\nCOMTEST ", out);
    num(out, baud, 0);
    fputs(flags & UART_F_RTSCTS ? " baud, RTS/CTS: " : " baud: ", out);
    num(out, n, 0);
    fputs(" bytes back in order\n", out);
    fputs(n == PC_PATN ? "COMTEST PASS\n" :
          "COMTEST FAIL byte lost or changed\n", out);
    return n == PC_PATN ? PCHK_OK : PCHK_EFAIL;
}

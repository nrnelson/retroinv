/*
 * COMTEST.C - the serial port test.
 *
 *   COMTEST [/COM1 | /COM2] [/BAUD:n] [/RTSCTS]
 *
 * COMTEST checks a serial port before INV or INVSERV uses it: COM1, or
 * COM2 with /COM2, in the chip's own loopback, so that nothing reaches
 * the line, at n baud (a rate of UART_BAUDOK in COMPAT.H; UART_DEFBAUD
 * without /BAUD:), with RTS/CTS flow control under /RTSCTS.  Switches
 * match in any case and may begin with '-' for '/'.  /? alone writes the
 * usage text to standard output and exits 0; any other bad command line
 * writes it to standard error and nothing to standard output.
 * Otherwise COMTEST sends every byte value and writes the chip, I/O
 * base, IRQ, speed and the bytes that came back in order to standard
 * output, each line starting "COMTEST ", the last the verdict, "COMTEST
 * PASS" or "COMTEST FAIL reason" (PORTCHK.H).  While the port is open,
 * BREAK checking is off and DOS's critical-error prompt is replaced, so
 * that neither can end the program with the port's interrupt handler
 * installed.
 *
 * Exit status: 0 every byte came back in order; 1 usage error; 2 the
 * port could not be opened; 3 a byte was lost or changed; 4 DOS older
 * than 3.1.
 */
#define COMPAT_UART
#include <stdio.h>
#include "COMPAT.H"
#include "PORTCHK.H"

#define CT_EXIT_USAGE   1       /* bad command line                     */
#define CT_EXIT_DOSVER  4       /* DOS older than 3.1                   */

/* Print the usage text to f and return CT_EXIT_USAGE. */
static int usage(FILE *f)
{
    fputs("usage: COMTEST [/COM1 | /COM2] [/BAUD:n] [/RTSCTS]\n", f);
    return CT_EXIT_USAGE;
}

int main(int argc, char **argv)
{
    const char *s;
    u16 baud;
    u8 port;
    u8 flags;
    int st;
    int i;
    u8 major;
    u8 minor;

    cmem_init();
    if (!dos_version(&major, &minor)) {
        fputs("COMTEST: requires MS-DOS 3.1 or later\n", stderr);
        return CT_EXIT_DOSVER;
    }
    s = argv[1];
    if (argc == 2 && (s[0] == '/' || s[0] == '-') && txt_ieq(s + 1, "?")) {
        usage(stdout);
        return PCHK_OK;
    }
    port = 0;
    baud = 0;
    flags = 0;
    for (i = 1; i < argc; i++) {
        s = argv[i];
        if (s[0] != '/' && s[0] != '-')
            return usage(stderr);
        s++;
        if ((txt_ieq(s, "COM1") || txt_ieq(s, "COM2")) && port == 0) {
            port = (u8)(s[3] - '0');
        } else if (txt_ieqn(s, "BAUD:", 5) && baud == 0) {
            baud = txt_num(s + 5, UART_MAXBAUD);
            if (!UART_BAUDOK(baud))
                return usage(stderr);
        } else if (txt_ieq(s, "RTSCTS") && flags == 0) {
            flags = UART_F_RTSCTS;
        } else {
            return usage(stderr);
        }
    }
    dos_break_off();
    dos_crit_install();
    st = pchk_run((u8)(port != 0 ? port : 1),
                  (u16)(baud != 0 ? baud : UART_DEFBAUD), flags, stdout);
    dos_crit_remove();
    dos_break_restore();
    return st;
}

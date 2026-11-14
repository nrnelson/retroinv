/*
 * INVSERV.C - the RETROINV server, INVSERV.EXE.
 *
 *   INVSERV [/CFG:file]
 *
 *   /CFG:    the settings file (default SRVCFG_NAME in the current
 *            directory; SRVCFG.H lists its keys)
 *
 * The switch is matched without regard to case and may begin with '-'
 * instead of '/'; /? writes the usage text to standard output.
 *
 * INVSERV is the one program that opens the data files while it runs:
 * it serves the protocol over three transports, to the clients that
 * call its NetBIOS name (TNETB.H), to those on its serial ports
 * (TSERIAL.H) and to the request files of its mailbox (TMBOX.H), one
 * session per connection slot (CONN.H), and shows its state on the
 * status screen (SRVSCR.H) until the operator shuts it down with F3 or
 * a client with ADMIN SHUTDOWN.  This file reads the command line,
 * after cmem_init (the first call of main, before any memory is taken),
 * the DOS version check and cstk_fill, which fills the stack below
 * main's frame for the STACK event of the log; srv_run (SRVRUN.H) does
 * the rest, with the message file MSG_SRVFILE looked for in the
 * program's directory and, when it is not there, in the current one
 * (msg_fopen).
 *
 * Exit status: the SRV_EXIT_ code of SRVRUN.H.
 */
#include <stddef.h>
#include <stdio.h>
#include "COMPAT.H"
#include "SRVCFG.H"
#include "SRVRUN.H"

static const char k_cfgname[] = SRVCFG_NAME;

static void usage(FILE *f)
{
    fputs("usage: INVSERV [/CFG:file]\n", f);
    fputs("  /CFG:   the settings file (default ", f);
    fputs(k_cfgname, f);
    fputs(")\n", f);
}

int main(int argc, char **argv)
{
    char progdir[SRV_DIRSZ];
    const char *cfgp;
    const char *a;
    int i;
    u8 major;
    u8 minor;

    cmem_init();
    if (!dos_version(&major, &minor)) {
        fputs("INVSERV: requires MS-DOS 3.1 or later\n", stderr);
        return SRV_EXIT_DOSVER;
    }
    cstk_fill();
    cfgp = NULL;
    for (i = 1; i < argc; i++) {
        a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && txt_ieq(a + 1, "?")) {
            usage(stdout);
            return SRV_EXIT_OK;
        }
        if ((a[0] != '/' && a[0] != '-') || !txt_ieqn(a + 1, "CFG:", 4) ||
            a[5] == '\0' || cfgp != NULL) {
            fputs("INVSERV: bad or repeated argument: ", stderr);
            fputs(a, stderr);
            fputc('\n', stderr);
            usage(stderr);
            return SRV_EXIT_USAGE;
        }
        cfgp = a + 5;
    }
    txt_path_dir(progdir, (u16)sizeof progdir, argc > 0 ? argv[0] : "");
    return srv_run(cfgp != NULL ? cfgp : k_cfgname, progdir);
}

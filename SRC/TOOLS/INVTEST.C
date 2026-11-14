/*
 * INVTEST.C - scripted test driver of the server over NetBIOS sessions,
 * serial lines and the mailbox.
 *
 *   INVTEST <server> <script> [/EXIT]
 *
 * INVTEST runs the script against the server named <server>, or with
 * "-" against no NetBIOS server, which leaves NetBIOS alone (INVSCR.H
 * gives the script format and the transcript), with the transcript on
 * standard output.  The last line is "INVTEST PASS" or "INVTEST FAIL
 * line n", n naming the script line that failed ("INVTEST FAIL" alone
 * when the script did not run).
 *
 * While the script runs, the critical-error handler of COMPAT.H is
 * installed, so that a disk or network error on the mailbox's drive
 * fails the call with its code instead of prompting on the console.
 *
 * Exit status: the result of invscr_run, 0 pass, 1 fail (a usage error
 * included), 2 NetBIOS not present, 3 a NetBIOS session could not be
 * opened, 4 a serial port could not be opened.  With /EXIT the status
 * is also written to the QEMU isa-debug-exit port so a test VM ends.
 */
#define COMPAT_NETBIOS
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "INVSCR.H"

/* Print the verdict for the exit status code, failed at script line
   line (0 for none), end the VM with code when exitvm is set, and
   return code. */
static int finish(int exitvm, int code, u16 line)
{
    if (code == INVSCR_PASS)
        printf("INVTEST PASS\n");
    else if (line != 0)
        printf("INVTEST FAIL line %u\n", line);
    else
        printf("INVTEST FAIL\n");
    fflush(stdout);
    if (exitvm)
        cexit_vm((u8)code);
    return code;
}

int main(int argc, char **argv)
{
    const char *arg[2];
    int exitvm;
    int nargs;
    int code;
    int i;
    u16 kept;
    u16 line;

    cmem_init();
    exitvm = 0;
    nargs = 0;
    for (i = 1; i < argc; i++) {
        if (txt_ieq(argv[i], "/EXIT"))
            exitvm = 1;
        else if (nargs < 2)
            arg[nargs++] = argv[i];
        else
            nargs = 3;
    }
    if (nargs != 2) {
        printf("usage: INVTEST <server>|- <script> [/EXIT]\n");
        return finish(exitvm, INVSCR_FAIL, 0);
    }
    dos_break_off();
    dos_crit_install();
    code = invscr_run(stdout, strcmp(arg[0], "-") != 0 ? arg[0] : NULL,
                      arg[1], &line);
    /* An NCB is kept only after NBCLI cancelled a command whose wait ran
       out, which has failed the run already. */
    kept = nba_stuck();
    if (kept != 0) {
        printf("%u NCBs kept, waiting for them (a key ends the wait)\n",
               kept);
        fflush(stdout);
        nba_drain();
    }
    dos_crit_remove();
    dos_break_restore();
    return finish(exitvm, code, line);
}

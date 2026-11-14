/*
 * CEXIT.C - end a QEMU test VM through the isa-debug-exit device.
 *
 * The device sits at the I/O port CEXIT_PORT; writing a byte there makes
 * QEMU exit with host status code * 2 + 1.  Without the device the write
 * goes to an unused port and has no effect, so the function returns
 * normally.
 *
 * QEMU stops the machine at once, before DOS closes the program's files,
 * and DOS records a file's size in its directory entry only when a handle
 * to it is closed.  So that redirected output (NBTEST ... > FILE) is not
 * left empty, standard output and standard error are flushed and DOS
 * handles 1 and 2 committed with dos_commit_handle() first.
 */
#include "COMPAT.H"
#include <stdio.h>
#ifdef COMPAT_OW
#include <conio.h>
#endif
#ifdef COMPAT_WC6
#include <conio.h>
#endif
#ifdef COMPAT_TC
#include <dos.h>
#endif

#define CEXIT_PORT      0x501

void cexit_vm(u8 code)
{
    fflush(stdout);
    fflush(stderr);
    dos_commit_handle(1);
    dos_commit_handle(2);
#ifdef COMPAT_HOST
    (void)code;
#endif
#ifdef COMPAT_OW
    outp(CEXIT_PORT, code);
#endif
#ifdef COMPAT_WC6
    outp(CEXIT_PORT, code);
#endif
#ifdef COMPAT_TC
    outportb(CEXIT_PORT, code);
#endif
}

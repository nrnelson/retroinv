/*
 * CNBDRAIN.C - the wait at a program's end for the NCBs nba_close kept
 * (nba_drain, COMPAT.H): portable C over nba_stuck, apart from CNETB.C,
 * so that the tests' fake provider serves it as it serves the programs.
 */
#define COMPAT_NETBIOS
#include "COMPAT.H"

u16 nba_drain(void)
{
    u16 left;

    while ((left = nba_stuck()) != 0 && key_peek() == KEY_NONE)
        dos_idle();
    if (left != 0)
        key_read();
    return left;
}

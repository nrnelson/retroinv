/*
 * CFINE.C - the timer chip's count (tmr_fine, COMPAT.H), a source of
 * entropy for the server's login tickets and WATTCP's initial sequence
 * numbers, in a module of its own so that only the programs that make
 * them link it.
 *
 * Channel 0 of the 8253 or 8254 is latched and read with interrupts
 * disabled, and the interrupt flag is then left as it was found.  The
 * host build returns the count the tests set.  DOS and host builds only.
 */
#define COMPAT_INTERNAL
#define COMPAT_FINE
#include "COMPAT.H"

#ifdef COMPAT_HOST

static u16 tm_fine;

u16 tmr_fine(void)
{
    return tm_fine;
}

void tmr_fine_set(u16 v)
{
    tm_fine = v;
}

#else

#include <dos.h>
#ifdef COMPAT_TC
#define TM_IN(port)     inportb(port)
#define TM_OUT(port, v) outportb((port), (u8)(v))
#else
#include <conio.h>
#define TM_IN(port)     inp(port)
#define TM_OUT(port, v) outp((port), (v))
#endif

#define TM_PITDATA      0x40    /* channel 0's count                    */
#define TM_PITCTL       0x43    /* mode and command register            */
#define TM_LATCH0       0x00    /* latch channel 0's count              */

u16 tmr_fine(void)
{
    u16 fl;
    u16 lo;
    u16 hi;

    fl = IRQ_SAVE();
    IRQ_OFF();
    TM_OUT(TM_PITCTL, TM_LATCH0);
    lo = (u16)TM_IN(TM_PITDATA);
    hi = (u16)TM_IN(TM_PITDATA);
    if (fl & IRQ_IF)
        IRQ_ON();
    return (u16)((hi << 8) | lo);
}

#endif

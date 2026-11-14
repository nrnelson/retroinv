/*
 * CTIMER.C - the BIOS tick counter, elapsed ticks and idle delays.
 *
 * Under DOS the counter is the double word at 0040:006C, which the
 * timer interrupt advances and sets back to 0 at midnight; it is read
 * with interrupts disabled so that its two words belong together, and
 * the interrupt flag is then left as it was found.  Under Windows it
 * is made from the milliseconds of GetTickCount.  The host build counts
 * virtual ticks, one per call of tmr_ticks.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"

#ifdef COMPAT_WIN16
#include "CWIN.H"
#else
#ifndef COMPAT_HOST
#include <dos.h>
#endif
#endif

#ifdef COMPAT_HOST

static u32 tm_virtual;

u32 tmr_ticks(void)
{
    tm_virtual++;
    if (tm_virtual >= TMR_DAY)
        tm_virtual = 0;
    return tm_virtual;
}

void tmr_delay(u16 n)
{
    (void)n;
}

#else

#ifdef COMPAT_WIN16

#define TM_10SECMS      10000UL /* milliseconds in TMR_10SEC ticks      */

/* The milliseconds are split at whole ten-second spans, so that neither
   product overflows. */
u32 tmr_ticks(void)
{
    u32 ms;

    ms = GetTickCount();
    return (ms / TM_10SECMS * TMR_10SEC +
            ms % TM_10SECMS * TMR_10SEC / TM_10SECMS) % TMR_DAY;
}

#else

#define TM_COUNT        0x006C  /* offset of the tick counter           */

u32 tmr_ticks(void)
{
    FARP(volatile u16) p;
    u16 fl;
    u16 lo;
    u16 hi;
    u32 t;

    p = CFP(volatile u16, BDA_SEG, TM_COUNT);
    fl = IRQ_SAVE();
    IRQ_OFF();
    lo = p[0];
    hi = p[1];
    if (fl & IRQ_IF)
        IRQ_ON();
    t = ((u32)hi << 16) | (u32)lo;
    if (t >= TMR_DAY)
        t = 0;
    return t;
}

#endif

/* The counter may have moved on just after start was read, so n + 1
   changes are waited for, which makes the wait at least n whole ticks. */
void tmr_delay(u16 n)
{
    u32 start;

    if (n == 0)
        return;
    start = tmr_ticks();
    while (tmr_since(start) <= (u32)n)
        dos_idle();
}

#endif

u32 tmr_since(u32 start)
{
    u32 now;

    now = tmr_ticks();
    if (now >= start)
        return now - start;
    return now + (u32)TMR_DAY - start;
}

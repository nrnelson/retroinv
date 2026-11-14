/*
 * T_CLOCK.C - unit test for dos_time of COMPAT.H under DOS: the time of
 * day stays in range and in order across the ticks where MS-DOS 3.10
 * reports second 60.
 *
 * usage: T_CLOCK
 *
 * The BIOS tick count is set (INT 1Ah function 01h) to every tick from
 * CK_FIRST to CK_LAST, each followed by a dos_time.  Tick CK_SIXTY is
 * the last tick of minute 0 (59.98 s); Compaq MS-DOS 3.10 reads it as
 * 0:00:60 (its CLOCK$ driver rounds it up to a second it does not carry
 * into the minute), as it does the last tick of two minutes in every
 * five.  The test sets the BIOS clock itself, which portable code may
 * not, so it lives in SRC/COMPAT/ and is built for DOS only.  Setting
 * the count clears the BIOS's midnight flag on some machines, so the test
 * first waits out the last TMR_SEC ticks before midnight, so that
 * midnight does not pass during the run, and reads the date, through
 * which DOS takes a flag still pending.  At the end the count is set
 * back to the one read at the start plus the ticks since the last set,
 * so the run goes on with the time it had, less the sweep's own
 * fraction of a tick.
 *
 * Checks:
 * - at every tick of the sweep the hour is 0, the minute 0 or 1 and the
 *   second 0..59, the time never goes back, and tick CK_SIXTY reads as
 *   0:00:59 (clamped on MS-DOS 3.10), or as 0:01:00 when a timer
 *   interrupt between the set and the read has moved the count on.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include "COMPAT.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "TESTFW.H"

#define CK_FIRST        1088UL  /* first tick of the sweep              */
#define CK_SIXTY        1092UL  /* the tick MS-DOS 3.10 reads 0:00:60   */
#define CK_LAST         1096UL  /* last tick of the sweep               */

/* Set the BIOS tick count to t (INT 1Ah function 01h, CX:DX). */
static void set_ticks(u32 t)
{
    union REGS r;

    memset(&r, 0, sizeof r);
    r.h.ah = 0x01;
    r.x.cx = (unsigned)(t >> 16);
    r.x.dx = (unsigned)(t & 0xFFFFUL);
    int86(0x1A, &r, &r);
}

static void test_minute(void)
{
    u32 start;
    u32 t;
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 min;
    u8 sec;
    u16 prev;
    u16 now;

    while (tmr_ticks() >= TMR_DAY - TMR_SEC)
        ;
    dos_today(&year, &month, &day);
    start = tmr_ticks();
    prev = 0;
    for (t = CK_FIRST; t <= CK_LAST; t++) {
        set_ticks(t);
        dos_time(&hour, &min, &sec);
        TF_EQ(hour, 0);
        TF_CHECK(min <= 1);
        TF_CHECK(sec <= 59);
        now = (u16)(min * 60u + sec);
        TF_CHECK(now >= prev);
        prev = now;
        if (t == CK_SIXTY)
            TF_CHECK((min == 0 && sec == 59) || (min == 1 && sec == 0));
    }
    set_ticks(start + tmr_since(CK_LAST));
}

int main(void)
{
    tf_begin("T_CLOCK");
    test_minute();
    return tf_done();
}

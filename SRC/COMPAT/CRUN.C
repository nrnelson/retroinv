/*
 * CRUN.C - the run helpers of the programs that log events from a
 * polling loop (INVSERV, INVTCP): the loop's tick count (tmr_clock) and
 * the time stamp of a log line (dos_stamp).  Portable C over CTIMER.C,
 * CDOS.C and CTEXT.C, in a module of its own so that no other program
 * links it.
 */
#include "COMPAT.H"

void tmr_clock_start(tmr_clock *c)
{
    c->now = 0;
    c->tick = tmr_ticks();
}

u32 tmr_clock_now(tmr_clock *c)
{
    u32 d;
    u16 year;
    u8 month;
    u8 day;

    d = tmr_since(c->tick);
    c->tick += d;
    if (c->tick >= TMR_DAY) {
        c->tick -= (u32)TMR_DAY;
        dos_today(&year, &month, &day);
    }
    c->now += d;
    return c->now;
}

void dos_stamp(char *d)
{
    u16 year;
    u8 t[5];                    /* month, day, hour, minute, second     */
    u8 day;
    int i;

    do {
        dos_today(&year, &t[0], &t[1]);
        dos_time(&t[2], &t[3], &t[4]);
        dos_today(&year, &t[0], &day);
    } while (day != t[1]);
    txt_u32w(d, year, 4, '0');
    for (i = 0; i < 5; i++) {
        d[4 + 3 * i] = "-- ::"[i];
        txt_u32w(d + 5 + 3 * i, t[i], 2, '0');
    }
    d[DOS_STAMPLEN] = '\0';
}

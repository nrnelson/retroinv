/*
 * T_CRUN.C - unit test for CRUN.C on the host: a loop's tick count over
 * the host's virtual ticks, and the time stamp, with dos_today and
 * dos_time faked here.
 *
 * usage: T_CRUN
 *
 * Checks:
 * - tmr_clock_now counts every tick from tmr_clock_start once, across
 *   two midnights of the tick count;
 * - it reads the date through DOS (dos_today) at each midnight it sees
 *   pass and at no other time;
 * - dos_stamp writes "YYYY-MM-DD HH:MM:SS", and when the day changes
 *   between its two reads of the date, reads the date and the time
 *   again, so the stamp holds the new day with its time.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include "COMPAT.H"
#include "TESTFW.H"

static u16 g_dates;             /* calls of dos_today                   */
static u8 g_day;                /* the day dos_today gives              */
static u8 g_roll;               /* calls of dos_today before the day
                                   moves on, 0 for never                */

/* Day g_day of November 2026, moved on after the g_roll-th call. */
void dos_today(u16 *year, u8 *month, u8 *day)
{
    g_dates++;
    *year = 2026;
    *month = 11;
    *day = g_day;
    if (g_roll != 0 && --g_roll == 0)
        g_day++;
}

/* The last second of day 6, and midnight on any other day. */
void dos_time(u8 *hour, u8 *min, u8 *sec)
{
    *hour = (u8)(g_day == 6 ? 23 : 0);
    *min = (u8)(g_day == 6 ? 59 : 0);
    *sec = (u8)(g_day == 6 ? 59 : 0);
}

/* Every tick counted once, the date read at each midnight and only
   then: after each call, as many reads as midnights seen. */
static void test_midnight(void)
{
    tmr_clock c;
    u32 n;
    u32 last;
    u32 bad;
    u16 wraps;

    g_day = 6;
    g_roll = 0;
    tmr_clock_start(&c);
    g_dates = 0;
    wraps = 0;
    bad = 0;
    for (n = 1; n <= 2 * (u32)TMR_DAY; n++) {
        last = c.tick;
        if (tmr_clock_now(&c) != n)
            bad++;
        if (c.tick < last)
            wraps++;
        if (g_dates != wraps)
            bad++;
    }
    TF_EQ(bad, 0);
    TF_EQ(wraps, 2);
    TF_EQ(g_dates, 2);
}

static void test_stamp(void)
{
    char d[DOS_STAMPLEN + 1];

    g_day = 6;
    g_roll = 0;
    dos_stamp(d);
    TF_STREQ(d, "2026-11-06 23:59:59");
    g_roll = 1;
    g_dates = 0;
    dos_stamp(d);
    TF_STREQ(d, "2026-11-07 00:00:00");
    TF_EQ(g_dates, 4);
}

int main(void)
{
    tf_begin("T_CRUN");
    test_midnight();
    test_stamp();
    return tf_done();
}

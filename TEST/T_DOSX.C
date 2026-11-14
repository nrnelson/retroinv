/*
 * T_DOSX.C - unit test for the client's DOS services in COMPAT.H: the
 * machine name, BREAK handling, keys, the tick counter and the stack
 * check.
 *
 * usage: T_DOSX [drive] [BREAK]
 *
 * BREAK, the last argument, says that BREAK is on when the program
 * starts, as the MS-DOS 3.10 runs set it (T_DOSX_DOSPRE of SRCS.MK);
 * drive, which those runs pass to every test, is not used.
 *
 * Checks:
 * - cstk_unused reports CSTK_UNKNOWN before cstk_fill (and always on the
 *   host).  Under DOS, after the fill, at least STK_MIN bytes must be
 *   unused (every build links a stack of 8 KB or more, so a lower bound
 *   set too high shows), and a call nesting that uses DEEP_BYTES of
 *   stack must lower the count by DEEP_BYTES less STK_SLACK at least:
 *   the fill starts a little below cstk_fill's own frame, so the first
 *   bytes the nesting uses were never filled;
 * - dos_version reports MS-DOS 3.10 or later, and returns 1 for it;
 * - dos_machine_name reports either no name (the host, and test machines
 *   without network software) or a name of 1 to DOS_MACHLEN characters
 *   without trailing spaces; the name is printed;
 * - BREAK, read with dos_break_get (function 33h under DOS), is on
 *   with BREAK and stays as it is through a dos_break_restore with no
 *   dos_break_off in force; under DOS, a Ctrl-C put into the BIOS
 *   keyboard buffer ahead of a line read from CON after the program's
 *   first dos_break_off makes DOS carry on with the read, which returns
 *   the line (DOSBox-X after one empty read), instead of ending the
 *   program; dos_break_off leaves BREAK off, also when called twice,
 *   and dos_break_restore brings back the setting found before, also
 *   when called twice;
 * - key_alt_letter gives the letter of every Alt-letter scan code and 0
 *   for every other scan code and for keys that are no Alt-letter;
 * - a key source hands over its keys in order and then KEY_EOS;
 *   key_ready and key_peek ask it, so they report no key until the
 *   source says its keys are typed ahead, then each waiting key, seen
 *   (and seen again) before key_read takes it, and none after the end;
 *   without a source the host has no keys, and a DOS test machine with
 *   nobody typing has none waiting;
 * - under DOS, key words put into the BIOS keyboard buffer with
 *   key_stuff are seen by key_ready and key_peek (without being taken)
 *   and then read by key_read with their KEY_ codes: ASCII keys,
 *   extended keys with AL 00h and with E0h, the character E0h itself and
 *   Ctrl-Break's 0000h; a key source hides them, its own waiting key
 *   answering key_peek instead; and the buffer takes
 *   KEYBUF keys or more (a BIOS may move it to a larger ring) and gives
 *   them back in order; while key_read waits, the idle hook (key_idle of
 *   CMOUSE.H) is called over and over, here until it stuffs a key.  The
 *   host has no keyboard buffer;
 * - the tick counter stays below TMR_DAY, tmr_since counts across
 *   midnight, and tmr_delay waits (under DOS) at least its ticks.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "TESTFW.H"

static void test_version(void)
{
    u8 major;
    u8 minor;

    TF_EQ(dos_version(&major, &minor), 1);
    TF_CHECK(major > 3 || (major == 3 && minor >= 10));
}

static void test_machine(void)
{
    char name[DOS_MACHLEN + 1];
    int rc;
    size_t n;

    memset(name, 'x', sizeof name);
    rc = dos_machine_name(name);
    TF_CHECK(rc == 0 || rc == 1);
    n = strlen(name);
    if (rc == 1) {
        TF_EQ(n, 0);
        printf("machine name: none\n");
    } else {
        TF_CHECK(n >= 1 && n <= DOS_MACHLEN);
        TF_CHECK(n >= 1 && name[n - 1] != ' ');
        printf("machine name: \"%s\"\n", name);
    }
#ifdef COMPAT_HOST
    TF_EQ(rc, 1);
#endif
}

/* Under DOS, Ctrl-C (BIOS word 2E03h), then x and Enter, read from CON
   while dos_break_off is in force: DOS sees the Ctrl-C in its line
   input, its INT 23h returns, and MS-DOS makes the read again, which
   returns the line; DOSBox-X ends that read with no bytes instead, so
   one empty read is let pass. */
static void break_con(void)
{
#ifndef COMPAT_HOST
    FILE *f;
    int c;

    dos_break_off();
    TF_EQ(key_stuff(0x2E03), 0);
    TF_EQ(key_stuff(0x2D78), 0);
    TF_EQ(key_stuff(0x1C0D), 0);
    f = fopen("CON", "r");
    if (TF_CHECK(f != NULL)) {
        c = fgetc(f);
        if (c == EOF) {
            clearerr(f);
            c = fgetc(f);
        }
        TF_EQ(c, 'x');
        TF_EQ(fgetc(f), '\n');
        fclose(f);
    }
    dos_break_restore();
#endif
}

/* BREAK handling, brk being 1 when BREAK is on at the start.  The
   restore with nothing in force comes first, and break_con makes the
   program's first dos_break_off, so that each proves its own case. */
static void test_break(int brk)
{
    int before;

    before = dos_break_get();
    TF_CHECK(before == 0 || before == 1);
    printf("BREAK was %s\n", before ? "ON" : "OFF");
    if (brk)
        TF_EQ(before, 1);
    dos_break_restore();
    TF_EQ(dos_break_get(), before);
    break_con();
    TF_EQ(dos_break_get(), before);
    dos_break_off();
    TF_EQ(dos_break_get(), 0);
    dos_break_off();
    TF_EQ(dos_break_get(), 0);
    dos_break_restore();
    TF_EQ(dos_break_get(), before);
    dos_break_restore();
    TF_EQ(dos_break_get(), before);
#ifdef COMPAT_HOST
    TF_EQ(before, 0);
#endif
}

/* The scan codes of the letter keys, in alphabetical order. */
static const u8 alt_scan[26] = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25,
    0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F,
    0x11, 0x2D, 0x15, 0x2C
};

static void test_alt(void)
{
    u16 scan;
    int i;
    int found;
    char want;

    for (scan = 0; scan < 0x100; scan++) {
        want = 0;
        found = 0;
        for (i = 0; i < (int)sizeof alt_scan; i++) {
            if (alt_scan[i] == scan) {
                want = (char)('A' + i);
                found++;
            }
        }
        TF_CHECK(found <= 1);
        TF_EQ(key_alt_letter((u16)(KEY_EXT + scan)), want);
    }
    TF_EQ(key_alt_letter('A'), 0);
    TF_EQ(key_alt_letter('a'), 0);
    TF_EQ(key_alt_letter(0x10), 0);
    TF_EQ(key_alt_letter(KEY_NONE), 0);
    TF_EQ(key_alt_letter(KEY_EOS), 0);
    TF_EQ(key_alt_letter(KEY_F1), 0);
}

static const u16 script[] = {
    'A', KEY_ENTER, KEY_F10, KEY_BREAK, KEY_STAB, 0x00E0, KEY_CPGUP
};
#define NSCRIPT         ((int)(sizeof script / sizeof script[0]))

#define SRC_AHEAD       3       /* the script's keys from this one on are
                                   typed ahead                          */

struct src_state {
    u16 next;
    u16 calls;
    u16 peeks;
};

static u16 src_fn(void *arg, int peek)
{
    struct src_state *st;

    st = (struct src_state *)arg;
    if (peek) {
        st->peeks++;
        if (st->next < SRC_AHEAD || st->next >= NSCRIPT)
            return KEY_NONE;
        return script[st->next];
    }
    st->calls++;
    if (st->next >= NSCRIPT)
        return KEY_EOS;
    return script[st->next++];
}

static void test_source(void)
{
    struct src_state st;
    int i;

    st.next = 0;
    st.calls = 0;
    st.peeks = 0;
    key_set_source(src_fn, &st);
    for (i = 0; i < NSCRIPT; i++) {
        if (i < SRC_AHEAD) {
            TF_EQ(key_ready(), 0);
            TF_EQ(key_peek(), KEY_NONE);
        } else {
            TF_EQ(key_ready(), 1);
            TF_EQ(key_peek(), script[i]);
            TF_EQ(key_peek(), script[i]);
        }
        TF_EQ(key_read(), script[i]);
    }
    TF_EQ(st.peeks, 2 * SRC_AHEAD + 3 * (NSCRIPT - SRC_AHEAD));
    TF_EQ(key_read(), KEY_EOS);
    TF_EQ(key_read(), KEY_EOS);
    TF_EQ(st.calls, NSCRIPT + 2);
    TF_EQ(key_peek(), KEY_NONE);
    TF_EQ(key_ready(), 0);
    key_set_source(NULL, NULL);
    TF_EQ(st.calls, NSCRIPT + 2);
    TF_EQ(st.peeks, 2 * SRC_AHEAD + 3 * (NSCRIPT - SRC_AHEAD) + 2);

    /* The keyboard: nobody types during a test run. */
    TF_EQ(key_ready(), 0);
    TF_EQ(key_peek(), KEY_NONE);
#ifdef COMPAT_HOST
    TF_EQ(key_read(), KEY_EOS);
#endif
}

static void test_timer(void)
{
    u32 t;
    u32 d;
    u32 u;
    int i;
    int ok;

    ok = 1;
    for (i = 0; i < 100; i++) {
        if (tmr_ticks() >= TMR_DAY)
            ok = 0;
    }
    TF_CHECK(ok);
    t = tmr_ticks();
    d = tmr_since(t);
    TF_CHECK(d < 10);
    /* A start after now counts as the day before. */
    u = (u32)((t + 1000UL) % TMR_DAY);
    d = tmr_since(u);
    if (u > t)
        TF_CHECK(d >= TMR_DAY - 1000UL && d < TMR_DAY);
    t = tmr_ticks();
    tmr_delay(3);
    d = tmr_since(t);
#ifdef COMPAT_HOST
    TF_CHECK(d < 10);
#else
    TF_CHECK(d >= 3 && d < 3 * TMR_SEC);
#endif
    tmr_delay(0);
}

#define DEEP_BYTES      1024
#define DEEP_LEVELS     8
#define STK_SLACK       128
#define STK_MIN         6144

/* Use DEEP_BYTES / DEEP_LEVELS bytes of stack per level. */
static u16 deep(int level)
{
    volatile u8 pad[DEEP_BYTES / DEEP_LEVELS];
    int i;
    u16 sum;

    for (i = 0; i < (int)sizeof pad; i++)
        pad[i] = (u8)i;
    sum = pad[level];
    if (level + 1 < DEEP_LEVELS)
        sum += deep(level + 1);
    return sum;
}

static void test_stack(void)
{
    u16 before;
    u16 after;

    TF_EQ(cstk_unused(), CSTK_UNKNOWN);
    cstk_fill();
    before = cstk_unused();
    TF_CHECK(deep(0) > 0);
    after = cstk_unused();
    printf("stack unused: %u, %u after %u bytes\n", (unsigned)before,
           (unsigned)after, (unsigned)DEEP_BYTES);
#ifdef COMPAT_HOST
    TF_EQ(before, CSTK_UNKNOWN);
    TF_EQ(after, CSTK_UNKNOWN);
#else
    TF_CHECK(before != CSTK_UNKNOWN && before >= STK_MIN);
    TF_CHECK(after != CSTK_UNKNOWN && after <= before &&
             before - after >= DEEP_BYTES - STK_SLACK);
#endif
}

/* A BIOS key word and the KEY_ code key_read must make of it. */
static const u16 stuffed[][2] = {
    { 0x1E61, 'a' },
    { 0x1C0D, KEY_ENTER },
    { 0x011B, KEY_ESC },
    { 0x4800, KEY_UP },
    { 0x48E0, KEY_UP },
    { 0x53E0, KEY_DEL },
    { 0x00E0, 0x00E0 },
    { 0x0000, KEY_BREAK },
    { 0x0F00, KEY_STAB },
    { 0x8400, KEY_CPGUP },
    { 0x1E00, KEY_EXT + 0x1E },
    { 0x4400, KEY_F10 }
};
#define NSTUFFED        ((int)(sizeof stuffed / sizeof stuffed[0]))
#define KEYBUF          15      /* keys the standard BIOS buffer holds  */
#define KEYMAX          64      /* keys stuffed at most                 */

/* The key at arg, typed ahead; once read, the source has ended. */
static u16 one_key(void *arg, int peek)
{
    u16 *k;
    u16 v;

    k = (u16 *)arg;
    v = *k;
    if (!peek)
        *k = KEY_EOS;
    return peek && v == KEY_EOS ? KEY_NONE : v;
}

#ifndef COMPAT_HOST
static u16 g_idles;             /* calls of idle_stuff                  */

/* key_idle: on its third call, put a key in the buffer. */
static void idle_stuff(void)
{
    g_idles++;
    if (g_idles == 3)
        key_stuff(0x2D78);
}
#endif

static void test_stuff(void)
{
    int i;
    int n;
    u16 k;
    int ok;
    int rc;

    /* The host has no keyboard buffer. */
    rc = key_stuff(stuffed[0][0]);
#ifdef COMPAT_HOST
    TF_EQ(rc, 1);
    TF_EQ(key_ready(), 0);
    return;
#else
    TF_EQ(rc, 0);
#endif
    for (i = 0; i < NSTUFFED; i++) {
        if (i > 0)
            TF_EQ(key_stuff(stuffed[i][0]), 0);
        TF_EQ(key_ready(), 1);
        TF_EQ(key_peek(), stuffed[i][1]);
        TF_EQ(key_peek(), stuffed[i][1]);
        k = key_read();
        TF_EQ(k, stuffed[i][1]);
        /* The BIOS word of Alt-A: its scan code over AL 00h. */
        if (stuffed[i][0] == (u16)(alt_scan[0] << 8))
            TF_EQ(key_alt_letter(k), 'A');
        TF_EQ(key_ready(), 0);
        TF_EQ(key_peek(), KEY_NONE);
    }

    /* A key source hides the keyboard, and the key stays waiting. */
    TF_EQ(key_stuff(0x2D78), 0);
    k = KEY_F5;
    key_set_source(one_key, &k);
    TF_EQ(key_ready(), 1);
    TF_EQ(key_peek(), KEY_F5);
    TF_EQ(key_read(), KEY_F5);
    TF_EQ(key_ready(), 0);
    TF_EQ(key_peek(), KEY_NONE);
    TF_EQ(key_read(), KEY_EOS);
    key_set_source(NULL, NULL);
    TF_EQ(key_ready(), 1);
    TF_EQ(key_read(), 'x');

    /* Fill the buffer, then read the keys back in order. */
    n = 0;
    while (n < KEYMAX && key_stuff((u16)(0x1E41 + n)) == 0)
        n++;
    TF_CHECK(n >= KEYBUF && n < KEYMAX);
    ok = 1;
    for (i = 0; i < n; i++) {
        if (key_read() != (u16)(0x41 + i))
            ok = 0;
    }
    TF_CHECK(ok);
    TF_EQ(key_ready(), 0);
#ifndef COMPAT_HOST
    key_idle = idle_stuff;
    TF_EQ(key_read(), 'x');
    key_idle = NULL;
    TF_EQ(g_idles, 3);
#endif
}

int main(int argc, char **argv)
{
    tf_begin("T_DOSX");
    test_stack();
    test_version();
    test_machine();
    test_break(argc > 1 && strcmp(argv[argc - 1], "BREAK") == 0);
    test_alt();
    test_source();
    test_stuff();
    test_timer();
    return tf_done();
}

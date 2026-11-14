/*
 * T_INTL.C - unit test of INTL.C, dates and numbers in the forms of
 * WIN.INI's [intl] section (INTL.H).
 *
 * usage: T_INTL
 *
 * Checks:
 * - intl_set keeps a supported short date picture and leaves "" for
 *   any other, without writing past the picture's room however long the
 *   one given; takes the first character of sDecimal and sThousand, an
 *   empty sDecimal as '.' and an empty sThousand as none; sets nolz for
 *   iLZero 0 only;
 * - intl_date shows each of d, dd, M, MM, yy and yyyy, leading zeros
 *   only for dd and MM, in the orders MDY, DMY and YMD, with the
 *   separators between the fields copied, one character or more, blanks
 *   included; a picture whose longest date (two digits for d and M) is
 *   MSG_DATEW characters is taken, one longer falls back to ISO even
 *   when the date at hand would fit, as do the names MMM, MMMM, ddd
 *   and dddd, y, yyy and five y, a field twice (also in a picture
 *   short enough) or missing, another letter (upper-case D and Y,
 *   lower-case m included) and the empty picture;
 * - intl_date returns 1 for a wire date and 0 with out "" for anything
 *   but MSG_DATELEN digits, whatever the picture, and never writes past
 *   MSG_DATEW + 1 bytes;
 * - intl_num groups the integer digits by three from the right with
 *   the thousands separator (none when it is '\0'), after a '-' and
 *   never before the first digit; shows the decimal separator for the
 *   point; with nolz drops an integer part "0" before the separator but
 *   not a "0" alone, without it puts "0" before a separator that has no
 *   integer part; drops leading blanks; copies any other form as it is,
 *   leading blanks dropped; cuts its result to n characters and writes
 *   n + 1 bytes at most;
 * - a zeroed intl_fmt shows ISO dates and the wire's numbers;
 * - Windows 3.0's US defaults (M/d/yy, '.', ',', iLZero 0);
 * - INTL_NUMW is the length of the widest number of w characters with
 *   dec decimals, and of no number of that width longer.
 *
 * Prints each failure and a summary line; exit status 0 when every
 * check passes, 1 when one fails.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "INTL.H"
#include "TESTFW.H"

#define SENT            '#'     /* sentinel filling every output buffer */
#define BUFSZ           40      /* size of g_buf                        */

/* A wire date: 5 March 2026, and the last day of 2026. */
static const char k_mar5[] = "20260305";
static const char k_dec31[] = "20261231";

static char g_buf[BUFSZ];

/* f made by intl_set from pic, the separators of Windows 3.0's US
   defaults and iLZero 1. */
static void set_pic(intl_fmt *f, const char *pic)
{
    intl_set(f, pic, ".", ",", 1);
}

/* 1 when intl_date(f, wire) returns ret and writes want, its NUL and
   nothing past MSG_DATEW + 1 bytes. */
static int date_is(const intl_fmt *f, const char *wire, int ret,
                   const char *want)
{
    int got;

    memset(g_buf, SENT, sizeof(g_buf));
    got = intl_date(f, g_buf, wire);
    if (got != ret || memchr(g_buf, '\0', MSG_DATEW + 1) == NULL ||
        strcmp(g_buf, want) != 0 || g_buf[MSG_DATEW + 1] != SENT) {
        g_buf[BUFSZ - 1] = '\0';
        printf("  intl_date \"%s\" \"%s\": got %d \"%s\"\n", f->date, wire,
               got, g_buf);
        return 0;
    }
    return 1;
}

/* 1 when the picture pic is supported and shows k_mar5 as want. */
static int pic_is(const char *pic, const char *want)
{
    intl_fmt f;

    set_pic(&f, pic);
    if (strcmp(f.date, pic) != 0) {
        printf("  intl_set \"%s\": got \"%s\"\n", pic, f.date);
        return 0;
    }
    return date_is(&f, k_mar5, 1, want);
}

/* 1 when the picture pic is not supported: kept as "", k_mar5 ISO. */
static int pic_iso(const char *pic)
{
    intl_fmt f;

    set_pic(&f, pic);
    if (f.date[0] != '\0') {
        printf("  intl_set \"%s\": got \"%s\"\n", pic, f.date);
        return 0;
    }
    return date_is(&f, k_mar5, 1, "2026-03-05");
}

/* 1 when intl_num(f, v, n) writes want and nothing past n + 1 bytes. */
static int num_is(const intl_fmt *f, const char *v, u16 n, const char *want)
{
    memset(g_buf, SENT, sizeof(g_buf));
    intl_num(f, g_buf, v, n);
    if (memchr(g_buf, '\0', n + 1) == NULL || strcmp(g_buf, want) != 0 ||
        g_buf[n + 1] != SENT) {
        g_buf[BUFSZ - 1] = '\0';
        printf("  intl_num \"%s\" %u: got \"%s\"\n", v, (unsigned)n, g_buf);
        return 0;
    }
    return 1;
}

static void test_set(void)
{
    static struct {
        intl_fmt f;
        char after[8];
    } g;
    intl_fmt f;

    intl_set(&f, "d.M.yyyy", ",", ".", 0);
    TF_STREQ(f.date, "d.M.yyyy");
    TF_EQ(f.dec, ',');
    TF_EQ(f.thou, '.');
    TF_EQ(f.nolz, 1);
    /* only the first character of each separator */
    intl_set(&f, "yy/MM/dd", ",.", "'x", 1);
    TF_STREQ(f.date, "yy/MM/dd");
    TF_EQ(f.dec, ',');
    TF_EQ(f.thou, '\'');
    TF_EQ(f.nolz, 0);
    intl_set(&f, "M/d/yy", "", "", 2);
    TF_EQ(f.dec, '.');
    TF_EQ(f.thou, '\0');
    TF_EQ(f.nolz, 0);
    /* a picture longer than its room: "", the fields after it intact */
    memset(&g, SENT, sizeof(g));
    intl_set(&g.f, "dddd, MMMM dd, yyyy (dddd, MMMM dd, yyyy)", ",", ".",
             0);
    TF_STREQ(g.f.date, "");
    TF_EQ(g.f.dec, ',');
    TF_EQ(g.f.thou, '.');
    TF_EQ(g.f.nolz, 1);
    TF_CHECK(g.after[0] == SENT);
    TF_CHECK(g.after[sizeof(g.after) - 1] == SENT);
}

static void test_pictures(void)
{
    intl_fmt f;

    /* MDY, DMY and YMD, each field with and without its leading zero */
    TF_CHECK(pic_is("M/d/yy", "3/5/26"));
    TF_CHECK(pic_is("MM/dd/yy", "03/05/26"));
    TF_CHECK(pic_is("MM/dd/yyyy", "03/05/2026"));
    TF_CHECK(pic_is("d.M.yyyy", "5.3.2026"));
    TF_CHECK(pic_is("dd.MM.yyyy", "05.03.2026"));
    TF_CHECK(pic_is("dd-MM-yy", "05-03-26"));
    TF_CHECK(pic_is("d/M/yy", "5/3/26"));
    TF_CHECK(pic_is("yy/M/d", "26/3/5"));
    TF_CHECK(pic_is("yyyy-MM-dd", "2026-03-05"));
    TF_CHECK(pic_is("yyyy.M.d", "2026.3.5"));
    /* separators of more than one character, and digits and blanks */
    TF_CHECK(pic_is("d. M.yy", "5. 3.26"));
    TF_CHECK(pic_is("d M yy", "5 3 26"));
    TF_CHECK(pic_is("d//M//yy", "5//3//26"));
    /* two-digit fields already two digits */
    set_pic(&f, "d.M.yy");
    TF_CHECK(date_is(&f, k_dec31, 1, "31.12.26"));
    set_pic(&f, "M/d/yyyy");
    TF_CHECK(date_is(&f, k_dec31, 1, "12/31/2026"));
    set_pic(&f, "dd.MM.yyyy");
    TF_CHECK(date_is(&f, "19990101", 1, "01.01.1999"));
    set_pic(&f, "M/d/yy");
    TF_CHECK(date_is(&f, "20000909", 1, "9/9/00"));
    /* the longest date exactly MSG_DATEW characters, and one past it */
    TF_EQ(MSG_DATEW, 10);
    TF_CHECK(pic_is("dd//MM//yy", "05//03//26"));
    TF_CHECK(pic_is("dd/MM/yyyy", "05/03/2026"));
    TF_CHECK(pic_iso("dd//MM/yyyy"));
    TF_CHECK(pic_iso("d. M. yyyy"));
    /* the longest, not this date's, decides: 5//3//2026 would fit */
    TF_CHECK(pic_iso("d//M//yyyy"));
}

static void test_fallback(void)
{
    TF_CHECK(pic_iso(""));
    TF_CHECK(pic_iso("MMM d, yyyy"));
    TF_CHECK(pic_iso("MMMM d yyyy"));
    TF_CHECK(pic_iso("ddd M/d/yy"));
    TF_CHECK(pic_iso("dddd d.M.yy"));
    TF_CHECK(pic_iso("d.M.y"));
    TF_CHECK(pic_iso("d.M.yyy"));
    TF_CHECK(pic_iso("d.M.yyyyy"));
    TF_CHECK(pic_iso("d.MMM.yy"));
    /* a field twice, or one missing */
    TF_CHECK(pic_iso("d/M/d"));
    TF_CHECK(pic_iso("d.M.yyd"));
    TF_CHECK(pic_iso("d/M/yy/yy"));
    TF_CHECK(pic_iso("M/d"));
    TF_CHECK(pic_iso("yyyy"));
    /* other letters, the case of the fields' own included */
    TF_CHECK(pic_iso("D/M/yy"));
    TF_CHECK(pic_iso("d/M/YY"));
    TF_CHECK(pic_iso("m/d/yy"));
    TF_CHECK(pic_iso("d/M/yy g"));
    TF_CHECK(pic_iso("d x M/yy"));
}

static void test_wire(void)
{
    intl_fmt f;
    intl_fmt z;

    memset(&z, 0, sizeof(z));
    set_pic(&f, "d.M.yyyy");
    TF_CHECK(date_is(&f, "", 0, ""));
    TF_CHECK(date_is(&f, "2026030", 0, ""));
    TF_CHECK(date_is(&f, "202603051", 0, ""));
    TF_CHECK(date_is(&f, "2026-03-05", 0, ""));
    TF_CHECK(date_is(&f, "2026O305", 0, ""));
    TF_CHECK(date_is(&f, " 20260305", 0, ""));
    TF_CHECK(date_is(&f, "20260305 ", 0, ""));
    TF_CHECK(date_is(&z, "2026030", 0, ""));
    TF_CHECK(date_is(&z, "", 0, ""));
}

static void test_num(void)
{
    intl_fmt f;

    /* sDecimal ",", sThousand ".", iLZero 0 */
    intl_set(&f, "d.M.yyyy", ",", ".", 0);
    TF_CHECK(num_is(&f, "1", 20, "1"));
    TF_CHECK(num_is(&f, "123", 20, "123"));
    TF_CHECK(num_is(&f, "1234", 20, "1.234"));
    TF_CHECK(num_is(&f, "12345", 20, "12.345"));
    TF_CHECK(num_is(&f, "123456", 20, "123.456"));
    TF_CHECK(num_is(&f, "1234567", 20, "1.234.567"));
    TF_CHECK(num_is(&f, "12345.67", 20, "12.345,67"));
    TF_CHECK(num_is(&f, "999.99", 20, "999,99"));
    TF_CHECK(num_is(&f, "1000.5", 20, "1.000,5"));
    TF_CHECK(num_is(&f, "-5", 20, "-5"));
    TF_CHECK(num_is(&f, "-123", 20, "-123"));
    TF_CHECK(num_is(&f, "-1234.50", 20, "-1.234,50"));
    TF_CHECK(num_is(&f, "-123456", 20, "-123.456"));
    /* nolz: the integer part "0" dropped before the separator only */
    TF_CHECK(num_is(&f, "0.70", 20, ",70"));
    TF_CHECK(num_is(&f, ".7", 20, ",7"));
    TF_CHECK(num_is(&f, "-0.5", 20, "-,5"));
    TF_CHECK(num_is(&f, "0", 20, "0"));
    TF_CHECK(num_is(&f, "10.5", 20, "10,5"));
    TF_CHECK(num_is(&f, "100", 20, "100"));
    /* leading blanks dropped, a numeric field's padding */
    TF_CHECK(num_is(&f, "    1234", 20, "1.234"));
    TF_CHECK(num_is(&f, "  -12.50", 20, "-12,50"));
    TF_CHECK(num_is(&f, "    0.25", 20, ",25"));
    /* any other form copied, leading blanks dropped */
    TF_CHECK(num_is(&f, "", 20, ""));
    TF_CHECK(num_is(&f, "    ", 20, ""));
    TF_CHECK(num_is(&f, "12a4", 20, "12a4"));
    TF_CHECK(num_is(&f, "  N/A", 20, "N/A"));
    TF_CHECK(num_is(&f, "1,234", 20, "1,234"));
    TF_CHECK(num_is(&f, "+5000", 20, "+5000"));
    TF_CHECK(num_is(&f, "1.2.3", 20, "1.2.3"));
    TF_CHECK(num_is(&f, "--12", 20, "--12"));
    TF_CHECK(num_is(&f, "1 234", 20, "1 234"));
    /* cut to n characters */
    TF_CHECK(num_is(&f, "12345.67", 9, "12.345,67"));
    TF_CHECK(num_is(&f, "12345.67", 8, "12.345,6"));
    TF_CHECK(num_is(&f, "12345.67", 1, "1"));
    TF_CHECK(num_is(&f, "12345.67", 0, ""));
    TF_CHECK(num_is(&f, "  abcdef", 3, "abc"));

    /* iLZero 1: a missing integer part becomes "0" */
    intl_set(&f, "", ",", ".", 1);
    TF_CHECK(num_is(&f, "0.70", 20, "0,70"));
    TF_CHECK(num_is(&f, ".7", 20, "0,7"));
    TF_CHECK(num_is(&f, "-.5", 20, "-0,5"));
    TF_CHECK(num_is(&f, "-0.5", 20, "-0,5"));
    TF_CHECK(num_is(&f, "1234.5", 20, "1.234,5"));

    /* an empty sThousand: no grouping; an empty sDecimal: '.' */
    intl_set(&f, "", ",", "", 1);
    TF_CHECK(num_is(&f, "1234567.5", 20, "1234567,5"));
    TF_CHECK(num_is(&f, "-1234", 20, "-1234"));
    intl_set(&f, "", "", "'", 1);
    TF_CHECK(num_is(&f, "1234567.5", 20, "1'234'567.5"));
    intl_set(&f, "", "", "", 1);
    TF_CHECK(num_is(&f, "  1234567.25", 20, "1234567.25"));
}

/* A zeroed intl_fmt: ISO dates, the wire's numbers less leading blanks. */
static void test_zero(void)
{
    intl_fmt z;

    memset(&z, 0, sizeof(z));
    TF_CHECK(date_is(&z, k_mar5, 1, "2026-03-05"));
    TF_CHECK(date_is(&z, k_dec31, 1, "2026-12-31"));
    TF_CHECK(num_is(&z, "1234567", 20, "1234567"));
    TF_CHECK(num_is(&z, "  12345.67", 20, "12345.67"));
    TF_CHECK(num_is(&z, "-1234.50", 20, "-1234.50"));
    TF_CHECK(num_is(&z, "0.50", 20, "0.50"));
    TF_CHECK(num_is(&z, "  N/A", 20, "N/A"));
    TF_CHECK(num_is(&z, "12345.67", 5, "12345"));
}

/* Windows 3.0's defaults: sShortDate M/d/yy, sDecimal '.', sThousand
   ',', iLZero 0. */
static void test_us(void)
{
    intl_fmt f;

    intl_set(&f, "M/d/yy", ".", ",", 0);
    TF_STREQ(f.date, "M/d/yy");
    TF_CHECK(date_is(&f, "20261008", 1, "10/8/26"));
    TF_CHECK(date_is(&f, k_mar5, 1, "3/5/26"));
    TF_CHECK(num_is(&f, "1234.5", 20, "1,234.5"));
    TF_CHECK(num_is(&f, "12345.67", 20, "12,345.67"));
    TF_CHECK(num_is(&f, "0.50", 20, ".50"));
    TF_CHECK(num_is(&f, "-1000000", 20, "-1,000,000"));
}

/* The widest number of w characters with dec decimals, all nines, and
   the narrowest, a minus and nines, against INTL_NUMW; each fits a
   field of INTL_NUMW(w, dec) cells and is not cut. */
static void test_numw(void)
{
    static char v[16];
    intl_fmt f;
    u16 w;
    u16 dec;
    u16 n;

    TF_EQ(INTL_NUMW(5, 0), 6);
    TF_EQ(INTL_NUMW(8, 2), 9);
    TF_EQ(INTL_NUMW(8, 0), 10);
    TF_EQ(INTL_NUMW(3, 0), 3);
    TF_EQ(INTL_NUMW(4, 0), 5);
    TF_EQ(INTL_NUMW(7, 0), 9);
    TF_EQ(INTL_NUMW(6, 2), 6);
    TF_EQ(INTL_NUMW(7, 2), 8);
    TF_EQ(INTL_NUMW(1, 0), 1);
    intl_set(&f, "", ",", ".", 1);
    for (dec = 0; dec <= 2; dec++) {
        for (w = dec ? dec + 2 : 1; w <= 12; w++) {
            n = (u16)INTL_NUMW(w, dec);
            memset(v, '9', w);
            v[w] = '\0';
            if (dec)
                v[w - dec - 1] = '.';
            memset(g_buf, SENT, sizeof(g_buf));
            intl_num(&f, g_buf, v, n);
            TF_EQ(strlen(g_buf), n);
            TF_CHECK(g_buf[n + 1] == SENT);
            if (w - (dec ? dec + 1 : 0) < 2)
                continue;
            v[0] = '-';
            intl_num(&f, g_buf, v, n);
            TF_CHECK(strlen(g_buf) <= n);
            TF_CHECK(g_buf[strlen(g_buf) - 1] == '9');
        }
    }
}

int main(void)
{
    tf_begin("T_INTL");
    test_set();
    test_pictures();
    test_fallback();
    test_wire();
    test_num();
    test_zero();
    test_us();
    test_numw();
    return tf_done();
}

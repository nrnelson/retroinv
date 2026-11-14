/*
 * T_CTEXT.C - unit test for the text helpers of COMPAT.H (txt_*,
 * TXT_ISDIGIT, TXT_ISALPHA, TXT_UPPER) and for cf_exists.
 *
 * usage: T_CTEXT [dir]   (scratch directory, default the current one)
 *
 * Checks:
 * - txt_u32 writes the digits and a NUL for 0, 9, 10, the 16-bit
 *   boundary and 0xFFFFFFFF, returns the digit count (TXT_U32LEN for
 *   the largest value) and writes nothing past the NUL;
 * - txt_u32w writes exactly w characters, right-aligned with ' ' or '0'
 *   padding, for w equal to the digit count and larger, and no NUL;
 * - txt_num gives the value of 1..max, also of max 65535 and with
 *   leading zeros, and 0 for an empty string, a sign, a blank or another
 *   character, 0 itself and a value above max, however long;
 * - txt_hexdig holds the sixteen upper-case hex digits, and txt_hexval
 *   maps every one of the 256 byte values: 0..15 for 0-9, A-F and a-f,
 *   -1 for the rest, including the neighbours of each range and the
 *   bytes from 0x80 up;
 * - txt_ieq and txt_ieqn fold ASCII letters only, so the pairs '@' '`'
 *   and '[' '{' (0x20 apart, outside the letters) and bytes from 0x80 up
 *   are not folded; strings of different lengths differ; txt_ieqn
 *   compares at most n bytes (none when n is 0) and stops at a NUL;
 * - txt_copy copies at most size - 1 bytes and a NUL, writes nothing
 *   for size 0, copies "" for a NULL source and never pads past the NUL;
 * - txt_ucopy copies at most size - 1 bytes and a NUL, never pads past
 *   the NUL, upper-cases the ASCII letters and keeps every other byte
 *   ('@' '[' '`' '{' and bytes from 0x80 up included), also in place;
 * - txt_word_num gives the 0-based index of a word among the words of a
 *   list (split at one or more spaces, spaces at the ends ignored),
 *   matched in whole and without regard to ASCII case, or -1 for a
 *   prefix or an extension of a listed word, an absent word, an empty
 *   word and an empty or all-space list; txt_word_at copies word i into
 *   size bytes (at most size - 1 characters and a NUL, nothing for size
 *   0, "" past the last word) and writes nothing past the NUL;
 * - txt_wipe zeroes exactly n bytes;
 * - txt_dosdev knows CON, AUX, PRN, NUL, COM1..COM4, LPT1..LPT3 and
 *   CLOCK$ in either case, of the length given, and no other name:
 *   COM0, COM5, LPT4, CON1, CO, a longer one, an empty one;
 * - txt_outfile_ok takes names of 1..max characters, a directory or
 *   drive before them, a byte from 0x80 up, and refuses a longer or an
 *   empty name, a control byte, a space, DEL and each byte DOS refuses,
 *   an empty base name, two dots, a device name in any case and with
 *   any extension, the data files' extensions (cut to three characters)
 *   and those starting like a scratch file's;
 * - txt_date_ok accepts exactly eight digits naming a real date in the
 *   years 0001..9999 (Gregorian leap years: 1900 no, 2000 and 2024 yes)
 *   and refuses month 00 or 13, day 00 or past the month's end, year
 *   0000, other lengths, non-digits, the empty string and NULL;
 * - txt_nbname_ok takes 1 to NB_NAMELEN - 1 characters, '*' among them
 *   but not first, and refuses the empty name, a longer one, a space,
 *   DEL and every control character, wherever they stand; every other
 *   byte, from 0x80 up included, is taken;
 * - txt_path_ext replaces or (keep 1) only adds an extension, finds it
 *   in the last component only, takes a trailing dot for an empty
 *   extension, lower-cases the letters of ext for a lower-case path,
 *   and returns CF_EINVAL, with out[0] a NUL, when the result and its
 *   NUL do not fit in outsz, including at DOS_PATHMAX;
 * - txt_path_dir keeps a path up to its last '\', '/' or ':', gives ""
 *   for a bare name or a part that does not fit with its NUL, and writes
 *   nothing past the NUL;
 * - TXT_ISDIGIT and TXT_ISALPHA over all 256 values of a char and of a
 *   signed char;
 * - TXT_UPPER over all 256 values of a char and of a signed char:
 *   'a'..'z' become 'A'..'Z' and every other byte is unchanged;
 * - cf_exists is 1 for a file made in dir, 0 once it is removed, 0
 *   for a name in a directory that does not exist and 0 for dir itself
 *   (named without its trailing separator; skipped for a drive root);
 * - txt_hexdig is a NUL-terminated string of 16 characters.
 *
 * The scratch file is removed at the start and at the end.  Prints each
 * failure and a summary line; exit status 0 when every check passes,
 * 1 when one fails, 2 when the directory name is too long.
 */
#include <stdio.h>
#include <string.h>
#define COMPAT_FIND
#include "COMPAT.H"
#include "TESTFW.H"

/* g_buf holds one converted, copied or wiped string and the sentinels
   after it; g_long and g_out hold a path one byte longer than
   DOS_PATHMAX allows, with room for sentinels. */
#define SENT            '#'     /* sentinel filling every output buffer */
#define BUFSZ           40      /* size of g_buf                        */
#define LONGSZ          (DOS_PATHMAX + 8) /* size of g_long and g_out   */

/* The scratch file, and its name in a directory that does not exist. */
static const char k_file[] = "CTXTEST.DAT";
static const char k_nodir[] = "CTNODIR";

static const char *g_dir;
static char g_buf[BUFSZ];
static char g_long[LONGSZ];
static char g_out[LONGSZ];

/* 1 when txt_u32w(v, w, pad) writes want (w characters) and leaves the
   byte after them untouched. */
static int u32w_is(u32 v, u16 w, char pad, const char *want)
{
    memset(g_buf, SENT, sizeof(g_buf));
    txt_u32w(g_buf, v, w, pad);
    if (memcmp(g_buf, want, w) != 0 || g_buf[w] != SENT) {
        g_buf[BUFSZ - 1] = '\0';
        printf("  txt_u32w %lu %u: got \"%s\"\n", (unsigned long)v,
               (unsigned)w, g_buf);
        return 0;
    }
    return 1;
}

/* 1 when txt_u32(v) writes want, returns its length and writes nothing
   after the NUL. */
static int u32_is(u32 v, const char *want)
{
    u16 n;

    memset(g_buf, SENT, sizeof(g_buf));
    n = txt_u32(g_buf, v);
    if (n != strlen(want) || strcmp(g_buf, want) != 0 ||
        g_buf[n + 1] != SENT) {
        g_buf[BUFSZ - 1] = '\0';
        printf("  txt_u32 %lu: got %u \"%s\"\n", (unsigned long)v,
               (unsigned)n, g_buf);
        return 0;
    }
    return 1;
}

static void test_u32(void)
{
    TF_EQ(TXT_U32LEN, 10);
    TF_CHECK(u32_is(0UL, "0"));
    TF_CHECK(u32_is(1UL, "1"));
    TF_CHECK(u32_is(9UL, "9"));
    TF_CHECK(u32_is(10UL, "10"));
    TF_CHECK(u32_is(99UL, "99"));
    TF_CHECK(u32_is(100UL, "100"));
    TF_CHECK(u32_is(32767UL, "32767"));
    TF_CHECK(u32_is(32768UL, "32768"));
    TF_CHECK(u32_is(65535UL, "65535"));
    TF_CHECK(u32_is(65536UL, "65536"));
    TF_CHECK(u32_is(999999999UL, "999999999"));
    TF_CHECK(u32_is(1000000000UL, "1000000000"));
    TF_CHECK(u32_is(2147483648UL, "2147483648"));
    TF_CHECK(u32_is(0xFFFFFFFFUL, "4294967295"));
    memset(g_buf, SENT, sizeof(g_buf));
    TF_EQ(txt_u32(g_buf, 0xFFFFFFFFUL), TXT_U32LEN);
}

static void test_u32w(void)
{
    TF_CHECK(u32w_is(0UL, 1, '0', "0"));
    TF_CHECK(u32w_is(0UL, 1, ' ', "0"));
    TF_CHECK(u32w_is(0UL, 3, ' ', "  0"));
    TF_CHECK(u32w_is(0UL, 3, '0', "000"));
    TF_CHECK(u32w_is(7UL, 1, ' ', "7"));
    TF_CHECK(u32w_is(9UL, 2, '0', "09"));
    TF_CHECK(u32w_is(10UL, 2, ' ', "10"));
    TF_CHECK(u32w_is(123UL, 3, ' ', "123"));
    TF_CHECK(u32w_is(123UL, 3, '0', "123"));
    TF_CHECK(u32w_is(123UL, 8, ' ', "     123"));
    TF_CHECK(u32w_is(123UL, 8, '0', "00000123"));
    TF_CHECK(u32w_is(65536UL, 8, '0', "00065536"));
    TF_CHECK(u32w_is(0xFFFFFFFFUL, 10, ' ', "4294967295"));
    TF_CHECK(u32w_is(0xFFFFFFFFUL, 10, '0', "4294967295"));
    TF_CHECK(u32w_is(0xFFFFFFFFUL, 12, ' ', "  4294967295"));
    TF_CHECK(u32w_is(0xFFFFFFFFUL, 12, '0', "004294967295"));
}

static void test_num(void)
{
    TF_EQ(txt_num("1", 1), 1);
    TF_EQ(txt_num("42", 100), 42);
    TF_EQ(txt_num("0042", 100), 42);
    TF_EQ(txt_num("100", 100), 100);
    TF_EQ(txt_num("65535", 65535u), 65535u);
    TF_EQ(txt_num("101", 100), 0);
    TF_EQ(txt_num("65536", 65535u), 0);
    TF_EQ(txt_num("99999999999999999999", 65535u), 0);
    TF_EQ(txt_num("4294967301", 100), 0);
    TF_EQ(txt_num("0", 100), 0);
    TF_EQ(txt_num("", 100), 0);
    TF_EQ(txt_num("+5", 100), 0);
    TF_EQ(txt_num("-5", 100), 0);
    TF_EQ(txt_num(" 5", 100), 0);
    TF_EQ(txt_num("5 ", 100), 0);
    TF_EQ(txt_num("5x", 100), 0);
}

/* The value txt_hexval must give for byte b. */
static int hex_model(int b)
{
    if (b >= 0x30 && b <= 0x39)
        return b - 0x30;
    if (b >= 0x41 && b <= 0x46)
        return b - 0x41 + 10;
    if (b >= 0x61 && b <= 0x66)
        return b - 0x61 + 10;
    return -1;
}

static void test_hex(void)
{
    int i;
    int bad;

    TF_CHECK(memcmp(txt_hexdig, "0123456789ABCDEF", 16) == 0);
    TF_EQ(txt_hexdig[16], '\0');
    TF_EQ(strlen(txt_hexdig), 16);
    for (i = 0; i < 16; i++)
        TF_EQ(txt_hexval(txt_hexdig[i]), i);
    TF_EQ(txt_hexval('0'), 0);
    TF_EQ(txt_hexval('9'), 9);
    TF_EQ(txt_hexval('A'), 10);
    TF_EQ(txt_hexval('F'), 15);
    TF_EQ(txt_hexval('a'), 10);
    TF_EQ(txt_hexval('f'), 15);
    TF_EQ(txt_hexval('/'), -1);
    TF_EQ(txt_hexval(':'), -1);
    TF_EQ(txt_hexval('@'), -1);
    TF_EQ(txt_hexval('G'), -1);
    TF_EQ(txt_hexval('`'), -1);
    TF_EQ(txt_hexval('g'), -1);
    TF_EQ(txt_hexval('\0'), -1);
    TF_EQ(txt_hexval((char)0x80), -1);
    TF_EQ(txt_hexval((char)0xB0), -1);
    TF_EQ(txt_hexval((char)0xC1), -1);
    TF_EQ(txt_hexval((char)0xE1), -1);
    TF_EQ(txt_hexval((char)0xFF), -1);
    bad = 0;
    for (i = 0; i < 256; i++) {
        if (txt_hexval((char)i) != hex_model(i)) {
            printf("  txt_hexval 0x%02X: got %d\n", (unsigned)i,
                   txt_hexval((char)i));
            bad++;
        }
    }
    TF_EQ(bad, 0);
}

static void test_ieq(void)
{
    TF_EQ(txt_ieq("", ""), 1);
    TF_EQ(txt_ieq("ABC", "ABC"), 1);
    TF_EQ(txt_ieq("abc", "ABC"), 1);
    TF_EQ(txt_ieq("Hello", "hELLO"), 1);
    TF_EQ(txt_ieq("q", "Q"), 1);
    TF_EQ(txt_ieq("Z", "z"), 1);
    TF_EQ(txt_ieq("AZaz", "azAZ"), 1);
    TF_EQ(txt_ieq("ITEM_ID9", "item_id9"), 1);
    TF_EQ(txt_ieq("", "A"), 0);
    TF_EQ(txt_ieq("A", ""), 0);
    TF_EQ(txt_ieq("AB", "ABC"), 0);
    TF_EQ(txt_ieq("abc", "AB"), 0);
    TF_EQ(txt_ieq("ABC", "ABD"), 0);
    TF_EQ(txt_ieq("@", "`"), 0);
    TF_EQ(txt_ieq("[", "{"), 0);
    TF_EQ(txt_ieq("\\", "|"), 0);
    TF_EQ(txt_ieq("^", "~"), 0);
    TF_EQ(txt_ieq("_", "\x7F"), 0);
    TF_EQ(txt_ieq("1", "\x11"), 0);
    TF_EQ(txt_ieq("X@", "x`"), 0);
    TF_EQ(txt_ieq("\xC1", "\xE1"), 0);
    TF_EQ(txt_ieq("\xE9", "\xE9"), 1);
    TF_EQ(txt_ieq("a\xE9", "A\xE9"), 1);
}

static void test_ieqn(void)
{
    TF_EQ(txt_ieqn("A", "B", 0), 1);
    TF_EQ(txt_ieqn("", "XYZ", 0), 1);
    TF_EQ(txt_ieqn("ABCD", "abcx", 3), 1);
    TF_EQ(txt_ieqn("ABCD", "abcx", 4), 0);
    TF_EQ(txt_ieqn("ABCD", "abcd", 4), 1);
    TF_EQ(txt_ieqn("AB", "ABC", 2), 1);
    TF_EQ(txt_ieqn("AB", "ABC", 3), 0);
    TF_EQ(txt_ieqn("ABC", "AB", 5), 0);
    TF_EQ(txt_ieqn("abc", "ABC", 100), 1);
    TF_EQ(txt_ieqn("x", "X", 0xFFFFu), 1);
    TF_EQ(txt_ieqn("@", "`", 1), 0);
    TF_EQ(txt_ieqn("[A", "{a", 2), 0);
    TF_EQ(txt_ieqn("Z[", "z[", 2), 1);
    TF_EQ(txt_ieqn("\xC1", "\xE1", 1), 0);
    TF_EQ(txt_ieqn("AB\0X", "ab\0Y", 4), 1);
}

/* 1 when bytes from..BUFSZ-1 of g_buf still hold the sentinel. */
static int untouched(u16 from)
{
    u16 i;

    for (i = from; i < BUFSZ; i++)
        if (g_buf[i] != SENT)
            return 0;
    return 1;
}

static void test_copy(void)
{
    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, "ABC", 0);
    TF_CHECK(untouched(0));
    txt_copy(g_buf, (const char *)0, 0);
    TF_CHECK(untouched(0));

    txt_copy(g_buf, "ABC", 1);
    TF_EQ(g_buf[0], '\0');
    TF_CHECK(untouched(1));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, "ABC", 4);
    TF_STREQ(g_buf, "ABC");
    TF_CHECK(untouched(4));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, "ABCDEF", 4);
    TF_STREQ(g_buf, "ABC");
    TF_CHECK(untouched(4));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, "AB", 10);
    TF_STREQ(g_buf, "AB");
    TF_CHECK(untouched(3));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, "", 10);
    TF_EQ(g_buf[0], '\0');
    TF_CHECK(untouched(1));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, (const char *)0, 10);
    TF_EQ(g_buf[0], '\0');
    TF_CHECK(untouched(1));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_copy(g_buf, "\xE9tag\xE8re", 8);
    TF_STREQ(g_buf, "\xE9tag\xE8re");
    TF_CHECK(untouched(8));
}

static void test_ucopy(void)
{
    memset(g_buf, SENT, sizeof(g_buf));
    txt_ucopy(g_buf, "ABC", 1);
    TF_EQ(g_buf[0], '\0');
    TF_CHECK(untouched(1));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_ucopy(g_buf, "abcdef", 4);
    TF_STREQ(g_buf, "ABC");
    TF_CHECK(untouched(4));

    memset(g_buf, SENT, sizeof(g_buf));
    txt_ucopy(g_buf, "az@[`{\xE9Z", 10);
    TF_STREQ(g_buf, "AZ@[`{\xE9Z");
    TF_CHECK(untouched(9));

    strcpy(g_buf, "in place");
    txt_ucopy(g_buf, g_buf, BUFSZ);
    TF_STREQ(g_buf, "IN PLACE");
}

/* 1 when txt_word_at(list, i) into size bytes of g_buf writes want and
   nothing after its NUL. */
static int word_at_is(const char *list, u16 i, u16 size, const char *want)
{
    memset(g_buf, SENT, sizeof(g_buf));
    txt_word_at(list, i, g_buf, size);
    if (strcmp(g_buf, want) != 0 || !untouched((u16)(strlen(want) + 1))) {
        g_buf[BUFSZ - 1] = '\0';
        printf("  txt_word_at \"%s\" %u %u: got \"%s\"\n", list,
               (unsigned)i, (unsigned)size, g_buf);
        return 0;
    }
    return 1;
}

static void test_words(void)
{
    /* First, middle, last and only word. */
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "ID"), 0);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "LOC"), 1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "MFR"), 2);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "SERIAL"), 3);
    TF_EQ(txt_word_num("EPSON", "EPSON"), 0);
    TF_EQ(txt_word_num("A B", "B"), 1);
    TF_EQ(txt_word_num("A B", "A"), 0);

    /* ASCII case only, in either string. */
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "serial"), 3);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "SeRiAl"), 3);
    TF_EQ(txt_word_num("lpt1 lpt2", "LPT2"), 1);
    TF_EQ(txt_word_num("Mfr", "mFR"), 0);
    TF_EQ(txt_word_num("A@ B", "a`"), -1);
    TF_EQ(txt_word_num("X[ Y", "x{"), -1);
    TF_EQ(txt_word_num("\xC1 B", "\xE1"), -1);
    TF_EQ(txt_word_num("\xE9T\xC9 B", "\xE9t\xC9"), 0);

    /* Whole words: a prefix or an extension does not match. */
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "SER"), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "SERIALX"), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "I"), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "IDS"), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "D"), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "ERIAL"), -1);
    TF_EQ(txt_word_num("LPT1 LPT2 LPT3 PRN", "LPT"), -1);
    TF_EQ(txt_word_num("LPT1 LPT2 LPT3 PRN", "LPT12"), -1);
    TF_EQ(txt_word_num("EPSON PLAIN", "EPSONS"), -1);
    TF_EQ(txt_word_num("EPSON PLAIN", "EPS"), -1);
    TF_EQ(txt_word_num("LPTX LPT", "LPT"), 1);
    TF_EQ(txt_word_num("LPT LPTX", "LPTX"), 1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "SIZE"), -1);

    /* A word holding a space is not one word of the list. */
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "ID LOC"), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", "LOC "), -1);
    TF_EQ(txt_word_num("ID LOC MFR SERIAL", " LOC"), -1);

    /* Repeated spaces, and spaces at the ends of the list. */
    TF_EQ(txt_word_num("  ID   LOC  MFR SERIAL   ", "ID"), 0);
    TF_EQ(txt_word_num("  ID   LOC  MFR SERIAL   ", "LOC"), 1);
    TF_EQ(txt_word_num("  ID   LOC  MFR SERIAL   ", "MFR"), 2);
    TF_EQ(txt_word_num("  ID   LOC  MFR SERIAL   ", "SERIAL"), 3);
    TF_EQ(txt_word_num(" EPSON ", "EPSON"), 0);
    TF_EQ(txt_word_num("  ID   LOC  ", "LO"), -1);

    /* Empty word, empty list, a list of spaces only. */
    TF_EQ(txt_word_num("ID LOC", ""), -1);
    TF_EQ(txt_word_num("  ID  LOC  ", ""), -1);
    TF_EQ(txt_word_num("", "ID"), -1);
    TF_EQ(txt_word_num("", ""), -1);
    TF_EQ(txt_word_num("   ", ""), -1);
    TF_EQ(txt_word_num("   ", "ID"), -1);

    /* txt_word_at: each word, with repeated and edge spaces. */
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 0, BUFSZ, "ID"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 1, BUFSZ, "LOC"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 2, BUFSZ, "MFR"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 3, BUFSZ, "SERIAL"));
    TF_CHECK(word_at_is("EPSON", 0, BUFSZ, "EPSON"));
    TF_CHECK(word_at_is("  ID   LOC  MFR SERIAL   ", 0, BUFSZ, "ID"));
    TF_CHECK(word_at_is("  ID   LOC  MFR SERIAL   ", 1, BUFSZ, "LOC"));
    TF_CHECK(word_at_is("  ID   LOC  MFR SERIAL   ", 3, BUFSZ, "SERIAL"));
    TF_CHECK(word_at_is("lpt1 Lpt2", 1, BUFSZ, "Lpt2"));

    /* Past the last word, and lists without words. */
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 4, BUFSZ, ""));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL   ", 4, BUFSZ, ""));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 0xFFFFu, BUFSZ, ""));
    TF_CHECK(word_at_is("EPSON", 1, BUFSZ, ""));
    TF_CHECK(word_at_is("", 0, BUFSZ, ""));
    TF_CHECK(word_at_is("   ", 0, BUFSZ, ""));
    TF_CHECK(word_at_is("", 0, 1, ""));

    /* Truncation: size 1 gives "", size 2 one character, the exact
       size the whole word, a larger size the word and its NUL only. */
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 3, 1, ""));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 3, 2, "S"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 3, 6, "SERIA"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 3, 7, "SERIAL"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 3, 8, "SERIAL"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 1, 3, "LO"));
    TF_CHECK(word_at_is("ID LOC MFR SERIAL", 1, 4, "LOC"));
    TF_CHECK(word_at_is("  ID   LOC  ", 0, 2, "I"));
    TF_CHECK(word_at_is("  ID   LOC  ", 0, 3, "ID"));
    TF_CHECK(word_at_is("ID LOC", 5, 1, ""));

    /* Size 0 writes nothing, for a word and past the last one. */
    memset(g_buf, SENT, sizeof(g_buf));
    txt_word_at("ID LOC MFR SERIAL", 0, g_buf, 0);
    TF_CHECK(untouched(0));
    txt_word_at("ID LOC MFR SERIAL", 9, g_buf, 0);
    TF_CHECK(untouched(0));
    txt_word_at("", 0, g_buf, 0);
    TF_CHECK(untouched(0));

    /* Every word read back by txt_word_at is found at its index. */
    TF_CHECK(word_at_is("LPT1 LPT2 LPT3 PRN", 2, BUFSZ, "LPT3"));
    TF_EQ(txt_word_num("LPT1 LPT2 LPT3 PRN", g_buf), 2);
}

static void test_wipe(void)
{
    u32 v;
    u16 i;
    int bad;

    memset(g_buf, SENT, sizeof(g_buf));
    txt_wipe(g_buf + 2, 0);
    TF_CHECK(untouched(0));
    txt_wipe(g_buf + 2, 5);
    bad = 0;
    for (i = 0; i < BUFSZ; i++)
        if (g_buf[i] != ((i >= 2 && i < 7) ? '\0' : SENT))
            bad++;
    TF_EQ(bad, 0);
    memset(g_buf, SENT, sizeof(g_buf));
    txt_wipe(g_buf, BUFSZ);
    bad = 0;
    for (i = 0; i < BUFSZ; i++)
        if (g_buf[i] != '\0')
            bad++;
    TF_EQ(bad, 0);
    v = 0xDEADBEEFUL;
    txt_wipe(&v, sizeof(v));
    TF_CHECK(v == 0UL);
}

static void test_dosdev(void)
{
    static const char *const yes[] = {
        "CON", "aux", "Prn", "NUL", "COM1", "com4", "LPT1", "LPT3",
        "CLOCK$", NULL
    };
    static const char *const no[] = {
        "COM0", "COM5", "LPT0", "LPT4", "CON1", "CO", "", "CLOCK", "NULL",
        NULL
    };
    int i;

    for (i = 0; yes[i] != NULL; i++)
        TF_EQ(txt_dosdev(yes[i], (u16)strlen(yes[i])), 1);
    for (i = 0; no[i] != NULL; i++)
        TF_EQ(txt_dosdev(no[i], (u16)strlen(no[i])), 0);
    TF_EQ(txt_dosdev("CON.TXT", 3), 1);
    TF_EQ(txt_dosdev("CLOCK$XYZ", 9), 0);
    TF_EQ(txt_dosdev("CLOCK$  ", 8), 0);
    TF_EQ(txt_dosdev("COM2", 3), 0);
}

static void test_outfile(void)
{
    static const char *const yes[] = {
        "R.TXT", "C:\\DIR.DBF\\X", "../X", "NOEXT", "X.", "X.DB",
        "CONS.TXT", "COM5", "\202.TXT", NULL
    };
    static const char *const no[] = {
        "", "ITEMS.DBF", "D\\items.dbt", "A:X.NDXX", "X.$$$", "INV.$$",
        "X.&C0", "X.%A", "X.#", "CON", "prn.txt", "A\\NUL.X", "CLOCK$",
        "X.Y.Z", ".TXT", "DIR\\", "A B", "A\tB", "A\177", "A\001",
        "A\"B", "A*", "A+B", "A,B", "A;B", "A<B", "A=B", "A>B", "A?",
        "A[B", "A]B", "A|B", NULL
    };
    int i;

    for (i = 0; yes[i] != NULL; i++)
        if (!TF_EQ(txt_outfile_ok(yes[i], 12), 1))
            printf("  \"%s\"\n", yes[i]);
    for (i = 0; no[i] != NULL; i++)
        if (!TF_EQ(txt_outfile_ok(no[i], 12), 0))
            printf("  \"%s\"\n", no[i]);
    TF_EQ(txt_outfile_ok(NULL, 12), 0);
    TF_EQ(txt_outfile_ok("ABCDEFGH.TXT", 12), 1);
    TF_EQ(txt_outfile_ok("ABCDEFGH.TXT", 11), 0);
}

static void test_date(void)
{
    TF_EQ(txt_date_ok("20240101"), 1);
    TF_EQ(txt_date_ok("20241231"), 1);
    TF_EQ(txt_date_ok("00010101"), 1);
    TF_EQ(txt_date_ok("99991231"), 1);
    TF_EQ(txt_date_ok("19991231"), 1);

    TF_EQ(txt_date_ok("19000228"), 1);
    TF_EQ(txt_date_ok("19000229"), 0);
    TF_EQ(txt_date_ok("20000229"), 1);
    TF_EQ(txt_date_ok("20000230"), 0);
    TF_EQ(txt_date_ok("20240229"), 1);
    TF_EQ(txt_date_ok("20240230"), 0);
    TF_EQ(txt_date_ok("20230228"), 1);
    TF_EQ(txt_date_ok("20230229"), 0);
    TF_EQ(txt_date_ok("21000229"), 0);
    TF_EQ(txt_date_ok("16000229"), 1);
    TF_EQ(txt_date_ok("00040229"), 1);

    TF_EQ(txt_date_ok("20240001"), 0);
    TF_EQ(txt_date_ok("20241301"), 0);
    TF_EQ(txt_date_ok("20249901"), 0);
    TF_EQ(txt_date_ok("20240100"), 0);
    TF_EQ(txt_date_ok("20240131"), 1);
    TF_EQ(txt_date_ok("20240132"), 0);
    TF_EQ(txt_date_ok("20241232"), 0);
    TF_EQ(txt_date_ok("20240331"), 1);
    TF_EQ(txt_date_ok("20240430"), 1);
    TF_EQ(txt_date_ok("20240431"), 0);
    TF_EQ(txt_date_ok("20240631"), 0);
    TF_EQ(txt_date_ok("20240731"), 1);
    TF_EQ(txt_date_ok("20240831"), 1);
    TF_EQ(txt_date_ok("20240931"), 0);
    TF_EQ(txt_date_ok("20241031"), 1);
    TF_EQ(txt_date_ok("20241131"), 0);
    TF_EQ(txt_date_ok("20241130"), 1);

    TF_EQ(txt_date_ok("00000101"), 0);
    TF_EQ(txt_date_ok("00001231"), 0);

    TF_EQ(txt_date_ok(""), 0);
    TF_EQ(txt_date_ok("2024010"), 0);
    TF_EQ(txt_date_ok("202401011"), 0);
    TF_EQ(txt_date_ok("20240101 "), 0);
    TF_EQ(txt_date_ok(" 2024010"), 0);
    TF_EQ(txt_date_ok("2024-1-1"), 0);
    TF_EQ(txt_date_ok("2024010a"), 0);
    TF_EQ(txt_date_ok("2024O101"), 0);
    TF_EQ(txt_date_ok("2024/101"), 0);
    TF_EQ(txt_date_ok("2024:101"), 0);
    TF_EQ(txt_date_ok("+0240101"), 0);
    TF_EQ(txt_date_ok("2024\xB0" "101"), 0);
    TF_EQ(txt_date_ok("        "), 0);
    TF_EQ(txt_date_ok((const char *)0), 0);
}

/* 1 when txt_path_ext(path, ext, keep) into outsz bytes returns 0 and
   writes want. */
static int ext_is(const char *path, const char *ext, int keep, u16 outsz,
                  const char *want)
{
    int rc;

    memset(g_out, SENT, sizeof(g_out));
    rc = txt_path_ext(g_out, outsz, path, ext, keep);
    if (rc != 0 || strcmp(g_out, want) != 0) {
        g_out[LONGSZ - 1] = '\0';
        printf("  txt_path_ext \"%s\" \"%s\" %d: got %d \"%s\"\n", path,
               ext, keep, rc, rc == 0 ? g_out : "");
        return 0;
    }
    return 1;
}

/* 1 when txt_path_ext(path, ext, keep) into outsz bytes returns
   CF_EINVAL and, when outsz is not 0, leaves out[0] a NUL. */
static int ext_fails(const char *path, const char *ext, int keep,
                     u16 outsz)
{
    int rc;

    memset(g_out, SENT, sizeof(g_out));
    rc = txt_path_ext(g_out, outsz, path, ext, keep);
    if (rc != CF_EINVAL || (outsz > 0 && g_out[0] != '\0')) {
        printf("  txt_path_ext \"%s\" %u: got %d, out[0] 0x%02X\n", path,
               (unsigned)outsz, rc, (unsigned)(unsigned char)g_out[0]);
        return 0;
    }
    return 1;
}

static void test_nbname(void)
{
    char n[NB_NAMELEN + 1];
    int i;
    int bad;

    TF_EQ(txt_nbname_ok("RETROINV"), 1);
    TF_EQ(txt_nbname_ok("lab-1*"), 1);
    TF_EQ(txt_nbname_ok(""), 0);
    TF_EQ(txt_nbname_ok("*"), 0);
    TF_EQ(txt_nbname_ok("*SRV"), 0);
    TF_EQ(txt_nbname_ok(" SRV"), 0);
    TF_EQ(txt_nbname_ok("SRV "), 0);
    memset(n, 'N', NB_NAMELEN);
    n[NB_NAMELEN - 1] = '\0';
    TF_EQ(txt_nbname_ok(n), 1);
    n[NB_NAMELEN - 1] = 'N';
    n[NB_NAMELEN] = '\0';
    TF_EQ(txt_nbname_ok(n), 0);
    bad = 0;
    n[0] = 'A';
    n[2] = 'B';
    n[3] = '\0';
    for (i = 1; i < 256; i++) {
        n[1] = (char)i;
        if (txt_nbname_ok(n) != (i > ' ' && i != 0x7F) ||
            txt_nbname_ok(n + 1) != (i > ' ' && i != 0x7F && i != '*')) {
            printf("  txt_nbname_ok 0x%02X wrong\n", (unsigned)i);
            bad++;
        }
    }
    TF_EQ(bad, 0);
}

static void test_path_ext(void)
{
    TF_CHECK(ext_is("FILE", "NDX", 0, BUFSZ, "FILE.NDX"));
    TF_CHECK(ext_is("FILE.DBF", "NDX", 0, BUFSZ, "FILE.NDX"));
    TF_CHECK(ext_is("FILE", "NDX", 1, BUFSZ, "FILE.NDX"));
    TF_CHECK(ext_is("FILE.DBF", "NDX", 1, BUFSZ, "FILE.DBF"));
    TF_CHECK(ext_is("A.B.C", "NDX", 0, BUFSZ, "A.B.NDX"));
    TF_CHECK(ext_is("A.B\\FILE", "NDX", 0, BUFSZ, "A.B\\FILE.NDX"));
    TF_CHECK(ext_is("A.B\\FILE", "NDX", 1, BUFSZ, "A.B\\FILE.NDX"));
    TF_CHECK(ext_is("A.B\\FILE.DBF", "BAK", 0, BUFSZ, "A.B\\FILE.BAK"));
    TF_CHECK(ext_is("A.B\\FILE.DBF", "BAK", 1, BUFSZ, "A.B\\FILE.DBF"));
    TF_CHECK(ext_is("C:\\DATA.V1\\ITEMS", "DBF", 0, BUFSZ,
                    "C:\\DATA.V1\\ITEMS.DBF"));
    TF_CHECK(ext_is("A.B/FILE", "NDX", 0, BUFSZ, "A.B/FILE.NDX"));
    TF_CHECK(ext_is("C:FILE.DBF", "&C0", 0, BUFSZ, "C:FILE.&C0"));
    TF_CHECK(ext_is("ITEMS.DBF", "$$$", 0, BUFSZ, "ITEMS.$$$"));
    TF_CHECK(ext_is("FILE.LONGER", "N", 0, BUFSZ, "FILE.N"));
    TF_CHECK(ext_is("FILE.", "NDX", 0, BUFSZ, "FILE.NDX"));
    TF_CHECK(ext_is("FILE.", "NDX", 1, BUFSZ, "FILE."));
    TF_CHECK(ext_is("A.B\\FILE.", "NDX", 0, BUFSZ, "A.B\\FILE.NDX"));

    /* Lower-case paths give lower-case letters of ext. */
    TF_CHECK(ext_is("data.dbf", "NDX", 0, BUFSZ, "data.ndx"));
    TF_CHECK(ext_is("FILE.dbf", "NDX", 0, BUFSZ, "FILE.ndx"));
    TF_CHECK(ext_is("data.DBF", "NDX", 0, BUFSZ, "data.NDX"));
    TF_CHECK(ext_is("data.dbf", "NDX", 1, BUFSZ, "data.dbf"));
    TF_CHECK(ext_is("data", "NDX", 0, BUFSZ, "data.ndx"));
    TF_CHECK(ext_is("data", "NDX", 1, BUFSZ, "data.ndx"));
    TF_CHECK(ext_is("DATa", "NDX", 0, BUFSZ, "DATa.ndx"));
    TF_CHECK(ext_is("data.dbf", "&C0", 0, BUFSZ, "data.&c0"));
    TF_CHECK(ext_is("items.dbf", "$$$", 0, BUFSZ, "items.$$$"));
    TF_CHECK(ext_is("a.b\\file", "NDX", 0, BUFSZ, "a.b\\file.ndx"));
    TF_CHECK(ext_is("DATA.DBF", "ndx", 0, BUFSZ, "DATA.ndx"));
    TF_CHECK(ext_is("c:\\data\\INV", "DBT", 0, BUFSZ,
                    "c:\\data\\INV.DBT"));
    TF_CHECK(ext_is("C:\\DATA\\inv", "DBT", 0, BUFSZ,
                    "C:\\DATA\\inv.dbt"));
    TF_CHECK(ext_is("FILE9", "NDX", 0, BUFSZ, "FILE9.NDX"));

    /* Exact fit, and one byte short. */
    TF_CHECK(ext_is("FILE", "NDX", 0, 9, "FILE.NDX"));
    TF_CHECK(ext_fails("FILE", "NDX", 0, 8));
    TF_CHECK(ext_is("FILE.DBF", "NDX", 0, 9, "FILE.NDX"));
    TF_CHECK(ext_fails("FILE.DBF", "NDX", 0, 8));
    TF_CHECK(ext_is("FILE.DBF", "NDX", 1, 9, "FILE.DBF"));
    TF_CHECK(ext_fails("FILE.DBF", "NDX", 1, 8));
    TF_CHECK(ext_is("FILE", "NDX", 1, 9, "FILE.NDX"));
    TF_CHECK(ext_fails("FILE", "NDX", 1, 8));
    TF_CHECK(ext_is("FILE.LONGER", "NDX", 0, 9, "FILE.NDX"));
    TF_CHECK(ext_is("FILE.", "NDX", 1, 6, "FILE."));
    TF_CHECK(ext_fails("FILE.", "NDX", 1, 5));
    TF_CHECK(ext_fails("FILE", "NDX", 0, 1));
    TF_CHECK(ext_fails("FILE", "NDX", 0, 0));
}

/* A path of DOS_PATHMAX - 1 characters fits a DOS_PATHMAX buffer; one
   character more does not. */
static void test_pathmax(void)
{
    u16 n;

    TF_EQ(DOS_PATHMAX, 128);
    n = (u16)(DOS_PATHMAX - 5);
    memset(g_long, 'A', n);
    g_long[n] = '\0';
    memset(g_out, SENT, sizeof(g_out));
    TF_EQ(txt_path_ext(g_out, DOS_PATHMAX, g_long, "NDX", 0), 0);
    TF_EQ(strlen(g_out), DOS_PATHMAX - 1);
    TF_STREQ(g_out + n, ".NDX");
    g_long[n] = 'A';
    g_long[n + 1] = '\0';
    TF_CHECK(ext_fails(g_long, "NDX", 0, DOS_PATHMAX));
}

/* 1 when txt_path_dir(path) into outsz bytes writes want and leaves
   the byte after its NUL untouched. */
static int dir_is(const char *path, u16 outsz, const char *want)
{
    memset(g_out, SENT, sizeof(g_out));
    txt_path_dir(g_out, outsz, path);
    if (strcmp(g_out, want) != 0 || g_out[strlen(want) + 1] != SENT) {
        g_out[LONGSZ - 1] = '\0';
        printf("  txt_path_dir \"%s\" %u: got \"%s\"\n", path,
               (unsigned)outsz, g_out);
        return 0;
    }
    return 1;
}

static void test_path_dir(void)
{
    TF_CHECK(dir_is("C:\\INV\\INVSERV.EXE", BUFSZ, "C:\\INV\\"));
    TF_CHECK(dir_is("OBJ/HOST/INV.bin", BUFSZ, "OBJ/HOST/"));
    TF_CHECK(dir_is("A:INV.EXE", BUFSZ, "A:"));
    TF_CHECK(dir_is("C:\\", BUFSZ, "C:\\"));
    TF_CHECK(dir_is("INV.EXE", BUFSZ, ""));
    TF_CHECK(dir_is("", BUFSZ, ""));
    /* The part "AB\" and its NUL take 4 bytes: 4 fits, 3 does not. */
    TF_CHECK(dir_is("AB\\C", 4, "AB\\"));
    TF_CHECK(dir_is("AB\\C", 3, ""));
    TF_CHECK(dir_is("AB\\C", 1, ""));
}

static void test_ctype(void)
{
    int i;
    int bad;
    int want;
    char c;
    signed char sc;

    bad = 0;
    for (i = 0; i < 256; i++) {
        c = (char)i;
        sc = (signed char)i;
        want = i >= 0x30 && i <= 0x39;
        if ((TXT_ISDIGIT(c) ? 1 : 0) != want ||
            (TXT_ISDIGIT(sc) ? 1 : 0) != want) {
            printf("  TXT_ISDIGIT 0x%02X wrong\n", (unsigned)i);
            bad++;
        }
        want = (i >= 0x41 && i <= 0x5A) || (i >= 0x61 && i <= 0x7A);
        if ((TXT_ISALPHA(c) ? 1 : 0) != want ||
            (TXT_ISALPHA(sc) ? 1 : 0) != want) {
            printf("  TXT_ISALPHA 0x%02X wrong\n", (unsigned)i);
            bad++;
        }
    }
    TF_EQ(bad, 0);
    TF_CHECK(TXT_ISDIGIT('0'));
    TF_CHECK(TXT_ISDIGIT('9'));
    TF_CHECK(!TXT_ISDIGIT('/'));
    TF_CHECK(!TXT_ISDIGIT(':'));
    TF_CHECK(TXT_ISALPHA('A'));
    TF_CHECK(TXT_ISALPHA('z'));
    TF_CHECK(!TXT_ISALPHA('@'));
    TF_CHECK(!TXT_ISALPHA('['));
    TF_CHECK(!TXT_ISALPHA('`'));
    TF_CHECK(!TXT_ISALPHA('{'));
    TF_CHECK(!TXT_ISALPHA((char)0xC1));
    TF_CHECK(!TXT_ISDIGIT((char)0xB0));
}

/* The value TXT_UPPER must give for byte b. */
static int upper_model(int b)
{
    return (b >= 0x61 && b <= 0x7A) ? b - 0x20 : b;
}

/* The result is compared as an unsigned char, so a high byte passes
   whether the macro yields it as 0x80..0xFF or as a negative char. */
static void test_upper(void)
{
    int i;
    int bad;
    char c;
    signed char sc;

    bad = 0;
    for (i = 0; i < 256; i++) {
        c = (char)i;
        sc = (signed char)i;
        if ((unsigned char)TXT_UPPER(c) != upper_model(i) ||
            (unsigned char)TXT_UPPER(sc) != upper_model(i)) {
            printf("  TXT_UPPER 0x%02X wrong\n", (unsigned)i);
            bad++;
        }
    }
    TF_EQ(bad, 0);
    TF_EQ(TXT_UPPER('a'), 'A');
    TF_EQ(TXT_UPPER('z'), 'Z');
    TF_EQ(TXT_UPPER('A'), 'A');
    TF_EQ(TXT_UPPER('Z'), 'Z');
    TF_EQ(TXT_UPPER('`'), '`');
    TF_EQ(TXT_UPPER('{'), '{');
    TF_EQ(TXT_UPPER('@'), '@');
    TF_EQ(TXT_UPPER('['), '[');
    TF_EQ(TXT_UPPER('0'), '0');
    TF_EQ((unsigned char)TXT_UPPER((char)0xE1), 0xE1);
    TF_EQ((unsigned char)TXT_UPPER((char)0xFF), 0xFF);
}

/* dir + separator + name into out (DOS_PATHMAX bytes); 0 if too long.
   The separator is a backslash when dir looks like a DOS path, a slash
   otherwise, and none after a trailing separator or drive colon. */
static int make_path(char *out, const char *name)
{
    size_t len;
    char last;

    len = strlen(g_dir);
    if (len + strlen(name) + 2 > DOS_PATHMAX)
        return 0;
    if (strcmp(g_dir, ".") == 0) {
        strcpy(out, name);
        return 1;
    }
    strcpy(out, g_dir);
    last = len > 0 ? g_dir[len - 1] : ':';
    if (last != '\\' && last != '/' && last != ':')
        strcat(out, (strchr(g_dir, '\\') != NULL ||
                     strchr(g_dir, ':') != NULL) ? "\\" : "/");
    strcat(out, name);
    return 1;
}

/* The scratch directory without its trailing separator into out
   (DOS_PATHMAX bytes); 0 when that leaves no directory name, as for a
   drive root such as "D:\" or for ".". */
static int dir_path(char *out)
{
    size_t len;

    len = strlen(g_dir);
    if (len + 1 > DOS_PATHMAX)
        return 0;
    strcpy(out, g_dir);
    while (len > 0 && (out[len - 1] == '\\' || out[len - 1] == '/'))
        out[--len] = '\0';
    return len > 0 && out[len - 1] != ':' && strcmp(out, ".") != 0;
}

/* A file is present until removed; a missing directory and an existing
   directory both count as absent.  The directory check is skipped when
   the scratch directory is a drive root (the DOS runs), which has no
   name of its own. */
static void test_exists(const char *file, const char *nodir)
{
    static char dir[DOS_PATHMAX];
    int h;

    cf_remove(file);
    TF_EQ(cf_exists(file), 0);
    if (TF_EQ(cf_create(file, &h), 0)) {
        TF_EQ(cf_write(h, "X", 1), 0);
        TF_EQ(cf_close(h), 0);
    }
    TF_EQ(cf_exists(file), 1);
    TF_EQ(cf_remove(file), 0);
    TF_EQ(cf_exists(file), 0);
    TF_EQ(cf_exists(nodir), 0);
    if (dir_path(dir))
        TF_EQ(cf_exists(dir), 0);
}

int main(int argc, char **argv)
{
    static char file[DOS_PATHMAX];
    static char nodir[DOS_PATHMAX];

    tf_begin("T_CTEXT");
    g_dir = argc > 1 ? argv[1] : ".";
    /* nodir gains a separator and k_file; sizeof counts its NUL. */
    if (!make_path(file, k_file) || !make_path(nodir, k_nodir) ||
        strlen(nodir) + 1 + sizeof(k_file) > DOS_PATHMAX) {
        printf("T_CTEXT: directory name too long\n");
        return 2;
    }
    strcat(nodir, (strchr(nodir, '\\') != NULL ||
                   strchr(nodir, ':') != NULL) ? "\\" : "/");
    strcat(nodir, k_file);
    dos_crit_install();

    test_u32();
    test_u32w();
    test_num();
    test_hex();
    test_ieq();
    test_ieqn();
    test_copy();
    test_ucopy();
    test_words();
    test_wipe();
    test_dosdev();
    test_outfile();
    test_date();
    test_nbname();
    test_path_ext();
    test_pathmax();
    test_path_dir();
    test_ctype();
    test_upper();
    test_exists(file, nodir);

    cf_remove(file);
    dos_crit_remove();
    return tf_done();
}

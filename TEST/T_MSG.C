/*
 * T_MSG.C - the message file loader (SRC/CLIENT/MSG.C).
 *
 * usage: T_MSG dir [msgdir]
 *
 * dir is a scratch directory prefix ("OBJ/HOST/MSGT/", "D:\") where the
 * test writes message files of its own, removed first and last; msgdir
 * is the directory prefix of the built INV.MSG (default: the program's
 * own directory).  The real file is loaded and a few shared texts
 * checked; its =STAMP line is then copied into every file the test
 * writes, so those match the program.
 *
 * Covered:
 * - a line's text as every byte after the number's one space (nothing taken
 *   for a name), a bare number or one space after it for an empty text;
 * - escapes, placeholders and one-pass substitution, a text of MSG_MAXLEN
 *   bytes kept whole and cutting at MSG_MAXLEN, the rotating buffers,
 *   unknown numbers;
 * - comments, both line ends and Ctrl-Z, every kind of malformed line, a
 *   line of MSG_MAXRAW bytes and one more;
 * - a foreign stamp (a named file of an older build among them) and the
 *   program's with a byte more on its line, a file too large for one far
 *   block, and a missing file;
 * - pages: a range read when first used, without memory, the slot used
 *   least recently replaced, a page that cannot be read tried once and then
 *   answering "?nnn" until its slot is reused;
 * - a file changed under a page (a longer text, one byte over the page, a
 *   number past the range's table, the file cut inside the range);
 * - a page of exactly MSG_PAGE bytes and one byte more, a range's lines
 *   apart, and the largest range's lines, far longer than its page, read
 *   whole;
 * - no page slots for a file without a paged range, whose texts, the
 *   server's from MSG_PAGEEND on included, stay loaded with the file gone,
 *   and a loaded range larger than MSG_PAGE;
 * - the far blocks of a load: freed last in first out, the block of the
 *   run sized to the index, the slots and read area of a file with a
 *   paged range and the loaded pages, the second read ending with the
 *   last loaded range's lines and left out when there is none;
 * - a load failing at either read or any allocation: the reason, the file
 *   closed once, no block left;
 * - a file changed between the load's two passes, where its handle reads
 *   the change: refused when its lines do not fit the pages as sized,
 *   loaded otherwise;
 * - unescaping against a reference copy of it that tests every byte, over
 *   every short text of the bytes that steer it and every short tail after
 *   texts near MSG_MAXLEN;
 * - which reason wins at MSG_MAXLEN and one byte past it, and a text of
 *   MSG_MAXLEN bytes on a line of MSG_MAXRAW;
 * - the text file reader (msg_rd_line, msg_rd_piece), on a handle and on a
 *   far block (msg_rd_blk), through every buffer size from one byte: CR LF,
 *   LF alone, a CR alone, a CR at a buffer boundary and before the end of
 *   the file, Ctrl-Z in a line, empty files, lines of exactly the limit and
 *   one more, long lines in pieces, and read errors;
 * - the reader against a reference copy of it that reads a byte at a time,
 *   call by call, over every short string of LF, CR, Ctrl-Z and a plain
 *   byte and over pseudo-random longer ones;
 * - msg_load with a Ctrl-Z at the first, second and last byte of one of its
 *   reads and just after it, every text before it loaded and none after;
 * - msg_fread inside and past the end of a file;
 * - msg_section through an index;
 * - msg_date on dates and non-dates;
 * - msg_dec at its caps.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "TUI.H"
#include "TESTFW.H"
#include "T_MSG.H"

#define PFX             2       /* "1 ", before message 1's text        */

char g_tdir[PATHMAX];
char g_stamp[STAMPLEN + 1];
/* The file text of the page and Ctrl-Z cases: nine lines of MSG_MAXRAW
   bytes. */
char g_text[9 * (MSG_MAXRAW + 1)];

int write_file(const char *name, const char *a, const char *b)
{
    char path[PATHMAX + 16];
    int h;
    int rc;

    strcpy(path, g_tdir);
    strcat(path, name);
    rc = cf_create(path, &h);
    if (rc != 0)
        return rc;
    if (a != NULL && *a != '\0')
        rc = cf_write(h, a, (u16)strlen(a));
    if (rc == 0 && b != NULL && *b != '\0')
        rc = cf_write(h, b, (u16)strlen(b));
    cf_close(h);
    return rc;
}

/* Write INV.MSG in the scratch directory: the stamp line, then text;
   load it. */
static int load_text(const char *text)
{
    TF_EQ(write_file(MSG_FILE, g_stamp, text), 0);
    return msg_load(g_tdir, MSG_FILE);
}

/* text must be refused, the reason starting with where. */
static void bad(const char *text, const char *where)
{
    TF_EQ(load_text(text), -1);
    if (!TF_CHECK(tf_starts(msg_why(), where)))
        printf("  why: %s (want %s)\n", msg_why(), where);
    TF_STREQ(msg(1), "?1");
}

static void test_real_file(const char *mdir)
{
    char path[PATHMAX + 16];
    u16 got;
    int h;

    got = 0;
    TF_EQ(msg_load(mdir, MSG_FILE), 0);
    TF_STREQ(msg_why(), "");
    TF_STREQ(msg(MT_NOTAVAIL), "Not available in local mode");
    TF_STREQ(msg_sub(MT_PERM, "2", NULL),
             "Permission denied (your access level is 2)");
    TF_STREQ(msg_sub(MT_DBERR, "300", "disk on fire"),
             "Database error 300: disk on fire");
    TF_STREQ(msg(MT_YESNO), "YN");
    strcpy(path, mdir);
    strcat(path, MSG_FILE);
    g_stamp[0] = '\0';
    if (TF_EQ(cf_open(path, CF_READ, &h), 0)) {
        TF_EQ(cf_read(h, g_stamp, STAMPLEN, &got), 0);
        cf_close(h);
    }
    TF_EQ(got, STAMPLEN);
    g_stamp[got < STAMPLEN ? 0 : STAMPLEN] = '\0';
    TF_CHECK(strncmp(g_stamp, "=STAMP ", 7) == 0 &&
             g_stamp[STAMPLEN - 2] == '\r');
}

static void test_texts(void)
{
    char buf[8];
    const char *a;
    const char *b;
    const char *c;

    /* 1199 below is the highest number, MSG_MAXNUM - 1; the refused
       lines further on use MSG_MAXNUM itself. */
    TF_EQ(MSG_MAXNUM, 1200);
    TF_EQ(load_text("# comment\r\n\r\n1 one\r\n"
                    "2 A\\x41b\\\\c\\nd\\xC4e\n"
                    "3 [^1|^2|^^|^^1]\n"
                    "4\n"
                    "5  two  spaces \n"
                    "6 M_A x\n"
                    "7 Yes\r\n"
                    "8 \r\n"
                    "9 123\n"
                    "10 ^1 and ^2\n"
                    "11\r\n"
                    "999 last\n"
                    "1199 top\n"
                    "1198 no line end"), 0);
    TF_STREQ(msg(1), "one");
    TF_STREQ(msg(2), "AAb\\c\nd\xC4" "e");
    TF_STREQ(msg(3), "[||^|^1]");
    TF_STREQ(msg_sub(3, "x", "y"), "[x|y|^|^1]");
    TF_STREQ(msg_sub(3, "^2", "^1"), "[^2|^1|^|^1]");
    TF_STREQ(msg_sub(3, NULL, "y"), "[|y|^|^1]");
    /* After the number, the end of the line or one space, and every
       byte after that space is text: nothing is taken for a name. */
    TF_STREQ(msg(4), "");
    TF_STREQ(msg(5), " two  spaces ");
    TF_STREQ(msg(6), "M_A x");
    TF_STREQ(msg(7), "Yes");
    TF_STREQ(msg(8), "");
    TF_STREQ(msg(9), "123");
    TF_STREQ(msg_sub(10, "a", "b"), "a and b");
    TF_STREQ(msg(11), "");
    TF_STREQ(msg(999), "last");
    TF_STREQ(msg(1199), "top");
    TF_STREQ(msg(1198), "no line end");
    TF_STREQ(msg(12), "?12");
    TF_STREQ(msg(0), "?0");
    TF_STREQ(msg(1000), "?1000");
    TF_STREQ(msg(42), "?42");

    /* Up to MSG_NBUF results stay valid, and results may be arguments. */
    a = msg(1);
    b = msg(999);
    c = msg_sub(3, a, b);
    TF_STREQ(a, "one");
    TF_STREQ(b, "last");
    TF_STREQ(c, "[one|last|^|^1]");
    TF_STREQ(msg_sub(3, msg(1), msg(999)), "[one|last|^|^1]");

    TF_EQ(msg_get(1, buf, sizeof buf), 3);
    TF_STREQ(buf, "one");
    TF_EQ(msg_get(5, buf, 4), 3);
    TF_STREQ(buf, " tw");
    TF_EQ(msg_get(3, buf, sizeof buf), 7);
    TF_STREQ(buf, "[||^|^1");

    msg_free();
    TF_STREQ(msg(1), "?1");
}

/* A text of MSG_MAXLEN bytes comes back whole; a substitution longer
   than MSG_MAXLEN is cut there. */
static void test_cutting(void)
{
    char arg[MSG_MAXLEN + 2];
    char want[MSG_MAXLEN + 1];
    char line[MSG_MAXLEN + 8];

    strcpy(line, "1 ");
    memset(line + PFX, 'y', MSG_MAXLEN - 1);
    line[PFX + MSG_MAXLEN - 1] = 'z';
    line[PFX + MSG_MAXLEN] = '\0';
    TF_EQ(load_text(line), 0);
    TF_STREQ(msg(1), line + PFX);
    TF_STREQ(msg_sub(1, "a", "b"), line + PFX);
    TF_EQ(msg_get(1, want, sizeof want), MSG_MAXLEN);
    TF_STREQ(want, line + PFX);

    TF_EQ(load_text("1 ab^1cd\n"), 0);
    memset(arg, 'z', MSG_MAXLEN + 1);
    arg[MSG_MAXLEN + 1] = '\0';
    memset(want, 'z', MSG_MAXLEN);
    memcpy(want, "ab", 2);
    want[MSG_MAXLEN] = '\0';
    TF_STREQ(msg_sub(1, arg, NULL), want);
    TF_EQ(strlen(msg_sub(1, arg, arg)), MSG_MAXLEN);
}

/* A number alone, or with one space after it, is a message with an
   empty text, with or without a line end. */
static void test_bare(void)
{
    TF_EQ(load_text("1"), 0);
    TF_STREQ(msg(1), "");
    TF_EQ(load_text("1 "), 0);
    TF_STREQ(msg(1), "");
    TF_EQ(load_text("12\r\n13 \n"), 0);
    TF_STREQ(msg(12), "");
    TF_STREQ(msg(13), "");
    TF_STREQ(msg(1), "?1");
}

static void test_malformed(void)
{
    bad("1 ok\n2 bad \\q escape\n", "line 3");
    bad("1 bad ^3\n", "line 2");
    bad("1 ^\n", "line 2");
    bad("1 trailing \\\n", "line 2");
    bad("1 \\x0\n", "line 2");
    bad("1 \\xZZ\n", "line 2");
    bad("1 \\x00\n", "line 2");
    bad("1 \\x01\n", "line 2");
    bad("1 \\x02\n", "line 2");
    bad("1 a\n1 b\n", "line 3: duplicate number");
    bad("1 a\tb\n", "line 2: control byte");
    bad("1 a\001b\n", "line 2: control byte");
    bad("1 a\037b\n", "line 2: control byte");
    /* After the number only the line end or a space may come. */
    bad("1x\n", "line 2: bad number");
    bad("1\tx\n", "line 2: bad number");
    bad("1\001\n", "line 2: bad number");
    bad("0 x\n", "line 2: bad number");
    bad("1200 x\n", "line 2: bad number");      /* MSG_MAXNUM */
    bad("10000 x\n", "line 2: bad number");
    bad("65537 x\n", "line 2: bad number");
    bad("01x x\n", "line 2: bad number");
    bad("01 x\n", "line 2: bad number");
    bad(" 1 x\n", "line 2: bad number");
    /* The format line is only in the data TEST/msgchk.py sums into the
       stamp, never in a file. */
    bad("1 x\n=FORMAT 2\n", "line 3");

    /* A Ctrl-Z ends the file. */
    TF_EQ(load_text("1 x\r\n\032" "2 \\q\n"), 0);
    TF_STREQ(msg(1), "x");
    TF_STREQ(msg(2), "?2");
}

/* The bytes msg_load's reader (scan, through a buffer of MSG_RDBUF of
   MSG.C) takes from the file at a time, and where the Ctrl-Z of
   test_ctrlz_places stands: at the first, the second and the last byte
   of a read, and at the first and second of the next one. */
#define CZ_READ         128
#define CZ_AT           (2 * CZ_READ)
static const u16 cz_offs[] = { 0, 1, CZ_READ - 1, CZ_READ, CZ_READ + 1 };
#define CZ_LINES        "1 s\n200 t\n450 u"
#define CZ_NLINES       ((u16)(sizeof CZ_LINES - 1))

/* A Ctrl-Z after the last lines of three ranges, two loaded and one
   paged, with and without a line end before it, and lines after it that
   would be refused: every text before it loads, none after it.  Where
   the Ctrl-Z falls in the read holding the starts of the ranges, a
   reader that loses track of its place in the file when it stops there
   reads a range from the wrong byte. */
static void test_ctrlz_places(void)
{
    char *p;
    u16 i;
    u16 k;
    u16 pad;
    int lf;

    TF_EQ(MSG_R_LISTV, 200);
    TF_EQ(MSG_R_ITEM, 450);
    TF_EQ(STAMPLEN, strlen(g_stamp));
    for (i = 0; i < sizeof cz_offs / sizeof cz_offs[0]; i++) {
        for (lf = 0; lf <= 1; lf++) {
            pad = (u16)(CZ_AT + cz_offs[i] - STAMPLEN - CZ_NLINES - lf);
            p = g_text;
            while (pad > 0) {
                k = pad > 64 ? 64 : pad;
                memset(p, '#', k);
                p[k - 1] = '\n';
                p += k;
                pad = (u16)(pad - k);
            }
            strcpy(p, CZ_LINES);
            strcat(p, lf ? "\n\032" : "\032");
            strcat(p, "451 v\n201 w\n2 x\nbad\n");
            if (!TF_EQ(load_text(g_text), 0))
                printf("  why: %s (Ctrl-Z at %u)\n", msg_why(),
                       (unsigned)(CZ_AT + cz_offs[i]));
            TF_EQ((u16)(strchr(g_text, '\032') - g_text) + STAMPLEN,
                  CZ_AT + cz_offs[i]);
            TF_STREQ(msg(1), "s");
            TF_STREQ(msg(200), "t");
            TF_STREQ(msg(450), "u");
            TF_STREQ(msg(451), "?451");
            TF_STREQ(msg(201), "?201");
            TF_STREQ(msg(2), "?2");
        }
    }
    msg_free();
}

/* A line of MSG_MAXRAW bytes before its line end is accepted, one byte
   more is not; its text is escapes, then plain bytes, unescaping to
   fewer than MSG_MAXLEN. */
static void test_rawlen(void)
{
    char line[MSG_MAXRAW + 24];

    TF_CHECK(MSG_MAXRAW - PFX - 100 <= MSG_MAXLEN);
    memset(line, 'a', MSG_MAXRAW);
    memcpy(line, "1 ", PFX);
    memset(line + PFX, '\\', 2 * 100);
    line[MSG_MAXRAW] = '\0';
    TF_EQ(load_text(line), 0);
    TF_EQ(strlen(msg(1)), 100 + MSG_MAXRAW - PFX - 2 * 100);
    /* The same at the limit with CR LF and with LF, then one byte
       over without a line end and with CR LF. */
    strcpy(line + MSG_MAXRAW, "\r\n2 x\r\n");
    TF_EQ(load_text(line), 0);
    TF_STREQ(msg(2), "x");
    strcpy(line + MSG_MAXRAW, "\n2 x\n");
    TF_EQ(load_text(line), 0);
    TF_STREQ(msg(2), "x");
    line[MSG_MAXRAW] = 'a';
    line[MSG_MAXRAW + 1] = '\0';
    bad(line, "line 2: line too long");
    strcpy(line + MSG_MAXRAW + 1, "\r\n2 x\r\n");
    bad(line, "line 2: line too long");
    /* A CR as the byte after MSG_MAXRAW, with more after it. */
    line[MSG_MAXRAW] = '\r';
    strcpy(line + MSG_MAXRAW + 1, "a\r\n");
    bad(line, "line 2");
    /* A plain text of MSG_MAXLEN bytes, then one more. */
    memset(line, 'a', MSG_MAXRAW);
    memcpy(line, "1 ", PFX);
    line[PFX + MSG_MAXLEN] = '\0';
    TF_EQ(load_text(line), 0);
    line[PFX + MSG_MAXLEN] = 'a';
    line[PFX + MSG_MAXLEN + 1] = '\0';
    bad(line, "line 2: text too long");
}

/* The stamp line: missing (another first line, the format line among
   them), foreign, or the program's with a byte more on its line.  A
   file of an older build, whose lines carry the checker's names after
   the stamp of its own messages, is refused at that line and shows none
   of its texts, names or not. */
static void test_stamp(void)
{
    char line[STAMPLEN + 8];

    TF_EQ(write_file(MSG_FILE, "1 x\n", NULL), 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
    TF_CHECK(strncmp(msg_why(), "line 1", 6) == 0);
    TF_EQ(write_file(MSG_FILE, "=FORMAT 2\r\n", g_stamp), 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
    TF_CHECK(strncmp(msg_why(), "line 1", 6) == 0);
    strcpy(line, g_stamp);
    line[10] = (char)(line[10] == '0' ? '1' : '0');
    TF_EQ(write_file(MSG_FILE, line, "1 M_A x\r\n2 MT_YES Yes\r\n"), 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
    TF_STREQ(msg_why(), "line 1: not made for this program");
    TF_STREQ(msg(1), "?1");
    TF_STREQ(msg(2), "?2");
    strcpy(line, g_stamp);
    strcpy(line + STAMPLEN - 2, "x\r\n");
    TF_EQ(write_file(MSG_FILE, line, "1 x\n"), 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
    TF_CHECK(strncmp(msg_why(), "line 1", 6) == 0);
    TF_EQ(write_file(MSG_FILE, "", NULL), 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
    TF_EQ(write_file(MSG_FILE, "\032", NULL), 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
}

/* A file of more than CMEM_MAXBLK bytes is refused. */
static void test_too_large(void)
{
    char path[PATHMAX + 16];
    char line[MSG_MAXLEN + 16];
    u16 i;
    u16 n;
    u32 total;
    int h;
    int rc;

    total = 0;
    strcpy(path, g_tdir);
    strcat(path, MSG_FILE);
    rc = cf_create(path, &h);
    TF_EQ(rc, 0);
    if (rc != 0)
        return;
    rc = cf_write(h, g_stamp, (u16)strlen(g_stamp));
    for (i = 1; rc == 0 && total <= CMEM_MAXBLK; i++) {
        memset(line, 'q', sizeof line);
        sprintf(line, "%u ", (unsigned)i);
        n = (u16)strlen(line);
        line[n] = 'q';
        line[MSG_MAXLEN + n - 1] = '\n';
        rc = cf_write(h, line, (u16)(MSG_MAXLEN + n));
        total += MSG_MAXLEN;
    }
    cf_close(h);
    TF_EQ(rc, 0);
    TF_EQ(msg_load(g_tdir, MSG_FILE), -1);
    TF_CHECK(strstr(msg_why(), "too large") != NULL);
}

/* Append to buf n message lines numbered from first on, each text k
   copies of c; buf has room for MSG_MAXRAW + 1 bytes a line. */
static void add_lines(char *buf, u16 first, u16 n, u16 k, char c)
{
    char *p;
    u16 i;

    for (i = 0; i < n; i++) {
        p = buf + strlen(buf);
        sprintf(p, "%u ", (unsigned)(first + i));
        p += strlen(p);
        memset(p, c, k);
        strcpy(p + k, "\n");
    }
}

/* A cmem_fault that refuses every allocation. */
static int no_memory(u16 size)
{
    return size > 0;
}

static u16 g_opens;

/* A cf_fault_rd hook that fails every open, counting them in g_opens. */
static int fail_open(int op)
{
    if (op != CF_OP_OPEN)
        return 0;
    g_opens++;
    return CF_EACCES;
}

const char *unknown(char *want, u16 n)
{
    sprintf(want, "?%u", (unsigned)n);
    return want;
}

/* A file changed after msg_load, as text after the stamp line, leaves
   the page of range MSG_R_LOGIN, read from it, without texts. */
static void changed(const char *text)
{
    static const char before[] = "1 s\n400 ab\n401 cd\n";
    char want[8];

    TF_EQ(MSG_R_LOGIN, 400);
    TF_EQ(load_text(before), 0);
    TF_EQ(write_file(MSG_FILE, g_stamp, text), 0);
    TF_STREQ(msg(400), unknown(want, 400));
    TF_STREQ(msg(401), unknown(want, 401));
    TF_STREQ(msg(1), "s");
}

/* The text of each of eight lines that fill a page exactly: a line takes
   two bytes of table and its text with a NUL. */
#define PG_FULL         (MSG_PAGE / 8 - 2 - 1)

/* The ranges from MSG_PAGEMIN up to MSG_PAGEEND are read as pages, into
   MSG_NSLOT slots of which the one used least recently is replaced; a
   page must fit MSG_PAGE bytes, and a range's lines must stand
   together. */
static void test_pages(void)
{
    char want[8];

    /* Shared texts, then three pages (with gaps in the numbers) and a
       range without texts, which is never read. */
    TF_EQ(MSG_NSLOT, 2);
    g_text[0] = '\0';
    add_lines(g_text, MSG_R_TUI, 1, 1, 's');
    add_lines(g_text, MSG_R_LOGIN + 2, 1, 1, 'a');
    add_lines(g_text, MSG_R_ITEM, 1, 1, 'b');
    add_lines(g_text, MSG_R_LOC + 5, 1, 1, 'c');
    TF_EQ(load_text(g_text), 0);
    /* Reading a page takes no memory. */
    cmem_fault = no_memory;
    TF_STREQ(msg(MSG_R_LOGIN + 2), "a");
    cmem_fault = NULL;
    TF_STREQ(msg(MSG_R_ITEM), "b");
    TF_STREQ(msg(MSG_R_LOGIN + 1), unknown(want, MSG_R_LOGIN + 1));
    /* With the file unreadable, the two pages held still answer; a
       third range takes the slot used least recently, tries the file
       once and answers "?nnn" from then on; a range without texts
       never tries it; shared texts stay. */
    g_opens = 0;
    cf_fault_rd = fail_open;
    TF_STREQ(msg(MSG_R_LOGIN + 2), "a");
    TF_STREQ(msg(MSG_R_ITEM), "b");
    TF_STREQ(msg(MSG_R_LOC + 5), unknown(want, MSG_R_LOC + 5));
    TF_STREQ(msg(MSG_R_LOC + 5), want);
    TF_STREQ(msg(MSG_R_LOC), unknown(want, MSG_R_LOC));
    TF_EQ(g_opens, 1);
    TF_STREQ(msg(MSG_R_ITEM), "b");
    TF_STREQ(msg(MSG_R_LOGIN + 2), unknown(want, MSG_R_LOGIN + 2));
    TF_STREQ(msg(MSG_R_LOAN), unknown(want, MSG_R_LOAN));
    TF_STREQ(msg(MSG_R_TUI), "s");
    TF_EQ(g_opens, 2);
    cf_fault_rd = NULL;
    /* Readable again, a page that failed is read again only once its
       slot has been used for another. */
    TF_STREQ(msg(MSG_R_LOGIN + 2), unknown(want, MSG_R_LOGIN + 2));
    TF_STREQ(msg(MSG_R_LOC + 5), "c");
    TF_STREQ(msg(MSG_R_ITEM), "b");
    TF_STREQ(msg(MSG_R_LOGIN + 2), "a");

    /* The file changed under a page: a longer text, a number past the
       range's table, the file cut inside the range. */
    changed("1 s\n400 abcdefghijklmno\n");
    changed("1 s\n400 ab\n405 cd\n");
    changed("1 s\n400 ab\n4");
    /* One byte over the page: 4 of table, 7 of text for 6 of room. */
    changed("1 s\n400 abcdef\n#2\n");

    /* Eight texts of PG_FULL bytes fill a page exactly; one more byte
       is too much. */
    g_text[0] = '\0';
    add_lines(g_text, MSG_R_ITEM, 8, PG_FULL, 'x');
    TF_EQ(load_text(g_text), 0);
    TF_EQ(strlen(msg(MSG_R_ITEM + 7)), PG_FULL);
    g_text[0] = '\0';
    add_lines(g_text, MSG_R_ITEM, 7, PG_FULL, 'x');
    add_lines(g_text, MSG_R_ITEM + 7, 1, PG_FULL + 1, 'x');
    bad(g_text, "line 9: page too large");

    /* A range's lines stand together. */
    g_text[0] = '\0';
    add_lines(g_text, MSG_R_ITEM, 1, 1, 'x');
    add_lines(g_text, MSG_R_LOC, 1, 1, 'y');
    add_lines(g_text, MSG_R_ITEM + 1, 1, 1, 'z');
    bad(g_text, "line 4: range split");
    msg_free();
}

#define PG_ESC          40      /* bytes of a test_area text as \xHH    */

/* The read area holds the lines of the largest paged range as the file
   has them: eight texts of PG_FULL bytes, PG_ESC of each written as
   \xHH, take far more than MSG_PAGE as lines while their page fits it
   exactly. */
static void test_area(void)
{
    char want[PG_FULL + 1];
    char *p;
    u16 i;
    u16 k;

    g_text[0] = '\0';
    add_lines(g_text, MSG_R_TUI, 1, 1, 's');
    for (i = 0; i < 8; i++) {
        p = g_text + strlen(g_text);
        sprintf(p, "%u ", (unsigned)(MSG_R_ITEM + i));
        p += strlen(p);
        for (k = 0; k < PG_ESC; k++, p += 4)
            memcpy(p, "\\x41", 4);
        memset(p, 'b', PG_FULL - PG_ESC);
        strcpy(p + PG_FULL - PG_ESC, "\n");
    }
    add_lines(g_text, MSG_R_LOC, 1, 1, 'c');
    TF_EQ(load_text(g_text), 0);
    memset(want, 'A', PG_ESC);
    memset(want + PG_ESC, 'b', PG_FULL - PG_ESC);
    want[PG_FULL] = '\0';
    for (i = 0; i < 8; i++)
        TF_STREQ(msg(MSG_R_ITEM + i), want);
    TF_STREQ(msg(MSG_R_LOC), "c");
    TF_STREQ(msg(MSG_R_ITEM), want);
    TF_STREQ(msg(MSG_R_TUI), "s");
    msg_free();
}

/* A file without a paged range, as the server's is, gets no page slots
   and no read area: its far block, the one msg_load leaves, holds the
   index and the loaded pages only, and its texts, those from
   MSG_PAGEEND on included, answer with the file gone; the last text,
   MSG_PAGEEND + 1, ends on the block's last byte, which a text is read
   up to.  A paged range brings the slots. */
static void test_loaded(void)
{
    g_text[0] = '\0';
    add_lines(g_text, MSG_R_TUI, 1, 1, 's');
    add_lines(g_text, MSG_PAGEEND, 2, 1, 'v');
    TF_EQ(load_watched(g_text, 0, 0, NULL), 0);
    TF_CHECK(g_live[0] < MSG_PAGE);
    g_opens = 0;
    cf_fault_rd = fail_open;
    TF_STREQ(msg(MSG_PAGEEND + 1), "v");
    TF_STREQ(msg(MSG_R_TUI), "s");
    TF_EQ(g_opens, 0);
    cf_fault_rd = NULL;
    /* A loaded range is not held to MSG_PAGE: eight texts of 140 bytes
       take more. */
    g_text[0] = '\0';
    add_lines(g_text, MSG_PAGEEND, 8, 140, 'w');
    TF_EQ(load_text(g_text), 0);
    TF_EQ(strlen(msg(MSG_PAGEEND + 7)), 140);
    g_text[0] = '\0';
    add_lines(g_text, MSG_PAGEEND - 1, 1, 1, 'p');
    add_lines(g_text, MSG_PAGEEND, 1, 1, 'v');
    TF_EQ(load_watched(g_text, 0, 0, NULL), 0);
    TF_CHECK(g_live[0] > MSG_NSLOT * MSG_PAGE);
    TF_STREQ(msg(MSG_PAGEEND - 1), "p");
    TF_STREQ(msg(MSG_PAGEEND), "v");
    msg_free();
}

static void test_missing(void)
{
    char path[PATHMAX + 16];
    int h;
    int here;

    strcpy(path, g_tdir);
    strcat(path, MSG_FILE);
    cf_remove(path);
    here = cf_open(MSG_FILE, CF_READ, &h) == 0;
    if (here)
        cf_close(h);
    /* Without a file in dir, the current directory is tried. */
    if (here) {
        TF_EQ(msg_load(g_tdir, MSG_FILE), 0);
    } else {
        TF_EQ(msg_load(g_tdir, MSG_FILE), CF_ENOENT);
        TF_CHECK(msg_why()[0] != '\0');
        TF_EQ(msg_load("", MSG_FILE), CF_ENOENT);
    }
    msg_free();
}

/* ---------------------------------------------------------------------
 * Unescaping against its reference.
 *
 * ref_unescape is MSG.C's unescape in its plainest form, every test
 * made at every byte, kept here as the definition of its results.
 * msg_load must give what it gives for every text: the same refusal and
 * reason, or the same stored bytes, read back with the places dropped
 * (msg) and filled (msg_sub).  unescape is static in MSG.C, so each text
 * is written as a line of a message file and loaded: a text the
 * reference refuses alone, as message 1, the others as many to a file as
 * g_text holds, numbered from 1 up to below MSG_PAGEMIN.  msg_load
 * unescapes a line in place, from the line's start, so a byte written
 * ahead of the byte being read changes a byte still to be read and shows
 * in the result.
 * The inputs are every text of 0..UE_EXLEN bytes over ue_syms, the
 * bytes that steer unescape, and every text of 0..UE_TAILLEN bytes over
 * them after each prefix of ue_heads, which brings the result up to
 * the limit.
 * --------------------------------------------------------------------- */
/* The texts of four bytes and the last of ue_heads run on the host only,
   since under DOS each text refused takes a file of its own. */
#ifdef COMPAT_HOST
#define UE_EXLEN        4
#define UE_NHEADS       4
#else
#define UE_EXLEN        3
#define UE_NHEADS       3
#endif
#define UE_TAILLEN      2
#define UE_NSYM         16
#define UE_SHOW         8       /* mismatches shown                     */
#define UE_TAIL         8       /* bytes of a text shown, its last ones */
#define REF_PH1         '\001'  /* MSG.C's places, MSG_PH1 and MSG_PH2  */
#define REF_PH2         '\002'

static const char ue_syms[UE_NSYM] = {
    'a', 'A', ' ', '~', '\177', '\377', '\\', '^', 'n', 'x', '1', '2',
    '3', 'F', '\037', '\001'
};

/* A head of the limit texts, for each k from MSG_MAXLEN - 3 to
   MSG_MAXLEN + 1: n copies of rep, each unescaping to one byte, then 'a'
   up to k bytes of result.  n is k for UE_K; for UE_HALF it is k / 2
   copies of "^1" (k even) and no 'a', a raw text of k bytes whose result
   stays far below the limit; UE_LAG copies of "^1" then 'a' reach the
   limit with the bytes written UE_LAG behind those read. */
typedef struct ue_head {
    const char *rep;
    u16 n;
} ue_head;

#define UE_K            0xFFFFU
#define UE_HALF         0xFFFEU
#define UE_LAG          40
static const ue_head ue_heads[4] = {
    { "a", UE_K }, { "A", UE_K }, { "^1", UE_HALF }, { "^1", UE_LAG }
};

static u16 g_ue_shown;
static u16 g_ue_num;            /* the last number in the batch file    */
static u16 g_ue_len;            /* bytes of the batch file's text       */

static int ref_unescape(const char *t, char *out, const char **what)
{
    u16 n;
    int h1;
    int h2;
    char c;

    n = 0;
    while (*t != '\0') {
        c = *t++;
        if (c == '\\') {
            c = *t++;
            if (c == 'n') {
                c = '\n';
            } else if (c == '\\') {
                c = '\\';
            } else if (c == 'x') {
                h1 = txt_hexval(t[0]);
                h2 = h1 < 0 ? -1 : txt_hexval(t[1]);
                if (h2 < 0 || h1 * 16 + h2 <= REF_PH2) {
                    *what = "bad \\x escape";
                    return -1;
                }
                c = (char)(u8)(h1 * 16 + h2);
                t += 2;
            } else {
                *what = "bad escape";
                return -1;
            }
        } else if (c == '^') {
            c = *t++;
            if (c == '1')
                c = REF_PH1;
            else if (c == '2')
                c = REF_PH2;
            else if (c != '^') {
                *what = "bad ^ sequence";
                return -1;
            }
        } else if ((u8)c < 0x20) {
            *what = "control byte";
            return -1;
        }
        if (n >= MSG_MAXLEN) {
            *what = "text too long";
            return -1;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return (int)n;
}

/* The stored text u as msg (sub 0) or msg_sub with "<1>" and "<2>"
   (sub 1) give it, into want (MSG_MAXLEN + 1 bytes). */
static void ue_want(const char *u, int sub, char *want)
{
    const char *a;
    u16 k;

    k = 0;
    for (; *u != '\0' && k < MSG_MAXLEN; u++) {
        if (*u != REF_PH1 && *u != REF_PH2) {
            want[k++] = *u;
        } else if (sub) {
            for (a = *u == REF_PH1 ? "<1>" : "<2>";
                 *a != '\0' && k < MSG_MAXLEN; a++)
                want[k++] = *a;
        }
    }
    want[k] = '\0';
}

/* Show the text t (its length and last UE_TAIL bytes), what msg_load
   or msg gave and what the reference wants, for the first UE_SHOW texts
   that differ. */
static void ue_show(const char *t, const char *got, const char *want)
{
    size_t n;

    if (g_ue_shown >= UE_SHOW)
        return;
    g_ue_shown++;
    n = strlen(t);
    printf("  text of %u bytes,%s", (unsigned)n, n > UE_TAIL ? " ..." : "");
    for (t += n > UE_TAIL ? n - UE_TAIL : 0; *t != '\0'; t++)
        printf(" %02X", (unsigned)(u8)*t);
    printf(": got \"%s\", want \"%s\"\n", got, want);
}

/* Load the batch file of texts the reference accepts, g_ue_num lines in
   g_text, and check each text; 0 when one differs. */
static int ue_flush(void)
{
    char u[MSG_MAXLEN + 1];
    char want[MSG_MAXLEN + 1];
    const char *what;
    const char *p;
    char *q;
    u16 num;
    int ok;

    ok = 1;
    if (g_ue_num == 0)
        return 1;
    if (load_text(g_text) != 0) {
        printf("  batch refused: %s\n", msg_why());
        g_ue_shown++;
        ok = 0;
    }
    for (p = g_text; ok && *p != '\0'; p = q + 1) {
        num = msg_dec(&p, MSG_MAXNUM);
        q = strchr(p, '\n');
        *q = '\0';
        p++;
        ref_unescape(p, u, &what);
        ue_want(u, 0, want);
        if (strcmp(msg(num), want) != 0) {
            ue_show(p, msg(num), want);
            ok = 0;
        }
        ue_want(u, 1, want);
        if (strcmp(msg_sub(num, "<1>", "<2>"), want) != 0) {
            ue_show(p, msg_sub(num, "<1>", "<2>"), want);
            ok = 0;
        }
    }
    g_text[0] = '\0';
    g_ue_num = 0;
    g_ue_len = 0;
    return ok;
}

/* Check the text t of len bytes: a text the reference refuses at once,
   alone in a file; one it accepts into the batch file, which is first
   loaded and checked when full.  0 when a text differs. */
static int ue_text(const char *t, u16 len)
{
    char line[MSG_MAXRAW + 2];
    char u[MSG_MAXLEN + 1];
    const char *what;
    int ok;

    ok = 1;
    if (ref_unescape(t, u, &what) < 0) {
        strcpy(line, "1 ");
        strcat(line, t);
        strcat(line, "\n");
        if (load_text(line) != -1) {
            ue_show(t, "loaded", what);
            return 0;
        }
        strcpy(u, "line 2: ");
        strcat(u, what);
        if (strcmp(msg_why(), u) != 0) {
            ue_show(t, msg_why(), u);
            return 0;
        }
        return 1;
    }
    if (g_ue_num + 1 >= MSG_PAGEMIN ||
        (u32)g_ue_len + TXT_U32LEN + len + 3 > sizeof g_text)
        ok = ue_flush();
    sprintf(g_text + g_ue_len, "%u %s\n", (unsigned)++g_ue_num, t);
    g_ue_len = (u16)(g_ue_len + strlen(g_text + g_ue_len));
    return ok;
}

/* Every text of 0..max bytes over ue_syms after the first at bytes of
   t; the number of failures, each a text refused alone that differs or
   a batch file whose check stopped at a text that differs. */
static u32 ue_all(char *t, u16 at, u16 max)
{
    u32 code;
    u32 count;
    u32 bad;
    u16 len;
    u16 i;

    bad = 0;
    for (len = 0; len <= max; len++) {
        count = (u32)1 << (4 * len);
        for (code = 0; code < count; code++) {
            for (i = 0; i < len; i++)
                t[at + i] = ue_syms[(u16)(code >> (4 * i)) & (UE_NSYM - 1)];
            t[at + len] = '\0';
            if (!ue_text(t, (u16)(at + len)))
                bad++;
        }
    }
    return bad;
}

static void test_unescape_ref(void)
{
    char t[MSG_MAXRAW + 1];
    const ue_head *h;
    u32 bad;
    u16 at;
    u16 k;
    u16 n;
    u16 i;
    u16 r;

    g_ue_shown = 0;
    g_ue_num = 0;
    g_ue_len = 0;
    g_text[0] = '\0';
    bad = ue_all(t, 0, UE_EXLEN);
    for (h = ue_heads; h < ue_heads + UE_NHEADS; h++) {
        for (k = MSG_MAXLEN - 3; k <= MSG_MAXLEN + 1; k++) {
            if (h->n == UE_HALF && k % 2 != 0)
                continue;
            n = h->n == UE_K ? k : h->n == UE_HALF ? (u16)(k / 2) : h->n;
            at = 0;
            for (i = 0; i < n; i++) {
                for (r = 0; h->rep[r] != '\0'; r++)
                    t[at++] = h->rep[r];
            }
            for (i = n; i < k && h->n != UE_HALF; i++)
                t[at++] = 'a';
            bad += ue_all(t, at, UE_TAILLEN);
        }
    }
    if (!ue_flush())
        bad++;
    TF_EQ(bad, 0);
    msg_free();
}

/* The named limits: a text of MSG_MAXLEN bytes loads, whether its last
   byte is plain or escaped and with its raw form filling a line of
   MSG_MAXRAW bytes; a byte at MSG_MAXLEN + 1, plain or escaped, is "text
   too long"; a bad sequence or a control byte there is its own reason
   when MSG_MAXLEN bytes stand before it, "text too long" when one more
   does.  A line of MSG_MAXRAW plain bytes, all read and written in
   place before the refusal, is "text too long". */
#define UE_ESC          ((MSG_MAXRAW - PFX - MSG_MAXLEN) / 3)
#define UE_LONG         "line 2: text too long"

/* Message 1 as nesc "\x41" escapes and plain 'a', the escapes first or
   (esclast) last, then tail, into line (MSG_MAXRAW + 2 bytes); the
   result of the part before tail into want (MSG_MAXRAW + 1 bytes). */
static void limit_line(char *line, char *want, u16 plain, u16 nesc,
                       int esclast, const char *tail)
{
    char *p;
    u16 i;

    strcpy(line, "1 ");
    p = line + PFX;
    for (i = 0; i < nesc; i++)
        memcpy(p + 4 * i + (esclast ? plain : 0), "\\x41", 4);
    memset(p + (esclast ? 0 : 4 * nesc), 'a', plain);
    strcpy(p + 4 * nesc + plain, tail);
    memset(want + (esclast ? plain : 0), 'A', nesc);
    memset(want + (esclast ? 0 : nesc), 'a', plain);
    want[nesc + plain] = '\0';
}

/* The line loads, msg giving its body then tmsg, msg_sub with "x" for
   ^1 its body then tsub. */
static void limit_ok(u16 plain, u16 nesc, int esclast, const char *tail,
                     const char *tmsg, const char *tsub)
{
    char line[MSG_MAXRAW + 2];
    char want[MSG_MAXRAW + 1];
    char body[MSG_MAXRAW + 1];

    limit_line(line, body, plain, nesc, esclast, tail);
    if (!TF_EQ(load_text(line), 0))
        printf("  why: %s\n", msg_why());
    strcpy(want, body);
    strcat(want, tmsg);
    TF_STREQ(msg(1), want);
    strcpy(want, body);
    strcat(want, tsub);
    TF_STREQ(msg_sub(1, "x", NULL), want);
}

/* The line is refused with why. */
static void limit_bad(u16 plain, u16 nesc, int esclast, const char *tail,
                      const char *why)
{
    char line[MSG_MAXRAW + 2];
    char body[MSG_MAXRAW + 1];

    limit_line(line, body, plain, nesc, esclast, tail);
    bad(line, why);
}

static void test_unescape_limits(void)
{
    limit_ok(MSG_MAXLEN - 1, 0, 0, "^1", "", "x");
    limit_ok(MSG_MAXLEN - 1, 0, 0, "\\x41", "A", "A");
    limit_bad(MSG_MAXLEN, 0, 0, "^1", UE_LONG);
    limit_bad(MSG_MAXLEN, 0, 0, "^^", UE_LONG);
    limit_bad(MSG_MAXLEN, 0, 0, "\\x41", UE_LONG);
    limit_bad(MSG_MAXLEN, 0, 0, "a", UE_LONG);
    limit_bad(MSG_MAXLEN, 0, 0, "\\q", "line 2: bad escape");
    limit_bad(MSG_MAXLEN, 0, 0, "\\", "line 2: bad escape");
    limit_bad(MSG_MAXLEN, 0, 0, "\\x4", "line 2: bad \\x escape");
    limit_bad(MSG_MAXLEN, 0, 0, "\\x02", "line 2: bad \\x escape");
    limit_bad(MSG_MAXLEN, 0, 0, "^3", "line 2: bad ^ sequence");
    limit_bad(MSG_MAXLEN, 0, 0, "\t", "line 2: control byte");
    limit_bad(MSG_MAXLEN + 1, 0, 0, "\\q", UE_LONG);
    limit_bad(MSG_MAXLEN + 1, 0, 0, "\\x4", UE_LONG);
    limit_bad(MSG_MAXLEN + 1, 0, 0, "^3", UE_LONG);
    limit_bad(MSG_MAXLEN + 1, 0, 0, "\t", UE_LONG);
    /* UE_ESC escapes of four bytes and the plain bytes after or before
       them: a text of MSG_MAXLEN bytes on a line of MSG_MAXRAW, then a
       byte more on a shorter line. */
    TF_EQ(PFX + 4 * UE_ESC + MSG_MAXLEN - UE_ESC, MSG_MAXRAW);
    limit_ok(MSG_MAXLEN - UE_ESC, UE_ESC, 0, "", "", "");
    limit_ok(MSG_MAXLEN - UE_ESC, UE_ESC, 1, "", "", "");
    limit_bad(MSG_MAXLEN - UE_ESC + 2, UE_ESC - 1, 0, "", UE_LONG);
    limit_bad(MSG_MAXLEN - UE_ESC + 2, UE_ESC - 1, 1, "", UE_LONG);
    limit_bad(MSG_MAXRAW - PFX, 0, 0, "", UE_LONG);
    /* A "\x" at the end of a line of MSG_MAXRAW bytes: no hex digit is
       read past the text's NUL. */
    limit_bad(MSG_MAXLEN - UE_ESC - 2, UE_ESC, 0, "\\x",
              "line 2: bad \\x escape");
    msg_free();
}

/* ---------------------------------------------------------------------
 * The text file reader, msg_date and msg_dec.
 * --------------------------------------------------------------------- */
#define RD_FILE         "RD.TXT"
#define RD_MAX          8       /* the cases' line and piece limit     */
#define RD_BUF          64      /* the largest reader buffer tried      */
#define RD_SIZES        13      /* reader buffers of 1..RD_SIZES bytes  */
#define RD_OUT          96
#define RD_CALLS        40      /* reader calls on one file at most, so
                                   a wrong result fails, not loops      */
#define RD_GUARD        2       /* '#' bytes around a block's file      */

static char g_rd_path[PATHMAX + 16];

/* A file's bytes, then what msg_rd_line makes of them with lines of at
   most RD_MAX bytes (each line followed by '|', a cut one as its first
   RD_MAX bytes and "+|"), and what msg_rd_piece makes of them (the
   pieces of each line joined, followed by '|'). */
typedef struct {
    const char *data;
    const char *lines;
    const char *pieces;
} rd_case;

static const rd_case rd_cases[] = {
    { "", "", "" },
    { "\032", "", "" },
    { "ab\r\ncd\r\n", "ab|cd|", "ab|cd|" },
    { "ab\ncd", "ab|cd|", "ab|cd|" },
    { "a\rb\r\n", "a\rb|", "a\rb|" },
    { "a\r\r\n", "a\r|", "a\r|" },
    { "\r\n\n", "||", "||" },
    { "ab\032cd\nef\n", "ab|", "ab|" },
    { "ab\r\n\032\r\n", "ab|", "ab|" },
    { "ab\r", "ab|", "ab|" },
    { "1234567\r\n", "1234567|", "1234567|" },
    { "12345678\r\nx\n", "12345678|x|", "12345678|x|" },
    { "12345678", "12345678|", "12345678|" },
    { "123456789\r\nx\n", "12345678+|x|", "123456789|x|" },
    { "1234567890abcdef\r\nx", "12345678+|x|", "1234567890abcdef|x|" },
    { "12345678\rx\n", "12345678+|", "12345678\rx|" }
};
#define RD_NCASES       (sizeof rd_cases / sizeof rd_cases[0])

/* Write RD_FILE holding the string data. */
static int write_rd(const char *data)
{
    int h;
    int rc;

    rc = cf_create(g_rd_path, &h);
    if (rc != 0)
        return rc;
    if (*data != '\0')
        rc = cf_write(h, data, (u16)strlen(data));
    cf_close(h);
    return rc;
}

/* Append n bytes of s to out (RD_OUT bytes, o used); 0 when they do not
   fit. */
static int put_out(char *out, u16 *o, const char *s, u16 n)
{
    if (*o + n >= RD_OUT)
        return 0;
    memcpy(out + *o, s, n);
    *o = (u16)(*o + n);
    out[*o] = '\0';
    return 1;
}

/* Open RD_FILE as *h and start r on it through buf (bufsz bytes): on
   the handle, or (blk 1) on a far block *b holding the file between
   RD_GUARD '#' bytes on each side, read into it with cf_readblk; *b is
   NULL for the handle.  0 when that fails. */
static int rd_start(msg_rd *r, char *buf, u16 bufsz, int blk, int *h,
                    cmem_blk **b)
{
    u32 size;
    u16 got;

    size = 0;
    got = 0;
    *b = NULL;
    if (!TF_EQ(cf_open(g_rd_path, CF_READ, h), 0))
        return 0;
    if (!blk) {
        msg_rd_init(r, *h, buf, bufsz);
        return 1;
    }
    TF_EQ(cf_size(*h, &size), 0);
    *b = cmem_alloc((u16)(size + 2 * RD_GUARD));
    if (!TF_CHECK(*b != NULL)) {
        cf_close(*h);
        return 0;
    }
    cmem_set(*b, 0, '#', (u16)(size + 2 * RD_GUARD));
    TF_EQ(cf_readblk(*h, *b, RD_GUARD, (u16)size, &got), 0);
    TF_EQ(got, size);
    msg_rd_blk(r, *b, RD_GUARD, (u16)(RD_GUARD + size), buf, bufsz);
    return 1;
}

/* RD_FILE read with msg_rd_line through a buffer of bufsz bytes (from a
   block when blk is 1), lines of at most max, into out as rd_case.lines
   has it.  Returns the result that ended the reading, 0 at the end of
   the file, or 1 when RD_CALLS calls did not reach the end. */
static int read_lines(u16 bufsz, int blk, u16 max, char *out)
{
    msg_rd r;
    cmem_blk *b;
    char buf[RD_BUF];
    char line[RD_MAX + 4];
    u16 len;
    u16 o;
    u16 turns;
    int h;
    int rc;

    o = 0;
    rc = -1;
    out[0] = '\0';
    if (!rd_start(&r, buf, bufsz, blk, &h, &b))
        return -1;
    for (turns = 0; turns < RD_CALLS; turns++) {
        memset(line, '#', sizeof line);
        len = 999;
        rc = msg_rd_line(&r, line, max, &len);
        if (rc != 1)
            break;
        /* The line takes at most max + 1 bytes of its buffer. */
        TF_EQ(line[max + 1], '#');
        if (len > max) {
            TF_EQ(len, max + 1);
            if (!put_out(out, &o, line, max) || !put_out(out, &o, "+|", 2))
                break;
        } else {
            TF_EQ(line[len], '\0');
            if (!put_out(out, &o, line, len) || !put_out(out, &o, "|", 1))
                break;
        }
    }
    /* The end stays the end. */
    if (rc == 0)
        TF_EQ(msg_rd_line(&r, line, max, &len), 0);
    cf_close(h);
    cmem_free(b);
    return rc;
}

/* The same with msg_rd_piece and pieces of at most max bytes, into out
   as rd_case.pieces has it.  Every line ends with a piece whose *eol is
   1, the last one of the file too. */
static int read_pieces(u16 bufsz, int blk, u16 max, char *out)
{
    msg_rd r;
    cmem_blk *b;
    char buf[RD_BUF];
    char p[RD_MAX + 2];
    u16 n;
    u16 o;
    u16 turns;
    int eol;
    int h;
    int rc;

    o = 0;
    rc = -1;
    out[0] = '\0';
    if (!rd_start(&r, buf, bufsz, blk, &h, &b))
        return -1;
    for (turns = 0; turns < RD_CALLS; turns++) {
        memset(p, '#', sizeof p);
        n = 999;
        eol = 9;
        rc = msg_rd_piece(&r, p, max, &n, &eol);
        if (rc != 1)
            break;
        TF_CHECK(n <= max);
        TF_CHECK(eol == 0 || eol == 1);
        TF_EQ(p[max], '#');
        if (n > max)
            break;
        if (!put_out(out, &o, p, n) ||
            (eol == 1 && !put_out(out, &o, "|", 1)))
            break;
    }
    if (rc == 0)
        TF_EQ(msg_rd_piece(&r, p, max, &n, &eol), 0);
    cf_close(h);
    cmem_free(b);
    return rc;
}

/* Every case, from the handle and from a block, through every buffer
   size from 1 (so that a CR, a LF and a Ctrl-Z fall at each buffer
   boundary) to RD_SIZES, and RD_BUF. */
static void test_reader_cases(void)
{
    char out[RD_OUT];
    u16 i;
    u16 sz;
    u16 bufsz;
    int blk;

    for (i = 0; i < RD_NCASES; i++) {
        if (!TF_EQ(write_rd(rd_cases[i].data), 0))
            return;
        for (sz = 1; sz <= 2 * (RD_SIZES + 1); sz++) {
            bufsz = sz % (RD_SIZES + 1) != 0 ? sz % (RD_SIZES + 1) : RD_BUF;
            blk = sz > RD_SIZES + 1;
            TF_EQ(read_lines(bufsz, blk, RD_MAX, out), 0);
            if (!TF_STREQ(out, rd_cases[i].lines))
                printf("  lines: case %u, buffer %u, block %d\n",
                       (unsigned)i, (unsigned)bufsz, blk);
            TF_EQ(read_pieces(bufsz, blk, RD_MAX, out), 0);
            if (!TF_STREQ(out, rd_cases[i].pieces))
                printf("  pieces: case %u, buffer %u, block %d\n",
                       (unsigned)i, (unsigned)bufsz, blk);
        }
    }
}

/* A long line comes in pieces of max bytes, only the last with *eol. */
static void test_reader_pieces(void)
{
    msg_rd r;
    char buf[RD_BUF];
    char p[RD_MAX + 2];
    u16 n;
    int eol;
    int h;

    if (!TF_EQ(write_rd("abcdefghij\r\nk\n"), 0))
        return;
    if (!TF_EQ(cf_open(g_rd_path, CF_READ, &h), 0))
        return;
    msg_rd_init(&r, h, buf, RD_BUF);
    TF_EQ(msg_rd_piece(&r, p, 4, &n, &eol), 1);
    TF_EQ(n, 4);
    TF_EQ(eol, 0);
    TF_CHECK(memcmp(p, "abcd", 4) == 0);
    TF_EQ(msg_rd_piece(&r, p, 4, &n, &eol), 1);
    TF_EQ(n, 4);
    TF_EQ(eol, 0);
    TF_CHECK(memcmp(p, "efgh", 4) == 0);
    TF_EQ(msg_rd_piece(&r, p, 4, &n, &eol), 1);
    TF_EQ(n, 2);
    TF_EQ(eol, 1);
    TF_CHECK(memcmp(p, "ij", 2) == 0);
    TF_EQ(msg_rd_piece(&r, p, 4, &n, &eol), 1);
    TF_EQ(n, 1);
    TF_EQ(eol, 1);
    TF_EQ(p[0], 'k');
    TF_EQ(msg_rd_piece(&r, p, 4, &n, &eol), 0);
    cf_close(h);

    /* Pieces and lines from one reader: a cut line's rest is skipped,
       not left for the next call. */
    if (!TF_EQ(write_rd("abcdefghij\nxy\nz\n"), 0))
        return;
    if (!TF_EQ(cf_open(g_rd_path, CF_READ, &h), 0))
        return;
    msg_rd_init(&r, h, buf, 3);
    TF_EQ(msg_rd_line(&r, p, 4, &n), 1);
    TF_EQ(n, 5);
    TF_CHECK(memcmp(p, "abcd", 4) == 0);
    TF_EQ(msg_rd_line(&r, p, 4, &n), 1);
    TF_EQ(n, 2);
    TF_STREQ(p, "xy");
    TF_EQ(msg_rd_piece(&r, p, 4, &n, &eol), 1);
    TF_EQ(n, 1);
    TF_EQ(eol, 1);
    TF_EQ(p[0], 'z');
    TF_EQ(msg_rd_line(&r, p, 4, &n), 0);
    cf_close(h);
}

/* A read error comes back as the negated CF_E code: a handle closed
   under the reader, before the first read and after some bytes. */
static void test_reader_error(void)
{
    msg_rd r;
    char buf[RD_BUF];
    char p[RD_MAX + 4];
    char got[RD_OUT];
    u16 n;
    u16 o;
    u16 turns;
    int eol;
    int h;
    int rc;

    o = 0;
    rc = 1;
    if (!TF_EQ(write_rd("abcdefgh\n"), 0))
        return;
    if (!TF_EQ(cf_open(g_rd_path, CF_READ, &h), 0))
        return;
    msg_rd_init(&r, h, buf, RD_BUF);
    cf_close(h);
    /* The piece reader first: msg_rd_line skips the rest of a cut line
       through it, and would never end on an error taken for a piece. */
    if (!TF_EQ(msg_rd_piece(&r, p, RD_MAX, &n, &eol), -CF_EBADF))
        return;
    if (!TF_EQ(cf_open(g_rd_path, CF_READ, &h), 0))
        return;
    msg_rd_init(&r, h, buf, RD_BUF);
    cf_close(h);
    TF_EQ(msg_rd_line(&r, p, RD_MAX, &n), -CF_EBADF);

    if (!TF_EQ(cf_open(g_rd_path, CF_READ, &h), 0))
        return;
    msg_rd_init(&r, h, buf, 4);
    TF_EQ(msg_rd_piece(&r, p, 2, &n, &eol), 1);
    TF_EQ(n, 2);
    TF_CHECK(memcmp(p, "ab", 2) == 0);
    cf_close(h);
    /* What was buffered may still come; then the error. */
    got[0] = '\0';
    for (turns = 0; turns < 8; turns++) {
        rc = msg_rd_piece(&r, p, 2, &n, &eol);
        if (rc != 1 || n > 2 || !put_out(got, &o, p, n))
            break;
    }
    TF_EQ(rc, -CF_EBADF);
    TF_CHECK(strncmp("cd", got, strlen(got)) == 0);
}

/* ---------------------------------------------------------------------
 * The reader against its reference.
 *
 * ref_init, ref_blk, ref_piece and ref_line are the reader written a
 * byte at a time, the plainest form of its rules, kept here as the
 * definition of its results.  msg_rd_piece and msg_rd_line must give
 * call by call what they give: the same results, bytes, lengths and
 * line ends, and on a block the same place in it after every line.
 * The inputs are every string of up to DF_EXLEN bytes over the bytes
 * that steer the reader, and DF_NRAND pseudo-random strings of up to
 * DF_RANDMAX bytes, read from a block that has line ends on both sides
 * of the string and from a handle (the short strings up to DF_HDLEN
 * bytes, the long ones through buffers of DF_HDRAND bytes or more),
 * through every buffer size and limit of df_bufs and df_maxes
 * up to the first one past the string's length: past it, every buffer
 * takes the string with one read and every line fits.
 * --------------------------------------------------------------------- */
/* Under DOS, where the DOS call of each read of a handle takes most of
   the time, the short strings stop a byte sooner, those read from a
   handle two, and the long ones are read from a handle only through
   buffers of at least DF_HDRAND bytes. */
#ifdef COMPAT_HOST
#define DF_EXLEN        7
#define DF_HDLEN        7
#define DF_HDRAND       1
#else
#define DF_EXLEN        6
#define DF_HDLEN        5
#define DF_HDRAND       31
#endif
#define DF_NOHD         0xFFFFU /* df_string's hdmin for no handle      */
#define DF_NRAND        100
#define DF_RANDMAX      300
#define DF_BUFMAX       129     /* the largest of df_bufs               */
#define DF_MAXTOP       300     /* the largest of df_maxes, past a u8   */
#define DF_SHOW         8       /* mismatches shown in full             */
#define DF_SKIPBUF      16      /* bytes of a cut line's rest at a time */

static const u16 df_bufs[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 31, 32, 33, 127, 128, DF_BUFMAX
};
static const u16 df_maxes[] = { 1, 2, 3, 4, 5, 8, MSG_MAXRAW, DF_MAXTOP };
#define DF_NBUFS        (sizeof df_bufs / sizeof df_bufs[0])
#define DF_NMAXES       (sizeof df_maxes / sizeof df_maxes[0])

/* The bytes of the short strings, and those of the long ones, Ctrl-Z
   last, so that the first seven make a string without one. */
static const char df_exbytes[4] = { 'a', '\r', '\n', '\032' };
static const char df_rndbytes[8] = {
    'a', 'b', '\r', '\n', '\t', '\200', '\377', '\032'
};

/* The string read, NUL-terminated (it holds no NUL), and its length. */
static char g_df_data[DF_RANDMAX + 1];
static u16 g_df_len;
/* The block holding it between RD_GUARD line ends on each side, the two
   handles on RD_FILE holding it, and the buffers and the pieces or
   lines of the reader [0] and the reference [1]. */
static cmem_blk *g_df_blk;
static int g_df_h[2];
static char g_df_buf[2][DF_BUFMAX];
static char g_df_out[2][DF_MAXTOP + 2];
static u16 g_df_shown;

static void ref_init(msg_rd *r, int h, char *buf, u16 bufsz)
{
    r->buf = buf;
    r->blk = NULL;
    r->h = h;
    r->size = bufsz;
    r->pos = 0;
    r->got = 0;
    r->eof = 0;
    r->cr = 0;
}

static void ref_blk(msg_rd *r, cmem_blk *b, u16 from, u16 to, char *buf,
                    u16 bufsz)
{
    ref_init(r, -1, buf, bufsz);
    r->blk = b;
    r->at = from;
    r->end = to;
}

static int ref_piece(msg_rd *r, char *p, u16 max, u16 *n, int *eol)
{
    int any;
    int rc;
    char c;

    any = r->cr;
    *n = 0;
    *eol = 0;
    for (;;) {
        if (r->pos >= r->got) {
            if (r->eof)
                break;
            if (r->blk != NULL) {
                r->got = (u16)(r->end - r->at);
                if (r->got > r->size)
                    r->got = r->size;
                cmem_get(r->blk, r->at, r->buf, r->got);
                r->at = (u16)(r->at + r->got);
            } else {
                rc = cf_read(r->h, r->buf, r->size, &r->got);
                if (rc != 0)
                    return -rc;
            }
            r->pos = 0;
            if (r->got == 0) {
                r->eof = 1;
                break;
            }
        }
        c = r->buf[r->pos];
        if (c == '\032') {
            r->eof = 1;
            break;
        }
        if (c == '\n') {
            r->pos++;
            r->cr = 0;
            *eol = 1;
            return 1;
        }
        /* A CR is held back until the next byte shows whether it ends
           the line; c is then looked at again. */
        if (r->cr) {
            if (*n >= max)
                return 1;
            p[(*n)++] = '\r';
            r->cr = 0;
            continue;
        }
        any = 1;
        if (c == '\r') {
            r->pos++;
            r->cr = 1;
            continue;
        }
        if (*n >= max)
            return 1;
        r->pos++;
        p[(*n)++] = c;
    }
    r->cr = 0;
    *eol = any;
    return any;
}

static int ref_line(msg_rd *r, char *line, u16 max, u16 *len)
{
    char skip[DF_SKIPBUF];
    u16 n;
    int eol;
    int rc;

    rc = ref_piece(r, line, max, len, &eol);
    if (rc <= 0)
        return rc;
    line[*len] = '\0';
    if (!eol)
        *len = (u16)(max + 1);
    while (!eol) {
        rc = ref_piece(r, skip, (u16)sizeof skip, &n, &eol);
        if (rc <= 0)
            return rc;
    }
    return 1;
}

/* The offset in its block of the next byte the block reader r has not
   given, as msg_load reckons it after a line. */
static u16 df_tell(const msg_rd *r)
{
    return (u16)(r->at - r->got + r->pos);
}

/* Show the result, length, line end and place that the reader (got)
   and the reference (want) gave at call k of a run, for the first
   DF_SHOW runs that differ. */
static void df_show(const char *what, u16 bufsz, u16 max, int blk,
                    u16 k, const long *got, const long *want)
{
    u16 i;

    if (g_df_shown >= DF_SHOW)
        return;
    g_df_shown++;
    printf("  %s on a %s, buffer %u, max %u, call %u: got %ld %ld %ld "
           "at %ld, want %ld %ld %ld at %ld; data", what,
           blk ? "block" : "handle", (unsigned)bufsz, (unsigned)max,
           (unsigned)k, got[0], got[1], got[2], got[3], want[0],
           want[1], want[2], want[3]);
    for (i = 0; i < g_df_len && i < 48; i++)
        printf(" %02X", (unsigned)(u8)g_df_data[i]);
    printf("%s\n", i < g_df_len ? " ..." : "");
}

/* One reading of the string by the reader and the reference side by
   side, through buffers of bufsz bytes, in lines (lines 1) or pieces of
   at most max bytes, on the block (blk 1) or the handles; 1 when every
   call gave the same, the end included and kept on the call after it. */
static int df_run(u16 bufsz, u16 max, int blk, int lines)
{
    msg_rd r[2];
    long res[2][4];
    u16 n[2];
    u16 k;
    u16 cmp;
    int eol[2];
    int rc[2];
    int ends;
    int i;

    ends = 0;
    memset(g_df_buf, '\n', sizeof g_df_buf);
    if (blk) {
        msg_rd_blk(&r[0], g_df_blk, RD_GUARD, (u16)(RD_GUARD + g_df_len),
                   g_df_buf[0], bufsz);
        ref_blk(&r[1], g_df_blk, RD_GUARD, (u16)(RD_GUARD + g_df_len),
                g_df_buf[1], bufsz);
    } else {
        if (cf_seek(g_df_h[0], 0) != 0 || cf_seek(g_df_h[1], 0) != 0)
            return 0;
        msg_rd_init(&r[0], g_df_h[0], g_df_buf[0], bufsz);
        ref_init(&r[1], g_df_h[1], g_df_buf[1], bufsz);
    }
    for (k = 0; k <= 2 * g_df_len + 2; k++) {
        memset(g_df_out, '#', sizeof g_df_out);
        n[0] = n[1] = 999;
        eol[0] = eol[1] = 1;
        if (lines) {
            rc[0] = msg_rd_line(&r[0], g_df_out[0], max, &n[0]);
            rc[1] = ref_line(&r[1], g_df_out[1], max, &n[1]);
        } else {
            rc[0] = msg_rd_piece(&r[0], g_df_out[0], max, &n[0], &eol[0]);
            rc[1] = ref_piece(&r[1], g_df_out[1], max, &n[1], &eol[1]);
        }
        /* The place is compared after a line and at the end, where
           msg_load reads it; inside a line it may differ. */
        for (i = 0; i < 2; i++) {
            res[i][0] = rc[i];
            res[i][1] = rc[i] > 0 ? (long)n[i] : 0;
            res[i][2] = rc[i] > 0 ? (long)eol[i] : 0;
            res[i][3] = blk && (rc[i] <= 0 || eol[i]) ? df_tell(&r[i]) : 0;
        }
        /* A piece is its n bytes; a line its bytes and the NUL after
           them, a cut one its first max. */
        cmp = !lines ? n[1] : n[1] > max ? (u16)(max + 1) : (u16)(n[1] + 1);
        if (memcmp(res[0], res[1], sizeof res[0]) != 0 ||
            g_df_out[0][max + lines] != '#' ||
            (rc[1] > 0 && memcmp(g_df_out[0], g_df_out[1], cmp) != 0)) {
            df_show(lines ? "lines" : "pieces", bufsz, max, blk, k,
                    res[0], res[1]);
            return 0;
        }
        if (rc[1] <= 0 && ++ends == 2)
            return 1;
    }
    df_show("no end", bufsz, max, blk, k, res[0], res[1]);
    return 0;
}

/* g_df_data, of g_df_len bytes, read every way df_run has, from a
   handle too through the buffers of hdmin bytes or more (DF_NOHD: none);
   0 when one of them differs. */
static int df_string(u16 hdmin)
{
    u16 b;
    u16 m;
    int blk;
    int lines;
    int hd;
    int ok;

    ok = 1;
    hd = hdmin <= DF_BUFMAX;
    g_df_data[g_df_len] = '\0';
    cmem_set(g_df_blk, 0, '\n', (u16)(DF_RANDMAX + 2 * RD_GUARD));
    if (g_df_len > 0)
        cmem_put(g_df_blk, RD_GUARD, g_df_data, g_df_len);
    if (hd && (!TF_EQ(write_rd(g_df_data), 0) ||
               !TF_EQ(cf_open(g_rd_path, CF_READ, &g_df_h[0]), 0)))
        return 0;
    if (hd && !TF_EQ(cf_open(g_rd_path, CF_READ, &g_df_h[1]), 0)) {
        cf_close(g_df_h[0]);
        return 0;
    }
    for (b = 0; b < DF_NBUFS && (b == 0 || df_bufs[b - 1] <= g_df_len);
         b++) {
        for (m = 0; m < DF_NMAXES &&
                    (m == 0 || df_maxes[m - 1] <= g_df_len); m++) {
            for (blk = df_bufs[b] < hdmin; blk <= 1; blk++) {
                for (lines = 0; lines <= 1; lines++) {
                    if (!df_run(df_bufs[b], df_maxes[m], blk, lines))
                        ok = 0;
                }
            }
        }
    }
    if (hd) {
        cf_close(g_df_h[0]);
        cf_close(g_df_h[1]);
    }
    return ok;
}

/* Every string of 0..DF_EXLEN bytes over df_exbytes, then DF_NRAND of
   0..DF_RANDMAX bytes from a fixed linear congruential sequence, every
   other one without a Ctrl-Z, so that its lines run to its end. */
static void test_reader_ref(void)
{
    u32 seed;
    u32 code;
    u32 count;
    u16 len;
    u16 i;
    u16 j;
    u16 bad;

    bad = 0;
    seed = 1;
    g_df_shown = 0;
    g_df_blk = cmem_alloc(DF_RANDMAX + 2 * RD_GUARD);
    if (!TF_CHECK(g_df_blk != NULL))
        return;
    for (len = 0; len <= DF_EXLEN; len++) {
        count = (u32)1 << (2 * len);
        for (code = 0; code < count; code++) {
            for (i = 0; i < len; i++)
                g_df_data[i] = df_exbytes[(u16)(code >> (2 * i)) & 3];
            g_df_len = len;
            if (!df_string(len <= DF_HDLEN ? 1 : DF_NOHD))
                bad++;
        }
    }
    for (j = 0; j < DF_NRAND; j++) {
        seed = (u32)(seed * 1103515245UL + 12345);
        g_df_len = (u16)((seed >> 16) % (DF_RANDMAX + 1));
        for (i = 0; i < g_df_len; i++) {
            seed = (u32)(seed * 1103515245UL + 12345);
            g_df_data[i] = df_rndbytes[(u16)(seed >> 16) % (j & 1 ? 7 : 8)];
        }
        if (!df_string(DF_HDRAND))
            bad++;
    }
    TF_EQ(bad, 0);
    cmem_free(g_df_blk);
}

/* msg_fread gives the bytes asked for, and nothing for a range past the
   end of the file. */
static void test_fread(void)
{
    cmem_blk *b;
    char got[8];
    int h;

    if (!TF_EQ(write_rd("0123456789"), 0))
        return;
    if (!TF_EQ(cf_open(g_rd_path, CF_READ, &h), 0))
        return;
    if (TF_EQ(msg_fread(h, 3, 4, &b), 0)) {
        TF_EQ(cmem_size(b), 4);
        cmem_get(b, 0, got, 4);
        TF_CHECK(memcmp(got, "3456", 4) == 0);
        cmem_free(b);
    }
    TF_EQ(msg_fread(h, 8, 3, &b), CF_EINVAL);
    TF_CHECK(b == NULL);
    cf_close(h);
}

/* msg_section finds a key in the index at the head of a file, in any
   case, and reads its section, past a line naming a longer key; a key
   the index lacks, a malformed index line, an empty section, one past
   the end of the file and a missing file give nothing. */
static void test_section(void)
{
    static const char file[] =
        "=ONEX0000 0003\r\n"
        "=ONE 0067 0004\r\n"
        "=TWO  0002 0003\r\n"
        "=Nil 0000 0000\r\n"
        "=Far 0067 0005\r\n"
        "=Two 0000 0003\r\n"
        "text\r\n"
        "ABCD";
    cmem_blk *b;
    char got[8];
    u16 n;

    if (!TF_EQ(write_rd(file), 0))
        return;
    if (TF_EQ(msg_section(g_tdir, RD_FILE, "one", &b, &n), 0)) {
        TF_EQ(n, 4);
        cmem_get(b, 0, got, 4);
        TF_CHECK(memcmp(got, "ABCD", 4) == 0);
        cmem_free(b);
    }
    if (TF_EQ(msg_section(g_tdir, RD_FILE, "TWO", &b, &n), 0)) {
        cmem_get(b, 0, got, 3);
        TF_CHECK(memcmp(got, "=ON", 3) == 0);
        cmem_free(b);
    }
    TF_EQ(msg_section(g_tdir, RD_FILE, "NIL", &b, &n), -1);
    TF_CHECK(b == NULL);
    TF_EQ(msg_section(g_tdir, RD_FILE, "TEXT", &b, &n), -1);
    TF_EQ(msg_section(g_tdir, RD_FILE, "FAR", &b, &n), CF_EINVAL);
    TF_CHECK(b == NULL);
    TF_EQ(msg_section(g_tdir, "NOFILE.HLP", "ONE", &b, &n), CF_ENOENT);
}

/* Remove the files the test writes in g_tdir. */
static void cleanup(void)
{
    char path[PATHMAX + 16];

    strcpy(path, g_tdir);
    strcat(path, MSG_FILE);
    cf_remove(path);
    cf_remove(g_rd_path);
}

int main(int argc, char **argv)
{
    char mdir[PATHMAX];

    tf_begin("T_MSG");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX - 1) {
        printf("usage: T_MSG dir [msgdir]\n");
        return 2;
    }
    strcpy(g_tdir, argv[1]);
    strcpy(g_rd_path, g_tdir);
    strcat(g_rd_path, RD_FILE);
    tf_progdir(mdir, PATHMAX, argv[0]);
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    cleanup();
    test_real_file(mdir);
    test_texts();
    test_cutting();
    test_bare();
    test_malformed();
    test_ctrlz_places();
    test_rawlen();
    test_stamp();
    test_too_large();
    test_pages();
    test_area();
    test_loaded();
    test_blocks(mdir);
    test_load_fails();
    test_swapped();
    test_unescape_ref();
    test_unescape_limits();
    test_reader_cases();
    test_reader_pieces();
    test_reader_error();
    test_reader_ref();
    test_fread();
    test_section();
    test_dates();
    test_decimals();
    test_missing();
    msg_free();
    cleanup();
    return tf_done();
}

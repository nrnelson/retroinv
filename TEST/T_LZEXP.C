/*
 * T_LZEXP.C - unit test for the SZDD expander (LZEXP.C), and the expander
 * of the round trips of TEST/szddchk.py.
 *
 * usage: T_LZEXP [COMP ORIG]
 *
 * The streams are built here, token by token, with the output each must
 * give, which follows from the distance of each match back into the
 * output (blanks before its start); the expander's window positions do
 * not enter it.  They are read through a function that hands out at most
 * a chosen number of bytes at a time, and written through one that
 * compares every byte with that output.  Checks:
 *   - an input shorter than the header, each byte of the magic changed
 *     and a method other than 'A' are LZ_EFORMAT, with nothing written;
 *     the character of the name is not checked;
 *   - the header alone expands to nothing, and a byte after it is
 *     LZ_EFORMAT;
 *   - ROUNDS rounds of literals, a match into the blanks the window
 *     starts with, a run (a match of the byte before) and matches of the
 *     shortest and longest lengths, longer than LZ_INSZ, led by 0 to
 *     SHIFTS - 1 literals so the refill falls at each place of a token:
 *     read 1, 2, 3 and LZ_INSZ bytes at a time;
 *   - every cut of the last of these short of its end is LZ_EFORMAT,
 *     what was written of it right; a stated length one too long is
 *     LZ_EFORMAT, as is one too short (a match past it, nothing written
 *     past it) and a byte after the last token;
 *   - a window wrap: 4100 literals, then a match from the oldest byte the
 *     window holds, in which the write position wraps, one from LZ_WINSZ
 *     bytes back and one whose source straddles the window's end;
 *   - a code of the reader or of the writer, first or later, comes back
 *     as it is, on that stream;
 *   - a long run, one literal and RUNS matches of the byte before, over
 *     LZ_WINSZ twice; cut every CUTSTEP bytes it is LZ_EFORMAT, with
 *     nothing wrong written; cut by a stated length just short of the
 *     window's second write, nothing written past it;
 *   - lz_name gives the names COMPRESS -r gives: the last character of a
 *     three-character extension '_', '_' added to a shorter one, "._"
 *     to a name without one.
 * lz_copy is tested through its callers, by T_INST and T_SETUP.
 * With COMP and ORIG, it also expands the file COMP and compares the
 * result with the file ORIG.
 * Exit status 0 when every check passes, 1 when one fails.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LZEXP.H"
#include "TESTFW.H"

#define COMPMAX         4864    /* bytes of the longest stream          */
#define WANTMAX         8448    /* bytes of the longest output          */
#define HDRSZ           14      /* bytes of the header                  */
#define WRAPN           4100    /* literals of the window wrap          */
#define RUNS            460     /* matches of the long run              */
#define ROUNDS          44      /* rounds of the token stream           */
#define SHIFTS          8       /* literals the token stream may lead   */
#define CUTSTEP         64      /* bytes between two cuts of the run    */
#define POISON          0xEE    /* the work's bytes before each run     */
#define MINLEN          3
#define MAXLEN          18
#define START           (LZ_WINSZ - 16)
#define RERR            5       /* code of the failing reader           */
#define WERR            7       /* code of the failing writer           */

static const u8 g_head[HDRSZ - 4] = {
    'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33, 'A', 'E'
};

/* The stream and the output it gives. */
static u8 g_comp[COMPMAX];
static u16 g_clen;
static u16 g_flag;                      /* its open flag byte           */
static int g_bits;                      /* tokens under that byte       */
static u8 g_want[WANTMAX];
static u16 g_wlen;

typedef struct {
    u16 at;                     /* next byte of g_comp read             */
    u16 end;                    /* bytes of g_comp there are to read    */
    u16 piece;                  /* most bytes one read gives            */
    u16 rfail;                  /* offset whose read fails, 0 for none  */
    u16 wfail;                  /* write number that fails, 0 for none  */
    u16 writes;
    u32 out;                    /* bytes written                        */
    int bad;                    /* a byte written was not g_want's      */
} mem_io;

static mem_io g_io;
static lz_work g_work;

static int mem_read(void *io, u8 *buf, u16 max, u16 *got)
{
    mem_io *m;
    u16 n;

    m = (mem_io *)io;
    if (m->rfail != 0 && m->at >= m->rfail)
        return RERR;
    n = (u16)(m->end - m->at);
    if (n > max)
        n = max;
    if (n > m->piece)
        n = m->piece;
    memcpy(buf, g_comp + m->at, n);
    m->at = (u16)(m->at + n);
    *got = n;
    return 0;
}

static int mem_write(void *io, const u8 *buf, u16 n)
{
    mem_io *m;

    m = (mem_io *)io;
    m->writes++;
    if (m->wfail != 0 && m->writes >= m->wfail)
        return WERR;
    if (m->out + n > g_wlen || memcmp(buf, g_want + (u16)m->out, n) != 0)
        m->bad = 1;
    m->out += n;
    return 0;
}

/* lz_expand's code for the first end bytes of g_comp read piece bytes at
   a time, its work filled with POISON first, so that no byte the
   expander did not read or set passes for one it did; g_io holds what
   was written. */
static int expand(u16 end, u16 piece)
{
    memset(&g_io, 0, sizeof g_io);
    memset(&g_work, POISON, sizeof g_work);
    g_io.end = end;
    g_io.piece = piece;
    return lz_expand(&g_work, mem_read, mem_write, &g_io);
}

/* 1 when the whole stream, read piece bytes at a time, gives the whole
   output. */
static int expands(u16 piece)
{
    return TF_EQ(expand(g_clen, piece), LZ_OK) &&
           TF_EQ(g_io.out, g_wlen) && TF_CHECK(!g_io.bad);
}

/* ---------------------------------------------------------------------
 * The stream builder.
 * --------------------------------------------------------------------- */

static void st_begin(void)
{
    memcpy(g_comp, g_head, sizeof g_head);
    g_clen = HDRSZ;
    g_bits = 8;
    g_wlen = 0;
}

/* State the output's length in the header. */
static void st_end(void)
{
    put_le32(g_comp + HDRSZ - 4, g_wlen);
}

static void st_flag(int literal)
{
    if (g_bits == 8) {
        g_flag = g_clen;
        g_comp[g_clen++] = 0;
        g_bits = 0;
    }
    if (literal)
        g_comp[g_flag] = (u8)(g_comp[g_flag] | (1 << g_bits));
    g_bits++;
}

static void st_lit(u8 c)
{
    st_flag(1);
    g_comp[g_clen++] = c;
    g_want[g_wlen++] = c;
}

/* A match of len bytes from window position pos: its distance back is
   that of pos from where the next byte goes, LZ_WINSZ when they are the
   same, and a byte before the start of the output is a blank. */
static void st_match(u16 pos, u16 len)
{
    u16 d;
    u16 i;

    st_flag(0);
    g_comp[g_clen++] = (u8)pos;
    g_comp[g_clen++] = (u8)(((pos >> 4) & 0xF0) | (len - MINLEN));
    d = (u16)((START + g_wlen - pos) & (LZ_WINSZ - 1));
    if (d == 0)
        d = LZ_WINSZ;
    for (i = 0; i < len; i++) {
        g_want[g_wlen] = (u8)(g_wlen >= d ? g_want[g_wlen - d] : ' ');
        g_wlen++;
    }
}

/* A match of len bytes from d bytes back. */
static void st_back(u16 d, u16 len)
{
    st_match((u16)((START + g_wlen - d) & (LZ_WINSZ - 1)), len);
}

/* ---------------------------------------------------------------------
 * The tests.
 * --------------------------------------------------------------------- */

static void test_header(void)
{
    u16 i;

    st_begin();
    st_lit('O');
    st_lit('K');
    st_end();
    TF_CHECK(expands(LZ_INSZ));
    for (i = 0; i < HDRSZ; i++)
        TF_CHECK(expand(i, LZ_INSZ) == LZ_EFORMAT && g_io.writes == 0);
    for (i = 0; i <= 8; i++) {
        g_comp[i] ^= 0x01;
        TF_CHECK(expand(g_clen, LZ_INSZ) == LZ_EFORMAT &&
                 g_io.writes == 0);
        g_comp[i] ^= 0x01;
    }
    g_comp[9] = 0;
    TF_CHECK(expands(LZ_INSZ));
}

static void test_empty(void)
{
    st_begin();
    st_end();
    TF_CHECK(expands(LZ_INSZ) && g_io.writes == 0);
    g_comp[g_clen] = 0;
    TF_EQ(expand((u16)(g_clen + 1), LZ_INSZ), LZ_EFORMAT);
}

static void test_tokens(void)
{
    static const u16 pieces[] = { 1, 2, 3, LZ_INSZ };
    static const char text[] = "Window blanks:";
    u16 i;
    u16 n;
    u16 r;
    u16 lead;

    for (lead = 0; lead < SHIFTS; lead++) {
        st_begin();
        for (i = 0; i < lead; i++)
            st_lit('<');
        for (r = 0; r < ROUNDS; r++) {
            for (i = 0; text[i] != '\0'; i++)
                st_lit((u8)text[i]);
            st_back((u16)(g_wlen + 4), 4);
            st_lit('*');
            st_back(1, MAXLEN);
            st_back(20, MINLEN);
            st_back(30, MAXLEN);
        }
        st_end();
        for (i = 0; i < sizeof pieces / sizeof pieces[0]; i++)
            TF_CHECK(expands(pieces[i]));
    }
    n = 0;
    for (i = 0; i < g_clen; i++)
        if (expand(i, LZ_INSZ) != LZ_EFORMAT || g_io.bad)
            n++;
    TF_EQ(n, 0);
    put_le32(g_comp + HDRSZ - 4, g_wlen + 1);
    TF_EQ(expand(g_clen, LZ_INSZ), LZ_EFORMAT);
    put_le32(g_comp + HDRSZ - 4, g_wlen - 1);
    TF_CHECK(expand(g_clen, LZ_INSZ) == LZ_EFORMAT && !g_io.bad &&
             g_io.out < g_wlen);
    st_end();
    g_comp[g_clen] = 0;
    TF_EQ(expand((u16)(g_clen + 1), LZ_INSZ), LZ_EFORMAT);
}

static void test_wrap(void)
{
    u16 i;

    st_begin();
    for (i = 0; i < WRAPN; i++)
        st_lit((u8)(i * 7 + (i >> 8)));
    st_back(LZ_WINSZ - 1, MAXLEN);
    st_back(LZ_WINSZ, MAXLEN);
    st_match(LZ_WINSZ - 6, MAXLEN);
    st_end();
    TF_CHECK(expands(LZ_INSZ));
    TF_CHECK(expands(1));
}

static void test_run(void)
{
    u16 i;
    u16 n;
    u32 len;

    st_begin();
    st_lit('x');
    for (i = 0; i < RUNS; i++)
        st_back(1, MAXLEN);
    st_end();
    TF_CHECK(expands(LZ_INSZ));
    n = 0;
    for (i = HDRSZ; i < g_clen; i = (u16)(i + CUTSTEP))
        if (expand(i, LZ_INSZ) != LZ_EFORMAT || g_io.bad)
            n++;
    TF_EQ(n, 0);
    for (len = LZ_WINSZ + 15; len <= LZ_WINSZ + 17; len++) {
        put_le32(g_comp + HDRSZ - 4, len);
        TF_CHECK(expand(g_clen, LZ_INSZ) == LZ_EFORMAT && !g_io.bad &&
                 g_io.out <= len);
    }
    st_end();
    TF_CHECK(expands(LZ_INSZ));
}

static void test_codes(void)
{
    memset(&g_io, 0, sizeof g_io);
    g_io.end = g_clen;
    g_io.piece = LZ_INSZ;
    g_io.rfail = 1;
    TF_EQ(lz_expand(&g_work, mem_read, mem_write, &g_io), RERR);
    g_io.at = 0;
    g_io.rfail = LZ_INSZ;
    TF_CHECK(lz_expand(&g_work, mem_read, mem_write, &g_io) == RERR &&
             g_io.writes != 0);
    memset(&g_io, 0, sizeof g_io);
    g_io.end = g_clen;
    g_io.piece = LZ_INSZ;
    g_io.wfail = 1;
    TF_EQ(lz_expand(&g_work, mem_read, mem_write, &g_io), WERR);
    memset(&g_io, 0, sizeof g_io);
    g_io.end = g_clen;
    g_io.piece = LZ_INSZ;
    g_io.wfail = 3;
    TF_CHECK(lz_expand(&g_work, mem_read, mem_write, &g_io) == WERR &&
             g_io.writes == 3);
}

/* ---------------------------------------------------------------------
 * A file and its original.
 * --------------------------------------------------------------------- */

typedef struct {
    FILE *comp;
    FILE *orig;
    int bad;                    /* a byte differed from the original    */
} file_io;

static int file_read(void *io, u8 *buf, u16 max, u16 *got)
{
    file_io *f;

    f = (file_io *)io;
    *got = (u16)fread(buf, 1, max, f->comp);
    return ferror(f->comp) ? RERR : 0;
}

static int file_write(void *io, const u8 *buf, u16 n)
{
    file_io *f;
    u16 i;

    f = (file_io *)io;
    for (i = 0; i < n; i++)
        if (getc(f->orig) != buf[i])
            f->bad = 1;
    return 0;
}

static void test_file(const char *comp, const char *orig)
{
    file_io f;

    f.comp = fopen(comp, "rb");
    f.orig = fopen(orig, "rb");
    f.bad = 0;
    if (TF_CHECK(f.comp != NULL && f.orig != NULL)) {
        TF_EQ(lz_expand(&g_work, file_read, file_write, &f), LZ_OK);
        TF_CHECK(!f.bad && getc(f.orig) == EOF);
    }
    if (f.comp != NULL)
        fclose(f.comp);
    if (f.orig != NULL)
        fclose(f.orig);
}

static void test_name(void)
{
    static const char *const names[][2] = {
        { "INV.EXE", "INV.EX_" }, { "DATA", "DATA._" }, { "A.B", "A.B_" },
        { "ABCDEFGH.IJ", "ABCDEFGH.IJ_" }
    };
    char name[sizeof "ABCDEFGH.IJ_"];
    int i;

    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        strcpy(name, names[i][0]);
        lz_name(name);
        TF_STREQ(name, names[i][1]);
    }
}

int main(int argc, char **argv)
{
    tf_begin("T_LZEXP");
    test_header();
    test_empty();
    test_tokens();
    test_wrap();
    test_codes();
    test_run();
    test_name();
    if (argc == 3)
        test_file(argv[1], argv[2]);
    return tf_done();
}

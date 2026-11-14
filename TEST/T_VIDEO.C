/*
 * T_VIDEO.C - unit test for the text screen of COMPAT.H (vid_*).
 *
 * usage: T_VIDEO [dir] [PAGE1 | WIDE]
 *
 * The screen is opened once per output mode: direct, direct with CGA
 * snow avoidance, and BIOS (INT 10h), and on the host a last time as a
 * monochrome adapter (VID_F_MDA).  After every flush vid_verify must find
 * the adapter holding the shadow; on the host the screen is the shadow
 * alone, so vid_verify always matches.  Under DOS the program's output
 * must be redirected, since it shares the screen.
 *
 * Under DOS the program first paints the screen through the console
 * device (CON, so that it reaches the screen whatever the standard
 * handles are): PAINT_LINES lines of VID_COLS characters, which leave
 * every row but the blank bottom one full of known characters, all in
 * the attribute the console's scrolling fills in.  With the argument
 * PAGE1 it does not paint: the screen was painted, with attribute 07h,
 * by PAGE1.COM (TEST/INTEG/PAGE1.ASM), which then made page 1 the
 * active page; the MS-DOS 3.10 boot that does so checks after the
 * program that page 1 is as PAGE1.COM left it.  With the argument WIDE,
 * after PAGE1 WIDE (the screen painted, mode 3 kept, but 81 columns in
 * the BIOS data area), the program only requires the first open to show
 * a cleared screen: vid_open must have set the mode, since a wider page
 * would overlap page 1.  The dir argument of the DOS runs is ignored.
 *
 * Checks, in each pass:
 * - vid_open succeeds, also a second time, and reports an adapter
 *   (on the host a CGA, or an MDA when asked, with vid_mono to match);
 * - the opened screen is cleared, and its first flush counts every cell;
 * - vid_put writes and clips at the right and bottom edges and ignores
 *   cells off the screen; under DOS a change not yet flushed is reported
 *   at its own cell, so the changed-cell spans are shown to reach the
 *   screen; vid_flushed counts exactly the span of the changed cells of
 *   each row, nothing after writes that leave every cell as it was;
 * - vid_fill clips, and a change of attribute alone marks its cells;
 * - the attributes of the colour and monochrome palettes survive;
 * - vid_save and vid_restore bring back a box, also cut at the edges,
 *   and a save off the screen holds only its header;
 * - vid_cursor sets the position and shape that vid_cursor_get reports,
 *   and a flush leaves them;
 * - vid_invalidate makes the next flush count every cell, and while snow
 *   avoidance is on vid_snow_active reports it exactly on a CGA outside
 *   BIOS mode (the host's virtual CGA included), so a DOSBox-X CGA run
 *   shows that no snow wait timed out;
 * - after vid_close, also a second one, snow avoidance is off and
 *   vid_save refuses;
 * - after a last frame that changes every cell, vid_close puts back
 *   the whole painted screen, which a new open then finds cell for cell
 *   (on the host, where vid_verify always matches, this shows nothing).
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"

#define LAST_CH         0xB1    /* every cell of the frame drawn last   */
#define LAST_AT         0x5A
#define PAINT_LINES     50      /* lines painted: every row scrolled in */
#define PAINT_TOP       (PAINT_LINES - (VID_ROWS - 1))  /* line on row 0 */
#define PAINT_SET       94      /* painted characters: 21h to 7Eh       */

static u8 g_row[VID_ROWBYTES];
static u8 g_attr;               /* attribute of the painted screen      */
static int g_attrok;            /* g_attr has been found                */

/* The character and attribute of one shadow cell. */
static u8 cell_ch(u8 row, u8 col)
{
    vid_getrow(row, g_row);
    return g_row[col * 2];
}

static u8 cell_at(u8 row, u8 col)
{
    vid_getrow(row, g_row);
    return g_row[col * 2 + 1];
}

/* Flush and require the adapter to match the shadow. */
static void flush_ok(const char *what)
{
    u8 r;
    u8 c;

    vid_flush();
    if (!TF_EQ(vid_verify(&r, &c), 0))
        printf("%s: screen differs at row %u col %u\n", what,
               (unsigned)r, (unsigned)c);
}

/* Under DOS, a change not flushed must show at (row, col). */
static void unflushed_at(u8 row, u8 col)
{
#ifdef COMPAT_HOST
    TF_CHECK(row < VID_ROWS && col < VID_COLS);
#else
    u8 r;
    u8 c;

    if (TF_EQ(vid_verify(&r, &c), 1)) {
        TF_EQ(r, row);
        TF_EQ(c, col);
    }
#endif
}

static void test_clear(void)
{
    u8 r;
    u8 c;
    int ok;

    ok = 1;
    for (r = 0; r < VID_ROWS; r++) {
        vid_getrow(r, g_row);
        for (c = 0; c < VID_COLS; c++) {
            if (g_row[c * 2] != ' ' || g_row[c * 2 + 1] != 0x07)
                ok = 0;
        }
    }
    TF_CHECK(ok);
    flush_ok("open");
    TF_EQ(vid_flushed(), VID_CELLS);
    vid_flush();
    TF_EQ(vid_flushed(), 0);
}

static void test_put(void)
{
    vid_put(0, 0, "Hello", 5, 0x1F);
    unflushed_at(0, 0);
    TF_EQ(cell_ch(0, 0), 'H');
    TF_EQ(cell_ch(0, 4), 'o');
    TF_EQ(cell_at(0, 4), 0x1F);
    TF_EQ(cell_ch(0, 5), ' ');
    TF_EQ(cell_at(0, 5), 0x07);
    flush_ok("put");
    TF_EQ(vid_flushed(), 5);

    /* Cut at the right edge; rows and columns off the screen ignored. */
    vid_put(3, 75, "ABCDEFGHIJ", 10, 0x70);
    TF_EQ(cell_ch(3, 75), 'A');
    TF_EQ(cell_ch(3, 79), 'E');
    TF_EQ(cell_ch(4, 0), ' ');
    vid_put(VID_ROWS, 0, "X", 1, 0x70);
    vid_put(5, VID_COLS, "X", 1, 0x70);
    vid_put(255, 255, "X", 1, 0x70);
    vid_fill(VID_ROWS, 0, VID_COLS, 'X', 0x70);
    TF_EQ(cell_ch(5, 0), ' ');
    TF_EQ(cell_ch(VID_ROWS - 1, VID_COLS - 1), ' ');
    flush_ok("clip");
    TF_EQ(vid_flushed(), 5);

    /* The same text again changes nothing; a later change in the
       middle of a row shows at its cell. */
    vid_put(0, 0, "Hello", 5, 0x1F);
    vid_put(0, 1, "el", 2, 0x1F);
    vid_fill(1, 0, VID_COLS, ' ', 0x07);
    flush_ok("rewrite");
    TF_EQ(vid_flushed(), 0);
    vid_put(10, 40, "Z", 1, 0x07);
    unflushed_at(10, 40);
    flush_ok("middle");
    TF_EQ(vid_flushed(), 1);

    /* Two changes in a row make one span; one in another row its own. */
    vid_put(14, 20, "b", 1, 0x07);
    vid_put(14, 10, "a", 1, 0x07);
    vid_put(14, 15, " ", 1, 0x07);
    vid_put(9, 79, "c", 1, 0x07);
    unflushed_at(9, 79);
    flush_ok("spans");
    TF_EQ(vid_flushed(), 11 + 1);

    /* Bytes are characters, NUL and control codes included. */
    vid_put(11, 0, "\0\r\n\x1B\xDB", 5, 0x0F);
    TF_EQ(cell_ch(11, 0), 0x00);
    TF_EQ(cell_ch(11, 1), 0x0D);
    TF_EQ(cell_ch(11, 3), 0x1B);
    TF_EQ(cell_ch(11, 4), 0xDB);
    flush_ok("control");
}

static void test_fill(void)
{
    u8 c;
    int ok;

    vid_fill(6, 10, 30, 0xC4, 0x1B);
    TF_EQ(cell_ch(6, 9), ' ');
    TF_EQ(cell_ch(6, 10), 0xC4);
    TF_EQ(cell_ch(6, 39), 0xC4);
    TF_EQ(cell_ch(6, 40), ' ');
    vid_fill(7, 70, 500, '*', 0x4F);
    TF_EQ(cell_ch(7, 79), '*');
    TF_EQ(cell_ch(8, 0), ' ');
    flush_ok("fill");

    /* The same characters with a new attribute are a change. */
    vid_fill(6, 20, 5, 0xC4, 0x30);
    unflushed_at(6, 20);
    ok = 1;
    for (c = 20; c < 25; c++) {
        if (cell_ch(6, c) != 0xC4 || cell_at(6, c) != 0x30)
            ok = 0;
    }
    TF_CHECK(ok);
    TF_EQ(cell_at(6, 19), 0x1B);
    TF_EQ(cell_at(6, 25), 0x1B);
    flush_ok("attribute");
    TF_EQ(vid_flushed(), 5);
}

/* Every attribute of the three palettes survives in the shadow and on
   the screen. */
static const u8 pal_color[] = {
    0x17, 0x1F, 0x1B, 0x1E, 0x30, 0x70, 0x78, 0x74, 0x07, 0x4F
};
static const u8 pal_mono[] = { 0x07, 0x0F, 0x70, 0x01, 0x09 };

static void test_palette(void)
{
    u8 i;

    for (i = 0; i < sizeof pal_color; i++)
        vid_fill(12, (u8)(i * 4), 4, (u8)('a' + i), pal_color[i]);
    for (i = 0; i < sizeof pal_mono; i++)
        vid_fill(13, (u8)(i * 4), 4, (u8)('A' + i), pal_mono[i]);
    for (i = 0; i < sizeof pal_color; i++)
        TF_EQ(cell_at(12, (u8)(i * 4 + 3)), pal_color[i]);
    for (i = 0; i < sizeof pal_mono; i++)
        TF_EQ(cell_at(13, (u8)(i * 4 + 3)), pal_mono[i]);
    flush_ok("palette");
}

/* A save at row, col that lies off the screen holds only its header,
   and restoring it leaves every shadow cell as it was. */
static void off_save(u8 row, u8 col, u8 h, u8 w)
{
    static u8 before[VID_ROWS][VID_ROWBYTES];
    cmem_blk *b;
    u8 r;
    int same;

    for (r = 0; r < VID_ROWS; r++)
        vid_getrow(r, before[r]);
    b = vid_save(row, col, h, w);
    if (!TF_CHECK(b != NULL))
        return;
    TF_EQ(cmem_size(b), 4);
    vid_restore(b);
    same = 1;
    for (r = 0; r < VID_ROWS; r++) {
        vid_getrow(r, g_row);
        if (memcmp(before[r], g_row, VID_ROWBYTES) != 0)
            same = 0;
    }
    TF_CHECK(same);
}

static void test_save(void)
{
    cmem_blk *b;
    u8 before[VID_ROWBYTES];
    u8 r;

    vid_put(15, 5, "under the box", 13, 0x1E);
    vid_put(16, 5, "second line", 11, 0x17);
    flush_ok("before box");
    b = vid_save(14, 4, 4, 20);
    if (!TF_CHECK(b != NULL))
        return;
    TF_EQ(cmem_size(b), 4 + 4 * 20 * 2);
    vid_getrow(15, before);
    for (r = 14; r < 18; r++)
        vid_fill(r, 4, 20, 0xB0, 0x70);
    TF_EQ(cell_ch(15, 5), 0xB0);
    flush_ok("box");
    vid_restore(b);
    vid_getrow(15, g_row);
    TF_CHECK(memcmp(before, g_row, VID_ROWBYTES) == 0);
    TF_EQ(cell_ch(16, 5), 's');
    TF_EQ(cell_at(17, 10), 0x07);
    flush_ok("restore");
    TF_EQ(vid_flushed(), 4 * 20);

    /* Cut at the bottom right corner: 2 rows of 3 cells. */
    vid_put(23, 77, "xyz", 3, 0x0F);
    vid_put(24, 77, "uvw", 3, 0x0F);
    b = vid_save(23, 77, 10, 10);
    if (TF_CHECK(b != NULL))
        TF_EQ(cmem_size(b), 4 + 2 * 3 * 2);
    vid_fill(23, 70, 10, '#', 0x70);
    vid_fill(24, 70, 10, '#', 0x70);
    vid_restore(b);
    TF_EQ(cell_ch(23, 77), 'x');
    TF_EQ(cell_ch(24, 79), 'w');
    TF_EQ(cell_ch(24, 76), '#');
    flush_ok("corner");

    /* Off the screen by row, by column or by both: an empty save, and
       a restore that changes no cell. */
    flush_ok("before empty saves");
    off_save(VID_ROWS, 0, 3, 3);
    off_save(5, VID_COLS, 3, 3);
    off_save(255, 255, 30, 90);
    vid_restore(NULL);
    flush_ok("empty save");
    TF_EQ(vid_flushed(), 0);
}

static void test_cursor(void)
{
    u8 r;
    u8 c;
    u8 s;

    vid_cursor(5, 12, VID_CUR_LINE);
    vid_cursor_get(&r, &c, &s);
    TF_EQ(r, 5);
    TF_EQ(c, 12);
    TF_EQ(s, VID_CUR_LINE);
    vid_cursor(24, 79, VID_CUR_BLOCK);
    vid_cursor_get(&r, &c, &s);
    TF_EQ(r, 24);
    TF_EQ(c, 79);
    TF_EQ(s, VID_CUR_BLOCK);
    vid_put(20, 0, "cursor", 6, 0x07);
    flush_ok("cursor");
    vid_cursor_get(&r, &c, &s);
    TF_EQ(r, 24);
    TF_EQ(c, 79);
    vid_cursor(0, 0, VID_CUR_HIDE);
    vid_cursor_get(&r, &c, &s);
    TF_EQ(s, VID_CUR_HIDE);
}

static void test_invalidate(u8 flags)
{
    int snowy;

    snowy = (vid_adapter() == VID_A_CGA && !(flags & VID_F_BIOS)) ? 1 : 0;
    TF_EQ(vid_snow_active(), (flags & VID_F_SNOW) ? snowy : 0);
    vid_invalidate();
    flush_ok("invalidate");
    TF_EQ(vid_flushed(), VID_CELLS);
    vid_snow(1);
    vid_put(21, 0, "snow on", 7, 0x1F);
    flush_ok("snow on");
    vid_invalidate();
    flush_ok("snow on, all");
    if (!TF_EQ(vid_snow_active(), snowy))
        printf("snow avoidance off: a wait timed out?\n");
    vid_snow(0);
    TF_EQ(vid_snow_active(), 0);
    vid_put(21, 0, "snow off", 8, 0x1F);
    flush_ok("snow off");
}

/* Draw the last frame of a pass, in which every cell changes, so that
   vid_close is shown to put back the whole DOS screen. */
static void last_frame(void)
{
    u8 r;

    for (r = 0; r < VID_ROWS; r++)
        vid_fill(r, 0, VID_COLS, LAST_CH, LAST_AT);
}

/* The n-th character painted. */
static char paint_ch(u16 n)
{
    return (char)(0x21 + n % PAINT_SET);
}

/* Paint the screen as described at the top (the host has none).  The
   leading CR starts the first line at column 0; each line fills its
   row, so the cursor wraps to the next without a line end, and after
   the last it scrolls once more. */
static void paint(void)
{
#ifndef COMPAT_HOST
    FILE *f;
    u16 n;

    f = fopen("CON", "w");
    if (!TF_CHECK(f != NULL))
        return;
    fputc('\r', f);
    for (n = 0; n < PAINT_LINES * VID_COLS; n++)
        fputc(paint_ch(n), f);
    fclose(f);
#endif
}

/* Fill the shadow with the painted screen in attribute attr. */
static void paint_shadow(u8 attr)
{
    char line[VID_COLS];
    u8 r;
    u8 c;

    for (r = 0; r < VID_ROWS - 1; r++) {
        for (c = 0; c < VID_COLS; c++)
            line[c] = paint_ch((u16)((PAINT_TOP + r) * VID_COLS + c));
        vid_put(r, 0, line, VID_COLS, attr);
    }
    vid_fill(VID_ROWS - 1, 0, VID_COLS, ' ', attr);
}

/* Open and require the painted screen in every cell.  Its attribute is
   found on the first call, by trying each in turn. */
static void test_dos(u8 flags)
{
    u16 a;
    u8 r;
    u8 c;

    if (!TF_EQ(vid_open(flags), 0))
        return;
    for (a = 0; a < 256 && !g_attrok; a++) {
        paint_shadow((u8)a);
        if (vid_verify(&r, &c) == 0) {
            g_attr = (u8)a;
            g_attrok = 1;
#ifndef COMPAT_HOST
            printf("painted screen: attribute %02X\n", (unsigned)a);
#endif
        }
    }
    TF_CHECK(g_attrok);
    paint_shadow(g_attr);
    if (!TF_EQ(vid_verify(&r, &c), 0))
        printf("DOS screen differs at row %u col %u\n", (unsigned)r,
               (unsigned)c);
    vid_close();
}

static const char *const pass_name[] = { "direct", "snow", "bios", "mda" };

static void pass(u8 flags, int k)
{
    int rc;

    rc = vid_open(flags);
    if (!TF_EQ(rc, 0))
        return;
    printf("%s: adapter %u, mono %d\n", pass_name[k],
           (unsigned)vid_adapter(), vid_mono());
    TF_CHECK(vid_adapter() >= VID_A_MDA && vid_adapter() <= VID_A_EGA);
    TF_EQ(vid_open(flags), 0);
#ifdef COMPAT_HOST
    if (flags & VID_F_MDA) {
        TF_EQ(vid_adapter(), VID_A_MDA);
        TF_EQ(vid_mono(), 1);
    } else {
        TF_EQ(vid_adapter(), VID_A_CGA);
        TF_EQ(vid_mono(), 0);
    }
#endif
    test_clear();
    test_put();
    test_fill();
    test_palette();
    test_save();
    test_cursor();
    test_invalidate(flags);
    last_frame();
    flush_ok("last frame");
    vid_close();
    TF_EQ(vid_snow_active(), 0);
    vid_close();
    TF_CHECK(vid_save(0, 0, 1, 1) == NULL);
    vid_put(0, 0, "x", 1, 0x07);
    vid_flush();
    test_dos(flags);
}

/* The WIDE run, as described at the top. */
static void test_wide(void)
{
    u8 r;
    u8 c;

    if (!TF_EQ(vid_open(0), 0))
        return;
    TF_EQ(vid_verify(&r, &c), 0);
    vid_close();
}

int main(int argc, char **argv)
{
    int page1;
    int i;

    tf_begin("T_VIDEO");
    page1 = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "WIDE") == 0) {
            test_wide();
            return tf_done();
        }
        if (strcmp(argv[i], "PAGE1") == 0)
            page1 = 1;
    }
    if (!page1)
        paint();
    test_dos(0);
    pass(0, 0);
    pass(VID_F_SNOW, 1);
    pass(VID_F_BIOS, 2);
#ifdef COMPAT_HOST
    pass(VID_F_MDA, 3);
#endif
    return tf_done();
}

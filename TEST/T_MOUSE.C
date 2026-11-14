/*
 * T_MOUSE.C - unit test for the click codes of the mouse (CMOUSE.C):
 * key_ms_press, which turns the pointer's pixel at a press of the left
 * button into KEY_CLICK or KEY_DCLICK plus the text cell.
 *
 * usage: T_MOUSE [drive]   (drive, which the DOS runs pass, is not used)
 *
 * Checks:
 * - the cell is the pixel divided by KEY_MS_CELL, row by row and column
 *   by column: cells (0,0) and the last, both sides of a cell's edge,
 *   and a pixel past the screen taken as the last row or column;
 * - the first press is a click; a press on the cell of the click before
 *   it is a double click, which clears the memory, so a third press is a
 *   click and a fourth a double click; a pixel is no more than its cell;
 * - a click on another cell replaces the click remembered, also on the
 *   next cell;
 * - a second press more than KEY_MS_DBLTICKS ticks after the first is a
 *   click, which is remembered in its turn.  The host counts one tick
 *   per tmr_ticks call, so there the limit is tested to the tick: a
 *   span of KEY_MS_DBLTICKS ticks still makes a double click, one more
 *   makes a click; under DOS one wait of more than the limit.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include <stdio.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "TESTFW.H"

#define PX(c)   ((u16)((c) * KEY_MS_CELL))      /* a cell's first pixel */

static u16 cell(u16 code, u16 row, u16 col)
{
    return (u16)(code + row * KEY_CROW + col);
}

/* Forget the click remembered: a press on the cell of the one before it
   is a double click, which clears; any other press is a click, which
   the second press on its cell then clears. */
static void clear(u16 x, u16 y)
{
    if (key_ms_press(x, y) < KEY_DCLICK)
        key_ms_press(x, y);
}

/* The code of a press at pixel x, y with nothing remembered before. */
static u16 fresh(u16 x, u16 y)
{
    clear(x, y);
    return key_ms_press(x, y);
}

static void test_first(void)
{
    TF_EQ(key_ms_press(0, 0), cell(KEY_CLICK, 0, 0));
    TF_EQ(key_ms_press(0, 0), cell(KEY_DCLICK, 0, 0));
}

static void test_cells(void)
{
    TF_EQ(fresh(0, 0), cell(KEY_CLICK, 0, 0));
    TF_EQ(fresh(KEY_MS_CELL - 1, KEY_MS_CELL - 1), cell(KEY_CLICK, 0, 0));
    TF_EQ(fresh(KEY_MS_CELL, 0), cell(KEY_CLICK, 0, 1));
    TF_EQ(fresh(0, KEY_MS_CELL), cell(KEY_CLICK, 1, 0));
    TF_EQ(fresh(KEY_MS_CELL, KEY_MS_CELL), cell(KEY_CLICK, 1, 1));
    TF_EQ(fresh(PX(VID_COLS - 1), PX(VID_ROWS - 1)),
          cell(KEY_CLICK, VID_ROWS - 1, VID_COLS - 1));
    TF_EQ(fresh((u16)(PX(VID_COLS) - 1), (u16)(PX(VID_ROWS) - 1)),
          cell(KEY_CLICK, VID_ROWS - 1, VID_COLS - 1));
    TF_EQ(fresh((u16)(PX(VID_COLS - 1) - 1), (u16)(PX(VID_ROWS - 1) - 1)),
          cell(KEY_CLICK, VID_ROWS - 2, VID_COLS - 2));
    TF_EQ(fresh(639, 199), cell(KEY_CLICK, 24, 79));
    TF_EQ(fresh(PX(VID_COLS), PX(VID_ROWS)),
          cell(KEY_CLICK, VID_ROWS - 1, VID_COLS - 1));
    TF_EQ(fresh(900, 400), cell(KEY_CLICK, 24, 79));
    TF_EQ(fresh(900, 0), cell(KEY_CLICK, 0, 79));
    TF_EQ(fresh(0, 400), cell(KEY_CLICK, 24, 0));
    TF_EQ(fresh(65535U, 65535U), cell(KEY_CLICK, 24, 79));
}

static void test_double(void)
{
    clear(PX(5), PX(3));
    TF_EQ(key_ms_press(PX(5), PX(3)), cell(KEY_CLICK, 3, 5));
    TF_EQ(key_ms_press(PX(5), PX(3)), cell(KEY_DCLICK, 3, 5));
    TF_EQ(key_ms_press(PX(5), PX(3)), cell(KEY_CLICK, 3, 5));
    TF_EQ(key_ms_press(PX(5), PX(3)), cell(KEY_DCLICK, 3, 5));
    TF_EQ(key_ms_press(PX(5), PX(3)), cell(KEY_CLICK, 3, 5));
    /* Another pixel of the cell is the same cell. */
    TF_EQ(key_ms_press((u16)(PX(5) + 7), (u16)(PX(3) + 7)),
          cell(KEY_DCLICK, 3, 5));
    /* The last cell: a double click there too. */
    TF_EQ(fresh(639, 199), cell(KEY_CLICK, 24, 79));
    TF_EQ(key_ms_press(900, 400), cell(KEY_DCLICK, 24, 79));
}

static void test_other(void)
{
    clear(PX(10), PX(10));
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_CLICK, 10, 10));
    TF_EQ(key_ms_press(PX(11), PX(10)), cell(KEY_CLICK, 10, 11));
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_CLICK, 10, 10));
    TF_EQ(key_ms_press(PX(10), PX(11)), cell(KEY_CLICK, 11, 10));
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_CLICK, 10, 10));
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_DCLICK, 10, 10));
    /* A click on another cell between two on the first is the last. */
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_CLICK, 10, 10));
    TF_EQ(key_ms_press(PX(20), PX(20)), cell(KEY_CLICK, 20, 20));
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_CLICK, 10, 10));
    TF_EQ(key_ms_press(PX(10), PX(10)), cell(KEY_DCLICK, 10, 10));
}

#ifdef COMPAT_HOST

/* A press, gap calls of tmr_ticks, a second press: key_ms_press reads
   the clock once for the first (the tick it keeps) and tmr_since once
   for the second, so the span is gap + 1 ticks. */
static u16 after(u16 gap, u16 x, u16 y)
{
    u16 i;

    key_ms_press(x, y);
    for (i = 0; i < gap; i++)
        tmr_ticks();
    return key_ms_press(x, y);
}

static void test_slow(void)
{
    clear(PX(2), PX(2));
    TF_EQ(after(KEY_MS_DBLTICKS - 1, PX(2), PX(2)),
          cell(KEY_DCLICK, 2, 2));
    clear(PX(2), PX(2));
    TF_EQ(after(KEY_MS_DBLTICKS, PX(2), PX(2)), cell(KEY_CLICK, 2, 2));
    /* That slow press is the click remembered. */
    TF_EQ(key_ms_press(PX(2), PX(2)), cell(KEY_DCLICK, 2, 2));
    clear(PX(2), PX(2));
    TF_EQ(after(0, PX(2), PX(2)), cell(KEY_DCLICK, 2, 2));
    clear(PX(2), PX(2));
    TF_EQ(after(KEY_MS_DBLTICKS + 50, PX(2), PX(2)), cell(KEY_CLICK, 2, 2));
}

#else

static void test_slow(void)
{
    clear(PX(2), PX(2));
    key_ms_press(PX(2), PX(2));
    tmr_delay(KEY_MS_DBLTICKS + 3);
    TF_EQ(key_ms_press(PX(2), PX(2)), cell(KEY_CLICK, 2, 2));
    TF_EQ(key_ms_press(PX(2), PX(2)), cell(KEY_DCLICK, 2, 2));
}

#endif

int main(void)
{
    tf_begin("T_MOUSE");
    test_first();
    test_cells();
    test_double();
    test_other();
    test_slow();
    return tf_done();
}

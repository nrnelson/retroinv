/*
 * CMOUSE.C - the mouse through the Microsoft mouse driver (INT 33h), its
 * presses given out among the keys of CKEY.C (key_mouse).
 *
 * Without a driver the INT 33h vector may be null or point at a lone
 * IRET, which DOS or the BIOS leaves there (Compaq MS-DOS 3.10 points it
 * into DOS), so the vector is checked before the driver is called;
 * either means that none is loaded.  A driver answers function 00h
 * (reset) with AX FFFFh.  The reset hides the pointer and sets the
 * driver's text pointer, a cell whose attribute is inverted, moving
 * over 640 x 200 virtual pixels in an 80-column text mode, KEY_MS_CELL
 * by KEY_MS_CELL per cell.
 *
 * Function 05h gives the presses of one button since its last call, and
 * where the pointer was at the last of them, so a press made while the
 * program is busy is not lost.  The driver draws the pointer into video
 * memory and puts back the cell it covered when it moves, which would
 * undo a write made under it, so the pointer is shown (function 01h) only
 * while key_read waits for a key and hidden (02h) before it returns.
 *
 * key_ms_press, which turns a press into its key, makes no DOS call, so
 * the host build has it too; it has no driver, so its key_mouse does
 * nothing.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include "CMOUSE.H"
#ifndef COMPAT_HOST
#include <dos.h>
#endif

static u16 ms_last;             /* the last click, KEY_NONE after a
                                   double click                         */
static u32 ms_at;               /* tmr_ticks at ms_last                 */

u16 key_ms_press(u16 x, u16 y)
{
    u16 row;
    u16 col;
    u16 k;

    row = (u16)(y / KEY_MS_CELL);
    col = (u16)(x / KEY_MS_CELL);
    if (row >= VID_ROWS)
        row = VID_ROWS - 1;
    if (col >= VID_COLS)
        col = VID_COLS - 1;
    k = (u16)(row * KEY_CROW + col);
    if (k + KEY_CLICK == ms_last && tmr_since(ms_at) <= KEY_MS_DBLTICKS) {
        ms_last = KEY_NONE;
        return (u16)(KEY_DCLICK + k);
    }
    ms_last = (u16)(KEY_CLICK + k);
    ms_at = tmr_ticks();
    return ms_last;
}

#ifdef COMPAT_HOST

void key_mouse(void)
{
}

#else

#define MS_INT          0x33
#define MS_IRET         0xCF    /* the opcode of a dummy handler        */
#define MS_FOUND        0xFFFFu /* AX of function 00h with a driver     */

/* ms_key calls function op - 1 for KEY_MS_SHOW and KEY_MS_HIDE: 01h
   shows the pointer, 02h hides it. */
extern char ms_ops_fit[1];
extern char ms_ops_fit[(KEY_MS_SHOW == 2 && KEY_MS_HIDE == 3) ? 1 : 2];

static u16 ms_pend;             /* the press read, not yet taken        */

/* INT 33h function fn with BX bx, its registers back into *r. */
static void ms_call(union REGS *r, u16 fn, u16 bx)
{
    r->x.ax = fn;
    r->x.bx = bx;
    int86(MS_INT, r, r);
}

/* The press waiting in the driver: KEY_ESC for the right button, the
   key_ms_press of the left, else KEY_NONE. */
static u16 ms_press(void)
{
    union REGS r;

    ms_call(&r, 0x05, 1);
    if (r.x.bx != 0)
        return KEY_ESC;
    ms_call(&r, 0x05, 0);
    if (r.x.bx == 0)
        return KEY_NONE;
    return key_ms_press((u16)r.x.cx, (u16)r.x.dx);
}

/* The key_ms_fn of key_mouse. */
static u16 ms_key(int op)
{
    union REGS r;
    u16 k;

    if (op >= KEY_MS_SHOW) {
        ms_call(&r, (u16)(op - 1), 0);
        return KEY_NONE;
    }
    if (ms_pend == KEY_NONE)
        ms_pend = ms_press();
    k = ms_pend;
    if (op == KEY_MS_TAKE)
        ms_pend = KEY_NONE;
    return k;
}

void key_mouse(void)
{
    union REGS r;
    u16 seg;
    u16 off;

    dos_getvect(MS_INT, &seg, &off);
    if ((seg == 0 && off == 0) || *CFP(u8, seg, off) == MS_IRET)
        return;
    ms_call(&r, 0x00, 0);
    if (r.x.ax == MS_FOUND)
        key_msfn = ms_key;
}

#endif

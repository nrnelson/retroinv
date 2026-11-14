/*
 * CMOUSE.H - the mouse (CMOUSE.C): its clicks among the keys of CKEY.C,
 * and key_mouse, which finds it; and key_read's idle hook (CKEY.C).
 *
 * Once key_mouse has found a mouse driver, key_read and key_peek report
 * the mouse after the keyboard: a press of the right button is KEY_ESC;
 * a press of the left button is a click on the text cell under the
 * pointer, KEY_CLICK + row * KEY_CROW + col, or KEY_DCLICK + row *
 * KEY_CROW + col when it follows a click on the same cell within
 * KEY_MS_DBLTICKS ticks, about half a second (a double click's second
 * press; a third is a click again), as key_ms_press codes them.  The
 * pointer shows only while key_read waits, so the screen is never
 * written under it.  A key source replaces the mouse as it replaces the
 * keyboard.
 *
 * This header stands apart from COMPAT.H, which every file reads, as
 * Watcom C 6.5 runs out of memory compiling the largest tests with any
 * more declarations there; only the files that take clicks or set the
 * idle hook include it, after COMPAT.H.
 */
#ifndef CMOUSE_H
#define CMOUSE_H

#include "COMPAT.H"

#define KEY_CLICK       0x8000  /* + cell: a click of the left button   */
#define KEY_DCLICK      0xA000  /* + cell: a double click's second      */
#define KEY_CROW        0x0080  /* a click's cell is row * it + col     */
#define KEY_MS_CELL     8       /* virtual pixels a text cell spans     */
#define KEY_MS_DBLTICKS (TMR_SEC / 2)   /* the longest double click     */

/* Look for a mouse driver (INT 33h) and, when one answers, reset it and
   report the mouse through key_read and key_peek from then on.  Called
   once the video mode is set, as the reset fits the pointer to the
   mode.  The host build has no driver and does nothing. */
void key_mouse(void);

/* The key of a press of the left button with the pointer at virtual
   pixel x, y: the cell y / KEY_MS_CELL, x / KEY_MS_CELL, a row past
   the screen's last taken as the last and a column likewise, coded as
   KEY_DCLICK + row * KEY_CROW + col when the press before it given to
   this function was a click on the same cell at most KEY_MS_DBLTICKS
   ticks earlier (tmr_since), so that the press after a double click is
   a click again, else as KEY_CLICK + row * KEY_CROW + col.  It makes no
   DOS call, so the host build has it too, on its virtual ticks. */
u16 key_ms_press(u16 x, u16 y);

/* The idle hook, NULL at start: when set, key_read calls it over and
   over while it waits for the keyboard or the mouse, after each
   dos_idle; never for a key source, which does not wait. */
extern void (*key_idle)(void);

/* The mouse's part in key_read and key_peek (CKEY.C), for SRC/COMPAT/
   only, which key_mouse sets (NULL without a mouse): with KEY_MS_PEEK
   it returns the press waiting, KEY_ESC or a click, without taking it,
   or KEY_NONE; with KEY_MS_TAKE the same, taken; with KEY_MS_SHOW and
   KEY_MS_HIDE, around the wait of key_read, it shows and hides the
   pointer and returns KEY_NONE. */
#ifdef COMPAT_INTERNAL
#define KEY_MS_PEEK     0
#define KEY_MS_TAKE     1
#define KEY_MS_SHOW     2
#define KEY_MS_HIDE     3
typedef u16 (*key_ms_fn)(int op);
extern key_ms_fn key_msfn;
#endif

#endif

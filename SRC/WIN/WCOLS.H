/*
 * WCOLS.H - the columns of a list's rows in the Windows programs: their
 * pixel layout in a font and their drawing (WCOLS.C).
 *
 * A row of a list (PAGER.H) is LV_ROWTEXT cells of CP437 text that its
 * source's format function lays out: each column a run of cells, then
 * the one blank lv_col puts after every column that does not end the
 * row.
 * A table names those columns, so that a row drawn in a proportional
 * font keeps them: the text is split at the columns' cells and each piece
 * drawn in its own pixel column, which a piece of any width never passes.
 *
 * Column k of a table starts at cell s(k), the cells of the columns
 * before it each with its blank: s(0) = 0, s(k + 1) = s(k) + cells(k) +
 * 1.  Its piece is the text's cells s(k) to s(k) + cells(k) - 1, as many
 * of them as the text holds (a list box's string has lost its trailing
 * blanks), its trailing blanks dropped; its leading blanks are dropped
 * too, but in a column of WCOL_INDENT, which keeps them as an indent of
 * as many cells (the depth of a location in the tree, a part under its
 * item).  A column has at least one cell, and the columns of a table end
 * within the row: s(n - 1) + cells(n - 1) <= LV_ROWTEXT; cells past them
 * are not drawn, and a blank a row starts with belongs to its first
 * column (the audit log's, before its date).  A table is constant data
 * of its list's owner, one per source and order.
 *
 * In pixels, n cells are px(n) = n * tmAveCharWidth * WCOL_NUM /
 * WCOL_DEN of the font, multiplied before the division.  Column k's slot
 * runs from x(k) to x(k + 1): x(0) = 0, and x(k + 1) = x(k) +
 * width(k) * tmAveCharWidth for a column with a default width, else
 * x(j) + px(s(k + 1) - s(j)), j the first column after the last one up
 * to k with a default width (0 for none), so that x(k) = px(s(k)) in a
 * table without default widths.  A width kept for the column in the
 * store (wcol_keep) stands for its default width, whether it has one or
 * not.  Its piece has the room from x(k) to
 * x(k + 1) - px(1), the slot's last cell a gap.  A piece wider than its
 * room starts at the room's left edge, whatever its flags, and is
 * clipped at the room's right edge, so the gap stays blank and the next
 * column's text never meets it; the last column's piece, which no column
 * follows, is clipped only at the end of the rectangle drawn in.
 *
 * The cell is as wide as an average capital, not an average character
 * (tmAveCharWidth, that of the lower case): the codes a list shows are
 * capitals (a category, a location type, a kind, a status letter), and a
 * code of n average capitals fills n cells, while mixed-case text and
 * digits, of one width in Helv, have room to spare.  It is not the
 * widest letter either, so only a run of wide capitals (W, M) is
 * clipped; a row of LV_ROWTEXT cells is wider than any screen, and a
 * list window scrolls it (WLIST.H).
 *
 * Headings.  A list window draws its header as headings (wcol_heads): a
 * column's slot in COLOR_BTNFACE with a shadow line at its right edge, its
 * divider, and the header's piece placed as a row's.  A column whose flags
 * name one of the list's orders (WCOL_ORDER) is a button, raised by a
 * white line along its slot's top and left edge, sunken while pressed
 * (shadow lines there, the text and arrow a pixel right and down); the
 * button of the order in use shows the arrow, a triangle pointing up,
 * WCOL_ARROWW across, at the end of the room away from the text (the left
 * for WCOL_RIGHT), the text clipped WCOL_ARROWW + WCOL_ARROWGAP pixels
 * short of it.  Every other column is flat: its face and divider.  The
 * shadow is wcol_unlike's, so it shows on a monochrome display, where
 * white does not and a button looks flat but for its arrow and press.
 *
 * Widths.  The program may keep widths in a store of its own, a far
 * block (CMEM) of WCOL_MAX bytes a row, one row per table that names it
 * (wcol_tab's keep): the width of each column in average characters,
 * WCOL_MINW at least, 0 for its own.  The store is read and written in
 * each call that needs it, so no far pointer outlives one.
 */
#ifndef WCOLS_H
#define WCOLS_H

#include "COMPAT.H"
#include "CWIN.H"
#include "PAGER.H"

/* The most columns of a table: the audit log's in INVWIN (WROWS.H), its
   date and its nine fields. */
#define WCOL_MAX        10

/* A cell's pixels per average character of the font, as a fraction:
   an average capital (the comment at the top of this file). */
#define WCOL_NUM        3
#define WCOL_DEN        2

/* A column: the cells of its text in the row, its blank after it not
   counted, its flags and its default width, the average characters of
   the font (tmAveCharWidth) its slot takes, its gap included, or 0 for
   its cells and gap. */
typedef struct {
    u8   cells;
    u8   flags;                 /* WCOL_RIGHT, WCOL_INDENT              */
    u8   width;
} wcol;

/* wcol's flags. */
#define WCOL_RIGHT      0x01    /* the piece ends at its room's right
                                   edge (numbers), else starts at its
                                   left                                 */
#define WCOL_INDENT     0x02    /* leading blanks are an indent; never
                                   with WCOL_RIGHT                      */
/* Bits 2..4: the index of the list source's orders (PAGER.H) that the
   column's heading names, plus 1; 0 when it names none.  WCOL_ORDOF
   gives the index, or -1. */
#define WCOL_ORDSHIFT   2
#define WCOL_ORDMASK    0x1C
#define WCOL_ORDER(i)   ((u8)(((i) + 1) << WCOL_ORDSHIFT))
#define WCOL_ORDOF(f)   ((int)(((f) & WCOL_ORDMASK) >> WCOL_ORDSHIFT) - 1)

/* The narrowest and widest width kept, in average characters. */
#define WCOL_MINW       3
#define WCOL_MAXW       255

/* A table: n columns (1..WCOL_MAX) of col, in the row's order; keep,
   its row of widths in the store, 1 for the first, or 0 for none; and
   head, the string resource of its header, the columns' words in their
   order, each ended by WCOL_SEP but the last (wcol_head), or 0 for a
   header that is a message (WLIST.H) or a static's text (WDLG.H). */
typedef struct {
    u8   n;
    u8   keep;
    const wcol *col;
    u16  head;
} wcol_tab;

#define WCOL_SEP        '|'

/* A table's columns in one font: x[k] the left edge of column k's slot
   (x[0] = 0) and x[n] the end of the row, the last column's blank
   included, in pixels from the left of the rectangle drawn in; cell the
   pixels of one cell, px(1); lineh the font's tmHeight.  A NULL table
   stands for one column of LV_ROWTEXT cells, WCOL_INDENT: a text drawn
   whole, as INV shows it, that is not a row (a list's empty text). */
typedef struct {
    int  x[WCOL_MAX + 1];
    int  cell;
    int  lineh;
} wcol_lay;

/* Lay the columns of t out in the font of metrics tm into *lay, as the
   header says, with the widths kept for t. */
void wcol_layout(wcol_lay *lay, const wcol_tab *t, const TEXTMETRIC *tm);

/* Keep the widths of every table from now on in store, a far block of
   WCOL_MAX bytes per keep (Widths, above), which stays the caller's and
   outlives every list; NULL, as at the start, for none. */
void wcol_keep(cmem_blk *store);

/* Row keep of the store into w (WCOL_MAX bytes), all 0 without a store or
   for keep 0. */
void wcol_widths(u8 keep, u8 *w);

/* Keep width n for column k of the tables of keep; nothing without a
   store or for keep 0. */
void wcol_set_width(u8 keep, int k, u8 n);

/* 1 while a store is set (wcol_keep), else 0. */
int wcol_kept(void);

/* The pixels either side of a divider that grab it (wcol_hit). */
#define WCOL_GRIP       3

/* The column of t laid out in lay whose slot holds x, in pixels from the
   left of the rectangle drawn in with the scroll added back, or -1 before
   the first slot and past the last; *div 1 when x lies within WCOL_GRIP
   pixels of a divider, the right edge x[j + 1] of a column j, which is
   then the column returned, the last such j; else 0.  With div NULL, or
   t NULL (the NULL table), no divider is hit.  No GDI call. */
int wcol_hit(const wcol_tab *t, const wcol_lay *lay, int x, int *div);

/* Lay words, a header's (wcol_tab's head, loaded), out as a row of the
   columns of t into text (LV_ROWTEXT + 1 bytes): each word in its
   column's cells, at their right end in a column of WCOL_RIGHT, else at
   their left, cut to them, then the blank after them; a column without
   a word is blank, a word past the last column left out, and the row's
   trailing blanks dropped. */
void wcol_head(const wcol_tab *t, const char *words, char *text);

/* Column k of t's piece of text, len characters from the row's first
   cell, in either character set: the column's cells that text holds,
   their trailing blanks dropped, and their leading ones but in a column
   of WCOL_INDENT, which keeps them as an indent of *ind cells (else 0).
   The piece's first character's index goes into *at, and its length,
   0 for none, is returned.  t NULL is the NULL table.  No GDI call: the
   test reads a row's pieces through it. */
int wcol_piece(const wcol_tab *t, int k, const char *text, int len,
               int *at, int *ind);

/* The left edge, from the left of the rectangle drawn in, of column k's
   piece in the columns of t laid out in lay, w pixels wide with an
   indent of ind cells (wcol_piece): its room's right edge less w for
   WCOL_RIGHT, else ind cells in, and the room's left edge when w is
   wider than the room.  w matters only for WCOL_RIGHT and an indent. */
int wcol_x(const wcol_tab *t, const wcol_lay *lay, int k, int ind, int w);

/* Draw text, CP437, NUL-terminated, in the rectangle box of dc scrolled
   hx pixels left, r, and with a table widened to the end of its columns,
   so that a row wider than the box is clipped by the box alone: in the
   columns of t laid out in lay (wcol_layout for t and the font selected
   in dc), each piece (wcol_piece) converted with OemToAnsi and drawn at
   wcol_x with ExtTextOut(ETO_CLIPPED | ETO_OPAQUE), its top
   (r->bottom - r->top - lay->lineh) / 2 pixels below r->top, in the text
   and background colours of dc.  Each column paints its slot, the gap
   before it (from r's left edge for the first) and its room (on to r's
   right edge for the last), no further than r's right edge, clipping
   its piece there, so every pixel of r is painted, the gaps and a column
   with no piece included.  While a watch is set (wcol_watch), the
   drawing is recorded there instead and nothing is drawn, each piece's
   x from box's left edge, as the screen puts it (negative when scrolled
   off).  Nothing else is kept from one call to the next. */
void wcol_draw(HDC dc, const RECT FAR *box, const char *text,
               const wcol_tab *t, const wcol_lay *lay, int hx);

/* A heading's state (Headings, above; wcol_pc's head). */
#define WCOL_H_FLAT     0       /* names no order of the source         */
#define WCOL_H_BTN      1       /* a button, not the order in use       */
#define WCOL_H_CUR      2       /* the order in use's button: the arrow */
#define WCOL_H_PRESS    3       /* a button held down                   */

#define WCOL_ARROWW     7       /* pixels across the arrow's base       */
#define WCOL_ARROWGAP   2       /* pixels between the arrow and text    */

/* Draw text, a header (CP437, NUL-terminated), as the headings of the
   columns of t laid out in lay (Headings, above) in the rectangle box of
   dc scrolled hx pixels left, each piece placed and clipped as wcol_draw
   places it, in COLOR_BTNTEXT on COLOR_BTNFACE, every pixel of box
   painted: a column whose flags name an order below norders is a
   button, order's with the arrow, column press's (-1 for none) pressed;
   the others are flat.  t NULL is the NULL table, one flat heading.
   While a watch is set, the drawing is recorded there instead, as
   wcol_draw's is, with each column's slot and state. */
void wcol_heads(HDC dc, const RECT FAR *box, const char *text,
                const wcol_tab *t, const wcol_lay *lay, int hx,
                u8 norders, u8 order, int press);

/* System colour sys as dc shows it, a dark shade drawn on COLOR_BTNFACE:
   when the display shows it as the face (Windows 3.0's default
   COLOR_GRAYTEXT does on a VGA, grey does as white on a monochrome
   display), COLOR_BTNSHADOW in its place, and when that too,
   COLOR_WINDOWFRAME. */
COLORREF wcol_unlike(HDC dc, int sys);

/* Draw the item of the WM_DRAWITEM d as a list window of WLIST.H draws
   its rows, in the font selected in d->hDC: the focus rectangle alone
   toggled for ODA_FOCUS; else, but for item -1 (none), the list box's
   row, drawn by wcol_draw in the columns of t laid out in lay, in the
   highlight's colours when selected, else in the text colour text on
   the window's colour, and the focus rectangle on it when it has the
   focus, the text scrolled hx pixels left (wcol_draw). */
void wcol_item(const DRAWITEMSTRUCT FAR *d, const wcol_tab *t,
               const wcol_lay *lay, COLORREF text, int hx);

/* A piece as wcol_draw placed it: len characters from text[at] (0 for
   none), its column's flags, its left edge x pixels from the left of the
   box drawn in (wcol_draw), its scroll included, and its width w, as
   GetTextExtent measures it; drawn by wcol_heads, also its column's slot,
   its left edge sx as x is measured and its width sw, x[k + 1] - x[k],
   and its heading's state head, a WCOL_H_. */
typedef struct {
    int  x;
    int  w;
    int  sx;
    int  sw;
    u8   at;
    u8   len;
    u8   flags;
    u8   head;
} wcol_pc;

/* What the last wcol_draw while watched drew, copied, as the table may
   not outlive the drawing: n the columns of its table (the NULL table's
   one for NULL), 0 until a drawing is seen; pc a piece per column; text
   the cells that columns hold, in the Windows set; fg and bg the text and
   background colours of the dc; cut 1 when some piece is not drawn
   whole within its slot (wcol_draw): its right end past the slot's
   right edge, which is its room's, or for the last column the
   rectangle's right edge (so the last piece may run into the row's
   last gap), and never past the rectangle's; or drawn without
   ETO_CLIPPED.  x, w and cut are taken from the very arguments of
   ExtTextOut.  heads is 1 when wcol_heads drew it, else 0. */
typedef struct {
    wcol_pc pc[WCOL_MAX];
    char text[LV_ROWTEXT];
    COLORREF fg;
    COLORREF bg;
    u8   n;
    u8   cut;
    u8   heads;
} wcol_seen;

/* Record into *s, from now on, each drawing of wcol_draw in place of
   drawing it, s->n set to 0 now; NULL ends the watch.  The test driver
   (TDRV.H) watches the owner of a box or header draw it. */
void wcol_watch(wcol_seen *s);

#endif

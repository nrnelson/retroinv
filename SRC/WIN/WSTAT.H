/*
 * WSTAT.H - the status bar of INVWIN's frame: a row of sunken panels of
 * text along its bottom, in a child window (WSTAT.C).
 *
 * Windows 3.0 has no status bar, so wsb_create makes a child window of
 * class "WSTAT" drawn with the GDI alone.  Its window text is its
 * panels' texts, in order, each after a WSB_SEP but the first, so that
 * the texts live in the window's own memory, outside DGROUP, and the
 * panels are as many as the pieces of the text.  The bar opens with one
 * empty panel; WSBM_PANELS and WSBM_SETTEXT change its text, and
 * nothing else may.
 *
 * Panels.  WSBM_PANELS (wParam n, lParam a const int FAR * to the
 * widths of the first n - 1 panels in pixels, their edges included,
 * NULL when n is 1) makes the bar n panels, those before the n-th as
 * wide as said, the last as wide as the rest of the bar; panels kept
 * keep their texts, cut to their new room (below), new ones are
 * empty.  It returns n, or 0, changing nothing, when n is not
 * 1..WSB_MAXPANELS.  WSBM_SETTEXT (wParam a
 * panel, lParam an LPCSTR, NULL for an empty text) sets a panel's text,
 * each WSB_SEP in it made a space and the text cut after WSB_MAXCELL
 * bytes, or for the last panel after the room the others leave at
 * their longest, WSB_MAXTEXT - (n - 1) * (WSB_MAXCELL + 1) bytes, so
 * that a panel's room never depends on the others' texts.  It returns
 * 1, or 0, changing nothing, when there is no such panel.  A panel is
 * drawn again only when its text or its place changes; WSBM_PANELS with
 * the bar's own count and widths changes nothing.  WSBM_CUT returns 1
 * when a panel holds a text cut to its room, by WSBM_SETTEXT or
 * WSBM_PANELS, or a panel but the last a text wider, in the bar's font,
 * than its room inside its edges less WSB_PADX, else 0; the test driver
 * marks such a bar CUT (TDRV.H).
 *
 * Drawing.  The bar is COLOR_BTNFACE with a COLOR_WINDOWFRAME line
 * along its top.  The panels stand left to right from WSB_MARGIN,
 * WSB_GAP pixels apart, and WSB_MARGIN pixels below the line and above
 * the bar's bottom, the last ending WSB_MARGIN pixels short of the
 * bar's right edge (at its left edge, when the others leave no room);
 * panels past the right edge are cut there.  Each is sunken: a
 * COLOR_BTNSHADOW edge along its left and top, as wcol_unlike of WCOLS.H
 * gives it, so that a program that links WSTAT.C links WCOLS.C, and a
 * white one along its right and bottom, its text inside them WSB_PADX
 * pixels from its left and WSB_PADY below its top, in the bar's font in
 * COLOR_BTNTEXT, '&' drawn as itself, cut at the panel's right edge.  No
 * GDI object outlives a message but the font, which the owner owns.
 */
#ifndef WSTAT_H
#define WSTAT_H

#include "COMPAT.H"
#include "CWIN.H"

#define WSB_SEP         '\t'    /* ends each panel's text but the last  */
#define WSB_MAXPANELS   4       /* panels a bar may have                */
#define WSB_MAXTEXT     323     /* bytes of the window text: room for
                                   WSB_MAXPANELS of WSB_MAXCELL and
                                   the WSB_SEPs between them            */
#define WSB_MAXCELL     80      /* bytes of a panel's text but the last
                                   panel's: a few words and a path      */
#define WSB_MARGIN      2       /* pixels about the panels              */
#define WSB_GAP         4       /* pixels between two panels            */
#define WSB_PADX        3       /* pixels left of a panel's text        */
#define WSB_PADY        1       /* pixels above and below it            */

#define WSBM_PANELS     (WM_USER + 0)   /* wParam panels, lParam widths */
#define WSBM_SETTEXT    (WM_USER + 1)   /* panel wParam's text lParam   */
#define WSBM_CUT        (WM_USER + 2)   /* 1: a panel's text cut        */

/* Make a bar of control ID id in owner, visible, its left, right and
   bottom edges those of *r in owner's client coordinates; *r's top is
   set to where the bar starts, as high as its font needs: the top
   line, the margins, the panels' edges and WSB_PADY about a line of
   the font.  Its font is font, NULL for ANSI_VAR_FONT, kept, never
   deleted, while the bar lasts.  The bar's height stays its window's
   for the owner's layout.  NULL when Windows made no window. */
HWND wsb_create(HWND owner, RECT *r, int id, HFONT font);

#endif

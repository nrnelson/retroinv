/*
 * WLOC.H - the location tree of INVWIN (WLOC.C), a screen of the frame
 * (WSCR.H), and the parts of the frame's screen of a location's
 * contents.
 *
 * The tree shows LOC LIST with the rows and columns of WROWS.H and the
 * empty text of SCRLFORM.H, each location's type by the name that the
 * LTYPE LIST of wloc_types gives it, and a letter typed seeks the next
 * location whose name starts with it (wl_letters of WLIST.H).  Its
 * records show in the location form of SCRLFORM.H, its type's name from
 * the same names (WFORM.H's name field), read with LOC GET, saved as LOC
 * with a record's caption MO_FTITLE and with their notes (mio_load and
 * mio_save of MEMOIO.H), Inside being the form's location field
 * (WFORM.H), blank for the top level (MO_TOP); they are deleted with LOC
 * DEL once MO_DELQ has been answered.  A location's name (WSCR.H) is its
 * LOC_NAME alone.  It is a tree (WS_TREE): a new location is inside the
 * selected one, refused at once on a location at the deepest level
 * (wloc_room), or at the top level when none is, of the type of the
 * first location already inside its container (scr_loc_first of
 * SCRLFORM.H), else of none, or at the top level ROOM.  Move moves the
 * selected location, with everything inside it, into the one chosen from
 * the pick list of WPICK.H captioned IW_S_MOVETO with the location's
 * name (LOC GET), opened on its container (PARENT_ID of LOC GET) and
 * refusing the location itself and those inside it (MO_INSELF), whose
 * lead row MO_TOP is the top level (LOC SET PARENT_ID=, 0 for the top
 * level); the container chosen again sends nothing, and one at the
 * deepest level is refused before LOC SET (wloc_room).  Its pane, the
 * selected location's contents, is wpane_loc of WPANE.H, and ENTER shows
 * them as a screen of their own (WS_HOLDS).  Its notes are MO_ADDED,
 * MO_SAVED, MO_DELETED and, for a move, MO_MOVED, each with the
 * location's name as ^2.  The contents are an item list, as INV's: LOC
 * CONTENTS of the location set through wloc_csrc, with TOP=1 while
 * wloc_top is set and ALL=1 while wloc_all is, titled with the
 * location's path (wloc_ctitle), a new item there taking the location
 * (wloc_cpreset).  wloc_enter sets the location they show until it sets
 * another.
 */
#ifndef WLOC_H
#define WLOC_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "SCRLFORM.H"
#include "WSCR.H"

extern const ws_scr wloc_scr;

/* Load the names of the location types for the tree's rows with LTYPE
   LIST through l, from w held busy as ws_send holds it: 1, or 0 with
   *act the action of the failure, shown over w (ws_fail), the rows then
   giving the types' codes.  The names stay until the next load;
   wloc_free frees them. */
int wloc_types(HWND w, link *l, u8 *act);

/* Free what wloc_types holds. */
void wloc_free(void);

/* Location id read with LOC GET from w (ws_send) into *x: 1, or 0 with
   *act the action of the failure, shown over w (ws_fail), else
   RQE_STAY. */
int wloc_get(HWND w, const char *id, scr_locinfo *x, u8 *act);

/* 1 when a location can go inside location id, read with LOC GET
   from w (ws_send), which is not at the deepest level the tree has
   (LINK_MAXLOCDEPTH); else 0, MO_TOODEEP said in a message box over w,
   or the failure shown (ws_fail) with *act its action (else RQE_STAY). */
int wloc_room(HWND w, const char *id, u8 *act);

/* 1 while the contents show only the location's items not installed in
   another (LOC CONTENTS TOP=1), else 0; and 1 while they show those of
   every location below it too (ALL=1), else 0.  The frame sets them. */
extern u8 wloc_top;
extern u8 wloc_all;

/* The LOC_ID of the location whose contents wloc_csrc shows. */
extern char wloc_id[LINK_IDLEN + 1];

/* Set location id as the one whose contents wloc_csrc shows, read with
   LOC GET from w (ws_send) for its path, all the items, at it alone or,
   when it holds locations (scr_loc_first), everywhere below it too, as
   INV's contents start (wloc_top 0, wloc_all 0 or 1):
   1, or 0 with *act the action of the failure, shown over w (ws_fail),
   and the location set kept. */
int wloc_enter(HWND w, const char *id, u8 *act);

/* Read the location set again with LOC GET from w (ws_send), for a name
   or path changed meanwhile, wloc_top and wloc_all kept: 1, or 0 with
   *act the action of the failure, shown over w (ws_fail). */
int wloc_refresh(HWND w, u8 *act);

/* The contents' source, a template as itl_src of ITEMLS.H is, which the
   frame copies, giving it fmt: itl_src's columns without its orders,
   LOC CONTENTS for the location set, and MO_NOITEMS for no rows. */
extern const lv_src wloc_csrc;

/* The contents' title: the path of the location set. */
const char *wloc_ctitle(void);

/* The name of the location set, for the count of its contents. */
const char *wloc_cname(void);

/* After location id has been saved from the location form f: its new
   name, when it is the location set, the one wloc_cname gives. */
void wloc_saved(const char *id, form *f);

/* A new item's first values in the contents, a screen's preset: those
   of scr_item_preset, the location the one set. */
void wloc_cpreset(form *f);

#endif

/*
 * WPICK.H - the pick lists of the Windows client: the categories, the
 * location types, the tags, the location tree or a list of the caller's,
 * shown in a modal dialog for the user to choose from (WPICK.C), as
 * INV's F4 shows them in its forms.
 *
 * The dialog, the template WF_R_PICK of WFORM.RC, holds the list above
 * OK and Cancel and is captioned with the title the caller gives.  Once
 * it shows, the list is fetched, the owner's busy bracket held around
 * each request:
 *
 *   - a code list (SCR_CK_CAT, SCR_CK_LTYPE of SCRKFORM.H) by one CAT
 *     LIST or LTYPE LIST, held whole (CODETAB.H), into the template's
 *     list box (control WF_C_PICK), a row each: the code in
 *     LINK_LTYPELEN cells, a blank and the name; the row of the code
 *     the caller gives is selected, else the first, and a character
 *     typed selects the next row starting with it, as Windows' list box
 *     does; a list without rows shows one, MT_NOCHOICE of TUI.H,
 *     which is no choice and is not selected;
 *   - the tags in use (SCR_CK_TAG) by ITEM TAGS, followed page by page
 *     to its end (ct_load_tags), as a code list's rows, a tag each;
 *     between two pages the messages waiting are dispatched but input,
 *     and an Esc among it stops there (wl_wait of WLIST.H).  Once
 *     loaded, the caption says how whole the list is: ME_TAGSPART for
 *     a list stopped or cut to LINK_MAXCODES tags, even one stopped
 *     before any tag came; else ME_NOTAGS for none, ME_TAGS for all;
 *   - the locations (WP_LOCS) by LTYPE LIST, for the names of the
 *     types, then LOC LIST, a page at a time, in a list window of
 *     WLIST.H (control WF_C_PICK, in the list box's place) over the
 *     tree's rows of WROWS.H, after a lead row, the caller's message
 *     lead, whose key is "0": no location, the top level
 *     or every location.  The location the caller gives is sought and
 *     selected (wl_find), else the lead row is; when the seek gives up
 *     short of it (LV_SEEKMAX rows), the row it stopped on is chosen
 *     only after a Yes to MO_PICKQ, No the default, so an untouched
 *     Enter cannot choose a location nobody picked.  It moves and pages as
 *     WLIST.H says, a letter seeking the next location whose name
 *     starts with it (wl_letters), and shows the first columns of the
 *     tree's (wrow_tpick_cols), the indented name and the type, leaving
 *     out the code and the capacity, as INV's pick list does;
 *   - a caller's source (wp_list), a page at a time, in a list window
 *     as the locations', without a lead row or letters, the row of the
 *     key the caller gives sought, else the first selected, in the
 *     columns of the caller's table.
 *
 * The list box draws its rows in OEM_FIXED_FONT, CP437 as they come; a
 * list window draws them in its own font, in columns (WLIST.H), under a
 * header.  Either is WP_ROWS rows high and as wide as its rows and a
 * scroll bar, a list window as wl_width and wl_height give, and the
 * dialog is sized to it.
 * Enter, OK or a double click on a row chooses it: a code list's code,
 * or a location's LOC_ID, "0" for the lead row, but a location the
 * caller says the choice may not be in, or one inside it (wp_within),
 * which MO_INSELF says in a message box captioned as the dialog is
 * (wd_say of WDLG.H), and the list stays.  Esc, Cancel or the system
 * menu's Close chooses nothing, as do Enter and OK on a list without
 * rows.  Tab and Shift-Tab go round the list and the two buttons.
 */
#ifndef WPICK_H
#define WPICK_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"
#include "SCRKFORM.H"
#include "WCOLS.H"

#define WP_ROWS         12      /* rows a pick list shows               */
#define WP_CODESZ       (LINK_ITAGLEN + 1)      /* a choice: a tag, or
                                   a row's key (LV_KEYSZ, no longer)    */

/* The lists: the code lists by their kind SCR_CK_CAT, SCR_CK_LTYPE and
   SCR_CK_TAG (SCRKFORM.H), and the location tree. */
#define WP_LOCS         3

/* The tables of the code lists of LIST by kind ("CAT", "LTYPE"), and
   LOC GET. */
extern const char *const wp_tables[];
extern const char wp_w_locget[];

/* wl_open's flags (WLIST.H) for the paged pick lists, the locations and
   wp_list's: WL_EXACT on a link whose requests are fast (LOCAL, NET,
   TCP), 0 on COM and MBOX, as the program sets it with its link; 0
   until then. */
extern u8 wp_flags;

/* The pick lists' results. */
#define WP_NONE         0       /* nothing chosen                       */
#define WP_OK           1       /* a row chosen                         */
#define WP_FAIL         2       /* a code list's request failed         */
#define WP_ENOMEM       3       /* the dialog or its list could not be
                                   made: no memory, or Windows refused
                                   a window                             */

/* Show pick list kind (SCR_CK_ or WP_LOCS) over owner, a window of the
   program, captioned with title (CP437, at most MSG_MAXLEN characters
   kept, so a msg result will do), its lead row message lead for
   WP_LOCS, the row of at selected (a code, a LOC_ID; "" for none), a
   location outside (a LOC_ID, or NULL) refusing any choice of itself or
   of a location inside it, with the link l and the user's access level
   (rqe_class), and return when it ends, WP_.  busy and arg are the
   owner's busy bracket of each request, as wf_ops' busy is (WFORM.H),
   given the pick list's dialog.  WP_OK leaves the choice in code
   (WP_CODESZ bytes), WP_FAIL the failed list's response in res, shown
   by no one.  A failed LOC LIST is shown by its list window (WLIST.H),
   and *act is set to its action when that leaves the list (RQE_LOGIN,
   RQE_QUIT or RQE_LOST of RQERR.H), which ends the dialog with WP_NONE;
   else *act is RQE_STAY. */
int wp_pick(HWND owner, link *l, u8 level, u8 kind, const char *title,
            u16 lead, const char *at, const char *outside,
            void (*busy)(void *arg, HWND dlg, int on), void *arg,
            char *code, rq_res *res, u8 *act);

/* Show the rows of src (copied) in the columns of cols, as wl_open's
   (WLIST.H), in a pick list as wp_pick shows the locations, captioned
   with title, the row of key at selected ("" for the first), and return
   when it ends: WP_OK with the row's key in code (WP_CODESZ bytes),
   WP_NONE or WP_ENOMEM, *act as wp_pick's. */
int wp_list(HWND owner, link *l, u8 level, const char *title,
            const lv_src *src, const wcol_tab *cols, const char *at,
            void (*busy)(void *arg, HWND dlg, int on), void *arg,
            char *code, u8 *act);

/* 1 when location id is location outside or one inside it (LOC_IDs),
   found by LOC GET up its containers (at most
   LINK_MAXLOCDEPTH + 1 requests, each in busy(arg, dlg) brackets as
   wp_pick's); else 0, as after a failed request, left to the server's
   refusal of the move to come. */
int wp_within(link *l, const char *id, const char *outside,
              void (*busy)(void *arg, HWND dlg, int on), void *arg,
              HWND dlg);

#endif

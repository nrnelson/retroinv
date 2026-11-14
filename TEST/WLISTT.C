/*
 * WLISTT.C - the test of the paged list window (SRC/WIN/WLIST.C): the
 * item list of the seeded data in a list window, which a key script
 * moves and snaps.
 *
 * usage: WIN WLISTT dir [/DRAG] [/MULTI] /KEYS:WLISTT.KEY /SNAP:WLISTT.SNP
 *
 * dir is a data directory seeded as the screen tests seed theirs (the
 * host INV /CMD /LOCAL dir /NOW:20261008120000 on TEST/KEYS/SEED.REQ),
 * and INV.MSG is in the current directory.  The program opens the LOCAL
 * link on dir, logs in as ADMIN with plain requests and shows ITEM LIST
 * in a list window of WLT_ROWS rows, so few that the seed's three items
 * take two pages, with two orders: ID with the command's default fields
 * and SERIAL with a field list of its own, each drawn in its own columns
 * (cols).  The list counts its rows (WL_EXACT), as on a fast link.  The
 * ID column is left-aligned, unlike its header, so that a row starts
 * with its key and a character typed could find a row if the box got
 * it.  Items of category DRV are marked, drawn in red, which no
 * snapshot shows.
 *
 * The frame's title is "what #n p/m c", "what #n p/m c q" once R has
 * turned q on, or "what #n" while there is no list: n counts the list's
 * WL_BUSY 1 notes, so a snapshot shows whether a move was busy; p and m
 * are the position and the top of the range of the list window's
 * scroll bar (GetScrollPos, GetScrollRange; 0/0 for a hidden bar); c is
 * the count wl_count gives, or "-" when it gives none; q the ITEM LIST
 * requests since the last WL_BUSY 1 note, those of the last busy move
 * (survey included); what says what the owner last saw ("WLISTT" at the
 * start).  With /MULTI the rows chosen (wl_nchosen) follow c, and a "*"
 * while every row is chosen (wl_all).  The title is made again after
 * every message the program's loop dispatches, at each WL_BUSY 1 note,
 * so that a failure's message box, captioned as the frame, shows the
 * move's count with the bar as the move found it, and after each
 * WL_BUSY 0 note (posted, so that the frame's title is new while that
 * box shows).  The keys the list leaves to its owner:
 *
 *   Enter     what becomes "OPEN id", id the selected key ("" with no
 *             rows); a double click comes here too
 *   SHIFT-F1  what becomes "LAST id [text]", the key and the first
 *             WLT_LASTW characters of the text wl_chosen gives of the
 *             last row chosen; F1 without SHIFT is any other key
 *   CTRL-/    wl_select_all: "ALL rc"
 *   CTRL-\    wl_deselect
 *   Tab       the next order (wl_move's PG_ORDER), as INVWIN's Next
 *             order takes it; with SHIFT or CTRL, as any other key
 *   F4        wl_find of item WLT_FINDID, from the first page: "FIND
 *             rc"; with SHIFT, letters on (wl_letters): "LETTERS"; with
 *             CTRL, the list made again with a lead row of key "0" and
 *             text WLT_LEAD (wl_lead), and its first page: "LEAD"
 *   F2, F3    post a click (F2) or a double click (F3) on the box's
 *             second row, or with SHIFT its first, at the row's middle,
 *             as the mouse makes them: down and up, then for F3 a double
 *             click and up; the list box does the rest
 *   F5        ITEM SET id QTY=WLT_QTY on the selected item, then a
 *             reload, which shows the new quantity
 *   F6        turns the filter that matches no item (CAT=WLT_NOCAT) on
 *             or off, in place of any other, then goes to the first page,
 *             the list counted again (wl_recount), as for F8
 *   F7        posts an Esc key to the list window's queue, then makes
 *             the End move, whose first wait finds the Esc and stops it;
 *             with SHIFT, wl_find of item WLT_FIRSTID, the first row,
 *             which it selects without a walk, whose fetches and waits
 *             would take the Esc: "FIRST n e", n the ITEM LIST requests
 *             it made and e 1 when the Esc was still queued, then dropped;
 *             with CTRL, posts the frame WLT_WM_GONE, on which it destroys
 *             the list, then makes the End move, whose first wait
 *             dispatches it, so that the move stops there: "GONE n", n
 *             the ITEM LIST requests the move made
 *   F8        turns the filter ITEM LIST refuses (102 for the key BOGUS)
 *             on or off, in place of any other, then goes to the first
 *             page, the list counted again (wl_recount): the failure's
 *             box, after which the list stays
 *   F9        BYE, then a reload, which fails as the session has ended:
 *             its message box, then the list's WL_LEAVE
 *   U, D, P, N, T, B
 *             post the list window the gesture of the scroll bar's
 *             SB_LINEUP, SB_LINEDOWN, SB_PAGEUP, SB_PAGEDOWN, SB_TOP or
 *             SB_BOTTOM: the code, then SB_ENDSCROLL; with SHIFT the
 *             code WLT_REPEAT times before SB_ENDSCROLL; with CTRL the
 *             gesture twice, the second ending while the first's move is
 *             posted
 *   0 to 9    post the gesture of the thumb released at that position:
 *             SB_THUMBTRACK, SB_THUMBPOSITION and SB_ENDSCROLL
 *   X         post SB_LINEDOWN, SB_PAGEUP, SB_ENDSCROLL: two kinds of
 *             code in one gesture
 *   L         post SB_LINEDOWN alone, a gesture whose SB_ENDSCROLL never
 *             comes
 *   G         post the gesture of SB_LINEDOWN, then make the End move,
 *             whose waits, from the first page, dispatch it while busy;
 *             with SHIFT, post SB_LINEDOWN alone, make the End move, then
 *             post SB_ENDSCROLL, which comes after it; with CTRL, send
 *             SB_LINEDOWN and SB_ENDSCROLL (SendMessage), so the
 *             gesture's move is posted at once, then make the End move,
 *             whose waits take that posted move while busy
 *   F         make the request after the next one fail (the
 *             WLT_FAILIN-th from now gets BOGUS=1, refused 102), then
 *             wl_recount and the Home move: its page loads and its
 *             survey's first request fails, the failure's box; with
 *             SHIFT that request is made to fail and no move made, for
 *             the script's next one
 *   R         turn q of the title on, or off
 *   E         post an Esc to the list window at the next WL_BUSY 1 note,
 *             so the first wait of that move finds it
 *   H         SetCapture on the list window, post the box a Down key,
 *             and release the capture two posted messages later
 *             (WLT_WM_FREE1, WLT_WM_FREE2), after the key's posted move
 *             was taken with the capture held
 *   SHIFT-0 to 9, CTRL-0 to 9
 *             what becomes the piece of that column of the box's
 *             selected row, or of its first row when none is selected,
 *             "PIECE d D at n ind x w", or with CTRL of the header,
 *             "HEAD d at n ind x w": D the row's item data (WL_WHOLE for
 *             the empty text, cut with the NULL table), at, n and ind of
 *             wcol_piece, x of wcol_x in the list's font, w the piece's
 *             width (GetTextExtent); "-" for the five when the table has
 *             no column d or the box no row
 *   A         wl_count_add of 1, with SHIFT of -1, then a reload; with
 *             CTRL, ITEM ADD of an item, the list's last by its ID, and
 *             no move, as another station adds one
 *   O         the list made again, as LEAD makes it but without a lead
 *             row, and its first page: "REOPEN r", r the ITEM LIST
 *             requests of that move; plain O counts no rows (flags 0),
 *             SHIFT-O counts them (WL_EXACT), CTRL-O counts them in a
 *             window of one row, CTRL-SHIFT-O too with a lead row (as
 *             LEAD's); C makes it so with WL_EXACT and, when wl_count gave
 *             a count, that count (wl_count_set): "CARRY r", and SHIFT-C
 *             so with a lead row
 *   Esc       closes the frame
 *   any other what becomes "KEY vk s c": the virtual key code in
 *             decimal, then 1 or 0 for SHIFT and CTRL down (GetKeyState)
 *
 * The keys of the headings and dividers are taken by the program's loop
 * before it dispatches them, whatever window has the focus, as a list
 * that holds the capture drops the keys of its box: they do nothing
 * without a list.  "The column" is column g_col of the table of the
 * order shown, the last when it has fewer; a point is in the list
 * window's client area, at the middle of the header line, and each
 * press (DOWN), move (MOVE, with the left button down) and release (UP)
 * is posted to the list window:
 *
 *   J         the next column ("COL k"), SHIFT-J the first, CTRL-J the
 *             one before
 *   M         DOWN and UP at the middle of the column's heading; SHIFT-M
 *             the DOWN alone
 *   I         DOWN on the column's divider, the right edge of its slot;
 *             SHIFT-I WCOL_GRIP - 1 pixels left of it, CTRL-I right
 *   K         MOVE WLT_STEP pixels right of the last point, SHIFT-K
 *             left; CTRL-K WLT_FAR left, CTRL-SHIFT-K right
 *   W         MOVE one pixel right of the last point, SHIFT-W left
 *   Q         UP at the last point; SHIFT-Q WM_CANCELMODE; CTRL-Q
 *             SetCapture on the frame, then post the box a move and
 *             release the capture two posted messages later
 *             (WLT_WM_FREE1, WLT_WM_FREE2), so the box gets a message
 *             without the capture; CTRL-SHIFT-Q posts the frame
 *             WLT_WM_GONE, on which it destroys the list
 *   V         what becomes "W k w0,...", the widths kept (wcol_widths)
 *             for the table of the order shown, of keep k, up to the
 *             last that is not 0, or "W k 0"
 *   Z         what becomes "PIX a b c", the screen's pixels (GetPixel),
 *             six hex digits each, at the last point's x on the header
 *             line and on the list's last row of pixels, and at the last
 *             press's x on the header line: the divider's guide shows
 *             there while a drag is on
 *   Y         what becomes "ROWS n", the rows wl_rows fits in a window
 *             as wide as the list's and as high as wl_height makes one of
 *             WLT_ROWS + 1 rows, for the table of the order shown: fewer
 *             once the stored widths make its row wider than that window
 *
 * The tables keep their widths, the ID order's in row 1 and the SERIAL
 * order's in row 2 of a far block of the program's (wcol_keep), and the
 * list is opened with WL_SIZES, but after S, which makes it again
 * without it, as plain O does ("SIZES r"), and again with it after
 * another S; a WL_COLS note makes what "COLS k".  The
 * ID heading names the ID order in both tables and the serial number's
 * the SERIAL order; the location's of the ID order names an order the
 * source lacks, so that it is flat.
 *
 * With /DRAG the list is opened with WL_DRAGS, so that its rows can be
 * dragged, and is its own target: each WL_DRAG note frames the row under
 * the drag's last point (wl_drag_over, wl_target) while the drag moves
 * and none once it ends, and what becomes "DRAG n s i key", n the
 * WL_DRAG notes so far, s the note's WL_DR_ step and i and key the row
 * wl_drag_over gives (-1 and "-" for none), or "DRAG n s" for WL_DR_END.
 * With /MULTI it is opened with WL_MULTI, so that it chooses several rows
 * and all of them; its WL_SEL and WL_COUNT notes do nothing, the title
 * showing the rows chosen after every message.
 *
 * On WL_LEAVE what becomes "LEAVE n", n the action, and the owner
 * destroys the list alone and gives the frame the focus, where Esc
 * closes it.  Every WL_BUSY note holds the test driver (td_busy).  A
 * failure to start puts "FAIL step n" in what, step the part that
 * failed and n its code, for the snapshot to show.
 *
 * TEST/WLISTT.KEY moves through the pages, to the end and back home,
 * changes the order, clicks, reloads, opens, types, stops a walk,
 * filters and fails twice; every mode of Windows must give
 * TEST/GOLDEN/WIN/WLISTT.SNP byte for byte.  TEST/WLISTTM.KEY seeks rows
 * by letters and by wl_find, and shows a lead row, and WLISTTN.KEY
 * destroys the list during a move.  The scroll bar's scripts: WLISTTO.KEY
 * moves the bar of the seed's three rows; WLISTTP.KEY, WLISTTQ.KEY,
 * WLISTTR.KEY and WLISTTS.KEY run on eleven items, the seed's and those
 * of TEST/KEYS/WLLONG.REQ, P the bar of a counted list, Q the growing
 * range and a count carried or counted again, R the count the owner
 * adjusts, S a window of one row; WLISTTT.KEY, on the eleven items too,
 * the jumps' requests, a failed survey, Esc stopping a jump, a move and
 * a survey, and a count carried to a list with a lead row; WLISTTU.KEY,
 * on the seed's, gestures while a move is busy, keys taken while the
 * list window holds the capture, and the pieces of rows and header;
 * WLISTTV.KEY, on the seed's, the headings' clicks and the dividers'
 * drags, and WLISTTW.KEY a drag shorter than half a character, the rows
 * that stored widths cost and a list without dividers.  WLISTTX.KEY,
 * with /DRAG on the seed's, drags rows: the threshold, the target, the
 * drop and the ends of a drag and of a press.  WLISTTZ.KEY, with /MULTI
 * on the seed's items and 257 more (TEST/WIN/mkitems.sh), chooses every
 * row: what keeps and ends the choice, a row added since, and what a
 * choice stopped, failed, refused or of an empty list leaves chosen and
 * counted.
 */
#define PAGER_CALLS
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PAGER.H"
#include "TDRV.H"
#include "WLIST.H"

#define WLT_CMDMAX      128     /* a DOS command tail and its NUL       */
/* What a title says, NUL included: the longest, V's, "W k " and
   WCOL_MAX widths of three digits, a comma between two. */
#define WLT_TITLESZ     (4 + 4 * WCOL_MAX)
#define WLT_ROWS        2       /* rows of the list's window            */
#define WLT_LEVEL       3       /* the access level of the seed's ADMIN */
#define WLT_LISTID      1       /* the list window's control ID         */
#define WLT_ORDSER      1       /* index of the SERIAL order            */
#define WLT_QTY         "9"     /* the quantity F5 sets                 */
#define WLT_NOCAT       "NONE"  /* a category no item has               */
#define WLT_F_NONE      0       /* the filters of item_args: none,      */
#define WLT_F_EMPTY     1       /* CAT=WLT_NOCAT,                       */
#define WLT_F_BAD       2       /* a key ITEM LIST refuses (102)        */
#define WLT_FINDID      "3"     /* the item F4 seeks                    */
#define WLT_FIRSTID     "1"     /* and SHIFT-F7, the first              */
#define WLT_LEAD        "(lead)"        /* the lead row of CTRL-F4      */
#define WLT_LASTW       8       /* characters SHIFT-F1 shows of the
                                   last chosen row's text               */
#define WLT_WM_GONE     (WM_USER + 1)   /* destroy the list             */
#define WLT_WM_SHOW     (WM_USER + 2)   /* make the title again         */
#define WLT_WM_FREE1    (WM_USER + 3)   /* post WLT_WM_FREE2            */
#define WLT_WM_FREE2    (WM_USER + 4)   /* release the capture          */
#define WLT_FAILIN      2       /* F: the request refused, counted from
                                   the next                             */
#define WLT_REPEAT      3       /* codes of a SHIFT gesture, more than
                                   WLT_ROWS                             */
#define WLT_STEP        20      /* pixels of K's move                   */
#define WLT_FAR         2000    /* of CTRL-K's, past any width          */
/* A title: what, " #", the moves, " ", the bar's position, "/", its
   range, " ", the count, " ", the rows chosen and "*" (/MULTI), " " and
   the requests (R), and the NUL. */
#define WLT_SHOWSZ      (WLT_TITLESZ + 6 * TXT_U32LEN + 9)

/* The columns of a row, laid out under the headers of the item list of
   ITEMLS.C, so that each header word lies in its column: copies of its
   MI_HDR (ID order) and MI_HDRSER (SERIAL order), with its empty text
   MI_EMPTY, which SCRITEM.C keeps to itself; the location is its name,
   cut, and the status column is as wide as its header. */
#define WLT_HDR         454
#define WLT_HDRSER      455
#define WLT_EMPTY       456
#define WC_ID           8
#define WC_NAME         15
#define WC_WORKS        5
#define WC_QTY          5
#define WC_LOC          10
#define WC_STATUS       6
#define WC_SERIAL       20
#define WC_SLOC         10

static const char k_keys[] = "/KEYS:";
static const char k_snap[] = "/SNAP:";
static const char k_drag[] = "/DRAG";
static const char k_multi[] = "/MULTI";
static const char k_class[] = "WLISTT";
static const char k_itemid[] = "ITEM_ID";
static const char k_locname[] = "LOC_NAME";

/* The columns of item_fmt's rows, by order. */
static const wcol cols_id[] = {
    { WC_ID, WCOL_ORDER(0) }, { LINK_CATLEN, 0 }, { WC_NAME, 0 },
    { WC_NAME, 0 }, { WC_WORKS, 0 }, { WC_QTY, WCOL_RIGHT },
    { WC_LOC, WCOL_ORDER(WLT_ORDSER + 1) }, { WC_STATUS, 0 }
};
static const wcol cols_ser[] = {
    { WC_ID, WCOL_ORDER(0) }, { WC_NAME, 0 }, { WC_NAME, 0 },
    { WC_SERIAL, WCOL_ORDER(WLT_ORDSER) }, { WC_SLOC, 0 }
};
static const wcol_tab cols[] = {
    { sizeof cols_id / sizeof cols_id[0], 1, cols_id },
    { sizeof cols_ser / sizeof cols_ser[0], 2, cols_ser }
};

/* The keys of the scroll bar's gestures and their codes. */
static const struct {
    UINT vk;
    UINT code;
} gestures[] = {
    { 'U', SB_LINEUP }, { 'D', SB_LINEDOWN }, { 'P', SB_PAGEUP },
    { 'N', SB_PAGEDOWN }, { 'T', SB_TOP }, { 'B', SB_BOTTOM }
};

static HINSTANCE g_inst;
static HWND g_frame;
static link *g_l;
static wl *g_v;
static int g_filter;            /* WLT_F_ in use                        */
static u16 g_moves;             /* WL_BUSY 1 notes so far               */
static u16 g_fetches;           /* ITEM LIST requests so far            */
static u16 g_moveq;             /* those since the last WL_BUSY 1 note  */
static u16 g_failin;            /* requests until the one refused, or 0 */
static int g_showq;             /* the title shows g_moveq (R)          */
static int g_col;               /* the column of the mouse's keys       */
static POINT g_pt;              /* their last point                     */
static POINT g_dn;              /* and their last press's               */
static u8 g_sizes = WL_SIZES;   /* the list's WL_SIZES, or 0 after S    */
static u8 g_drags;              /* WL_DRAGS with /DRAG, else 0          */
static u8 g_multi;              /* WL_MULTI with /MULTI, else 0         */
static u16 g_dragn;             /* WL_DRAG notes so far                 */
static int g_escbusy;           /* post an Esc at the next WL_BUSY 1 (E) */
static char g_what[WLT_TITLESZ] = "WLISTT"; /* the title but the count  */

/* ---------------------------------------------------------------------
 * The item list.
 * --------------------------------------------------------------------- */

static void item_args(void *arg, rq *q)
{
    (void)arg;
    g_fetches++;
    g_moveq++;
    if (g_failin > 0 && --g_failin == 0)
        rq_pair(q, "BOGUS", "1");
    else if (g_filter == WLT_F_EMPTY)
        rq_pair(q, "CAT", WLT_NOCAT);
    else if (g_filter == WLT_F_BAD)
        rq_pair(q, "BOGUS", "1");
}

static void item_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    const char *id;
    u16 at;

    (void)arg;
    id = lv_val(r, k_itemid);
    while (*id == ' ')
        id++;
    txt_copy(key, id, LV_KEYSZ);
    at = 0;
    lv_col(text, &at, id, WC_ID, 0);
    if (wl_order(g_v) == WLT_ORDSER) {
        lv_col(text, &at, lv_val(r, "MFR"), WC_NAME, 0);
        lv_col(text, &at, lv_val(r, "MODEL"), WC_NAME, 0);
        lv_col(text, &at, lv_val(r, "SERIALNO"), WC_SERIAL, 0);
        lv_col(text, &at, lv_val(r, k_locname), WC_SLOC, 0);
    } else {
        lv_col(text, &at, lv_val(r, "CATEGORY"), LINK_CATLEN, 0);
        lv_col(text, &at, lv_val(r, "MFR"), WC_NAME, 0);
        lv_col(text, &at, lv_val(r, "MODEL"), WC_NAME, 0);
        lv_col(text, &at, lv_val(r, "WORKS"), WC_WORKS, 0);
        lv_col(text, &at, lv_val(r, "QTY"), WC_QTY, 1);
        lv_col(text, &at, lv_val(r, k_locname), WC_LOC, 0);
        lv_col(text, &at, lv_val(r, "STATUS"), WC_STATUS, 0);
    }
}

static int item_mark(void *arg, const lv_rec *r)
{
    (void)arg;
    return strcmp(lv_val(r, "CATEGORY"), "DRV") == 0;
}

static const lv_src item_src = {
    "ITEM LIST", 2,
    { "ID", "SERIAL" },
    { NULL, "ITEM_ID,MFR,MODEL,SERIALNO,LOC_NAME" },
    { WLT_HDR, WLT_HDRSER },
    WLT_EMPTY, k_itemid, item_args, item_fmt, NULL, item_mark
};

/* ---------------------------------------------------------------------
 * The owner.
 * --------------------------------------------------------------------- */

/* Make the frame's title, as this file's header gives it, when it
   changed and the frame is still there. */
static void show_title(void)
{
    char t[WLT_SHOWSZ];
    char was[WLT_SHOWSZ];
    HWND w;
    u32 n;
    int lo;
    int hi;
    int k;

    if (g_frame == NULL)
        return;
    k = wsprintf(t, "%s #%u", (LPCSTR)g_what, g_moves);
    if (g_v != NULL) {
        w = wl_hwnd(g_v);
        GetScrollRange(w, SB_VERT, &lo, &hi);
        k += wsprintf(t + k, " %d/%d ", GetScrollPos(w, SB_VERT), hi);
        if (wl_count(g_v, &n))
            k += wsprintf(t + k, "%lu", n);
        else
            k += wsprintf(t + k, "-");
        if (g_multi)
            k += wsprintf(t + k, " %u%s", wl_nchosen(g_v),
                          (LPCSTR)(wl_all(g_v) ? "*" : ""));
        if (g_showq)
            wsprintf(t + k, " %u", g_moveq);
    }
    GetWindowText(g_frame, was, (int)sizeof was);
    if (strcmp(t, was) != 0)
        SetWindowText(g_frame, t);
}

/* Say s, then n, as what the owner last saw. */
static void title(const char *s, u32 n)
{
    wsprintf(g_what, "%s %lu", (LPCSTR)s, n);
}

/* Post the list window of v the scroll bar's code n times, its thumb at
   pos, then SB_ENDSCROLL when end is 1. */
static void gesture(wl *v, UINT code, int n, int pos, int end)
{
    HWND w;

    w = wl_hwnd(v);
    for (; n > 0; n--)
        PostMessage(w, WM_VSCROLL, code, MAKELPARAM(pos, 0));
    if (end)
        PostMessage(w, WM_VSCROLL, SB_ENDSCROLL, 0L);
}

/* Send q, its response into res; res->code. */
static int request(rq *q, rq_res *res)
{
    return rq_send(g_l, q, NULL, NULL, res);
}

/* Post to the list box of v a click on its row r, a double click when
   dbl is 1, as the mouse makes them. */
static void click(wl *v, int r, int dbl)
{
    HWND box;
    RECT rc;
    LPARAM at;

    box = GetDlgItem(wl_hwnd(v), WL_BOXID);
    SetRectEmpty(&rc);
    SendMessage(box, LB_GETITEMRECT, (WPARAM)r, (LPARAM)(RECT FAR *)&rc);
    at = MAKELPARAM(rc.left + 1, (rc.top + rc.bottom) / 2);
    PostMessage(box, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessage(box, WM_LBUTTONUP, 0, at);
    if (dbl) {
        PostMessage(box, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        PostMessage(box, WM_LBUTTONUP, 0, at);
    }
}

static void note(void *arg, wl *v, int ev, UINT val);

/* Open the list in a window of rows rows with flags, a lead row given
   when lead is 1 and the count *n when n is not NULL (wl_count_set),
   give it the focus and load its first page; 0 when it does not open,
   said in the title. */
static int list(int lead, u8 flags, u8 rows, const u32 *n)
{
    int rc;

    rc = wl_open(&g_v, g_frame, WLT_LISTID, g_l, &item_src, cols, rows,
                 WLT_LEVEL, (u8)(flags | g_sizes | g_drags | g_multi), note,
                 NULL);
    if (rc != PG_OK) {
        title("FAIL LIST", (u32)rc);
        return 0;
    }
    if (lead)
        wl_lead(g_v, "0", WLT_LEAD);
    if (n != NULL)
        wl_count_set(g_v, *n);
    SetFocus(wl_hwnd(g_v));
    wl_move(g_v, PG_HOME);
    return 1;
}

/* Make the list again (key O, C or S, flags and rows as list takes them,
   the count *n when n is not NULL) and say what, then the ITEM LIST
   requests of its first page. */
static void reopen(const char *what, int lead, u8 flags, u8 rows,
                   const u32 *n)
{
    DestroyWindow(wl_hwnd(g_v));
    g_v = NULL;
    g_fetches = 0;
    if (list(lead, flags, rows, n))
        title(what, (u32)g_fetches);
}

/* Say in the title the piece of column d of the box's selected row (its
   first row when none is selected), or of the header when head is 1,
   as WCOLS.H cuts and places it in the list's font: "PIECE d D at n ind
   x w" (D the row's item data) or "HEAD d at n ind x w", w the piece's
   width as GetTextExtent measures it; "-" in place of at n ind x w when
   the row's table has no column d or the box no row. */
static void piece(wl *v, int d, int head)
{
    char text[LV_ROWTEXT + 1];
    const wcol_tab *t;
    TEXTMETRIC tm;
    wcol_lay lay;
    HWND box;
    HDC dc;
    LRESULT r;
    LRESULT data;
    int len;
    int at;
    int ind;
    int n;
    int w;
    int k;

    box = GetDlgItem(wl_hwnd(v), WL_BOXID);
    t = &cols[wl_order(v)];
    data = 0;
    if (head) {
        len = GetWindowText(wl_hwnd(v), text, (int)sizeof text);
        k = wsprintf(g_what, "HEAD %d", d);
    } else {
        r = SendMessage(box, LB_GETCURSEL, 0, 0L);
        if (r < 0)
            r = 0;
        len = (int)SendMessage(box, LB_GETTEXT, (WPARAM)r,
                               (LPARAM)(LPSTR)text);
        data = SendMessage(box, LB_GETITEMDATA, (WPARAM)r, 0L);
        if (data == WL_WHOLE)
            t = NULL;
        k = wsprintf(g_what, "PIECE %d %ld", d, (long)data);
    }
    if (len < 0 || d >= (t != NULL ? t->n : 1)) {
        strcpy(g_what + k, " -");
        return;
    }
    if (len > LV_ROWTEXT)
        len = LV_ROWTEXT;
    OemToAnsiBuff(text, text, len);
    n = wcol_piece(t, d, text, len, &at, &ind);
    dc = GetDC(box);
    SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    GetTextMetrics(dc, &tm);
    w = n > 0 ? (int)LOWORD(GetTextExtent(dc, text + at, n)) : 0;
    ReleaseDC(box, dc);
    wcol_layout(&lay, t, &tm);
    wsprintf(g_what + k, " %d %d %d %d %d", at, n, ind,
             wcol_x(t, &lay, d, ind, w), w);
}

/* Turn filter f on, or off when it is on, and go to the first page,
   the list counted again (wl_recount), as its rows changed. */
static void filter(wl *v, int f)
{
    g_filter = g_filter == f ? WLT_F_NONE : f;
    wl_recount(v);
    wl_move(v, PG_HOME);
}

/* The keys the list leaves to its owner, as this file's header gives
   them. */
static void owner_key(wl *v, UINT vk)
{
    char text[LV_ROWTEXT + 1];
    char key[LV_KEYSZ];
    int shift;
    int ctrl;
    int i;
    u32 n;
    MSG m;
    rq q;
    rq_res res;

    shift = GetKeyState(VK_SHIFT) < 0;
    ctrl = GetKeyState(VK_CONTROL) < 0;
    for (i = 0; i < (int)(sizeof gestures / sizeof gestures[0]); i++)
        if (gestures[i].vk == vk) {
            gesture(v, gestures[i].code, shift ? WLT_REPEAT : 1, 0, 1);
            if (ctrl)
                gesture(v, gestures[i].code, 1, 0, 1);
            return;
        }
    if (vk >= '0' && vk <= '9' && (shift || ctrl)) {
        piece(v, (int)(vk - '0'), ctrl);
        return;
    }
    if (vk >= '0' && vk <= '9') {
        gesture(v, SB_THUMBTRACK, 1, (int)(vk - '0'), 0);
        gesture(v, SB_THUMBPOSITION, 1, (int)(vk - '0'), 1);
        return;
    }
    if (vk == VK_TAB && !shift && !ctrl) {
        wl_move(v, PG_ORDER);
        return;
    }
    if (vk == WL_VK_ALL && ctrl) {
        title("ALL", (u32)wl_select_all(v));
        return;
    }
    if (vk == WL_VK_NONE && ctrl) {
        wl_deselect(v);
        return;
    }
    if (vk == VK_F1 && shift) {
        strcpy(text, "?");
        wl_chosen(v, (u16)(wl_nchosen(v) - 1), key, text);
        text[WLT_LASTW] = '\0';
        wsprintf(g_what, "LAST %s [%s]", (LPCSTR)key, (LPCSTR)text);
        return;
    }
    switch (vk) {
    case VK_F4:
        if (ctrl) {
            DestroyWindow(wl_hwnd(v));
            g_v = NULL;
            if (list(1, WL_EXACT, WLT_ROWS, NULL))
                title("LEAD", 0);
        } else if (shift) {
            wl_letters(v);
            title("LETTERS", 0);
        } else {
            title("FIND", (u32)wl_find(v, WLT_FINDID));
        }
        break;
    case VK_RETURN:
        wl_selected(v, key);
        wsprintf(g_what, "OPEN %s", (LPCSTR)key);
        break;
    case VK_F2:
    case VK_F3:
        click(v, shift ? 0 : 1, vk == VK_F3);
        break;
    case VK_F5:
        if (wl_selected(v, key)) {
            rq_begin(&q, "ITEM SET");
            rq_word(&q, key);
            rq_pair(&q, "QTY", WLT_QTY);
            if (request(&q, &res) != RQ_OK)
                title("FAIL SET", (u32)res.code);
        }
        wl_move(v, PG_RELOAD);
        break;
    case VK_F6:
        filter(v, WLT_F_EMPTY);
        break;
    case VK_F7:
        if (ctrl) {
            g_fetches = 0;
            PostMessage(g_frame, WLT_WM_GONE, 0, 0L);
            wl_move(v, PG_END);
            title("GONE", (u32)g_fetches);
        } else if (!shift) {
            PostMessage(wl_hwnd(v), WM_KEYDOWN, VK_ESCAPE, 0L);
            wl_move(v, PG_END);
        } else {
            PostMessage(wl_hwnd(v), WM_KEYDOWN, VK_ESCAPE, 0L);
            g_fetches = 0;
            wl_find(v, WLT_FIRSTID);
            wsprintf(g_what, "FIRST %u %d", g_fetches,
                     PeekMessage(&m, wl_hwnd(v), WM_KEYDOWN, WM_KEYDOWN,
                                 PM_REMOVE) != 0);
        }
        break;
    case VK_F8:
        filter(v, WLT_F_BAD);
        break;
    case VK_F9:
        rq_begin(&q, "BYE");
        request(&q, &res);
        wl_move(v, PG_RELOAD);
        break;
    case 'X':
        gesture(v, SB_LINEDOWN, 1, 0, 0);
        gesture(v, SB_PAGEUP, 1, 0, 1);
        break;
    case 'L':
        gesture(v, SB_LINEDOWN, 1, 0, 0);
        break;
    case 'G':
        if (ctrl) {
            SendMessage(wl_hwnd(v), WM_VSCROLL, SB_LINEDOWN, 0L);
            SendMessage(wl_hwnd(v), WM_VSCROLL, SB_ENDSCROLL, 0L);
            wl_move(v, PG_END);
        } else {
            gesture(v, SB_LINEDOWN, 1, 0, !shift);
            wl_move(v, PG_END);
            if (shift)
                gesture(v, SB_ENDSCROLL, 0, 0, 1);
        }
        break;
    case 'F':
        g_failin = WLT_FAILIN;
        if (!shift) {
            wl_recount(v);
            wl_move(v, PG_HOME);
        }
        break;
    case 'R':
        g_showq = !g_showq;
        break;
    case 'E':
        g_escbusy = 1;
        break;
    case 'H':
        SetCapture(wl_hwnd(v));
        PostMessage(GetDlgItem(wl_hwnd(v), WL_BOXID), WM_KEYDOWN, VK_DOWN,
                    0L);
        PostMessage(g_frame, WLT_WM_FREE1, 0, 0L);
        break;
    case 'A':
        if (ctrl) {
            rq_begin(&q, "ITEM ADD");
            rq_pair(&q, "CATEGORY", "PART");
            rq_pair(&q, "MODEL", "Added");
            rq_pair(&q, "LOC_ID", "B2");
            if (request(&q, &res) != RQ_OK)
                title("FAIL ADD", (u32)res.code);
        } else {
            wl_count_add(v, shift ? -1 : 1);
            wl_move(v, PG_RELOAD);
        }
        break;
    case 'O':
        reopen("REOPEN", shift && ctrl, (u8)(shift || ctrl ? WL_EXACT : 0),
               (u8)(ctrl ? 1 : WLT_ROWS), NULL);
        break;
    case 'C':
        reopen("CARRY", shift, WL_EXACT, WLT_ROWS,
               wl_count(v, &n) ? &n : NULL);
        break;
    case 'S':
        g_sizes ^= WL_SIZES;
        reopen("SIZES", 0, 0, WLT_ROWS, NULL);
        break;
    case VK_ESCAPE:
        PostMessage(g_frame, WM_CLOSE, 0, 0L);
        break;
    default:
        wsprintf(g_what, "KEY %u %d %d", vk, shift, ctrl);
        break;
    }
}

/* A WL_DRAG note of step val, as this file's header gives it. */
static void drag(wl *v, UINT val)
{
    char key[LV_KEYSZ];
    int i;
    int k;

    g_dragn++;
    k = wsprintf(g_what, "DRAG %u %u", g_dragn, val);
    if (val == WL_DR_END) {
        wl_target(v, WL_NOROW);
        return;
    }
    key[0] = '\0';
    i = wl_drag_over(v, v, key);
    wl_target(v, val == WL_DR_DROP ? WL_NOROW : i);
    wsprintf(g_what + k, " %d %s", i, i >= 0 ? (LPCSTR)key : (LPCSTR)"-");
}

static void note(void *arg, wl *v, int ev, UINT val)
{
    (void)arg;
    if (ev == WL_BUSY) {
        if (val) {
            g_moves++;
            g_moveq = 0;
            if (g_escbusy)
                PostMessage(wl_hwnd(v), WM_KEYDOWN, VK_ESCAPE, 0L);
            g_escbusy = 0;
            show_title();
        } else {
            PostMessage(g_frame, WLT_WM_SHOW, 0, 0L);
        }
        td_busy((int)val);
    } else if (ev == WL_COLS) {
        title("COLS", (u32)val);
    } else if (ev == WL_DRAG) {
        drag(v, val);
    } else if (ev == WL_LEAVE) {
        title("LEAVE", (u32)val);
        DestroyWindow(wl_hwnd(v));
        g_v = NULL;
        SetFocus(g_frame);
    } else if (ev == WL_KEY) {
        owner_key(v, val);
    }
}

/* Post the list window wm at the point p, a press or a move with the
   left button down. */
static void mouse(UINT wm, POINT p)
{
    PostMessage(wl_hwnd(g_v), wm, wm == WM_LBUTTONUP ? 0 : MK_LBUTTON,
                MAKELPARAM((UINT)p.x, (UINT)p.y));
}

/* The screen's pixel at the list's client point (x, y), six hex digits
   into t: the frame clips its children, so its own display context
   shows none of the list's. */
static int pixel(int x, int y, char *t)
{
    POINT p;
    HDC dc;
    u32 c;

    p.x = x;
    p.y = y;
    ClientToScreen(wl_hwnd(g_v), &p);
    dc = GetDC(NULL);
    c = (u32)GetPixel(dc, p.x, p.y);
    ReleaseDC(NULL, dc);
    return wsprintf(t, " %06lX", c);
}

/* Carry out vk if it is a key of the headings and dividers (this file's
   header): 1, or 0 for any other key. */
static int mouse_key(UINT vk)
{
    const wcol_tab *t;
    TEXTMETRIC tm;
    wcol_lay lay;
    RECT r;
    HDC dc;
    u8 w[WCOL_MAX];
    int shift;
    int ctrl;
    int k;
    int n;
    int x;

    if (vk != 'I' && vk != 'J' && vk != 'K' && vk != 'M' && vk != 'Q' &&
        vk != 'V' && vk != 'W' && vk != 'Y' && vk != 'Z')
        return 0;
    if (g_v == NULL)
        return 1;
    shift = GetKeyState(VK_SHIFT) < 0;
    ctrl = GetKeyState(VK_CONTROL) < 0;
    t = &cols[wl_order(g_v)];
    dc = GetDC(g_frame);
    SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    GetTextMetrics(dc, &tm);
    wcol_layout(&lay, t, &tm);
    k = g_col < t->n ? g_col : t->n - 1;
    x = GetSystemMetrics(SM_CXBORDER) - GetScrollPos(wl_hwnd(g_v), SB_HORZ);
    switch (vk) {
    case 'J':
        g_col = shift ? 0 : (g_col + (ctrl ? t->n - 1 : 1)) % t->n;
        title("COL", (u32)g_col);
        break;
    case 'M':
        g_pt.x = x + (lay.x[k] + lay.x[k + 1]) / 2;
        g_pt.y = tm.tmHeight / 2;
        g_dn = g_pt;
        mouse(WM_LBUTTONDOWN, g_pt);
        if (!shift)
            mouse(WM_LBUTTONUP, g_pt);
        break;
    case 'I':
        g_pt.x = x + lay.x[k + 1] + (shift ? 1 - WCOL_GRIP :
                                     ctrl ? WCOL_GRIP - 1 : 0);
        g_pt.y = tm.tmHeight / 2;
        g_dn = g_pt;
        mouse(WM_LBUTTONDOWN, g_pt);
        break;
    case 'K':
        n = ctrl ? WLT_FAR : WLT_STEP;
        g_pt.x += ctrl == shift ? n : -n;
        mouse(WM_MOUSEMOVE, g_pt);
        break;
    case 'W':
        g_pt.x += shift ? -1 : 1;
        mouse(WM_MOUSEMOVE, g_pt);
        break;
    case 'Y':
        GetWindowRect(wl_hwnd(g_v), &r);
        title("ROWS", wl_rows(g_frame, wl_height(g_frame, WLT_ROWS + 1),
                              r.right - r.left, t));
        break;
    case 'Q':
        if (ctrl && shift) {
            PostMessage(g_frame, WLT_WM_GONE, 0, 0L);
        } else if (ctrl) {
            SetCapture(g_frame);
            PostMessage(GetDlgItem(wl_hwnd(g_v), WL_BOXID), WM_MOUSEMOVE,
                        MK_LBUTTON, 0L);
            PostMessage(g_frame, WLT_WM_FREE1, 0, 0L);
        } else if (shift) {
            PostMessage(wl_hwnd(g_v), WM_CANCELMODE, 0, 0L);
        } else {
            mouse(WM_LBUTTONUP, g_pt);
        }
        break;
    case 'V':
        wcol_widths(t->keep, w);
        for (n = WCOL_MAX; n > 1 && w[n - 1] == 0; n--)
            ;
        x = wsprintf(g_what, "W %u ", t->keep);
        for (k = 0; k < n; k++)
            x += wsprintf(g_what + x, k > 0 ? ",%u" : "%u", w[k]);
        break;
    default:
        GetClientRect(wl_hwnd(g_v), &r);
        x = wsprintf(g_what, "PIX");
        x += pixel(g_pt.x, tm.tmHeight / 2, g_what + x);
        x += pixel(g_pt.x, r.bottom - 1, g_what + x);
        pixel(g_dn.x, tm.tmHeight / 2, g_what + x);
        break;
    }
    ReleaseDC(g_frame, dc);
    return 1;
}

LRESULT WCALLBACK wlt_frame(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    switch (wm) {
    case WM_SETFOCUS:
        if (g_v != NULL)
            SetFocus(wl_hwnd(g_v));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            PostMessage(w, WM_CLOSE, 0, 0L);
        return 0;
    case WLT_WM_GONE:
        DestroyWindow(wl_hwnd(g_v));
        g_v = NULL;
        SetFocus(w);
        return 0;
    case WLT_WM_SHOW:
        show_title();
        return 0;
    case WLT_WM_FREE1:
        PostMessage(w, WLT_WM_FREE2, 0, 0L);
        return 0;
    case WLT_WM_FREE2:
        ReleaseCapture();
        return 0;
    case WM_DESTROY:
        g_v = NULL;
        g_frame = NULL;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(w, wm, wp, lp);
}

/* ---------------------------------------------------------------------
 * Start and end.
 * --------------------------------------------------------------------- */

/* Make the frame; 0 when Windows refuses. */
static int make_frame(int show)
{
    WNDCLASS wc;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wlt_frame;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = k_class;
    if (RegisterClass(&wc) == 0)
        return 0;
    g_frame = CreateWindow(k_class, k_class,
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL,
                           g_inst, NULL);
    if (g_frame == NULL)
        return 0;
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    return 1;
}

/* Load the messages, open the link on dir, log in and open the list,
   its first page loaded; a failure said in the title. */
static void start(const char *dir)
{
    char why[LINK_WHYSZ];
    rq q;
    rq_res res;
    int rc;

    rc = msg_load("", MSG_FILE);
    if (rc != 0) {
        title("FAIL MSG", (u32)rc);
        return;
    }
    rc = link_open_local(&g_l, dir, NULL, why, (u16)sizeof why);
    if (rc != 0) {
        title("FAIL OPEN", (u32)rc);
        return;
    }
    rq_begin(&q, "HELLO");
    rq_word(&q, k_class);
    if (request(&q, &res) == RQ_OK) {
        rq_begin(&q, "LOGIN");
        rq_word(&q, "ADMIN");
        rq_word(&q, "Retro1986");
        request(&q, &res);
    }
    if (res.code != RQ_OK) {
        title("FAIL LOGIN", (u32)res.code);
        return;
    }
    list(0, WL_EXACT, WLT_ROWS, NULL);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char words[WLT_CMDMAX];
    const char *keys;
    const char *snap;
    const char *dir;
    cmem_blk *widths;
    char *p;
    MSG m;

    (void)prev;
    for (p = words; *cmd != '\0' && p < words + sizeof words - 1; )
        *p++ = *cmd++;
    *p = '\0';
    keys = NULL;
    snap = NULL;
    dir = "";
    for (p = words; *p != '\0'; ) {
        while (*p == ' ')
            *p++ = '\0';
        if (txt_ieqn(p, k_keys, sizeof k_keys - 1))
            keys = p + sizeof k_keys - 1;
        else if (txt_ieqn(p, k_snap, sizeof k_snap - 1))
            snap = p + sizeof k_snap - 1;
        else if (txt_ieqn(p, k_drag, sizeof k_drag - 1))
            g_drags = WL_DRAGS;
        else if (txt_ieqn(p, k_multi, sizeof k_multi - 1))
            g_multi = WL_MULTI;
        else if (*p != '\0')
            dir = p;
        while (*p != '\0' && *p != ' ')
            p++;
    }
    td_init(inst, keys, snap);
    g_inst = inst;
    dos_inst = (u16)inst;
    m.wParam = 0;
    widths = cmem_alloc(2 * WCOL_MAX);
    if (widths != NULL) {
        cmem_set(widths, 0, 0, 2 * WCOL_MAX);
        wcol_keep(widths);
    }
    if (make_frame(show)) {
        start(dir);
        while (GetMessage(&m, NULL, 0, 0)) {
            if (m.message != WM_KEYDOWN || !mouse_key(m.wParam)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
            show_title();
        }
    }
    wcol_keep(NULL);
    cmem_free(widths);
    link_close(g_l);
    msg_free();
    td_done();
    return (int)m.wParam;
}

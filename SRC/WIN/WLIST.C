/*
 * WLIST.C - the paged list window of the Windows programs (WLIST.H).
 *
 * The window's procedure owns the list: window word 0 holds the near
 * pointer to it, set as soon as the window exists, before the list box
 * whose WM_MEASUREITEM needs it is made, and cleared as the window is
 * destroyed, so a message that comes before or after goes to
 * DefWindowProc.  The list box's procedure is replaced by the box's own
 * (subclassing), which takes its keys and characters and passes the
 * rest on; its keys, its characters while the list takes them
 * (wl_letters), and the clicks the box reports, or in a list of several
 * rows takes itself, come back to the window as WM_WLKEY, WM_WLCHAR and
 * WM_WLPICK, posted, a scroll bar gesture's move as WM_WLSCROLL and a
 * heading's click as WM_WLORDER, so that every move and note runs from
 * the message loop, never from inside the list box's or the scroll bar's
 * code.
 *
 * A heading held or a divider dragged holds the capture (drag), which
 * makes taken() drop the box's keys meanwhile, and the guide of a drag
 * is inverted on the screen itself, as the list window and its parent
 * clip their children (WS_CLIPCHILDREN) and the guide runs down the
 * window and its box.  A row's
 * drag lives in the box's procedure, where its press, moves and release
 * come whichever loop dispatches them; the box holds the capture, and
 * the release posts WM_WLDROP, so that the owner's drop runs from the
 * message loop.
 *
 * The box is drawn from its own strings, and the header in the columns
 * of the order it shows, never from the pager, whose rows and order
 * change during a move while the waits dispatch WM_PAINT; the focus row
 * is a box row too (focus), set with the box's selection.  A box row's
 * item data says how it is drawn: WL_WHOLE (the empty text) whole, else
 * as a row, WL_DMARK in red, WL_DDROP framed.
 *
 * All rows chosen (all) is the key set alone, with the rows of each box
 * filled whose keys it holds selected to show it (set_rows).  A gesture
 * that ends it only marks it
 * (WL_A_ENDS); the move the gesture makes ends it as it sets the box's
 * selection (all_rule), so that the rows chosen are counted before and
 * after in one place (kept) and a busy move's fill shows one row.
 */
#define PAGER_CALLS
#define RQERR_CLASS
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PAGER.H"
#include "PAGERSV.H"
#include "KEYSET.H"
#include "RQERR.H"
#include "WDLG.H"
#include "WLIST.H"

#define WM_WLKEY        (WM_USER + 0)   /* wParam a virtual key code    */
#define WM_WLPICK       (WM_USER + 1)   /* wParam a box row clicked,
                                           lParam its WL_R_ rule        */
#define WM_WLCHAR       (WM_USER + 2)   /* wParam a character typed     */
#define WM_WLSCROLL     (WM_USER + 3)   /* wParam a gesture's code,
                                           lParam its count and thumb  */
#define WM_WLORDER      (WM_USER + 4)   /* wParam an order clicked      */
#define WM_WLDROP       (WM_USER + 5)   /* a row's drag released        */
#define WL_ALERT        RGB(255, 0, 0)  /* the text of a marked row     */
#define WL_DMARK        1       /* item data: a marked row              */
#define WL_JUMP         (PG_ORDER + 1)  /* run's jump, beside the moves */
#define WL_ALL          (PG_ORDER + 2)  /* run's choice of every row    */
#define WL_NOSB         0xFF    /* no gesture recorded                  */
#define WL_ALTKEY       0x20000000L     /* a key's lParam: ALT held     */
#define WL_D_HEAD       1       /* drag: a heading is held              */
#define WL_D_SIZE       2       /* drag: a divider is dragged           */
#define WL_D_ROW        3       /* drag: a row pressed, the drag armed  */
#define WL_D_OUT        4       /* drag: a row dragged out              */
#define WL_DROPW        1       /* pixels across a drop target's frame  */
/* How the box's selection follows the focus row (choose; WLIST.H,
   Several rows). */
#define WL_R_ONE        0       /* the focus row alone                  */
#define WL_R_RANGE      1       /* anchor to focus row alone            */
#define WL_R_ADD        2       /* anchor to focus row added            */
#define WL_R_FLIP       3       /* the focus row toggled                */
#define WL_R_KEEP       4       /* unchanged                            */
/* Whether every row of the list is chosen (all). */
#define WL_A_ON         1       /* they are                             */
#define WL_A_ENDS       2       /* until the move under way sets the
                                   box's selection                      */
/* The survey that collects the keys of every row (walk). */
#define WL_W_KEYS       1       /* it runs                              */
#define WL_W_OVER       2       /* it was stopped at a key refused      */

const char wl_k_class[] = "WLIST";

struct wl {
    pg   p;
    psv  sv;                    /* the list's survey                    */
    ks_set keys;                /* the keys of every row, while all     */
    wcol_lay lay;               /* the columns of order in the font     */
    const wcol_tab *cols;       /* a table per order, or NULL           */
    HWND w;                     /* the list window                      */
    HWND box;                   /* its list box                         */
    HFONT font;                 /* ANSI_VAR_FONT                        */
    FARPROC thunk;              /* the box procedure's thunk, or NULL   */
    WNDPROC old;                /* the list box's procedure             */
    wl_note_fn note;
    void *arg;                  /* passed to note                       */
    u32  total;                 /* the list's rows T, as of the last
                                   move                                 */
    int  sbpos;                 /* the gesture's last thumb position    */
    int  hx;                    /* pixels the rows are scrolled left    */
    int  grab;                  /* a divider's distance from the pointer
                                   as it was pressed                    */
    int  guide;                 /* the guide's x in the client area     */
    int  avew;                  /* the font's tmAveCharWidth, as of the
                                   press                                */
    int  target;                /* the box's drop target row, or
                                   WL_NOROW                             */
    int  focus;                 /* the box row of the focus row, or
                                   WL_NOROW                             */
    POINT from;                 /* a row's press, in the box            */
    POINT at;                   /* its drag's last point, on the screen */
    u8   level;                 /* the user's access level              */
    u8   flags;                 /* wl_open's flags                      */
    u8   order;                 /* the order the header shows           */
    u8   busy;                  /* a move is under way                  */
    u8   gone;                  /* destroyed while busy, to be freed    */
    u8   quit;                  /* a wait found the program to end      */
    u8   halt;                  /* the move under way was stopped       */
    u8   letters;               /* characters typed seek rows           */
    u8   pending;               /* a count is pending                   */
    u8   exact;                 /* total is exact                       */
    u8   sbcode;                /* the gesture's code, or WL_NOSB       */
    u8   sbn;                   /* its line or page codes, counted      */
    u8   sbdrop;                /* it is dropped                        */
    u8   sbpost;                /* its move is posted and not taken     */
    u8   drag;                  /* WL_D_, or 0                          */
    u8   col;                   /* the column held or sized             */
    u8   on;                    /* the pointer is on the heading held   */
    u8   anchor;                /* the box row Shift extends from       */
    u8   held;                  /* the box row + 1 of a press held on a
                                   selection, 0 for none                */
    u8   how;                   /* WL_R_ rule of the next quiet move    */
    u8   adding;                /* add mode (WLIST.H, Several rows)     */
    u8   shown;                 /* the add mode last noted (kept)       */
    u8   all;                   /* WL_A_, or 0                          */
    u8   walk;                  /* WL_W_, or 0                          */
    rqe  *e;                    /* the last failure of the move under
                                   way, on run's stack; else NULL       */
};

/* ---------------------------------------------------------------------
 * The pager's functions.
 * --------------------------------------------------------------------- */

/* An Esc comes as WM_SYSKEYDOWN when no window has the focus, as while
   the owner holds its window disabled; with ALT held it is ALT+ESC, the
   switch to the next program, and stops nothing. */
int wl_wait(void)
{
    MSG m;
    int stop;

    stop = 0;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) {
            PostQuitMessage((int)m.wParam);
            return WL_QUIT;
        }
        if (m.wParam == VK_ESCAPE && (m.message == WM_KEYDOWN ||
                                      (m.message == WM_SYSKEYDOWN &&
                                       !(m.lParam & WL_ALTKEY))))
            stop = 1;
        else if (!(m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) &&
                 !(m.message >= WM_MOUSEFIRST &&
                   m.message <= WM_MOUSELAST) &&
                 !(m.message >= WM_NCMOUSEMOVE &&
                   m.message <= WM_NCMBUTTONDBLCLK))
            DispatchMessage(&m);
    }
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    return stop;
}

/* pg_wait_fn: wl_wait, which also stops once the list was destroyed,
   and keeps a WM_QUIT found in quit: no seek follows a move or survey
   it stopped, as nothing should once the program is to end.  The survey
   that collects keys (walk) is stopped too once its set has refused one,
   as no more of the list is wanted then, and else notes the owner the n
   rows it has counted. */
static int wait(void *arg, int walk, u32 n)
{
    wl *v;
    int stop;

    (void)walk;
    v = (wl *)arg;
    stop = wl_wait();
    if (stop == WL_QUIT)
        v->quit = 1;
    if (stop != 0 || v->gone)
        return 1;
    if (v->walk) {
        if (v->keys.over) {
            v->walk = WL_W_OVER;
            return 1;
        }
        v->note(v->arg, v, WL_COUNT, (UINT)n);
    }
    return 0;
}

/* pg_fail_fn: the failure classified and kept for the end of the move;
   1 when the list stays. */
static int fail(void *arg, const rq_res *res)
{
    wl *v;

    v = (wl *)arg;
    rqe_class(v->e, res, v->level);
    return v->e->act == RQE_STAY;
}

/* ---------------------------------------------------------------------
 * The font and the columns.
 * --------------------------------------------------------------------- */

/* The metrics of the list's font, read through a display context of
   w. */
static void metrics(HWND w, TEXTMETRIC *tm)
{
    HDC dc;

    dc = GetDC(w);
    SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    GetTextMetrics(dc, tm);
    ReleaseDC(w, dc);
}

/* wl_width for t in the font of tm. */
static int width(const TEXTMETRIC *tm, const wcol_tab *t)
{
    wcol_lay lay;

    wcol_layout(&lay, t, tm);
    return lay.x[t != NULL ? t->n : 1] +
           2 * GetSystemMetrics(SM_CXBORDER) + GetSystemMetrics(SM_CXVSCROLL);
}

/* wl_height for n rows in the font of tm. */
static int height(const TEXTMETRIC *tm, u8 n)
{
    return tm->tmHeight + n * (tm->tmHeight + WL_ROWPAD) +
           2 * GetSystemMetrics(SM_CYBORDER);
}

/* The table of the order the header shows, NULL for rows drawn whole. */
static const wcol_tab *table(wl *v)
{
    return v->cols != NULL ? &v->cols[v->order] : NULL;
}

/* The header of order into text (LV_ROWTEXT + 1 bytes): its table's
   words laid out in its columns, or the source's message. */
static void head(wl *v, u8 order, char *text)
{
    char words[LV_ROWTEXT + 1];
    const wcol_tab *t;

    t = v->cols != NULL ? &v->cols[order] : NULL;
    if (t != NULL && t->head != 0 &&
        LoadString((HINSTANCE)GetWindowWord(v->w, GWW_HINSTANCE), t->head,
                   words, (int)sizeof words) > 0)
        wcol_head(t, words, text);
    else
        txt_copy(text, msg(v->p.src->header[order]), LV_ROWTEXT + 1);
}

/* The pixels of a row in the columns of the order shown; 0 for rows
   drawn whole, which are never scrolled. */
static int row_end(wl *v)
{
    return v->cols != NULL ? v->lay.x[table(v)->n] : 0;
}

/* The pixels of the row that the box does not show, 0 when it fits. */
static int hmax(wl *v)
{
    RECT r;

    GetClientRect(v->box, &r);
    return row_end(v) > r.right ? row_end(v) - r.right : 0;
}

/* Keep hx within hmax and set the horizontal scroll bar for it. */
static void hbar(wl *v)
{
    int m;

    m = hmax(v);
    if (v->hx > m)
        v->hx = m;
    SetScrollRange(v->w, SB_HORZ, 0, m, FALSE);
    SetScrollPos(v->w, SB_HORZ, v->hx, TRUE);
}

/* Lay the columns of the order shown out again, with the widths kept,
   and draw the header and the rows in them. */
static void relay(wl *v)
{
    TEXTMETRIC tm;

    metrics(v->w, &tm);
    wcol_layout(&v->lay, table(v), &tm);
    InvalidateRect(v->w, NULL, TRUE);
    InvalidateRect(v->box, NULL, TRUE);
    hbar(v);
}

/* ---------------------------------------------------------------------
 * The length and the scroll bar.
 * --------------------------------------------------------------------- */

/* The rows above the pager's first row held, 0 while not known. */
static u32 rows_above(pg *p)
{
    return p->above != PG_NOABOVE ? p->above : 0;
}

/* The window's top row t. */
static u32 top(pg *p)
{
    return rows_above(p) + p->first;
}

/* 1 when the pager shows a lead row, which the count leaves out. */
static int lead(pg *p)
{
    return p->leadtext != NULL && p->ps > 1;
}

/* The bar's highest top row M, as of the last move, and its unit *u. */
static u32 range(wl *v, u32 *u)
{
    u32 m;

    m = v->total > v->p.ps ? v->total - v->p.ps : 0;
    *u = m <= WL_MAXPOS ? 1 : m / WL_MAXPOS + 1;
    return m;
}

/* Make s count nothing, as a failed survey leaves it. */
static void drop(psv *s)
{
    s->rows = 0;
    s->pages = 0;
    s->all = 0;
}

/* Make s count the whole list, n rows, as a survey to its end would. */
static void count_all(psv *s, u32 n)
{
    s->rows = n;
    s->all = 1;
}

/* Keep the list's rows T and whether they are exact, and set the scroll
   bar's range and position for them: a list whose end is held, above
   known, is counted, and one the pager no longer places loses its
   count, pending again with WL_EXACT. */
static void bar(wl *v)
{
    pg *p;
    u32 m;
    u32 u;
    u32 t;
    int known;
    int end;

    p = &v->p;
    known = p->above != PG_NOABOVE;
    end = pg_at_end(p);
    v->total = rows_above(p) + pg_rows(p);
    if (!known) {
        drop(&v->sv);
        v->pending = (u8)((v->flags & WL_EXACT) != 0);
    } else if (end) {
        count_all(&v->sv, v->total);
    }
    v->exact = (u8)(known && (end || v->sv.all));
    if (!end) {
        if (!v->exact)
            v->total += p->ps;
        if (known && v->total < v->sv.rows)
            v->total = v->sv.rows;
    }
    m = range(v, &u);
    t = top(p);
    SetScrollRange(v->w, SB_VERT, 0, (int)(m / u), FALSE);
    SetScrollPos(v->w, SB_VERT, (int)((t < m ? t : m) / u), TRUE);
}

/* ---------------------------------------------------------------------
 * The headings.
 * --------------------------------------------------------------------- */

/* The order the heading of column k names, or -1 when it names none of
   the source's (a flat heading). */
static int names(wl *v, int k)
{
    int o;

    o = v->cols != NULL ? WCOL_ORDOF(table(v)->col[k].flags) : -1;
    return o < v->p.src->norders ? o : -1;
}

/* The column of the header line at the client point (x, y), or -1; with
   div not NULL, *div 1 when it is a divider that can be dragged
   (WL_SIZES, a table with keep, a store kept): wcol_hit, else 0. */
static int hit(wl *v, int x, int y, int *div)
{
    if (div != NULL) {
        *div = 0;
        if (!(v->flags & WL_SIZES) || v->cols == NULL ||
            table(v)->keep == 0 || !wcol_kept())
            div = NULL;
    }
    if (y < 0 || y >= v->lay.lineh)
        return -1;
    return wcol_hit(table(v), &v->lay,
                    x - GetSystemMetrics(SM_CXBORDER) + v->hx, div);
}

/* Column k's slot's left edge in the client area. */
static int slot(wl *v, int k)
{
    return GetSystemMetrics(SM_CXBORDER) + v->lay.x[k] - v->hx;
}

/* Draw the header line again, for a heading's state. */
static void heads_dirty(wl *v)
{
    RECT r;

    GetClientRect(v->w, &r);
    r.bottom = v->lay.lineh;
    InvalidateRect(v->w, &r, FALSE);
}

/* Invert the guide at x = guide on the screen: a band WL_GUIDEW pixels
   wide down the client area, over the box. */
static void invert(wl *v)
{
    POINT o;
    RECT r;
    HDC dc;

    GetClientRect(v->w, &r);
    o.x = v->guide;
    o.y = 0;
    ClientToScreen(v->w, &o);
    OffsetRect(&r, o.x, o.y);
    r.right = r.left + WL_GUIDEW;
    dc = GetDC(NULL);
    InvertRect(dc, &r);
    ReleaseDC(NULL, dc);
}

/* End a heading held, a divider dragged or a row's drag with nothing
   done: the guide erased, the heading drawn raised, the capture of the
   window, of a row dragged out or of a row pressed in a list of several
   rows (which took it, row_drag) released and, with say, the owner noted
   WL_DR_END for a row dragged out.  A row only pressed in another list
   keeps the box's own press, which its release ends as a click. */
static void cancel(wl *v, int say)
{
    int kind;

    kind = v->drag;
    if (kind == WL_D_SIZE)
        invert(v);
    else if (kind == WL_D_HEAD)
        heads_dirty(v);
    v->drag = 0;
    if (GetCapture() == v->w ||
        ((kind == WL_D_OUT || (kind == WL_D_ROW && (v->flags & WL_MULTI))) &&
         GetCapture() == v->box))
        ReleaseCapture();
    if (kind == WL_D_OUT && say)
        v->note(v->arg, v, WL_DRAG, WL_DR_END);
}

/* A press of the left button at (x, y): in the header line, the box
   focused, a divider of a list that sizes taken up with its guide, or a
   heading that names an order held down. */
static void press(wl *v, int x, int y)
{
    TEXTMETRIC tm;
    int div;
    int k;

    if (y >= v->lay.lineh || v->busy || GetCapture() != NULL)
        return;
    SetFocus(v->box);
    k = hit(v, x, y, &div);
    if (k < 0)
        return;
    v->col = (u8)k;
    if (div) {
        metrics(v->w, &tm);
        v->avew = tm.tmAveCharWidth;
        v->guide = slot(v, k + 1);
        v->grab = x - v->guide;
        v->drag = WL_D_SIZE;
        SetCapture(v->w);
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        invert(v);
    } else if (names(v, k) >= 0) {
        v->on = 1;
        v->drag = WL_D_HEAD;
        SetCapture(v->w);
        heads_dirty(v);
    }
}

/* The pointer at (x, y) while held: a heading pressed while the pointer
   is on it, else raised; a guide moved to it, its column held from
   WCOL_MINW to WCOL_MAXW average characters wide.  A capture lost ends
   the drag (cancel). */
static void track(wl *v, int x, int y)
{
    int lo;
    int on;

    if (v->drag != WL_D_HEAD && v->drag != WL_D_SIZE)
        return;
    if (GetCapture() != v->w) {
        cancel(v, 1);
        return;
    }
    if (v->drag == WL_D_HEAD) {
        on = hit(v, x, y, NULL) == v->col;
        if (on != v->on) {
            v->on = (u8)on;
            heads_dirty(v);
        }
        return;
    }
    lo = slot(v, v->col);
    x -= v->grab;
    x = max(lo + WCOL_MINW * v->avew, min(x, lo + WCOL_MAXW * v->avew));
    if (x != v->guide) {
        invert(v);
        v->guide = x;
        invert(v);
    }
}

/* The left button released at (x, y): a heading released on itself
   posts the switch to its order (WM_WLORDER) unless that is the order in
   use; a divider released elsewhere than where it was pressed gives its
   column the guide's width, which track holds to WCOL_MINW..WCOL_MAXW,
   in whole average characters, in the widths kept, the columns laid out
   again and the owner noted (WL_COLS), when it differs from the width it
   has. */
static void release_at(wl *v, int x, int y)
{
    int kind;
    int k;
    int n;
    u8 keep;

    track(v, x, y);
    kind = v->drag;
    if (kind != WL_D_HEAD && kind != WL_D_SIZE)
        return;
    k = v->col;
    cancel(v, 0);
    if (kind == WL_D_HEAD) {
        if (v->on && names(v, k) != v->order)
            PostMessage(v->w, WM_WLORDER, (WPARAM)names(v, k), 0L);
        return;
    }
    if (x - v->grab == slot(v, k + 1))
        return;
    n = (v->guide - slot(v, k) + v->avew / 2) / v->avew;
    keep = table(v)->keep;
    if (n != (v->lay.x[k + 1] - v->lay.x[k] + v->avew / 2) / v->avew) {
        wcol_set_width(keep, k, (u8)n);
        relay(v);
        v->note(v->arg, v, WL_COLS, keep);
    }
}

/* 1 when the pointer, at the screen's point, is on a divider that can
   be dragged. */
static int on_divider(wl *v)
{
    POINT pt;
    int div;

    GetCursorPos(&pt);
    ScreenToClient(v->w, &pt);
    return hit(v, pt.x, pt.y, &div) >= 0 && div;
}

/* ---------------------------------------------------------------------
 * The box.
 * --------------------------------------------------------------------- */

/* Draw box row r again, unless it is WL_NOROW. */
static void row_dirty(wl *v, int r)
{
    RECT b;

    if (r != WL_NOROW &&
        SendMessage(v->box, LB_GETITEMRECT, (WPARAM)r,
                    (LPARAM)(RECT FAR *)&b) !=
            LB_ERR)
        InvalidateRect(v->box, &b, FALSE);
}

/* 1 when box row r is selected. */
static int is_sel(wl *v, int r)
{
    return SendMessage(v->box, LB_GETSEL, r, 0L) > 0;
}

/* The rule the box's selection is set by after a move of rule: kept as
   it is while every row of the list is chosen, and the focus row alone
   after the move that ends that (WL_A_ENDS), the keys freed. */
static u8 all_rule(wl *v, u8 rule)
{
    if (v->all == WL_A_ON)
        return WL_R_KEEP;
    if (v->all == WL_A_ENDS) {
        v->all = 0;
        ks_free(&v->keys);
        return WL_R_ONE;
    }
    return rule;
}

/* Make the pager's selected row the focus row, none without rows, and
   set the box's selection for it by rule (WL_R_): without WL_MULTI,
   that row (LB_SETCURSEL); else as WLIST.H's Several rows says, the
   focus row kept in view, its focus rectangle moved. */
static void choose(wl *v, u8 rule)
{
    RECT b;
    int was;
    int f;
    int top;
    int n;

    was = v->focus;
    f = pg_rows(&v->p) > 0 ? v->p.sel - v->p.first : WL_NOROW;
    v->focus = f;
    if (!(v->flags & WL_MULTI)) {
        SendMessage(v->box, LB_SETCURSEL, (WPARAM)f, 0L);
        return;
    }
    if (f == WL_NOROW)
        return;
    if (rule == WL_R_ONE || rule == WL_R_FLIP)
        v->anchor = (u8)f;
    if (rule == WL_R_FLIP) {
        SendMessage(v->box, LB_SETSEL, !is_sel(v, f), (LPARAM)f);
    } else if (rule == WL_R_ONE) {
        SendMessage(v->box, LB_SETSEL, FALSE, -1L);
        SendMessage(v->box, LB_SETSEL, TRUE, (LPARAM)f);
    } else if (rule != WL_R_KEEP) {
        SendMessage(v->box, WM_SETREDRAW, FALSE, 0L);
        if (rule == WL_R_RANGE)
            SendMessage(v->box, LB_SETSEL, FALSE, -1L);
        SendMessage(v->box, LB_SELITEMRANGE, TRUE,
                    MAKELPARAM(min(v->anchor, f), max(v->anchor, f)));
        SendMessage(v->box, WM_SETREDRAW, TRUE, 0L);
        InvalidateRect(v->box, NULL, FALSE);
    }
    GetClientRect(v->box, &b);
    n = max(1, b.bottom / (v->lay.lineh + WL_ROWPAD));
    top = (int)SendMessage(v->box, LB_GETTOPINDEX, 0, 0L);
    if (f < top || f >= top + n)
        SendMessage(v->box, LB_SETTOPINDEX, f < top ? f : f - n + 1, 0L);
    row_dirty(v, was);
    row_dirty(v, f);
}

/* The row of the box under the screen's point pt, or WL_NOROW when pt
   lies outside the box, below its last row or on the empty text. */
static int row_at(wl *v, POINT pt)
{
    RECT r;
    int i;

    ScreenToClient(v->box, &pt);
    GetClientRect(v->box, &r);
    if (!PtInRect(&r, pt))
        return WL_NOROW;
    i = (int)SendMessage(v->box, LB_GETTOPINDEX, 0, 0L) +
        pt.y / (v->lay.lineh + WL_ROWPAD);
    return i < (int)SendMessage(v->box, LB_GETCOUNT, 0, 0L) &&
           SendMessage(v->box, LB_GETITEMDATA, (WPARAM)i, 0L) != WL_WHOLE ?
           i : WL_NOROW;
}

/* Row i of the box's key into key (LV_KEYSZ bytes).  The pager reads
   the selected row's key alone, so its selection is set to the row and
   put back: the pager's state alone, with no message sent, no row drawn
   and nothing noted. */
static void row_key(wl *v, int i, char *key)
{
    u16 sel;

    sel = v->p.sel;
    v->p.sel = (u16)(v->p.first + i);
    pg_selected(&v->p, key);
    v->p.sel = sel;
}

/* Set row i's WL_DDROP in its item data when on, else clear it, and
   draw the row again; nothing for WL_NOROW. */
static void mark(wl *v, int i, int on)
{
    LRESULT d;
    RECT r;

    if (i == WL_NOROW)
        return;
    d = SendMessage(v->box, LB_GETITEMDATA, (WPARAM)i, 0L);
    SendMessage(v->box, LB_SETITEMDATA, (WPARAM)i,
                on ? d | WL_DDROP : d & ~(LRESULT)WL_DDROP);
    SendMessage(v->box, LB_GETITEMRECT, (WPARAM)i, (LPARAM)(RECT FAR *)&r);
    InvalidateRect(v->box, &r, FALSE);
}

/* Add text (LV_ROWTEXT + 1 bytes) as a row of the box, cut before its
   trailing spaces, its item data data. */
static void add_row(wl *v, char *text, int data)
{
    LRESULT i;
    int n;

    n = (int)strlen(text);
    while (n > 0 && text[n - 1] == ' ')
        n--;
    text[n] = '\0';
    i = SendMessage(v->box, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)text);
    if (data != 0)
        SendMessage(v->box, LB_SETITEMDATA, (WPARAM)i, (LPARAM)data);
}

/* Select the rows of a box just filled whose keys the set holds, and no
   other: a row added to the list since the survey is none of the rows
   chosen.  The set is in the list's order, so each row's key is sought
   from the one after the row's before it, the first from the window's
   top row. */
static void set_rows(wl *v)
{
    char key[LV_KEYSZ];
    u16 from;
    u16 k;
    int n;
    int i;

    from = (u16)top(&v->p);
    n = (int)SendMessage(v->box, LB_GETCOUNT, 0, 0L);
    for (i = 0; i < n; i++) {
        row_key(v, i, key);
        k = ks_find(&v->keys, key, from);
        if (k < v->keys.n) {
            SendMessage(v->box, LB_SETSEL, TRUE, (LPARAM)i);
            from = (u16)(k + 1);
        }
    }
}

/* Fill the box with the rows of the pager's window, or the empty text,
   the header with the order in use and its columns, and set the bar;
   the focus row alone is selected, or, while all are chosen, the rows of
   the set (set_rows). */
static void fill(wl *v)
{
    char text[LV_ROWTEXT + 1];
    pg *p;
    u16 i;
    u16 end;

    p = &v->p;
    if (v->order != p->order) {
        v->order = p->order;
        head(v, v->order, text);
        SetWindowText(v->w, text);
        relay(v);
    }
    SendMessage(v->box, WM_SETREDRAW, FALSE, 0L);
    SendMessage(v->box, LB_RESETCONTENT, 0, 0L);
    v->target = WL_NOROW;
    end = pg_rows(p);
    if (end > p->first + p->ps)
        end = (u16)(p->first + p->ps);
    for (i = p->first; i < end; i++)
        add_row(v, text, pg_text(p, i, text) ? WL_DMARK : 0);
    if (end == 0 && pg_at_end(p)) {
        txt_copy(text, msg(p->src->empty), (u16)sizeof text);
        add_row(v, text, WL_WHOLE);
    }
    v->held = 0;
    choose(v, all_rule(v, WL_R_ONE));
    if (v->all)
        set_rows(v);
    SendMessage(v->box, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(v->box, NULL, TRUE);
    bar(v);
}

/* Draw the row of d whole (wcol_item), the focus rectangle on it when it
   is the focus row and the box has the focus, whatever the box's own
   caret is (WLIST.H, Several rows), so a focus change redraws the row
   rather than toggling a rectangle: the empty text whole, a marked row
   in red, a drop target framed, the frame inverted, so that it shows on
   the selected row too. */
static void draw(wl *v, const DRAWITEMSTRUCT FAR *dp)
{
    DRAWITEMSTRUCT d;
    TEXTMETRIC tm;
    wcol_lay whole;
    RECT r;

    d = *dp;
    d.itemAction = ODA_DRAWENTIRE;
    d.itemState &= ~ODS_FOCUS;
    if ((int)d.itemID == v->focus && GetFocus() == v->box)
        d.itemState |= ODS_FOCUS;
    SelectObject(d.hDC, v->font);
    if (d.itemData != WL_WHOLE) {
        wcol_item(&d, table(v), &v->lay, (d.itemData & WL_DMARK) ? WL_ALERT :
                  GetSysColor(COLOR_WINDOWTEXT), v->hx);
        if (d.itemData & WL_DDROP) {
            r = d.rcItem;
            InvertRect(d.hDC, &r);
            InflateRect(&r, -WL_DROPW, -WL_DROPW);
            InvertRect(d.hDC, &r);
        }
        return;
    }
    GetTextMetrics(d.hDC, &tm);
    wcol_layout(&whole, NULL, &tm);
    wcol_item(&d, NULL, &whole, GetSysColor(COLOR_WINDOWTEXT), 0);
}

/* The screen's point of the mouse message lp of the box. */
static POINT screen_at(wl *v, LPARAM lp)
{
    POINT pt;

    pt.x = (int)LOWORD(lp);
    pt.y = (int)HIWORD(lp);
    ClientToScreen(v->box, &pt);
    return pt;
}

/* A press of the left button in the box of a list of several rows (wm
   WM_LBUTTONDOWN or WM_LBUTTONDBLCLK, mk its wParam, lp its point): the
   box focused, then the row there chosen by the rule of the modifiers, a
   plain press on a row of a selection of several held until its release
   (WLIST.H, Several rows), posted; a double click's second press is
   ENTER too. */
static void press_row(wl *v, UINT wm, WPARAM mk, LPARAM lp)
{
    int r;
    u8 rule;

    v->held = 0;
    SetFocus(v->box);
    r = row_at(v, screen_at(v, lp));
    if (r == WL_NOROW)
        return;
    rule = (u8)(mk & MK_SHIFT ? (mk & MK_CONTROL || v->adding ? WL_R_ADD :
                                 WL_R_RANGE) :
                mk & MK_CONTROL ? WL_R_FLIP : WL_R_ONE);
    if (rule == WL_R_ONE && is_sel(v, r) && wl_nchosen(v) > 1) {
        rule = WL_R_KEEP;
        v->held = (u8)(r + 1);
    }
    PostMessage(v->w, WM_WLPICK, (WPARAM)r, rule);
    if (wm == WM_LBUTTONDBLCLK)
        PostMessage(v->w, WM_WLKEY, VK_RETURN, 0L);
}

/* The release of the left button in the box of a list of several rows,
   lp its point: the capture of a press armed for a drag given back, and
   a press held there completed as a plain click, that row alone.  A
   row's drag out forgets the press first (row_drag). */
static void release_row(wl *v, LPARAM lp)
{
    if (GetCapture() == v->box)
        ReleaseCapture();
    if (v->held != 0 && row_at(v, screen_at(v, lp)) == v->held - 1)
        PostMessage(v->w, WM_WLPICK, v->held - 1, WL_R_ONE);
    v->held = 0;
}

/* 1 when message wm of the box, lp its point for the mouse, is a row's
   drag's (WLIST.H, Rows dragged), which takes it: a press on a row arms
   the drag, after the box has taken it, or in a list of several rows the
   list (press_row), the box then holding the capture; a move within
   WL_DRAGMIN pixels of it goes nowhere, and the first beyond ends the
   box's press where it was, so that it is a click, or forgets the list's
   held press, and drags the row out, the box holding the capture; then
   each move is noted and the release posts WM_WLDROP.  A release before
   is the click: the box's, or the list's (release_row).  A capture lost
   disarms a press and ends a drag first (cancel); a WM_CANCELMODE ends
   either and goes on to the box. */
static int row_drag(wl *v, UINT wm, WPARAM wp, LPARAM lp)
{
    if (v->drag >= WL_D_ROW && GetCapture() != v->box)
        cancel(v, 1);
    switch (wm) {
    case WM_LBUTTONDOWN:
        if (v->drag != 0)
            return v->drag == WL_D_OUT;
        if (v->busy || GetCapture() != NULL ||
            row_at(v, screen_at(v, lp)) == WL_NOROW)
            return 0;
        if (v->flags & WL_MULTI) {
            press_row(v, wm, wp, lp);
            SetCapture(v->box);
        } else {
            CallWindowProc(v->old, v->box, wm, wp, lp);
        }
        v->from.x = (int)LOWORD(lp);
        v->from.y = (int)HIWORD(lp);
        v->drag = WL_D_ROW;
        return 1;
    case WM_MOUSEMOVE:
        if (v->drag == WL_D_ROW) {
            if (abs((int)LOWORD(lp) - v->from.x) <= WL_DRAGMIN &&
                abs((int)HIWORD(lp) - v->from.y) <= WL_DRAGMIN)
                return 1;
            if (!(v->flags & WL_MULTI))
                CallWindowProc(v->old, v->box, WM_LBUTTONUP, 0,
                               MAKELPARAM(v->from.x, v->from.y));
            v->held = 0;
            SetCapture(v->box);
            v->drag = WL_D_OUT;
            v->at = screen_at(v, lp);
            v->note(v->arg, v, WL_DRAG, WL_DR_START);
        }
        if (v->drag != WL_D_OUT)
            return 0;
        v->at = screen_at(v, lp);
        v->note(v->arg, v, WL_DRAG, WL_DR_OVER);
        return 1;
    case WM_LBUTTONUP:
        if (v->drag == WL_D_ROW)
            v->drag = 0;
        if (v->drag != WL_D_OUT)
            return 0;
        v->at = screen_at(v, lp);
        v->drag = 0;
        ReleaseCapture();
        PostMessage(v->w, WM_WLDROP, 0, 0L);
        return 1;
    case WM_CANCELMODE:
        cancel(v, 1);
        return 0;
    default:
        return 0;
    }
}

/* The box's procedure: a row's drag's messages its (row_drag), in a list
   that drags its rows; its keys but SHIFT and CTRL alone go to the
   window, posted, its characters too while the list takes them and no
   drag is under way (else nowhere), and it wants every key of a dialog;
   while a heading is held, a divider dragged or a row pressed or
   dragged, Esc ends it (cancel) and other keys go nowhere.  In a list of
   several rows the left button's presses and releases are the list's
   (press_row, release_row), never the box's; a focus gained or lost
   redraws the focus row (draw). */
LRESULT WCALLBACK wl_boxproc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    wl *v;

    v = (wl *)GetWindowWord(GetParent(w), 0);
    if ((v->flags & WL_DRAGS) && row_drag(v, wm, wp, lp))
        return 0;
    switch (wm) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!(v->flags & WL_MULTI))
            break;
        press_row(v, wm, wp, lp);
        return 0;
    case WM_LBUTTONUP:
        if (!(v->flags & WL_MULTI))
            break;
        release_row(v, lp);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        r = CallWindowProc(v->old, w, wm, wp, lp);
        row_dirty(v, v->focus);
        return r;
    case WM_KEYDOWN:
        if (v->drag != 0) {
            if (wp == VK_ESCAPE)
                cancel(v, 1);
        } else if (wp != VK_SHIFT && wp != VK_CONTROL)
            PostMessage(v->w, WM_WLKEY, wp, 0L);
        return 0;
    case WM_CHAR:
        if (v->letters && v->drag == 0)
            PostMessage(v->w, WM_WLCHAR, wp, 0L);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    default:
        break;
    }
    return CallWindowProc(v->old, w, wm, wp, lp);
}

/* ---------------------------------------------------------------------
 * Moves.
 * --------------------------------------------------------------------- */

/* 1 when n moves m (PG_UP or PG_DOWN) only move the selection within
   the window, and the pager can fetch nothing for them: the window is
   full of rows, or the list ends with the rows held.  Else even a
   PG_DOWN inside the window goes on filling a page that a stop or a
   failure left short (PAGER.H). */
static int quiet(pg *p, int m, u16 n)
{
    if (p->first + p->ps > pg_rows(p) && !pg_at_end(p))
        return 0;
    if (m == PG_UP)
        return p->sel >= p->first + n;
    return m == PG_DOWN && p->sel + n < p->first + p->ps &&
           p->sel + n < pg_rows(p);
}

/* Free the list. */
static void release(wl *v)
{
    ks_free(&v->keys);
    psv_free(&v->sv);
    pg_close(&v->p);
    free(v);
}

/* 1 when the row's text, leading blanks skipped, starts with the
   letter *arg (wl_letters' seek), in either case. */
static int starts(void *arg, const char *key, const char *text)
{
    char c;

    (void)key;
    while (*text == ' ')
        text++;
    c = *text;
    return (u8)TXT_UPPER(c) == *(const u8 *)arg;
}

/* 1 when the row of key is the row of key arg (wl_find's seek). */
static int is_key(void *arg, const char *key, const char *text)
{
    (void)text;
    return strcmp(key, (const char *)arg) == 0;
}

/* 1 when match (arg marg) finds the selected row. */
static int sel_is(pg *p, lv_match_fn match, void *marg)
{
    char key[LV_KEYSZ];
    char text[LV_ROWTEXT + 1];

    if (!pg_selected(p, key))
        return 0;
    pg_text(p, p->sel, text);
    return match(marg, key, text);
}

/* After move m, whose code is rc, the survey where it counts the list
   (WLIST.H, Length and scroll bar): the survey's code, RQ_OK when none
   ran.  A move succeeded only when it was not stopped either (halt): an
   Esc that stopped it leaves the count pending.  A list the pager no
   longer places (above PG_NOABOVE) is not surveyed, as the next fill
   would drop the count again.  Every PG_ORDER drops the count first,
   surveyed or not, as its page starts begin the pages of the order
   before, which a jump must not load. */
static int survey(wl *v, int m, int rc)
{
    pg *p;

    p = &v->p;
    if (m == PG_ORDER)
        drop(&v->sv);
    if (!(v->flags & WL_EXACT) || rc != RQ_OK || v->halt || v->gone ||
        p->above == PG_NOABOVE || !(m == PG_ORDER || v->pending))
        return RQ_OK;
    rc = psv_survey(&v->sv, p, NULL);
    v->pending = (u8)(rc != RQ_OK);
    return rc;
}

/* run's WL_ALL: survey the list for the key of every row (psv_survey
   with the list's set), as of now.  RQ_OK with every row chosen (all)
   and the add mode ended, when the survey came to the list's end with a
   key at least; else what was chosen stays so, a set of an earlier
   choice of all, kept aside meanwhile, included: WL_EOVER when the set
   or the survey holds fewer rows than the list has, the walk stopped at
   the first wait after a key was refused; RQ_OK after a stop (halt) or
   in an empty list, or the failed request's code.  A survey that kept
   no page starts (stopped, failed, without memory) leaves the list
   counted as it was, without the page starts it had. */
static int collect(wl *v)
{
    ks_set was;
    u32 rows;
    u8 counted;
    u8 all;
    int rc;

    rows = v->sv.rows;
    counted = v->sv.all;
    was = v->keys;
    all = v->all;
    ks_init(&v->keys);
    v->all = 0;
    v->walk = WL_W_KEYS;
    rc = psv_survey(&v->sv, &v->p, &v->keys);
    v->halt = (u8)(v->p.stopped && v->walk == WL_W_KEYS);
    if (rc == RQ_OK && (v->walk == WL_W_OVER || v->keys.over))
        rc = WL_EOVER;
    v->walk = 0;
    if (v->sv.pages > 0) {
        v->pending = 0;
    } else {
        v->sv.rows = rows;
        v->sv.all = counted;
    }
    if (rc == RQ_OK && !v->halt && v->keys.n > 0) {
        ks_free(&was);
        v->all = WL_A_ON;
        v->adding = 0;
    } else {
        ks_free(&v->keys);
        v->keys = was;
        v->all = all;
    }
    return rc;
}

/* A jump's step: move m, its code in *rc, after the wait when an
   earlier step may have sent a request (*sent) and this one may; 1 when
   it moved the window or the selection, 0 when it failed, moved nothing
   or was stopped, by that wait or the move's own (halt). */
static int step(wl *v, int m, int *sent, int *rc)
{
    pg *p;
    u32 t;
    u32 s;

    p = &v->p;
    if (!quiet(p, m, 1)) {
        if (*sent && wait(v, 0, 0)) {
            v->halt = 1;
            return 0;
        }
        *sent = 1;
    }
    t = top(p);
    s = rows_above(p) + p->sel;
    *rc = pg_move(p, m);
    v->halt = p->stopped;
    return *rc == RQ_OK && !v->halt &&
           (top(p) != t || rows_above(p) + p->sel != s);
}

/* Bring row r to the window's top, as WLIST.H gives a jump: the code
   of the last move. */
static int jump(wl *v, u32 r)
{
    pg *p;
    u32 u;
    u32 t;
    int sent;
    int rc;
    int m;

    p = &v->p;
    if (r == range(v, &u) && v->exact) {
        rc = pg_move(p, PG_END);
        v->halt = p->stopped;
        return rc;
    }
    sent = 0;
    rc = RQ_OK;
    if (v->sv.pages > 0) {
        rc = psv_jump(&v->sv, p, r);
        v->halt = p->stopped;
        if (rc != RQ_OK || v->halt)
            return rc;
        sent = 1;
    }
    for (;;) {
        t = top(p);
        if (r >= t + p->ps)
            m = PG_PGDN;
        else if (r < t)
            m = PG_PGUP;
        else if (t < r)
            m = PG_DOWN;
        else if (p->sel > p->first)
            m = PG_UP;
        else
            return rc;
        if (!step(v, m, &sent, &rc))
            return rc;
    }
}

/* The selection by rule after a move that left the window where it was,
   kept while every row is chosen (all_rule), the next quiet move's rule
   ONE again, noted (WL_SEL) in a list of several rows when the number of
   rows chosen or the add mode changed or the choice of every row ended
   (of a list of one row too), the last thing the list does. */
static void kept(wl *v, u8 rule)
{
    u16 was;
    u16 n;
    u8 all;

    v->how = WL_R_ONE;
    was = wl_nchosen(v);
    all = v->all;
    choose(v, all_rule(v, rule));
    n = wl_nchosen(v);
    if ((v->flags & WL_MULTI) &&
        (n != was || v->adding != v->shown || (all && !v->all))) {
        v->shown = v->adding;
        v->note(v->arg, v, WL_SEL, n);
    }
}

/* Make move m n times, as WLIST.H gives a move, stopping at a failure
   or a stop (halt), or with WL_JUMP the jump to row n, or with WL_ALL
   the choice of every row (collect), after which the box is filled only
   while every row is chosen: else the window has not moved, and the rows
   chosen in it stay.  Then, with match, seek the row match (arg marg)
   finds from the row reached (pg_seek), which after PG_HOME is itself
   tried first.  The code of the last pg_move, of a failed survey, else
   of pg_seek.  A list destroyed meanwhile is freed at the end, after
   its WL_BUSY 0 note. */
static int run(wl *v, int m, u32 n, lv_match_fn match, void *marg)
{
    rqe e;
    pg *p;
    int rc;
    int rcs;
    u8 how;

    p = &v->p;
    rc = RQ_OK;
    how = v->how;
    v->how = WL_R_ONE;
    /* Such a move leaves the window where it is, and so the bar, whose
       range and position cannot change, and the rows chosen in it. */
    if (match == NULL && quiet(p, m, (u16)n)) {
        for (; n > 0; n--)
            pg_move(p, m);
        kept(v, how);
        return rc;
    }
    v->busy = 1;
    v->e = &e;
    e.show = RQE_NONE;
    e.act = RQE_STAY;
    v->note(v->arg, v, WL_BUSY, 1);
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    v->halt = 0;
    if (m == WL_JUMP) {
        rc = jump(v, n);
    } else if (m == WL_ALL) {
        rc = collect(v);
    } else {
        for (; n > 0 && rc == RQ_OK && !v->gone && !v->halt; n--) {
            rc = pg_move(p, m);
            v->halt = p->stopped;
        }
    }
    rcs = survey(v, m, rc);
    if (match != NULL && rc == RQ_OK && !v->gone && !v->quit &&
        !(m == PG_HOME && sel_is(p, match, marg)))
        rc = pg_seek(p, match, marg, LV_SEEKMAX);
    if (rcs != RQ_OK)
        rc = rcs;
    v->e = NULL;
    v->note(v->arg, v, WL_BUSY, 0);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    if (!v->gone) {
        if (m != WL_ALL || v->all)
            fill(v);
        else
            bar(v);
        if (e.show != RQE_NONE) {
            OemToAnsi(e.text, e.text);
            wd_say(GetParent(v->w), WD_FAILICON(e.show), e.text);
        }
    }
    v->busy = 0;
    if (v->gone)
        release(v);
    else if (e.act != RQE_STAY)
        v->note(v->arg, v, WL_LEAVE, e.act);
    return rc;
}

/* The choice of every row, if made, ends with the move to come. */
static void ends(wl *v)
{
    if (v->all)
        v->all = WL_A_ENDS;
}

/* The owner's move m, with match the seek after it (run), which ends a
   choice of every row. */
static int owned(wl *v, int m, lv_match_fn match, void *marg)
{
    ends(v);
    return run(v, m, 1, match, marg);
}

int wl_move(wl *v, int m)
{
    return owned(v, m, NULL, NULL);
}

int wl_find(wl *v, const char *key)
{
    return owned(v, PG_HOME, is_key, (void *)key);
}

int wl_select_all(wl *v)
{
    return run(v, WL_ALL, 0, NULL, NULL);
}

void wl_deselect(wl *v)
{
    ends(v);
    v->adding = 0;
    kept(v, WL_R_ONE);
}

/* Seek the next row starting with the character c typed, in the Windows
   set (wl_letters), which ends a choice of every row. */
static void letter(wl *v, UINT c)
{
    char t[2];

    t[0] = (char)c;
    t[1] = '\0';
    AnsiToOem(t, t);
    c = (u8)t[0];
    if (c > ' ') {
        t[0] = (char)TXT_UPPER(c);
        ends(v);
        run(v, PG_DOWN, 0, starts, t);
    }
}

/* Scroll the rows sideways by the horizontal bar's code, its thumb at
   pos, or by Left and Right (SB_LINEUP, SB_LINEDOWN): a cell, the box's
   width, the thumb, either end; it sends no request. */
static void sideways(wl *v, UINT code, int pos)
{
    RECT r;
    int hx;
    int m;

    GetClientRect(v->box, &r);
    hx = v->hx;
    switch (code) {
    case SB_LINEUP:
        hx -= v->lay.cell;
        break;
    case SB_LINEDOWN:
        hx += v->lay.cell;
        break;
    case SB_PAGEUP:
        hx -= r.right;
        break;
    case SB_PAGEDOWN:
        hx += r.right;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        hx = pos;
        break;
    case SB_TOP:
        hx = 0;
        break;
    case SB_BOTTOM:
        hx = row_end(v);
        break;
    default:
        return;
    }
    m = hmax(v);
    hx = hx < 0 ? 0 : hx > m ? m : hx;
    if (hx != v->hx) {
        v->hx = hx;
        SetScrollPos(v->w, SB_HORZ, hx, TRUE);
        InvalidateRect(v->w, NULL, TRUE);
        InvalidateRect(v->box, NULL, TRUE);
    }
}

/* 1 when m, PG_UP or PG_DOWN, would not move the selection, at the
   list's first or last row: a SHIFT with it keeps the rows chosen, where
   the busy move would fill the box again. */
static int at_end(pg *p, int m)
{
    if (m == PG_UP)
        return p->sel == 0 && p->above == 0;
    return m == PG_DOWN && pg_at_end(p) && p->sel + 1 >= pg_rows(p);
}

/* Carry out the key vk: a move, but PgUp and PgDn with CTRL, Up and
   Down ending a choice of every row and the others keeping it, and Up
   and Down with SHIFT while several rows are chosen when the move would
   fill the box again, which beep and keep them; the add mode's keys in
   a list of several rows; or the owner's note. */
static void key(wl *v, UINT vk)
{
    static const u8 moves[][2] = {
        { VK_UP, PG_UP }, { VK_DOWN, PG_DOWN }, { VK_PRIOR, PG_PGUP },
        { VK_NEXT, PG_PGDN }, { VK_HOME, PG_HOME }, { VK_END, PG_END }
    };
    int shift;
    int ctrl;
    int i;

    shift = GetKeyState(VK_SHIFT) < 0;
    ctrl = GetKeyState(VK_CONTROL) < 0;
    for (i = 0; i < (int)(sizeof moves / sizeof moves[0]); i++)
        if (moves[i][0] == vk &&
            !(ctrl && (vk == VK_PRIOR || vk == VK_NEXT))) {
            if (v->all) {
                if (vk == VK_UP || vk == VK_DOWN)
                    ends(v);
            } else if (shift && (v->flags & WL_MULTI)) {
                if (at_end(&v->p, moves[i][1]))
                    return;
                if ((vk == VK_UP || vk == VK_DOWN) && wl_nchosen(v) > 1 &&
                    !quiet(&v->p, moves[i][1], 1)) {
                    MessageBeep(0);
                    return;
                }
                v->how = v->adding ? WL_R_ADD : WL_R_RANGE;
            } else if (v->adding) {
                v->how = WL_R_KEEP;
            }
            run(v, moves[i][1], 1, NULL, NULL);
            return;
        }
    if (vk == VK_F8 && (v->flags & WL_MULTI) && shift && !ctrl) {
        ends(v);
        v->adding = (u8)!v->adding;
        kept(v, WL_R_KEEP);
    } else if (vk == VK_SPACE && v->adding) {
        kept(v, WL_R_FLIP);
    } else if (vk == VK_LEFT || vk == VK_RIGHT) {
        sideways(v, vk == VK_LEFT ? SB_LINEUP : SB_LINEDOWN, 0);
    } else {
        v->note(v->arg, v, WL_KEY, vk);
    }
}

/* Switch to order o of the source unless it is the order in use (a
   heading's click, WM_WLORDER): the order before it selected, then
   PG_ORDER. */
static void reorder(wl *v, UINT o)
{
    const lv_src *src;

    src = v->p.src;
    if (o < src->norders && o != v->p.order) {
        ends(v);
        pg_set_order(&v->p, src->orders[(o + src->norders - 1) %
                                        src->norders]);
        run(v, PG_ORDER, 1, NULL, NULL);
    }
}

/* Move the selection to box row i, clicked (none past the rows, such as
   LB_GETCURSEL's -1), row by row inside the window, the rows chosen by
   rule (WL_R_), which, choosing the row alone, ends the add mode, and
   unless it keeps them ends the choice of every row. */
static void pick(wl *v, UINT i, u8 rule)
{
    pg *p;
    u16 to;

    p = &v->p;
    to = i < (UINT)(pg_rows(p) - p->first) ? (u16)(p->first + i) : p->sel;
    if (rule == WL_R_ONE)
        v->adding = 0;
    if (rule != WL_R_KEEP)
        ends(v);
    v->how = rule;
    if (to > p->sel)
        run(v, PG_DOWN, (u16)(to - p->sel), NULL, NULL);
    else if (to < p->sel)
        run(v, PG_UP, (u16)(p->sel - to), NULL, NULL);
    else
        kept(v, rule);
}

/* ---------------------------------------------------------------------
 * The scroll bar.
 * --------------------------------------------------------------------- */

/* Record code, its thumb at pos, in the gesture under way, and at
   SB_ENDSCROLL post the gesture's move, unless it is dropped (WLIST.H,
   Length and scroll bar). */
static void record(wl *v, UINT code, int pos)
{
    if (code == SB_THUMBTRACK)
        return;
    if (code == SB_ENDSCROLL) {
        if (v->sbcode != WL_NOSB && !v->sbdrop && !v->busy && !v->sbpost) {
            v->sbpost = 1;
            PostMessage(v->w, WM_WLSCROLL, v->sbcode,
                        MAKELPARAM(v->sbn, v->sbpos));
        }
        v->sbcode = WL_NOSB;
        return;
    }
    if (code != v->sbcode) {
        v->sbcode = (u8)code;
        v->sbn = 0;
        v->sbdrop = 0;
    }
    if (v->busy)
        v->sbdrop = 1;
    if (v->sbn < (code == SB_LINEUP || code == SB_LINEDOWN ? v->p.ps : 1))
        v->sbn++;
    v->sbpos = pos;
}

/* Make the move of a gesture of code: n of its line codes, its thumb
   released at pos. */
static void scroll(wl *v, UINT code, u8 n, int pos)
{
    u32 m;
    u32 u;

    switch (code) {
    case SB_LINEUP:
        run(v, PG_UP, n, NULL, NULL);
        break;
    case SB_LINEDOWN:
        run(v, PG_DOWN, n, NULL, NULL);
        break;
    case SB_PAGEUP:
        run(v, PG_PGUP, 1, NULL, NULL);
        break;
    case SB_PAGEDOWN:
        run(v, PG_PGDN, 1, NULL, NULL);
        break;
    case SB_TOP:
        run(v, PG_HOME, 1, NULL, NULL);
        break;
    case SB_BOTTOM:
        run(v, PG_END, 1, NULL, NULL);
        break;
    case SB_THUMBPOSITION:
        if (pos != GetScrollPos(v->w, SB_VERT)) {
            m = range(v, &u);
            run(v, WL_JUMP, pos == (int)(m / u) ? m : (u32)pos * u, NULL,
                NULL);
        }
        break;
    default:
        break;
    }
}

/* 1 when a key, character or click the box posted is to be carried
   out: not while Windows tracks the scroll bar (the window holds the
   capture) nor while a move is busy.  One taken without the capture
   drops any gesture left. */
static int taken(wl *v)
{
    if (GetCapture() == v->w)
        return 0;
    v->sbcode = WL_NOSB;
    return !v->busy;
}

/* ---------------------------------------------------------------------
 * The window.
 * --------------------------------------------------------------------- */

/* Draw the header on the first line as headings, in the columns of the
   rows, the heading held pressed while the pointer is on it. */
static void paint(wl *v)
{
    char text[LV_ROWTEXT + 1];
    PAINTSTRUCT ps;
    RECT r;

    BeginPaint(v->w, &ps);
    GetWindowText(v->w, text, (int)sizeof text);
    GetClientRect(v->w, &r);
    r.left = GetSystemMetrics(SM_CXBORDER);
    r.bottom = v->lay.lineh;
    SelectObject(ps.hdc, v->font);
    wcol_heads(ps.hdc, &r, text, table(v), &v->lay, v->hx,
               v->p.src->norders, v->order,
               v->drag == WL_D_HEAD && v->on ? v->col : -1);
    EndPaint(v->w, &ps);
}

/* Free the list as its window is destroyed, a drag ended (cancel) and
   the box's procedure given back first, or leave that to the move under
   way. */
static void destroy(wl *v)
{
    cancel(v, 0);
    if (v->thunk != NULL) {
        SetWindowLong(v->box, GWL_WNDPROC, (LONG)v->old);
        FreeProcInstance(v->thunk);
    }
    SetWindowWord(v->w, 0, 0);
    if (v->busy)
        v->gone = 1;
    else
        release(v);
}

LRESULT WCALLBACK wl_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    wl *v;
    int h;

    v = (wl *)GetWindowWord(w, 0);
    if (v == NULL)
        return DefWindowProc(w, wm, wp, lp);
    switch (wm) {
    case WM_PAINT:
        paint(v);
        return 0;
    case WM_SIZE:
        h = (int)HIWORD(lp) - v->lay.lineh;
        MoveWindow(v->box, 0, v->lay.lineh, LOWORD(lp), h > 0 ? h : 0,
                   TRUE);
        hbar(v);
        return 0;
    case WM_SETFOCUS:
        SetFocus(v->box);
        return 0;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight =
            (UINT)(v->lay.lineh + WL_ROWPAD);
        return TRUE;
    case WM_DRAWITEM:
        draw(v, (const DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(lp) == LBN_SELCHANGE)
            PostMessage(w, WM_WLPICK,
                        (WPARAM)SendMessage(v->box, LB_GETCURSEL, 0, 0L),
                        WL_R_ONE);
        else if (HIWORD(lp) == LBN_DBLCLK)
            PostMessage(w, WM_WLKEY, VK_RETURN, 0L);
        return 0;
    case WM_WLKEY:
        if (taken(v))
            key(v, wp);
        return 0;
    case WM_WLPICK:
        if (taken(v))
            pick(v, wp, (u8)lp);
        return 0;
    case WM_WLCHAR:
        if (taken(v))
            letter(v, wp);
        return 0;
    case WM_WLORDER:
        if (taken(v))
            reorder(v, wp);
        return 0;
    case WM_WLDROP:
        v->note(v->arg, v, WL_DRAG, taken(v) ? WL_DR_DROP : WL_DR_END);
        return 0;
    case WM_LBUTTONDOWN:
        press(v, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_MOUSEMOVE:
        track(v, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        release_at(v, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_CANCELMODE:
        cancel(v, 1);
        return 0;
    case WM_SETCURSOR:
        if ((HWND)wp == w && LOWORD(lp) == HTCLIENT && on_divider(v) &&
            !SendMessage(GetParent(w), WM_SETCURSOR, wp, lp)) {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTVSCROLL)
            v->sbcode = WL_NOSB;
        break;
    case WM_VSCROLL:
        record(v, wp, (int)LOWORD(lp));
        return 0;
    case WM_HSCROLL:
        sideways(v, wp, (int)LOWORD(lp));
        return 0;
    case WM_WLSCROLL:
        v->sbpost = 0;
        if (!v->busy)
            scroll(v, wp, (u8)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_DESTROY:
        destroy(v);
        return 0;
    default:
        break;
    }
    return DefWindowProc(w, wm, wp, lp);
}

/* ---------------------------------------------------------------------
 * The interface.
 * --------------------------------------------------------------------- */

int wl_open(wl **out, HWND parent, UINT id, link *l, const lv_src *src,
            const wcol_tab *cols, u8 ps, u8 level, u8 flags,
            wl_note_fn note, void *arg)
{
    char text[LV_ROWTEXT + 1];
    HINSTANCE inst;
    WNDCLASS wc;
    TEXTMETRIC tm;
    RECT r;
    wl *v;
    int rc;

    *out = NULL;
    v = (wl *)malloc(sizeof *v);
    if (v == NULL)
        return PG_ENOMEM;
    memset(v, 0, sizeof *v);
    rc = pg_open(&v->p, l, src, ps, wait, fail, v);
    if (rc != PG_OK) {
        free(v);
        return rc;
    }
    psv_init(&v->sv);
    ks_init(&v->keys);
    inst = (HINSTANCE)GetWindowWord(parent, GWW_HINSTANCE);
    v->font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    v->cols = cols;
    v->flags = flags;
    v->note = note;
    v->arg = arg;
    v->level = level;
    v->pending = (u8)((flags & WL_EXACT) != 0);
    v->sbcode = WL_NOSB;
    v->target = WL_NOROW;
    v->focus = WL_NOROW;
    metrics(parent, &tm);
    wcol_layout(&v->lay, cols, &tm);
    if (!GetClassInfo(inst, wl_k_class, &wc)) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = wl_proc;
        wc.cbWndExtra = sizeof(wl *);
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = wl_k_class;
        RegisterClass(&wc);
    }
    v->w = CreateWindow(wl_k_class, NULL, WS_CHILD | WS_VISIBLE |
                        WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL, 0, 0,
                        width(&tm, cols), height(&tm, ps), parent, (HMENU)id,
                        inst, NULL);
    if (v->w == NULL) {
        release(v);
        return WL_EWIN;
    }
    /* No bar until the first move counts the list. */
    SetScrollRange(v->w, SB_VERT, 0, 0, FALSE);
    SetWindowWord(v->w, 0, (WORD)v);
    GetClientRect(v->w, &r);
    v->box = CreateWindow("ListBox", NULL, WS_CHILD | WS_VISIBLE |
                          WS_BORDER | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                          LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                          (flags & WL_MULTI ? LBS_EXTENDEDSEL : 0), 0,
                          v->lay.lineh, r.right, r.bottom - v->lay.lineh,
                          v->w, (HMENU)WL_BOXID, inst, NULL);
    if (v->box != NULL)
        v->thunk = MakeProcInstance((FARPROC)wl_boxproc, inst);
    if (v->thunk == NULL) {
        rc = v->box == NULL ? WL_EWIN : PG_ENOMEM;
        DestroyWindow(v->w);
        return rc;
    }
    v->old = (WNDPROC)SetWindowLong(v->box, GWL_WNDPROC, (LONG)v->thunk);
    head(v, 0, text);
    SetWindowText(v->w, text);
    hbar(v);
    *out = v;
    return PG_OK;
}

HWND wl_hwnd(wl *v)
{
    return v->w;
}

int wl_adding(wl *v)
{
    return v->adding;
}

int wl_set_order(wl *v, const char *name)
{
    return pg_set_order(&v->p, name);
}

void wl_lead(wl *v, const char *key, const char *text)
{
    pg_lead(&v->p, key, text);
}

void wl_letters(wl *v)
{
    v->letters = 1;
}

void wl_unpaged(wl *v)
{
    pg_unpaged(&v->p);
}

int wl_selected(wl *v, char *key)
{
    return pg_selected(&v->p, key);
}

int wl_all(wl *v)
{
    return v->all != 0;
}

u16 wl_nchosen(wl *v)
{
    LRESULT n;

    if (v->all)
        return v->keys.n;
    if (pg_rows(&v->p) == 0)
        return 0;
    n = v->flags & WL_MULTI ? SendMessage(v->box, LB_GETSELCOUNT, 0, 0L) : 0;
    return (u16)(n > 0 ? n : 1);
}

int wl_chosen(wl *v, u16 i, char *key, char *text)
{
    char none[LV_KEYSZ];
    LRESULT n;
    int r;
    int k;

    if (key == NULL)
        key = none;
    key[0] = '\0';
    if (i >= wl_nchosen(v))
        return 0;
    if (v->all) {
        if (text != NULL)
            text[0] = '\0';
        return ks_get(&v->keys, i, key);
    }
    r = v->p.sel - v->p.first;
    n = v->flags & WL_MULTI ? SendMessage(v->box, LB_GETCOUNT, 0, 0L) : 0;
    if (n > 0 && SendMessage(v->box, LB_GETSELCOUNT, 0, 0L) > 0)
        for (k = 0; k < (int)n; k++)
            if (is_sel(v, k) && i-- == 0) {
                r = k;
                break;
            }
    row_key(v, r, key);
    if (text != NULL)
        pg_text(&v->p, (u16)(v->p.first + r), text);
    return 1;
}

int wl_focus(wl *v)
{
    return v->focus;
}

void wl_right(wl *v, int x, int y)
{
    POINT pt;
    int r;

    SetFocus(v->box);
    pt.x = x;
    pt.y = y;
    ClientToScreen(v->box, &pt);
    r = row_at(v, pt);
    if (r != WL_NOROW)
        PostMessage(v->w, WM_WLPICK, (WPARAM)r,
                    v->flags & WL_MULTI && is_sel(v, r) ? WL_R_KEEP :
                                                        WL_R_ONE);
}

int wl_text(wl *v, char *text)
{
    text[0] = '\0';
    if (v->p.sel >= pg_rows(&v->p))
        return 0;
    pg_text(&v->p, v->p.sel, text);
    return 1;
}

int wl_drag_over(wl *v, wl *t, char *key)
{
    int i;

    i = row_at(t, v->at);
    if (i != WL_NOROW && key != NULL)
        row_key(t, i, key);
    return i;
}

void wl_target(wl *v, int i)
{
    if (i == v->target)
        return;
    mark(v, v->target, 0);
    mark(v, i, 1);
    v->target = i;
}

u8 wl_order(wl *v)
{
    return v->p.order;
}

int wl_count(wl *v, u32 *n)
{
    if (!v->exact)
        return 0;
    *n = v->total - (u32)lead(&v->p);
    return 1;
}

void wl_count_add(wl *v, int n)
{
    u32 least;

    if (!v->sv.all)
        return;
    least = rows_above(&v->p) + pg_rows(&v->p);
    v->sv.rows = n < 0 && (u32)-(i32)n > v->sv.rows ? 0 :
                 v->sv.rows + (u32)(i32)n;
    if (v->sv.rows < least)
        v->sv.rows = least;
    v->sv.pages = 0;
}

void wl_recount(wl *v)
{
    v->pending = (u8)((v->flags & WL_EXACT) != 0);
    v->exact = 0;
    drop(&v->sv);
}

void wl_count_set(wl *v, u32 n)
{
    v->pending = 0;
    v->sv.pages = 0;
    count_all(&v->sv, n + (u32)lead(&v->p));
}

int wl_width(HWND w, const wcol_tab *t)
{
    TEXTMETRIC tm;

    metrics(w, &tm);
    return width(&tm, t);
}

int wl_height(HWND w, u8 n)
{
    TEXTMETRIC tm;

    metrics(w, &tm);
    return height(&tm, n);
}

u8 wl_rows(HWND w, int h, int cx, const wcol_tab *t)
{
    TEXTMETRIC tm;
    int n;

    metrics(w, &tm);
    if (t != NULL && width(&tm, t) > cx)
        h -= GetSystemMetrics(SM_CYHSCROLL);
    n = (h - height(&tm, 0)) / (tm.tmHeight + WL_ROWPAD);
    return (u8)(n < 1 ? 1 : n > LV_MAXPAGE ? LV_MAXPAGE : n);
}

/*
 * WDLG.C - the place, the width and the look of the Windows programs'
 * dialogs, their list boxes of columns and the message boxes over them
 * (WDLG.H).
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "MSG.H"
#include "WLIST.H"
#include "WDLG.H"

/* The CtlType of a header's WM_DRAWITEM, none of Windows 3.0's ODT_. */
#define WD_ODTHEAD      0

/* The colour of the stock LTGRAY_BRUSH, Windows 3.0's own COLOR_BTNFACE. */
#define WD_GREY         RGB(192, 192, 192)

const char wd_k_edit[] = "Edit";

/* The class, beside wd_k_edit and WLIST.H's wl_k_class, of the wells
   (WDLG.H's look) that show a border without WS_BORDER: an edit control
   of Windows 3.0 drops it from its style and draws its border itself. */
static const char k_combo[] = "ComboBox";

static FARPROC wd_thunk;        /* wd_headproc's thunk, made once for
                                   every header, or NULL               */

/* The near edge of a span n pixels long centred on the span a..b, then
   moved onto 0..max as wd_centre says. */
static int place(int a, int b, int n, int max)
{
    int x;

    x = a + (b - a - n) / 2;
    if (x > max - n)
        x = max - n;
    return x < 0 ? 0 : x;
}

void wd_centre(HWND dlg)
{
    RECT o;
    RECT d;
    HWND w;
    int cx;
    int cy;

    cx = GetSystemMetrics(SM_CXSCREEN);
    cy = GetSystemMetrics(SM_CYSCREEN);
    w = GetWindow(dlg, GW_OWNER);
    if (w == NULL || IsIconic(w))
        SetRect(&o, 0, 0, cx, cy);
    else
        GetWindowRect(w, &o);
    GetWindowRect(dlg, &d);
    SetWindowPos(dlg, NULL, place(o.left, o.right, d.right - d.left, cx),
                 place(o.top, o.bottom, d.bottom - d.top, cy), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Move the control id of dlg right by dx and widen it by dw pixels. */
static void shift(HWND dlg, int id, int dx, int dw)
{
    RECT r;
    POINT p;
    HWND c;

    c = GetDlgItem(dlg, id);
    GetWindowRect(c, &r);
    p.x = r.left;
    p.y = r.top;
    ScreenToClient(dlg, &p);
    MoveWindow(c, p.x + dx, p.y, r.right - r.left + dw, r.bottom - r.top,
               FALSE);
}

/* The metrics of ANSI_VAR_FONT, selected into dc. */
static void var_font(HDC dc, TEXTMETRIC *tm)
{
    SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    GetTextMetrics(dc, tm);
}

void wd_fit(HWND dlg, int box, const wcol_tab *t, const int *wide,
            const int *move)
{
    RECT r;
    int grow;

    GetWindowRect(GetDlgItem(dlg, box), &r);
    grow = wl_width(dlg, t) - (r.right - r.left);
    if (grow <= 0)
        return;
    for (; *wide != 0; wide++)
        shift(dlg, *wide, 0, grow);
    for (; *move != 0; move++)
        shift(dlg, *move, grow, 0);
    GetWindowRect(dlg, &r);
    SetWindowPos(dlg, NULL, 0, 0, r.right - r.left + grow,
                 r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

/* A header's procedure: WM_PAINT sent to its dialog as WM_DRAWITEM, the
   rest to the static class's own procedure. */
LRESULT WCALLBACK wd_headproc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    DRAWITEMSTRUCT d;
    PAINTSTRUCT ps;

    if (wm != WM_PAINT)
        return CallWindowProc((WNDPROC)GetClassLong(w, GCL_WNDPROC), w, wm,
                              wp, lp);
    memset(&d, 0, sizeof d);
    d.CtlType = WD_ODTHEAD;
    d.CtlID = GetWindowWord(w, GWW_ID);
    d.itemAction = ODA_DRAWENTIRE;
    d.hwndItem = w;
    d.hDC = BeginPaint(w, &ps);
    GetClientRect(w, &d.rcItem);
    d.rcItem.left = GetSystemMetrics(SM_CXBORDER);
    SendMessage(GetParent(w), WM_DRAWITEM, d.CtlID,
                (LPARAM)(DRAWITEMSTRUCT FAR *)&d);
    EndPaint(w, &ps);
    return 0;
}

void wd_head(HWND hdr)
{
    if (wd_thunk == NULL)
        wd_thunk = MakeProcInstance((FARPROC)wd_headproc,
                                    (HINSTANCE)GetWindowWord(hdr,
                                                             GWW_HINSTANCE));
    if (wd_thunk != NULL)
        SetWindowLong(hdr, GWL_WNDPROC, (LONG)wd_thunk);
}

void wd_item(HWND dlg, UINT wm, LPARAM lp, const wcol_tab *rows,
             const wcol_tab *head)
{
    char text[LV_ROWTEXT + 1];
    const DRAWITEMSTRUCT FAR *d;
    TEXTMETRIC tm;
    wcol_lay lay;
    HDC dc;

    if (wm == WM_MEASUREITEM) {
        dc = GetDC(dlg);
        var_font(dc, &tm);
        ReleaseDC(dlg, dc);
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight =
            (UINT)(tm.tmHeight + WL_ROWPAD);
        return;
    }
    d = (const DRAWITEMSTRUCT FAR *)lp;
    var_font(d->hDC, &tm);
    if (d->CtlType != WD_ODTHEAD) {
        wcol_layout(&lay, rows, &tm);
        wcol_item(d, rows, &lay, GetSysColor(COLOR_WINDOWTEXT), 0);
        return;
    }
    wcol_layout(&lay, head, &tm);
    GetWindowText(d->hwndItem, text, (int)sizeof text);
    wcol_heads(d->hDC, &d->rcItem, text, head, &lay, 0, 0, 0, -1);
}

/* Draw the frame of the well w, a child of dlg, into dc: shadow as
   the pen dark along its left and top, white along its right and
   bottom, one pixel outside its window. */
static void frame(HWND dlg, HWND w, HDC dc, HPEN dark)
{
    RECT r;

    GetWindowRect(w, &r);
    ScreenToClient(dlg, (POINT FAR *)&r.left);
    ScreenToClient(dlg, (POINT FAR *)&r.right);
    SelectObject(dc, dark);
    MoveTo(dc, r.left - 1, r.bottom);
    LineTo(dc, r.left - 1, r.top - 1);
    LineTo(dc, r.right, r.top - 1);
    SelectObject(dc, GetStockObject(WHITE_PEN));
    LineTo(dc, r.right, r.bottom);
    LineTo(dc, r.left - 2, r.bottom);
}

/* 1 when w is of a class of the wells that need no WS_BORDER. */
static int boxed(HWND w)
{
    char cls[sizeof k_combo];

    GetClassName(w, cls, (int)sizeof cls);
    return txt_ieq(cls, wd_k_edit) || txt_ieq(cls, k_combo) ||
           txt_ieq(cls, wl_k_class);
}

/* 1 when w is a well of the look. */
static int well(HWND w)
{
    return IsWindowVisible(w) &&
           ((GetWindowLong(w, GWL_STYLE) & WS_BORDER) || boxed(w));
}

/* Paint the frames of dlg's wells, in the Z-order. */
static void paint(HWND dlg)
{
    PAINTSTRUCT ps;
    HPEN dark;
    HPEN old;
    HWND w;

    BeginPaint(dlg, &ps);
    dark = CreatePen(PS_SOLID, 1, wcol_unlike(ps.hdc, COLOR_BTNSHADOW));
    if (dark != NULL) {
        old = (HPEN)SelectObject(ps.hdc, dark);
        for (w = GetWindow(dlg, GW_CHILD); w != NULL;
             w = GetWindow(w, GW_HWNDNEXT))
            if (well(w))
                frame(dlg, w, ps.hdc, dark);
        SelectObject(ps.hdc, old);
        DeleteObject(dark);
    }
    EndPaint(dlg, &ps);
}

BOOL wd_look(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    HDC dc;
    UINT type;

    if (wm == WM_PAINT) {
        paint(dlg);
        return TRUE;
    }
    if (wm != WM_CTLCOLOR)
        return FALSE;
    dc = (HDC)wp;
    type = HIWORD(lp);
    if ((type != CTLCOLOR_DLG && type != CTLCOLOR_STATIC &&
         type != CTLCOLOR_BTN) ||
        GetSysColor(COLOR_BTNFACE) != WD_GREY ||
        GetNearestColor(dc, WD_GREY) != WD_GREY ||
        (type != CTLCOLOR_DLG && boxed((HWND)LOWORD(lp))))
        return FALSE;
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    SetBkColor(dc, WD_GREY);
    return (BOOL)GetStockObject(LTGRAY_BRUSH);
}

int wd_say(HWND w, UINT st, const char *text)
{
    char cap[MSG_MAXLEN + 1];

    GetWindowText(w, cap, (int)sizeof cap);
    return MessageBox(w, text, cap, st);
}

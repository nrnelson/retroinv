/*
 * WTOOL.C - the tool bar of INVWIN's frame (WTOOL.H).
 *
 * The bar keeps in its window words the slot's width, the handle of
 * the bitmap resource, loaded once, and its buttons' windows, and
 * nothing else: its pieces are read from its window text when they are
 * laid out, and each glyph is made from the locked resource as its
 * button is drawn, so that no near memory and no GDI object outlives a
 * message.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "PAGER.H"
#include "WCOLS.H"
#include "WTOOL.H"

#define WTB_WSLOT       0       /* window word: the slot's width        */
#define WTB_WRES        2       /* window word: the bitmap resource     */
#define WTB_WBTN        4       /* window words: button k's window at
                                   WTB_WBTN + 2 * k                     */
#define WTB_EXTRA       (WTB_WBTN + 2 * WTB_MAXBTNS)    /* their bytes  */

/* The bitmap's colours that stand for others (INVTOOL.PY), and how many
   it has. */
#define WTB_IFACE       7       /* light grey: COLOR_BTNFACE            */
#define WTB_IGRAY       8       /* dark grey: COLOR_GRAYTEXT            */
#define WTB_COLOURS     16
#define WTB_ROWB        (WTB_GLYPH / 2) /* bytes of a glyph's row, 4-bit
                                           pixels                       */

static const char k_class[] = "WTOOL";
static const char k_button[] = "Button";

/* The instance thunk of wtb_btnproc, made by the first bar. */
static FARPROC wtb_thunk;

/* The header of a glyph's bitmap, its colours as drawn. */
typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[WTB_COLOURS];
} wtb_dib;

/* Read the text of bar w into s, each piece NUL-ended; the number of
   pieces. */
static int pieces(HWND w, char *s)
{
    int n;

    GetWindowText(w, s, WTB_MAXTEXT + 1);
    for (n = 1; *s != '\0'; s++)
        if (*s == WTB_SEP) {
            *s = '\0';
            n++;
        }
    return n;
}

/* 1 when the piece p is the character ch alone. */
static int alone(const char *p, char ch)
{
    return p[0] == ch && p[1] == '\0';
}

/* Button k of bar w, or NULL. */
static HWND button(HWND w, int k)
{
    return (HWND)GetWindowWord(w, WTB_WBTN + 2 * k);
}

/* Put the buttons of bar w where its pieces put them (WTOOL.H); 1 and
   the slot's rectangle in *slot when it has one, else 0. */
static int place(HWND w, RECT *slot)
{
    char s[WTB_MAXTEXT + 1];
    const char *p;
    int after;
    int has;
    int n;
    int x;
    int k;

    n = pieces(w, s);
    x = WTB_PADX;
    after = 0;
    has = 0;
    for (p = s, k = 0; n-- > 0; p += strlen(p) + 1) {
        if (alone(p, WTB_GAP)) {
            x += WTB_SPACE;
            after = 0;
        } else if (alone(p, WTB_SLOT)) {
            SetRect(slot, x, WTB_PADY,
                    x + (int)GetWindowWord(w, WTB_WSLOT),
                    WTB_PADY + WTB_BTNH);
            x = slot->right;
            after = 0;
            has = 1;
        } else if (k < WTB_MAXBTNS) {
            x -= after;
            MoveWindow(button(w, k++), x, WTB_PADY, WTB_BTNW, WTB_BTNH,
                       TRUE);
            x += WTB_BTNW;
            after = 1;
        }
    }
    return has;
}

/* Draw glyph k of bar w at (x, y) of dc, its grayed form when gray,
   light grey as face: a bitmap of the glyph's rows alone made from the
   resource, its stand-in colours set, and copied through a memory
   display context. */
static void glyph(HWND w, HDC dc, int x, int y, int k, int gray,
                  COLORREF face)
{
    const BITMAPINFOHEADER FAR *src;
    COLORREF grey;
    HBITMAP bm;
    HBITMAP old;
    HGLOBAL res;
    HDC mem;
    wtb_dib d;
    int row;

    res = (HGLOBAL)GetWindowWord(w, WTB_WRES);
    src = (const BITMAPINFOHEADER FAR *)LockResource(res);
    if (src == NULL)
        return;
    row = (int)src->biHeight - (2 * k + 1 + gray) * WTB_GLYPH;
    if (row >= 0) {
        d = *(const wtb_dib FAR *)src;
        d.h.biHeight = WTB_GLYPH;
        d.h.biSizeImage = 0;
        grey = wcol_unlike(dc, COLOR_GRAYTEXT);
        d.c[WTB_IFACE].rgbRed = GetRValue(face);
        d.c[WTB_IFACE].rgbGreen = GetGValue(face);
        d.c[WTB_IFACE].rgbBlue = GetBValue(face);
        d.c[WTB_IGRAY].rgbRed = GetRValue(grey);
        d.c[WTB_IGRAY].rgbGreen = GetGValue(grey);
        d.c[WTB_IGRAY].rgbBlue = GetBValue(grey);
        bm = CreateDIBitmap(dc, &d.h, CBM_INIT,
                            (const char FAR *)src + sizeof d +
                            (long)row * WTB_ROWB,
                            (BITMAPINFO *)&d, DIB_RGB_COLORS);
        mem = CreateCompatibleDC(dc);
        if (bm != NULL && mem != NULL) {
            old = (HBITMAP)SelectObject(mem, bm);
            BitBlt(dc, x, y, WTB_GLYPH, WTB_GLYPH, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
        }
        if (mem != NULL)
            DeleteDC(mem);
        if (bm != NULL)
            DeleteObject(bm);
    }
    UnlockResource(res);
}

/* Draw the button of *d, a button of bar w, as WTOOL.H says. */
static void draw(HWND w, const DRAWITEMSTRUCT FAR *d)
{
    COLORREF face;
    HBRUSH brush;
    HPEN frame;
    HPEN dark;
    HPEN old;
    RECT r;
    HDC dc;
    int down;
    int k;

    for (k = 0; k < WTB_MAXBTNS && button(w, k) != d->hwndItem; k++)
        ;
    dc = d->hDC;
    r = d->rcItem;
    down = (d->itemState & ODS_SELECTED) != 0;
    face = GetNearestColor(dc, GetSysColor(COLOR_BTNFACE));
    brush = CreateSolidBrush(face);
    frame = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWFRAME));
    dark = CreatePen(PS_SOLID, 1, wcol_unlike(dc, COLOR_BTNSHADOW));
    if (brush != NULL && frame != NULL && dark != NULL) {
        InflateRect(&r, -1, -1);
        FillRect(dc, &r, brush);
        InflateRect(&r, 1, 1);
        old = (HPEN)SelectObject(dc, frame);
        MoveTo(dc, r.left + 1, r.top);
        LineTo(dc, r.right - 1, r.top);
        MoveTo(dc, r.left + 1, r.bottom - 1);
        LineTo(dc, r.right - 1, r.bottom - 1);
        MoveTo(dc, r.left, r.top + 1);
        LineTo(dc, r.left, r.bottom - 1);
        MoveTo(dc, r.right - 1, r.top + 1);
        LineTo(dc, r.right - 1, r.bottom - 1);
        SelectObject(dc, down ? dark : (HPEN)GetStockObject(WHITE_PEN));
        MoveTo(dc, r.left + 1, r.bottom - (down ? 2 : 3));
        LineTo(dc, r.left + 1, r.top + 1);
        LineTo(dc, r.right - (down ? 1 : 2), r.top + 1);
        if (!down) {
            SelectObject(dc, dark);
            MoveTo(dc, r.right - 2, r.top + 1);
            LineTo(dc, r.right - 2, r.bottom - 2);
            LineTo(dc, r.left, r.bottom - 2);
            MoveTo(dc, r.right - 3, r.top + 2);
            LineTo(dc, r.right - 3, r.bottom - 3);
            LineTo(dc, r.left + 1, r.bottom - 3);
        }
        SelectObject(dc, old);
        glyph(w, dc, (WTB_BTNW - WTB_GLYPH) / 2 + down,
              (WTB_BTNH - WTB_GLYPH) / 2 + down, k,
              (d->itemState & ODS_DISABLED) != 0, face);
    }
    if (dark != NULL)
        DeleteObject(dark);
    if (frame != NULL)
        DeleteObject(frame);
    if (brush != NULL)
        DeleteObject(brush);
}

/* 1 when the point of a mouse message's lp is over the button b. */
static int over(HWND b, LPARAM lp)
{
    RECT r;
    POINT pt;

    pt.x = (int)LOWORD(lp);
    pt.y = (int)HIWORD(lp);
    GetClientRect(b, &r);
    return PtInRect(&r, pt);
}

/* The procedure of a bar's buttons: the mouse taken here, without the
   focus, as WTOOL.H says; the rest to the class Button's own, after a
   press in progress is let go on WM_CANCELMODE. */
LRESULT WCALLBACK wtb_btnproc(HWND b, UINT wm, WPARAM wp, LPARAM lp)
{
    switch (wm) {
    case WM_CANCELMODE:
        if (GetCapture() == b)
            ReleaseCapture();
        SendMessage(b, BM_SETSTATE, FALSE, 0L);
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (IsWindowEnabled(b)) {
            SetCapture(b);
            SendMessage(b, BM_SETSTATE, TRUE, 0L);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == b)
            SendMessage(b, BM_SETSTATE, (WPARAM)over(b, lp), 0L);
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() != b)
            return 0;
        ReleaseCapture();
        SendMessage(b, BM_SETSTATE, FALSE, 0L);
        if (over(b, lp) && IsWindowEnabled(b) &&
            IsWindowEnabled(GetParent(GetParent(b))))
            SendMessage(GetParent(b), WM_COMMAND,
                        (WPARAM)GetWindowWord(b, GWW_ID),
                        MAKELPARAM(b, BN_CLICKED));
        return 0;
    default:
        break;
    }
    return CallWindowProc((WNDPROC)GetClassLong(b, GCL_WNDPROC), b, wm, wp,
                          lp);
}

LRESULT WCALLBACK wtb_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HBRUSH line;
    HWND b;
    RECT r;

    switch (wm) {
    case WM_CANCELMODE:
        b = GetCapture();
        if (b != NULL && GetParent(b) == w)
            SendMessage(b, WM_CANCELMODE, 0, 0L);
        return 0;
    case WM_DESTROY:
        FreeResource((HGLOBAL)GetWindowWord(w, WTB_WRES));
        return 0;
    case WM_PAINT:
        BeginPaint(w, &ps);
        line = CreateSolidBrush(GetSysColor(COLOR_WINDOWFRAME));
        if (line != NULL) {
            GetClientRect(w, &r);
            r.top = r.bottom - 1;
            FillRect(ps.hdc, &r, line);
            DeleteObject(line);
        }
        EndPaint(w, &ps);
        return 0;
    case WM_DRAWITEM:
        draw(w, (const DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_COMMAND:
        return SendMessage(GetParent(w), WM_COMMAND, wp, lp);
    case WTBM_SLOT:
        SetWindowWord(w, WTB_WSLOT, (WORD)wp);
        if (!place(w, &r))
            return 0;
        *(RECT FAR *)lp = r;
        return 1;
    case WTBM_BUTTON:
        return wp < WTB_MAXBTNS ? (LRESULT)(UINT)button(w, (int)wp) : 0;
    default:
        return DefWindowProc(w, wm, wp, lp);
    }
}

HWND wtb_create(HWND owner, RECT *r, int id, UINT sid, const UINT *cmds)
{
    char s[WTB_MAXTEXT + 1];
    const char *p;
    WNDCLASS wc;
    HINSTANCE inst;
    HGLOBAL res;
    HRSRC rs;
    RECT slot;
    HWND w;
    HWND b;
    int n;
    int k;

    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    if (LoadString(inst, sid, s, (int)sizeof s) == 0)
        return NULL;
    rs = FindResource(inst, k_class, RT_BITMAP);
    res = rs == NULL ? NULL : LoadResource(inst, rs);
    if (res == NULL)
        return NULL;
    if (wtb_thunk == NULL)
        wtb_thunk = MakeProcInstance((FARPROC)wtb_btnproc, inst);
    if (wtb_thunk == NULL) {
        FreeResource(res);
        return NULL;
    }
    if (!GetClassInfo(inst, k_class, &wc)) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = wtb_proc;
        wc.cbWndExtra = WTB_EXTRA;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = k_class;
        RegisterClass(&wc);
    }
    r->bottom = r->top + WTB_HEIGHT;
    w = CreateWindow(k_class, s, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                     r->left, r->top, r->right - r->left, WTB_HEIGHT, owner,
                     (HMENU)id, inst, NULL);
    if (w == NULL) {
        FreeResource(res);
        return NULL;
    }
    SetWindowWord(w, WTB_WRES, (WORD)res);
    n = pieces(w, s);
    for (p = s, k = 0; n-- > 0 && k < WTB_MAXBTNS; p += strlen(p) + 1) {
        if (alone(p, WTB_GAP) || alone(p, WTB_SLOT))
            continue;
        b = CreateWindow(k_button, p, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                         0, 0, 0, 0, w, (HMENU)cmds[k], inst, NULL);
        if (b == NULL) {
            DestroyWindow(w);
            return NULL;
        }
        SetWindowLong(b, GWL_WNDPROC, (LONG)wtb_thunk);
        SetWindowWord(w, WTB_WBTN + 2 * k++, (WORD)b);
    }
    place(w, &slot);
    return w;
}

/*
 * WSTAT.C - the status bar of INVWIN's frame (WSTAT.H).
 *
 * The bar keeps in its window words its font, its panels, those whose
 * texts were cut to their room and their widths, and nothing else: its
 * texts are its window text, read when a message needs them, so that no
 * near memory and no GDI object but the owner's font outlives a
 * message.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "WSTAT.H"
#include "PAGER.H"
#include "WCOLS.H"

#define WSB_WFONT       0       /* window word: the bar's font          */
#define WSB_WN          2       /* window word: its panels              */
#define WSB_WCUT        4       /* window word: bit i, panel i's text
                                   cut to its room                      */
#define WSB_WWIDTH      6       /* window words: panel i's width at
                                   WSB_WWIDTH + 2 * i, but the last's   */
#define WSB_EXTRA       (WSB_WWIDTH + 2 * (WSB_MAXPANELS - 1))

extern char wsb_text_fits[1];
extern char wsb_text_fits[(WSB_MAXTEXT ==
                           WSB_MAXPANELS * (WSB_MAXCELL + 1) - 1) ? 1 : 2];

static const char k_class[] = "WSTAT";

/* The bytes panel i of n may hold (WSB_MAXTEXT). */
static int room(int i, int n)
{
    return i < n - 1 ? WSB_MAXCELL :
           WSB_MAXTEXT - (n - 1) * (WSB_MAXCELL + 1);
}

/* The height of a bar whose font's lines are lineh pixels high. */
static int height(int lineh)
{
    return 1 + 2 * (WSB_MARGIN + 1 + WSB_PADY) + lineh;
}

/* The start of panel i's text in s, the bar's text. */
static char *text_of(char *s, int i)
{
    for (; i > 0; i--)
        s = strchr(s, WSB_SEP) + 1;
    return s;
}

/* The bytes of the text at p, up to its WSB_SEP or NUL. */
static int len_of(const char *p)
{
    const char *e;

    e = strchr(p, WSB_SEP);
    return e == NULL ? (int)strlen(p) : (int)(e - p);
}

/* The rectangle of panel i of the n of bar w, which is wide, its edges
   included. */
static void rect_of(HWND w, int i, int n, int wide, RECT *r)
{
    int j;

    GetClientRect(w, r);
    r->top = 1 + WSB_MARGIN;
    r->bottom -= WSB_MARGIN;
    r->left = WSB_MARGIN;
    for (j = 0; j < i; j++)
        r->left += (int)GetWindowWord(w, WSB_WWIDTH + 2 * j) + WSB_GAP;
    r->right = i < n - 1 ?
               r->left + (int)GetWindowWord(w, WSB_WWIDTH + 2 * i) :
               wide - WSB_MARGIN;
    if (r->right < r->left)
        r->right = r->left;
}

/* Paint bar w: its top line and its panels, each sunken, with its text. */
static void paint(HWND w)
{
    char s[WSB_MAXTEXT + 1];
    PAINTSTRUCT ps;
    HBRUSH line;
    HFONT font;
    HPEN dark;
    HPEN pen;
    RECT c;
    RECT r;
    char *p;
    int n;
    int i;

    BeginPaint(w, &ps);
    line = CreateSolidBrush(GetSysColor(COLOR_WINDOWFRAME));
    dark = CreatePen(PS_SOLID, 1, wcol_unlike(ps.hdc, COLOR_BTNSHADOW));
    if (line != NULL && dark != NULL) {
        GetClientRect(w, &c);
        SetRect(&r, 0, 0, c.right, 1);
        FillRect(ps.hdc, &r, line);
        font = (HFONT)SelectObject(ps.hdc,
                                   (HFONT)GetWindowWord(w, WSB_WFONT));
        pen = (HPEN)SelectObject(ps.hdc, dark);
        SetBkMode(ps.hdc, TRANSPARENT);
        SetTextColor(ps.hdc, GetSysColor(COLOR_BTNTEXT));
        GetWindowText(w, s, (int)sizeof s);
        n = (int)GetWindowWord(w, WSB_WN);
        for (i = 0, p = s; i < n; i++, p += len_of(p) + 1) {
            rect_of(w, i, n, c.right, &r);
            if (r.left >= c.right)
                break;
            SelectObject(ps.hdc, dark);
            MoveTo(ps.hdc, r.left, r.bottom - 2);
            LineTo(ps.hdc, r.left, r.top);
            LineTo(ps.hdc, r.right - 1, r.top);
            SelectObject(ps.hdc, GetStockObject(WHITE_PEN));
            MoveTo(ps.hdc, r.right - 1, r.top);
            LineTo(ps.hdc, r.right - 1, r.bottom - 1);
            LineTo(ps.hdc, r.left - 1, r.bottom - 1);
            InflateRect(&r, -1, -1);
            r.left += WSB_PADX;
            r.top += WSB_PADY;
            DrawText(ps.hdc, p, len_of(p), &r,
                     DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(ps.hdc, pen);
        SelectObject(ps.hdc, font);
    }
    if (dark != NULL)
        DeleteObject(dark);
    if (line != NULL)
        DeleteObject(line);
    EndPaint(w, &ps);
}

/* WSBM_PANELS: bar w made n panels, the first n - 1 wide as widths
   says, the texts kept cut to their room, those cut noted, drawn again
   unless nothing changed; n, or 0 when n is out of range.  The pieces
   are copied down in place: none grows. */
static LRESULT panels(HWND w, int n, const int FAR *widths)
{
    char s[WSB_MAXTEXT + 1];
    const char *p;
    char *q;
    UINT cut;
    int same;
    int len;
    int m;
    int i;

    if (n < 1 || n > WSB_MAXPANELS)
        return 0;
    m = (int)GetWindowWord(w, WSB_WN);
    same = n == m;
    for (i = 0; i < n - 1 && same; i++)
        same = widths[i] == (int)GetWindowWord(w, WSB_WWIDTH + 2 * i);
    if (same)
        return n;
    GetWindowText(w, s, (int)sizeof s);
    cut = GetWindowWord(w, WSB_WCUT);
    for (i = 0, p = s, q = s; i < n; i++) {
        if (i > 0)
            *q++ = WSB_SEP;
        if (i < m) {
            len = len_of(p);
            if (len > room(i, n))
                cut |= 1u << i;
            memmove(q, p, (size_t)min(len, room(i, n)));
            q += min(len, room(i, n));
            p += len + 1;
        }
    }
    *q = '\0';
    SetWindowWord(w, WSB_WCUT, (WORD)(cut & ((1u << n) - 1)));
    for (i = 0; i < n - 1; i++)
        SetWindowWord(w, WSB_WWIDTH + 2 * i, (WORD)widths[i]);
    SetWindowWord(w, WSB_WN, (WORD)n);
    SetWindowText(w, s);
    InvalidateRect(w, NULL, TRUE);
    return n;
}

/* WSBM_SETTEXT: panel i of bar w given the text t, NULL for none, noted
   when cut to its room; 1, or 0 when there is no such panel. */
static LRESULT settext(HWND w, int i, LPCSTR t)
{
    char s[WSB_MAXTEXT + 1];
    RECT r;
    UINT cut;
    char *p;
    int len;
    int old;
    int n;
    int k;

    n = (int)GetWindowWord(w, WSB_WN);
    if (i < 0 || i >= n)
        return 0;
    GetWindowText(w, s, (int)sizeof s);
    p = text_of(s, i);
    old = len_of(p);
    for (len = 0; t != NULL && t[len] != '\0' && len < room(i, n); len++)
        ;
    cut = GetWindowWord(w, WSB_WCUT) & ~(1u << i);
    if (t != NULL && t[len] != '\0')
        cut |= 1u << i;
    SetWindowWord(w, WSB_WCUT, (WORD)cut);
    for (k = 0; k < len && p[k] == (t[k] == WSB_SEP ? ' ' : t[k]); k++)
        ;
    if (k == len && len == old)
        return 1;
    memmove(p + len, p + old, strlen(p + old) + 1);
    for (k = 0; k < len; k++)
        p[k] = t[k] == WSB_SEP ? ' ' : t[k];
    SetWindowText(w, s);
    GetClientRect(w, &r);
    rect_of(w, i, n, r.right, &r);
    InvalidateRect(w, &r, TRUE);
    return 1;
}

/* WSBM_CUT: 1 when a panel of bar w holds a text cut to its room, or
   one but the last a text wider, in the bar's font, than the room inside
   its edges and WSB_PADX, else 0. */
static LRESULT cut(HWND w)
{
    char s[WSB_MAXTEXT + 1];
    HFONT font;
    RECT r;
    char *p;
    HDC dc;
    int n;
    int i;
    int c;

    if (GetWindowWord(w, WSB_WCUT) != 0)
        return 1;
    GetWindowText(w, s, (int)sizeof s);
    n = (int)GetWindowWord(w, WSB_WN);
    dc = GetDC(w);
    font = (HFONT)SelectObject(dc, (HFONT)GetWindowWord(w, WSB_WFONT));
    c = 0;
    for (i = 0, p = s; i < n - 1 && !c; i++, p += len_of(p) + 1) {
        rect_of(w, i, n, 0, &r);
        c = (int)LOWORD(GetTextExtent(dc, p, len_of(p))) >
            r.right - r.left - 2 - WSB_PADX;
    }
    SelectObject(dc, font);
    ReleaseDC(w, dc);
    return c;
}

LRESULT WCALLBACK wsb_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    switch (wm) {
    case WM_PAINT:
        paint(w);
        return 0;
    case WSBM_PANELS:
        return panels(w, (int)wp, (const int FAR *)lp);
    case WSBM_SETTEXT:
        return settext(w, (int)wp, (LPCSTR)lp);
    case WSBM_CUT:
        return cut(w);
    default:
        return DefWindowProc(w, wm, wp, lp);
    }
}

HWND wsb_create(HWND owner, RECT *r, int id, HFONT font)
{
    TEXTMETRIC tm;
    WNDCLASS wc;
    HINSTANCE inst;
    HFONT old;
    HDC dc;
    HWND w;

    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    if (font == NULL)
        font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    dc = GetDC(owner);
    old = (HFONT)SelectObject(dc, font);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(owner, dc);
    r->top = r->bottom - height(tm.tmHeight);
    if (!GetClassInfo(inst, k_class, &wc)) {
        memset(&wc, 0, sizeof wc);
        wc.style = CS_HREDRAW;
        wc.lpfnWndProc = wsb_proc;
        wc.cbWndExtra = WSB_EXTRA;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = k_class;
        RegisterClass(&wc);
    }
    w = CreateWindow(k_class, NULL, WS_CHILD | WS_VISIBLE, r->left, r->top,
                     r->right - r->left, r->bottom - r->top, owner,
                     (HMENU)id, inst, NULL);
    if (w != NULL) {
        SetWindowWord(w, WSB_WFONT, (WORD)font);
        SetWindowWord(w, WSB_WN, 1);
    }
    return w;
}

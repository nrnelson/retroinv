/*
 * WTAB.C - the tab strip of the Windows programs' dialogs (WTAB.H).
 *
 * The strip keeps two window words, the current tab and the font, and
 * nothing else: each message that needs the tabs lays them out afresh
 * from the window text in the strip's font (layout), on the stack, so
 * that no near memory and no GDI object outlives a message.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "WCOLS.H"
#include "WTAB.H"

#define WT_WCUR         0       /* window word: the current tab         */
#define WT_WFONT        2       /* window word: the strip's font        */
#define WT_EXTRA        4       /* bytes of the window words            */

static const char k_class[] = "WTAB";

/* The tabs of a strip as laid out: the texts, each with the single '&'
   that marks its mnemonic (one put before its first character when it
   has none), and each tab's place. */
typedef struct {
    char s[WT_MAXTABS + WT_MAXTEXT + 1];        /* the texts, NUL-ended */
    u8 at[WT_MAXTABS];          /* where each tab's text starts in s    */
    int x[WT_MAXTABS + 1];      /* each tab's left edge, unlifted, then
                                   the last one's right edge            */
    int n;                      /* the tabs, 1..WT_MAXTABS              */
    int cur;                    /* the current one                      */
    int lineh;                  /* a line of the font                   */
    int h;                      /* the strip's height                   */
} wt_lay;

/* The pens and the brush a strip is drawn with. */
typedef struct {
    HPEN frame;                 /* COLOR_WINDOWFRAME                    */
    HPEN light;                 /* COLOR_WINDOW                         */
    HPEN dark;                  /* COLOR_BTNSHADOW, as wcol_unlike's    */
    HBRUSH face;                /* COLOR_BTNFACE                        */
} wt_gdi;

/* The height of a strip whose font's lines are lineh pixels high. */
static int height(int lineh)
{
    return WT_LIFT + 2 * WT_PADY + lineh + 1;
}

/* The width in dc's font of a tab's text s as DrawText draws it: its
   marking '&' left out, each "&&" one '&'. */
static int width(HDC dc, const char *s)
{
    char t[WT_MAXTEXT + 1];
    int n;

    for (n = 0; *s != '\0'; s++) {
        if (*s == '&' && *++s == '\0')
            break;
        t[n++] = *s;
    }
    return (int)LOWORD(GetTextExtent(dc, t, n));
}

/* The character ch of the Windows set in upper case, as Windows folds
   it: AnsiUpper takes a character for a pointer whose segment is 0. */
static int upper(int ch)
{
    return (int)(BYTE)(DWORD)AnsiUpper((LPSTR)MAKELP(0, (BYTE)ch));
}

/* The mnemonic of a tab's text s, the character after its single '&',
   in upper case, or 0 for none. */
static int mnemonic(const char *s)
{
    for (; *s != '\0'; s++)
        if (*s == '&' && *++s != '&')
            return upper(*s);
    return 0;
}

/* Lay out the tabs of strip w into *t, dc's font the strip's.  The
   window text is read into the end of s and copied to its start, each
   WT_SEP made a NUL and an '&' put before a text that has no single one:
   the copy stays behind the text it reads, as each tab adds one byte at
   most. */
static void layout(HWND w, HDC dc, wt_lay *t)
{
    TEXTMETRIC tm;
    char *src;
    char *p;
    int len;
    int has;

    src = t->s + WT_MAXTABS;
    GetWindowText(w, src, WT_MAXTEXT + 1);
    GetTextMetrics(dc, &tm);
    t->lineh = tm.tmHeight;
    t->h = height(t->lineh);
    t->x[0] = WT_LIFT;
    p = t->s;
    for (t->n = 0; t->n < WT_MAXTABS; t->n++) {
        t->at[t->n] = (u8)(p - t->s);
        has = 0;
        for (len = 0; !has && src[len] != '\0' && src[len] != WT_SEP;
             len++)
            if (src[len] == '&')
                has = src[++len] != '&';
        if (!has)
            *p++ = '&';
        while (*src != '\0' && *src != WT_SEP)
            *p++ = *src++;
        *p++ = '\0';
        t->x[t->n + 1] = t->x[t->n] + width(dc, t->s + t->at[t->n]) +
                         2 * WT_PADX;
        if (*src++ != WT_SEP) {
            t->n++;
            break;
        }
    }
    t->cur = (int)GetWindowWord(w, WT_WCUR);
    if (t->cur >= t->n)
        t->cur = t->n - 1;
}

/* Lay out the tabs of strip w into *t through a display context of its
   own. */
static void measure(HWND w, wt_lay *t)
{
    HDC dc;
    HFONT old;

    dc = GetDC(w);
    old = (HFONT)SelectObject(dc, (HFONT)GetWindowWord(w, WT_WFONT));
    layout(w, dc, t);
    SelectObject(dc, old);
    ReleaseDC(w, dc);
}

/* The rectangle of tab i of t, as WTM_GETRECT gives it. */
static void rect_of(const wt_lay *t, int i, RECT *r)
{
    SetRect(r, t->x[i], WT_LIFT, t->x[i + 1], t->h);
    if (i == t->cur) {
        r->left -= WT_LIFT;
        r->right += WT_LIFT;
        r->top = 0;
    }
}

/* The tab of t at (x, y), the current one first as it stands over its
   neighbours, or -1. */
static int hit(const wt_lay *t, int x, int y)
{
    RECT r;
    POINT pt;
    int i;

    pt.x = x;
    pt.y = y;
    rect_of(t, t->cur, &r);
    if (PtInRect(&r, pt))
        return t->cur;
    for (i = 0; i < t->n; i++) {
        rect_of(t, i, &r);
        if (PtInRect(&r, pt))
            return i;
    }
    return -1;
}

/* Make tab i current in strip w and draw it again. */
static void set(HWND w, int i)
{
    SetWindowWord(w, WT_WCUR, (WORD)i);
    InvalidateRect(w, NULL, TRUE);
}

/* The user's choice of tab i in strip w: made current, drawn and its
   owner notified, unless it is current already. */
static void choose(HWND w, int i)
{
    if (i == (int)GetWindowWord(w, WT_WCUR))
        return;
    set(w, i);
    UpdateWindow(w);
    SendMessage(GetParent(w), WM_COMMAND, (WPARAM)GetWindowWord(w, GWW_ID),
                MAKELPARAM(w, WTN_CHANGE));
}

/* Draw tab i of t into dc with g, its outline, edges, face and text.
   A tab other than the current one ends above the bottom line. */
static void tab(HDC dc, const wt_lay *t, int i, const wt_gdi *g)
{
    RECT r;
    RECT f;
    int e;

    rect_of(t, i, &r);
    e = i == t->cur ? r.bottom : r.bottom - 1;
    SetRect(&f, r.left + 1, r.top + 1, r.right - 1, e);
    FillRect(dc, &f, g->face);
    SelectObject(dc, g->frame);
    MoveTo(dc, r.left, e - 1);
    LineTo(dc, r.left, r.top + 2);
    LineTo(dc, r.left + 2, r.top);
    LineTo(dc, r.right - 3, r.top);
    LineTo(dc, r.right - 1, r.top + 2);
    LineTo(dc, r.right - 1, e);
    SelectObject(dc, g->light);
    MoveTo(dc, r.left + 1, e - 1);
    LineTo(dc, r.left + 1, r.top + 1);
    MoveTo(dc, r.left + 2, r.top + 1);
    LineTo(dc, r.right - 2, r.top + 1);
    SelectObject(dc, g->dark);
    MoveTo(dc, r.right - 2, r.top + 2);
    LineTo(dc, r.right - 2, e);
    SetRect(&f, t->x[i] + WT_PADX, r.top + WT_PADY,
            t->x[i + 1] - WT_PADX, r.top + WT_PADY + t->lineh);
    DrawText(dc, t->s + t->at[i], -1, &f, DT_SINGLELINE);
}

/* Paint strip w: the bottom line, the other tabs, then the current one
   over it and its neighbours, and its focus rectangle. */
static void paint(HWND w)
{
    PAINTSTRUCT ps;
    wt_lay t;
    wt_gdi g;
    RECT r;
    HFONT font;
    HPEN pen;
    int i;

    BeginPaint(w, &ps);
    g.frame = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWFRAME));
    g.light = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOW));
    g.dark = CreatePen(PS_SOLID, 1, wcol_unlike(ps.hdc, COLOR_BTNSHADOW));
    g.face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    if (g.frame != NULL && g.light != NULL && g.dark != NULL &&
        g.face != NULL) {
        font = (HFONT)SelectObject(ps.hdc,
                                   (HFONT)GetWindowWord(w, WT_WFONT));
        pen = (HPEN)SelectObject(ps.hdc, g.frame);
        layout(w, ps.hdc, &t);
        GetClientRect(w, &r);
        MoveTo(ps.hdc, 0, t.h - 1);
        LineTo(ps.hdc, r.right, t.h - 1);
        SetBkMode(ps.hdc, TRANSPARENT);
        SetTextColor(ps.hdc, GetSysColor(COLOR_BTNTEXT));
        for (i = 0; i < t.n; i++)
            if (i != t.cur)
                tab(ps.hdc, &t, i, &g);
        tab(ps.hdc, &t, t.cur, &g);
        if (GetFocus() == w) {
            SetRect(&r, t.x[t.cur] + WT_PADX - 2, WT_PADY - 1,
                    t.x[t.cur + 1] - WT_PADX + 2, WT_PADY + t.lineh + 1);
            DrawFocusRect(ps.hdc, &r);
        }
        SelectObject(ps.hdc, pen);
        SelectObject(ps.hdc, font);
    }
    if (g.frame != NULL)
        DeleteObject(g.frame);
    if (g.light != NULL)
        DeleteObject(g.light);
    if (g.dark != NULL)
        DeleteObject(g.dark);
    if (g.face != NULL)
        DeleteObject(g.face);
    EndPaint(w, &ps);
}

LRESULT WCALLBACK wt_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    wt_lay t;
    RECT rc;
    LRESULT r;
    int i;

    switch (wm) {
    case WM_PAINT:
        paint(w);
        return 0;
    case WM_ERASEBKGND:
        GetClientRect(w, &rc);
        FillRect((HDC)wp, &rc,
                 (HBRUSH)SendMessage(GetParent(w), WM_CTLCOLOR, wp,
                                     MAKELPARAM(w, CTLCOLOR_STATIC)));
        return 1;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(w, NULL, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(w);
        measure(w, &t);
        i = hit(&t, (int)LOWORD(lp), (int)HIWORD(lp));
        if (i >= 0)
            choose(w, i);
        return 0;
    case WM_KEYDOWN:
        if (wp != VK_LEFT && wp != VK_RIGHT)
            break;
        measure(w, &t);
        i = t.cur + (wp == VK_LEFT ? -1 : 1);
        if (i >= 0 && i < t.n)
            choose(w, i);
        return 0;
    case WM_SETTEXT:
        r = DefWindowProc(w, wm, wp, lp);
        measure(w, &t);
        set(w, t.cur);
        return r;
    case WTM_GETCUR:
        return (LRESULT)(int)GetWindowWord(w, WT_WCUR);
    case WTM_SETCUR:
        measure(w, &t);
        if (wp >= (WPARAM)t.n)
            return -1;
        set(w, (int)wp);
        return (LRESULT)wp;
    case WTM_GETRECT:
        measure(w, &t);
        if (wp >= (WPARAM)t.n)
            return 0;
        rect_of(&t, (int)wp, &rc);
        *(RECT FAR *)lp = rc;
        return 1;
    default:
        break;
    }
    return DefWindowProc(w, wm, wp, lp);
}

HWND wt_create(HWND owner, RECT *r, int id, UINT sid, HFONT font)
{
    char s[WT_MAXTEXT + 1];
    TEXTMETRIC tm;
    WNDCLASS wc;
    HINSTANCE inst;
    HFONT old;
    HDC dc;
    HWND w;

    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    if (LoadString(inst, sid, s, (int)sizeof s) == 0)
        return NULL;
    if (font == NULL)
        font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    dc = GetDC(owner);
    old = (HFONT)SelectObject(dc, font);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(owner, dc);
    r->bottom = r->top + height(tm.tmHeight);
    if (!GetClassInfo(inst, k_class, &wc)) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = wt_proc;
        wc.cbWndExtra = WT_EXTRA;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = k_class;
        RegisterClass(&wc);
    }
    w = CreateWindow(k_class, s, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                     r->left, r->top, r->right - r->left, r->bottom - r->top,
                     owner, (HMENU)id, inst, NULL);
    if (w != NULL)
        SetWindowWord(w, WT_WFONT, (WORD)font);
    return w;
}

int wt_key(HWND w, UINT wm, WPARAM wp)
{
    wt_lay t;
    HWND f;
    int back;
    int ch;
    int i;

    if (wm == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0 &&
        (wp == VK_NEXT || wp == VK_PRIOR || wp == VK_TAB)) {
        back = wp == VK_PRIOR ||
               (wp == VK_TAB && GetKeyState(VK_SHIFT) < 0);
        measure(w, &t);
        choose(w, (t.cur + (back ? t.n - 1 : 1)) % t.n);
        return 1;
    }
    f = GetFocus();
    if (wm != WM_SYSCHAR &&
        (wm != WM_CHAR || f == NULL ||
         (SendMessage(f, WM_GETDLGCODE, 0, 0L) & DLGC_WANTCHARS)))
        return 0;
    ch = upper((int)wp);
    measure(w, &t);
    for (i = 0; i < t.n; i++)
        if (mnemonic(t.s + t.at[i]) == ch) {
            choose(w, i);
            return 1;
        }
    return 0;
}

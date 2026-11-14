/*
 * WPREV.C - the preview panel of INVWIN's frame (WPREV.H).
 *
 * The panel keeps in its window words its block of lines, the bytes of
 * it in use, the lines that are fields, and what it measures whenever
 * its lines or its size change: the label column's width and a row's
 * height.  A line is its label and its value, each with its NUL, a
 * line of the notes with an empty label, one after the other from the
 * block's start; the top row is the scroll bar's place.  So no near
 * buffer outlives a message.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "WPREV.H"

#define WPV_WBLK        0       /* window word: the lines' block        */
#define WPV_WUSED       2       /* its bytes in use                     */
#define WPV_WNF         4       /* the lines that are fields, the first */
#define WPV_WLABW       6       /* the label column's width in pixels   */
#define WPV_WROWH       8       /* a row's height in pixels             */
#define WPV_EXTRA       10

#define WPV_PADX        4       /* pixels left and right of the lines   */
#define WPV_PADY        2       /* pixels above them                    */
#define WPV_GAP         6       /* pixels between a label and its value,
                                   and the least width of a value       */
/* A string of the string table that wpv_text takes, its NUL included,
   and the panel's text made from one. */
#define WPV_FMTSZ       128
#define WPV_TEXTSZ      (WPV_FMTSZ + WPV_VALW)

static const char k_class[] = "WPREV";
static const char k_empty[] = "";
static const char k_blank[] = " ";

/* Each key that scrolls and the scroll bar's code it stands for. */
static const u8 scroll_keys[][2] = {
    { VK_UP, SB_LINEUP }, { VK_DOWN, SB_LINEDOWN }, { VK_PRIOR, SB_PAGEUP },
    { VK_NEXT, SB_PAGEDOWN }, { VK_HOME, SB_TOP }, { VK_END, SB_BOTTOM }
};

static cmem_blk *block(HWND w)
{
    return (cmem_blk *)GetWindowWord(w, WPV_WBLK);
}

/* The line of panel w at *off into t (WPV_LINESZ bytes), *off moved past
   it: 1, or 0 at the end of the lines. */
static int next(HWND w, u16 *off, char *t)
{
    u16 used;
    u16 n;

    used = GetWindowWord(w, WPV_WUSED);
    if (*off >= used)
        return 0;
    n = (u16)(used - *off);
    cmem_get(block(w), *off, t, (u16)min(n, WPV_LINESZ));
    n = (u16)(strlen(t) + 1);
    *off = (u16)(*off + n + strlen(t + n) + 1);
    return 1;
}

/* Add the line of label and value to panel w, each cut to what a line
   keeps: 1, or 0 when the block has no room for it. */
static int put(HWND w, const char *label, const char *value)
{
    char t[WPV_LINESZ];
    u16 used;
    u16 n;

    txt_copy(t, label, WPV_LABELW + 1);
    n = (u16)(strlen(t) + 1);
    txt_copy(t + n, value, WPV_VALW + 1);
    n = (u16)(n + strlen(t + n) + 1);
    used = GetWindowWord(w, WPV_WUSED);
    if (n > WPV_BYTES - used)
        return 0;
    cmem_put(block(w), used, t, n);
    SetWindowWord(w, WPV_WUSED, (WORD)(used + n));
    return 1;
}

/* The width panel w's lines are laid out in: its window's inside its
   border less a scroll bar's, shown or not, so that the rows stay as
   the bar comes and goes. */
static int width(HWND w)
{
    RECT r;

    GetWindowRect(w, &r);
    return r.right - r.left - 2 * GetSystemMetrics(SM_CXBORDER) -
           GetSystemMetrics(SM_CXVSCROLL);
}

/* Draw the line t of panel w in dc, a field or a line of the notes, its
   top at y, in lines wide pixels wide down to bottom; or, with show 0,
   only measure it.  Its height.  t is left in the Windows set. */
static int draw(HWND w, HDC dc, char *t, int field, int y, int wide,
                int bottom, int show)
{
    RECT r;
    char *v;
    int h;

    v = t + strlen(t) + 1;
    OemToAnsi(t, t);
    OemToAnsi(v, v);
    SetRect(&r, WPV_PADX, y, wide - WPV_PADX, bottom);
    if (field)
        r.left += (int)GetWindowWord(w, WPV_WLABW) + WPV_GAP;
    r.right = max(r.right, r.left + WPV_GAP);
    h = DrawText(dc, v[0] != '\0' ? v : k_blank, -1, &r,
                 DT_WORDBREAK | DT_NOPREFIX | (show ? 0 : DT_CALCRECT));
    if (show && field) {
        r.right = r.left - WPV_GAP;
        r.left = WPV_PADX;
        DrawText(dc, t, -1, &r, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    }
    return h;
}

/* Measure panel w's lines for its width: the label column, as wide as
   the widest label, a row's height, and the rows that do not fit its
   height, the scroll bar's range, its thumb held within it. */
static void measure(HWND w)
{
    char t[WPV_LINESZ];
    TEXTMETRIC tm;
    HFONT font;
    RECT c;
    HDC dc;
    u16 off;
    u16 nf;
    u16 i;
    int wide;
    int labw;
    int tall;
    int most;
    int x;

    dc = GetDC(w);
    font = (HFONT)SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    GetTextMetrics(dc, &tm);
    nf = GetWindowWord(w, WPV_WNF);
    labw = 0;
    for (off = 0, i = 0; i < nf && next(w, &off, t); i++) {
        OemToAnsi(t, t);
        x = (int)LOWORD(GetTextExtent(dc, t, (int)strlen(t)));
        labw = max(labw, x);
    }
    SetWindowWord(w, WPV_WLABW, (WORD)labw);
    SetWindowWord(w, WPV_WROWH, (WORD)tm.tmHeight);
    wide = width(w);
    tall = WPV_PADY;
    for (off = 0, i = 0; next(w, &off, t); i++)
        tall += draw(w, dc, t, i < nf, 0, wide, 0, 0);
    SelectObject(dc, font);
    ReleaseDC(w, dc);
    GetClientRect(w, &c);
    most = tall > c.bottom ?
           (tall - c.bottom + tm.tmHeight - 1) / tm.tmHeight : 0;
    x = GetScrollPos(w, SB_VERT);
    SetScrollRange(w, SB_VERT, 0, most, FALSE);
    SetScrollPos(w, SB_VERT, min(x, most), TRUE);
}

/* Paint panel w: its lines from the scroll bar's row, or without lines
   its text, and the focus rectangle while it has the focus. */
static void paint(HWND w)
{
    char t[WPV_LINESZ];
    char s[WPV_TEXTSZ];
    PAINTSTRUCT ps;
    HFONT font;
    RECT c;
    RECT r;
    u16 off;
    u16 nf;
    u16 i;
    int wide;
    int y;

    BeginPaint(w, &ps);
    font = (HFONT)SelectObject(ps.hdc, GetStockObject(ANSI_VAR_FONT));
    SetBkMode(ps.hdc, TRANSPARENT);
    SetTextColor(ps.hdc, GetSysColor(COLOR_WINDOWTEXT));
    GetClientRect(w, &c);
    wide = width(w);
    nf = GetWindowWord(w, WPV_WNF);
    if (GetWindowWord(w, WPV_WUSED) == 0) {
        GetWindowText(w, s, (int)sizeof s);
        SetRect(&r, WPV_PADX, WPV_PADY, max(wide - WPV_PADX, WPV_PADX),
                c.bottom);
        DrawText(ps.hdc, s, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }
    y = WPV_PADY -
        GetScrollPos(w, SB_VERT) * (int)GetWindowWord(w, WPV_WROWH);
    for (off = 0, i = 0; y < c.bottom && next(w, &off, t); i++)
        y += draw(w, ps.hdc, t, i < nf, y, wide, c.bottom, 1);
    if (GetFocus() == w) {
        InflateRect(&c, -1, -1);
        DrawFocusRect(ps.hdc, &c);
    }
    SelectObject(ps.hdc, font);
    EndPaint(w, &ps);
}

/* Scroll panel w as the scroll bar's code says, to row thumb for its
   thumb's. */
static void scroll(HWND w, UINT code, int thumb)
{
    RECT c;
    int most;
    int page;
    int pos;
    int to;

    GetScrollRange(w, SB_VERT, &to, &most);
    GetClientRect(w, &c);
    page = max(c.bottom / (int)GetWindowWord(w, WPV_WROWH) - 1, 1);
    pos = GetScrollPos(w, SB_VERT);
    switch (code) {
    case SB_LINEUP:
        to = pos - 1;
        break;
    case SB_LINEDOWN:
        to = pos + 1;
        break;
    case SB_PAGEUP:
        to = pos - page;
        break;
    case SB_PAGEDOWN:
        to = pos + page;
        break;
    case SB_TOP:
        to = 0;
        break;
    case SB_BOTTOM:
        to = most;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        to = thumb;
        break;
    default:
        to = pos;
        break;
    }
    to = max(0, min(to, most));
    if (to != pos) {
        SetScrollPos(w, SB_VERT, to, TRUE);
        InvalidateRect(w, NULL, TRUE);
    }
}

/* The key vk typed in panel w: one of scroll_keys scrolls, SHIFT and
   CTRL alone do nothing, and any other is its owner's (WPVN_KEY). */
static void key(HWND w, UINT vk)
{
    int i;

    for (i = 0; i < (int)(sizeof scroll_keys / sizeof scroll_keys[0]); i++)
        if (scroll_keys[i][0] == vk) {
            scroll(w, scroll_keys[i][1], 0);
            return;
        }
    if (vk != VK_SHIFT && vk != VK_CONTROL)
        SendMessage(GetParent(w), WM_COMMAND, GetWindowWord(w, GWW_ID),
                    MAKELPARAM(vk, WPVN_KEY));
}

/* WPVM_LINE: line i of panel w into out. */
static LRESULT line_at(HWND w, u16 i, LPSTR out)
{
    char t[WPV_LINESZ];
    u16 off;
    u16 k;

    for (off = 0, k = 0; next(w, &off, t); k++)
        if (k == i) {
            lstrcpy(out, t);
            lstrcpy(out + strlen(t) + 1, t + strlen(t) + 1);
            return i < GetWindowWord(w, WPV_WNF) ? WPV_FIELD : WPV_NOTE;
        }
    return 0;
}

LRESULT WCALLBACK wpv_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    cmem_blk *b;

    switch (wm) {
    case WM_CREATE:
        b = cmem_alloc(WPV_BYTES);
        if (b == NULL)
            return -1;
        SetWindowWord(w, WPV_WBLK, (WORD)b);
        measure(w);
        return 0;
    case WM_DESTROY:
        cmem_free(block(w));
        return 0;
    case WM_PAINT:
        paint(w);
        return 0;
    case WM_SIZE:
        measure(w);
        return 0;
    case WM_VSCROLL:
        scroll(w, (UINT)wp, (int)LOWORD(lp));
        return 0;
    case WM_KEYDOWN:
        key(w, (UINT)wp);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(w);
        if (wm == WM_LBUTTONDBLCLK)
            key(w, VK_RETURN);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(w, NULL, TRUE);
        return 0;
    case WPVM_LINE:
        return line_at(w, (u16)wp, (LPSTR)lp);
    default:
        return DefWindowProc(w, wm, wp, lp);
    }
}

HWND wpv_create(HWND owner, int id)
{
    WNDCLASS wc;
    HINSTANCE inst;

    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    if (!GetClassInfo(inst, k_class, &wc)) {
        memset(&wc, 0, sizeof wc);
        wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wpv_proc;
        wc.cbWndExtra = WPV_EXTRA;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = k_class;
        RegisterClass(&wc);
    }
    return CreateWindow(k_class, NULL, WS_CHILD | WS_VISIBLE | WS_BORDER |
                        WS_VSCROLL, 0, 0, 0, 0, owner, (HMENU)id, inst, NULL);
}

void wpv_text(HWND w, UINT ids, const char *arg)
{
    char fmt[WPV_FMTSZ];
    char a[WPV_VALW + 1];
    char t[WPV_TEXTSZ];

    a[0] = '\0';
    if (arg != NULL) {
        txt_copy(a, arg, (u16)sizeof a);
        OemToAnsi(a, a);
    }
    t[0] = '\0';
    if (ids != 0 && LoadString((HINSTANCE)GetWindowWord(w, GWW_HINSTANCE),
                               ids, fmt, sizeof fmt) != 0)
        wsprintf(t, fmt, (LPCSTR)a);
    SetWindowWord(w, WPV_WUSED, 0);
    SetWindowWord(w, WPV_WNF, 0);
    SetWindowText(w, t);
    measure(w);
    InvalidateRect(w, NULL, TRUE);
    UpdateWindow(w);
}

int wpv_add(HWND w, const char *label, const char *value)
{
    if (!put(w, label, value))
        return 0;
    SetWindowWord(w, WPV_WNF, (WORD)(GetWindowWord(w, WPV_WNF) + 1));
    return 1;
}

int wpv_note(HWND w, const char *line)
{
    return put(w, k_empty, line);
}

void wpv_done(HWND w)
{
    SetWindowText(w, k_empty);
    measure(w);
    InvalidateRect(w, NULL, TRUE);
}

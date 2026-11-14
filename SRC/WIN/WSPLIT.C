/*
 * WSPLIT.C - the splitter between two panes of INVWIN's frame
 * (WSPLIT.H).
 *
 * The splitter keeps in its window words its limits, the guide's left
 * edge, where the pointer took it and the window that had the focus;
 * it drags while it holds the mouse (GetCapture), so no near memory
 * outlives a message.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "WSPLIT.H"

#define WSP_WLO         0       /* window word: the lowest left edge    */
#define WSP_WHI         2       /* the highest                          */
#define WSP_WX          4       /* the guide's left edge                */
#define WSP_WGRAB       6       /* the pointer's x within the splitter  */
#define WSP_WFOCUS      8       /* the window that had the focus        */
#define WSP_EXTRA       10

static const char k_class[] = "WSPLIT";

/* Splitter w's place in its parent's client coordinates, into *r. */
static void place_of(HWND w, RECT *r)
{
    GetWindowRect(w, r);
    ScreenToClient(GetParent(w), (POINT FAR *)&r->left);
    ScreenToClient(GetParent(w), (POINT FAR *)&r->right);
}

/* Invert the guide of splitter w at left edge x in its parent, on the
   screen itself, so over the windows there. */
static void guide(HWND w, int x)
{
    POINT p;
    RECT r;
    HDC dc;

    place_of(w, &r);
    p.x = x;
    p.y = r.top;
    ClientToScreen(GetParent(w), &p);
    dc = GetDC(NULL);
    PatBlt(dc, p.x, p.y, WSP_WIDTH, r.bottom - r.top, DSTINVERT);
    ReleaseDC(NULL, dc);
}

/* Paint splitter w: a frame line down each long edge, the face between. */
static void paint(HWND w)
{
    PAINTSTRUCT ps;
    HBRUSH face;
    HBRUSH line;
    RECT r;

    BeginPaint(w, &ps);
    face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    line = CreateSolidBrush(GetSysColor(COLOR_WINDOWFRAME));
    if (face != NULL && line != NULL) {
        GetClientRect(w, &r);
        FillRect(ps.hdc, &r, face);
        r.right = 1;
        FillRect(ps.hdc, &r, line);
        GetClientRect(w, &r);
        r.left = r.right - 1;
        FillRect(ps.hdc, &r, line);
    }
    if (line != NULL)
        DeleteObject(line);
    if (face != NULL)
        DeleteObject(face);
    EndPaint(w, &ps);
}

/* End splitter w's drag: the guide erased, the mouse released and the
   focus given back; then, with moved, the splitter at the guide's place
   and its parent told, when that is another. */
static void drop(HWND w, int moved)
{
    RECT r;
    HWND f;
    int x;

    if (GetCapture() != w)
        return;
    x = (int)GetWindowWord(w, WSP_WX);
    guide(w, x);
    ReleaseCapture();
    f = (HWND)GetWindowWord(w, WSP_WFOCUS);
    if (f != NULL && IsWindow(f))
        SetFocus(f);
    place_of(w, &r);
    if (!moved || x == r.left)
        return;
    MoveWindow(w, x, r.top, WSP_WIDTH, r.bottom - r.top, TRUE);
    SendMessage(GetParent(w), WM_COMMAND, GetWindowWord(w, GWW_ID),
                MAKELPARAM(w, WSPN_MOVED));
}

LRESULT WCALLBACK wsp_proc(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    RECT r;
    int x;

    switch (wm) {
    case WM_PAINT:
        paint(w);
        return 0;
    case WSPM_LIMITS:
        SetWindowWord(w, WSP_WLO, (WORD)wp);
        SetWindowWord(w, WSP_WHI, LOWORD(lp));
        return 0;
    case WSPM_GETLIMITS:
        return MAKELRESULT(GetWindowWord(w, WSP_WLO),
                           GetWindowWord(w, WSP_WHI));
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (GetCapture() == w || !IsWindowEnabled(w))
            return 0;
        place_of(w, &r);
        SetWindowWord(w, WSP_WX, (WORD)r.left);
        SetWindowWord(w, WSP_WGRAB, LOWORD(lp));
        SetWindowWord(w, WSP_WFOCUS, (WORD)GetFocus());
        SetCapture(w);
        SetFocus(w);
        guide(w, r.left);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() != w)
            return 0;
        place_of(w, &r);
        x = r.left + (int)LOWORD(lp) - (int)GetWindowWord(w, WSP_WGRAB);
        x = max(x, (int)GetWindowWord(w, WSP_WLO));
        x = min(x, (int)GetWindowWord(w, WSP_WHI));
        if (x != (int)GetWindowWord(w, WSP_WX)) {
            guide(w, (int)GetWindowWord(w, WSP_WX));
            SetWindowWord(w, WSP_WX, (WORD)x);
            guide(w, x);
        }
        return 0;
    case WM_LBUTTONUP:
        drop(w, 1);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            drop(w, 0);
        return 0;
    case WM_CANCELMODE:
        drop(w, 0);
        return 0;
    default:
        return DefWindowProc(w, wm, wp, lp);
    }
}

HWND wsp_create(HWND owner, int id)
{
    WNDCLASS wc;
    HINSTANCE inst;
    RECT r;
    HWND w;

    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    if (!GetClassInfo(inst, k_class, &wc)) {
        memset(&wc, 0, sizeof wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = wsp_proc;
        wc.cbWndExtra = WSP_EXTRA;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_SIZEWE);
        wc.lpszClassName = k_class;
        RegisterClass(&wc);
    }
    w = CreateWindow(k_class, NULL, WS_CHILD | WS_VISIBLE, 0, 0, WSP_WIDTH,
                     0, owner, (HMENU)id, inst, NULL);
    if (w != NULL) {
        GetClientRect(owner, &r);
        SetWindowWord(w, WSP_WHI, (WORD)max(r.right - WSP_WIDTH, 0));
    }
    return w;
}

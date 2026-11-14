/*
 * WSMOKE.C - the Windows smoke test: a Win16 program that registers a
 * window class, makes a window, reports the Windows it runs on and ends
 * the Windows session.
 *
 * usage: WIN WSMOKE   (from the directory the report is to be written in)
 *
 * It writes WSMOKE.OUT in its current directory, lines ending CR LF:
 *
 *   WSMOKE WINDOWS 3.00   the Windows version of GetVersion, major.minor
 *   WSMOKE DOS 3.10       the DOS version of GetVersion
 *   WSMOKE MODE STANDARD  REAL, STANDARD or ENHANCED, from GetWinFlags
 *   WSMOKE FREE 412160    the bytes free in the global heap, GetFreeSpace
 *   WSMOKE PASS           or WSMOKE FAIL
 *
 * PASS says that the title came from the string table bound in by the
 * resource compiler, the class was registered (by the first instance
 * only, as Windows 3.0 keeps one class per module) and the window made,
 * and that Windows called the window procedure for WM_CREATE: the flag it
 * sets lives in the program's data segment, which a wrong DS on entry
 * would miss.  It then calls ExitWindows, so that WIN.COM returns to DOS;
 * should another program refuse to end, it destroys its window and ends
 * alone.
 *
 * The program is real-mode safe: its window procedure is WCALLBACK
 * (CWIN.H), exported with the entry code that loads DS from the AX that
 * Windows sets, and reached through its class, for which Windows makes
 * the instance thunk itself, so no MakeProcInstance is needed.  It keeps
 * no pointer into movable memory, and formats with wsprintf of USER, as
 * the C library's sprintf imports a function Windows 3.0 lacks.
 */
#include "COMPAT.H"
#include "CWIN.H"
#include "WSMOKE.H"

/* The longest title the string table may give, its NUL included. */
#define WSM_TITLELEN    64
/* The report's size: its five lines at their longest. */
#define WSM_REPORTLEN   128

static const char k_class[] = "WSMOKE";
static const char k_outname[] = "WSMOKE.OUT";
static const char k_report[] = "WSMOKE WINDOWS %u.%02u\r\n"
                               "WSMOKE DOS %u.%02u\r\n"
                               "WSMOKE MODE %s\r\n"
                               "WSMOKE FREE %lu\r\n"
                               "WSMOKE %s\r\n";

/* Set by the window procedure when Windows sends it WM_CREATE. */
static int wsm_created;

LRESULT WCALLBACK wsm_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE)
        wsm_created = 1;
    return DefWindowProc(wnd, msg, wp, lp);
}

/* The name of the mode Windows runs in, from GetWinFlags. */
static const char *mode_name(u32 flags)
{
    if (!(flags & WF_PMODE))
        return "REAL";
    return (flags & WF_ENHANCED) ? "ENHANCED" : "STANDARD";
}

/* Write the report, ending in PASS when ok is not zero. */
static void report(int ok)
{
    char text[WSM_REPORTLEN];
    u32 ver;
    HFILE f;
    int n;

    ver = GetVersion();
    n = wsprintf(text, k_report,
                 LOBYTE(LOWORD(ver)), HIBYTE(LOWORD(ver)),
                 HIBYTE(HIWORD(ver)), LOBYTE(HIWORD(ver)),
                 (LPCSTR)mode_name(GetWinFlags()), GetFreeSpace(0),
                 (LPCSTR)(ok ? "PASS" : "FAIL"));
    f = _lcreat(k_outname, 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, text, n);
        _lclose(f);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char title[WSM_TITLELEN];
    WNDCLASS wc;
    HWND wnd;
    int ok;

    (void)cmd;
    ok = LoadString(inst, WSM_S_TITLE, title, sizeof title) > 0;
    if (ok && prev == NULL) {
        wc.style = 0;
        wc.lpfnWndProc = wsm_wndproc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = k_class;
        ok = RegisterClass(&wc) != 0;
    }
    wnd = NULL;
    if (ok)
        wnd = CreateWindow(k_class, title, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                           NULL, NULL, inst, NULL);
    if (wnd != NULL) {
        ShowWindow(wnd, show);
        UpdateWindow(wnd);
    }
    report(wnd != NULL && wsm_created);
    ExitWindows(0L, 0);
    if (wnd != NULL)
        DestroyWindow(wnd);
    return 0;
}

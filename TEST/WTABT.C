/*
 * WTABT.C - the test of the tab strip (SRC/WIN/WTAB.C): a modal dialog
 * holding a strip, which a key script switches by keys and clicks and
 * snaps.
 *
 * usage: WIN WTABT /KEYS:WTABT.KEY /SNAP:WTABT.SNP
 *
 * The program opens its dialog (TEST/WTABT.RC) over a frame as it
 * starts, and ends when the dialog ends.  The dialog holds the strip,
 * made as it opens from string WTT_S_TABS in ANSI_VAR_FONT and put
 * first in the tab order, just before the edit control, which gets the
 * focus; a static, "SEEN n CUR c", n the WTN_CHANGE notifications the
 * dialog has had and c the current tab (WTM_GETCUR) at the last of
 * them; and OK and Cancel, which end it.  A message filter
 * (WH_MSGFILTER) hands every key message of the dialog to wt_key first,
 * as WTAB.H asks, then takes these keys of its own:
 *
 *   F2   a click on tab 2, the mouse's down and up posted to the strip
 *        at the middle of the tab's rectangle (WTM_GETRECT); with
 *        SHIFT, on the current tab; with CTRL, at the strip's right
 *        end, beyond the last tab
 *   F3   WTM_SETCUR 3; with SHIFT, WTM_SETCUR, then WTM_GETRECT, on
 *        WTT_NOTAB, the first tab past the four the strip opens with:
 *        the static's c is then the first answer, -1 for none, and " RECT
 *        g" after it says the second, 0 for none
 *   F4   the tabs made k_two by SetWindowText
 *   F5   the tabs made k_many, more tabs than WT_MAXTABS and more bytes
 *        than WT_MAXTEXT, its first WT_MAXTABS tabs within those bytes
 *
 * TEST/WTABT.KEY types into it; every mode of Windows must give
 * TEST/GOLDEN/WIN/WTABT.SNP byte for byte, which TEST/tabchk.py checks.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "TDRV.H"
#include "WTAB.H"
#include "WTABT.H"

#define WTT_CMDMAX      128     /* a DOS command tail and its NUL       */
#define WTT_SEENSZ      32      /* the static's text and its NUL        */
#define WTT_MARGIN      4       /* dialog units about the strip         */
#define WTT_NOTAB       4       /* the tab SHIFT-F3 asks for            */
#define WTT_NORECT      (-1)    /* no WTM_GETRECT answer to say         */
#define WTT_WM_OPEN     (WM_USER + 0)   /* the frame opens the dialog   */

static const char k_class[] = "WTABT";
static const char k_keys[] = "/KEYS:";
static const char k_snap[] = "/SNAP:";
static const char k_two[] = "One|Two";
static const char k_many[] =
    "Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine|Ten|Eleven|Twelve";

static HINSTANCE g_inst;
static HWND g_dlg;              /* the dialog while it is open          */
static HHOOK g_next;            /* the message filter before ours       */
static int g_seen;              /* the notifications so far             */

/* Say n and c in the dialog's static, then " RECT g" unless g is
   WTT_NORECT. */
static void seen(int c, int g)
{
    char t[WTT_SEENSZ];
    int n;

    n = wsprintf(t, "SEEN %d CUR %d", g_seen, c);
    if (g != WTT_NORECT)
        wsprintf(t + n, " RECT %d", g);
    SetDlgItemText(g_dlg, WTT_SEEN, t);
}

/* Post the strip a click on tab i, or at its right end when i is -1. */
static void click(HWND strip, int i)
{
    RECT r;
    LPARAM at;

    if (i < 0) {
        GetClientRect(strip, &r);
        at = MAKELPARAM(r.right - 1, r.bottom / 2);
    } else {
        SetRectEmpty(&r);
        SendMessage(strip, WTM_GETRECT, (WPARAM)i, (LPARAM)(RECT FAR *)&r);
        at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }
    PostMessage(strip, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessage(strip, WM_LBUTTONUP, 0, at);
}

/* The dialog's own keys, as this file's header gives them; 1 when vk is
   one. */
static int key(UINT vk)
{
    HWND strip;
    RECT r;
    int shift;
    int c;

    strip = GetDlgItem(g_dlg, WTT_STRIP);
    shift = GetKeyState(VK_SHIFT) < 0;
    switch (vk) {
    case VK_F2:
        click(strip, shift ? (int)SendMessage(strip, WTM_GETCUR, 0, 0L) :
                     GetKeyState(VK_CONTROL) < 0 ? -1 : 2);
        return 1;
    case VK_F3:
        if (!shift) {
            seen((int)SendMessage(strip, WTM_SETCUR, 3, 0L), WTT_NORECT);
            return 1;
        }
        c = (int)SendMessage(strip, WTM_SETCUR, WTT_NOTAB, 0L);
        SetRectEmpty(&r);
        seen(c, (int)SendMessage(strip, WTM_GETRECT, WTT_NOTAB,
                                 (LPARAM)(RECT FAR *)&r));
        return 1;
    case VK_F4:
    case VK_F5:
        SetWindowText(strip, vk == VK_F4 ? k_two : k_many);
        return 1;
    default:
        return 0;
    }
}

/* The message filter: a key message of the dialog to wt_key, then to
   key. */
LRESULT WCALLBACK wtt_filter(int code, WPARAM wp, LPARAM lp)
{
    const MSG FAR *m;

    m = (const MSG FAR *)lp;
    if (code == MSGF_DIALOGBOX && g_dlg != NULL &&
        (m->hwnd == g_dlg || IsChild(g_dlg, m->hwnd)) &&
        (wt_key(GetDlgItem(g_dlg, WTT_STRIP), m->message, m->wParam) ||
         (m->message == WM_KEYDOWN && key(m->wParam))))
        return 1;
    return DefHookProc(code, wp, lp, &g_next);
}

BOOL WCALLBACK wtt_dlg(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    RECT m;
    RECT r;
    HWND strip;

    switch (wm) {
    case WM_INITDIALOG:
        g_dlg = dlg;
        SetRect(&m, WTT_MARGIN, WTT_MARGIN, 0, 0);
        MapDialogRect(dlg, &m);
        GetClientRect(dlg, &r);
        SetRect(&r, m.left, m.top, r.right - m.left, 0);
        strip = wt_create(dlg, &r, WTT_STRIP, WTT_S_TABS, NULL);
        if (strip != NULL)
            SetWindowPos(strip, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetFocus(GetDlgItem(dlg, WTT_EDIT));
        return FALSE;
    case WM_COMMAND:
        if (wp == WTT_STRIP && HIWORD(lp) == WTN_CHANGE) {
            g_seen++;
            seen((int)SendMessage((HWND)LOWORD(lp), WTM_GETCUR, 0, 0L),
                 WTT_NORECT);
        } else if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(dlg, (int)wp);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

/* Open the dialog over the frame w, the message filter set meanwhile,
   then end the program. */
static void run(HWND w)
{
    FARPROC filter;
    FARPROC proc;

    filter = MakeProcInstance((FARPROC)wtt_filter, g_inst);
    proc = MakeProcInstance((FARPROC)wtt_dlg, g_inst);
    if (filter != NULL && proc != NULL) {
        g_next = SetWindowsHook(WH_MSGFILTER, (HOOKPROC)filter);
        DialogBox(g_inst, MAKEINTRESOURCE(WTT_D_MAIN), w, (DLGPROC)proc);
        UnhookWindowsHook(WH_MSGFILTER, (HOOKPROC)filter);
    }
    g_dlg = NULL;
    if (proc != NULL)
        FreeProcInstance(proc);
    if (filter != NULL)
        FreeProcInstance(filter);
    DestroyWindow(w);
}

LRESULT WCALLBACK wtt_frame(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    switch (wm) {
    case WTT_WM_OPEN:
        run(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(w, wm, wp, lp);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char words[WTT_CMDMAX];
    const char *keys;
    const char *snap;
    WNDCLASS wc;
    HWND w;
    char *p;
    MSG m;

    (void)prev;
    for (p = words; *cmd != '\0' && p < words + sizeof words - 1; )
        *p++ = *cmd++;
    *p = '\0';
    keys = NULL;
    snap = NULL;
    for (p = words; *p != '\0'; ) {
        while (*p == ' ')
            *p++ = '\0';
        if (txt_ieqn(p, k_keys, sizeof k_keys - 1))
            keys = p + sizeof k_keys - 1;
        else if (txt_ieqn(p, k_snap, sizeof k_snap - 1))
            snap = p + sizeof k_snap - 1;
        while (*p != '\0' && *p != ' ')
            p++;
    }
    td_init(inst, keys, snap);
    g_inst = inst;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wtt_frame;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = k_class;
    m.wParam = 0;
    w = RegisterClass(&wc) == 0 ? NULL :
        CreateWindow(k_class, k_class, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                     CW_USEDEFAULT, 0, NULL, NULL, inst, NULL);
    if (w != NULL) {
        ShowWindow(w, show);
        UpdateWindow(w);
        PostMessage(w, WTT_WM_OPEN, 0, 0L);
        while (GetMessage(&m, NULL, 0, 0)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    td_done();
    return (int)m.wParam;
}

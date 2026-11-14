/*
 * WHELP.C - INVWIN's help (WHELP.H).
 *
 * WH_HLP is looked for at each request, so that help found missing at
 * one shows in Windows Help once the file is there; its path is built
 * on the stack and given to WinHelp in the ANSI set.  HELP_QUIT names
 * the file alone: Windows Help ends for the window, whatever the path.
 * A field's section is looked for at each request too, read from
 * WH_TEXT, as WH_HLP holds the same topics but cannot be asked whether
 * it has one.
 *
 * The help window's section is a far block of msg_section, wh_blk,
 * held while the window is open and given to its control line by line
 * through the text-file reader of MSG.H, each line appended with
 * EM_REPLACESEL, so that the text goes to the control's own memory,
 * outside DGROUP, with no near buffer of its size.  One help window is
 * open at a time; F1 over another window while it is open does nothing.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "MSG.H"
#include "TUI.H"
#include "WDLG.H"
#include "WEDIT.H"
#include "WFORM.H"
#include "WSCR.H"
#include "WHELP.H"
#include "INVWIN.H"

#define WH_ROWS         20      /* lines the help window shows          */
#define WH_RDBUF        128     /* the line reader's buffer             */

static const char k_empty[] = "";

static HINSTANCE wh_inst;
static HWND wh_frame;
static const char *wh_dir;
static const char *wh_cur = WH_HELPKEYS;
static FARPROC wh_thunk;        /* the message filter's, or NULL        */
static HHOOK wh_next;           /* the filter before it                 */
static HWND wh_dlg;             /* the help window, NULL when closed    */
static cmem_blk *wh_blk;        /* the section it shows                 */
static u16 wh_n;                /* its bytes                            */
static u8 wh_used;              /* 1 once Windows Help has shown help   */

/* The context number of topic (WH_CTXMUL). */
static u32 ctx(const char *topic)
{
    u32 h;

    h = 0;
    while (*topic != '\0')
        h = h * WH_CTXMUL + (u8)*topic++;
    return h;
}

/* The topic of the field with the focus in the open form dialog (WHELP.H)
   in t (MSG_IDXLINE bytes), or NULL when there is none or WH_TEXT has
   no section of it. */
static const char *field(char *t)
{
    const char *form;
    const char *name;
    cmem_blk *b;
    u16 n;
    u16 k;

    form = wf_topic();
    name = wf_field();
    if (form == NULL || name == NULL)
        return NULL;
    k = (u16)strlen(form);
    if (k + 1 + strlen(name) >= MSG_IDXLINE)
        return NULL;
    memcpy(t, form, k);
    t[k] = '_';
    strcpy(t + k + 1, name);
    if (msg_section(wh_dir, WH_TEXT, t, &b, &n) != 0)
        return NULL;
    cmem_free(b);
    return t;
}

/* Put the full path of WH_HLP in the program's directory into p
   (DOS_PATHMAX bytes) in the ANSI set; 0 when it is not there.  Windows
   Help would take a name without a directory as one in its own. */
static int hlp_path(char *p)
{
    if (strlen(wh_dir) + sizeof WH_HLP > DOS_PATHMAX)
        return 0;
    strcpy(p, wh_dir);
    strcat(p, WH_HLP);
    if (cf_exists(p) != 1)
        return 0;
    OemToAnsi(p, p);
    return 1;
}

/* Read section topic into a new far block *b of *n bytes: 0, or as
   msg_section does, the failure said over owner. */
static int load(HWND owner, const char *topic, cmem_blk **b, u16 *n)
{
    int rc;

    rc = msg_section(wh_dir, WH_TEXT, topic, b, n);
    if (rc != 0)
        ws_say_oem(owner, MB_ICONEXCLAMATION,
                   rc == CF_ENOMEM ? msg(MT_NOMEM)
                                   : msg_sub(MT_NOHELP, topic, NULL));
    return rc;
}

/* Put the lines of wh_blk into the control e, CR LF between them, each
   cut to WH_HELPW characters, and the caret at the start. */
static void fill(HWND e)
{
    msg_rd r;
    char rbuf[WH_RDBUF];
    char t[2 + WH_HELPW + 1];
    const char *p;
    u16 len;

    SetWindowText(e, k_empty);
    msg_rd_blk(&r, wh_blk, 0, wh_n, rbuf, (u16)sizeof rbuf);
    t[0] = '\r';
    t[1] = '\n';
    p = t + 2;
    while (msg_rd_line(&r, t + 2, WH_HELPW, &len) > 0) {
        SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)p);
        p = t;
    }
    SendMessage(e, EM_SETSEL, 0, MAKELONG(0, 0));
}

/* Size the control e to WH_ROWS lines of WH_HELPW characters of
   OEM_FIXED_FONT, one more for the margins inside its border, with its
   scroll bar, and the help window dlg to hold it alone. */
static void fit(HWND dlg, HWND e)
{
    TEXTMETRIC tm;
    RECT w;
    RECT c;
    HDC dc;
    int cx;
    int cy;

    dc = GetDC(e);
    SelectObject(dc, GetStockObject(OEM_FIXED_FONT));
    GetTextMetrics(dc, &tm);
    ReleaseDC(e, dc);
    cx = (WH_HELPW + 1) * tm.tmAveCharWidth +
         2 * GetSystemMetrics(SM_CXBORDER) + GetSystemMetrics(SM_CXVSCROLL);
    cy = WH_ROWS * tm.tmHeight + 2 * GetSystemMetrics(SM_CYBORDER);
    MoveWindow(e, 0, 0, cx, cy, FALSE);
    GetWindowRect(dlg, &w);
    GetClientRect(dlg, &c);
    cx += w.right - w.left - c.right;
    cy += w.bottom - w.top - c.bottom;
    SetWindowPos(dlg, NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER);
}

/* The help window: its control filled from wh_blk as INITDIALOG, the
   focus on it with nothing selected; IDOK and IDCANCEL end it. */
BOOL WCALLBACK wh_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    HWND e;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    e = GetDlgItem(dlg, IW_C_HELP);
    switch (wm) {
    case WM_INITDIALOG:
        wh_dlg = dlg;
        SendMessage(e, WM_SETFONT, (WPARAM)GetStockObject(OEM_FIXED_FONT),
                    FALSE);
        if (!we_attach(e, 1))
            EnableWindow(e, FALSE);
        fit(dlg, e);
        wd_centre(dlg);
        fill(e);
        SetFocus(e);
        return FALSE;
    case WM_COMMAND:
        if (wp != IDOK && wp != IDCANCEL)
            return FALSE;
        EndDialog(dlg, 0);
        return TRUE;
    case WM_DESTROY:
        we_detach(e);
        wh_dlg = NULL;
        return FALSE;
    default:
        return FALSE;
    }
}

/* The message filter: F1 in a dialog box (wh_key), and
   ESC and ENTER in the help window's control, which, multi-line, takes
   every key from the dialog manager, ending the window. */
LRESULT WCALLBACK wh_filter(int code, WPARAM wp, LPARAM lp)
{
    MSG m;

    if (code != MSGF_DIALOGBOX)
        return DefHookProc(code, wp, lp, &wh_next);
    m = *(MSG FAR *)lp;
    if (wh_key(&m))
        return 1;
    if (wh_dlg == NULL || m.hwnd != GetDlgItem(wh_dlg, IW_C_HELP) ||
        m.message != WM_KEYDOWN ||
        (m.wParam != VK_ESCAPE && m.wParam != VK_RETURN))
        return DefHookProc(code, wp, lp, &wh_next);
    EndDialog(wh_dlg, 0);
    return 1;
}

int wh_init(HINSTANCE inst, HWND frame, const char *dir)
{
    wh_inst = inst;
    wh_frame = frame;
    wh_dir = dir;
    wh_thunk = MakeProcInstance((FARPROC)wh_filter, inst);
    if (wh_thunk == NULL)
        return 0;
    wh_next = SetWindowsHook(WH_MSGFILTER, (HOOKPROC)wh_thunk);
    return 1;
}

void wh_done(void)
{
    if (wh_used)
        WinHelp(wh_frame, WH_HLP, HELP_QUIT, 0L);
    wh_used = 0;
    if (wh_thunk == NULL)
        return;
    UnhookWindowsHook(WH_MSGFILTER, (HOOKPROC)wh_thunk);
    FreeProcInstance(wh_thunk);
    wh_thunk = NULL;
}

const char *wh_topic(const char *topic)
{
    const char *old;

    old = wh_cur;
    if (topic != NULL)
        wh_cur = topic;
    return old;
}

int wh_key(const MSG *m)
{
    cmem_blk *b;
    u16 n;

    if (m->message != WM_KEYDOWN || m->wParam != VK_F1 ||
        GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_CONTROL) < 0 ||
        GetKeyState(VK_MENU) < 0)
        return 0;
    if (wh_dlg == NULL) {
        wh_show(GetActiveWindow(), NULL);
    } else if (GetActiveWindow() == wh_dlg &&
               load(wh_dlg, WH_HELPKEYS, &b, &n) == 0) {
        cmem_free(wh_blk);
        wh_blk = b;
        wh_n = n;
        fill(GetDlgItem(wh_dlg, IW_C_HELP));
    }
    return 1;
}

void wh_show(HWND owner, const char *topic)
{
    char p[DOS_PATHMAX];
    char t[MSG_IDXLINE];
    FARPROC proc;
    HWND cap;
    int r;

    cap = GetCapture();
    if (cap != NULL)
        SendMessage(cap, WM_CANCELMODE, 0, 0L);
    if (topic == NULL)
        topic = field(t);
    if (topic == NULL)
        topic = wf_topic();
    if (topic == NULL)
        topic = wh_cur;
    if (wh_dlg != NULL)
        return;
    if (hlp_path(p) && WinHelp(wh_frame, p, HELP_CONTEXT, ctx(topic))) {
        wh_used = 1;
        return;
    }
    if (load(owner, topic, &wh_blk, &wh_n) != 0)
        return;
    proc = MakeProcInstance((FARPROC)wh_proc, wh_inst);
    r = proc != NULL ? DialogBoxParam(wh_inst, MAKEINTRESOURCE(IW_R_HELP),
                                      owner, (DLGPROC)proc, 0L) : -1;
    if (proc != NULL)
        FreeProcInstance(proc);
    cmem_free(wh_blk);
    wh_blk = NULL;
    if (r < 0)
        ws_say_oem(owner, MB_ICONEXCLAMATION, msg(MT_NOMEM));
}

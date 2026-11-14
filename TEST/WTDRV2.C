/*
 * WTDRV2.C - a second proof of the Windows test driver, SRC/WIN/TDRV.C:
 * one window with the controls its snapshots show and the commands its
 * scripts drive.
 *
 * usage: WIN WTDRV2 [/HOG] [/MOUSE] [/BLANK] /KEYS:file /SNAP:file
 *
 * The window holds a label with an OEM character, a single-selection and
 * an extended-selection list box, a drop-down combo box, a multi-line and
 * a password edit control, a disabled, a hidden and a default push
 * button, a check box and a status line.  Its menu and accelerators move
 * the focus between the lists and edit controls (a control is never
 * reached by TAB: the window is no dialog), ask a question in a message
 * box, hold the script with td_busy for a while, which an Esc stops, as
 * one stops a long request, and which asks for the keys of the script's
 * A lines as it starts (td_ahead), toggle a menu check and end the
 * program;
 * Ctrl+B holds the script as Busy does, Ctrl+W holds it for good, Ctrl+H
 * hides the window and Ctrl+Q ends the program, to make the driver's
 * failures.
 * /HOG takes every timer Windows has before the driver asks for one.
 * /BLANK gives the extended-selection list box a second row without
 * text, of which the screen shows nothing, for the driver's C line.
 *
 * /MOUSE adds three pads for the driver's M lines: A, of a class with
 * CS_DBLCLKS, holding C, and B, both of a class without.  A pad writes
 * each mouse message of the script in its window text (report): a left
 * press or double click there takes the mouse, a left release lets it
 * go, and a right press or double click gives it to the main window,
 * which then reports in the status line and lets it go on the right
 * release.  A move at the screen's pointer is Windows' own, not the
 * script's (pointer_move): neither a pad nor the status line reports it.
 */
#include "COMPAT.H"
#include "CWIN.H"
#include "TDRV.H"
#include "WTDRV2.H"

#define WT2_CMDLEN      128     /* bytes of the command line kept       */
#define WT2_TIMER       1       /* ID of the busy timer                 */
#define WT2_BUSYMS      1500    /* ms the Busy command holds the script */
#define WT2_MAXTIMERS   64       /* timers /HOG tries to take            */
#define WT2_LONGROW     (TD_MAXTEXT + 2)        /* a row too long to show */
#define WT2_REPLEN      48      /* bytes of a report, NUL included      */

static const char k_class[] = "WTDRV2";
static const char k_status[] = "idle";
static const char k_pad[] = "WT2PAD";
static const char k_padn[] = "WT2PADN";

static const char *const w2_list[] = {
    "Alpha", "Beta", "Gamma", "R\351sum\351"
};
static const char *const w2_marks[] = { "Aa", "Bb", "Cc", "Dd" };
static const char *const w2_combo[] = { "One", "Two", "Three" };

static HINSTANCE w2_inst;
static char w2_cmd[WT2_CMDLEN];
static const char *w2_keys;     /* the /KEYS: file, or NULL     */
static const char *w2_snap;     /* the /SNAP: file, or NULL     */
static int w2_hog;              /* /HOG given                   */
static int w2_checked;          /* the Check item is marked     */
static int w2_mouse;            /* /MOUSE given                 */
static int w2_blank;            /* /BLANK given                 */
static HWND w2_main;            /* the main window              */
static int w2_busy;             /* Busy holds the script        */
static UINT w2_mice;            /* the main window's reports    */

/* Split the command line into its switches. */
static void parse(LPCSTR cmd)
{
    char *p, *q;
    int i;

    for (i = 0; i < WT2_CMDLEN - 1 && cmd[i] != '\0'; i++)
        w2_cmd[i] = cmd[i];
    w2_cmd[i] = '\0';
    p = w2_cmd;
    while (*p != '\0') {
        for (q = p; *q != '\0' && *q != ' ' && *q != '\t'; q++)
            ;
        if (*q != '\0')
            *q++ = '\0';
        if (txt_ieqn(p, "/KEYS:", 6))
            w2_keys = p + 6;
        else if (txt_ieqn(p, "/SNAP:", 6))
            w2_snap = p + 6;
        else if (txt_ieq(p, "/HOG"))
            w2_hog = 1;
        else if (txt_ieq(p, "/MOUSE"))
            w2_mouse = 1;
        else if (txt_ieq(p, "/BLANK"))
            w2_blank = 1;
        p = q;
        while (*p == ' ' || *p == '\t')
            p++;
    }
}

/* Make the child window id of class cls. */
static HWND child(HWND wnd, const char *cls, const char *text, DWORD style,
                  int id, int x, int y, int w, int h)
{
    return CreateWindow(cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, h,
                        wnd, (HMENU)id, w2_inst, NULL);
}

/* Add the n strings of rows to the list or combo box ctl, msg the
   message that adds one. */
static void fill(HWND ctl, UINT msg, const char *const *rows, int n)
{
    int i;

    for (i = 0; i < n; i++)
        SendMessage(ctl, msg, 0, (LPARAM)(LPSTR)rows[i]);
}

/* Make the controls of the window. */
static void make_controls(HWND wnd)
{
    char row[WT2_LONGROW + 1];
    HWND ctl;
    int i;

    child(wnd, "Static", "Caf\351:", SS_LEFT, WT2_ID_LABEL, 4, 4, 80, 12);
    ctl = child(wnd, "ListBox", NULL, WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                WT2_ID_LIST, 4, 20, 120, 60);
    fill(ctl, LB_ADDSTRING, w2_list, 4);
    for (i = 0; i < WT2_LONGROW; i++)
        row[i] = 'x';
    row[i] = '\0';
    SendMessage(ctl, LB_ADDSTRING, 0, (LPARAM)(LPSTR)row);
    SendMessage(ctl, LB_SETCURSEL, 0, 0L);
    ctl = child(wnd, "ListBox", NULL,
                WS_BORDER | WS_VSCROLL | LBS_EXTENDEDSEL | LBS_NOTIFY,
                WT2_ID_MARKS, 130, 20, 80, 60);
    fill(ctl, LB_ADDSTRING, w2_marks, 4);
    if (w2_blank)
        SendMessage(ctl, LB_INSERTSTRING, 1, (LPARAM)(LPSTR)"");
    ctl = child(wnd, "ComboBox", NULL, WS_VSCROLL | CBS_DROPDOWN,
                WT2_ID_COMBO, 216, 20, 100, 80);
    fill(ctl, CB_ADDSTRING, w2_combo, 3);
    SendMessage(ctl, CB_SETCURSEL, 1, 0L);
    child(wnd, "Edit", NULL, WS_BORDER | WS_VSCROLL | ES_MULTILINE |
          ES_AUTOVSCROLL, WT2_ID_EDIT, 4, 90, 200, 50);
    child(wnd, "Edit", NULL, WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
          WT2_ID_PASS, 216, 90, 100, 14);
    child(wnd, "Button", "&Off", BS_PUSHBUTTON | WS_DISABLED, WT2_ID_OFF,
          4, 150, 50, 14);
    child(wnd, "Button", "Hid", BS_PUSHBUTTON, WT2_ID_HID, 60, 150, 50, 14);
    ShowWindow(GetDlgItem(wnd, WT2_ID_HID), SW_HIDE);
    child(wnd, "Button", "&Go", BS_DEFPUSHBUTTON, WT2_ID_GO, 116, 150, 50,
          14);
    ctl = child(wnd, "Button", "C&heck", BS_AUTOCHECKBOX, WT2_ID_BOX, 172,
                150, 60, 14);
    SendMessage(ctl, BM_SETCHECK, 1, 0L);
    ctl = child(wnd, "Button", "Tri", BS_3STATE, WT2_ID_TRI, 236, 150, 40,
                14);
    SendMessage(ctl, BM_SETCHECK, 2, 0L);
    child(wnd, "Static", k_status, SS_LEFT, WT2_ID_STATUS, 4, 170, 200, 12);
    if (w2_mouse) {
        ctl = child(wnd, k_pad, NULL, 0, WT2_ID_PADA, WT2_PADX, WT2_PADY,
                    WT2_PADW, WT2_PADH);
        child(ctl, k_padn, NULL, 0, WT2_ID_PADC, WT2_PADCX, WT2_PADCY,
              WT2_PADCW, WT2_PADCH);
        child(wnd, k_padn, NULL, 0, WT2_ID_PADB, WT2_PADBX, WT2_PADY,
              WT2_PADW, WT2_PADH);
    }
}

/* Write in the window text of to the n-th mouse message reported there,
   msg with wp and lp: "n msg x y wp keys holder", msg and wp in hex, x
   and y signed, keys L and R for the buttons GetKeyState holds down, '-'
   for each up, then S, C and A for SHIFT, CTRL and ALT held down, none
   for one up, holder the window holding the mouse once the message is
   handled, '-' for none, W for the main window, else its control ID. */
static void report(HWND to, UINT n, UINT msg, WPARAM wp, LPARAM lp)
{
    char t[WT2_REPLEN];
    HWND h;
    int i;

    i = wsprintf(t, "%u %03X %d %d %04X %c%c", n, msg, (int)LOWORD(lp),
                 (int)HIWORD(lp), wp,
                 GetKeyState(VK_LBUTTON) < 0 ? 'L' : '-',
                 GetKeyState(VK_RBUTTON) < 0 ? 'R' : '-');
    if (GetKeyState(VK_SHIFT) < 0)
        t[i++] = 'S';
    if (GetKeyState(VK_CONTROL) < 0)
        t[i++] = 'C';
    if (GetKeyState(VK_MENU) < 0)
        t[i++] = 'A';
    t[i++] = ' ';
    h = GetCapture();
    if (h == NULL)
        t[i++] = '-';
    else if (h == w2_main)
        t[i++] = 'W';
    else
        i += wsprintf(t + i, "%u", GetWindowWord(h, GWW_ID));
    t[i] = '\0';
    SetWindowText(to, t);
}

/* Whether msg, lp in the client area of w, is a move at the screen's
   pointer: one Windows makes itself, at a time the load of the machine
   decides, as the driver leaves the pointer where it is and the scripts
   never aim there. */
static int pointer_move(HWND w, UINT msg, LPARAM lp)
{
    POINT p;
    POINT c;

    if (msg != WM_MOUSEMOVE)
        return 0;
    p.x = (int)LOWORD(lp);
    p.y = (int)HIWORD(lp);
    ClientToScreen(w, &p);
    GetCursorPos(&c);
    return p.x == c.x && p.y == c.y;
}

/* A pad: its mouse messages handled as the header says and reported,
   counted in its window word. */
LRESULT WCALLBACK w2_padproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    UINT n;

    if (msg < WM_MOUSEFIRST || msg > WM_MOUSELAST)
        return DefWindowProc(w, msg, wp, lp);
    if (pointer_move(w, msg, lp))
        return 0;
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(w);
        break;
    case WM_LBUTTONUP:
        if (GetCapture() == w)
            ReleaseCapture();
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        SetCapture(w2_main);
        break;
    default:
        break;
    }
    n = GetWindowWord(w, 0) + 1;
    SetWindowWord(w, 0, n);
    report(w, n, msg, wp, lp);
    return 0;
}

/* Move the focus to the control id. */
static void focus(HWND wnd, int id)
{
    SetFocus(GetDlgItem(wnd, id));
}

/* Write text on the status line. */
static void status(HWND wnd, const char *text)
{
    SetDlgItemText(wnd, WT2_ID_STATUS, text);
}

/* Ask the question; the focus is where it was after the box. */
static void ask(HWND wnd)
{
    HWND saved;
    int r;

    saved = GetFocus();
    r = MessageBox(wnd, "Proceed?", "Question", MB_YESNO | MB_ICONQUESTION);
    SetFocus(saved);
    status(wnd, r == IDYES ? "yes" : "no");
}

/* End Busy's hold, with text on the status line. */
static void unbusy(HWND wnd, const char *text)
{
    KillTimer(wnd, WT2_TIMER);
    w2_busy = 0;
    status(wnd, text);
    td_busy(0);
}

/* Run the command id. */
static void command(HWND wnd, WPARAM id)
{
    switch (id) {
    case WT2_C_LIST:
        focus(wnd, WT2_ID_LIST);
        break;
    case WT2_C_MARKS:
        focus(wnd, WT2_ID_MARKS);
        break;
    case WT2_C_COMBO:
        focus(wnd, WT2_ID_COMBO);
        break;
    case WT2_C_EDIT:
        focus(wnd, WT2_ID_EDIT);
        break;
    case WT2_C_PASS:
        focus(wnd, WT2_ID_PASS);
        break;
    case WT2_C_MSG:
        ask(wnd);
        break;
    case WT2_C_BUSY:
        w2_busy = 1;
        td_busy(1);
        td_ahead();
        status(wnd, "busy");
        SetTimer(wnd, WT2_TIMER, WT2_BUSYMS, NULL);
        break;
    case WT2_C_CHECK:
        w2_checked = !w2_checked;
        CheckMenuItem(GetMenu(wnd), WT2_C_CHECK,
                      MF_BYCOMMAND | (w2_checked ? MF_CHECKED : MF_UNCHECKED));
        break;
    case WT2_C_DEEP:
        status(wnd, "deep");
        break;
    case WT2_C_STALL:
        td_busy(1);
        break;
    case WT2_C_HIDE:
        ShowWindow(wnd, SW_HIDE);
        break;
    case WT2_C_EXIT:
        DestroyWindow(wnd);
        break;
    }
}

LRESULT WCALLBACK w2_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (w2_mouse && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST &&
        GetCapture() == wnd) {
        if (pointer_move(wnd, msg, lp))
            return 0;
        if (msg == WM_RBUTTONUP)
            ReleaseCapture();
        report(GetDlgItem(wnd, WT2_ID_STATUS), ++w2_mice, msg, wp, lp);
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        make_controls(wnd);
        return 0;
    case WM_COMMAND:
        command(wnd, wp);
        return 0;
    case WM_TIMER:
        if (wp == WT2_TIMER)
            unbusy(wnd, "done");
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(wnd, msg, wp, lp);
}

/* Take every timer Windows has to give, for the window wnd; they are
   never killed and their messages are not heeded. */
static void hog_timers(HWND wnd)
{
    int i;

    for (i = 0; i < WT2_MAXTIMERS; i++)
        if (SetTimer(wnd, WT2_TIMER + 1 + i, 60000u, NULL) == 0)
            break;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char title[32];
    WNDCLASS wc;
    HACCEL accel;
    HWND wnd;
    MSG msg;

    w2_inst = inst;
    parse(cmd);
    if (prev == NULL) {
        wc.style = 0;
        wc.lpfnWndProc = w2_wndproc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = MAKEINTRESOURCE(WT2_MENU);
        wc.lpszClassName = k_class;
        if (!RegisterClass(&wc))
            return 1;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = w2_padproc;
        wc.cbWndExtra = sizeof(WORD);
        wc.hIcon = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = k_pad;
        if (!RegisterClass(&wc))
            return 1;
        wc.style = 0;
        wc.lpszClassName = k_padn;
        if (!RegisterClass(&wc))
            return 1;
    }
    LoadString(inst, WT2_S_TITLE, title, sizeof title);
    wnd = CreateWindow(k_class, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                       CW_USEDEFAULT, 0, NULL, NULL, inst, NULL);
    if (wnd == NULL)
        return 1;
    w2_main = wnd;
    accel = LoadAccelerators(inst, MAKEINTRESOURCE(WT2_ACCEL));
    if (w2_hog)
        hog_timers(wnd);
    ShowWindow(wnd, show);
    UpdateWindow(wnd);
    td_init(inst, w2_keys, w2_snap);
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (w2_busy && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            unbusy(wnd, "stopped");
        else if (!TranslateAccelerator(wnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    td_done();
    return msg.wParam;
}

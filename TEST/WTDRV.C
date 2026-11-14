/*
 * WTDRV.C - the test of the Windows test driver (SRC/WIN/TDRV.C): a
 * small dialog program that the driver types into and snaps.
 *
 * usage: WIN WTDRV [/PLACE] [/DROP] /KEYS:WTDRV.KEY /SNAP:WTDRV.SNP
 *
 * The main window is a modeless dialog (TEST/WTDRV.RC) with an edit
 * control, a check box, a sorted list box holding ALPHA and BRAVO, the
 * buttons Add (the name becomes a row) and About (the modal dialog, with
 * a password edit control, whose OK or Cancel adds DIALOG OK or DIALOG
 * CANCEL), and a menu: File with Dialog (the modal dialog), Work (F5)
 * and Exit, Options with Marked (CTRL+K, which checks or unchecks it)
 * and Gray, always grayed.  Work tells the driver the program is busy
 * (td_busy) until a timer of WTD_WORKMS ms adds WORK DONE, so a snapshot
 * taken after F5 shows the row only if the driver waited.  The message
 * loop takes accelerators and dialog keys as a real program's does.
 *
 * TEST/WTDRV.KEY types into it and ends it; every mode of Windows must
 * give the snapshot file TEST/GOLDEN/WIN/WTDRV.SNP, byte for byte.  Run
 * without /KEYS:, it is an ordinary program.  /PLACE has the driver
 * check where its dialogs open (td_place), which fails the run with
 * PLACE: the main window stands where its template puts it, not centred
 * on the screen.  /DROP takes the messages waiting before the message
 * loop starts and drops a WM_QUIT among them, as a program does whose
 * start-up empties its queue: a run that td_init refuses must end all
 * the same (TEST/WTDRVD.KEY).
 */
#include "COMPAT.H"
#include "CWIN.H"
#include "TDRV.H"
#include "WTDRV.H"

#define WTD_CMDMAX      128     /* a DOS command tail and its NUL       */
#define WTD_NAMELEN     32      /* characters of a name kept, NUL
                                   included                             */
#define WTD_WORKMS      1000    /* how long Work takes                  */
#define WTD_T_WORK      1       /* the timer of Work                    */

static const char k_keys[] = "/KEYS:";
static const char k_snap[] = "/SNAP:";
static const char k_place[] = "/PLACE";
static const char k_drop[] = "/DROP";

static HINSTANCE wtd_inst;

/* Add the row text to the list box of the main window dlg and select
   it. */
static void add_row(HWND dlg, const char *text)
{
    LRESULT i;

    i = SendDlgItemMessage(dlg, WTD_LIST, LB_ADDSTRING, 0,
                           (LPARAM)(LPCSTR)text);
    SendDlgItemMessage(dlg, WTD_LIST, LB_SETCURSEL, (WPARAM)i, 0L);
}

BOOL WCALLBACK wtd_about(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(dlg, (int)wp);
        return TRUE;
    }
    return FALSE;
}

/* Run the modal dialog over the main window dlg and add the row that
   says how it was closed. */
static void about(HWND dlg)
{
    FARPROC proc;
    int rc;

    proc = MakeProcInstance((FARPROC)wtd_about, wtd_inst);
    rc = DialogBox(wtd_inst, MAKEINTRESOURCE(WTD_D_ABOUT), dlg,
                   (DLGPROC)proc);
    FreeProcInstance(proc);
    add_row(dlg, rc == IDOK ? "DIALOG OK" : "DIALOG CANCEL");
}

/* Carry out the command id of the main window dlg. */
static void command(HWND dlg, WPARAM id)
{
    char name[WTD_NAMELEN];
    HMENU m;

    switch (id) {
    case WTD_ADD:
        GetDlgItemText(dlg, WTD_NAME, name, (int)sizeof name);
        add_row(dlg, name);
        break;
    case WTD_ABOUT:
    case WTD_C_DIALOG:
        about(dlg);
        break;
    case WTD_C_WORK:
        if (SetTimer(dlg, WTD_T_WORK, WTD_WORKMS, NULL) != 0)
            td_busy(1);
        break;
    case WTD_C_MARK:
        m = GetMenu(dlg);
        CheckMenuItem(m, WTD_C_MARK,
                      (GetMenuState(m, WTD_C_MARK, MF_BYCOMMAND) &
                       MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED);
        break;
    case WTD_C_EXIT:
        DestroyWindow(dlg);
        break;
    default:
        break;
    }
}

BOOL WCALLBACK wtd_main(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        add_row(dlg, "BRAVO");
        add_row(dlg, "ALPHA");
        return TRUE;
    case WM_COMMAND:
        command(dlg, wp);
        return TRUE;
    case WM_TIMER:
        KillTimer(dlg, WTD_T_WORK);
        add_row(dlg, "WORK DONE");
        td_busy(0);
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(dlg);
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    default:
        return FALSE;
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char words[WTD_CMDMAX];
    const char *keys;
    const char *snap;
    char *p;
    int place;
    int drop;
    FARPROC proc;
    HACCEL acc;
    HWND dlg;
    MSG msg;

    (void)prev;
    (void)show;
    for (p = words; *cmd != '\0' && p < words + sizeof words - 1; )
        *p++ = *cmd++;
    *p = '\0';
    keys = NULL;
    snap = NULL;
    place = 0;
    drop = 0;
    for (p = words; *p != '\0'; ) {
        while (*p == ' ')
            *p++ = '\0';
        if (txt_ieqn(p, k_keys, sizeof k_keys - 1))
            keys = p + sizeof k_keys - 1;
        else if (txt_ieqn(p, k_snap, sizeof k_snap - 1))
            snap = p + sizeof k_snap - 1;
        else if (txt_ieqn(p, k_place, sizeof k_place - 1))
            place = 1;
        else if (txt_ieqn(p, k_drop, sizeof k_drop - 1))
            drop = 1;
        while (*p != '\0' && *p != ' ')
            p++;
    }
    td_init(inst, keys, snap);
    if (place)
        td_place();
    wtd_inst = inst;
    proc = MakeProcInstance((FARPROC)wtd_main, inst);
    dlg = CreateDialog(inst, MAKEINTRESOURCE(WTD_D_MAIN), NULL,
                       (DLGPROC)proc);
    acc = LoadAccelerators(inst, MAKEINTRESOURCE(WTD_A_MAIN));
    msg.wParam = 0;
    if (dlg != NULL) {
        if (drop)
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessage(&msg);
        while (GetMessage(&msg, NULL, 0, 0))
            if (!TranslateAccelerator(dlg, acc, &msg) &&
                !IsDialogMessage(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
    }
    FreeProcInstance(proc);
    td_done();
    return (int)msg.wParam;
}

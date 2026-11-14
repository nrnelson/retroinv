/*
 * SETUP.C - SETUP, the installer of RETROINV's Windows client: it copies
 * the client's files from the disk it starts from and makes its Program
 * Manager group, or with /U removes them.
 *
 * usage: SETUP      install, from Program Manager's File > Run: A:SETUP
 *        SETUP /U   remove the installation in SETUP's own directory
 *
 * Installing.  One dialog asks for the directory, by default SETUP's own
 * when that is on a fixed disk, else C:\RETROINV, of which SETUP makes
 * the last level, as INSTALL does, and the switches of the link that the
 * item gives INVWIN.  The files are those of the list INS_LIST beside the
 * program whose roles hold W (SETUPL.H), SETUP.EXE among them, then the
 * list itself, so that SETUP /U, run from the directory, knows them.  The
 * sources are opened in compatibility mode, the only one in which DOS
 * with SHARE lets SETUP read its own program file, which Windows holds
 * open for the task that started SETUP.  Each is copied, or expanded when
 * the disk holds it compressed, into the temporary file SU_TEMP of the
 * directory first, which replaces an earlier copy only once it is whole,
 * so a failure leaves no partly written file (su_copy); the data, the
 * settings and every other file stay.  Started from the
 * directory it would install into, as after the DOS INSTALL has copied
 * the files there, SETUP copies nothing and makes the group alone.
 *
 * Program Manager.  Windows 3.0 has no DDE library, so SETUP holds the
 * conversation in raw messages, from a hidden window of its own: it
 * broadcasts WM_DDE_INITIATE for the application and topic PROGMAN,
 * keeps the first window that acknowledges and terminates any other,
 * posts each command string as WM_DDE_EXECUTE in a shared global block,
 * which it frees on the WM_DDE_ACK, and ends with WM_DDE_TERMINATE.
 * Every wait ends after SU_WAITMS.  Program Manager 3.0 has no command
 * that replaces an item, so SETUP deletes the group first and makes it
 * afresh with one item: a second run leaves one item, with that run's
 * switches.  When no Program Manager answers, as under another shell, or
 * it refuses the commands, the files stay installed and SETUP says how
 * to make the item by hand.
 *
 * Removing.  SETUP /U asks first, then deletes the group and, in its own
 * directory, the files of the list's W lines but itself, which Windows
 * keeps open while it runs, and but a file the DOS programs share
 * (INV.MSG) while one that only they need is there.  SETUP.EXE stays
 * with the list, for the DOS INSTALL /U, which removes every file the
 * list names.  A directory on a floppy disk is refused, as it is the
 * release disk's.
 *
 * WIN.INI, SYSTEM.INI and every file outside the directory are left as
 * they are.  WinMain fills its stack with cstk_fill's pattern first, so
 * that its test WSETUP can count from outside the bytes SETUP never
 * reached.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include <dde.h>
#include "SETUPL.H"
#include "SETUP.H"

#define SU_TEXTMAX      1024    /* bytes of a text, wsprintf's most     */
#define SU_LINEMAX      256     /* bytes of a string of SETUP.RC        */
#define SU_NUMLEN       8       /* bytes of a DOS error's number        */
#define SU_WAITMS       10000   /* the longest wait for an answer, ms   */
#define SU_TIMER        1       /* the wait's timer                     */

/* The states of a wait for Program Manager. */
#define SU_WAITING      0
#define SU_ACKED        1       /* it carried out the command or ended  */
#define SU_REFUSED      2
#define SU_TIMEDOUT     3

static const char k_class[] = "RETROINVSETUP";
static const char k_progman[] = "PROGMAN";
static const char k_defdir[] = "C:\\" SU_NAME;

static HINSTANCE su_inst;
static char su_src[DOS_PATHMAX];    /* SETUP's directory and a '\'      */
static char su_self[DOS_FINDNAME];  /* SETUP's own file name            */
static su_list su_files;            /* the list, every line checked     */
static HWND su_server;              /* Program Manager's, or NULL       */
static int su_state;                /* the wait's SU_ state             */
static u8 su_init;                  /* 1 while INITIATE is answered     */
static u8 su_closing;               /* 1 once our TERMINATE is posted   */

/* The string id of SETUP.RC with its %s filled in from a, b and c, into
   out (SU_TEXTMAX bytes). */
static void text(char *out, int id, const char *a, const char *b,
                 const char *c)
{
    char f[SU_LINEMAX];

    LoadString(su_inst, id, f, sizeof f);
    wsprintf(out, f, (LPCSTR)a, (LPCSTR)b, (LPCSTR)c);
}

/* Show the string id of SETUP.RC, its %s filled in from a, b and c, in
   a message box of style st over owner; the button chosen. */
static int tell(HWND owner, UINT st, int id, const char *a, const char *b,
                const char *c)
{
    char cap[SU_LINEMAX];
    char t[SU_TEXTMAX];

    text(t, id, a, b, c);
    LoadString(su_inst, SU_S_TITLE, cap, sizeof cap);
    return MessageBox(owner, t, cap, st);
}

/* The code rc of su_copy or cf_remove in words, for SU_S_NOCOPY and
   SU_S_NODEL, into out (SU_TEXTMAX bytes). */
static void reason(char *out, int rc)
{
    char num[SU_NUMLEN];
    int id;

    wsprintf(num, "%d", rc);
    if (rc == LZ_EFORMAT)
        id = SU_S_RBAD;
    else if (rc == CF_ENOENT)
        id = SU_S_RNOENT;
    else if (rc == CF_EFULL)
        id = SU_S_RFULL;
    else if (rc == CF_EACCES || rc == CF_ESHARE)
        id = SU_S_RUSED;
    else
        id = SU_S_RDOS;
    text(out, id, num, NULL, NULL);
}

/* ---------------------------------------------------------------------
 * Program Manager.
 * --------------------------------------------------------------------- */

LRESULT WCALLBACK su_ddeproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_DDE_ACK:
        if (su_init) {
            GlobalDeleteAtom(LOWORD(lp));
            GlobalDeleteAtom(HIWORD(lp));
            if (su_server == NULL)
                su_server = (HWND)wp;
            else
                PostMessage((HWND)wp, WM_DDE_TERMINATE, (WPARAM)w, 0L);
        } else if ((HWND)wp == su_server) {
            GlobalFree((HGLOBAL)HIWORD(lp));
            if (su_state == SU_WAITING)
                su_state = (LOWORD(lp) & 0x8000) ? SU_ACKED : SU_REFUSED;
        }
        return 0;
    case WM_DDE_TERMINATE:
        if ((HWND)wp == su_server) {
            if (!su_closing)
                PostMessage(su_server, WM_DDE_TERMINATE, (WPARAM)w, 0L);
            su_server = NULL;
            su_state = su_closing ? SU_ACKED : SU_REFUSED;
        }
        return 0;
    case WM_TIMER:
        su_state = SU_TIMEDOUT;
        return 0;
    default:
        return DefWindowProc(w, m, wp, lp);
    }
}

/* Dispatch messages until Program Manager answers w or SU_WAITMS pass,
   su_state then saying which. */
static void wait_answer(HWND w)
{
    MSG msg;

    if (SetTimer(w, SU_TIMER, SU_WAITMS, NULL) == 0) {
        su_state = SU_TIMEDOUT;
        return;
    }
    while (su_state == SU_WAITING && GetMessage(&msg, NULL, 0, 0))
        DispatchMessage(&msg);
    KillTimer(w, SU_TIMER);
}

/* Send Program Manager the commands s from w: 1 when it carried them
   out. */
static int dde_exec(HWND w, const char *s)
{
    HGLOBAL h;

    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)strlen(s) + 1);
    if (h == NULL)
        return 0;
    lstrcpy(GlobalLock(h), s);
    GlobalUnlock(h);
    su_state = SU_WAITING;
    if (!PostMessage(su_server, WM_DDE_EXECUTE, (WPARAM)w,
                     MAKELPARAM(0, h))) {
        GlobalFree(h);
        return 0;
    }
    wait_answer(w);
    return su_state == SU_ACKED;
}

/* In a conversation with Program Manager, delete the group, then run
   the commands add unless it is NULL: 1 when Program Manager answered
   and carried out add, 0 when it refused add, -1 when none answered. */
static int progman(const char *add)
{
    WNDCLASS wc;
    HWND w;
    ATOM a;
    int rc;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = su_ddeproc;
    wc.hInstance = su_inst;
    wc.lpszClassName = k_class;
    RegisterClass(&wc);
    w = CreateWindow(k_class, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL,
                     su_inst, NULL);
    if (w == NULL)
        return -1;
    a = GlobalAddAtom(k_progman);
    su_server = NULL;
    su_init = 1;
    SendMessage(HWND_BROADCAST, WM_DDE_INITIATE, (WPARAM)w,
                MAKELPARAM(a, a));
    su_init = 0;
    GlobalDeleteAtom(a);
    rc = -1;
    if (su_server != NULL) {
        dde_exec(w, su_k_delete);
        rc = add == NULL || dde_exec(w, add);
    }
    if (su_server != NULL) {
        su_closing = 1;
        su_state = SU_WAITING;
        PostMessage(su_server, WM_DDE_TERMINATE, (WPARAM)w, 0L);
        wait_answer(w);
        su_closing = 0;
        su_server = NULL;
    }
    DestroyWindow(w);
    return rc;
}

/* ---------------------------------------------------------------------
 * Installing.
 * --------------------------------------------------------------------- */

/* Copy the Windows client's files of the list, then the list, into dir,
   naming each in the dialog dlg's note: 0, or su_copy's code for the
   file that failed, its name in name (DOS_FINDNAME bytes). */
static int copy_all(HWND dlg, const char *dir, char *name)
{
    char t[SU_TEXTMAX];
    su_list l;
    u8 roles;
    int more;
    int rc;

    l = su_files;
    do {
        more = su_list_next(&l, name, &roles) > 0;
        if (!more)
            strcpy(name, INS_LIST);
        else if (!(roles & INS_R_WIN))
            continue;
        text(t, SU_S_COPYING, name, NULL, NULL);
        SetDlgItemText(dlg, SU_C_NOTE, t);
        UpdateWindow(GetDlgItem(dlg, SU_C_NOTE));
        rc = su_copy(su_src, dir, name, more && (roles & INS_R_LZ));
        if (rc != 0)
            return rc;
    } while (more);
    return 0;
}

/* Show the note under the switches. */
static void note(HWND dlg)
{
    char t[SU_LINEMAX];

    LoadString(su_inst, SU_S_NOTE, t, sizeof t);
    SetDlgItemText(dlg, SU_C_NOTE, t);
}

/* Install into the directory, and with the switches, that the dialog
   dlg holds, and say how it went over dlg: 1 when the files are
   installed, 0 when the user is to put something right. */
static int install(HWND dlg)
{
    char dir[DOS_PATHMAX];
    char opts[SU_CMDMAX];
    char cmd[SU_CMDMAX];
    char dde[SU_DDEMAX];
    char name[DOS_FINDNAME];
    char why[SU_TEXTMAX];
    HCURSOR old;
    int same;
    int bad;
    int rc;
    int pm;

    GetDlgItemText(dlg, SU_C_DIR, dir, sizeof dir);
    GetDlgItemText(dlg, SU_C_OPTS, opts, sizeof opts);
    bad = !su_dir_ok(dir) ? SU_S_BADDIR : !su_cmd(cmd, dir, opts) ?
          SU_S_BADOPTS : 0;
    if (bad == 0) {
        su_join(why, dir, "");
        same = lstrcmpi(why, su_src) == 0;
        if (same && su_packed(&su_files, su_src))
            bad = SU_S_PACKED;
    }
    if (bad != 0) {
        tell(dlg, MB_ICONEXCLAMATION, bad, dir, NULL, NULL);
        SetFocus(GetDlgItem(dlg, bad == SU_S_BADOPTS ? SU_C_OPTS :
                                                        SU_C_DIR));
        return 0;
    }
    EnableWindow(dlg, FALSE);
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    rc = 0;
    name[0] = '\0';
    if (!same) {
        rc = cf_mkdir(dir);
        if (rc == CF_EACCES)
            rc = 0;
        if (rc == 0)
            rc = copy_all(dlg, dir, name);
    }
    pm = 0;
    if (rc == 0) {
        su_dde_add(dde, cmd);
        pm = progman(dde);
    }
    SetCursor(old);
    EnableWindow(dlg, TRUE);
    note(dlg);
    if (rc != 0 && name[0] == '\0') {
        tell(dlg, MB_ICONSTOP, SU_S_NODIR, dir, NULL, NULL);
    } else if (rc != 0) {
        reason(why, rc);
        tell(dlg, MB_ICONSTOP, SU_S_NOCOPY, name, dir, why);
    } else if (pm == 1) {
        tell(dlg, MB_ICONINFORMATION, SU_S_DONE, dir, NULL, NULL);
    } else {
        text(why, pm < 0 ? SU_S_NOPM : SU_S_PMFAIL, NULL, NULL, NULL);
        tell(dlg, MB_ICONEXCLAMATION, SU_S_BYHAND, why, dir, cmd);
    }
    return rc == 0;
}

/* Centre the dialog dlg on the screen, as Windows 3.0 has no
   DS_CENTER. */
static void centre(HWND dlg)
{
    RECT r;

    GetWindowRect(dlg, &r);
    SetWindowPos(dlg, NULL,
                 (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                 (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

BOOL WCALLBACK su_dlgproc(HWND dlg, UINT m, WPARAM wp, LPARAM lp)
{
    char t[SU_LINEMAX];

    (void)lp;
    switch (m) {
    case WM_INITDIALOG:
        LoadString(su_inst, SU_S_TITLE, t, sizeof t);
        SetWindowText(dlg, t);
        note(dlg);
        strcpy(t, su_src);
        if (GetDriveType(su_src[0] - 'A') == DRIVE_FIXED)
            t[strlen(t) - 1] = '\0';
        else
            strcpy(t, k_defdir);
        SetDlgItemText(dlg, SU_C_DIR, t);
        SendDlgItemMessage(dlg, SU_C_DIR, EM_LIMITTEXT, SU_DIRMAX, 0L);
        SendDlgItemMessage(dlg, SU_C_OPTS, EM_LIMITTEXT, SU_CMDMAX - 1,
                           0L);
        centre(dlg);
        return TRUE;
    case WM_COMMAND:
        if (wp != IDOK && wp != IDCANCEL)
            return FALSE;
        if (wp == IDCANCEL || install(dlg))
            EndDialog(dlg, wp);
        return TRUE;
    default:
        return FALSE;
    }
}

/* ---------------------------------------------------------------------
 * Removing.
 * --------------------------------------------------------------------- */

/* 1 when the directory dir holds a file of the list that only the DOS
   programs need, the list itself aside. */
static int dos_here(const char *dir)
{
    char path[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    su_list l;
    u8 roles;

    l = su_files;
    while (su_list_next(&l, name, &roles) > 0) {
        if (!(roles & INS_R_DOS) || (roles & INS_R_WIN) ||
            strcmp(name, INS_LIST) == 0)
            continue;
        su_join(path, dir, name);
        if (cf_exists(path) == 1)
            return 1;
    }
    return 0;
}

/* SETUP /U: remove the Windows client from SETUP's directory after
   asking, and say how it went. */
static void uninstall(void)
{
    char dir[DOS_PATHMAX];
    char path[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    char failed[DOS_FINDNAME];
    char group[SU_LINEMAX];
    char why[SU_TEXTMAX];
    su_list l;
    u8 roles;
    int keep;
    int rc;
    int rc2;

    strcpy(dir, su_src);
    dir[strlen(dir) - 1] = '\0';
    if (GetDriveType(dir[0] - 'A') == DRIVE_REMOVABLE) {
        tell(NULL, MB_ICONSTOP, SU_S_FLOPPY, NULL, NULL, NULL);
        return;
    }
    if (tell(NULL, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, SU_S_ASK,
             dir, NULL, NULL) != IDYES)
        return;
    group[0] = '\0';
    if (progman(NULL) < 0)
        LoadString(su_inst, SU_S_NOGROUP, group, sizeof group);
    keep = dos_here(dir);
    l = su_files;
    rc = 0;
    while (su_list_next(&l, name, &roles) > 0) {
        if (!(roles & INS_R_WIN) || (keep && (roles & INS_R_DOS)) ||
            strcmp(name, su_self) == 0)
            continue;
        su_join(path, dir, name);
        rc2 = cf_remove(path);
        if (rc2 != 0 && rc2 != CF_ENOENT && rc == 0) {
            rc = rc2;
            strcpy(failed, name);
        }
    }
    if (rc != 0) {
        reason(why, rc);
        tell(NULL, MB_ICONSTOP, SU_S_NODEL, failed, why, group);
    } else {
        tell(NULL, group[0] != '\0' ? MB_ICONEXCLAMATION :
             MB_ICONINFORMATION, SU_S_GONE, dir, group, NULL);
    }
}

/* ---------------------------------------------------------------------
 * Start.
 * --------------------------------------------------------------------- */

/* Read the list beside the program into buf (INS_LISTMAX bytes), open
   su_files on it and check every line: 1, or 0 when it is missing or
   damaged, which is said. */
static int load_list(char *buf)
{
    char path[DOS_PATHMAX];
    char name[DOS_FINDNAME];
    su_list l;
    u8 roles;
    u16 got;
    int h;
    int rc;

    su_join(path, su_src, INS_LIST);
    rc = cf_open(path, CF_READ, &h);
    if (rc == 0) {
        rc = cf_read(h, buf, INS_LISTMAX, &got);
        cf_close(h);
    }
    if (rc == 0 && su_list_open(&su_files, buf, got)) {
        l = su_files;
        while ((rc = su_list_next(&l, name, &roles)) > 0)
            ;
        if (rc == 0)
            return 1;
    }
    tell(NULL, MB_ICONSTOP, SU_S_NOLIST, path, NULL, NULL);
    return 0;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char list[INS_LISTMAX];
    char arg[DOS_PATHMAX];
    char *p;
    char *s;
    FARPROC proc;

    (void)prev;
    (void)show;
    cstk_fill();
    su_inst = inst;
    GetModuleFileName(inst, su_src, sizeof su_src);
    s = strrchr(su_src, '\\') + 1;
    strcpy(su_self, s);
    *s = '\0';
    lstrcpy(arg, cmd);
    for (p = arg + strlen(arg); p > arg && p[-1] == ' '; p--)
        ;
    *p = '\0';
    for (p = arg; *p == ' '; p++)
        ;
    if (*p != '\0' && lstrcmpi(p, "/U") != 0 && lstrcmpi(p, "-U") != 0) {
        tell(NULL, MB_ICONINFORMATION, SU_S_USAGE, NULL, NULL, NULL);
    } else if (load_list(list)) {
        if (*p != '\0') {
            uninstall();
        } else {
            proc = MakeProcInstance((FARPROC)su_dlgproc, inst);
            DialogBox(inst, MAKEINTRESOURCE(SU_R_MAIN), NULL,
                      (DLGPROC)proc);
            FreeProcInstance(proc);
        }
    }
    return 0;
}

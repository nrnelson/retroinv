/*
 * WFILE.C - the file name dialog of the Windows programs (WFILE.H).
 *
 * The dialog's state (fl_dlg) is on wfile_run's stack, and fl_cur points
 * to it while the dialog runs, for the dialog procedure.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "MSG.H"
#include "WDLG.H"
#include "WFILE.H"

#define FL_SPECSZ       13      /* a pattern: an 8.3 name and its NUL   */
#define FL_ATTRS        (DDL_DIRECTORY | DDL_DRIVES)   /* listed rows   */
#define FL_TEXTSZ       128     /* a refusal, its number included       */

static const char k_all[] = "*.*";
static const char k_empty[] = "";

typedef struct {
    HWND dlg;
    char *path;                 /* the owner's: the file chosen         */
    u16  pathsz;
    u16  title;                 /* the caption's message                */
    char home[DOS_PATHMAX];     /* the task's directory, CP437, as cwd
                                   gives it                             */
    char dir[DOS_PATHMAX];      /* the directory listed, likewise       */
    char spec[FL_SPECSZ];       /* the pattern listed, Windows set      */
} fl_dlg;

static fl_dlg *fl_cur;          /* the dialog running, or NULL          */

/* The task's current drive and directory into dir (DOS_PATHMAX bytes),
   CP437 and ending in its separator, as OpenFile parses a name there;
   "" when it cannot. */
static void cwd(char *dir)
{
    OFSTRUCT of;

    if (OpenFile("X", &of, OF_PARSE) == HFILE_ERROR)
        dir[0] = '\0';
    else
        txt_path_dir(dir, DOS_PATHMAX, of.szPathName);
}

/* Make dir, as cwd gives one, the task's current drive and directory,
   as DlgDirList does before it lists: "" leaves them. */
static void go(fl_dlg *c, const char *dir)
{
    char p[DOS_PATHMAX + sizeof k_all];

    strcpy(p, dir);
    strcat(p, k_all);
    OemToAnsi(p, p);
    DlgDirList(c->dlg, p, 0, 0, 0);
}

/* List spec (in the Windows set, a path whose last part may hold * and
   ?, which DlgDirList leaves in spec) from the directory listed, which
   becomes the one it names, its last part the pattern; the task's
   directory stays as it was.  1, or 0 when spec names no directory, the
   list as it was. */
static int list(fl_dlg *c, char *spec)
{
    int found;

    go(c, c->dir);
    found = DlgDirList(c->dlg, spec, WFILE_C_LIST, WFILE_C_DIR, FL_ATTRS);
    if (found) {
        cwd(c->dir);
        txt_copy(c->spec, spec, (u16)sizeof c->spec);
    }
    go(c, c->home);
    return found;
}

/* Put text (Windows set) into the name, selected, with the focus. */
static void show(fl_dlg *c, const char *text)
{
    HWND e;

    e = GetDlgItem(c->dlg, WFILE_C_NAME);
    SetWindowText(e, text);
    SendMessage(c->dlg, WM_NEXTDLGCTL, (WPARAM)e, 1L);
}

/* Refuse the name with string ids, given n, on the message line, the
   focus on the name. */
static void refuse(fl_dlg *c, UINT ids, int n)
{
    char fmt[FL_TEXTSZ];
    char t[FL_TEXTSZ];

    LoadString((HINSTANCE)GetWindowWord(c->dlg, GWW_HINSTANCE), ids, fmt,
               (int)sizeof fmt);
    wsprintf(t, fmt, n);
    SetDlgItemText(c->dlg, WFILE_C_MSG, t);
    SendMessage(c->dlg, WM_NEXTDLGCTL,
                (WPARAM)GetDlgItem(c->dlg, WFILE_C_NAME), 1L);
}

/* The row chosen into the name, a directory's or a drive's path
   followed by the pattern. */
static void chosen(fl_dlg *c)
{
    char t[DOS_PATHMAX + FL_SPECSZ];

    if (DlgDirSelect(c->dlg, t, WFILE_C_LIST))
        strcat(t, c->spec);
    SetDlgItemText(c->dlg, WFILE_C_NAME, t);
}

/* OK: the name taken as WFILE.H says.  Its control holds at most
   pathsz - 1 characters, fewer than DOS_PATHMAX, so a pattern after it
   fits p. */
static void ok(fl_dlg *c)
{
    char t[DOS_PATHMAX];
    char p[DOS_PATHMAX + FL_SPECSZ];
    char *s;
    u16 n;
    u16 d;

    SetDlgItemText(c->dlg, WFILE_C_MSG, k_empty);
    GetDlgItemText(c->dlg, WFILE_C_NAME, t, (int)sizeof t);
    for (s = t; *s == ' '; s++)
        ;
    n = (u16)strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = '\0';
    if (n == 0) {
        show(c, s);
        return;
    }
    if (strpbrk(s, "*?") != NULL) {
        strcpy(p, s);
        if (list(c, p))
            show(c, c->spec);
        else
            refuse(c, WFILE_S_NODIR, 0);
        return;
    }
    /* A directory or a drive: listed with the pattern. */
    strcpy(p, s);
    if (s[n - 1] != '\\' && s[n - 1] != ':')
        strcat(p, "\\");
    strcat(p, c->spec);
    if (list(c, p)) {
        show(c, c->spec);
        return;
    }
    /* A file: its directory part listed first. */
    txt_path_dir(p, (u16)sizeof p, s);
    d = (u16)strlen(p);
    if (d > 0) {
        strcat(p, c->spec);
        if (!list(c, p)) {
            refuse(c, WFILE_S_NODIR, 0);
            return;
        }
        show(c, s + d);
    }
    if (strlen(c->dir) + n - d >= c->pathsz) {
        refuse(c, WFILE_S_LONG, c->pathsz - 1);
        return;
    }
    strcpy(c->path, c->dir);
    AnsiToOem(s + d, c->path + strlen(c->dir));
    EndDialog(c->dlg, WFILE_OK);
}

/* Caption the dialog, fill the list from the task's directory with the
   pattern and put the pattern into the name. */
static void init(fl_dlg *c)
{
    char t[MSG_MAXLEN + 1];

    msg_get(c->title, t, (u16)sizeof t);
    OemToAnsi(t, t);
    SetWindowText(c->dlg, t);
    SendDlgItemMessage(c->dlg, WFILE_C_NAME, EM_LIMITTEXT, c->pathsz - 1,
                       0L);
    cwd(c->home);
    strcpy(c->dir, c->home);
    strcpy(t, c->spec);
    list(c, t);
    show(c, c->spec);
}

BOOL WCALLBACK wfile_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    fl_dlg *c;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    c = fl_cur;
    if (wm == WM_INITDIALOG) {
        c->dlg = dlg;
        wd_centre(dlg);
        init(c);
        return FALSE;
    }
    if (c == NULL || c->dlg != dlg || wm != WM_COMMAND)
        return FALSE;
    if (wp == WFILE_C_LIST && HIWORD(lp) == LBN_SELCHANGE)
        chosen(c);
    else if (wp == IDOK || (wp == WFILE_C_LIST && HIWORD(lp) == LBN_DBLCLK))
        ok(c);
    else if (wp == IDCANCEL)
        EndDialog(dlg, WFILE_CANCEL);
    else
        return FALSE;
    return TRUE;
}

int wfile_run(HWND owner, u16 title, const char *spec, char *path,
              u16 pathsz)
{
    HINSTANCE inst;
    FARPROC proc;
    fl_dlg c;
    int r;

    if (fl_cur != NULL)
        return WFILE_EFAIL;
    c.path = path;
    c.pathsz = pathsz;
    c.title = title;
    txt_copy(c.spec, spec, (u16)sizeof c.spec);
    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    proc = MakeProcInstance((FARPROC)wfile_proc, inst);
    if (proc == NULL)
        return WFILE_EFAIL;
    c.dlg = NULL;
    fl_cur = &c;
    r = DialogBoxParam(inst, MAKEINTRESOURCE(WFILE_R_DLG), owner,
                       (DLGPROC)proc, 0L);
    fl_cur = NULL;
    FreeProcInstance(proc);
    return r < 0 ? WFILE_EFAIL : r;
}

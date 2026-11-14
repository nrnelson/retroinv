/*
 * WSETUP.C - test of SETUP, the installer of the Windows client
 * (SRC/WIN/SETUP.C), on the Windows 3.0 test machine.
 *
 * usage: WIN WSETUP   (from the directory holding SETUP.EXE, C:\RUN)
 *
 * It writes beside SETUP.EXE a release's files of its own making, the
 * list INS_LIST and the Windows client's files it names, then starts
 * SETUP again and again with WinExec and drives it as a user would:
 * it types into the dialog's fields, chooses its buttons and answers
 * its message boxes, each found as a visible, enabled top-level window
 * captioned with SETUP's title, a box being one without the directory's
 * field, its keys and commands posted to SETUP's queue.  Each box's text
 * must be the string of SETUP.RC that the step expects, filled in with
 * its arguments, read from the running SETUP's own string table.
 * Program Manager, the machine's shell, keeps its group of RETROINV in
 * RETROINV.GRP of the Windows directory, which shows the group and its
 * item's command line.
 *
 * Checks, in order:
 * - Program Manager runs; SETUP /X shows its usage, and a SETUP with no
 *   list beside it says so, naming the list's path;
 * - a SETUP whose list has a line naming ..\VICTIM.TXT says the list
 *   cannot be read, both to install and with /U, and leaves VICTIM.TXT
 *   of the directory above, where that line points, as it was;
 * - an installation into a new directory C:\RUN\INST typed in lower
 *   case, with switches between blanks: the files of the list's W lines
 *   and the list are there with the bytes of their sources, one of two
 *   copy blocks among them, an earlier INVWIN.HLP replaced, the settings
 *   and another file kept, no temporary file left, and the group's item
 *   starts INVWIN with the switches;
 * - a directory and switches Program Manager could not take are
 *   refused, as is SETUP's own directory, which holds a file
 *   compressed, the dialog staying, and Cancel changes nothing;
 * - SETUP started from that directory offers it and, Install chosen
 *   with nothing typed, installs into it: it copies nothing, and the
 *   group is made afresh with one item, without the switches of the
 *   first run;
 * - a source file missing and a read-only file in the way each stop the
 *   copy with their reason, the files before them copied, no temporary
 *   file left, the earlier file kept and the group unchanged;
 * - with Program Manager deaf to WM_DDE_INITIATE (its top-level windows
 *   subclassed for the step), the files still install and SETUP says
 *   how to make the item by hand; SETUP /U then removes the files and
 *   says to delete the group by hand;
 * - SETUP /U answered No removes nothing; answered Yes, it removes the
 *   group and the Windows client's files, but SETUP.EXE, the list, the
 *   settings, another file and INV.MSG, which a DOS installation there
 *   (its README.TXT) shares;
 * - no other dialog, such as an error of Program Manager's, is left
 *   open after a step.
 * INVWIN.HLP is on the disk compressed, as INVWIN.HL_ with no plain copy
 * beside it, so every installation expands it.  While each message box
 * is up, the deepest SETUP goes, WSETUP counts the bytes at the low end
 * of SETUP's stack that still hold cstk_fill's pattern: between the
 * stack's lowest address, which Windows keeps in the task's DGROUP at
 * WS_STACKTOP, and its highest, at WS_STACKBOT, skipping the bytes below
 * the start-up code's _STACKLOW.
 *
 * Writes each failure and a summary line into WSETUP.OUT (TEST/WTEST.C),
 * with each message box's text as a "WSETUP SAID" line, and the fewest
 * bytes of SETUP's stack left unused as a "SETUP STACK" line, which
 * TEST/WIN/wintest.sh holds to its margin; status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include <dde.h>
#include "SETUPL.H"
#include "SETUP.H"
#include "TESTFW.H"

#define WS_WAITMS       30000   /* the longest wait for SETUP, ms       */
#define WS_TEXTMAX      1024    /* bytes of a box's text                */
#define WS_LINEMAX      256     /* bytes of a string of SETUP.RC        */
#define WS_GRPMAX       2048    /* bytes of a group file read           */
#define WS_MAXPM        8       /* Program Manager's windows muted      */
#define WS_BIGLEN       4500    /* bytes of INVWIN.EXE, two blocks      */
#define WS_LEN          100     /* bytes of each other file             */
#define WS_STACKTOP     0x0A    /* DGROUP word: the stack's lowest byte */
#define WS_STACKBOT     0x0E    /* DGROUP word: the byte above its top  */
#define WS_SZDDHDR      14      /* bytes of a compressed file's header  */
#define WS_LITS         8       /* literals after each flag byte        */

static const char k_list[] =
    "RETROINV TEST\r\n"
    "720 RETROINV1 RETROINV2\r\n"
    "2\r\n"
    "SVNT 12 100 README.TXT\r\n"
    "SVNT 12 100 INSTALL.LST\r\n"
    "SVNTW 12 100 INV.MSG\r\n"
    "W 2 4500 INVWIN.EXE\r\n"
    "WZ 2 100 INVWIN.HLP\r\n"
    "W 2 100 INVWIN.HTX\r\n"
    "W 2 100 SETUP.EXE\r\n"
    "D 1 100 DATA\r\n";
/* The Windows client's files of k_list but SETUP.EXE, which is real. */
static const char *const k_files[] = {
    "INV.MSG", "INVWIN.EXE", "INVWIN.HLP", "INVWIN.HTX"
};
static const char k_old[] = "OLD";
static const char k_cmd[] = "C:\\RUN\\INST\\" SU_PROG;
static const char k_net[] = " /NET RETROINV";

static HINSTANCE g_inst;                /* the SETUP running            */
static char g_title[WS_LINEMAX];        /* its title                    */
static char g_grp[DOS_PATHMAX];         /* the group's file             */
static HWND g_pm[WS_MAXPM];             /* Program Manager's, muted     */
static WNDPROC g_pmproc[WS_MAXPM];      /* their own procedures         */
static int g_npm;
static int g_lost;                      /* 1 once SETUP stops answering */
static FARPROC g_mute;                  /* ws_mute's instance thunk     */
static char g_want[WS_BIGLEN];          /* a release file's bytes       */
static char g_got[WS_BIGLEN + 1];       /* a file read back             */
static u16 g_unused = 0xFFFFu;          /* SETUP's stack never reached  */

/* ---------------------------------------------------------------------
 * Files.
 * --------------------------------------------------------------------- */

/* The bytes of file name, len of them: its name over and over. */
static void content(char *buf, const char *name, int len)
{
    int n;
    int i;

    n = (int)strlen(name);
    for (i = 0; i < len; i++)
        buf[i] = name[i % n];
}

/* Write the n bytes at p as the file path: 1 when written. */
static int put(const char *path, const char *p, u16 n)
{
    int h;
    int rc;

    if (cf_create(path, &h) != 0)
        return 0;
    rc = cf_write(h, p, n);
    cf_close(h);
    return rc == 0;
}

/* Read the file path into buf (size bytes): its length, -1 when it
   cannot be read. */
static int get(const char *path, char *buf, u16 size)
{
    u16 got;
    int h;
    int rc;

    if (cf_open(path, CF_READ | CF_DENYNONE, &h) != 0)
        return -1;
    rc = cf_read(h, buf, size, &got);
    cf_close(h);
    return rc == 0 ? (int)got : -1;
}

/* The file name of the directory dir into path (DOS_PATHMAX bytes). */
static void join(char *path, const char *dir, const char *name)
{
    strcpy(path, dir);
    strcat(path, "\\");
    strcat(path, name);
}

/* The release's bytes of the file name into g_want: their count. */
static int release(const char *name)
{
    int len;

    len = strcmp(name, SU_PROG) == 0 ? WS_BIGLEN : WS_LEN;
    content(g_want, name, len);
    return len;
}

/* Write the release file name into dir. */
static void make(const char *dir, const char *name)
{
    char path[DOS_PATHMAX];
    int len;

    len = release(name);
    join(path, dir, name);
    TF_CHECK(put(path, g_want, (u16)len));
}

/* 1 when the file name of dir holds the n bytes at p. */
static int holds(const char *dir, const char *name, const char *p, int n)
{
    char path[DOS_PATHMAX];

    join(path, dir, name);
    return get(path, g_got, sizeof g_got) == n &&
           memcmp(g_got, p, (size_t)n) == 0;
}

/* Write the release file name into C:\RUN compressed, all literals,
   under its compressed name, the plain one removed. */
static void make_packed(const char *name)
{
    char path[DOS_PATHMAX];
    char packed[DOS_FINDNAME];
    int len;
    int n;
    int i;

    len = release(name);
    memcpy(g_got, "SZDD\x88\xF0\x27\x33" "A_", WS_SZDDHDR - 4);
    put_le32((u8 *)g_got + WS_SZDDHDR - 4, (u32)len);
    for (n = WS_SZDDHDR, i = 0; i < len; i++) {
        if (i % WS_LITS == 0)
            g_got[n++] = (char)0xFF;
        g_got[n++] = g_want[i];
    }
    strcpy(packed, name);
    lz_name(packed);
    join(path, "C:\\RUN", packed);
    TF_CHECK(put(path, g_got, (u16)n));
    join(path, "C:\\RUN", name);
    TF_EQ(cf_remove(path), 0);
}

/* Check that the file name of dir is the release's, as make wrote it. */
static void is_release(const char *dir, const char *name)
{
    int len;

    len = release(name);
    if (!TF_CHECK(holds(dir, name, g_want, len)))
        printf("  not the release's: %s\\%s\n", dir, name);
}

/* Check that the file name of dir is there (want 1) or not (0). */
static void is_there(const char *dir, const char *name, int want)
{
    char path[DOS_PATHMAX];

    join(path, dir, name);
    if (!TF_EQ(cf_exists(path), want))
        printf("  %s\n", path);
}

/* Copy the file name of C:\RUN into dir. */
static void copy(const char *name, const char *dir)
{
    char path[DOS_PATHMAX];
    u16 got;
    int in;
    int out;
    int rc;

    join(path, "C:\\RUN", name);
    if (!TF_EQ(cf_open(path, CF_READ | CF_DENYNONE, &in), 0))
        return;
    join(path, dir, name);
    rc = cf_create(path, &out);
    if (TF_EQ(rc, 0)) {
        do {
            rc = cf_read(in, g_got, sizeof g_got, &got);
            if (rc == 0 && got != 0)
                rc = cf_write(out, g_got, got);
        } while (rc == 0 && got != 0);
        TF_EQ(rc, 0);
        cf_close(out);
    }
    cf_close(in);
}

/* The group's file into buf (WS_GRPMAX bytes): its length, or -1 when
   there is none. */
static int group(char *buf)
{
    return get(g_grp, buf, WS_GRPMAX);
}

/* 1 when the n bytes at p hold the string s. */
static int has(const char *p, int n, const char *s)
{
    int k;
    int i;

    k = (int)strlen(s);
    for (i = 0; i + k <= n; i++)
        if (memcmp(p + i, s, (size_t)k) == 0)
            return 1;
    return 0;
}

/* Check that the group's file is the n bytes at was. */
static void group_same(const char *was, int n)
{
    TF_CHECK(group(g_got) == n && memcmp(g_got, was, (size_t)n) == 0);
}

/* ---------------------------------------------------------------------
 * SETUP's windows.
 * --------------------------------------------------------------------- */

/* Dispatch WSETUP's messages and let the other tasks run. */
static void pump(void)
{
    MSG m;

    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        DispatchMessage(&m);
}

/* The first top-level window, Program Manager's frame being one. */
static HWND first(void)
{
    return GetWindow(FindWindow("Progman", NULL), GW_HWNDFIRST);
}

/* The visible, enabled top-level window captioned SETUP's title that
   is SETUP's dialog (dlg 1) or not (0), or NULL. */
static HWND find(int dlg)
{
    char t[WS_LINEMAX];
    HWND w;

    for (w = first(); w != NULL; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w) || !IsWindowEnabled(w))
            continue;
        GetWindowText(w, t, sizeof t);
        if (strcmp(t, g_title) == 0 &&
            (GetDlgItem(w, SU_C_DIR) != NULL) == dlg)
            return w;
    }
    return NULL;
}

/* Wait for find(dlg), at most WS_WAITMS: the window, or NULL. */
static HWND wait_for(int dlg)
{
    DWORD t0;
    HWND w;

    t0 = GetTickCount();
    do {
        pump();
        w = find(dlg);
    } while (w == NULL && GetTickCount() - t0 < WS_WAITMS);
    if (!TF_CHECK(w != NULL)) {
        g_lost = 1;
        printf("  no %s; SETUP's dialog %s\n", dlg ? "dialog" : "box",
               find(1) != NULL ? "waits" : "does not wait");
    }
    return w;
}

/* Start SETUP with the command line cmd: 1 when it started. */
static int start(const char *cmd)
{
    UINT rc;

    if (!TF_CHECK(!g_lost))
        return 0;
    rc = WinExec(cmd, SW_SHOWNORMAL);
    g_inst = rc > 32 ? (HINSTANCE)rc : NULL;
    if (!TF_CHECK(g_inst != NULL))
        return 0;
    LoadString(g_inst, SU_S_TITLE, g_title, sizeof g_title);
    return TF_CHECK(g_title[0] != '\0');
}

/* Wait for SETUP to end, at most WS_WAITMS, then check that no dialog
   is left open, of SETUP or of Program Manager. */
static void wait_end(void)
{
    char cls[WS_LINEMAX];
    DWORD t0;
    HWND w;

    t0 = GetTickCount();
    do
        pump();
    while (GetModuleHandle("SETUP") != NULL &&
           GetTickCount() - t0 < WS_WAITMS);
    TF_CHECK(GetModuleHandle("SETUP") == NULL);
    for (w = first(); w != NULL; w = GetWindow(w, GW_HWNDNEXT)) {
        GetClassName(w, cls, sizeof cls);
        if (!TF_CHECK(!IsWindowVisible(w) || strcmp(cls, "#32770") != 0)) {
            GetWindowText(w, cls, sizeof cls);
            printf("  a dialog is open: %s\n", cls);
        }
    }
}

/* Post SETUP's window w the message m, wp, lp, again while its queue,
   which holds 8 messages, is full, at most WS_WAITMS. */
static void post(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    DWORD t0;
    BOOL ok;

    t0 = GetTickCount();
    while (!(ok = PostMessage(w, m, wp, lp)) &&
           GetTickCount() - t0 < WS_WAITMS)
        pump();
    TF_CHECK(ok);
}

/* Type text into the edit id of the dialog d as a user does, over its
   whole text selected, or delete that text for "".  The edit keeps its
   text in SETUP's own data, which WM_SETTEXT from another task does not
   reach, so its keys are posted. */
static void type(HWND d, int id, const char *text)
{
    HWND e;

    e = GetDlgItem(d, id);
    post(e, EM_SETSEL, 0, MAKELPARAM(0, 0x7FFF));
    if (*text == '\0')
        post(e, WM_CHAR, '\b', 0L);
    for (; *text != '\0'; text++)
        post(e, WM_CHAR, (WPARAM)(u8)*text, 0L);
}

/* Wait for SETUP's dialog, type dir and opts into its fields, each
   unless NULL, and choose the button choice. */
static void fill(const char *dir, const char *opts, int choice)
{
    HWND d;

    d = wait_for(1);
    if (d == NULL)
        return;
    if (dir != NULL)
        type(d, SU_C_DIR, dir);
    if (opts != NULL)
        type(d, SU_C_OPTS, opts);
    post(d, WM_COMMAND, (WPARAM)choice, 0L);
}

/* Count the bytes at the low end of the stack of the SETUP running that
   still hold CSTK_PAT, and keep the fewest in g_unused. */
static void stack_seen(void)
{
    const BYTE FAR *d;
    u16 p;
    u16 end;
    u16 n;

    d = (const BYTE FAR *)GlobalLock((HGLOBAL)g_inst);
    if (!TF_CHECK(d != NULL))
        return;
    p = *(const WORD FAR *)(d + WS_STACKTOP);
    end = *(const WORD FAR *)(d + WS_STACKBOT);
    while (p < end && d[p] != CSTK_PAT)
        p++;
    for (n = 0; p < end && d[p] == CSTK_PAT; p++)
        n++;
    GlobalUnlock((HGLOBAL)g_inst);
    if (n < g_unused)
        g_unused = n;
}

/* Wait for SETUP's message box, check that its text is the string id
   of SETUP.RC filled in from a, b and c, and choose its button choice:
   for IDOK, the lone button of an MB_OK box, which Windows 3.0 numbers
   IDCANCEL. */
static void answer(int id, const char *a, const char *b, const char *c,
                   int choice)
{
    char f[WS_LINEMAX];
    char want[WS_TEXTMAX];
    char got[WS_TEXTMAX];
    char *p;
    HWND box;
    HWND w;

    box = wait_for(0);
    if (box == NULL)
        return;
    stack_seen();
    LoadString(g_inst, id, f, sizeof f);
    wsprintf(want, f, (LPCSTR)a, (LPCSTR)b, (LPCSTR)c);
    got[0] = '\0';
    for (w = GetWindow(box, GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT)) {
        GetClassName(w, f, sizeof f);
        if (lstrcmpi(f, "Static") == 0 &&
            GetWindowTextLength(w) > (int)strlen(got))
            GetWindowText(w, got, sizeof got);
    }
    for (p = got; *p != '\0'; p++)
        if (*p == '\n')
            *p = '|';
    printf("WSETUP SAID %s\n", got);
    for (p = want; *p != '\0'; p++)
        if (*p == '\n')
            *p = '|';
    TF_STREQ(got, want);
    if (choice == IDOK && GetDlgItem(box, IDOK) == NULL)
        choice = IDCANCEL;
    post(box, WM_COMMAND, (WPARAM)choice, 0L);
}

/* ---------------------------------------------------------------------
 * Program Manager made deaf.
 * --------------------------------------------------------------------- */

/* The procedure of Program Manager's top-level windows while muted: it
   drops WM_DDE_INITIATE.  It runs on Program Manager's stack, so it
   takes the address of no local. */
LRESULT WCALLBACK ws_mute(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    int i;

    for (i = 0; i < g_npm - 1 && g_pm[i] != w; i++)
        ;
    if (m == WM_DDE_INITIATE)
        return 0;
    return CallWindowProc(g_pmproc[i], w, m, wp, lp);
}

/* Mute Program Manager's top-level windows (on 1) or give them their
   own procedures back (0). */
static void mute(int on)
{
    HTASK task;
    HWND w;

    if (!on) {
        while (g_npm > 0) {
            g_npm--;
            SetWindowLong(g_pm[g_npm], GWL_WNDPROC,
                          (LONG)g_pmproc[g_npm]);
        }
        FreeProcInstance(g_mute);
        return;
    }
    task = GetWindowTask(FindWindow("Progman", NULL));
    g_mute = MakeProcInstance((FARPROC)ws_mute, (HINSTANCE)dos_inst);
    for (w = first(); w != NULL && g_npm < WS_MAXPM;
         w = GetWindow(w, GW_HWNDNEXT)) {
        if (GetWindowTask(w) != task)
            continue;
        g_pm[g_npm] = w;
        g_pmproc[g_npm++] = (WNDPROC)SetWindowLong(w, GWL_WNDPROC,
                                                   (LONG)g_mute);
    }
    TF_CHECK(g_npm > 0);
}

/* ---------------------------------------------------------------------
 * The steps.
 * --------------------------------------------------------------------- */

static void test_start(void)
{
    char path[DOS_PATHMAX];
    DWORD t0;
    int i;

    t0 = GetTickCount();
    while (FindWindow("Progman", NULL) == NULL &&
           GetTickCount() - t0 < WS_WAITMS)
        pump();
    TF_CHECK(FindWindow("Progman", NULL) != NULL);
    GetWindowsDirectory(g_grp, sizeof g_grp);
    join(path, g_grp, SU_NAME ".GRP");
    strcpy(g_grp, path);
    TF_CHECK(put(INS_LIST, k_list, (u16)(sizeof k_list - 1)));
    for (i = 0; i < (int)(sizeof k_files / sizeof k_files[0]); i++)
        make("C:\\RUN", k_files[i]);
    make_packed("INVWIN.HLP");
    TF_EQ(cf_mkdir("C:\\RUN\\BARE"), 0);
    copy("SETUP.EXE", "C:\\RUN\\BARE");
    if (start("SETUP /X")) {
        answer(SU_S_USAGE, NULL, NULL, NULL, IDOK);
        wait_end();
    }
    if (start("C:\\RUN\\BARE\\SETUP.EXE")) {
        answer(SU_S_NOLIST, "C:\\RUN\\BARE\\" INS_LIST, NULL, NULL, IDOK);
        wait_end();
    }
}

static void test_damaged(void)
{
    static const char bad[] = "W 2 1 ..\\VICTIM.TXT\r\n";
    static const char *const cmds[] = {
        "C:\\RUN\\BAD\\SETUP.EXE", "C:\\RUN\\BAD\\SETUP.EXE /U"
    };
    int i;

    TF_EQ(cf_mkdir("C:\\RUN\\BAD"), 0);
    copy("SETUP.EXE", "C:\\RUN\\BAD");
    strcpy(g_got, k_list);
    strcat(g_got, bad);
    TF_CHECK(put("C:\\RUN\\BAD\\" INS_LIST, g_got, (u16)strlen(g_got)));
    TF_CHECK(put("C:\\RUN\\VICTIM.TXT", k_old, 3));
    for (i = 0; i < (int)(sizeof cmds / sizeof cmds[0]); i++)
        if (start(cmds[i])) {
            answer(SU_S_NOLIST, "C:\\RUN\\BAD\\" INS_LIST, NULL, NULL,
                   IDOK);
            wait_end();
        }
    TF_CHECK(holds("C:\\RUN", "VICTIM.TXT", k_old, 3));
}

static void test_install(void)
{
    char grp[WS_GRPMAX];
    int i;
    int n;

    TF_EQ(cf_mkdir("C:\\RUN\\INST"), 0);
    TF_CHECK(put("C:\\RUN\\INST\\KEEP.TXT", "KEEP", 4));
    TF_CHECK(put("C:\\RUN\\INST\\INV.CFG", "CFG", 3));
    TF_CHECK(put("C:\\RUN\\INST\\INVWIN.HLP", k_old, 3));
    TF_EQ(group(grp), -1);
    if (start("SETUP.EXE")) {
        fill("c:\\run\\inst", k_net, IDOK);
        answer(SU_S_DONE, "C:\\RUN\\INST", NULL, NULL, IDOK);
        wait_end();
    }
    for (i = 0; i < (int)(sizeof k_files / sizeof k_files[0]); i++)
        is_release("C:\\RUN\\INST", k_files[i]);
    TF_CHECK(holds("C:\\RUN\\INST", INS_LIST, k_list,
                   (int)sizeof k_list - 1));
    is_there("C:\\RUN\\INST", "SETUP.EXE", 1);
    TF_CHECK(holds("C:\\RUN\\INST", "KEEP.TXT", "KEEP", 4));
    TF_CHECK(holds("C:\\RUN\\INST", "INV.CFG", "CFG", 3));
    is_there("C:\\RUN\\INST", SU_TEMP, 0);
    n = group(grp);
    TF_CHECK(n > 0 && has(grp, n, SU_NAME));
    if (!TF_CHECK(n > 0 && has(grp, n, "C:\\RUN\\INST\\" SU_PROG
                                        " /NET RETROINV")))
        printf("  the group's file: %d bytes\n", n);
}

static void test_refuse(void)
{
    char was[WS_GRPMAX];
    int n;

    n = group(was);
    if (start("SETUP.EXE")) {
        fill("C:\\RUN\\A(B)", "", IDOK);
        answer(SU_S_BADDIR, NULL, NULL, NULL, IDOK);
        fill("C:\\RUN\\INST", "/NET A,B", IDOK);
        answer(SU_S_BADOPTS, NULL, NULL, NULL, IDOK);
        fill("C:\\RUN", "", IDOK);
        answer(SU_S_PACKED, "C:\\RUN", NULL, NULL, IDOK);
        fill(NULL, NULL, IDCANCEL);
        wait_end();
    }
    group_same(was, n);
}

static void test_same(void)
{
    char grp[WS_GRPMAX];
    int n;

    TF_EQ(cf_remove("C:\\RUN\\INST\\INVWIN.HTX"), 0);
    if (start("C:\\RUN\\INST\\SETUP.EXE")) {
        fill(NULL, NULL, IDOK);
        answer(SU_S_DONE, "C:\\RUN\\INST", NULL, NULL, IDOK);
        wait_end();
    }
    is_there("C:\\RUN\\INST", "INVWIN.HTX", 0);
    n = group(grp);
    TF_CHECK(n > 0 && has(grp, n, k_cmd));
    TF_CHECK(n > 0 && !has(grp, n, k_net));
}

static void test_fail(void)
{
    char was[WS_GRPMAX];
    char why[WS_LINEMAX];
    HFILE f;
    int n;

    n = group(was);
    TF_EQ(cf_rename("C:\\RUN\\INVWIN.HTX", "C:\\RUN\\HTX.SAV"), 0);
    if (start("SETUP.EXE")) {
        fill("C:\\RUN\\INST2", "", IDOK);
        LoadString(g_inst, SU_S_RNOENT, why, sizeof why);
        answer(SU_S_NOCOPY, "INVWIN.HTX", "C:\\RUN\\INST2", why, IDOK);
        fill(NULL, NULL, IDCANCEL);
        wait_end();
    }
    TF_EQ(cf_rename("C:\\RUN\\HTX.SAV", "C:\\RUN\\INVWIN.HTX"), 0);
    is_release("C:\\RUN\\INST2", "INV.MSG");
    is_release("C:\\RUN\\INST2", SU_PROG);
    is_release("C:\\RUN\\INST2", "INVWIN.HLP");
    is_there("C:\\RUN\\INST2", "INVWIN.HTX", 0);
    is_there("C:\\RUN\\INST2", "SETUP.EXE", 0);
    is_there("C:\\RUN\\INST2", SU_TEMP, 0);
    f = _lcreat("C:\\RUN\\INST2\\INVWIN.HTX", 1);
    TF_CHECK(f != HFILE_ERROR);
    if (f != HFILE_ERROR) {
        _lwrite(f, k_old, 3);
        _lclose(f);
    }
    if (start("SETUP.EXE")) {
        fill("C:\\RUN\\INST2", "", IDOK);
        LoadString(g_inst, SU_S_RUSED, why, sizeof why);
        answer(SU_S_NOCOPY, "INVWIN.HTX", "C:\\RUN\\INST2", why, IDOK);
        fill(NULL, NULL, IDCANCEL);
        wait_end();
    }
    TF_CHECK(holds("C:\\RUN\\INST2", "INVWIN.HTX", k_old, 3));
    is_there("C:\\RUN\\INST2", SU_TEMP, 0);
    group_same(was, n);
}

static void test_noprogman(void)
{
    char was[WS_GRPMAX];
    char why[WS_LINEMAX];
    char gone[WS_LINEMAX];
    int i;
    int n;

    n = group(was);
    mute(1);
    if (start("SETUP.EXE")) {
        fill("C:\\RUN\\INST3", "/COM1", IDOK);
        LoadString(g_inst, SU_S_NOPM, why, sizeof why);
        answer(SU_S_BYHAND, why, "C:\\RUN\\INST3",
               "C:\\RUN\\INST3\\" SU_PROG " /COM1", IDOK);
        wait_end();
    }
    for (i = 0; i < (int)(sizeof k_files / sizeof k_files[0]); i++)
        is_release("C:\\RUN\\INST3", k_files[i]);
    if (start("C:\\RUN\\INST3\\SETUP.EXE /U")) {
        answer(SU_S_ASK, "C:\\RUN\\INST3", NULL, NULL, IDYES);
        LoadString(g_inst, SU_S_NOGROUP, gone, sizeof gone);
        answer(SU_S_GONE, "C:\\RUN\\INST3", gone, NULL, IDOK);
        wait_end();
    }
    mute(0);
    for (i = 0; i < (int)(sizeof k_files / sizeof k_files[0]); i++)
        is_there("C:\\RUN\\INST3", k_files[i], 0);
    is_there("C:\\RUN\\INST3", "SETUP.EXE", 1);
    is_there("C:\\RUN\\INST3", INS_LIST, 1);
    group_same(was, n);
}

static void test_remove(void)
{
    char was[WS_GRPMAX];
    int n;

    n = group(was);
    if (start("C:\\RUN\\INST\\SETUP.EXE -u")) {
        answer(SU_S_ASK, "C:\\RUN\\INST", NULL, NULL, IDNO);
        wait_end();
    }
    is_release("C:\\RUN\\INST", SU_PROG);
    group_same(was, n);
    make("C:\\RUN\\INST", "README.TXT");
    if (start("C:\\RUN\\INST\\SETUP.EXE /U")) {
        answer(SU_S_ASK, "C:\\RUN\\INST", NULL, NULL, IDYES);
        answer(SU_S_GONE, "C:\\RUN\\INST", "", NULL, IDOK);
        wait_end();
    }
    is_there("C:\\RUN\\INST", SU_PROG, 0);
    is_there("C:\\RUN\\INST", "INVWIN.HLP", 0);
    is_release("C:\\RUN\\INST", "INV.MSG");
    is_release("C:\\RUN\\INST", "README.TXT");
    is_there("C:\\RUN\\INST", "SETUP.EXE", 1);
    is_there("C:\\RUN\\INST", INS_LIST, 1);
    is_there("C:\\RUN\\INST", "KEEP.TXT", 1);
    is_there("C:\\RUN\\INST", "INV.CFG", 1);
    TF_EQ(group(was), -1);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    tf_begin("WSETUP");
    test_start();
    test_damaged();
    test_install();
    test_refuse();
    test_same();
    test_fail();
    test_noprogman();
    test_remove();
    printf("SETUP STACK %u\n", g_unused);
    return tf_done();
}

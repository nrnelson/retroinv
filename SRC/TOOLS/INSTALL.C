/*
 * INSTALL.C - the installer of the release disks.
 *
 *   INSTALL            install from the disk or directory it is in
 *   INSTALL /U [dir]   remove an installation, by default the one in
 *                      the directory INSTALL is in
 *
 * INSTALL asks its questions on standard input and output, one line
 * each, so that any display serves and a test can give the answers
 * from a file: what the PC is (the roles of its set's list INSTPLAN.H
 * offers), whether the Windows client goes along where the set has it,
 * the directory, and for a PC that keeps the data whether it starts
 * from the sample data; then whether to replace each settings file an
 * earlier installation left there.  Enter takes the default in
 * brackets; Esc or Ctrl+C at any question leaves.  Every question comes
 * before the first file is copied.  INSTPLAN.C decides what is copied,
 * kept and removed; this file only asks and tells.  A console is read
 * a key at a time in raw mode, with the Ctrl+C and Ctrl+Break checks
 * off, so that both reach the questions as keys; a file is read a line
 * at a time, its end taken as Esc.  Nothing outside the directory is
 * touched.
 *
 * Exit status: 0 done; 1 usage error; 2 a step failed (the files copied
 * before it stay); 3 the user left at a question; 4 DOS older than
 * 3.1.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "INSTPLAN.H"

#define IN_EXIT_USAGE   1       /* bad command line                     */
#define IN_EXIT_FAIL    2       /* a step failed                        */
#define IN_EXIT_LEFT    3       /* the user left at a question          */
#define IN_EXIT_DOSVER  4       /* DOS older than 3.1                   */

#define IN_ANSWER       64      /* bytes of an answer, NUL included     */
#define IN_KEY_BS       0x08    /* Backspace                            */
#define IN_KEY_ESC      0x1B    /* Esc                                  */
#define IN_KEY_EXT      0x00    /* first byte of a key with no ASCII    */

static const char k_nl[] = "\r\n";
static const char k_yn[] = " (Y/N) [";
static const char k_defdir[] = "C:\\RETROINV";
static const char k_usage[] = "usage: INSTALL [/U [directory]]\r\n";
static const char k_none[] = "Nothing was installed.";

static u8 in_con;               /* 1 when standard input is a device    */
static char in_drive[3];        /* the source drive, "A:", or ""        */
static char in_dir[DOS_PATHMAX];        /* the target directory         */

/* Write s to standard output. */
static void put(const char *s)
{
    if (*s != '\0')
        cf_write(1, s, (u16)strlen(s));
}

/* Write a, b and c, then a line end. */
static void say(const char *a, const char *b, const char *c)
{
    put(a);
    put(b);
    put(c);
    put(k_nl);
}

/* Read an answer into a (IN_ANSWER bytes), echoed: its length, or -1
   when the user left (Esc, Ctrl+C, or the end of a file).  A CR, an LF
   or both end it; Backspace takes back a character.  A device's failed
   read is tried again: a serial line made the console (CTTY COM1)
   fails each read that the BIOS waits on longer than its time-out. */
static int answer(char *a)
{
    static u8 cr;
    char c;
    u16 got;
    int n;
    int rc;

    n = 0;
    for (;;) {
        rc = cf_read(0, &c, 1, &got);
        if (rc != 0 && in_con)
            continue;
        if (rc != 0 || got == 0)
            return -1;
        if (c == '\n' && cr) {
            cr = 0;
            continue;
        }
        cr = (u8)(c == '\r');
        if (c == '\r' || c == '\n')
            break;
        if (c == IN_KEY_ESC || c == KEY_CTRL('C'))
            return -1;
        if (c == IN_KEY_EXT && in_con) {
            cf_read(0, &c, 1, &got);
        } else if (c == IN_KEY_BS) {
            if (n > 0) {
                n--;
                put("\b \b");
            }
        } else if ((unsigned char)c >= ' ' && n < IN_ANSWER - 1) {
            a[n++] = c;
            cf_write(1, &c, 1);
        }
    }
    put(k_nl);
    a[n] = '\0';
    return n;
}

/* Ask q, offering def, until the answer is one character of ok or
   empty: that character, def for an empty answer, or -1 when the user
   left. */
static int choose(const char *q, char def, const char *ok)
{
    char a[IN_ANSWER];
    char d[2];
    int c;

    d[0] = def;
    d[1] = '\0';
    for (;;) {
        put(q);
        put(d);
        put("]: ");
        c = answer(a);
        if (c < 0)
            return -1;
        if (c == 0)
            return def;
        c = TXT_UPPER(a[0]);
        if (a[1] == '\0' && strchr(ok, c) != NULL)
            return c;
    }
}

/* Ask the yes-or-no question of a, b and c, whose b may be a path,
   offering def (1 yes): 1 yes, 0 no, -1 left. */
static int yes(const char *a, const char *b, const char *c, int def)
{
    char buf[DOS_PATHMAX + IN_ANSWER];

    strcpy(buf, a);
    strcat(buf, b);
    strcat(buf, c);
    strcat(buf, k_yn);
    def = choose(buf, (char)(def ? 'Y' : 'N'), "YN");
    return def < 0 ? -1 : def == 'Y';
}

/* What INSTPLAN.C asks and tells. */
static int ui(int q, const char *s)
{
    char a[IN_ANSWER];

    switch (q) {
    case INS_Q_DISK:
        put("Insert the disk labelled ");
        put(s);
        put(in_drive[0] != '\0' ? " in " : "");
        put(in_drive);
        put(" and press Enter: ");
        return answer(a) < 0 ? -1 : 1;
    case INS_Q_REPLACE:
        return yes("Replace the ", s, " already there with the new one?",
                   0);
    case INS_Q_DATA:
        return yes("Delete your data in ", s, " too?", 0);
    case INS_N_COPY:
        say("  ", s, "");
        return 0;
    case INS_N_WRONG:
        say("That is not the disk asked for.", "", "");
        if (*s != '\0')
            say("It is labelled ", s, ".");
        return 0;
    case INS_N_LEFT:
        say(s, " holds other files, so it stays.", "");
        return 0;
    default:
        return 0;
    }
}

/* Tell what code rc of a step on path means, and return IN_EXIT_FAIL. */
static int failed(int rc, const char *path)
{
    char n[TXT_U32LEN + 1];
    const char *why;

    switch (rc) {
    case CF_EFULL:
        why = "the disk is full";
        break;
    case CF_EWRPROT:
    case CF_EACCES:
        why = "it is write-protected or read-only";
        break;
    case CF_ENOENT:
    case CF_ENOPATH:
        why = "it is not there";
        break;
    case CF_ENOTREADY:
        why = "the drive is not ready";
        break;
    case INS_EFORMAT:
        why = "the file is damaged";
        break;
    case INS_EPACKED:
        why = "it holds the disks' compressed files; install elsewhere";
        break;
    default:
        txt_u32(n, (u32)rc);
        put("DOS error ");
        why = n;
        break;
    }
    say(path, ": ", why);
    return IN_EXIT_FAIL;
}

/* Tell that the user left and return IN_EXIT_LEFT. */
static int left(const char *what)
{
    say("Stopped. ", what, "");
    return IN_EXIT_LEFT;
}

/* KB of n bytes, rounded up, as text into t (TXT_U32LEN + 1 bytes). */
static const char *kb(char *t, u32 n)
{
    txt_u32(t, (n + 1023) / 1024);
    return t;
}

/* Ask for the target directory into in_dir: 0, or -1 when the user
   left. */
static int ask_dir(void)
{
    int n;

    for (;;) {
        put("Install in which directory? [");
        put(k_defdir);
        put("]: ");
        n = answer(in_dir);
        if (n < 0)
            return -1;
        if (n == 0)
            strcpy(in_dir, k_defdir);
        if (ins_dir_ok(in_dir))
            return 0;
        say("Give a whole path below a drive's root, such as ", k_defdir,
            ".");
    }
}

/* Say how to start what roles installed. */
static void done(const ins_list *l, u8 roles)
{
    char thin[DOS_FINDNAME];

    say("RETROINV is installed in ", in_dir, ". To start it, type:");
    in_dir[2] = '\0';
    say("  ", in_dir, "");
    in_dir[2] = '\\';
    say("  CD ", in_dir + 2, "");
    if (roles & INS_R_SERVER)
        say("  INVSERV", k_nl, "Read INVSERV.CFG first: it names the server.");
    else if (roles & INS_R_ALONE)
        say("  INV", "", "");
    if (roles & (INS_R_CLIENT | INS_R_THIN)) {
        ins_prog(l, INS_R_THIN, thin);
        say("  ", roles & INS_R_THIN ? thin : "INV", " /NET RETROINV");
        say("RETROINV is the server's name in its INVSERV.CFG.", "", "");
    }
    if (roles & INS_R_WIN) {
        say("For the Windows client's icon, in Windows choose File, Run in",
            "", "");
        say("Program Manager and run ", in_dir, "\\SETUP.");
    }
}

/* Read the list of the directory src into l, through path
   (DOS_PATHMAX bytes): 1, or 0 when it cannot be read, which is said. */
static int load(ins_list *l, const char *src, char *path)
{
    int rc;

    strcpy(path, src);
    strcat(path, INS_LIST);
    rc = ins_load(l, path);
    if (rc == INS_OK)
        return 1;
    failed(rc, path);
    say("Start INSTALL from a RETROINV disk.", "", "");
    return 0;
}

/* Install from the directory src, the drive of argv[0]. */
static int install(const char *src, u8 drive)
{
    static const char *const menu[] = {
        "  1  This PC alone: INV and the data",
        "  2  The server: INVSERV, the TCP bridge, the tools, INV, the data",
        "  3  A network client: INV",
        "  4  A network client too small for INV: "
    };
    ins_list l;
    char path[DOS_PATHMAX];
    char t[TXT_U32LEN + 1];
    char thin[DOS_FINDNAME];
    char ok[5];
    u32 need;
    u32 have;
    u8 can;
    u8 roles;
    int c;
    int i;
    int rc;

    if (!load(&l, src, path))
        return IN_EXIT_FAIL;
    say(l.title, " installation", "");
    put(k_nl);
    say("What is this PC?", "", "");
    can = ins_roles(&l);
    ins_prog(&l, INS_R_THIN, thin);
    for (c = 0, i = 0; i < 4; i++)
        if (can & (1 << i)) {
            say(menu[i], i == 3 ? thin : "", "");
            ok[c++] = (char)('1' + i);
        }
    ok[c] = '\0';
    if (c == 0)
        return failed(INS_EFORMAT, path);
    c = choose("Your choice [", ok[0], ok);
    if (c < 0)
        return left(k_none);
    roles = (u8)(1 << (c - '1'));
    if (can & INS_R_WIN) {
        c = yes("Also the Windows client, INVWIN?", "", "", 0);
        if (c < 0)
            return left(k_none);
        if (c)
            roles |= INS_R_WIN;
    }
    if (ask_dir() < 0)
        return left(k_none);
    if (roles & (INS_R_ALONE | INS_R_SERVER)) {
        strcpy(path, in_dir);
        strcat(path, "\\" INS_DATA);
        if (ins_has_data(in_dir)) {
            say("Your data in ", path, " stays as it is.");
        } else {
            c = yes("Start with the sample data (N: an empty inventory)?",
                    "", "", 1);
            if (c < 0)
                return left(k_none);
            if (c)
                roles |= INS_R_DATA;
        }
    }
    ins_plan(&l, roles);
    if (ins_keep(&l, in_dir, ui) != INS_OK)
        return left(k_none);
    rc = ins_room(&l, in_dir, &need, &have);
    if (rc == INS_EROOM) {
        put("This needs ");
        put(kb(t, need));
        put(" KB; ");
        put(in_dir);
        say(" has ", kb(t, have), " KB free.");
        return IN_EXIT_FAIL;
    }
    if (rc != INS_OK)
        return failed(rc, in_dir);
    say("Copying to ", in_dir, ":");
    rc = ins_copy(&l, src, drive, in_dir, ui, path);
    if (rc == INS_ECANCEL)
        return left("Run INSTALL again to finish.");
    if (rc != INS_OK)
        return failed(rc, path);
    done(&l, roles);
    return 0;
}

/* Remove the installation in the directory dir. */
static int uninstall(const char *dir)
{
    ins_list l;
    char path[DOS_PATHMAX];
    char win[DOS_FINDNAME];
    int rc;

    strcpy(in_dir, dir);
    if (!ins_dir_ok(in_dir)) {
        say("Name the directory of the installation, such as ", k_defdir,
            ".");
        return IN_EXIT_USAGE;
    }
    strcpy(path, in_dir);
    strcat(path, "\\" INS_LIST);
    rc = ins_load(&l, path);
    if (rc != INS_OK)
        return failed(rc, path);
    ins_prog(&l, INS_R_WIN, win);
    strcpy(path, in_dir);
    strcat(path, "\\");
    strcat(path, win);
    strcat(path, ".EXE");
    if (win[0] != '\0' && cf_exists(path) == 1)
        say("If you added the Windows client's icon, first run SETUP /U",
            k_nl, "in Windows to remove its group.");
    rc = yes("Remove RETROINV from ", in_dir, "?", 1);
    if (rc <= 0)
        return left("Nothing was removed.");
    rc = ins_remove(&l, in_dir, ui, path);
    if (rc == INS_ECANCEL)
        return left("Nothing was removed.");
    if (rc != INS_OK)
        return failed(rc, path);
    say("RETROINV is removed from ", in_dir, ".");
    return 0;
}

int main(int argc, char **argv)
{
    char src[DOS_PATHMAX];
    const char *s;
    u8 drive;
    u8 major;
    u8 minor;
    int was;
    int st;

    if (!dos_version(&major, &minor)) {
        put("INSTALL needs MS-DOS 3.1 or later.\r\n");
        return IN_EXIT_DOSVER;
    }
    s = argc > 1 ? argv[1] : "";
    if ((s[0] == '/' || s[0] == '-') && s[1] == '?' && argc == 2) {
        put(k_usage);
        return 0;
    }
    if (argc > 3 || (argc > 1 && !txt_ieq(s, "/U") && !txt_ieq(s, "-U"))) {
        cf_write(2, k_usage, sizeof k_usage - 1);
        return IN_EXIT_USAGE;
    }
    txt_path_dir(src, sizeof src, argv[0]);
    drive = 0;
    if (src[0] != '\0' && src[1] == ':') {
        in_drive[0] = (char)TXT_UPPER(src[0]);
        in_drive[1] = ':';
        drive = (u8)(in_drive[0] - 'A' + 1);
    }
    dos_crit_install();
    dos_break_off();
    in_con = (u8)(dos_dev_raw(0, 1, &was) == 0);
    if (argc == 1) {
        st = install(src, drive);
    } else {
        if (argc == 2 && src[0] != '\0')
            src[strlen(src) - 1] = '\0';
        st = uninstall(argc == 3 ? argv[2] : src);
    }
    if (in_con)
        dos_dev_raw(0, was, &was);
    dos_break_restore();
    dos_crit_remove();
    return st;
}

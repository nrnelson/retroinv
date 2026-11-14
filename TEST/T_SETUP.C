/*
 * T_SETUP.C - unit test for SETUPL.C, the portable part of SETUP, the
 * installer of the Windows client.
 *
 * usage: T_SETUP dir
 *
 * dir is the scratch directory, which holds a disk, SRC, and a target,
 * DST.  Checks:
 * - su_list_open refuses a text of fewer than three lines or of
 *   INS_LISTMAX bytes or more, takes one a byte shorter, and starts
 *   past them, on lines ending in LF or CR LF, the last one with or
 *   without its LF;
 * - su_list_next gives each file's name and roles, the INS_R_ bit of
 *   each letter of INS_ROLES in its ROLES (W the Windows client's, S, V,
 *   N and T a DOS installation's, D and K); it passes over blank lines,
 *   ends with 0 and stays there, and refuses with -1 a line of fewer
 *   than four words, a name of DOS_FINDNAME characters or more, one
 *   holding a blank and one holding a character of INS_SEPS;
 * - su_dir_ok takes "X:\NAME..." of 8.3 names of letters, digits and
 *   DOS's punctuation, of up to SU_DIRMAX characters, upper-cased and
 *   with the blanks at its ends taken off in place, and refuses a root,
 *   a drive alone, a relative path, an empty, doubled or final '\', a
 *   name or extension too long, an empty extension, a blank, a comma, a
 *   parenthesis, a bracket and a longer path;
 * - su_cmd writes dir\SU_PROG, with the switches after a blank when
 *   there are any, their blanks at both ends taken off, up to
 *   SU_CMDMAX - 1 characters, and refuses a longer line and switches
 *   holding a comma, a parenthesis, a bracket, a double quote or a
 *   control character;
 * - su_dde_add and su_k_delete give Program Manager's commands, the
 *   longest filling SU_DDEMAX exactly;
 * - su_join puts one separator between a directory and a name;
 * - su_copy expands a file marked compressed that the disk holds under
 *   its compressed name, copies one the disk holds under its own name as
 *   it is, as an installation does, and a plain file longer than the
 *   copy's buffer whole, leaving no temporary file; a damaged compressed
 *   file is LZ_EFORMAT, a disk that fills during an expansion CF_EFULL
 *   and a missing file CF_ENOENT, each keeping the earlier copy and
 *   leaving no temporary file;
 * - su_packed finds a W file the list marks compressed under its
 *   compressed name, and nothing in a directory without one.
 *
 * Exit status 0 when every check passes, 1 otherwise.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "SETUPL.H"
#include "TESTFW.H"
#include "T_FILES.H"

static const char k_list[] =
    "RETROINV 1.0\r\n"
    "720 RETROINV1 RETROINV2\r\n"
    "2\r\n"
    "SVNT 12 1000 README.TXT\r\n"
    "SVNTW 12 2000 INV.MSG\r\n"
    "\r\n"
    "W 2 3000 INVWIN.EXE\n"
    "VK 1 40 INVSERV.CFG\n"
    "D 1 5000 DATA\n"
    "W 2 9000 SETUP.EXE";

static char g_big[INS_LISTMAX];

/* Read the next file of l: 1, its name and roles as given. */
static int next_is(su_list *l, const char *name, u8 roles)
{
    char got[DOS_FINDNAME];
    u8 k;

    return TF_EQ(su_list_next(l, got, &k), 1) && TF_STREQ(got, name) &&
           TF_EQ(k, roles);
}

/* The verdict of su_list_next on a list whose one file line is line. */
static int line_rc(const char *line)
{
    char text[INS_LISTMAX];
    char name[DOS_FINDNAME];
    su_list l;
    u8 k;

    strcpy(text, "T\nS\n1\n");
    strcat(text, line);
    TF_EQ(su_list_open(&l, text, (u16)strlen(text)), 1);
    return su_list_next(&l, name, &k);
}

static void test_list(void)
{
    su_list l;
    char name[DOS_FINDNAME];
    u8 k;

    TF_EQ(su_list_open(&l, k_list, 0), 0);
    TF_EQ(su_list_open(&l, "T\nS\n", 4), 0);
    TF_EQ(su_list_open(&l, "T\nS\n1", 5), 1);
    memset(g_big, '\n', sizeof g_big);
    TF_EQ(su_list_open(&l, g_big, INS_LISTMAX - 1), 1);
    TF_EQ(su_list_open(&l, g_big, INS_LISTMAX), 0);
    TF_EQ(su_list_next(&l, name, &k), 0);
    TF_EQ(su_list_open(&l, k_list, (u16)(sizeof k_list - 1)), 1);
    next_is(&l, "README.TXT", INS_R_DOS);
    next_is(&l, "INV.MSG", INS_R_DOS | INS_R_WIN);
    next_is(&l, "INVWIN.EXE", INS_R_WIN);
    next_is(&l, "INVSERV.CFG", INS_R_SERVER | INS_R_KEEP);
    next_is(&l, "DATA", INS_R_DATA);
    next_is(&l, "SETUP.EXE", INS_R_WIN);
    TF_EQ(su_list_next(&l, name, &k), 0);
    TF_EQ(su_list_next(&l, name, &k), 0);
    TF_EQ(line_rc("W 2 3000 INVWIN.EXE\n"), 1);
    TF_EQ(line_rc("W 2 INVWIN.EXE\n"), -1);
    TF_EQ(line_rc("W 2 3000 \n"), -1);
    TF_EQ(line_rc("W 2 3000 ABCDEFGH.IJK\n"), 1);
    TF_EQ(line_rc("W 2 3000 ABCDEFGHI.JKL\n"), -1);
    TF_EQ(line_rc("W 2 3000 INVWIN.EXE X\n"), -1);
    TF_EQ(line_rc("W 2 3000 ..\\X.EXE\n"), -1);
    TF_EQ(line_rc("W 2 3000 \\X\n"), -1);
    TF_EQ(line_rc("W 2 3000 C:X\n"), -1);
    TF_EQ(line_rc("W 2 3000 A/B\n"), -1);
}

/* su_dir_ok on a copy of dir: its verdict, and the copy as it leaves
   it in out (DOS_PATHMAX bytes). */
static int dir_rc(const char *dir, char *out)
{
    strcpy(out, dir);
    return su_dir_ok(out);
}

static void test_dir(void)
{
    static const char *const bad[] = {
        "C:\\", "C:", "RETROINV", "\\RETROINV", "1:\\RETROINV", "",
        "C:RETROINV", "C:\\\\RETROINV", "C:\\RETROINV\\", "C:\\A\\\\B",
        "C:\\ABCDEFGHI", "C:\\A.BCDE", "C:\\A.", "C:\\.A", "C:\\A B",
        "C:\\A,B", "C:\\A(B)", "C:\\A[B]", "C:\\A\"B", "C:\\A*",
        "C:\\A.B.C"
    };
    char d[DOS_PATHMAX];
    int i;

    TF_EQ(dir_rc("c:\\retroinv", d), 1);
    TF_STREQ(d, "C:\\RETROINV");
    TF_EQ(dir_rc("  C:\\APPS\\RETRO.INV  ", d), 1);
    TF_STREQ(d, "C:\\APPS\\RETRO.INV");
    TF_EQ(dir_rc("D:\\$%'-_@~`.!{}\\^#&", d), 1);
    TF_EQ(dir_rc("C:\\ABCDEFGH.IJK\\1", d), 1);
    for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++)
        if (!TF_EQ(dir_rc(bad[i], d), 0))
            printf("  refused: \"%s\"\n", bad[i]);
    strcpy(d, "C:");
    while (strlen(d) + 2 <= SU_DIRMAX)
        strcat(d, "\\A");
    if (strlen(d) < SU_DIRMAX)
        strcat(d, "B");
    TF_EQ(strlen(d), SU_DIRMAX);
    TF_EQ(su_dir_ok(d), 1);
    strcat(d, "C");
    TF_EQ(su_dir_ok(d), 0);
}

static void test_cmd(void)
{
    static const char bad[] = ",()[]\"\t\x01";
    char cmd[SU_CMDMAX];
    char opts[SU_CMDMAX + 1];
    int i;

    TF_EQ(su_cmd(cmd, "C:\\RETROINV", ""), 1);
    TF_STREQ(cmd, "C:\\RETROINV\\" SU_PROG);
    TF_EQ(su_cmd(cmd, "C:\\RETROINV", "   "), 1);
    TF_STREQ(cmd, "C:\\RETROINV\\" SU_PROG);
    TF_EQ(su_cmd(cmd, "C:\\RETROINV", "  /NET RETROINV "), 1);
    TF_STREQ(cmd, "C:\\RETROINV\\" SU_PROG " /NET RETROINV");
    TF_EQ(su_cmd(cmd, "D:\\X", "/MBOX:F:\\MBOX /CFG:A.CFG"), 1);
    TF_STREQ(cmd, "D:\\X\\" SU_PROG " /MBOX:F:\\MBOX /CFG:A.CFG");
    for (i = 0; bad[i] != '\0'; i++) {
        strcpy(opts, "/NET X");
        opts[4] = bad[i];
        if (!TF_EQ(su_cmd(cmd, "C:\\R", opts), 0))
            printf("  refused: character %d\n", bad[i]);
    }
    memset(opts, 'X', sizeof opts - 1);
    i = SU_CMDMAX - 1 - (int)strlen("C:\\R\\" SU_PROG " ");
    opts[i] = '\0';
    TF_EQ(su_cmd(cmd, "C:\\R", opts), 1);
    TF_EQ(strlen(cmd), SU_CMDMAX - 1);
    opts[i] = 'X';
    opts[i + 1] = '\0';
    TF_EQ(su_cmd(cmd, "C:\\R", opts), 0);
}

static void test_dde(void)
{
    char out[SU_DDEMAX];
    char cmd[SU_CMDMAX];

    su_dde_add(out, "C:\\RETROINV\\INVWIN.EXE /NET RETROINV");
    TF_STREQ(out, "[CreateGroup(RETROINV)][AddItem(C:\\RETROINV\\"
                  "INVWIN.EXE /NET RETROINV,RETROINV)]");
    TF_STREQ(su_k_delete, "[DeleteGroup(RETROINV)]");
    memset(cmd, 'X', sizeof cmd - 1);
    cmd[sizeof cmd - 1] = '\0';
    su_dde_add(out, cmd);
    TF_EQ(strlen(out), sizeof out - 1);
}

/* A compressed file of "ABCABCABC": its header, then three literals and
   a match of six from the first. */
static const u8 k_abc[] = {
    'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33, 'A', '_', 9, 0, 0, 0,
    0x07, 'A', 'B', 'C', 0xF0, 0xF3
};
#define TS_ABC          "ABCABCABC"     /* what k_abc expands to        */
#define TS_BIG          5000    /* bytes of the plain file              */

static const char *const k_dirs[] = { "SRC", "DST", NULL };

/* 1 when path holds the bytes of text. */
static int holds_text(const char *path, const char *text)
{
    char buf[16];
    u16 got;

    return tfs_read_all(path, buf, sizeof buf, &got) == 0 &&
           got == strlen(text) && memcmp(buf, text, got) == 0;
}

static void test_copy(void)
{
    static u8 big[TS_BIG];
    char src[DOS_PATHMAX];
    char dst[DOS_PATHMAX];
    char out[DOS_PATHMAX];
    u16 i;

    su_join(out, "A", "B");
    sprintf(src, "A%cB", DOS_DIRSEP);
    TF_STREQ(out, src);
    sprintf(dst, "A%c", DOS_DIRSEP);
    su_join(out, dst, "B");
    TF_STREQ(out, src);

    tfs_dirs(k_dirs, 0);
    TF_EQ(tfs_dirs(k_dirs, 1), 0);
    strcpy(src, tfs_subpath("SRC", ""));
    strcpy(dst, tfs_path("DST", ""));
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INVWIN.EX_"), k_abc,
                        sizeof k_abc), 0);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INV.MSG"), "MSG", 3), 0);
    for (i = 0; i < TS_BIG; i++)
        big[i] = (u8)(i * 7);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "SETUP.EXE"), big, TS_BIG), 0);
    TF_EQ(su_copy(src, dst, "INVWIN.EXE", 1), 0);
    TF_CHECK(holds_text(tfs_subpath("DST", "INVWIN.EXE"), TS_ABC));
    TF_EQ(su_copy(src, dst, "INV.MSG", 1), 0);
    TF_CHECK(holds_text(tfs_subpath("DST", "INV.MSG"), "MSG"));
    TF_EQ(su_copy(src, dst, "SETUP.EXE", 0), 0);
    TF_CHECK(tfs_same(tfs_subpath("SRC", "SETUP.EXE"),
                      tfs_subpath("DST", "SETUP.EXE")));
    TF_CHECK(!tfs_exists(tfs_subpath("DST", SU_TEMP)));

    /* Each failure keeps the copy made above. */
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, 0);
    TF_EQ(su_copy(src, dst, "INVWIN.EXE", 1), CF_EFULL);
    tfs_fault_off();
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INVWIN.EX_"), k_abc,
                        sizeof k_abc - 1), 0);
    TF_EQ(su_copy(src, dst, "INVWIN.EXE", 1), LZ_EFORMAT);
    TF_EQ(su_copy(src, dst, "INVWIN.HLP", 1), CF_ENOENT);
    TF_CHECK(holds_text(tfs_subpath("DST", "INVWIN.EXE"), TS_ABC));
    TF_CHECK(!tfs_exists(tfs_subpath("DST", SU_TEMP)));
    tfs_dirs(k_dirs, 0);
}

static void test_packed(void)
{
    static const char list[] =
        "T\nS\n1\nSZ 1 9 INV.EXE\nW 1 9 SETUP.EXE\nWZ 1 9 INVWIN.EXE\n";
    su_list l;
    char src[DOS_PATHMAX];

    tfs_dirs(k_dirs, 0);
    TF_EQ(tfs_dirs(k_dirs, 1), 0);
    strcpy(src, tfs_subpath("SRC", ""));
    TF_EQ(su_list_open(&l, list, (u16)strlen(list)), 1);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INV.EX_"), "X", 1), 0);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "SETUP.EX_"), "X", 1), 0);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INVWIN.EXE"), "X", 1), 0);
    TF_EQ(su_packed(&l, src), 0);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INVWIN.EX_"), "X", 1), 0);
    TF_EQ(su_packed(&l, src), 1);
    tfs_dirs(k_dirs, 0);
}

int main(int argc, char **argv)
{
    tf_begin("T_SETUP");
    if (argc < 2)
        return 2;
    tfs_set_dir(argv[1]);
    test_list();
    test_dir();
    test_cmd();
    test_dde();
    test_copy();
    test_packed();
    return tf_done();
}

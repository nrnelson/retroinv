/*
 * T_TUI2.C - the second half of the T_TUI test (see T_TUI.C): key
 * scripts and their snapshot files (SRC/CLIENT/KEYSCR.C), the key names
 * and the clicks of the mouse.  It is a file of its own only to stay
 * within the compiler memory of Watcom C 6.5; T_TUI.H holds what both
 * share.
 */
#define TUI_CLICKS
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "TUI.H"
#include "KEYSCR.H"
/* Checks keep no expression text: Turbo C stores every string literal
   again, and the near data of this test would pass the limit. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_PART
#define TF_FILE "T_TUI2"
#define TF_KEYSRC
#include "TESTFW.H"
#include "T_TUI.H"

static const char script[] =
    "# a comment, then a blank line\n"
    "\n"
    "T ab \304\r\n"
    "k enter Alt-x ctrl-c x0041 F10\tstab\n"
    "M 0 2 3\n"
    "M 24 79 5\n"
    "S SNAP_1\n"
    "K SPACE\n"
    "M clear\n"
    "S S2\n"
    "T\n"
    "K break";

static const u16 script_keys[] = {
    'a', 'b', ' ', 0xC4, KEY_ENTER, KEY_EXT + 0x2D, 3, 'A', KEY_F10,
    KEY_STAB, ' ', KEY_ESC, KEY_NONE
};

static u16 g_got_keys[KS_MAXLINE];
static u16 g_ngot;

static void do_script(void)
{
    u16 k;

    for (;;) {
        k = tui_key();
        if (g_ngot < KS_MAXLINE)
            g_got_keys[g_ngot++] = k;
    }
}

static int g_snaph;

/* The snapshot file the script writes: for each snapshot its =SNAP
   line, VID_ROWS rows of SNAPROW bytes ('|', the cells, '|', CR LF),
   its =ATTR line and VID_ROWS rows of attribute letters.  LASTCELLS is
   the offset of the last three cells of the last row in either part. */
#define SNAPROW         (VID_COLS + 4)
#define LASTCELLS       (SNAPROW * (VID_ROWS - 1) + VID_COLS - 2)
#define SNAP1   "=SNAP SNAP_1 PALETTE=COLOR CURSOR=3,4,LINE VRAM=OK\r\n"
#define ATTR1   "=ATTR A=17 B=00 C=4F\r\n"
#define SNAP2   "=SNAP S2 PALETTE=COLOR CURSOR=3,4,LINE VRAM=OK\r\n"
#define ATTR2   "=ATTR A=17 B=4F\r\n"

/* 1 when the snapshot file holds lit (shorter than AT_MAX) at offset
   off. */
#define AT_MAX          64
static int at(u32 off, const char *lit)
{
    char b[AT_MAX];
    u16 n;
    u16 got;
    int ok;

    n = (u16)strlen(lit);
    got = 0;
    if (n >= AT_MAX)
        return 0;
    b[0] = '\0';
    if (cf_seek(g_snaph, off) == 0 && cf_read(g_snaph, b, n, &got) == 0)
        b[got] = '\0';
    ok = got == n && memcmp(b, lit, n) == 0;
    if (!ok)
        printf("  at %lu \"%.30s\"\n  want \"%.30s\"\n",
               (unsigned long)off, b, lit);
    return ok;
}

static void write_script(const char *name, const char *text, u16 n)
{
    char path[PATHMAX + 16];
    int h;

    strcpy(path, g_tdir);
    strcat(path, name);
    if (!TF_EQ(cf_create(path, &h), 0))
        return;
    if (n > 0)
        cf_write(h, text, n);
    cf_close(h);
}

static void bad_scriptn(const char *text, u16 n, int line)
{
    char path[PATHMAX + 16];
    char why[64];
    char want[PATHMAX + 24];

    write_script("BAD.KEY", text, n);
    strcpy(path, g_tdir);
    strcat(path, "BAD.KEY");
    TF_EQ(ks_open(path, NULL, why, sizeof why), -1);
    sprintf(want, "%s:%d", path, line);
    TF_STREQ(why, want);
}

/* A script whose keys must be exactly want (ended by KEY_NONE). */
static void good_script(const char *text, u16 n, const u16 *want)
{
    char path[PATHMAX + 16];
    char why[64];
    u16 i;

    write_script("GOOD.KEY", text, n);
    strcpy(path, g_tdir);
    strcat(path, "GOOD.KEY");
    if (!TF_EQ(ks_open(path, NULL, why, sizeof why), 0))
        return;
    g_ngot = 0;
    if (setjmp(tk_jb) == 0) {
        do_script();
        TF_CHECK(0);
    }
    TF_EQ(ks_close(), 0);
    for (i = 0; want[i] != KEY_NONE; i++)
        TF_EQ(g_got_keys[i], want[i]);
    TF_EQ(g_ngot, i);
}

/* Only P keys are seen waiting: a peek before each read sees nothing
   while K or T keys come (also inside a T line that has started), and
   the P key before it is read; nothing after the end. */
void test_script_peeks(void)
{
    static const char text[] = "K ENTER\nT ab\nP ESC\n";
    static const u16 seen[] = { KEY_NONE, KEY_NONE, KEY_NONE, KEY_ESC };
    static const u16 read[] = { KEY_ENTER, 'a', 'b', KEY_ESC };
    char path[PATHMAX + 16];
    char why[64];
    u16 i;

    write_script("GOOD.KEY", text, (u16)strlen(text));
    strcpy(path, g_tdir);
    strcat(path, "GOOD.KEY");
    if (!TF_EQ(ks_open(path, NULL, why, sizeof why), 0))
        return;
    for (i = 0; i < sizeof read / sizeof read[0]; i++) {
        TF_EQ(key_peek(), seen[i]);
        TF_EQ(key_read(), read[i]);
    }
    TF_EQ(key_peek(), KEY_NONE);
    TF_EQ(ks_close(), 0);
    TF_EQ(ks_close(), 0);
}

/* cf_fault_rd: every read fails. */
static int fail_read(int op)
{
    return op == CF_OP_READ ? CF_EACCES : 0;
}

void test_script_limits(void)
{
    static const u16 kz[] = { 'a', 'b', KEY_NONE };
    static const u16 kz2[] = { KEY_ENTER, KEY_NONE };
    static const u16 kp[] = { KEY_ENTER, KEY_ESC, 'a', KEY_NONE };
    static u16 klong[KS_MAXLINE];
    char t[KS_MAXLINE + 8];
    char path[PATHMAX + 16];
    char why[64];
    u16 i;

    /* A script that cannot be read is refused with the read's error,
       which the ks_close of the next script does not give again. */
    write_script("GOOD.KEY", "K ENTER\n", 8);
    strcpy(path, g_tdir);
    strcat(path, "GOOD.KEY");
    cf_fault_rd = fail_read;
    TF_EQ(ks_open(path, NULL, why, sizeof why), CF_EACCES);
    cf_fault_rd = NULL;
    good_script("K ENTER\n", 8, kz2);
    /* A Ctrl-Z ends the script, inside a T line too. */
    good_script("T ab\032cd\nK ENTER\n", 16, kz);
    good_script("K ENTER\n\032K ESC\n", 15, kz2);
    /* P keys are read in order, as K keys are; a P line may follow
       an S line once a K or T line has come between. */
    good_script("S A\nK ENTER\nP ESC\nT a\n", 22, kp);
    /* A NUL makes the line bad. */
    bad_scriptn("K ENTER\nT a\0b\n", 14, 2);
    /* KS_MAXLINE bytes are a line, one more is not, with LF or CR LF. */
    memset(t, 'x', sizeof t);
    t[0] = 'T';
    t[1] = ' ';
    t[KS_MAXLINE] = '\n';
    for (i = 0; i < KS_MAXLINE - 2; i++)
        klong[i] = 'x';
    klong[KS_MAXLINE - 2] = KEY_NONE;
    good_script(t, KS_MAXLINE + 1, klong);
    t[KS_MAXLINE] = '\r';
    t[KS_MAXLINE + 1] = '\n';
    good_script(t, KS_MAXLINE + 2, klong);
    t[KS_MAXLINE] = 'x';
    t[KS_MAXLINE + 1] = '\n';
    bad_scriptn(t, KS_MAXLINE + 2, 1);
    t[KS_MAXLINE + 1] = '\r';
    t[KS_MAXLINE + 2] = '\n';
    bad_scriptn(t, KS_MAXLINE + 3, 1);
}

static void bad_script(const char *text, int line)
{
    bad_scriptn(text, (u16)strlen(text), line);
}

/* The code of a click (KEY_CLICK or KEY_DCLICK as base) on row, col. */
#define CLICK(base, row, col)   ((u16)((base) + (row) * KEY_CROW + (col)))

/* tui_hit's rectangles; tui_keyname's single characters, n of 0 and
   Ctrl- with a key name; the C and D lines of key scripts, their
   refusals and their peeks; clicks on the hint row's words standing for
   their keys. */
void test_clicks(void)
{
    static const u16 kc[] = {
        CLICK(KEY_CLICK, 0, 0), CLICK(KEY_DCLICK, 24, 79),
        CLICK(KEY_CLICK, 3, 7), KEY_ESC, KEY_NONE
    };
    /* The words of the hint below and the key each click gives. */
    static const char hint[] = "F2=Save Up/Down=Move Esc=Back 2Ins=New";
    static const u8 hcol[] = { 1, 6, 9, 12, 11, 17, 22, 31, 0, 40 };
    u16 hkey[sizeof hcol + 3];
    char t[sizeof hcol * 12 + 32];
    char path[PATHMAX + 16];
    char why[64];
    u16 i;

    TF_EQ(tui_hit(CLICK(KEY_CLICK, 5, 10), 5, 10, 1, 1), 0);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 7, 12), 5, 10, 3, 4), 2 * KEY_CROW + 2);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 7, 13), 5, 10, 3, 4), 2 * KEY_CROW + 3);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 8, 12), 5, 10, 3, 4), -1);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 4, 12), 5, 10, 3, 4), -1);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 6, 14), 5, 10, 3, 4), -1);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 6, 9), 5, 10, 3, 4), -1);
    TF_EQ(tui_hit(CLICK(KEY_CLICK, 24, 79), 0, 0, VID_ROWS, VID_COLS),
          24 * KEY_CROW + 79);
    TF_EQ(tui_hit(CLICK(KEY_DCLICK, 6, 11), 5, 10, 3, 4), -1);
    TF_EQ(tui_hit(KEY_ENTER, 0, 0, VID_ROWS, VID_COLS), -1);
    TF_EQ(tui_hit(KEY_EOS, 0, 0, VID_ROWS, VID_COLS), -1);

    TF_EQ(tui_keyname("y", 1), 'y');
    TF_EQ(tui_keyname("Esc=", 3), KEY_ESC);
    TF_EQ(tui_keyname("PgDn", 4), KEY_PGDN);
    TF_EQ(tui_keyname("Ctrl-Y", 6), KEY_CTRL('Y'));
    TF_EQ(tui_keyname("Ctrl-PgDn", 9), KEY_CPGDN);
    TF_EQ(tui_keyname("ctrl-end", 8), KEY_CEND);
    TF_EQ(tui_keyname("Ctrl-Ins", 8), KEY_NONE);
    TF_EQ(tui_keyname("Ctrl-1", 6), KEY_NONE);
    TF_EQ(tui_keyname("Esc", 0), KEY_NONE);

    bad_script("C 25 0\n", 1);
    bad_script("C 0 80\n", 1);
    bad_script("C 0\n", 1);
    bad_script("D 0 0 1\n", 1);
    bad_script("D\n", 1);
    bad_script("CC 0 0\n", 1);
    /* A click is a key before a P line, and never seen waiting. */
    strcpy(t, "C 0 0\nD 24 79\nc 3 7\nP ESC\n");
    good_script(t, (u16)strlen(t), kc);
    write_script("GOOD.KEY", t, (u16)strlen(t));
    strcpy(path, g_tdir);
    strcat(path, "GOOD.KEY");
    if (TF_EQ(ks_open(path, NULL, why, sizeof why), 0)) {
        for (i = 0; kc[i] != KEY_NONE; i++) {
            TF_EQ(key_peek(), kc[i] == KEY_ESC ? KEY_ESC : KEY_NONE);
            TF_EQ(key_read(), kc[i]);
        }
        TF_EQ(ks_close(), 0);
    }

    /* A click on a message over the hints stays a click.  Then each
       click on the hint row in turn: F2 on its name and its text, Up
       and Down on theirs, Up on the '/' and past the '=', Esc, Ins
       after its unshown level digit; a click on the row's first cell
       and one past the words stay clicks, and so does the second press
       of a double click. */
    hkey[0] = CLICK(KEY_CLICK, TUI_HINTROW, 1);
    hkey[1] = KEY_F2;
    hkey[2] = KEY_F2;
    hkey[3] = KEY_UP;
    hkey[4] = KEY_DOWN;
    hkey[5] = KEY_UP;
    hkey[6] = KEY_UP;
    hkey[7] = KEY_ESC;
    hkey[8] = KEY_INS;
    sprintf(t, "C %d 1\n", TUI_HINTROW);
    for (i = 0; i < sizeof hcol; i++) {
        if (i >= 8)
            hkey[i + 1] = CLICK(KEY_CLICK, TUI_HINTROW, hcol[i]);
        sprintf(t + strlen(t), "C %d %d\n", TUI_HINTROW, hcol[i]);
    }
    hkey[i + 1] = CLICK(KEY_DCLICK, TUI_HINTROW, 1);
    hkey[i + 2] = KEY_NONE;
    sprintf(t + strlen(t), "D %d 1\n", TUI_HINTROW);
    tui_hint(hint);
    tui_msg("x", 0);
    write_script("GOOD.KEY", t, (u16)strlen(t));
    if (TF_EQ(ks_open(path, NULL, why, sizeof why), 0)) {
        for (i = 0; hkey[i] != KEY_NONE; i++)
            TF_EQ(tui_key(), hkey[i]);
        TF_EQ(ks_close(), 0);
    }
}

void test_keyscripts(void)
{
    char path[PATHMAX + 16];
    char spath[PATHMAX + 16];
    char why[64];
    u32 o;
    u32 size;
    u16 i;

    bad_script("K ENTER\nK NOSUCH\n", 2);
    bad_script("X\n", 1);
    bad_script("TT\n", 1);
    bad_script("S lower\n", 1);
    bad_script("S TOOLONGNAME\n", 1);
    bad_script("S\n", 1);
    bad_script("M 25 0 1\n", 1);
    bad_script("M 0 80 1\n", 1);
    bad_script("M 0 0 0\n", 1);
    bad_script("M 0 0\n", 1);
    bad_script("M 0 1000 1\n", 1);
    bad_script("M 1000 0 1\n", 1);
    bad_script("K x00\n", 1);
    bad_script("K ALT-1\n", 1);
    bad_script("K ENT\n", 1);
    /* A P line is refused after an S or M line, comments, blank lines
       and K or T lines without keys between them or not. */
    bad_script("P NOSUCH\n", 1);
    bad_script("S A\n# c\n\nP ESC\n", 4);
    bad_script("M 0 0 1\nP ESC\n", 2);
    bad_script("S A\nK\nT\nT \nP ESC\n", 5);
    bad_script("M 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\n"
               "M 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\n"
               "M 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\nM 0 0 1\n", 17);
    strcpy(path, g_tdir);
    strcat(path, "NOSUCH.KEY");
    cf_remove(path);
    TF_EQ(ks_open(path, NULL, why, sizeof why), CF_ENOENT);

    write_script("T.KEY", script, (u16)strlen(script));
    strcpy(path, g_tdir);
    strcat(path, "T.KEY");
    strcpy(spath, g_tdir);
    strcat(spath, "T.SNP");
    tui_palette(TP_COLOR);
    tui_clear(0, VID_ROWS - 1);
    tui_text(0, 0, "HEAD", TA_NORMAL);
    tui_text(24, 77, "end", TA_ERROR);
    vid_cursor(3, 4, VID_CUR_LINE);
    TF_EQ(ks_open(path, spath, why, sizeof why), 0);
    g_ngot = 0;
    if (setjmp(tk_jb) == 0) {
        do_script();
        TF_CHECK(0);
    }
    TF_EQ(tk_status, 0);
    TF_EQ(ks_close(), 0);
    key_set_source(NULL, NULL);
    for (i = 0; script_keys[i] != KEY_NONE; i++)
        TF_EQ(g_got_keys[i], script_keys[i]);
    if (!TF_EQ(g_ngot, i))
        for (i = 0; i < g_ngot; i++)
            printf("  key %u: %04X\n", (unsigned)i, (unsigned)g_got_keys[i]);

    if (!TF_EQ(cf_open(spath, CF_READ, &g_snaph), 0))
        return;
    /* Masked cells are '?' with attribute 00h; a mask is cut at the end
       of its row; attribute letters follow the order of first use. */
    o = 0;
    TF_CHECK(at(o, SNAP1 "|HE???   "));
    o += sizeof SNAP1 - 1;
    TF_CHECK(at(o + SNAPROW * (VID_ROWS - 1), "|                "));
    TF_CHECK(at(o + LASTCELLS, "en?|\r\n" ATTR1));
    o += SNAPROW * VID_ROWS + sizeof ATTR1 - 1;
    TF_CHECK(at(o, "|AABBBAAA"));
    TF_CHECK(at(o + LASTCELLS, "CCB|\r\n=SNAP S2 "));
    o += SNAPROW * VID_ROWS;
    TF_CHECK(at(o, SNAP2 "|HEAD    "));
    o += sizeof SNAP2 - 1;
    TF_CHECK(at(o + LASTCELLS, "end|\r\n" ATTR2 "|AAAA"));
    o += SNAPROW * VID_ROWS + sizeof ATTR2 - 1;
    TF_CHECK(at(o + LASTCELLS, "BBB|\r\n"));
    o += SNAPROW * VID_ROWS;
    TF_EQ(cf_size(g_snaph, &size), 0);
    if (cstk_unused() == CSTK_UNKNOWN) {
        TF_EQ(size, o);
    } else {
        TF_CHECK(at(o, "=STACK UNUSED="));
        TF_CHECK(size > o + 16 && size < o + 22);
    }
    cf_close(g_snaph);
}

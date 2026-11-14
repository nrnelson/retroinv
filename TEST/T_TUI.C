/*
 * T_TUI.C - screen primitives, dialogs, menus and the main loop
 * (SRC/CLIENT/TUI.C) on the shadow screen.
 *
 * usage: T_TUI dir [msgdir]
 *
 * dir is a scratch directory prefix ("OBJ/HOST/TUIT/", "D:\") where the
 * test writes its help, message and key script files, removed first and,
 * when every check passed, last; msgdir is the directory prefix of the
 * built INV.MSG (default: the program's own directory).  Keys come from
 * a key source (key_set_source) fed from a list per scenario; when a
 * list runs out the source returns KEY_EOS, tui_key ends the program
 * through the exit function, and the test's exit function leaves with
 * longjmp, which ends the scenario.  The source can also copy the screen
 * at a chosen key, so a dialog can be checked while it is open.
 *
 * Covered:
 * - palettes, drawing and its clean-up of control bytes, frames, hints with
 *   level digits, messages over the hints and hints kept under them;
 * - the line editor, prompts, pick lists (empty, clamping, scrolling,
 *   letters);
 * - message boxes and questions putting back what they covered, the status
 *   line's information text read back and its kind of link, a message box
 *   drawn without saving or a key;
 * - help (F1 on the topic, F9 for the keys, tui_help called directly, the
 *   menus' own topic);
 * - dialogs and menus refusing to open when memory has run out, Ctrl-Break;
 * - the menus with dimmed entries and titles and "&&" labels, the global
 *   keys, permission checks, the Record menu (tui_record) and the keys of
 *   its verbs;
 * - the tui_fail texts and results (a lost NET link ending the program,
 *   the idle hook removed first), tui_req over a link that answers from
 *   a table;
 * - the idle hook: on NET and serial links a PING every half minute, at
 *   a steady interval (counted on the host only, whose clock is
 *   virtual), none while a request is under way, not even to rq_send; a
 *   lost link ending the program; a session found ended telling so and
 *   ending the program with 0; on LOCAL no PING; on the host the DOS date
 *   read once as the ticks pass midnight; no hook after tui_run;
 * - the main loop, the end of a key script;
 * - in T_TUI2.C the key scripts themselves (their lines, their
 *   snapshots, one that cannot be read), the key names and the clicks
 *   of the mouse (tui_hit, the hint row's words).
 */
#define TUI_VERBS
#define TUI_VERBTAB
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "MSG.H"
#include "RQ.H"
#include "TUI.H"
#include "KEYSCR.H"
#define TF_KEYSRC
#define TK_MAXKEYS      40
#include "TESTFW.H"
#include "T_TUI.H"

#define SB_MINW         24      /* the least width of a message box     */

char g_tdir[PATHMAX];
static char g_snap[VID_ROWS][VID_COLS + 1];     /* the screen at a key  */
static u8 g_snapattr[VID_ROWS][VID_COLS];
static u8 g_snapcur[3];         /* its cursor: row, column and shape    */

static const char *g_resp;      /* link_send's response, lines ended LF */
static char g_sent[LINK_MAXLINE + 1];   /* the last request line        */
static link *g_link;            /* the link it was sent on              */
static u16 g_nsent;             /* requests link_send was given         */
static int g_sendrc;            /* LINK_OK, or what link_send fails with
                                   instead of answering                 */
static u8 g_nested;             /* link_send calls key_idle first, as a
                                   printer's question in a response     */

/* Every file the test makes in g_tdir but the help file. */
static const char *const k_files[] = {
    MSG_FILE, "BAD.KEY", "GOOD.KEY", "T.KEY", "T.SNP", NULL
};

/* The screen now into g_snap and g_snapattr. */
static void grab(void)
{
    u8 cells[VID_ROWBYTES];
    u8 r;
    u8 c;

    for (r = 0; r < VID_ROWS; r++) {
        vid_getrow(r, cells);
        for (c = 0; c < VID_COLS; c++) {
            g_snap[r][c] = (char)cells[2 * c];
            g_snapattr[r][c] = cells[2 * c + 1];
        }
        g_snap[r][VID_COLS] = '\0';
    }
    vid_cursor_get(&g_snapcur[0], &g_snapcur[1], &g_snapcur[2]);
}

/* Install the keys of a scenario, a list ended by KEY_NONE, the screen
   copied into g_snap at key at. */
static void feed(const u16 *k, u16 at)
{
    memset(g_snap, 0, sizeof g_snap);
    tk_copy = grab;
    tk_feed(k, at);
}

/* Run fn with the keys k; 1 when it returned, 0 when the keys ran out. */
static int run(void (*fn)(void), const u16 *k, u16 at)
{
    feed(k, at);
    if (setjmp(tk_jb) != 0)
        return 0;
    fn();
    return 1;
}

/* 1 when s occurs in the copied screen. */
static int snap_has(const char *s)
{
    u8 r;

    for (r = 0; r < VID_ROWS; r++)
        if (strstr(g_snap[r], s) != NULL)
            return 1;
    return 0;
}

static int screen_has(const char *s)
{
    u8 r;

    for (r = 0; r < VID_ROWS; r++)
        if (strstr(tk_row(r), s) != NULL)
            return 1;
    return 0;
}

static void test_palettes(void)
{
    tui_palette(TP_COLOR);
    TF_EQ(tui_pal(), TP_COLOR);
    TF_EQ(tui_attr(TA_NORMAL), 0x17);
    TF_EQ(tui_attr(TA_ERROR), 0x4F);
    TF_EQ(tui_attr(TA_MENUOFF), 0x78);
    TF_EQ(tui_attr(TA_ALERT), 0x1C);
    TF_EQ(tui_attr(TA_NROLES), 0x17);
    tui_palette(TP_MONO);
    TF_EQ(tui_attr(TA_MENUHOT), 0x09);
    TF_EQ(tui_attr(TA_INPUT), 0x01);
    TF_EQ(tui_attr(TA_ALERT), 0x0F);
    tui_palette(TP_MONOCGA);
    TF_EQ(tui_attr(TA_MENUHOT), 0x0F);
    TF_EQ(tui_attr(TA_INPUT), 0x07);
    TF_EQ(tui_attr(TA_ALERT), 0x0F);
    tui_palette(7);
    TF_EQ(tui_pal(), TP_MONOCGA);
    tui_palette(TP_COLOR);
}

static void test_drawing(void)
{
    char want[VID_COLS + 1];

    tui_clear(0, VID_ROWS - 1);
    TF_STREQ(tk_row(5), "");
    TF_EQ(tk_attr(5, 0), 0x17);
    tui_text(2, 3, "a\nb\rc\032d\001", TA_BOLD);
    TF_STREQ(tk_row(2), "   a.b.c.d\001");
    TF_EQ(tk_attr(2, 3), 0x1F);
    tui_text(3, 78, "xyz", TA_NORMAL);
    TF_STREQ(tk_row(3), "                                       "
                        "                                       xy");
    tui_textn(4, 0, "abc", 5, TA_SELECT);
    TF_EQ(tk_attr(4, 4), 0x30);
    TF_EQ(tk_attr(4, 5), 0x17);
    tui_textn(4, 0, "xyzdef", 2, TA_NORMAL);
    TF_STREQ(tk_row(4), "xyc");
    tui_center(6, 10, 10, "mid", TA_TITLE);
    TF_STREQ(tk_row(6), "             mid");
    TF_EQ(tk_attr(6, 14), 0x1E);

    tui_clear(0, VID_ROWS - 1);
    tui_box(1, 0, 4, 12, TB_SINGLE, "Tt");
    memset(want, 0, sizeof want);
    want[0] = TF_STL;
    want[1] = TF_SH;
    want[2] = TF_SH;
    want[3] = TF_SH;
    strcat(want, " Tt ");
    want[8] = TF_SH;
    want[9] = TF_SH;
    want[10] = TF_SH;
    want[11] = TF_STR;
    TF_STREQ(tk_row(1), want);
    TF_EQ(tk_attr(1, 0), 0x1B);
    TF_EQ(tk_attr(1, 5), 0x1E);
    TF_EQ((u8)tk_row(4)[11], (u8)TF_SBR);
    tui_box(6, 0, 3, 8, TB_DOUBLE, NULL);
    TF_EQ((u8)tk_row(6)[0], (u8)TF_DTL);
    TF_EQ((u8)tk_row(6)[3], (u8)TF_DH);
    TF_EQ((u8)tk_row(7)[7], (u8)TF_DV);
}

static void test_hints(void)
{
    tui_s.level = 1;
    tui_hint("F2=Save 2F4=Edit Esc=Cancel");
    TF_STREQ(tk_row(TUI_HINTROW), " F2=Save F4=Edit Esc=Cancel");
    TF_EQ(tk_attr(TUI_HINTROW, 1), tui_attr(TA_HINTKEY));
    TF_EQ(tk_attr(TUI_HINTROW, 3), tui_attr(TA_HINT));
    TF_EQ(tk_attr(TUI_HINTROW, 9), tui_attr(TA_MENUOFF));
    TF_EQ(tk_attr(TUI_HINTROW, 12), tui_attr(TA_MENUOFF));
    TF_EQ(tk_attr(TUI_HINTROW, 17), tui_attr(TA_HINTKEY));
    tui_s.level = 2;
    tui_hint("F2=Save 2F4=Edit 3Del");
    TF_EQ(tk_attr(TUI_HINTROW, 9), tui_attr(TA_HINTKEY));
    TF_EQ(tk_attr(TUI_HINTROW, 12), tui_attr(TA_HINT));
    TF_EQ(tk_attr(TUI_HINTROW, 17), tui_attr(TA_MENUOFF));
    TF_STREQ(tk_row(TUI_HINTROW), " F2=Save F4=Edit Del");
}

static void test_key_one(void)
{
    static const u16 k1[] = { 'a', KEY_NONE };

    tui_hint("Esc=Back");
    tui_msg("Oops", 1);
    TF_STREQ(tk_row(TUI_HINTROW), " Oops");
    TF_EQ(tk_attr(TUI_HINTROW, 1), tui_attr(TA_ERROR));
    feed(k1, 0);
    TF_EQ(tui_key(), 'a');
    TF_STREQ(tk_row(TUI_HINTROW), " Esc=Back");

    /* tui_hint_keep under a message keeps the message until the next
       key, then shows the kept hints; without a message it draws them. */
    tui_msg("Gone", 1);
    tui_hint_keep("Enter=Open");
    TF_STREQ(tk_row(TUI_HINTROW), " Gone");
    feed(k1, 0);
    TF_EQ(tui_key(), 'a');
    TF_STREQ(tk_row(TUI_HINTROW), " Enter=Open");
    tui_hint_keep("Esc=Back");
    TF_STREQ(tk_row(TUI_HINTROW), " Esc=Back");
}

static void test_key_break(void)
{
    static const u16 k[] = { KEY_BREAK, KEY_NONE };

    feed(k, 0);
    TF_EQ(tui_key(), KEY_ESC);
}

static char g_ibuf[32];
static u16 g_ikey;
static u8 g_iflags;

static void do_input(void)
{
    g_ikey = tui_input(10, 5, 8, g_ibuf, 10, g_iflags);
}

/* A field of two cells for two characters. */
static void do_input2(void)
{
    g_ikey = tui_input(10, 5, 2, g_ibuf, 2, 0);
}

/* A field of one cell for one character. */
static void do_input1(void)
{
    g_ikey = tui_input(10, 5, 1, g_ibuf, 1, 0);
}

static void test_input(void)
{
    static const u16 k1[] = { 'a', 'b', 'c', KEY_LEFT, KEY_LEFT, 'X',
                              KEY_INS, 'Y', KEY_HOME, KEY_DEL, KEY_END,
                              KEY_BS, KEY_TAB, KEY_NONE };
    static const u16 k2[] = { 0x19, 'q', 'w', 'e', 'r', 't', 'y', 'u',
                              'i', 'o', 'p', 'z', 'z', KEY_ENTER,
                              KEY_NONE };
    static const u16 k3[] = { '1', 'a', '2', KEY_F5, KEY_NONE };
    static const u16 k3c[] = { 'a', KEY_CTRL('S'), KEY_NONE };
    static const u16 k4[] = { 's', 'e', 'c', KEY_ESC, KEY_NONE };
    static const u16 k5[] = { ' ', '~', 0x7F, 0xC4, '!', KEY_UP, KEY_NONE };
    static const u16 k6[] = { '9', KEY_ENTER, KEY_NONE };
    static const u16 k7[] = { KEY_DEL, KEY_LEFT, 'x', KEY_ENTER, KEY_NONE };
    static const u16 k8[] = { KEY_INS, '5', KEY_ENTER, KEY_NONE };
    static const u16 k9[] = { KEY_BS, KEY_ENTER, KEY_NONE };
    static const u16 k10[] = { '1', '0', KEY_BS, KEY_ENTER, KEY_NONE };
    static const u16 k11[] = { KEY_LEFT, KEY_DEL, KEY_ENTER, KEY_NONE };
    static const u16 k12[] = { KEY_LEFT, KEY_BS, KEY_ENTER, KEY_NONE };
    static const u16 k13[] = { KEY_LEFT, 'z', KEY_ENTER, KEY_NONE };

    g_ibuf[0] = '\0';
    g_iflags = 0;
    TF_CHECK(run(do_input, k1, 0));
    TF_STREQ(g_ibuf, "XY");
    TF_EQ(g_ikey, KEY_TAB);
    TF_STREQ(tk_row(10), "     XY");
    TF_EQ(tk_attr(10, 5), tui_attr(TA_INPUT));

    /* Ctrl-Y clears; at most max characters; scrolled to show the end;
       TI_UPPER folds. */
    strcpy(g_ibuf, "old");
    g_iflags = TI_UPPER;
    TF_CHECK(run(do_input, k2, 13));
    TF_STREQ(g_ibuf, "QWERTYUIOP");
    TF_EQ(g_ikey, KEY_ENTER);
    TF_CHECK(strncmp(g_snap[10] + 5, "RTYUIOP ", 8) == 0);
    TF_EQ(g_snapattr[10][5], tui_attr(TA_INPUTON));

    g_ibuf[0] = '\0';
    g_iflags = TI_DIGITS;
    TF_CHECK(run(do_input, k3, 0));
    TF_STREQ(g_ibuf, "12");
    TF_EQ(g_ikey, KEY_F5);

    /* A control code ends the edit, as the keys of a form's Ctrl-S. */
    g_ibuf[0] = '\0';
    g_iflags = 0;
    TF_CHECK(run(do_input, k3c, 0));
    TF_STREQ(g_ibuf, "a");
    TF_EQ(g_ikey, KEY_CTRL('S'));

    /* Nothing is shown of a password. */
    g_ibuf[0] = '\0';
    g_iflags = TI_NOECHO | TI_PRINT;
    TF_CHECK(run(do_input, k4, 4));
    TF_STREQ(g_ibuf, "sec");
    TF_EQ(g_ikey, KEY_ESC);
    TF_CHECK(strncmp(g_snap[10] + 5, "        ", 8) == 0);
    TF_STREQ(tk_row(10), "");

    g_ibuf[0] = '\0';
    g_iflags = TI_PRINT;
    TF_CHECK(run(do_input, k5, 0));
    TF_STREQ(g_ibuf, "~!");
    g_iflags = 0;
    g_ibuf[0] = '\0';
    TF_CHECK(run(do_input, k5, 0));
    TF_STREQ(g_ibuf, " ~\304!");
    TF_EQ(g_ikey, KEY_UP);

    /* At the end of a full field that fits its cells the whole text is
       shown, the cursor on its last character; a key typed there adds
       nothing, Del removes that character and Left then moves to the
       first. */
    strcpy(g_ibuf, "10");
    TF_CHECK(run(do_input2, k6, 1));
    TF_STREQ(g_ibuf, "10");
    TF_CHECK(strncmp(g_snap[10] + 5, "10", 2) == 0);
    TF_EQ(g_snapcur[0], 10);
    TF_EQ(g_snapcur[1], 6);
    TF_EQ(g_snapcur[2], VID_CUR_LINE);
    strcpy(g_ibuf, "10");
    TF_CHECK(run(do_input2, k7, 3));
    TF_STREQ(g_ibuf, "x1");
    TF_EQ(g_snapcur[1], 5);
    /* Overwriting replaces the last character, Backspace removes it. */
    strcpy(g_ibuf, "10");
    TF_CHECK(run(do_input2, k8, 0));
    TF_STREQ(g_ibuf, "15");
    strcpy(g_ibuf, "10");
    TF_CHECK(run(do_input2, k9, 0));
    TF_STREQ(g_ibuf, "1");
    /* The same once the last character has been typed. */
    g_ibuf[0] = '\0';
    TF_CHECK(run(do_input2, k10, 3));
    TF_CHECK(strncmp(g_snap[10] + 5, "10", 2) == 0);
    TF_EQ(g_snapcur[1], 6);
    TF_STREQ(g_ibuf, "1");
    /* Left at the end of a full field moves off the last character at
       once: Del then removes the one before it.  In a field of one
       cell Left has nowhere to go, and at the start of an empty field
       it changes nothing. */
    strcpy(g_ibuf, "10");
    TF_CHECK(run(do_input2, k11, 2));
    TF_STREQ(g_ibuf, "0");
    TF_EQ(g_snapcur[1], 5);
    strcpy(g_ibuf, "a");
    TF_CHECK(run(do_input1, k12, 0));
    TF_STREQ(g_ibuf, "");
    g_ibuf[0] = '\0';
    TF_CHECK(run(do_input2, k13, 0));
    TF_STREQ(g_ibuf, "z");
}

static int g_pres;

static void do_prompt(void)
{
    g_pres = tui_prompt(msg(MT_ERRTITLE), msg(MT_HINTOK), g_ibuf, 6, 0);
}

static const char *g_plabel;
static u16 g_pmax;

static void do_prompt2(void)
{
    g_pres = tui_prompt("Settings", g_plabel, g_ibuf, g_pmax, 0);
}

/* A label and field wider than the screen: the field narrows, then the
   label is cut, and the box stays on the screen. */
static void test_prompt_wide(void)
{
    static const u16 k[] = { 'x', KEY_ENTER, KEY_NONE };
    static char longlab[MSG_MAXLEN + 1];
    u8 r;

    r = (u8)(TUI_TOP + TUI_WORKROWS / 2 - 1);
    g_plabel = "Data directory of the LOCAL connection:";
    g_pmax = 64;
    g_ibuf[0] = '\0';
    tui_clear(0, VID_ROWS - 1);
    TF_CHECK(run(do_prompt2, k, 2));
    TF_EQ(g_pres, 1);
    TF_STREQ(g_ibuf, "x");
    TF_EQ((u8)g_snap[r][0], (u8)TF_DV);
    TF_EQ((u8)g_snap[r][VID_COLS - 1], (u8)TF_DV);
    TF_CHECK(strncmp(g_snap[r] + 2, g_plabel, strlen(g_plabel)) == 0);

    memset(longlab, 'L', MSG_MAXLEN);
    longlab[MSG_MAXLEN] = '\0';
    g_plabel = longlab;
    g_pmax = 20;
    g_ibuf[0] = '\0';
    TF_CHECK(run(do_prompt2, k, 2));
    TF_STREQ(g_ibuf, "x");
    TF_EQ((u8)g_snap[r][0], (u8)TF_DV);
    TF_EQ((u8)g_snap[r][VID_COLS - 1], (u8)TF_DV);
    /* The field keeps TUI_PROMPTMINW cells, followed by three blank
       cells and the frame; the label fills the columns from 2 up to the
       space before the field. */
    TF_EQ((u8)g_snap[r][VID_COLS - 6 - TUI_PROMPTMINW], (u8)'L');
    TF_EQ((u8)g_snap[r][VID_COLS - 5 - TUI_PROMPTMINW], (u8)' ');
    TF_EQ((u8)g_snap[r][VID_COLS - 4 - TUI_PROMPTMINW], (u8)'x');

    /* A title longer than the screen is cut inside the frame. */
    tui_box(2, 0, 3, VID_COLS, TB_SINGLE, longlab);
    TF_EQ((u8)tk_row(2)[VID_COLS - 1], (u8)TF_STR);
    TF_EQ((u8)tk_row(2)[0], (u8)TF_STL);
}

static void test_prompt(void)
{
    static const u16 k1[] = { 'a', KEY_TAB, 'b', KEY_ENTER, KEY_NONE };
    static const u16 k2[] = { 'z', KEY_ESC, KEY_NONE };

    tui_clear(0, VID_ROWS - 1);
    tui_hint("F2=Save");
    g_ibuf[0] = '\0';
    TF_CHECK(run(do_prompt, k1, 4));
    TF_EQ(g_pres, 1);
    TF_STREQ(g_ibuf, "ab");
    TF_CHECK(snap_has(" Error "));
    TF_CHECK(snap_has("Enter=OK ab"));
    TF_STREQ(tk_row(TUI_HINTROW), " F2=Save");
    TF_CHECK(!screen_has("Error"));
    TF_CHECK(run(do_prompt, k2, 0));
    TF_EQ(g_pres, 0);
    TF_STREQ(g_ibuf, "abz");
}

static void label(void *arg, u16 i, char *buf)
{
    TF_CHECK(arg == (void *)g_ibuf);
    sprintf(buf, "%c entry %02u", (char)(i % 3 == 0 ? 'A' : 'b'),
            (unsigned)i);
}

static u16 g_psel;
static u16 g_pn;

static void do_pick(void)
{
    g_pres = tui_pick("Pick", NULL, g_pn, label, g_ibuf, &g_psel);
}

static void test_pick(void)
{
    static const u16 kd[] = { KEY_DOWN, KEY_DOWN, KEY_ENTER, KEY_NONE };
    static const u16 ke[] = { KEY_END, KEY_UP, KEY_ENTER, KEY_NONE };
    static const u16 kp[] = { KEY_PGDN, KEY_PGDN, KEY_PGUP, KEY_ENTER,
                              KEY_NONE };
    static const u16 kl[] = { 'a', 'a', 'B', KEY_ENTER, KEY_NONE };
    static const u16 kx[] = { KEY_DOWN, KEY_ESC, KEY_NONE };
    static const u16 kh[] = { KEY_HOME, KEY_UP, KEY_ENTER, KEY_NONE };

    tui_clear(0, VID_ROWS - 1);
    g_pn = 0;
    g_psel = 5;
    TF_CHECK(run(do_pick, kd, 0));
    TF_EQ(g_pres, 0);
    TF_EQ(tk_pos(), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MT_NOCHOICE)) != NULL);

    g_pn = 30;
    g_psel = 99;
    TF_CHECK(run(do_pick, ke, 1));
    TF_EQ(g_pres, 1);
    TF_EQ(g_psel, 28);
    /* Clamped to the last entry, which is shown and marked. */
    TF_CHECK(snap_has("b entry 29"));
    TF_CHECK(!snap_has("A entry 00"));
    TF_CHECK(snap_has(" Pick "));

    g_psel = 0;
    TF_CHECK(run(do_pick, kd, 3));
    TF_EQ(g_psel, 2);
    TF_CHECK(snap_has("A entry 00"));
    TF_CHECK(!snap_has("A entry 18"));

    g_psel = 0;
    TF_CHECK(run(do_pick, kp, 4));
    TF_EQ(g_psel, 29 - TUI_PICKROWS);
    g_psel = 0;
    TF_CHECK(run(do_pick, kl, 0));
    TF_EQ(g_psel, 7);
    g_psel = 4;
    TF_CHECK(run(do_pick, kx, 0));
    TF_EQ(g_pres, 0);
    TF_EQ(g_psel, 4);
    TF_CHECK(run(do_pick, kh, 0));
    TF_EQ(g_psel, 0);
    TF_STREQ(tk_row(10), "");
}

static void do_msgbox(void)
{
    tui_msgbox(msg(MT_ERRTITLE), "A long text that goes on and on so that "
        "the box has to wrap it over more than one line\nand a second "
        "paragraph");
}

static void do_confirm(void)
{
    g_pres = tui_confirm(msg(MT_QUIT));
}

static void test_dialogs(void)
{
    static const u16 k1[] = { 'x', KEY_ENTER, KEY_NONE };
    static const u16 ky[] = { 'x', 'y', KEY_NONE };
    static const u16 kn[] = { 'N', KEY_NONE };
    static const u16 kesc[] = { KEY_ESC, KEY_NONE };
    static const u16 kalt[] = { KEY_EXT | 0x79, KEY_ESC, KEY_NONE };
    char before[VID_COLS + 1];

    tui_clear(0, VID_ROWS - 1);
    tui_text(10, 0, "underneath the box, which comes back afterwards",
             TA_NORMAL);
    tui_hint("Esc=Back");
    strcpy(before, tk_row(10));
    /* A box hides the cursor the screen under it left showing. */
    vid_cursor(10, 3, VID_CUR_LINE);
    TF_CHECK(run(do_msgbox, k1, 2));
    TF_EQ(tk_pos(), 2);
    TF_EQ(g_snapcur[2], VID_CUR_HIDE);
    TF_CHECK(snap_has(" Error "));
    TF_CHECK(snap_has("A long text that goes on"));
    TF_CHECK(snap_has("and a second paragraph"));
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "Enter=OK") != NULL);
    TF_STREQ(tk_row(10), before);
    TF_STREQ(tk_row(TUI_HINTROW), " Esc=Back");

    vid_cursor(10, 3, VID_CUR_LINE);
    TF_CHECK(run(do_confirm, ky, 1));
    TF_EQ(g_pres, 1);
    TF_EQ(g_snapcur[2], VID_CUR_HIDE);
    TF_CHECK(snap_has("Quit RETROINV?"));
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "Y=Yes N=No") != NULL);
    TF_CHECK(run(do_confirm, kn, 0));
    TF_EQ(g_pres, 0);
    TF_CHECK(run(do_confirm, kesc, 0));
    TF_EQ(g_pres, 0);
    /* An extended key (Alt-2, 179h) is no answer, though its low byte
       is the letter 'y'. */
    TF_CHECK(run(do_confirm, kalt, 0));
    TF_EQ(g_pres, 0);
    TF_EQ(tk_pos(), 2);
    TF_STREQ(tk_row(10), before);
}

/* tui_info_text gives the text tui_info last showed, as shown. */
static void test_info_text(void)
{
    char t[VID_COLS + 1];
    u16 i;

    tui_clear(0, VID_ROWS - 1);
    tui_info("Abc");
    TF_STREQ(tui_info_text(), "Abc");
    TF_STREQ(tk_row(TUI_STATROW) + TUI_ST_INFO, "Abc");
    /* Copied: the caller's buffer may change afterwards. */
    strcpy(t, "first");
    tui_info(t);
    strcpy(t, "other");
    TF_STREQ(tui_info_text(), "first");
    /* Longer than the field: the text kept is the text shown. */
    for (i = 0; i < VID_COLS; i++)
        t[i] = (char)('a' + i % 26);
    t[VID_COLS] = '\0';
    tui_info(t);
    TF_EQ(strlen(tui_info_text()), VID_COLS - TUI_ST_INFO);
    TF_STREQ(tui_info_text(), tk_row(TUI_STATROW) + TUI_ST_INFO);
    TF_CHECK(strncmp(tui_info_text(), t, VID_COLS - TUI_ST_INFO) == 0);
    tui_info("");
    TF_STREQ(tui_info_text(), "");
}

static int g_allocs;            /* far allocations, counted from 1      */
static int g_failat;            /* the first of them to fail, or 0      */

/* Counts far allocations; from the one numbered g_failat on, each fails
   as if memory had run out. */
static int count_alloc(u16 size)
{
    if (size > 0)
        g_allocs++;
    return g_failat != 0 && g_allocs >= g_failat;
}

static void do_showbox(void)
{
    tui_showbox("Tt", "Hello\nsecond line");
}

static void do_showbox2(void)
{
    tui_showbox(msg(MT_ERRTITLE), "abcdefghijklmnopqrstuvwxyz0123");
}

/* The box of tui_msgbox: a double frame tui_showbox centres in the work
   area, h = lines + 4 rows and w = the longest line + 4 columns (at
   least SB_MINW, and the title + 6), the text from the second row
   inside, two columns in.  Drawn without saving what it covers or
   reading a key; the hint row stays as it was. */
static void test_showbox(void)
{
    static const u16 none[] = { KEY_NONE };
    char under[VID_COLS + 1];
    char want[VID_COLS + 1];
    u8 top;
    u8 left;
    u8 h;
    u8 w;
    u8 r;

    tui_clear(0, VID_ROWS - 1);
    tui_text(10, 0, "underneath the box, which stays covered afterwards",
             TA_NORMAL);
    strcpy(under, tk_row(10));
    tui_hint("Esc=Back");
    g_allocs = 0;
    cmem_fault = count_alloc;
    TF_CHECK(run(do_showbox, none, 0));
    cmem_fault = NULL;
    TF_EQ(tk_pos(), 0);
    TF_EQ(g_allocs, 0);
    TF_STREQ(tk_row(TUI_HINTROW), " Esc=Back");

    /* Two lines, the longer of 11 characters: the least width. */
    h = 2 + 4;
    w = SB_MINW;
    top = (u8)(TUI_TOP + (TUI_WORKROWS - h) / 2);
    left = (u8)((VID_COLS - w) / 2);
    TF_EQ(tk_row(top)[left], TF_DTL);
    TF_EQ(tk_row(top)[left + w - 1], TF_DTR);
    TF_EQ(strlen(tk_row(top)), left + w);
    TF_CHECK(strstr(tk_row(top), " Tt ") != NULL);
    TF_EQ(tk_row((u8)(top + h - 1))[left], TF_DBL);
    TF_EQ(tk_row((u8)(top + h - 1))[left + w - 1], TF_DBR);
    TF_EQ(tk_attr(top, left), tui_attr(TA_FRAME));
    for (r = (u8)(top + 1); r < top + h - 1; r++) {
        TF_EQ(tk_row(r)[left], TF_DV);
        TF_EQ(tk_row(r)[left + w - 1], TF_DV);
        TF_EQ(strlen(tk_row(r)), left + w);
    }
    TF_EQ(tk_row((u8)(top + 1))[left + 1], ' ');
    TF_CHECK(tf_starts(tk_row((u8)(top + 2)) + left + 1, " Hello "));
    TF_CHECK(tf_starts(tk_row((u8)(top + 3)) + left + 1, " second line "));
    TF_EQ(tk_attr((u8)(top + 2), (u8)(left + 2)), tui_attr(TA_NORMAL));
    TF_EQ(tk_row((u8)(top - 1))[0], '\0');
    TF_EQ(tk_row((u8)(top + h))[0], '\0');
    /* What lies beside the box on a covered row is left alone. */
    TF_CHECK(top <= 10 && 10 < top + h);
    TF_CHECK(strncmp(tk_row(10), under, left) == 0);

    /* One line of 30 characters: w = 34; the hint row still as it was. */
    tui_clear(0, VID_ROWS - 1);
    tui_hint("F2=Save Esc=Cancel");
    g_allocs = 0;
    cmem_fault = count_alloc;
    TF_CHECK(run(do_showbox2, none, 0));
    cmem_fault = NULL;
    TF_EQ(g_allocs, 0);
    TF_STREQ(tk_row(TUI_HINTROW), " F2=Save Esc=Cancel");
    h = 1 + 4;
    w = 30 + 4;
    top = (u8)(TUI_TOP + (TUI_WORKROWS - h) / 2);
    left = (u8)((VID_COLS - w) / 2);
    TF_EQ(tk_row(top)[left], TF_DTL);
    TF_EQ(tk_row(top)[left + w - 1], TF_DTR);
    TF_CHECK(strstr(tk_row(top), " Error ") != NULL);
    strcpy(want, "abcdefghijklmnopqrstuvwxyz0123 ");
    want[strlen(want) + 1] = '\0';
    want[strlen(want)] = TF_DV;
    TF_STREQ(tk_row((u8)(top + 2)) + left + 2, want);
    TF_EQ(tk_row((u8)(top + h - 1))[left + w - 1], TF_DBR);
}

static const char help_text[] =
    "This file holds the help.\r\n"
    "@OTHER\r\n"
    "other text\r\n"
    "@items\r\n"
    "first help line\r\n"
    "second help line\n"
    /* 70 characters, the help box's text width (TUI_HELPW), then 4 more */
    "widewwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww"
    "wwwwwwwwwwwwwwwwwwwwwwwwwwedgeGONE\r\n"
    "@LAST\n"
    "last\n"
    "@MENU\n"
    "menu help line\n"
    "@KEYS\n"
    "keys help line\n";

static void do_key(void)
{
    g_ikey = tui_key();
}

static void do_help(void)
{
    tui_help("LAST");
}

/* Write help_text, which holds '@' only where a section starts, as the
   help file in g_tdir after an index of its sections as the build makes
   it, and the index line of a section without bytes, EMPTY. */
static void write_help(void)
{
    static const char empty[] = "=EMPTY 0000 0000\r\n";
    char path[PATHMAX + 16];
    char line[48];
    const char *p;
    const char *body;
    const char *end;
    u16 size;
    int pass;
    int h;

    size = sizeof empty - 1;
    strcpy(path, g_tdir);
    strcat(path, TUI_HELP);
    if (!TF_EQ(cf_create(path, &h), 0))
        return;
    /* The first pass adds up the size of the index, whose lines keep
       their length whatever the offsets; the second writes it. */
    for (pass = 0; pass < 2; pass++) {
        for (p = strchr(help_text, '@'); p != NULL; p = end) {
            body = strchr(p, '\n') + 1;
            end = strchr(body, '@');
            sprintf(line, "=%.*s %04X %04X\r\n",
                    (int)strcspn(p + 1, "\r\n"), p + 1,
                    (unsigned)(size + (body - help_text)),
                    (unsigned)((end != NULL ? end : body + strlen(body)) -
                               body));
            if (pass == 0)
                size = (u16)(size + strlen(line));
            else
                TF_EQ(cf_write(h, line, (u16)strlen(line)), 0);
        }
    }
    TF_EQ(cf_write(h, empty, (u16)(sizeof empty - 1)), 0);
    TF_EQ(cf_write(h, help_text, (u16)strlen(help_text)), 0);
    cf_close(h);
}

static void test_help(void)
{
    static const u16 k1[] = { KEY_F1, KEY_DOWN, KEY_F9, KEY_ESC, 'z',
                              KEY_NONE };
    static const u16 k2[] = { KEY_F1, 'q', KEY_NONE };
    static const u16 k3[] = { KEY_ENTER, KEY_NONE };
    static const u16 k4[] = { KEY_F9, KEY_ESC, 'z', KEY_NONE };
    static const u16 k5[] = { KEY_F1, KEY_F9, KEY_ESC, 'z', KEY_NONE };
    const char *old;

    write_help();
    tui_set_helpdir(g_tdir);
    tui_clear(0, VID_ROWS - 1);
    tui_hint("Esc=Back");
    old = tui_topic("ITEMS");
    TF_CHECK(run(do_key, k1, 2));
    TF_EQ(g_ikey, 'z');
    TF_EQ(tk_pos(), 5);
    TF_CHECK(snap_has(" Help "));
    TF_CHECK(snap_has("first help line"));
    TF_CHECK(snap_has("wwwwedge"));
    TF_CHECK(!snap_has("GONE"));
    TF_CHECK(snap_has("second help line"));
    TF_CHECK(!snap_has("other text"));
    TF_CHECK(!snap_has("last"));
    TF_STREQ(tk_row(TUI_HINTROW), " Esc=Back");
    TF_CHECK(!screen_has("help line"));

    /* F9 shows the keys' section, from a screen and from the help box. */
    TF_CHECK(run(do_key, k4, 2));
    TF_EQ(g_ikey, 'z');
    TF_CHECK(snap_has("keys help line"));
    TF_CHECK(run(do_key, k5, 3));
    TF_EQ(g_ikey, 'z');
    TF_CHECK(snap_has("keys help line"));
    TF_CHECK(!snap_has("first help line"));
    TF_CHECK(!screen_has("help line"));

    /* Called directly, tui_help shows the section it is given, not the
       topic's, until Enter, and puts back what it covered. */
    TF_CHECK(run(do_help, k3, 1));
    TF_EQ(tk_pos(), 1);
    TF_CHECK(snap_has(" Help "));
    TF_CHECK(snap_has("last"));
    TF_CHECK(!snap_has("first help line"));
    TF_CHECK(!screen_has("last"));
    TF_STREQ(tui_topic("NOWHERE"), "ITEMS");

    TF_CHECK(run(do_key, k2, 2));
    TF_EQ(g_ikey, 'q');
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "No help: NOWHERE") != NULL);
    tui_topic("EMPTY");
    TF_CHECK(run(do_key, k2, 2));
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "No help: EMPTY") != NULL);
    TF_STREQ(tui_topic(old), "EMPTY");
}

/* When the cells a dialog covers cannot be saved, it does not open: it
   shows MT_NOMEM, reads no key and gives its cancel answer; tui_msgbox
   shows its text on the hint row instead.  The help box fails at its
   second far allocation, the save after the block of its lines. */
static void test_nomem(void)
{
    static const u16 ky[] = { 'y', KEY_ENTER, KEY_NONE };
    static const u16 kz[] = { KEY_F1, 'z', KEY_NONE };
    const char *old;

    tui_clear(0, VID_ROWS - 1);
    tui_hint("Esc=Back");
    g_allocs = 0;
    g_failat = 1;
    cmem_fault = count_alloc;
    g_pres = -1;
    TF_CHECK(run(do_confirm, ky, 0));
    TF_EQ(g_pres, 0);
    TF_EQ(tk_pos(), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MT_NOMEM)) != NULL);
    TF_CHECK(!screen_has("Quit"));
    g_pres = -1;
    TF_CHECK(run(do_prompt, ky, 0));
    TF_EQ(g_pres, 0);
    TF_EQ(tk_pos(), 0);
    g_pn = 30;
    g_psel = 0;
    g_pres = -1;
    TF_CHECK(run(do_pick, ky, 0));
    TF_EQ(g_pres, 0);
    TF_EQ(tk_pos(), 0);
    TF_CHECK(!screen_has("entry"));
    TF_CHECK(run(do_msgbox, ky, 0));
    TF_EQ(tk_pos(), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), "A long text that") != NULL);
    TF_CHECK(!screen_has(" Error "));
    TF_EQ(g_allocs, 4);

    g_allocs = 0;
    g_failat = 2;
    g_ikey = 0;
    old = tui_topic("ITEMS");
    TF_CHECK(run(do_key, kz, 2));
    TF_EQ(g_ikey, 'z');
    TF_EQ(g_allocs, 2);
    TF_CHECK(strstr(g_snap[TUI_HINTROW], msg(MT_NOMEM)) != NULL);
    TF_CHECK(!snap_has("help line"));
    tui_topic(old);
    cmem_fault = NULL;
    g_failat = 0;
}

/* Menus, with labels from the test's own messages (see main). */
static const tui_item items1[] = {
    { 1193, 1, 7 },             /* &Browse                              */
    { 1194, 2, 8 },             /* &New, level 2                        */
    { 1195, 3, 10 }             /* &Loans, level 3                      */
};
static const tui_item items2[] = {
    { 1196, 3, 11 },            /* &Users, level 3                      */
    { 1197, 3, 12 }             /* &Pack, level 3                       */
};
static const tui_item items3[] = {
    { 1190, 1, TUI_CMD_QUIT },  /* Save && &Quit                        */
    { 1199, 3, 9 }              /* &Settings, level 3                   */
};

static u8 g_cmd_seen[8];
static u8 g_ncmd;
static u8 g_relogin;            /* logins that ask to be run again      */
static u8 g_nblank;             /* logins entered with the user cleared */

static u8 scr_login(u8 cmd)
{
    if (g_ncmd < sizeof g_cmd_seen)
        g_cmd_seen[g_ncmd++] = cmd;
    if (tui_s.level == 0 && tui_s.user[0] == '\0' && tui_s.name[0] == '\0')
        g_nblank++;
    tui_s.level = 1;
    strcpy(tui_s.user, "BOB");
    strcpy(tui_s.name, "Bob");
    if (g_relogin > 0) {
        g_relogin--;
        return TUI_CMD_LOGIN;
    }
    return TUI_CMD_NONE;
}

static u8 scr_home(u8 cmd)
{
    u16 k;
    int used;
    u8 c;

    if (g_ncmd < sizeof g_cmd_seen)
        g_cmd_seen[g_ncmd++] = cmd;
    for (;;) {
        k = tui_key();
        c = tui_global(k, &used);
        if (used)
            return c;
    }
}

static u8 scr_run(u8 cmd)
{
    if (g_ncmd < sizeof g_cmd_seen)
        g_cmd_seen[g_ncmd++] = cmd;
    return cmd == 7 ? TUI_CMD_LOGIN : TUI_CMD_NONE;
}

static const tui_item items5[] = {
    { 1189, 1, 7 }              /* a label wider than the screen        */
};

static tui_menu g_menus[5];
static tui_app g_app;

static void do_run(void)
{
    tui_run(&g_app);
}

static u8 g_gcmd;
static int g_gused;
static u16 g_gkey;

static void do_global(void)
{
    g_gcmd = tui_global(g_gkey, &g_gused);
}

static void test_menus(void)
{
    /* The first login asks to be run again; then F10, Down to the level
       2 entry, Enter (refused), Up, Enter: command 7, which logs out;
       after the login F10, Right twice to Options, Enter on its quit
       entry, Y.  Each login starts with the session cleared. */
    static const u16 k1[] = { KEY_F10, KEY_DOWN, KEY_ENTER, KEY_UP,
                              KEY_ENTER, KEY_F10, KEY_RIGHT, KEY_RIGHT,
                              KEY_ENTER, 'Y', KEY_NONE };

    g_menus[0].label = 1191;              /* &Items                     */
    g_menus[0].nitems = 3;
    g_menus[0].items = items1;
    g_menus[1].label = 1192;              /* &Admin                     */
    g_menus[1].nitems = 2;
    g_menus[1].items = items2;
    g_menus[2].label = 1198;              /* &Options                   */
    g_menus[2].nitems = 2;
    g_menus[2].items = items3;
    g_app.nmenus = 3;
    g_app.menus = g_menus;
    g_app.run = scr_run;
    g_app.home = scr_home;
    g_app.login = scr_login;
    g_app.search = 20;

    g_ncmd = 0;
    g_nblank = 0;
    g_relogin = 1;
    tui_s.level = 0;
    TF_CHECK(run(do_run, k1, 4));
    TF_EQ(tk_pos(), 10);
    TF_EQ(g_ncmd, 6);
    TF_EQ(g_nblank, 3);
    TF_EQ(g_cmd_seen[0], TUI_CMD_LOGIN);
    TF_EQ(g_cmd_seen[1], TUI_CMD_LOGIN);
    TF_EQ(g_cmd_seen[2], TUI_CMD_NONE);
    TF_EQ(g_cmd_seen[3], 7);
    TF_EQ(g_cmd_seen[4], TUI_CMD_LOGIN);
    TF_EQ(g_cmd_seen[5], TUI_CMD_NONE);
    TF_CHECK(strstr(g_snap[TUI_HINTROW],
                    "Permission denied (your access level is 1)") != NULL);

    /* After tui_run the menus are gone; tui_global needs an app, so
       the rest runs inside a fresh tui_run. */
    tui_s.level = 1;
    TF_CHECK(strncmp(tk_row(TUI_STATROW) + TUI_ST_USER, "BOB 1", 5) == 0);
}

static void global_keys(void)
{
    static const u16 kd[] = { KEY_DOWN, KEY_ESC, KEY_NONE };
    static const u16 kf[] = { KEY_F10, KEY_NONE };
    static const u16 kl[] = { KEY_LEFT, KEY_DOWN, KEY_ESC, KEY_NONE };
    static const u16 kb[] = { 'b', KEY_NONE };
    static const u16 ka[] = { KEY_ENTER, KEY_ESC, KEY_NONE };
    static const u16 ks[] = { 's', KEY_ESC, KEY_NONE };
    static const u16 kn[] = { 'N', KEY_NONE };
    static const u16 ky[] = { 'y', KEY_NONE };
    static const u16 kw[] = { KEY_LEFT, KEY_DOWN, KEY_LEFT, KEY_DOWN,
                              KEY_UP, KEY_HOME, KEY_END, KEY_ENTER, 'x',
                              KEY_ESC, KEY_NONE };
    static const u16 kw2[] = { KEY_LEFT, KEY_ENTER, KEY_NONE };
    static const u16 kh[] = { KEY_F1, KEY_ESC, KEY_ESC, KEY_NONE };
    const char *old;
    u8 c;

    /* The menu bar: Admin is dimmed, all its entries needing level 3;
       Options has one allowed entry. */
    tui_frame();
    TF_CHECK(strncmp(tk_row(TUI_MENUROW), " Items  Admin  Options", 22) == 0);
    TF_EQ(tk_attr(TUI_MENUROW, 1), tui_attr(TA_MENUHOT));
    TF_EQ(tk_attr(TUI_MENUROW, 2), tui_attr(TA_MENU));
    TF_EQ(tk_attr(TUI_MENUROW, 8), tui_attr(TA_MENUOFF));
    TF_EQ(tk_attr(TUI_MENUROW, 9), tui_attr(TA_MENUOFF));
    TF_EQ(tk_attr(TUI_MENUROW, 15), tui_attr(TA_MENUHOT));

    g_gkey = KEY_F10;
    TF_CHECK(run(do_global, kd, 2));
    TF_EQ(g_gused, 0);
    TF_EQ(g_gcmd, TUI_CMD_NONE);
    TF_EQ(g_snapattr[TUI_TOP + 1][2], tui_attr(TA_MENUHOT));
    TF_EQ(g_snapattr[TUI_TOP + 1][3], tui_attr(TA_MENU));
    TF_EQ(g_snapattr[TUI_TOP + 2][2], tui_attr(TA_SELECT));
    TF_EQ(g_snapattr[TUI_TOP + 3][2], tui_attr(TA_MENUOFF));
    TF_EQ(g_snapattr[TUI_MENUROW][1], tui_attr(TA_SELECT));
    TF_CHECK(strncmp(g_snap[TUI_TOP + 2] + 2, "New", 3) == 0);
    TF_CHECK(!screen_has("Browse"));

    /* F10 closes the menu it opened. */
    TF_CHECK(run(do_global, kf, 0));
    TF_EQ(g_gused, 0);
    TF_EQ(tk_pos(), 1);
    TF_CHECK(!screen_has("Browse"));

    /* Left wraps to Options; "&&" is a literal '&'. */
    TF_CHECK(run(do_global, kl, 3));
    TF_CHECK(strncmp(g_snap[TUI_TOP + 1] + 16, "Save & Quit", 11) == 0);
    TF_EQ(g_snapattr[TUI_TOP + 1][23], tui_attr(TA_MENUHOT));
    TF_EQ(g_snapattr[TUI_TOP + 2][16], tui_attr(TA_SELECT));

    /* F1 in an open menu shows the MENU help, whatever the screen's
       topic; the topic is the screen's again once the menu closes. */
    old = tui_topic("HOME");
    TF_CHECK(run(do_global, kh, 2));
    TF_EQ(g_gused, 0);
    TF_CHECK(snap_has("menu help line"));
    TF_STREQ(tui_topic(old), "HOME");

    /* A letter chooses at once. */
    TF_CHECK(run(do_global, kb, 0));
    TF_EQ(g_gcmd, 7);
    TF_EQ(g_gused, 1);

    /* A dimmed menu still opens; its entries are refused. */
    g_gkey = (u16)(KEY_EXT + 0x1E);
    TF_CHECK(run(do_global, ka, 2));
    TF_EQ(g_gused, 0);
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "your access level is 1") != NULL);
    TF_CHECK(strncmp(g_snap[TUI_TOP + 1] + 9, "Users", 5) == 0);

    /* Alt+O, then a letter of an entry above the level. */
    g_gkey = (u16)(KEY_EXT + 0x18);
    TF_CHECK(run(do_global, ks, 2));
    TF_EQ(g_gused, 0);
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "your access level is 1") != NULL);

    /* Alt with a letter no menu has is not global. */
    TF_EQ(tui_global((u16)(KEY_EXT + 0x2C), &g_gused), TUI_CMD_NONE);
    TF_EQ(g_gused, 0);

    g_gkey = KEY_CTRL('F');
    TF_CHECK(run(do_global, kn, 0));
    TF_EQ(g_gcmd, 20);
    TF_EQ(g_gused, 1);
    TF_EQ(tk_pos(), 0);
    g_gkey = KEY_F3;
    TF_CHECK(run(do_global, kn, 0));
    TF_EQ(g_gused, 0);
    TF_CHECK(run(do_global, ky, 0));
    TF_EQ(g_gused, 1);
    TF_EQ(g_gcmd, TUI_CMD_QUIT);
    c = tui_global('x', &g_gused);
    TF_EQ(c, TUI_CMD_NONE);
    TF_EQ(g_gused, 0);
    tui_s.level = 0;
    g_gkey = KEY_CTRL('F');
    TF_CHECK(run(do_global, kn, 0));
    TF_EQ(g_gused, 0);
    tui_s.level = 1;

    /* A menu without entries and one with an over-long entry. */
    g_menus[3].label = MT_NOTFOUND;
    g_menus[3].nitems = 0;
    g_menus[3].items = items5;
    g_menus[4].label = MT_DUP;
    g_menus[4].nitems = 1;
    g_menus[4].items = items5;
    g_app.nmenus = 5;
    g_gkey = KEY_F10;
    TF_CHECK(run(do_global, kw, 2));
    TF_EQ((u8)g_snap[TUI_TOP + 1][VID_COLS - 1], (u8)TF_SV);
    TF_EQ((u8)g_snap[TUI_TOP + 1][0], (u8)TF_SV);
    TF_EQ(g_gused, 0);
    TF_EQ(g_gcmd, TUI_CMD_NONE);
    TF_CHECK(run(do_global, kw2, 0));
    TF_EQ(g_gcmd, 7);
    g_app.nmenus = 3;

    /* A menu whose cells cannot be saved does not open: MT_NOMEM, no
       key read, the bar and the help topic as before. */
    old = tui_topic("HOME");
    g_failat = 1;
    cmem_fault = count_alloc;
    TF_CHECK(run(do_global, kn, 0));
    cmem_fault = NULL;
    g_failat = 0;
    TF_STREQ(tui_topic(old), "HOME");
    TF_EQ(g_gused, 0);
    TF_EQ(g_gcmd, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MT_NOMEM)) != NULL);
    TF_CHECK(!screen_has("Browse"));
    TF_EQ(tk_attr(TUI_MENUROW, 1), tui_attr(TA_MENUHOT));
}

/* The Record menu: three verbs (texts from the test's own messages, from
   1193 on: &Browse naming the key Ctrl-PgDn, &New naming Ctrl-E, with
   level 2, &Loans naming none), a verb's key given by the next tui_key,
   an entry above the level refused, Left from the first menu to it, the
   bar with and without it. */

static void do_rkey(void)
{
    g_ikey = tui_key();
}

static void rec_keys(void)
{
    static const u16 kl[] = { 'l', KEY_NONE };
    static const u16 kn[] = { 'n', KEY_ESC, KEY_NONE };
    static const u16 kz[] = { 'z', KEY_NONE };
    static const u16 kf[] = { KEY_LEFT, KEY_ENTER, KEY_NONE };

    TF_CHECK(tui_record(7) == 0);
    TF_STREQ(tk_row(TUI_MENUROW), " Items  Admin  Options  Record");
    g_gkey = (u16)(KEY_EXT + 0x2E);             /* Alt-C */
    TF_CHECK(run(do_global, kl, 1));
    TF_CHECK(strncmp(g_snap[TUI_TOP + 3] + 25, "Loans", 5) == 0);
    TF_EQ(g_gused, 0);
    TF_EQ(g_gcmd, TUI_CMD_NONE);
    TF_CHECK(run(do_rkey, kz, 0));
    TF_EQ(g_ikey, TUI_VERBKEY + 3);
    TF_EQ(tk_pos(), 0);
    TF_CHECK(run(do_rkey, kz, 0));
    TF_EQ(g_ikey, 'z');
    /* New needs level 2: refused, and no key is given. */
    TF_CHECK(run(do_global, kn, 2));
    TF_CHECK(strstr(g_snap[TUI_HINTROW], "your access level is 1") != NULL);
    TF_CHECK(run(do_rkey, kz, 0));
    TF_EQ(g_ikey, 'z');
    /* Left from the first menu opens the last, the Record menu; Enter on
       its first entry gives that verb's key. */
    g_gkey = KEY_F10;
    TF_CHECK(run(do_global, kf, 0));
    TF_CHECK(run(do_rkey, kz, 0));
    TF_EQ(g_ikey, KEY_CPGDN);
    TF_EQ(tk_attr(TUI_MENUROW, 26), tui_attr(TA_MENUHOT));
    /* A Record menu of only New, above the level, is drawn dimmed. */
    TF_CHECK(tui_record(2) == 7);
    TF_EQ(tk_attr(TUI_MENUROW, 26), tui_attr(TA_MENUOFF));
    TF_CHECK(tui_record(4) == 2);
    TF_CHECK(run(do_global, kf, 2));
    TF_CHECK(strncmp(g_snap[TUI_TOP + 1] + 25, "Loans", 5) == 0);
    TF_CHECK(run(do_rkey, kz, 0));
    TF_EQ(g_ikey, TUI_VERBKEY + 3);
    TF_CHECK(tui_record(0) == 4);
    TF_STREQ(tk_row(TUI_MENUROW), " Items  Admin  Options");
}

/* tui_global works while tui_run runs: the home screen of this app
   runs the global key checks, then those of the Record menu. */
static u8 gk_home(u8 cmd)
{
    g_cmd_seen[0] = cmd;
    global_keys();
    rec_keys();
    return TUI_CMD_QUIT;
}

static void do_run2(void)
{
    g_app.home = gk_home;
    tui_verbs(2, 1193);
    tui_run(&g_app);
    g_app.home = scr_home;
    tui_verbs(0, 0);
}

static void test_globals(void)
{
    static const u16 none[] = { KEY_NONE };

    TF_CHECK(run(do_run2, none, 0));
}

/* The text of the server's -ERR line in test_failures; only 102 and 202
   show it. */
static const char k_why[] = "server reason";

static void fail_one(int code, const char *text, int closed, u8 want,
                     const char *shown)
{
    static const u16 k[] = { KEY_ENTER, KEY_NONE };
    rq_res res;
    u8 c;

    memset(&res, 0, sizeof res);
    res.code = code;
    res.closed = (u8)closed;
    strcpy(res.text, text);
    tui_hint("Esc=Back");
    feed(k, 1);
    c = tui_fail(&res);
    TF_EQ(c, want);
    if (tk_pos() == 0)
        grab();
    if (!TF_CHECK(snap_has(shown)))
        printf("  code %d: \"%s\" not shown\n", code, shown);
    /* The server's text from its second letter: its first is shown in
       upper case. */
    if (text[0] != '\0')
        TF_EQ(snap_has(text + 1), code == RQ_EARG || code == RQ_ECONSTR);
}

static rq_res g_lostres;

static void do_lost(void)
{
    tui_fail(&g_lostres);
}

/* A stand-in for the idle hook, which tui_fatal must remove. */
static void no_idle(void)
{
}

/* A lost NET link: tui_fail shows it, with the warning that a change
   may have been made, and ends the program with TUI_EXIT_LOST after a
   key, the idle hook removed, and also when the key script ends at the
   box. */
static void test_lost(void)
{
    static const u16 k[] = { KEY_ENTER, KEY_NONE };

    memset(&g_lostres, 0, sizeof g_lostres);
    g_lostres.code = RQ_ELOST;
    g_lostres.closed = 1;
    tk_status = -1;
    key_idle = no_idle;
    TF_EQ(run(do_lost, k, 1), 0);
    TF_CHECK(key_idle == NULL);
    TF_EQ(tk_status, TUI_EXIT_LOST);
    TF_CHECK(snap_has("Connection to the server lost"));
    TF_CHECK(snap_has("it may have been made"));
    tk_status = -1;
    TF_EQ(run(do_lost, k + 1, 0), 0);
    TF_EQ(tk_status, TUI_EXIT_LOST);
    TF_CHECK(screen_has("Connection to the server lost"));
    tui_clear(TUI_TOP, TUI_BOTTOM);
}

/* The link RQ.C sends tui_req's requests on: the request line is kept
   and answered with the lines of g_resp. */
int link_send(link *l, const char *line, u16 len, link_line_fn fn,
              void *arg)
{
    char ln[LINK_MAXLINE + 1];
    const char *p;
    u16 n;

    p = g_resp;
    g_link = l;
    g_nsent++;
    if (len > LINK_MAXLINE)
        len = LINK_MAXLINE;
    memcpy(g_sent, line, len);
    g_sent[len] = '\0';
    if (g_sendrc != LINK_OK)
        return g_sendrc;
    if (g_nested && key_idle != NULL)
        (*key_idle)();
    while (*p != '\0') {
        for (n = 0; p[n] != '\n'; n++)
            ln[n] = p[n];
        ln[n] = '\0';
        if (fn(arg, ln, n) != 0)
            return LINK_EOUTPUT;
        p += n + 1;
    }
    return LINK_OK;
}

static int req_line(void *arg, const rq_line *ln)
{
    if (ln->kind == 'L')
        strcpy((char *)arg, ln->v[0]);
    return 0;
}

static void test_requests(void)
{
    static const u16 k[] = { KEY_ENTER, KEY_NONE };
    char got[RQ_TEXTLEN + 1];
    rq_res res;
    u8 cmd;

    cmd = 9;
    tui_clear(0, VID_ROWS - 1);
    g_resp = "+DATA\nL shelf two\n.\n";
    got[0] = '\0';
    TF_EQ(tui_req("LOC GET", "A 1", req_line, got, &res, &cmd), 1);
    TF_STREQ(g_sent, "LOC GET \"A 1\"");
    TF_CHECK(g_link == tui_s.l);
    /* The status line names the kind of a link, which this test's
       link_send never looks at. */
    tui_s.l = (link *)g_sent;
    tui_s.conn = TUI_CONN_COM;
    tui_frame();
    TF_CHECK(strncmp(tk_row(TUI_STATROW) + TUI_ST_CONN, "COM ", 4) == 0);
    tui_s.conn = TUI_CONN_NET;
    tui_frame();
    TF_CHECK(strncmp(tk_row(TUI_STATROW) + TUI_ST_CONN, "NET ", 4) == 0);
    tui_s.conn = TUI_CONN_LOCAL;
    tui_frame();
    TF_CHECK(strncmp(tk_row(TUI_STATROW) + TUI_ST_CONN, "LOCAL", 5) == 0);
    tui_s.l = NULL;
    TF_STREQ(got, "shelf two");
    TF_EQ(res.code, RQ_OK);
    TF_EQ(cmd, TUI_CMD_NONE);
    g_resp = "+OK\n";
    cmd = 9;
    TF_EQ(tui_req("ITEM DEL", "7", NULL, NULL, NULL, &cmd), 1);
    TF_STREQ(g_sent, "ITEM DEL 7");
    TF_EQ(cmd, TUI_CMD_NONE);
    g_resp = "-ERR 200 not found\n";
    cmd = 9;
    TF_EQ(tui_req("ITEM GET", "8", NULL, NULL, &res, &cmd), 0);
    TF_EQ(res.code, RQ_ENOTFOUND);
    TF_EQ(cmd, TUI_CMD_NONE);
    TF_STREQ(tk_row(TUI_HINTROW), " The record no longer exists");
    g_resp = "-ERR 400 session expired\n";
    cmd = 9;
    feed(k, 0);
    TF_EQ(tui_req("ITEM GET", "8", NULL, NULL, NULL, &cmd), 0);
    TF_EQ(cmd, TUI_CMD_LOGIN);
}

/* Ticks the idle hook may wait before its first PING: about half a
   minute, at least IDLE_MIN and at most IDLE_MAX, well under the
   server's least IDLE= of one minute. */
#define IDLE_MIN        (2 * TMR_10SEC)
#define IDLE_MAX        (4 * TMR_10SEC)

/* The calls of the idle hook that sent a PING, counted from 1, in the
   host's virtual ticks, each call of the hook taking one. */
static u16 g_ping1;
static u16 g_ping2;

/* Call the idle hook as key_read does while it waits, up to n times or
   until a second request is sent; on the host, note which calls sent
   them. */
static void idle_calls(u16 n)
{
    u16 i;
    u16 before;

    g_ping1 = 0;
    g_ping2 = 0;
    for (i = 1; i <= n && key_idle != NULL && g_ping2 == 0; i++) {
        before = g_nsent;
        (*key_idle)();
        if (g_nsent != before && g_ping1 == 0)
            g_ping1 = i;
        else if (g_nsent != before)
            g_ping2 = i;
    }
}

#ifdef COMPAT_HOST
/* The host build links with dos_today and rq_send wrapped
   (T_TUI_HOSTWRAP), so that the idle hook's reads of the date are
   counted, and a PING dropped while a request is under way is seen not
   to reach rq_send. */
static u16 g_todays;
static u16 g_rqsends;

void __real_dos_today(u16 *year, u8 *month, u8 *day);
void __wrap_dos_today(u16 *year, u8 *month, u8 *day);
int __real_rq_send(link *l, rq *q, rq_fn fn, void *arg, rq_res *res);
int __wrap_rq_send(link *l, rq *q, rq_fn fn, void *arg, rq_res *res);

void __wrap_dos_today(u16 *year, u8 *month, u8 *day)
{
    g_todays++;
    __real_dos_today(year, month, day);
}

int __wrap_rq_send(link *l, rq *q, rq_fn fn, void *arg, rq_res *res)
{
    g_rqsends++;
    return __real_rq_send(l, q, fn, arg, res);
}

/* On the host, whose clock is virtual: a PING one interval of IDLE_MIN
   to IDLE_MAX ticks after tui_run started, IDLE_MIN / 2 of them gone
   already, and the next one interval later; a PING due while a request
   is under way dropped before rq_send, the hook kept; then a lost link,
   which ends the program (TUI_EXIT_LOST), so this never returns. */
static void idle_net(void)
{
    rq q;
    rq_res res;
    u16 d;

    idle_calls(3 * IDLE_MAX);
    TF_STREQ(g_sent, "PING");
    d = (u16)(g_ping2 - g_ping1);
    TF_CHECK(d >= IDLE_MIN && d <= IDLE_MAX);
    TF_EQ(g_ping1 + IDLE_MIN / 2, d);
    g_nsent = 0;
    g_nested = 1;
    idle_calls((u16)(d - 1));
    rq_begin(&q, "VER");
    g_rqsends = 0;
    TF_EQ(rq_send(tui_s.l, &q, NULL, NULL, &res), RQ_OK);
    g_nested = 0;
    TF_EQ(g_nsent, 1);
    TF_EQ(g_rqsends, 1);
    TF_CHECK(key_idle != NULL);
    g_sendrc = LINK_ELOST;
    idle_calls((u16)(d + 1));
    TF_CHECK(0);
}

/* On the host: a serial link's PINGs as NET's; then a session ended
   but not lost (LINK_CLOSED, as a shutdown's -ERR 501 leaves it), which
   tui_fail reports in a box and which ends the program with 0, so this
   never returns. */
static void idle_com(void)
{
    idle_calls(3 * IDLE_MAX);
    TF_STREQ(g_sent, "PING");
    TF_CHECK(g_ping2 != 0);
    g_sendrc = LINK_CLOSED;
    idle_calls(3 * IDLE_MAX);
    TF_CHECK(0);
}
#endif

/* The login function of the idle hook's runs, inside tui_run: on LOCAL
   no PING ever, and on the host the date read once as the tick count
   passes midnight, as it does once in TMR_DAY of the hook's calls; on a
   remote link no PING within IDLE_MIN / 2 ticks, and on the host the
   checks of idle_net or idle_com, the hook removed by the end they
   lead to. */
static u8 idle_login(u8 cmd)
{
#ifdef COMPAT_HOST
    static const u16 k[] = { KEY_ENTER, KEY_NONE };
    u32 i;
#endif

    if (cmd != TUI_CMD_LOGIN || !TF_CHECK(key_idle != NULL))
        return TUI_CMD_QUIT;
    g_nsent = 0;
    if (tui_s.conn == TUI_CONN_LOCAL) {
        idle_calls(3 * IDLE_MAX);
        TF_EQ(g_nsent, 0);
#ifdef COMPAT_HOST
        g_todays = 0;
        for (i = 0; i < TMR_DAY; i++)
            (*key_idle)();
        TF_EQ(g_todays, 1);
#endif
        return TUI_CMD_QUIT;
    }
    idle_calls(IDLE_MIN / 2);
    TF_EQ(g_nsent, 0);
#ifdef COMPAT_HOST
    tk_status = -1;
    feed(k, 1);
    if (setjmp(tk_jb) == 0) {
        if (tui_s.conn == TUI_CONN_NET)
            idle_net();
        else
            idle_com();
    }
    g_sendrc = LINK_OK;
    g_nested = 0;
    if (tui_s.conn == TUI_CONN_NET) {
        TF_EQ(tk_status, TUI_EXIT_LOST);
        TF_CHECK(snap_has("Connection to the server lost"));
    } else {
        TF_EQ(tk_status, 0);
        TF_CHECK(snap_has("The session has ended"));
    }
    TF_CHECK(key_idle == NULL);
#endif
    return TUI_CMD_QUIT;
}

static void test_idle(void)
{
    static const tui_app app = { 0, NULL, NULL, NULL, idle_login, 0 };

    tui_s.l = (link *)g_sent;
    g_resp = "+OK\n";
    tui_s.conn = TUI_CONN_NET;
    tui_run(&app);
    TF_CHECK(key_idle == NULL);
    tui_s.conn = TUI_CONN_COM;
    tui_run(&app);
    TF_CHECK(key_idle == NULL);
    tui_s.conn = TUI_CONN_LOCAL;
    tui_run(&app);
    TF_CHECK(key_idle == NULL);
    tui_s.l = NULL;
    tui_clear(0, VID_ROWS - 1);
}

static void test_failures(void)
{
    tui_s.level = 1;
    tui_clear(0, VID_ROWS - 1);
    fail_one(RQ_ENOTAVAIL, k_why, 0, TUI_CMD_NONE,
             "Not available in local mode");
    fail_one(RQ_ENOTFOUND, k_why, 0, TUI_CMD_NONE,
             "The record no longer exists");
    fail_one(RQ_EDUP, k_why, 0, TUI_CMD_NONE, "Already exists");
    fail_one(RQ_EARG, "bad quantity", 0, TUI_CMD_NONE, "Bad quantity");
    fail_one(RQ_ECONSTR, "location holds items", 0, TUI_CMD_NONE,
             "Location holds items");
    fail_one(RQ_EPERM, k_why, 0, TUI_CMD_NONE,
             "Permission denied (your access level is 1)");
    fail_one(RQ_ECURSOR, k_why, 0, TUI_CMD_NONE,
             "The list has changed; starting again at the top");
    fail_one(RQ_EIO, k_why, 0, TUI_CMD_NONE,
             "Database error 300: a read or write error");
    fail_one(RQ_EINDEX, k_why, 0, TUI_CMD_NONE,
             "Rebuild the indexes: Admin > Reindex, or DBPACK /R");
    TF_CHECK(snap_has("Database error 301: an index is damaged"));
    fail_one(RQ_EINUSE, k_why, 0, TUI_CMD_NONE,
             "Database error 304: a file is in use by another program");
    fail_one(RQ_EBADPW, k_why, 0, TUI_CMD_NONE,
             "Invalid user name or password");
    fail_one(RQ_EDISABLED, k_why, 0, TUI_CMD_NONE, "Account disabled");
    fail_one(RQ_EBUSY, k_why, 0, TUI_CMD_NONE,
             "Other sessions are open; this needs the system to itself");
    fail_one(RQ_ENOLOGIN, k_why, 0, TUI_CMD_LOGIN, "Session expired");
    fail_one(RQ_EMUSTCHG, k_why, 0, TUI_CMD_LOGIN, "Password change required");
    fail_one(RQ_ESHUTDOWN, k_why, 0, TUI_CMD_QUIT, "The session has ended");
    fail_one(RQ_ECLOSED, "", 1, TUI_CMD_QUIT, "The session has ended");
    fail_one(RQ_EBADPW, "", 1, TUI_CMD_QUIT, "The session has ended");
    fail_one(RQ_ESYNTAX, k_why, 0, TUI_CMD_NONE, "Internal error 100");
    fail_one(RQ_EINTERNAL, k_why, 0, TUI_CMD_NONE, "Internal error 500");
    fail_one(999, k_why, 0, TUI_CMD_NONE, "Internal error 999");

    tui_hint("Esc=Back");
    TF_EQ(tui_allow(1), 1);
    TF_STREQ(tk_row(TUI_HINTROW), " Esc=Back");
    TF_EQ(tui_allow(3), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), "your access level is 1") != NULL);
}

static void test_script_end(void)
{
    static const u16 k[] = { 'a', KEY_NONE };

    tk_status = -1;
    TF_EQ(run(do_key, k, 0), 1);
    TF_EQ(run(do_key, k + 1, 0), 0);
    TF_EQ(tk_status, 0);
    tk_status = -1;
    if (setjmp(tk_jb) == 0) {
        tui_exit(5);
        TF_CHECK(0);
    }
    TF_EQ(tk_status, 5);
}

/* The built INV.MSG with the test's own menu labels added, as INV.MSG
   in the scratch directory; loaded from there.  They take the top of
   the numbers no module uses, so they are read as a page. */
static const char extra[] =
    "1190 Save && &Quit\r\n"
    "1191 &Items\r\n"
    "1192 &Admin\r\n"
    "1193 &Browse  Ctrl-PgDn\r\n"
    "1194 &New  Ctrl-E\r\n"
    "1195 &Loans\r\n"
    "1196 &Users\r\n"
    "1197 &Pack\r\n"
    "1198 &Options\r\n"
    "1199 &Settings\r\n";

/* The start of a line of MSG_MAXLEN bytes of text, after extra. */
static const char k_long[] = "1189 ";

static int load_test_messages(const char *mdir)
{
    char path[PATHMAX + 16];
    char buf[256];
    int in;
    int out;
    u16 got;
    int rc;

    strcpy(path, mdir);
    strcat(path, MSG_FILE);
    rc = cf_open(path, CF_READ, &in);
    if (rc != 0)
        return rc;
    strcpy(path, g_tdir);
    strcat(path, MSG_FILE);
    rc = cf_create(path, &out);
    if (rc != 0) {
        cf_close(in);
        return rc;
    }
    for (;;) {
        rc = cf_read(in, buf, sizeof buf, &got);
        if (rc != 0 || got == 0)
            break;
        rc = cf_write(out, buf, got);
        if (rc != 0)
            break;
    }
    if (rc == 0)
        rc = cf_write(out, extra, (u16)strlen(extra));
    if (rc == 0) {
        strcpy(buf, k_long);
        memset(buf + sizeof k_long - 1, 'W', MSG_MAXLEN);
        strcpy(buf + sizeof k_long - 1 + MSG_MAXLEN, "\r\n");
        rc = cf_write(out, buf, (u16)strlen(buf));
    }
    cf_close(in);
    cf_close(out);
    if (rc == 0)
        rc = msg_load(g_tdir, MSG_FILE);
    return rc;
}

/* Remove the files the test makes in g_tdir. */
static void cleanup(void)
{
    char path[PATHMAX + 16];
    u16 i;

    strcpy(path, g_tdir);
    strcat(path, TUI_HELP);
    cf_remove(path);
    for (i = 0; k_files[i] != NULL; i++) {
        strcpy(path, g_tdir);
        strcat(path, k_files[i]);
        cf_remove(path);
    }
}

int main(int argc, char **argv)
{
    char mdir[PATHMAX];

    tf_begin("T_TUI");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_TUI dir [msgdir]\n");
        return 2;
    }
    strcpy(g_tdir, argv[1]);
    tf_progdir(mdir, PATHMAX, argv[0]);
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    cleanup();
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0) ||
        !TF_EQ(load_test_messages(mdir), 0)) {
        printf("T_TUI: %s\n", msg_why());
        return tf_done();
    }
    if (!TF_EQ(vid_open(0), 0))
        return tf_done();
    tui_set_exit(tk_exit);
    test_palettes();
    test_drawing();
    test_hints();
    test_key_one();
    test_key_break();
    test_input();
    test_prompt();
    test_prompt_wide();
    test_pick();
    test_dialogs();
    test_info_text();
    test_showbox();
    test_help();
    test_nomem();
    test_menus();
    test_globals();
    test_failures();
    test_lost();
    test_requests();
    test_idle();
    test_script_end();
    test_keyscripts();
    test_script_limits();
    test_script_peeks();
    test_clicks();
    key_set_source(NULL, NULL);
    vid_close();
    msg_free();
    if (tf_fails == 0)
        cleanup();
    return tf_done();
}

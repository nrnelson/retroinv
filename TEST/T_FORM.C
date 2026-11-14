/*
 * T_FORM.C - typed forms (SRC/CLIENT/FORM.C) and the memo editor
 * (SRC/CLIENT/MEMO.C) over the engine, on the shadow screen.
 *
 * usage: T_FORM dir [msgdir]
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/FMDATA/", "C:\")
 * holding nothing but this test's files; its engine files are removed
 * first and last.  msgdir is the directory prefix of the built INV.MSG
 * (default: the program's own directory).  The test links the recording
 * link of TRLINK.H instead of LINK.C, so the requests of a save are
 * checked exactly and a chosen request can be made to fail.  Keys come
 * from a key source; when a scenario's keys run out, tui_key ends the
 * program through the exit function, which leaves with longjmp, and the
 * source can copy the screen when a chosen key is asked for.  The memo
 * editor, form_get_text, the choice words, form_field_error and
 * form_store are tested in T_FORM2.C.
 *
 *
 *
 * Covered:
 * - the local checks of every field type (required, numbers with sign,
 *   decimals and width, real dates, choices);
 * - where the cursor starts (the first field that can be changed, the
 *   last of a form with none) and that it stays on a read-only field
 *   when form_run runs again;
 * - editing through keys (upper case, the date template, Space and F4 on
 *   choices, the pick function on codes, logical and password fields, a
 *   password typed when the keys run out held only in the form's
 *   storage, which form_wipe wipes, its saved copy too, and closes,
 *   once);
 * - form_open refusing a field wider than FORM_MAXVAL without keeping a
 *   block;
 * - form_save splitting a new record over an ADD carrying the required
 *   fields and SETs within LINK_MAXLINE, the form turning into an edit form
 *   after its ADD whatever follows and a repeated save sending only what is
 *   left, nothing sent and an empty response when nothing changed, a pair
 *   too long for any SET refused unsent, a server error naming a field
 *   getting the cursor and a 201 naming none the key field's, the
 *   partial-save box shown once;
 * - Ctrl-F, F3 and F10 kept while an edit form has changes and F5 to F8
 *   returned all the same, Ctrl-E returned by a view only, F4 opening the
 *   memo in either mode and Enter never, Esc asking, form_noask, form_stop
 *   from the memo and pick functions, the memo field of a new record, the
 *   title;
 * - form_loop (a save on Enter that stays or leaves with its command, Esc
 *   with form_stop's command and without, Ctrl-E turning a view to edit
 *   with the hint or refused by the level, the key hook seeing every key
 *   first, other keys going to tui_global, the Record menu its own while
 *   it runs and the caller's after);
 * - the memo editor saving as SET NOTES= and NOTES ADD= requests that each
 *   fit (a request of exactly LINK_MAXLINE bytes, and one escape more
 *   moving its last line on), read back with NOTES, long lines split when
 *   loaded and the memo still editable, a save failing part-way, read-only
 *   memos and the empty memo;
 * - the shared helpers: form_kline (K lines only), form_get_text,
 *   form_choice_num and form_choice_word (unknown words, cut words),
 *   form_field_error;
 * - form_store's three results (saved; reported with the screen staying,
 *   retitled after an ADD whose later SET failed; the screen leaving after
 *   a vanished record or an ended session) and memo_field keeping the old
 *   count when the memo cannot be loaded and stopping the form when the
 *   screen must leave;
 * - the fields the cursor stops at (Tab, Shift-Tab, PgDn and clicks pass
 *   a read-only field outside view mode, not the memo) and the value of a
 *   field entered shown selected, replaced by what is typed, cleared by
 *   Backspace, edited in place after an arrow or a click on the field.
 */
#define TUI_VERBS
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "TRLINK.H"
#include "TEWIPE.H"
/* Checks keep no expression text: Turbo C stores every string literal
   again, and the near data of this test would pass the limit. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_KEYSRC
#define TF_MALLOC
#define TK_MAXKEYS      500
#define TK_EXIT0
#define TF_LINK
#include "TESTFW.H"
#include "T_FORM.H"

#define PATHMAX         96

int all_fit(void)
{
    const char *p;
    const char *e;

    p = tl_log();
    while ((e = strchr(p, '\n')) != NULL) {
        if (e - p > LINK_MAXLINE)
            return 0;
        p = e + 1;
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * The screen copied at a key.
 * --------------------------------------------------------------------- */
static u8 g_snap_row;
char g_snap[VID_COLS + 1];
#define SNAPSZ          (VID_ROWS * (VID_COLS + 1) + 1)
char *g_snapall;
u16 *g_typed;
static char g_ddir[PATHMAX];

/* tk_copy: row g_snap_row into g_snap, and the whole screen into
   g_snapall, rows ended by '\n'. */
static void snap_screen(void)
{
    u8 r;

    strcpy(g_snap, tk_row(g_snap_row));
    g_snapall[0] = '\0';
    for (r = 0; r < VID_ROWS; r++) {
        strcat(g_snapall, tk_row(r));
        strcat(g_snapall, "\n");
    }
}

void feed(const u16 *k, u16 at, u8 r)
{
    g_snap_row = r;
    g_snap[0] = '\0';
    tk_copy = snap_screen;
    tk_feed(k, at);
}

/* ---------------------------------------------------------------------
 * The forms.
 * --------------------------------------------------------------------- */
const form_field item_fields[F_N] = {
    { "ITEM_ID", MT_NOTAVAIL, 0, 0, VCOL, FT_NUM, 8, 0, FF_RO, 0, NULL },
    { "CATEGORY", MT_NOTFOUND, 1, 0, VCOL, FT_CODE, 4, 0, FF_REQ | FF_UPPER,
      0, NULL },
    { "ITEMTYPE", MT_DUP, 2, 0, VCOL, FT_CHAR, 20, 0, 0, 0, NULL },
    { "MFR", MT_BUSY, 3, 0, VCOL, FT_CHAR, 30, 0, 0, 0, NULL },
    { "MODEL", MT_EXPIRED, 4, 0, VCOL, FT_CHAR, 30, 0, 0, 0, NULL },
    { "SERIALNO", MT_ENDED, 5, 0, VCOL, FT_CHAR, 30, 0, 0, 0, NULL },
    { "DESCR", MT_NOMEM, 6, 0, VCOL, FT_CHAR, 60, 0, 0, 0, NULL },
    { "COND", MT_QUIT, 7, 0, VCOL, FT_CHOICE, 3, 0, 0, 0,
      "NEW EXC GD FR PR NW UT PT" },
    { "QTY", MT_DISCARD, 8, 0, VCOL, FT_NUM, 5, 0, FF_NONNEG, 0, NULL },
    { "LOC_ID", MT_NOCHOICE, 9, 0, VCOL, FT_CODE, LINK_LOCLEN, 0, FF_UPPER,
      0, NULL },
    { "ACQ_DATE", MT_NOHELP, 10, 0, VCOL, FT_DATE, 8, 0, 0, 0, NULL },
    { "ACQ_SRC", MT_ERRTITLE, 11, 0, VCOL, FT_CHAR, 30, 0, 0, 0, NULL },
    { "ACQ_PRICE", MT_HINTOK, 12, 0, VCOL, FT_NUM, 9, 2, FF_NONNEG, 0,
      NULL },
    { "TAGS", MT_YESNO, 13, 0, VCOL, FT_CHAR, 60, 0, 0, 0, NULL },
    { "STATUS", MT_BADPW, 14, 0, VCOL, FT_CHOICE, 1, 0, 0, 0, "A D" },
    { "NOTES", MT_DISABLED, 15, 0, VCOL, FT_MEMO, 5, 0, 0, 30, NULL }
};

static const form_def item_def = {
    MT_INTERNAL, "ITEM", BOXTOP, BOXLEFT, 18, 80, F_N, item_fields
};

static const form_field misc_fields[2] = {
    { "PW", MT_NOTAVAIL, 0, 0, VCOL, FT_SECRET, 16, 0, FF_LOCAL, 0, NULL },
    { "FLAG", MT_NOTFOUND, 1, 0, VCOL, FT_LOGIC, 1, 0, FF_LOCAL, 0, NULL }
};

static const form_def misc_def = {
    MT_INTERNAL, "ITEM", BOXTOP, BOXLEFT, 4, 40, 2, misc_fields
};

/* A password field, then one wider than FORM_MAXVAL: form_open refuses
   it. */
static const form_field wide_fields[2] = {
    { "PW", MT_NOTAVAIL, 0, 0, VCOL, FT_SECRET, 16, 0, FF_LOCAL, 0, NULL },
    { "WIDE", MT_NOTFOUND, 1, 0, VCOL, FT_CHAR, FORM_MAXVAL + 1, 0, 0, 0,
      NULL }
};

static const form_def wide_def = {
    MT_INTERNAL, "ITEM", BOXTOP, BOXLEFT, 4, 40, 2, wide_fields
};

/* Two fields that cannot be changed, on the rows of F_ID and F_CAT. */
static const form_field ro_fields[2] = {
    { "A", MT_NOTAVAIL, 0, 0, VCOL, FT_CHAR, 4, 0, FF_RO, 0, NULL },
    { "B", MT_NOTFOUND, 1, 0, VCOL, FT_CHAR, 4, 0, FF_RO, 0, NULL }
};

static const form_def ro_def = {
    MT_INTERNAL, "ITEM", BOXTOP, BOXLEFT, 4, 40, 2, ro_fields
};

/* A signed N(6,2) field, on the row of F_ID so that f2 finds its
   cursor there. */
static const form_field amt_field = {
    "AMT", MT_NOTAVAIL, 0, 0, VCOL, FT_NUM, 6, 2, FF_LOCAL, 0, NULL
};

static const form_def amt_def = {
    MT_INTERNAL, "ITEM", BOXTOP, BOXLEFT, 3, 40, 1, &amt_field
};

u8 vrow(u16 i)
{
    return (u8)(BOXTOP + 1 + item_fields[i].row);
}

u8 vcol(void)
{
    return (u8)(BOXLEFT + 1 + VCOL);
}

static u16 g_pick_calls;
static u16 g_memo_calls;
static u8 g_stop_in_cb;         /* the callbacks call form_stop         */
static u8 g_stop_cmd;           /* ... with this command                */

static u16 g_typed_calls;       /* pick calls for a value typed         */
static u16 g_typed_field;       /* ... and the field of the last        */

/* F4 picks A1 in the location field; a value typed in a code field is
   counted. */
static int pick(void *arg, form *f, u16 field, char *val)
{
    TF_CHECK(arg == (void *)&g_pick_calls);
    TF_CHECK(f != NULL);
    if (val == NULL) {
        g_typed_calls++;
        g_typed_field = field;
        if (g_stop_in_cb)
            form_stop(f, g_stop_cmd);
        return 0;
    }
    TF_EQ(field, F_LOC);
    g_pick_calls++;
    if (g_stop_in_cb)
        form_stop(f, g_stop_cmd);
    strcpy(val, "A1");
    return 1;
}

static u16 memo(void *arg, form *f, u16 field)
{
    TF_CHECK(arg == (void *)&g_pick_calls);
    TF_CHECK(f != NULL);
    TF_EQ(field, F_NOTES);
    g_memo_calls++;
    if (g_stop_in_cb)
        form_stop(f, g_stop_cmd);
    return 7;
}

form *open_item(u8 mode)
{
    return form_open(&item_def, mode, pick, memo, &g_pick_calls);
}

static u16 g_run_key;
form *g_run_form;

int run(const u16 *k, u16 at, u8 r)
{
    feed(k, at, r);
    g_run_key = 0;
    if (setjmp(tk_jb) != 0)
        return 0;
    g_run_key = form_run(g_run_form);
    return 1;
}

/* Enter on f: 1 when every field passed, else 0 with the message
   expected on the hint row and the cursor on field want: drawn as the
   field being edited, its value selected or not. */
static int save_run(form *f, u16 want_msg, u16 want_field)
{
    static const u16 k[] = { KEY_ENTER, KEY_NONE };
    char m[MSG_MAXLEN + 2];

    g_run_form = f;
    if (run(k, 0, 0))
        return TF_EQ(g_run_key, KEY_ENTER);
    m[0] = ' ';
    strcpy(m + 1, msg(want_msg));
    TF_STREQ(tk_row(TUI_HINTROW), m);
    TF_CHECK(tk_attr(vrow(want_field), vcol()) != tui_attr(TA_INPUT));
    return 0;
}

static void test_checks(void)
{
    form *f;

    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    TF_EQ(save_run(f, MF_REQUIRED, F_CAT), 0);
    TF_EQ(form_put(f, "CATEGORY", "SYS"), 0);
    TF_EQ(form_put(f, "NOSUCH", "x"), -1);
    TF_CHECK(form_get(f, "NOSUCH") == NULL);
    TF_EQ(form_load(f, "NOSUCH", "x"), -1);
    TF_EQ(save_run(f, 0, 0), 1);
    TF_EQ(form_put(f, "QTY", "-1"), 0);
    TF_EQ(save_run(f, MF_NEGNUM, F_QTY), 0);
    form_put(f, "QTY", "-0");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "QTY", "1x");
    TF_EQ(save_run(f, MF_BADNUM, F_QTY), 0);
    form_put(f, "QTY", "1.5");
    TF_EQ(save_run(f, MF_BADNUM, F_QTY), 0);
    form_put(f, "QTY", "12345");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "ACQ_PRICE", "1.234");
    TF_EQ(save_run(f, MF_BADNUM, F_PRICE), 0);
    form_put(f, "ACQ_PRICE", "-");
    TF_EQ(save_run(f, MF_BADNUM, F_PRICE), 0);
    form_put(f, "ACQ_PRICE", "-0.00");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "ACQ_PRICE", "-0.05");
    TF_EQ(save_run(f, MF_NEGNUM, F_PRICE), 0);
    form_put(f, "ACQ_PRICE", "1234567.5");
    TF_EQ(save_run(f, MF_BADNUM, F_PRICE), 0);
    form_put(f, "ACQ_PRICE", "123456.50");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "ACQ_PRICE", "12.50");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "ACQ_DATE", "20260230");
    TF_EQ(save_run(f, MF_BADDATE, F_DATE), 0);
    form_put(f, "ACQ_DATE", "19000229");
    TF_EQ(save_run(f, MF_BADDATE, F_DATE), 0);
    form_put(f, "ACQ_DATE", "2026013");
    TF_EQ(save_run(f, MF_BADDATE, F_DATE), 0);
    form_put(f, "ACQ_DATE", "20000229");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "COND", "XX");
    TF_EQ(save_run(f, MF_BADCHOICE, F_COND), 0);
    form_put(f, "COND", "PT");
    TF_EQ(save_run(f, 0, 0), 1);
    /* Shown: the date with its hyphens, the memo field of a new record. */
    form_paint(f);
    TF_CHECK(tf_starts(tk_row(vrow(F_DATE)) + vcol(), "2000-02-29"));
    TF_CHECK(tf_starts(tk_row(vrow(F_NOTES)) + vcol(), msg(MF_MEMONEW)));
    TF_CHECK(tf_starts(tk_row(vrow(F_PRICE)) + vcol(), "12.50"));
    form_close(f);

    /* The sign takes a cell of a decimal field's width, as the point
       and the dec digits do: -999 would be stored as -999.00. */
    f = form_open(&amt_def, FORM_NEW, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "AMT", "-999");
    TF_EQ(save_run(f, MF_BADNUM, F_ID), 0);
    form_put(f, "AMT", "-99.5");
    TF_EQ(save_run(f, 0, 0), 1);
    form_put(f, "AMT", "999");
    TF_EQ(save_run(f, 0, 0), 1);
    form_close(f);
}

/* A value typed into a code field goes to the pick function, val NULL,
   once the cursor leaves the field with any key, Ctrl-S included, and only
   when the value changed; a form_stop there ends form_run with Esc. */
static void test_typed(void)
{
    static const u16 k1[] = { KEY_PGDN, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
                              KEY_UP, 'b', '2', KEY_CTRL('S'), KEY_NONE };
    static const u16 k2[] = { KEY_TAB, KEY_NONE };
    static const u16 k3[] = { KEY_UP, KEY_END, KEY_BS, KEY_TAB,
                              KEY_NONE };
    form *f;

    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "CATEGORY", "SYS");
    g_run_form = f;
    g_typed_calls = 0;
    TF_EQ(run(k1, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ENTER);
    TF_STREQ(form_get(f, "LOC_ID"), "B2");
    TF_EQ(g_typed_calls, 1);
    TF_EQ(g_typed_field, F_LOC);
    /* Left unchanged: no call (the keys run out in the next field). */
    TF_EQ(run(k2, 0, 0), 0);
    TF_EQ(g_typed_calls, 1);
    /* A call that stops the form. */
    g_stop_in_cb = 1;
    g_stop_cmd = TUI_CMD_QUIT;
    TF_EQ(run(k3, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ESC);
    TF_EQ(g_typed_calls, 2);
    TF_STREQ(form_get(f, "LOC_ID"), "B");
    g_stop_in_cb = 0;
    form_close(f);

    /* In view mode nothing can be typed, so nothing is called; Ctrl-S
       does nothing, and the keys run out. */
    f = open_item(FORM_VIEW);
    if (!TF_CHECK(f != NULL))
        return;
    g_run_form = f;
    TF_EQ(run(k1, 0, 0), 0);
    TF_EQ(g_typed_calls, 2);
    form_close(f);
}

static void test_editing(void)
{
    static const u16 k[] = {
        's', 'y', 's', KEY_TAB, KEY_TAB, 'I', 'B', 'M', ' ', ' ',
        KEY_BS, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_DOWN, ' ', ' ', KEY_F4,
        KEY_DOWN, KEY_ENTER, KEY_DOWN, '5', KEY_DOWN, KEY_F4, KEY_DOWN,
        '2', '0', '2', '6', '0', '1', '3', '1', KEY_LEFT, '2', KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_UP, KEY_UP, KEY_UP, KEY_STAB, KEY_ENTER, KEY_NONE
    };
    static const u16 k2[] = { ' ', 'a', 'b', KEY_TAB, ' ', ' ', 'n',
                              KEY_CTRL('S'), KEY_NONE };
    static const u16 k3[] = { 'p', 'w', KEY_NONE };
    form *f;
    u16 held;
#ifdef COMPAT_HOST
    char *b[2];
    int w;
#endif

    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    g_pick_calls = 0;
    g_typed_calls = 0;
    g_run_form = f;
    TF_EQ(run(k, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ENTER);
    /* The category typed went to the pick function, with val NULL, as
       the cursor left it; the location picked with F4 did not. */
    TF_EQ(g_typed_calls, 1);
    TF_EQ(g_typed_field, F_CAT);
    TF_STREQ(form_get(f, "CATEGORY"), "SYS");
    TF_STREQ(form_get(f, "MFR"), "IBM");
    TF_STREQ(form_get(f, "COND"), "GD");
    TF_STREQ(form_get(f, "QTY"), "5");
    TF_STREQ(form_get(f, "LOC_ID"), "A1");
    TF_EQ(g_pick_calls, 1);
    TF_STREQ(form_get(f, "ACQ_DATE"), "20260121");
    TF_CHECK(tf_starts(tk_row(vrow(F_DATE)) + vcol(), "2026-01-21"));
    TF_CHECK(form_dirty(f));
    form_saved(f);
    TF_CHECK(!form_dirty(f));
    form_close(f);

    /* A password takes 21h..7Eh and is never shown; Space and the
       letters set a logical field. */
    g_run_form = form_open(&misc_def, FORM_NEW, NULL, NULL, NULL);
    if (!TF_CHECK(g_run_form != NULL))
        return;
    TF_EQ(run(k2, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ENTER);
    TF_STREQ(form_get(g_run_form, "PW"), "ab");
    TF_STREQ(form_get(g_run_form, "FLAG"), "F");
    TF_EQ(tk_row(BOXTOP + 1)[vcol()], ' ');
    TF_EQ(tk_row(BOXTOP + 1)[vcol() + 1], ' ');
    TF_EQ(tk_row(BOXTOP + 2)[vcol()], msg(MF_YN)[1]);
    form_close(g_run_form);
    form_wipe();                /* closed: not freed a second time      */

    /* A key script ending in the middle of a password leaves through
       the exit function from inside the edit.  The bytes typed must be
       in the form's own storage, which form_close wipes (INV's exit
       path closes the open password form with form_wipe), not in a
       copy of the edit's that nothing could wipe; form_wipe zeroes
       the password before it frees the form's block, the form's only
       one, whose value sets (the current values, then the loaded ones,
       PW's slot first in each) follow the struct; form_saved copies the
       password into the loaded ones too, as a save does; FLAG was never
       set, so both sets are zero once both of PW's are wiped, and the
       watch ends on the block's last byte. */
    held = tm_held();
    g_run_form = form_open(&misc_def, FORM_NEW, NULL, NULL, NULL);
    if (!TF_CHECK(g_run_form != NULL))
        return;
    TF_EQ(run(k3, 0, 0), 0);
    TF_STREQ(form_get(g_run_form, "PW"), "pw");
    form_saved(g_run_form);
#ifdef COMPAT_HOST
    tm_expect_wiped(form_get(g_run_form, "PW"), (u16)(2 *
        (misc_fields[0].width + 1 + misc_fields[1].width + 1)));
#endif
    form_wipe();
#ifdef COMPAT_HOST
    TF_CHECK(tm_wiped());

    /* Only the free of the block holding the watched bytes decides the
       watch: another block freed first, while they are not yet zero,
       leaves it on.  Each of two blocks is watched in turn, so the
       other one lies below it once. */
    for (w = 0; w < 2; w++) {
        b[0] = (char *)malloc(sizeof "pw");
        b[1] = (char *)malloc(sizeof "pw");
        if (!TF_CHECK(b[0] != NULL && b[1] != NULL))
            break;
        memset(b[w], 'x', sizeof "pw");
        tm_expect_wiped(b[w], (u16)sizeof "pw");
        free(b[!w]);
        memset(b[w], 0, sizeof "pw");
        free(b[w]);
        TF_CHECK(tm_wiped());
    }
#endif
    form_wipe();
    TM_HELD(held);

    /* A field wider than FORM_MAXVAL: no form, no block kept, and none
       left for form_wipe, although a password field came first. */
    TF_CHECK(form_open(&wide_def, FORM_NEW, NULL, NULL, NULL) == NULL);
    form_wipe();
    TM_HELD(held);
}

void fill_long(form *f)
{
    form_put(f, "CATEGORY", "SYS");
    form_put(f, "ITEMTYPE", "ISA VGA card type tt");
    form_put(f, "MFR", "International Business Machine");
    form_put(f, "MODEL", "Colour Graphics \"Adapter\" 512");
    form_put(f, "SERIALNO", "SN-0001-0002-0003-0004-0005-00");
    form_put(f, "DESCR", "Colour graphics adapter with a composite output "
             "and lightpen");
    form_put(f, "COND", "GD");
    form_put(f, "QTY", "2");
    form_put(f, "LOC_ID", "1");
    form_put(f, "ACQ_SRC", "Swap meet in the parking lot z");
    form_put(f, "ACQ_PRICE", "12.50");
    form_put(f, "TAGS", "video,isa,cga,ibm,composite,lightpen,rgbi,8bit,"
             "pc,xt,at,yes");
    form_put(f, "STATUS", "A");
    form_put(f, "ACQ_DATE", "19860214");
}

static form *g_got_form;

form *get_item(const char *id)
{
    rq q;
    rq_res res;
    form *g;

    g = open_item(FORM_VIEW);
    if (g == NULL)
        return NULL;
    rq_begin(&q, "ITEM GET");
    rq_word(&q, id);
    TF_EQ(rq_send(tc_lk, &q, form_kline, g, &res), RQ_OK);
    return g;
}

/* A table name that, with a DESCR of LINK_DESCRLEN escaped bytes (four
   bytes each, \xHH), passes LINK_MAXLINE in a SET of item 1. */
static const char k_longtab[] = "ITEM_TABLE_OF_A_NAME_LONG_ENOUGH_FOR_IT";
extern char longtab_fits[1];
extern char longtab_fits[(sizeof k_longtab - 1 + sizeof " SET 1 DESCR=\"\"" -
                          1 + 4 * LINK_DESCRLEN > LINK_MAXLINE) ? 1 : 2];

static void same_values(form *f, const char *id)
{
    u16 i;

    g_got_form = get_item(id);
    if (!TF_CHECK(g_got_form != NULL))
        return;
    for (i = 1; i < F_N - 1; i++)
        TF_STREQ(form_get(g_got_form, item_fields[i].name),
                 form_get(f, item_fields[i].name));
    TF_STREQ(form_get(g_got_form, "ITEM_ID"), id);
    form_close(g_got_form);
}

/* Where the cursor starts: an edit form opened on a read-only first
   field starts on the first field that can be changed, and stays where
   it was put when form_run runs again; a form with no such field starts
   on its last. */
static void test_start(void)
{
    static const u16 k[] = { KEY_PGDN, TUI_VERBKEY, KEY_NONE };
    form *f;

    f = open_item(FORM_EDIT);
    if (!TF_CHECK(f != NULL))
        return;
    g_run_form = f;
    TF_EQ(run(k + 2, 0, 0), 0);
    TF_EQ(tk_attr(vrow(F_CAT), vcol()), tui_attr(TA_INPUTON));
    TF_EQ(run(k, 0, 0), 1);
    TF_EQ(g_run_key, TUI_VERBKEY);
    TF_EQ(run(k + 2, 0, 0), 0);
    TF_EQ(tk_attr(vrow(F_NOTES), vcol()), tui_attr(TA_INPUTON));
    form_close(f);

    f = form_open(&ro_def, FORM_EDIT, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    g_run_form = f;
    TF_EQ(run(k + 2, 0, 0), 0);
    TF_EQ(tk_attr(vrow(1), vcol()), tui_attr(TA_INPUTON));
    form_close(f);
}

static void test_saving(void)
{
    form *f;
    rq_res res;
    char id[LINK_IDLEN + 1];
    char want[LINK_MAXLINE + 1];
    u16 i;

    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    fill_long(f);
    id[0] = '\0';
    tl_clear();
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_STREQ(id, "1");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_CHECK(!form_dirty(f));
    TF_CHECK(tl_count() >= 2);
    TF_CHECK(all_fit());
    TF_CHECK(tf_starts(tl_req(0), "ITEM ADD CATEGORY=SYS "));
    TF_CHECK(strstr(tl_log(), "ITEM SET 1 ") != NULL);
    for (i = 1; i < tl_count(); i++)
        TF_CHECK(tf_starts(tl_req(i), "ITEM SET 1 "));
    TF_CHECK(strstr(tl_log(), "NOTES") == NULL);
    TF_CHECK(strstr(tl_log(), "ITEM_ID") == NULL);
    same_values(f, "1");

    /* Nothing changed: no request, and a response with nothing in it. */
    tl_clear();
    res.data = 1;
    res.closed = 1;
    strcpy(res.text, "X");
    strcpy(res.field, "X");
    strcpy(res.more, "X");
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 0);
    TF_CHECK(res.data == 0 && res.closed == 0 && res.text[0] == '\0' &&
             res.field[0] == '\0' && res.more[0] == '\0');
    /* One change: one SET with that pair alone. */
    form_put(f, "QTY", "3");
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM SET 1 QTY=3");
    /* A pair too long for a SET even alone: refused, nothing sent, the
       change kept.  No item field's value fills a request at
       LINK_MAXLINE, so the table's name is long (LONGTAB): the request
       never reaches the engine. */
    memset(want, '\177', FORM_MAXVAL);
    want[FORM_MAXVAL] = '\0';
    form_put(f, "DESCR", want);
    tl_clear();
    TF_EQ(form_save(f, tc_lk, k_longtab, id, sizeof id, &res), RQ_ETOOLONG);
    TF_EQ(tl_count(), 0);
    TF_CHECK(form_dirty(f));
    form_close(f);

    /* The ADD succeeds, the SET after it fails: the form is an edit
       form for the new record with the ADD's fields saved, and saving
       again sends only the rest, as a SET. */
    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    fill_long(f);
    id[0] = '\0';
    tl_clear();
    tl_fail(2, "-ERR 302 disk full");
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_EFULL);
    TF_STREQ(id, "2");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_CHECK(form_dirty(f));
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(0), "ITEM ADD CATEGORY=SYS "));
    strcpy(want, tl_req(1));
    tl_clear();
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), want);
    for (i = 0; i < tl_count(); i++)
        TF_CHECK(tf_starts(tl_req(i), "ITEM SET 2 "));
    TF_CHECK(strstr(tl_log(), "ITEM ADD") == NULL);
    TF_CHECK(!form_dirty(f));
    same_values(f, "2");
    form_close(f);
}

static const form_field cat_fields[3] = {
    { "CAT_CODE", MT_NOTAVAIL, 0, 0, VCOL, FT_CHAR, LINK_CATLEN, 0,
      FF_KEY | FF_REQ | FF_NEWONLY | FF_UPPER, 0, NULL },
    { "CAT_NAME", MT_NOTFOUND, 1, 0, VCOL, FT_CHAR, 20, 0, 0, 0, NULL },
    { "SORTORD", MT_DUP, 2, 0, VCOL, FT_NUM, 2, 0, FF_NONNEG, 0, NULL }
};

const form_def cat_def = {
    MT_INTERNAL, "CATS", BOXTOP, BOXLEFT, 5, 60, 3, cat_fields
};

/* The screen row of cat_fields[i]'s value. */
#define CAT_ROW(i)      ((u8)(BOXTOP + 1 + cat_fields[i].row))

/* The key goes as the positional word of the ADD, and names the record
   when the +OK has no ID=; an ADD without a key needs the ID=. */
static void test_keyed(void)
{
    form *f;
    rq_res res;
    char id[LINK_IDLEN + 1];

    f = form_open(&cat_def, FORM_NEW, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "CAT_CODE", "B9");
    form_put(f, "CAT_NAME", "Shelf B, bottom");
    id[0] = '\0';
    tl_clear();
    TF_EQ(form_save(f, tc_lk, "CAT", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "CAT ADD B9 CAT_NAME=\"Shelf B, bottom\"");
    TF_STREQ(id, "B9");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    form_put(f, "SORTORD", "4");
    TF_EQ(form_save(f, tc_lk, "CAT", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(1), "CAT SET B9 SORTORD=4");
    form_close(f);

    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "CATEGORY", "SYS");
    tl_clear();
    tl_fail(1, "+OK");
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_EINTERNAL);
    TF_EQ(form_mode_get(f), FORM_NEW);
    form_close(f);
}

static void test_errors(void)
{
    static const u16 k1[] = { KEY_ENTER, KEY_ENTER, KEY_NONE };
    static const u16 none[] = { KEY_NONE };
    form *f;
    rq_res res;
    char id[LINK_IDLEN + 1];
    char m[RQ_TEXTLEN + 2];
    u8 cmd;

    f = open_item(FORM_NEW);
    cmd = 99;
    if (!TF_CHECK(f != NULL))
        return;
    /* After a partial ADD the error reports first that fields may not
       have been saved, then the reason (two message boxes). */
    fill_long(f);
    tl_clear();
    tl_fail(2, "-ERR 302 disk full");
    TF_EQ(form_save(f, tc_lk, "ITEM", id, sizeof id, &res), RQ_EFULL);
    feed(k1, 1, (u8)(TUI_TOP + TUI_WORKROWS / 2));
    if (setjmp(tk_jb) == 0)
        cmd = form_error(f, &res);
    TF_EQ(cmd, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 2);
    /* That error cleared partial: the next gives the reason alone. */
    feed(k1, 1, (u8)(TUI_TOP + TUI_WORKROWS / 2));
    cmd = 99;
    if (setjmp(tk_jb) == 0)
        cmd = form_error(f, &res);
    TF_EQ(cmd, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 1);
    form_close(f);

    /* A 201 naming none of the form's fields puts the cursor on its
       key field, away from the field a 102 put it on. */
    f = form_open(&cat_def, FORM_NEW, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    memset(&res, 0, sizeof res);
    res.code = RQ_EARG;
    strcpy(res.field, "SORTORD");
    strcpy(res.text, "bad number");
    TF_EQ(form_error(f, &res), TUI_CMD_NONE);
    g_run_form = f;
    TF_EQ(run(none, 0, 0), 0);
    TF_EQ(tk_attr(CAT_ROW(2), vcol()), tui_attr(TA_INPUTON));
    res.code = RQ_EDUP;
    res.field[0] = '\0';
    strcpy(res.text, "already exists");
    TF_EQ(form_error(f, &res), TUI_CMD_NONE);
    TF_EQ(run(none, 0, 0), 0);
    TF_EQ(tk_attr(CAT_ROW(0), vcol()), tui_attr(TA_INPUTON));
    TF_EQ(tk_attr(CAT_ROW(2), vcol()), tui_attr(TA_INPUT));
    form_close(f);

    /* A field named by the server gets the cursor and the text. */
    f = get_item("1");
    if (!TF_CHECK(f != NULL))
        return;
    form_mode_set(f, FORM_EDIT);
    strcpy(id, "1");
    form_put(f, "LOC_ID", "ZZ");
    tl_clear();
    TF_CHECK(form_save(f, tc_lk, "ITEM", id, sizeof id, &res) != RQ_OK);
    TF_STREQ(res.field, "LOC_ID");
    TF_EQ(form_error(f, &res), TUI_CMD_NONE);
    g_run_form = f;
    TF_EQ(run(none, 0, 0), 0);
    /* The server's text, its first letter in upper case. */
    m[0] = ' ';
    strcpy(m + 1, res.text);
    m[1] = (char)TXT_UPPER(m[1]);
    TF_STREQ(tk_row(TUI_HINTROW), m);
    TF_EQ(tk_attr(vrow(F_LOC), vcol()), tui_attr(TA_BOLD));
    form_close(f);
}

static void test_keys_kept(void)
{
    static const u16 k1[] = { KEY_CTRL('F'), KEY_F3, KEY_F10, KEY_ESC, 'N',
                              KEY_ESC, 'Y', KEY_NONE };
    static const u16 k2[] = { KEY_CTRL('F'), KEY_NONE };
    static const u16 k3[] = { KEY_F10, KEY_NONE };
    static const u16 k4[] = { KEY_CTRL('E'), KEY_NONE };
    static const u16 k5[] = { KEY_F4, KEY_NONE };
    static const u16 k6[] = { KEY_PGDN, KEY_F4, KEY_ESC, KEY_NONE };
    static const u16 k9[] = { KEY_F4, KEY_ENTER, KEY_PGDN, KEY_ENTER,
                              KEY_NONE };
    static const u16 k7[] = { KEY_PGDN, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
                              KEY_UP, KEY_UP, KEY_F4, KEY_ESC, KEY_NONE };
    static const u16 k8[] = { KEY_ESC, KEY_NONE };
    u16 kf[2];
    u16 i;
    char m[MSG_MAXLEN + 2];

    kf[1] = KEY_NONE;
    g_run_form = get_item("1");
    if (!TF_CHECK(g_run_form != NULL))
        return;
    form_mode_set(g_run_form, FORM_EDIT);
    form_put(g_run_form, "MFR", "Changed");
    TF_EQ(run(k1, 2, TUI_HINTROW), 1);
    TF_EQ(g_run_key, KEY_ESC);
    m[0] = ' ';
    strcpy(m + 1, msg(MT_SAVEFIRST));
    TF_STREQ(g_snap, m);
    TF_EQ(tk_pos(), 7);
    /* The keys of the Record menu's verbs go to the caller in edit mode
       even with changes, which stay; a function key the form does not
       use, F5, stays in the form. */
    for (i = 0; i < 2; i++) {
        kf[0] = (u16)(i == 0 ? TUI_VERBKEY : TUI_VERBLAST);
        TF_EQ(run(kf, 0, 0), 1);
        TF_EQ(g_run_key, kf[0]);
        TF_EQ(tk_pos(), 1);
        TF_CHECK(form_dirty(g_run_form));
    }
    kf[0] = KEY_F5;
    TF_EQ(run(kf, 0, 0), 0);
    TF_EQ(form_mode_get(g_run_form), FORM_EDIT);
    /* Without changes the keys go to the caller. */
    form_put(g_run_form, "MFR", form_get(g_run_form, "MODEL"));
    form_saved(g_run_form);
    TF_EQ(run(k2, 0, 0), 1);
    TF_EQ(g_run_key, KEY_CTRL('F'));
    /* Ctrl-E is a view's key: an edit form keeps it. */
    TF_EQ(run(k4, 0, 0), 0);
    /* In view mode too, even with changes; Ctrl-E there goes back, while
       F4 off the memo field and Enter do nothing, nor does Enter on the
       memo field. */
    form_mode_set(g_run_form, FORM_VIEW);
    form_put(g_run_form, "MFR", "Changed");
    TF_EQ(run(k3, 0, 0), 1);
    TF_EQ(g_run_key, KEY_F10);
    TF_EQ(run(k4, 0, 0), 1);
    TF_EQ(g_run_key, KEY_CTRL('E'));
    g_memo_calls = 0;
    TF_EQ(run(k9, 0, 0), 0);
    TF_EQ(tk_pos(), 4);
    TF_EQ(g_memo_calls, 0);
    /* F4 on the memo field of a saved record calls the memo function, in
       view mode too. */
    g_memo_calls = 0;
    TF_EQ(run(k6, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ESC);
    TF_EQ(g_memo_calls, 1);
    TF_STREQ(form_get(g_run_form, "NOTES"), "7");
    TF_CHECK(tf_starts(tk_row(vrow(F_NOTES)) + vcol(),
                       msg_sub(MF_MEMOLINES, "7", NULL)));
    /* A form without a record: changes hold no key and Esc asks
       nothing. */
    form_close(g_run_form);
    g_run_form = open_item(FORM_NEW);
    if (!TF_CHECK(g_run_form != NULL))
        return;
    form_noask(g_run_form);
    form_put(g_run_form, "MFR", "Changed");
    TF_EQ(run(k2, 0, 0), 1);
    TF_EQ(g_run_key, KEY_CTRL('F'));
    TF_EQ(run(k8, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ESC);
    TF_EQ(tk_pos(), 1);
    TF_EQ(run(k3, 0, 0), 1);
    TF_EQ(g_run_key, KEY_F10);
    TF_CHECK(form_dirty(g_run_form));
    form_close(g_run_form);
    g_run_form = get_item("1");
    if (!TF_CHECK(g_run_form != NULL))
        return;

    /* form_stop in a callback ends form_run with Esc, unasked. */
    form_mode_set(g_run_form, FORM_EDIT);
    form_put(g_run_form, "MFR", "Changed again");
    g_stop_in_cb = 1;
    g_memo_calls = 0;
    TF_EQ(run(k6, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ESC);
    TF_EQ(g_memo_calls, 1);
    TF_EQ(tk_pos(), 2);
    g_pick_calls = 0;
    TF_EQ(run(k7, 0, 0), 1);
    TF_EQ(g_run_key, KEY_ESC);
    TF_EQ(g_pick_calls, 1);
    TF_STREQ(form_get(g_run_form, "LOC_ID"), "A1");
    g_stop_in_cb = 0;
    /* The title can be replaced. */
    form_set_title(g_run_form,
                   "Item 12 with a title much longer than allowed");
    form_paint(g_run_form);
    TF_CHECK(strstr(tk_row(BOXTOP),
                    "Item 12 with a title much longer than al") != NULL);
    TF_CHECK(strstr(tk_row(BOXTOP), "allowed") == NULL);
    form_close(g_run_form);

    /* The memo field of a new record, which the keys pass but a field
       error can name, says to save first. */
    g_run_form = open_item(FORM_NEW);
    if (!TF_CHECK(g_run_form != NULL))
        return;
    g_memo_calls = 0;
    form_field_error(g_run_form, "NOTES", "");
    TF_EQ(run(k5, 0, 0), 0);
    TF_EQ(g_memo_calls, 0);
    m[0] = ' ';
    strcpy(m + 1, msg(MF_SAVEMEMO));
    TF_STREQ(tk_row(TUI_HINTROW), m);
    form_close(g_run_form);
    g_run_form = form_open(&item_def, FORM_NEW, NULL, NULL, NULL);
    TF_CHECK(g_run_form != NULL);
    form_close(g_run_form);
}

/* ---------------------------------------------------------------------
 * form_loop.
 * --------------------------------------------------------------------- */
#define LP_HOOKCMD      42      /* the hook's command on F7             */
#define LP_SEARCH       77      /* the test application's F3 command    */
#define LP_VERBS        5       /* form_loop's Record menu              */
#define LP_OUTER        2       /* and the one around it                */

/* The state of the form_loop tests; &g_lp is form_loop's arg. */
static struct {
    u16  saves;                 /* lp_save calls                        */
    u16  stay;                  /* the first calls that return 0        */
    int  rc;                    /* then this, with *cmd set to cmd      */
    u8   cmd;
    u8   take;                  /* 1: the hook takes every key but F7   */
    u8   got;                   /* form_loop's command                  */
    u16  nkeys;                 /* keys the hook saw, the first ones
                                   in keys                              */
    u16  keys[4];
} g_lp;
static tui_app g_app;

static int lp_save(void *arg, form *f, u8 *cmd)
{
    TF_CHECK(arg == (void *)&g_lp && f == g_run_form);
    TF_CHECK(tui_record(LP_VERBS) == LP_VERBS);
    if (++g_lp.saves <= g_lp.stay)
        return 0;
    *cmd = g_lp.cmd;
    return g_lp.rc;
}

/* The keys of three verbs of a Record menu, which form_run returns: the
   hook takes the first, leaves on the second, and lets the third go on
   to tui_global. */
#define LP_TAKE         (TUI_VERBKEY + 1)
#define LP_LEAVE        (TUI_VERBKEY + 2)
#define LP_OTHER        (TUI_VERBKEY + 3)

/* Takes LP_TAKE (and every key with g_lp.take), leaves with LP_HOOKCMD
   on LP_LEAVE, leaves the rest to form_loop. */
static int lp_key(void *arg, form *f, u16 k, u8 *cmd)
{
    TF_CHECK(arg == (void *)&g_lp && f == g_run_form);
    if (g_lp.nkeys < sizeof g_lp.keys / sizeof g_lp.keys[0])
        g_lp.keys[g_lp.nkeys] = k;
    g_lp.nkeys++;
    if (k == LP_LEAVE) {
        *cmd = LP_HOOKCMD;
        return 0;
    }
    return k == LP_TAKE || g_lp.take ? 1 : -1;
}

static const form_ops lp_plain = { lp_save, NULL, MF_REQUIRED };
static const form_ops lp_hook = { lp_save, lp_key, MF_REQUIRED };

/* The login, home and command screen of the test application: a login
   at TUI_LVL_ADMIN, then form_loop on g_run_form. */
static u8 lp_screen(u8 cmd)
{
    if (cmd == TUI_CMD_LOGIN) {
        tui_s.level = TUI_LVL_ADMIN;
        return TUI_CMD_NONE;
    }
    g_lp.got = form_loop(g_run_form, &lp_plain, &g_lp, LP_VERBS);
    return TUI_CMD_QUIT;
}

/* form_loop on f with ops and the keys k, or, with ops NULL, the test
   application (tui_run); 1 when it returned, with its command in
   g_lp.got and the Record menu as it was before. */
static int loop(form *f, const form_ops *ops, const u16 *k)
{
    feed(k, 0, 0);
    g_run_form = f;
    g_lp.saves = 0;
    g_lp.nkeys = 0;
    g_lp.got = TUI_CMD_MAX;
    if (setjmp(tk_jb) != 0)
        return 0;
    tui_record(LP_OUTER);
    if (ops == NULL)
        tui_run(&g_app);
    else
        g_lp.got = form_loop(f, ops, &g_lp, LP_VERBS);
    TF_CHECK(tui_record(0) == LP_OUTER);
    return 1;
}

static void test_loop(void)
{
    /* Key scripts, each ended by KEY_NONE, used from the index noted. */
    static const u16 k[] = {
        KEY_ENTER, KEY_ENTER, KEY_NONE,                 /* 0            */
        KEY_CTRL('E'), KEY_ESC, KEY_NONE,               /* 3; Esc at 4  */
        LP_TAKE, KEY_ENTER, LP_OTHER, LP_LEAVE, KEY_NONE, /* 6          */
        KEY_ENTER, KEY_ESC, LP_LEAVE, KEY_NONE,         /* 11           */
        LP_OTHER, KEY_CTRL('F'), KEY_NONE,              /* 15           */
        KEY_PGDN, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP, KEY_UP,
        KEY_F4, KEY_NONE                                /* 18           */
    };
    tui_sess sess;
    form *f;

    f = form_open(&misc_def, FORM_EDIT, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    /* Enter: a save returning 0 stays, the next leaves with its *cmd.
       The Record menu is the form's until then (lp_save), and the
       caller's after every way out (loop). */
    g_lp.stay = 1;
    g_lp.rc = 1;
    g_lp.cmd = TUI_CMD_NONE;
    TF_EQ(loop(f, &lp_plain, k), 1);
    TF_EQ(g_lp.saves, 2);
    TF_EQ(g_lp.got, TUI_CMD_NONE);
    g_lp.stay = 0;
    g_lp.rc = -1;
    g_lp.cmd = TUI_CMD_LOGIN;
    TF_EQ(loop(f, &lp_plain, k), 1);
    TF_EQ(tk_pos(), 1);
    TF_EQ(g_lp.got, TUI_CMD_LOGIN);
    /* Esc without a stop leaves with TUI_CMD_NONE; Ctrl-E in edit mode
       is the form's own and sets no hint. */
    tui_hint("");
    TF_EQ(loop(f, &lp_plain, k + 3), 1);
    TF_EQ(g_lp.got, TUI_CMD_NONE);
    TF_EQ(g_lp.saves, 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MF_REQUIRED)) == NULL);
    /* The hook sees every key first: LP_TAKE taken, Enter and LP_OTHER not
       its own (Enter saves and stays), LP_LEAVE leaves with its command. */
    g_lp.stay = 1;
    TF_EQ(loop(f, &lp_hook, k + 6), 1);
    TF_EQ(g_lp.got, LP_HOOKCMD);
    TF_EQ(g_lp.nkeys, 4);
    TF_EQ(g_lp.keys[0], LP_TAKE);
    TF_EQ(g_lp.keys[1], KEY_ENTER);
    TF_EQ(g_lp.keys[3], LP_LEAVE);
    TF_EQ(g_lp.saves, 1);
    /* A key the hook takes goes no further: no save, no leaving on
       Esc. */
    g_lp.take = 1;
    TF_EQ(loop(f, &lp_hook, k + 11), 1);
    g_lp.take = 0;
    TF_EQ(tk_pos(), 3);
    TF_EQ(g_lp.saves, 0);
    TF_EQ(g_lp.got, LP_HOOKCMD);
    form_close(f);

    /* Ctrl-E in view mode turns the form to edit with the hint, once
       tui_allow grants the level; refused, the form stays a view. */
    f = form_open(&misc_def, FORM_VIEW, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    tui_s.level = TUI_LVL_READ;
    TF_EQ(loop(f, &lp_plain, k + 3), 1);
    TF_EQ(form_mode_get(f), FORM_VIEW);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MF_REQUIRED)) == NULL);
    tui_s.level = TUI_LVL_OPER;
    TF_EQ(loop(f, &lp_plain, k + 3), 1);
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MF_REQUIRED)) != NULL);
    tui_s.level = TUI_LVL_ADMIN;

    /* Any other key goes to tui_global: LP_OTHER is not global and the
       form goes on; Ctrl-F leaves with the application's search command. */
    form_mode_set(f, FORM_VIEW);
    g_app.run = lp_screen;
    g_app.home = lp_screen;
    g_app.login = lp_screen;
    g_app.search = LP_SEARCH;
    sess = tui_s;
    TF_EQ(loop(f, NULL, k + 15), 1);
    tui_s = sess;
    TF_EQ(tk_pos(), 2);
    TF_EQ(g_lp.got, LP_SEARCH);
    form_close(f);

    /* Esc after form_stop leaves with its command; the next run starts
       without it. */
    f = open_item(FORM_EDIT);
    if (!TF_CHECK(f != NULL))
        return;
    g_stop_in_cb = 1;
    g_stop_cmd = TUI_CMD_LOGIN;
    TF_EQ(loop(f, &lp_plain, k + 18), 1);
    g_stop_in_cb = 0;
    g_stop_cmd = TUI_CMD_NONE;
    TF_EQ(g_lp.got, TUI_CMD_LOGIN);
    form_saved(f);
    TF_EQ(loop(f, &lp_plain, k + 4), 1);
    TF_EQ(g_lp.got, TUI_CMD_NONE);
    form_close(f);
}

/* ---------------------------------------------------------------------
 * memo_field, the memo field helper the screens share.
 * --------------------------------------------------------------------- */
static u16 g_mf_lines;

/* memo_field on field F_NOTES of f for item 1, with the keys k; 1 when
   it returned. */
static int memo_fld(form *f, int ro, const u16 *k)
{
    feed(k, 0, 0);
    g_mf_lines = 999;
    if (setjmp(tk_jb) != 0)
        return 0;
    g_mf_lines = memo_field(f, F_NOTES, tc_lk, "ITEM", "1", ro, "Notes");
    return 1;
}

/* memo_load_count into NOTES of f for item 1, with the keys k (the
   screen copied at the first); its result, or -1 when the keys ran
   out. */
static int load_count(form *f, const u16 *k, u8 *cmd)
{
    feed(k, 1, 0);
    if (setjmp(tk_jb) != 0)
        return -1;
    return memo_load_count(f, "NOTES", tc_lk, "ITEM", "1", cmd);
}

static u16 g_mf_calls;

/* The memo function of a form: memo_field for item 1, as a screen's
   callback does it. */
static u16 mf_cb(void *arg, form *f, u16 field)
{
    TF_CHECK(arg == (void *)&g_mf_calls);
    g_mf_calls++;
    g_mf_lines = memo_field(f, field, tc_lk, "ITEM", "1", 0, "Notes");
    return g_mf_lines;
}

/* Inside form_loop: a failure the screen must leave for stops the form
   (form_run returns Esc without asking for another key) and form_loop
   returns memo_leave's command; any other failure leaves it running. */
static void test_memo_stops(void)
{
    static const u16 k_stop[] = { KEY_PGDN, KEY_F4, KEY_ENTER, KEY_NONE };
    static const u16 k_stay[] = { KEY_PGDN, KEY_F4, KEY_ENTER, KEY_ESC,
                                  KEY_NONE };
    rq q;
    rq_res res;

    g_run_form = form_open(&item_def, FORM_VIEW, NULL, mf_cb, &g_mf_calls);
    if (!TF_CHECK(g_run_form != NULL))
        return;
    rq_begin(&q, "ITEM GET 1");
    TF_EQ(rq_send(tc_lk, &q, form_kline, g_run_form, &res), RQ_OK);
    TF_EQ(form_load(g_run_form, "NOTES", "42"), 0);

    g_mf_calls = 0;
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    TF_EQ(loop(g_run_form, &lp_plain, k_stay), 1);
    TF_EQ(g_lp.got, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 4);
    TF_EQ(g_mf_calls, 1);

    g_mf_calls = 0;
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    TF_EQ(loop(g_run_form, &lp_plain, k_stop), 1);
    TF_EQ(g_lp.got, TUI_CMD_LOGIN);
    TF_EQ(tk_pos(), 3);
    TF_EQ(g_mf_calls, 1);
    TF_STREQ(form_get(g_run_form, "NOTES"), "42");
    tl_clear();
    form_close(g_run_form);
    key_set_source(NULL, NULL);
}

static void test_memo_fields(void)
{
    static const u16 k_esc[] = { KEY_ESC, KEY_NONE };
    static const u16 k_ok[] = { KEY_ENTER, KEY_ENTER, KEY_NONE };
    static const u16 k_box[] = { KEY_ENTER, KEY_NONE };
    static const u16 k_none[] = { KEY_NONE };
    form *f;
    u8 cmd;

    f = get_item("1");
    if (!TF_CHECK(f != NULL))
        return;
    TF_EQ(tc_send("ITEM SET 1 NOTES=\"one\\ntwo\""), RQ_OK);

    /* Loaded into the field: the count; 0 after a failure the screen
       stays for, reported in a box in the client's text; nothing
       after one it must leave for. */
    tl_clear();
    cmd = TUI_CMD_QUIT;
    TF_EQ(load_count(f, k_none, &cmd), 1);
    TF_EQ(cmd, TUI_CMD_QUIT);
    TF_STREQ(form_get(f, "NOTES"), "2");
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    TF_EQ(load_count(f, k_box, &cmd), 1);
    TF_EQ(cmd, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 1);
    TF_CHECK(strstr(g_snapall, "Database error 302: the disk is full") !=
             NULL);
    TF_STREQ(form_get(f, "NOTES"), "0");
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    TF_EQ(form_load(f, "NOTES", "7"), 0);
    TF_EQ(load_count(f, k_box, &cmd), 0);
    TF_EQ(cmd, TUI_CMD_LOGIN);
    TF_STREQ(form_get(f, "NOTES"), "7");
    TF_EQ(form_load(f, "NOTES", "42"), 0);

    /* Shown: the count the server holds. */
    tl_clear();
    TF_EQ(memo_fld(f, 1, k_esc), 1);
    TF_EQ(g_mf_lines, 2);
    TF_STREQ(tl_req(0), "ITEM NOTES 1");

    /* A failed load is reported and the old count stays. */
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    TF_EQ(memo_fld(f, 0, k_ok), 1);
    TF_EQ(g_mf_lines, 42);

    /* A failure the screen must leave for: the old count stays (the
       command it stops the form with is checked through form_loop). */
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    TF_EQ(memo_fld(f, 0, k_ok), 1);
    TF_EQ(g_mf_lines, 42);
    tl_clear();
    form_close(f);
    key_set_source(NULL, NULL);
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    char mdir[PATHMAX];
    int rc;

    tf_begin("T_FORM");
    cmem_init();
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_FORM dir [msgdir]\n");
        return 2;
    }
    strcpy(g_ddir, argv[1]);
    tf_progdir(mdir, PATHMAX, argv[0]);
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    g_snapall = (char *)malloc(SNAPSZ);
    g_typed = (u16 *)malloc((TK_MAXKEYS + 1) * sizeof *g_typed);
    if (!TF_CHECK(g_snapall != NULL && g_typed != NULL))
        return tf_done();
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_FORM: %s\n", why);
        return tf_done();
    }
    /* The far blocks come after the engine's near heap has grown: a
       DOS program's near heap cannot grow past memory allocated after
       it. */
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("T_FORM: %s\n", msg_why());
        return tf_done();
    }
    if (!TF_EQ(vid_open(0), 0))
        return tf_done();
    tui_set_exit(tk_exit);
    tui_palette(TP_COLOR);
    tui_s.l = tc_lk;
    tui_s.level = TUI_LVL_ADMIN;
    if (!tc_admin())
        return tf_done();
    test_checks();
    test_editing();
    test_start();
    test_typed();
    test_saving();
    test_keyed();
    test_errors();
    test_keys_kept();
    test_loop();
    test_part2();
    test_memo_fields();
    test_memo_stops();
    vid_close();
    TF_EQ(link_close(tc_lk), 0);
    msg_free();
    free(g_typed);
    free(g_snapall);
    te_remove(g_ddir, NULL);
    return tf_done();
}

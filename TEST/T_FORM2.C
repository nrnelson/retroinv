/*
 * T_FORM2.C - the second half of the T_FORM test (see T_FORM.C): the
 * memo editor, the helpers the screens share for values as text, choice
 * words, local field errors and form_store, and the fields the cursor
 * stops at with the value selected on entry.  It is a file of its
 * own only to stay within the compiler memory of Watcom C 6.5; T_FORM.H
 * holds what both share.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "MEMOIO.H"
#include "TRLINK.H"
/* Checks keep no expression text, as in T_FORM.C: Turbo C stores every
   string literal again. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_PART
#define TF_FILE "T_FORM2"
#define TF_KEYSRC
#define TF_LINK
#include "TESTFW.H"
#include "T_FORM.H"

/* ---------------------------------------------------------------------
 * The memo editor.
 * --------------------------------------------------------------------- */
/* The typed memo: lines of MEMO_TLEN characters, two of them escaped
   (tch), MEMO_TPER of them with their escaped newlines to a request (a
   NOTES NEW= is as long as a SET), and MEMO_TLINES of them, which take
   two requests. */
#define MEMO_TLEN       58
#define MEMO_TPER       ((u16)((LINK_MAXLINE + 2 -                       \
                                (sizeof "ITEM SET 1 NOTES=\"\"" - 1)) /  \
                               (MEMO_TLEN + 4)))
#define MEMO_TLINES     ((u16)(MEMO_TPER + 2))

#define NOTEW           (MEMO_TLEN + 4)

static char g_notes[MEMO_TLINES + 2][NOTEW + 1];
static u16 g_nnotes;

/* rq_fn of ITEM NOTES: each L line into g_notes, cut to NOTEW. */
static int note_line(void *arg, const rq_line *ln)
{
    TF_CHECK(arg == (void *)g_notes);
    if (ln->kind == 'L' && g_nnotes < MEMO_TLINES + 2) {
        strncpy(g_notes[g_nnotes], ln->v[0], NOTEW);
        g_notes[g_nnotes++][NOTEW] = '\0';
    }
    return 0;
}

static void read_notes(const char *id)
{
    rq q;
    rq_res res;

    g_nnotes = 0;
    rq_begin(&q, "ITEM NOTES");
    rq_word(&q, id);
    TF_EQ(rq_send(tc_lk, &q, note_line, g_notes, &res), RQ_OK);
}

static int g_memo_rc;
static u16 g_memo_lines;

static int edit(const char *id, int ro, const u16 *k, u16 at, u8 r)
{
    feed(k, at, r);
    g_memo_rc = -1;
    g_memo_lines = 999;
    if (setjmp(tk_jb) != 0)
        return 0;
    g_memo_rc = memo_edit(tc_lk, "ITEM", id, ro, "Notes", &g_memo_lines);
    return 1;
}

/* The character of line i, column c of the typed memo. */
static char tch(u16 i, u16 c)
{
    if (c == 10)
        return '"';
    if (c == 20)
        return '\\';
    return (char)('a' + (i + c) % 26);
}

static void test_memos(void)
{
    static const u16 k_esc[] = { KEY_ESC, KEY_NONE };
    static const u16 k_ro[] = { 'a', KEY_CTRL('S'), KEY_DEL, KEY_ESC,
                                KEY_NONE };
    static const u16 k_fail[] = { 'Z', KEY_CTRL('S'), KEY_ENTER, KEY_ENTER,
                                  KEY_ESC, 'Y', KEY_NONE };
    static const u16 k_hi[] = { 'h', 'i', ' ', ' ', KEY_ENTER, KEY_ENTER,
                                KEY_CTRL('S'), KEY_NONE };
    static const u16 k_kill[] = { 0x19, KEY_CTRL('S'), KEY_NONE };
    static const u16 k_add[] = { KEY_CEND, 'x', KEY_CTRL('S'), KEY_NONE };
    static const u16 k_ok[] = { KEY_ENTER, KEY_NONE };
    char want[MEMO_TLEN + 1];
    char m[MSG_MAXLEN + 2];
    rq q;
    rq_res res;
    u16 *k;
    u16 n;
    u16 i;
    u16 c;
    u16 lines;

    k = g_typed;
    n = 0;
    for (i = 0; i < MEMO_TLINES; i++) {
        for (c = 0; c < MEMO_TLEN; c++)
            k[n++] = (u16)(u8)tch(i, c);
        if (i + 1 < MEMO_TLINES)
            k[n++] = KEY_ENTER;
    }
    k[n++] = KEY_CTRL('S');
    k[n] = KEY_NONE;
    tl_clear();
    TF_EQ(edit("1", 0, k, 0, 0), 1);
    TF_EQ(g_memo_rc, RQ_OK);
    TF_EQ(g_memo_lines, MEMO_TLINES);
    TF_CHECK(all_fit());
    TF_STREQ(tl_req(0), "ITEM NOTES 1");
    TF_CHECK(tf_starts(tl_req(1), "ITEM NOTES 1 NEW=\""));
    TF_EQ(tl_count(), 3);
    TF_CHECK(tf_starts(tl_req(2), "ITEM NOTES 1 END=\""));
    read_notes("1");
    TF_EQ(g_nnotes, MEMO_TLINES);
    for (i = 0; i < MEMO_TLINES && i < g_nnotes; i++) {
        for (c = 0; c < MEMO_TLEN; c++)
            want[c] = tch(i, c);
        want[MEMO_TLEN] = '\0';
        TF_STREQ(g_notes[i], want);
    }
    TF_EQ(mio_count(tc_lk, "ITEM", "1", &lines, &res), RQ_OK);
    TF_EQ(lines, MEMO_TLINES);

    /* Read-only: nothing changes, nothing is sent. */
    tl_clear();
    TF_EQ(edit("1", 1, k_ro, 0, 0), 1);
    TF_EQ(g_memo_rc, RQ_OK);
    TF_EQ(g_memo_lines, MEMO_TLINES);
    TF_EQ(tl_count(), 1);

    /* A save failing at its last request: the editor stays open and
       says how many lines arrived, the stored memo and its count being
       as they were; Esc then asks before leaving. */
    tl_clear();
    tl_fail(3, "-ERR 302 disk full");
    TF_EQ(edit("1", 0, k_fail, 3, (u8)(TUI_TOP + TUI_WORKROWS / 2)), 1);
    TF_EQ(tk_pos(), 6);
    TF_EQ(g_memo_rc, RQ_EFULL);
    TF_EQ(tl_count(), 3);
    TF_CHECK(tf_starts(tl_req(1), "ITEM NOTES 1 NEW=\"Z"));
    TF_CHECK(strstr(g_snapall, "Saving stopped after") != NULL);
    TF_CHECK(strstr(g_snapall, "the stored notes are") != NULL &&
             strstr(g_snapall, " unchanged ") != NULL);
    sprintf(m, " %u of %u lines;", (unsigned)MEMO_TPER,
            (unsigned)MEMO_TLINES);
    TF_CHECK(strstr(g_snapall, m) != NULL);
    TF_EQ(g_memo_lines, MEMO_TLINES);
    read_notes("1");
    TF_EQ(g_nnotes, MEMO_TLINES);
    TF_EQ(g_notes[0][0], tch(0, 0));

    /* An older server refuses NEW=: the editor says so in place of the
       refusal, and stays. */
    tl_clear();
    tl_fail(2, "-ERR 102 unexpected key FIELD=NEW");
    TF_EQ(edit("1", 0, k_fail, 3, (u8)(TUI_TOP + TUI_WORKROWS / 2)), 1);
    TF_EQ(g_memo_rc, RQ_EARG);
    TF_CHECK(strstr(g_snapall, "too old to save notes") != NULL);
    TF_CHECK(strstr(g_snapall, "nexpected key") == NULL);
    tl_clear();
    tl_fail(2, "-ERR 102 unexpected key FIELD=NEW");
    TF_EQ(edit("1", 0, k_fail, 4, TUI_HINTROW), 1);
    TF_CHECK(strstr(g_snap, "nexpected key") == NULL);
    read_notes("1");
    TF_EQ(g_notes[0][0], tch(0, 0));

    /* Long and TAB-holding lines are split when loaded and said so, the
       memo staying editable; they are written back only by a save. */
    rq_begin(&q, "ITEM SET 2");
    rq_pair(&q, "NOTES",
            "\tabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
            "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijxyz");
    TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
    TF_EQ(mio_count(tc_lk, "ITEM", "2", &lines, &res), RQ_OK);
    TF_EQ(lines, 2);
    tl_clear();
    TF_EQ(edit("2", 0, k_esc, 1, TUI_HINTROW), 1);
    TF_EQ(g_memo_rc, RQ_OK);
    TF_EQ(g_memo_lines, 2);
    TF_EQ(tl_count(), 1);
    m[0] = ' ';
    strcpy(m + 1, msg(MM_CHANGED));
    TF_STREQ(g_snap, m);
    tl_clear();
    TF_EQ(edit("2", 0, k_add, 0, 0), 1);
    TF_EQ(g_memo_rc, RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(1), "ITEM SET 2 NOTES=\"        abc"));
    TF_CHECK(strstr(tl_req(1), "xyzx\"") != NULL);

    /* TABs that expand past the stored limit: read-only, nothing sent. */
    tc_send("ITEM SET 2 NOTES=\"\"");
    for (i = 0; i < 6; i++) {
        rq_begin(&q, "ITEM NOTES 2");
        for (c = 0; c < 50; c++)
            want[c] = '\t';
        TF_CHECK(rq_trypairn(&q, "ADD", want, 50));
        TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
    }
    tl_clear();
    TF_EQ(edit("2", 0, k_ro, 1, TUI_HINTROW), 1);
    TF_EQ(g_memo_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    m[0] = ' ';
    strcpy(m + 1, msg(MM_TOOLONG));
    TF_STREQ(g_snap, m);

    /* A failure that ends the login: memo_leave gives the command. */
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    TF_EQ(edit("2", 0, k_ok, 0, 0), 1);
    TF_EQ(g_memo_rc, RQ_EMUSTCHG);
    TF_EQ(memo_leave(), TUI_CMD_LOGIN);
    TF_EQ(edit("2", 1, k_esc, 0, 0), 1);
    TF_EQ(memo_leave(), TUI_CMD_NONE);

    /* Trailing spaces and empty lines are dropped; Ctrl-Y on the only
       line and a save send the empty memo. */
    tc_send("ITEM SET 2 NOTES=\"\"");
    tl_clear();
    TF_EQ(edit("2", 0, k_hi, 0, 0), 1);
    TF_EQ(g_memo_lines, 1);
    TF_STREQ(tl_req(1), "ITEM SET 2 NOTES=hi");
    tl_clear();
    TF_EQ(edit("2", 0, k_kill, 0, 0), 1);
    TF_EQ(g_memo_lines, 0);
    TF_STREQ(tl_req(1), "ITEM SET 2 NOTES=\"\"");
    read_notes("2");
    TF_EQ(g_nnotes, 0);
    key_set_source(NULL, NULL);
}

/* The split memo: SPLIT_N lines of SPLIT_LEN characters, the first
   SPLIT_REM more, holding SPLIT_N + 1 quotes and backslashes, each
   escaped on the line, so that "ITEM SET 2 NOTES=" with the memo
   quoted, its newlines escaped, is exactly LINK_MAXLINE bytes; SPLIT_N
   is the fewest lines that keep each within MEMO_COLS. */
#define SPLIT_N         ((LINK_MAXLINE - 18) / (MEMO_COLS - 1) + 1)
#define SPLIT_ALL       (LINK_MAXLINE -                                 \
                         (sizeof "ITEM SET 2 NOTES=\"\"" - 1) -          \
                         (SPLIT_N + 1) - 2 * (SPLIT_N - 1))
#define SPLIT_LEN       ((u16)(SPLIT_ALL / SPLIT_N))
#define SPLIT_REM       ((u16)(SPLIT_ALL % SPLIT_N))
extern char split_fits[1];
extern char split_fits[(SPLIT_LEN + SPLIT_REM <= MEMO_COLS) ? 1 : 2];

/* The characters of line i of the split memo. */
static u16 split_len(u16 i)
{
    return (u16)(SPLIT_LEN + (i == 0 ? SPLIT_REM : 0));
}

/* The character of line i, column c of the split memo: a backslash or
   a quote where one is escaped, the first line holding two; extra adds
   one more backslash to the last line. */
static char sch(u16 i, u16 c, int extra)
{
    if (c == 0 || (i == 0 && c == 1) ||
        (extra && i + 1 == SPLIT_N && c == 2))
        return c == 1 ? '"' : '\\';
    return (char)('a' + (i + c) % 26);
}

/* A save puts as many memo lines in a request as fit LINK_MAXLINE, its
   escapes counted: lines making a SET of exactly LINK_MAXLINE bytes go
   in one request; one escape more stages the memo, NOTES NEW= as long
   as a SET, and moves the last line to the NOTES END= that writes it,
   leaving the NEW= shorter by that line (SPLIT_LEN bytes and two
   escapes, its own and its newline's). */
static void test_memo_split(void)
{
    char want[SPLIT_LEN + SPLIT_REM + 1];
    int extra;
    u16 n;
    u16 i;
    u16 c;

    for (extra = 0; extra < 2; extra++) {
        n = 0;
        for (i = 0; i < SPLIT_N; i++) {
            for (c = 0; c < split_len(i); c++)
                g_typed[n++] = (u16)(u8)sch(i, c, extra);
            if (i + 1 < SPLIT_N)
                g_typed[n++] = KEY_ENTER;
        }
        g_typed[n++] = KEY_CTRL('S');
        g_typed[n] = KEY_NONE;
        TF_EQ(tc_send("ITEM SET 2 NOTES=\"\""), RQ_OK);
        tl_clear();
        TF_EQ(edit("2", 0, g_typed, 0, 0), 1);
        TF_EQ(g_memo_rc, RQ_OK);
        TF_EQ(g_memo_lines, SPLIT_N);
        TF_EQ(tl_count(), extra ? 3 : 2);
        TF_CHECK(tf_starts(tl_req(1), extra ? "ITEM NOTES 2 NEW=\"" :
                           "ITEM SET 2 NOTES=\""));
        TF_EQ(strlen(tl_req(1)),
              extra ? LINK_MAXLINE - (SPLIT_LEN + 3) : LINK_MAXLINE);
        if (extra)
            TF_CHECK(tf_starts(tl_req(2), "ITEM NOTES 2 END=\""));
        read_notes("2");
        TF_EQ(g_nnotes, SPLIT_N);
        for (i = 0; i < SPLIT_N && i < g_nnotes; i++) {
            for (c = 0; c < split_len(i); c++)
                want[c] = sch(i, c, extra);
            want[split_len(i)] = '\0';
            TF_STREQ(g_notes[i], want);
        }
    }
    TF_EQ(tc_send("ITEM SET 2 NOTES=\"\""), RQ_OK);
    key_set_source(NULL, NULL);
}

/* ---------------------------------------------------------------------
 * The helpers the screens share: values as text, choice words, local
 * field errors, form_store and memo_field.
 * --------------------------------------------------------------------- */
static void test_values(void)
{
    const char *ch;
    char d[FORM_MAXVAL + 2];
    char w[8];
    rq_line ln;
    form *f;

    ch = item_fields[F_COND].choices;
    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    /* form_kline loads the pair of a K line as form_load does, and
       passes over every other line: another kind, a K line without its
       value, a name the form has not. */
    strcpy(d, "SERIALNO");
    strcpy(w, "XT");
    ln.kind = 'K';
    ln.n = 2;
    ln.over = 0;
    ln.v[0] = d;
    ln.v[1] = w;
    TF_EQ(form_kline(f, &ln), 0);
    TF_STREQ(form_get(f, "SERIALNO"), "XT");
    TF_CHECK(!form_dirty(f));
    strcpy(w, "AT");
    ln.kind = 'R';
    TF_EQ(form_kline(f, &ln), 0);
    ln.kind = 'K';
    ln.n = 1;
    TF_EQ(form_kline(f, &ln), 0);
    TF_STREQ(form_get(f, "SERIALNO"), "XT");
    strcpy(d, "NOSUCH");
    ln.n = 2;
    TF_EQ(form_kline(f, &ln), 0);
    form_put(f, "MFR", "IBM");
    strcpy(d, "junk");
    form_get_text(f, "MFR", d, sizeof d);
    TF_STREQ(d, "IBM");
    form_get_text(f, "MFR", d, 4);
    TF_STREQ(d, "IBM");
    form_get_text(f, "MFR", d, 3);
    TF_STREQ(d, "IB");
    form_get_text(f, "MFR", d, 1);
    TF_STREQ(d, "");
    strcpy(d, "junk");
    form_get_text(f, "MODEL", d, sizeof d);
    TF_STREQ(d, "");
    strcpy(d, "junk");
    form_get_text(f, "NOSUCH", d, sizeof d);
    TF_STREQ(d, "");
    form_close(f);

    TF_EQ(MSG_DATELEN, 8);
    TF_EQ(FORM_DATEW, MSG_DATEW);
    TF_EQ(FORM_MEMOW, 5);
    TF_EQ(FORM_MEMODISP, 30);

    /* Words count from 0; a word not in the list (a prefix, a longer
       word, nothing) gives 0. */
    TF_EQ(form_choice_num(ch, "NEW"), 0);
    TF_EQ(form_choice_num(ch, "EXC"), 1);
    TF_EQ(form_choice_num(ch, "GD"), 2);
    TF_EQ(form_choice_num(ch, "PT"), 7);
    TF_EQ(form_choice_num(ch, "XX"), 0);
    TF_EQ(form_choice_num(ch, "EX"), 0);
    TF_EQ(form_choice_num(ch, "GDX"), 0);
    TF_EQ(form_choice_num(ch, "gd"), 0);
    TF_EQ(form_choice_num(ch, ""), 0);
    TF_EQ(form_choice_num(" A  D ", "D"), 1);
    TF_EQ(form_choice_num("A D", "A D"), 0);
    strcpy(w, "junk");
    form_choice_word(ch, 0, w, sizeof w);
    TF_STREQ(w, "NEW");
    form_choice_word(ch, 1, w, sizeof w);
    TF_STREQ(w, "EXC");
    form_choice_word(ch, 7, w, sizeof w);
    TF_STREQ(w, "PT");
    strcpy(w, "junk");
    form_choice_word(ch, 8, w, sizeof w);
    TF_STREQ(w, "");
    strcpy(w, "junk");
    form_choice_word(ch, 999, w, sizeof w);
    TF_STREQ(w, "");
    form_choice_word(ch, 1, w, 4);
    TF_STREQ(w, "EXC");
    form_choice_word(ch, 1, w, 3);
    TF_STREQ(w, "EX");
    form_choice_word(ch, 1, w, 1);
    TF_STREQ(w, "");
    form_choice_word(" A  D ", 1, w, sizeof w);
    TF_STREQ(w, "D");
    strcpy(w, "junk");
    form_choice_word("", 0, w, sizeof w);
    TF_STREQ(w, "");
}

/* form_field_error on f with no keys to read; 1 when it returned
   without asking for one. */
static int field_error(form *f, const char *name, const char *text)
{
    static const u16 none[] = { KEY_NONE };

    feed(none, 0, 0);
    if (setjmp(tk_jb) != 0)
        return 0;
    form_field_error(f, name, text);
    return 1;
}

/* form_field_error: the cursor on the named field and its text on the
   hint row, as for a field the server names. */
static void test_field_errors(void)
{
    static const u16 none[] = { KEY_NONE };
    form *f;

    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "CATEGORY", "SYS");
    g_run_form = f;
    if (!TF_CHECK(field_error(f, "QTY", "Too many"))) {
        form_close(f);
        return;
    }
    TF_EQ(run(none, 0, 0), 0);
    TF_STREQ(tk_row(TUI_HINTROW), " Too many");
    TF_EQ(tk_attr(vrow(F_QTY), vcol()), tui_attr(TA_INPUTON));
    if (!TF_CHECK(field_error(f, "ACQ_PRICE", "Bad price"))) {
        form_close(f);
        return;
    }
    TF_EQ(run(none, 0, 0), 0);
    TF_STREQ(tk_row(TUI_HINTROW), " Bad price");
    TF_EQ(tk_attr(vrow(F_PRICE), vcol()), tui_attr(TA_INPUTON));
    TF_EQ(tk_attr(vrow(F_QTY), vcol()) == tui_attr(TA_INPUTON), 0);

    /* A name the form lacks: the text on the hint row, the cursor where
       it was. */
    if (!TF_CHECK(field_error(f, "NOSUCH", "Plain words"))) {
        form_close(f);
        return;
    }
    TF_EQ(run(none, 0, 0), 0);
    TF_STREQ(tk_row(TUI_HINTROW), " Plain words");
    TF_EQ(tk_attr(vrow(F_PRICE), vcol()), tui_attr(TA_INPUTON));
    TF_STREQ(form_get(f, "CATEGORY"), "SYS");
    form_close(f);
}

static int g_store_rc;
static u8 g_store_cmd;
static rq_res g_store_res;

/* form_store on f, titled MT_NOHELP ("No help: ^1"), with the keys k
   for the message boxes; 1 when it returned. */
static int store(form *f, const char *table, char *id, u16 idsz,
                 const char *idfield, const u16 *k)
{
    feed(k, 0, 0);
    g_store_rc = 99;
    g_store_cmd = 99;
    if (setjmp(tk_jb) != 0)
        return 0;
    g_store_rc = form_store(f, tc_lk, table, id, idsz, idfield, MT_NOHELP,
                            &g_store_res, &g_store_cmd);
    return 1;
}

static void test_storing(void)
{
    static const u16 none[] = { KEY_NONE };
    static const u16 k2[] = { KEY_ENTER, KEY_ENTER, KEY_ENTER, KEY_NONE };
    form *f;
    char id[LINK_IDLEN + 1];
    char code[LINK_CATLEN + 1];
    char want[LINK_IDLEN + 16];
    u16 i;

    f = form_open(&cat_def, FORM_NEW, NULL, NULL, NULL);
    /* Saved: 1, nothing reported, the key as the record's code. */
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "CAT_CODE", "C7");
    form_put(f, "CAT_NAME", "Seven");
    code[0] = '\0';
    tl_clear();
    TF_EQ(store(f, "CAT", code, sizeof code, NULL, none), 1);
    TF_EQ(g_store_rc, 1);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 0);
    TF_STREQ(code, "C7");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "CAT ADD C7 CAT_NAME=Seven");
    form_close(f);

    /* The ADD itself fails: reported, the form stays a new one with its
       own title, and the screen stays. */
    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    form_put(f, "CATEGORY", "SYS");
    id[0] = '\0';
    tl_clear();
    tl_fail(1, "-ERR 302 disk full");
    TF_EQ(store(f, "ITEM", id, sizeof id, "ITEM_ID", k2), 1);
    TF_EQ(g_store_rc, 0);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_EQ(g_store_res.code, RQ_EFULL);
    TF_CHECK(tk_pos() >= 1);
    TF_EQ(form_mode_get(f), FORM_NEW);
    TF_CHECK(form_dirty(f));
    form_paint(f);
    TF_CHECK(strstr(tk_row(BOXTOP), "No help") == NULL);
    form_close(f);

    /* The ADD succeeds and a later SET fails: 0, the form retitled for
       the new record and its ID field loaded, both messages shown (two
       boxes), and the screen stays; saving again sends only SETs. */
    f = open_item(FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    fill_long(f);
    id[0] = '\0';
    tl_clear();
    tl_fail(2, "-ERR 302 disk full");
    TF_EQ(store(f, "ITEM", id, sizeof id, "ITEM_ID", k2), 1);
    TF_EQ(g_store_rc, 0);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_EQ(g_store_res.code, RQ_EFULL);
    TF_EQ(tk_pos(), 2);
    TF_CHECK(id[0] >= '1' && id[0] <= '9');
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_STREQ(form_get(f, "ITEM_ID"), id);
    form_paint(f);
    TF_CHECK(strstr(tk_row(BOXTOP), msg_sub(MT_NOHELP, id, NULL)) != NULL);
    tl_clear();
    TF_EQ(store(f, "ITEM", id, sizeof id, "ITEM_ID", none), 1);
    TF_EQ(g_store_rc, 1);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_EQ(tk_pos(), 0);
    TF_CHECK(tl_count() >= 1);
    strcpy(want, "ITEM SET ");
    strcat(want, id);
    strcat(want, " ");
    for (i = 0; i < tl_count(); i++)
        TF_CHECK(tf_starts(tl_req(i), want));
    TF_CHECK(strstr(tl_log(), "ITEM ADD") == NULL);
    form_close(f);

    /* A field the server names: 0, no box, the cursor on the field. */
    g_run_form = get_item("1");
    if (!TF_CHECK(g_run_form != NULL))
        return;
    form_mode_set(g_run_form, FORM_EDIT);
    strcpy(id, "1");
    form_put(g_run_form, "LOC_ID", "ZZ");
    tl_clear();
    TF_EQ(store(g_run_form, "ITEM", id, sizeof id, "ITEM_ID", none), 1);
    TF_EQ(g_store_rc, 0);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_STREQ(g_store_res.field, "LOC_ID");
    TF_EQ(run(none, 0, 0), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), g_store_res.text + 1) != NULL);
    TF_EQ(tk_attr(vrow(F_LOC), vcol()), tui_attr(TA_BOLD));
    form_put(g_run_form, "LOC_ID", "A1");

    /* A record a field names is gone: a field error, the form staying
       with the server's text, the cursor on the field. */
    tl_clear();
    tl_fail(1, "-ERR 200 no such location FIELD=LOC_ID");
    TF_EQ(store(g_run_form, "ITEM", id, sizeof id, "ITEM_ID", none), 1);
    TF_EQ(g_store_rc, 0);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_EQ(run(none, 0, 0), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), "No such location") != NULL);
    TF_EQ(tk_attr(vrow(F_LOC), vcol()), tui_attr(TA_BOLD));

    /* The record is gone: reported, then the screen leaves for the home
       screen. */
    form_put(g_run_form, "MFR", "Gone");
    tl_clear();
    tl_fail(1, "-ERR 200 no such record");
    TF_EQ(store(g_run_form, "ITEM", id, sizeof id, "ITEM_ID", k2), 1);
    TF_EQ(g_store_rc, -1);
    TF_EQ(g_store_cmd, TUI_CMD_NONE);
    TF_EQ(g_store_res.code, RQ_ENOTFOUND);

    /* A failure that ends the session: the screen leaves with the
       command tui_fail gives. */
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    TF_EQ(store(g_run_form, "ITEM", id, sizeof id, "ITEM_ID", k2), 1);
    TF_EQ(g_store_rc, -1);
    TF_EQ(g_store_cmd, TUI_CMD_LOGIN);
    TF_EQ(g_store_res.code, RQ_EMUSTCHG);
    form_close(g_run_form);
    tl_clear();
}

/* ---------------------------------------------------------------------
 * Tab stops and the selected value.
 * --------------------------------------------------------------------- */
/* A text field, one that cannot be changed, a date and a memo, on the
   rows of the item form's first four fields. */
static const form_field sel_fields[4] = {
    { "A", MT_NOTAVAIL, 0, 0, VCOL, FT_CHAR, 8, 0, 0, 0, NULL },
    { "B", MT_NOTFOUND, 1, 0, VCOL, FT_CHAR, 8, 0, FF_RO, 0, NULL },
    { "D", MT_DUP, 2, 0, VCOL, FT_DATE, 8, 0, 0, 0, NULL },
    { "N", MT_BUSY, 3, 0, VCOL, FT_MEMO, 5, 0, 0, 30, NULL }
};

static const form_def sel_def = {
    MT_INTERNAL, "ITEM", BOXTOP, BOXLEFT, 6, 40, 4, sel_fields
};

/* The role field i's first value cell is drawn in. */
static int role_at(u16 i, u8 role)
{
    return TF_EQ(tk_attr(vrow(i), vcol()), tui_attr(role));
}

/* g_typed: a click on field i's value, then key k. */
static const u16 *click_then(u16 i, u16 k)
{
    g_typed[0] = (u16)(KEY_CLICK + vrow(i) * KEY_CROW + vcol() + 1);
    g_typed[1] = k;
    g_typed[2] = KEY_NONE;
    return g_typed;
}

static void test_select(void)
{
    static const u16 none[] = { KEY_NONE };
    static const u16 k1[] = { KEY_TAB, '2', '0', '2', '6', '0', '1', '0',
                              '2', KEY_STAB, 'S', 'h', 'u', KEY_NONE };
    static const u16 k2[] = { KEY_LEFT, 'x', KEY_NONE };
    static const u16 k3[] = { KEY_TAB, KEY_NONE };
    static const u16 k4[] = { KEY_BS, KEY_NONE };
    static const u16 k5[] = { KEY_PGDN, KEY_NONE };
    static const u16 k6[] = { KEY_DEL, KEY_NONE };
    static const u16 k7[] = { KEY_PGDN, 'x', KEY_NONE };
    form *f;

    f = form_open(&sel_def, FORM_EDIT, NULL, NULL, NULL);
    if (!TF_CHECK(f != NULL))
        return;
    form_load(f, "A", "Intel");
    form_load(f, "B", "ro");
    form_load(f, "D", "19990512");
    g_run_form = f;
    /* The value of the field the cursor starts on is selected. */
    TF_EQ(run(none, 0, 0), 0);
    role_at(0, TA_BOLD);
    /* Tab and Shift-Tab pass B; what is typed replaces the selected
       date and text. */
    TF_EQ(run(k1, 0, 0), 0);
    TF_STREQ(form_get(f, "D"), "20260102");
    TF_STREQ(form_get(f, "A"), "Shu");
    TF_STREQ(form_get(f, "B"), "ro");
    /* An arrow drops the selection: the text is edited in place. */
    TF_EQ(run(k2, 0, 0), 0);
    TF_STREQ(form_get(f, "A"), "Shxu");
    role_at(0, TA_INPUTON);
    /* A date entered is selected too, and Backspace clears it. */
    TF_EQ(run(k3, 0, 0), 0);
    role_at(2, TA_BOLD);
    TF_EQ(run(k4, 0, 0), 0);
    TF_STREQ(form_get(f, "D"), "");
    /* So does Del. */
    form_put(f, "D", "19990512");
    TF_EQ(run(k6, 0, 0), 0);
    TF_STREQ(form_get(f, "D"), "");
    /* A click on B leaves the cursor on D; one on A selects its value;
       one on A again, the field being edited, edits it in place. */
    TF_EQ(run(click_then(1, '7'), 0, 0), 0);
    TF_STREQ(form_get(f, "D"), "7");
    TF_EQ(run(click_then(0, 'Q'), 0, 0), 0);
    TF_STREQ(form_get(f, "A"), "Q");
    TF_EQ(run(click_then(0, 'y'), 0, 0), 0);
    TF_STREQ(form_get(f, "A"), "Qy");
    /* Del and Backspace clear a selected text. */
    TF_EQ(run(k6, 0, 0), 0);
    TF_STREQ(form_get(f, "A"), "");
    form_put(f, "A", "Qy");
    TF_EQ(run(k4, 0, 0), 0);
    TF_STREQ(form_get(f, "A"), "");
    /* The memo of a saved record is a stop all the same: PgDn goes to
       it, and a character typed there says that F4 opens it. */
    TF_EQ(run(k5, 0, 0), 0);
    role_at(3, TA_INPUTON);
    TF_EQ(run(k7, 0, 0), 0);
    TF_CHECK(strstr(tk_row(TUI_HINTROW), msg(MF_MEMOKEY)) != NULL);
    /* A new record's memo is not: PgDn goes to D, selected. */
    form_mode_set(f, FORM_NEW);
    TF_EQ(run(k5, 0, 0), 0);
    role_at(2, TA_BOLD);
    /* In view mode Tab stops at B. */
    form_mode_set(f, FORM_VIEW);
    TF_EQ(run(click_then(0, KEY_TAB), 0, 0), 0);
    role_at(1, TA_INPUTON);
    form_close(f);
}

void test_part2(void)
{
    test_memos();
    test_memo_split();
    test_values();
    test_field_errors();
    test_storing();
    test_select();
}

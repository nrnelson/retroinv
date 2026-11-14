/*
 * FORM.C - typed data-entry forms of the full-screen client: drawing and
 * editing over the form model (FMODEL.C).
 *
 * Positions in the form table count from the box's first inside cell: a
 * label at row r, column c is drawn at top + 1 + r, left + 1 + c, and
 * its value at column left + 1 + vcol.
 *
 * Dates are typed into a template with fixed hyphens (YYYY-MM-DD) and
 * held as the digits typed, so a complete date is its wire form
 * YYYYMMDD; the check before a save refuses anything else.
 */
#define TUI_VERBS
#define FMODEL_CALLS
#define TUI_CLICKS
#define TUI_SELECT
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "FMODEL.H"
#include "FORM.H"

#define MF_YN           255     /* the letters shown for T and F        */
#define MF_MEMOLINES    256     /* the memo field: ^1 lines             */
#define MF_MEMONEW      257     /* the memo field of a new record       */
#define MF_SAVEMEMO     258     /* F4 on the memo of a new record       */
#define MF_MEMOKEY      260     /* a character typed on the memo field  */

struct form {
    form_model m;               /* first, for the model's functions     */
    form_pick_fn pick;
    form_memo_fn memo;
    void *arg;
    u8   cur;                   /* the field with the cursor            */
    u8   was;                   /* the field edited last; FORM_MAXFIELDS
                                   when form_run has edited none yet    */
    u8   hastitle;
    u8   run;                   /* form_run is drawing the cursor       */
    u8   stop;                  /* form_stop: form_run returns at once  */
    u8   leave;                 /* form_stop: the command to leave with */
    u8   noask;                 /* form_noask: changes never hold keys  */
    char title[FORM_TITLEMAX + 1];
};

/* Field i of the form's table. */
static const form_field *fld(form *f, u16 i)
{
    return &f->m.def->fields[i];
}

/* Cells a field's value takes on screen. */
static u16 disp_w(const form_field *d)
{
    if (d->type == FT_DATE)
        return FORM_DATEW;
    if (d->type == FT_LOGIC)
        return 1;
    if (d->dispw != 0)
        return d->dispw;
    if (d->type == FT_MEMO)
        return FORM_MEMODISP;
    return d->width;
}

/* 1 when field i can be changed in the form's mode. */
static int editable(form *f, u16 i)
{
    const form_field *d;

    d = fld(f, i);
    if (f->m.mode == FORM_VIEW || (d->flags & FF_RO) || d->type == FT_MEMO)
        return 0;
    if ((d->flags & FF_NEWONLY) && f->m.mode != FORM_NEW)
        return 0;
    return 1;
}

/* 1 when the cursor stops at field i: outside view mode a field that
   can be changed or the memo of a saved record (F4), in view mode any
   but one that only shows a value (FF_LOCAL | FF_RO, such as a
   location's path). */
static int stop(form *f, u16 i)
{
    const form_field *d;

    d = fld(f, i);
    if (f->m.mode == FORM_VIEW)
        return (d->flags & (FF_LOCAL | FF_RO)) != (FF_LOCAL | FF_RO);
    return (d->type == FT_MEMO && f->m.mode != FORM_NEW) || editable(f, i);
}

/* Outside view mode, the cursor from a field that cannot be changed (a
   record ID) on to the first after it that can, or to the last field. */
static void to_edit(form *f)
{
    if (f->m.mode == FORM_VIEW)
        return;
    while (f->cur + 1 < f->m.def->nfields && !editable(f, f->cur))
        f->cur++;
}

form *form_open(const form_def *def, u8 mode, form_pick_fn pick,
                form_memo_fn memo, void *arg)
{
    form *f;

    f = form_new(def, mode, (u16)sizeof *f);
    if (f != NULL) {
        f->pick = pick;
        f->memo = memo;
        f->arg = arg;
        to_edit(f);
    }
    return f;
}

void form_set_title(form *f, const char *title)
{
    txt_copy(f->title, title, (u16)sizeof f->title);
    f->hastitle = 1;
}

/* ---------------------------------------------------------------------
 * Drawing.
 * --------------------------------------------------------------------- */
static u8 vrow(form *f, u16 i)
{
    return (u8)(f->m.def->top + 1 + fld(f, i)->row);
}

static u8 vcol(form *f, u16 i)
{
    return (u8)(f->m.def->left + 1 + fld(f, i)->vcol);
}

/* The cell of the YYYY-MM-DD template that shows digit pos of a date:
   the digits skip the hyphens after the year and the month. */
static u16 date_cell(u16 pos)
{
    return (u16)(pos < 4 ? pos : (pos < 6 ? pos + 1 : pos + 2));
}

static void draw_value(form *f, u16 i, u8 role)
{
    const form_field *d;
    const char *v;
    char t[FORM_MAXVAL + 1];
    u16 n;

    d = fld(f, i);
    v = form_val(f, i);
    switch (d->type) {
    case FT_DATE:
        n = (u16)strlen(v);
        memset(t, ' ', FORM_DATEW);
        t[FORM_DATEW] = '\0';
        if (n > 0 || role == TA_INPUTON) {
            /* the two cells date_cell skips */
            t[4] = '-';
            t[7] = '-';
        }
        for (n = 0; n < FORM_DATELEN && v[n] != '\0'; n++)
            t[date_cell(n)] = v[n];
        v = t;
        break;
    case FT_LOGIC:
        t[0] = v[0] == 'T' ? msg(MF_YN)[0] :
               (v[0] == 'F' ? msg(MF_YN)[1] : ' ');
        t[1] = '\0';
        v = t;
        break;
    case FT_MEMO:
        v = f->m.mode == FORM_NEW ? msg(MF_MEMONEW) :
            msg_sub(MF_MEMOLINES, v[0] != '\0' ? v : "0", NULL);
        break;
    case FT_SECRET:
        v = "";
        break;
    default:
        break;
    }
    tui_textn(vrow(f, i), vcol(f, i), v, disp_w(d), role);
}

void form_paint(form *f)
{
    const form_def *def;
    u16 i;

    def = f->m.def;
    tui_box(def->top, def->left, def->h, def->w, TB_SINGLE,
            f->hastitle ? f->title : msg(def->title));
    for (i = 0; i < def->nfields; i++) {
        tui_text(vrow(f, i), (u8)(def->left + 1 + fld(f, i)->col),
                 msg(fld(f, i)->label), TA_BOLD);
        draw_value(f, i, (u8)(f->run && i == f->cur ? TA_INPUTON : TA_INPUT));
    }
}

/* ---------------------------------------------------------------------
 * Editing.
 * --------------------------------------------------------------------- */
/* Edit date field i in its template: digits only, the hyphens
   skipped; the key that ended the edit.  With role TA_BOLD the date is
   shown selected until the first key, and a digit, Backspace or Del then
   clears it first, as tui_input does with TI_SELECT; role is
   TA_INPUTON otherwise. */
static u16 edit_date(form *f, u16 i, u8 role)
{
    char *v;
    u16 len;
    u16 pos;
    u16 k;

    v = form_val(f, i);
    len = (u16)strlen(v);
    pos = len;
    for (;;) {
        draw_value(f, i, role);
        vid_cursor(vrow(f, i), (u8)(vcol(f, i) + date_cell(pos)),
                   VID_CUR_LINE);
        k = tui_key();
        if (role == TA_BOLD &&
            ((k >= '0' && k <= '9') || k == KEY_BS || k == KEY_DEL)) {
            v[0] = '\0';
            len = 0;
            pos = 0;
        }
        role = TA_INPUTON;
        if (k >= '0' && k <= '9') {
            if (pos < len) {
                v[pos++] = (char)k;
            } else if (len < FORM_DATELEN) {
                v[len++] = (char)k;
                v[len] = '\0';
                pos = len;
            }
            if (pos >= FORM_DATELEN)
                pos = (u16)(len < FORM_DATELEN ? len : FORM_DATELEN - 1);
        } else if (k == KEY_LEFT) {
            if (pos > 0)
                pos--;
        } else if (k == KEY_RIGHT) {
            if (pos < len)
                pos++;
        } else if (k == KEY_HOME) {
            pos = 0;
        } else if (k == KEY_END) {
            pos = len;
        } else if (k == KEY_BS) {
            if (pos > 0) {
                memmove(v + pos - 1, v + pos, (size_t)(len - pos + 1));
                pos--;
                len--;
            }
        } else if (k == KEY_DEL) {
            if (pos < len) {
                memmove(v + pos, v + pos + 1, (size_t)(len - pos));
                len--;
            }
        } else if (k == KEY_CTRL('Y')) {
            v[0] = '\0';
            len = 0;
            pos = 0;
        } else if (k != ' ') {
            vid_cursor(0, 0, VID_CUR_HIDE);
            return k;
        }
    }
}

/* Choice i of field arg: its word, and when it has a name, the word
   padded to the field's width, a blank and the name. */
static void pick_label(void *arg, u16 i, char *buf)
{
    const form_field *d;
    u16 m;
    u16 n;

    d = (const form_field *)arg;
    txt_word_at(d->choices, i, buf, TUI_PICKW + 1);
    m = form_choice_name(d, i);
    if (m == 0)
        return;
    for (n = (u16)strlen(buf); n <= d->width && n < TUI_PICKW; n++)
        buf[n] = ' ';
    msg_get(m, buf + n, (u16)(TUI_PICKW + 1 - n));
}

/* F4 on a choice field: the list built from its choices. */
static void pick_choice(form *f, u16 i)
{
    const form_field *d;
    char *v;
    u16 sel;

    d = fld(f, i);
    v = form_val(f, i);
    sel = form_choice_num(d->choices, v);
    if (tui_pick(msg(d->label), NULL, form_choice_count(d->choices),
                 pick_label, (void *)d, &sel))
        txt_word_at(d->choices, sel, v, form_valsz(d));
}

/* F4 on a code field: the form's pick function chooses the value. */
static void pick_code(form *f, u16 i)
{
    char val[FORM_MAXVAL + 1];
    char *v;
    u16 size;

    v = form_val(f, i);
    size = form_valsz(fld(f, i));
    txt_copy(val, v, size);
    if (f->pick(f->arg, f, i, val))
        txt_copy(v, val, size);
}

/* Edit field i (editable or not); the key that ended the edit.  A
   typed value (not a choice or a logical) starts selected when the
   cursor has just come to the field. */
static u16 edit_field(form *f, u16 i)
{
    const form_field *d;
    char *v;
    u16 n;
    int at;
    u16 k;
    u8 fl;

    d = fld(f, i);
    v = form_val(f, i);
    fl = (u8)(i != f->was ? TI_SELECT : 0);
    f->was = (u8)i;
    if (!editable(f, i)) {
        draw_value(f, i, TA_INPUTON);
        return tui_key();
    }
    switch (d->type) {
    case FT_DATE:
        return edit_date(f, i,
                         (u8)(fl && v[0] != '\0' ? TA_BOLD : TA_INPUTON));
    case FT_LOGIC:
    case FT_CHOICE:
        for (;;) {
            draw_value(f, i, TA_INPUTON);
            k = tui_key();
            if (d->type == FT_LOGIC) {
                if (k >= 'a' && k <= 'z')
                    k = (u16)(k - 'a' + 'A');
                if (k == ' ')
                    strcpy(v, v[0] == 'T' ? "F" : "T");
                else if (k == (u16)(u8)msg(MF_YN)[0])
                    strcpy(v, "T");
                else if (k == (u16)(u8)msg(MF_YN)[1])
                    strcpy(v, "F");
                else
                    return k;
            } else if (k == ' ' && d->choices != NULL) {
                /* Past the last word an optional field (a filter, a
                   grade) goes blank, a required one back to the first
                   word. */
                at = form_choice_find(d->choices, v) + 1;
                if (at == (int)form_choice_count(d->choices) &&
                    (d->flags & FF_REQ))
                    at = 0;
                txt_word_at(d->choices, (u16)at, v, form_valsz(d));
            } else if (k == KEY_CTRL('Y') && !(d->flags & FF_REQ)) {
                v[0] = '\0';
            } else {
                return k;
            }
        }
    default:
        break;
    }
    if (d->flags & FF_UPPER)
        fl |= TI_UPPER;
    /* A password being set, wider than any password, takes every byte,
       so that its check sees a space or one character too many. */
    if (d->type == FT_SECRET)
        fl |= d->width > LINK_PWMAX ? TI_NOECHO : TI_NOECHO | TI_PRINT;
    /* Edited in its own slot (width + 1 bytes), so a password being
       typed is never copied: form_close wipes it, also when the end of
       a key script leaves through tui_exit in the middle of it. */
    k = tui_input(vrow(f, i), vcol(f, i), (u8)disp_w(d), v, d->width, fl);
    if (d->type != FT_SECRET) {
        n = (u16)strlen(v);
        while (n > 0 && v[n - 1] == ' ')
            v[--n] = '\0';
    }
    return k;
}

/* Edit field i into *k (edit_field); a code field whose value the
   typing changed then goes to the pick function with val NULL.  0 when
   that function stopped the form (form_stop). */
static int typed_code(form *f, u16 i, u16 *k)
{
    char was[FORM_MAXVAL + 1];
    int code;

    code = fld(f, i)->type == FT_CODE && f->pick != NULL && editable(f, i);
    if (code)
        strcpy(was, form_val(f, i));
    *k = edit_field(f, i);
    if (code && strcmp(was, form_val(f, i)) != 0)
        f->pick(f->arg, f, i, NULL);
    return !f->stop;
}

static void open_memo(form *f, u16 i)
{
    char num[TXT_U32LEN + 1];

    if (f->m.mode == FORM_NEW) {
        tui_msg(msg(MF_SAVEMEMO), 1);
        return;
    }
    if (f->memo == NULL)
        return;
    txt_u32(num, f->memo(f->arg, f, i));
    form_load(f, fld(f, i)->name, num);
}

u16 memo_field(form *f, u16 field, link *l, const char *table,
               const char *id, int readonly, const char *title)
{
    const char *v;
    u16 old;
    u16 lines;
    u8 cmd;

    v = form_val(f, field);
    old = msg_dec(&v, MSG_DECCAP);
    lines = 0;
    if (memo_edit(l, table, id, readonly, title, &lines) != RQ_OK &&
        lines == 0)
        lines = old;
    cmd = memo_leave();
    if (cmd != TUI_CMD_NONE)
        form_stop(f, cmd);
    return lines;
}

void form_noask(form *f)
{
    f->noask = 1;
}

void form_stop(form *f, u8 cmd)
{
    f->stop = 1;
    f->leave = cmd;
}

/* A click (key k) on the label or value of a field the cursor stops at
   (stop) puts the cursor there. */
static void click_field(form *f, u16 k)
{
    u16 i;
    u8 c;

    for (i = 0; i < f->m.def->nfields; i++) {
        c = (u8)(f->m.def->left + 1 + fld(f, i)->col);
        if (stop(f, i) &&
            tui_hit(k, vrow(f, i), c, 1,
                    (u8)(vcol(f, i) + disp_w(fld(f, i)) - c)) >= 0)
            f->cur = (u8)i;
    }
}

/* The field after field i, by steps of by (1 forward, nfields - 1
   back), that the cursor stops at (stop); i itself when there is none,
   0 in a form without fields. */
static u8 step(form *f, u8 i, u16 by)
{
    u16 nf;
    u16 j;

    nf = f->m.def->nfields;
    if (nf == 0)
        return 0;
    for (j = (u16)((i + by) % nf); j != i; j = (u16)((j + by) % nf))
        if (stop(f, j))
            return (u8)j;
    return i;
}

u16 form_run(form *f)
{
    const char *oldtopic;
    const form_field *d;
    u16 nf;
    u16 k;
    u16 mf;
    int bad;
    int glob;

    oldtopic = tui_topic(f->m.def->topic);
    nf = f->m.def->nfields;
    f->run = 1;
    f->was = FORM_MAXFIELDS;
    f->stop = 0;
    f->leave = TUI_CMD_NONE;
    for (;;) {
        form_paint(f);
        if (nf == 0) {
            k = tui_key();
        } else if (!typed_code(f, f->cur, &k)) {
            k = KEY_ESC;
            break;
        }
        d = nf > 0 ? fld(f, f->cur) : NULL;
        if (d != NULL && d->type == FT_MEMO && k == KEY_F4) {
            open_memo(f, f->cur);
            if (f->stop) {
                k = KEY_ESC;
                break;
            }
            continue;
        }
        if (d != NULL && d->type == FT_MEMO && k >= ' ' && k < KEY_EXT) {
            tui_msg(msg(MF_MEMOKEY), 1);
            continue;
        }
        switch (k) {
        case KEY_TAB:
        case KEY_DOWN:
            f->cur = step(f, f->cur, 1);
            continue;
        case KEY_STAB:
        case KEY_UP:
            f->cur = step(f, f->cur, (u16)(nf - 1));
            continue;
        case KEY_PGUP:
            f->cur = step(f, (u8)(nf - 1), 1);
            continue;
        case KEY_PGDN:
            f->cur = step(f, 0, (u16)(nf - 1));
            continue;
        case KEY_ENTER:
        case KEY_CTRL('S'):
            if (f->m.mode == FORM_VIEW)
                continue;
            k = KEY_ENTER;
            bad = form_check(f, &mf);
            if (bad < 0)
                break;
            f->cur = (u8)bad;
            tui_msg(msg(mf), 1);
            continue;
        case KEY_ESC:
            if (f->m.mode != FORM_VIEW && !f->noask && form_dirty(f) &&
                !tui_confirm(msg(MT_DISCARD)))
                continue;
            break;
        case KEY_CTRL('E'):
            if (f->m.mode == FORM_VIEW)
                break;
            continue;
        case KEY_F4:
            if (f->m.mode != FORM_VIEW && d != NULL && editable(f, f->cur)) {
                if (d->type == FT_CHOICE && d->choices != NULL)
                    pick_choice(f, f->cur);
                else if (d->type == FT_CODE && f->pick != NULL)
                    pick_code(f, f->cur);
            }
            if (f->stop) {
                k = KEY_ESC;
                break;
            }
            continue;
        default:
            glob = tui_isglobal(k);
            if (glob && f->m.mode != FORM_VIEW && !f->noask &&
                form_dirty(f)) {
                tui_msg(msg(MT_SAVEFIRST), 1);
                continue;
            }
            if (glob || (k >= TUI_VERBKEY && k <= TUI_VERBLAST))
                break;
            click_field(f, k);
            continue;
        }
        break;
    }
    f->run = 0;
    form_paint(f);
    tui_topic(oldtopic);
    return k;
}

u8 form_loop(form *f, const form_ops *ops, void *arg, u32 verbs)
{
    u16 k;
    u8 cmd;
    int used;
    int rc;

    verbs = tui_record(verbs);
    for (;;) {
        k = form_run(f);
        if (ops->key != NULL && (rc = ops->key(arg, f, k, &cmd)) >= 0) {
            if (rc == 0)
                break;
        } else if (k == KEY_ENTER) {
            if (ops->save(arg, f, &cmd) != 0)
                break;
        } else if (k == KEY_ESC) {
            cmd = f->leave;
            break;
        } else if (k == KEY_CTRL('E')) {
            if (tui_allow(TUI_LVL_OPER)) {
                form_mode_set(f, FORM_EDIT);
                to_edit(f);
                tui_hint(msg(ops->hint));
            }
        } else {
            cmd = tui_global(k, &used);
            if (used)
                break;
        }
    }
    tui_record(verbs);
    return cmd;
}

int form_store(form *f, link *l, const char *table, char *id, u16 idsz,
               const char *idfield, u16 title, rq_res *res, u8 *cmd)
{
    *cmd = TUI_CMD_NONE;
    if (form_save(f, l, table, id, idsz, res) == RQ_OK)
        return 1;
    if (f->m.partial) {
        if (idfield != NULL)
            form_load(f, idfield, id);
        form_set_title(f, msg_sub(title, id, NULL));
    }
    return form_fail(f, res, cmd);
}

void form_field_error(form *f, const char *name, const char *text)
{
    int i;

    i = form_find(f, name);
    if (i >= 0)
        f->cur = (u8)i;
    tui_msg(text, 1);
}

u8 form_error(form *f, const rq_res *res)
{
    rq_res r;
    int i;

    if (f->m.partial) {
        f->m.partial = 0;
        tui_msgbox(msg(MT_ERRTITLE), msg(MF_PARTIAL));
    }
    memcpy(&r, res, sizeof r);
    i = form_err_field(f, &r);
    if (i >= 0 && r.code == RQ_ENOTFOUND)
        r.code = RQ_EARG;
    if (i < 0 && r.code == RQ_EDUP)
        i = form_key(f);
    if (i >= 0)
        f->cur = (u8)i;
    return tui_fail(&r);
}

int form_fail(form *f, const rq_res *res, u8 *cmd)
{
    *cmd = form_error(f, res);
    return *cmd != TUI_CMD_NONE ||
           (res->code == RQ_ENOTFOUND && form_err_field(f, res) < 0) ? -1 : 0;
}

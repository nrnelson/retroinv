/*
 * SCRLOC.C - the location tree, the location form and the contents of a
 * location in the full-screen client (SCR.H, SCRSHR.H).
 *
 * The tree is the location tree's view (scr_lt of SCRPICK.C) over the work
 * area.  Enter shows the contents of the selected location through
 * scr_item_list, whose Record menu switches to the items not installed in
 * another and back, and to the items of every location below it and back;
 * Ins adds a location inside the selected one (at the top level in an empty
 * tree; refused at once on a row at the deepest level), Ctrl-E edits, the
 * Record menu's Move moves the location with everything in it into one
 * chosen from the pick list,
 * which opens on the location it is in and refuses it and those inside it,
 * or to the top level (the one it is in moves nothing, one at the deepest
 * level is refused); the form refuses an Inside naming the location or one
 * inside it with the pick list's words, and a new Inside at the deepest
 * level as Ins does.  Del deletes, asking by its full name (LOC GET); those
 * four need level 2.  A letter moves to the next location whose name starts
 * with it.  After an add, an edit or a move the tree is fetched again from
 * its start, as a change can move rows between pages, and the changed
 * location is sought; after a delete its rows are fetched again where they
 * were.
 *
 * As in SCRITEM.C, requests are built and sent only in small leaf
 * functions, and a form that closed after a save leaves a note the list
 * shows once it has redrawn itself.
 */
#define TUI_VERBS
#define SCRSHR_LOCF
#define SCRSHR_TREE
#define MSG_PATHS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "LISTV.H"
#include "FORM.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRVERB.H"

#define MO_HINT         552     /* hints of the tree                    */
#define MO_HINTEDIT     553     /* hints of the form                    */
#define MO_NOTES        554     /* Notes of location ^1                 */

#define LC_ROOM         "ROOM"  /* the type of a new top-level location */

static const char w_loc[] = "LOC";
static const char w_locget[] = "LOC GET";
static const char k_zero[] = "0";

/* The location form being shown. */
typedef struct {
    char id[LINK_IDLEN + 1];    /* LOC_ID, "" for a new one until saved */
    char parent[LINK_LOCLEN + 1];   /* PARENT_ID preset of a new one    */
    char orig[LINK_LOCLEN + 1]; /* Inside as the form was given it      */
    char type[LINK_LTYPELEN + 1];   /* the LTYPE a new one was given    */
    ctab *types;                /* the type names (scr_lt_types)        */
    u8   leave;                 /* a pick list ended the session        */
    u8   saved;                 /* a save succeeded                     */
    u8   inself;                /* Inside names the location itself or
                                   one inside it                        */
} lf_ctx;

/* ---------------------------------------------------------------------
 * Requests (leaf functions).
 * --------------------------------------------------------------------- */

/* Load location id into f, PARENT_ID 0 as blank and the path of the
   location it is inside, then the memo's line count, each request in a
   frame of its own. */
static int loc_get(form *f, const char *id, u8 *cmd)
{
    const char *par;

    if (!tui_req(w_locget, id, form_kline, f, NULL, cmd))
        return 0;
    par = form_get(f, scr_loc_fields[SCR_LF_PARENT].name);
    if (strcmp(par, k_zero) == 0)
        form_load(f, scr_loc_fields[SCR_LF_PARENT].name, "");
    return scr_loc_show(f, &scr_loc_inside,
                        form_get(f, scr_loc_fields[SCR_LF_PARENT].name), 1,
                        cmd) &&
           memo_load_count(f, scr_loc_fields[SCR_LF_NOTES].name, tui_s.l,
                           w_loc, id, cmd);
}

/* The type of the first location inside location id into type
   (LINK_LTYPELEN + 1 bytes), left as it is when id holds none: the first
   row of LOC LIST under it.  1, or 0 with the failure reported and *cmd
   the command to leave with. */
static int child_type(const char *id, char *type, u8 *cmd)
{
    char t[LINK_LTYPELEN + 1];
    rq_res res;

    *cmd = TUI_CMD_NONE;
    if (scr_loc_first(tui_s.l, id, t, &res) != RQ_OK) {
        *cmd = tui_fail(&res);
        return 0;
    }
    if (t[0] != '\0')
        strcpy(type, t);
    return 1;
}

/* 1 when a new Inside of f, a location the form knows (its path shown),
   is not at the deepest level, by its LOC GET; else 0, the field told
   as the tree's Ins tells it, or *cmd the command a failed request asks
   to leave with. */
static int room_inside(form *f, lf_ctx *c, u8 *cmd)
{
    scr_locinfo x;
    const char *par;

    par = form_get(f, scr_loc_fields[SCR_LF_PARENT].name);
    if (par[0] == '\0' || strcmp(par, c->orig) == 0 ||
        form_get(f, scr_loc_fields[SCR_LF_WHERE].name)[0] == '\0')
        return 1;
    if (!scr_loc_get(par, &x, cmd))
        return 0;
    if (x.depth < LINK_MAXLOCDEPTH)
        return 1;
    form_field_error(f, scr_loc_fields[SCR_LF_PARENT].name,
                     msg_sub(MO_TOODEEP, x.name, NULL));
    return 0;
}

/* Save f (form_ops), with the note naming the location.  A PARENT_ID
   preset for a new location was loaded as its original value, so that
   the untouched form is not changed; it is made a change here, for the
   ADD to carry it. */
static int loc_save(void *arg, form *f, u8 *cmd)
{
    lf_ctx *c;
    const char *par;
    rq_res res;
    int isnew;
    int rc;

    c = (lf_ctx *)arg;
    *cmd = TUI_CMD_NONE;
    if (c->inself) {
        form_field_error(f, scr_loc_fields[SCR_LF_PARENT].name,
                         msg(MO_INSELF));
        return 0;
    }
    if (!room_inside(f, c, cmd))
        return *cmd != TUI_CMD_NONE ? -1 : 0;
    isnew = form_mode_get(f) == FORM_NEW;
    par = form_get(f, scr_loc_fields[SCR_LF_PARENT].name);
    if (isnew && c->parent[0] != '\0' && strcmp(par, c->parent) == 0) {
        form_load(f, scr_loc_fields[SCR_LF_PARENT].name, "");
        form_put(f, scr_loc_fields[SCR_LF_PARENT].name, c->parent);
    }
    rc = form_store(f, tui_s.l, w_loc, c->id, (u16)sizeof c->id, NULL,
                    MO_FTITLE, &res, cmd);
    if (rc > 0) {
        c->saved = 1;
        lv_note((u16)(isnew ? MO_ADDED : MO_SAVED), NULL,
                form_get(f, scr_loc_fields[SCR_LF_NAME].name));
    }
    return rc;
}

/* LOC SET id PARENT_ID=to: 1 when moved, else 0 with the refusal
   reported and *cmd the command to leave with. */
static int loc_move(const char *id, const char *to, u8 *cmd)
{
    rq q;
    rq_res res;

    *cmd = TUI_CMD_NONE;
    rq_begin(&q, "LOC SET");
    rq_word(&q, id);
    rq_pair(&q, scr_loc_fields[SCR_LF_PARENT].name, to);
    if (rq_send(tui_s.l, &q, NULL, NULL, &res) == RQ_OK)
        return 1;
    *cmd = tui_fail(&res);
    return 0;
}

/* ---------------------------------------------------------------------
 * The location form.
 * --------------------------------------------------------------------- */

/* The name of type code beside the type in f. */
static void type_show(form *f, lf_ctx *c, const char *code)
{
    char name[LINK_CODENAMELEN + 1];

    name[0] = '\0';
    if (code[0] != '\0')
        ct_name(c->types, code, name);
    form_load(f, scr_loc_fields[SCR_LF_TNAME].name, name);
}

/* In a new form f of c whose type is still the one it was given: the
   type for a location inside par (blank: the top level), as loc_new
   chooses it, unless par names no location (its path shows nothing). */
static void type_again(form *f, lf_ctx *c, const char *par)
{
    const char *tf;
    u8 cmd;

    tf = scr_loc_fields[SCR_LF_TYPE].name;
    if (form_mode_get(f) != FORM_NEW ||
        strcmp(form_get(f, tf), c->type) != 0 ||
        form_get(f, scr_loc_fields[SCR_LF_WHERE].name)[0] == '\0')
        return;
    strcpy(c->type, par[0] == '\0' ? LC_ROOM : "");
    if (par[0] != '\0' && !child_type(par, c->type, &cmd)) {
        if (cmd != TUI_CMD_NONE) {
            c->leave = cmd;
            form_stop(f, cmd);
        }
        return;
    }
    form_load(f, tf, c->type);
    type_show(f, c, c->type);
}

/* In the form f of location c->id, a container typed: c->inself set,
   and said on the hint row, when it is the location or one inside it, as
   the pick list refuses them. */
static void inside_check(form *f, lf_ctx *c)
{
    const char *par;
    u8 cmd;

    cmd = TUI_CMD_NONE;
    par = form_get(f, scr_loc_fields[SCR_LF_PARENT].name);
    c->inself = (u8)(c->id[0] != '\0' && par[0] != '\0' &&
                     form_get(f, scr_loc_fields[SCR_LF_WHERE].name)[0] !=
                     '\0' && scr_loc_within(par, c->id, &cmd));
    if (c->inself)
        form_field_error(f, scr_loc_fields[SCR_LF_PARENT].name,
                         msg(MO_INSELF));
    else if (cmd != TUI_CMD_NONE) {
        c->leave = cmd;
        form_stop(f, cmd);
    }
}

/* F4 on the type (its pick list) or on the container, a type or a
   container typed (form_pick_fn): the type's name, or the container's
   path and, in a new form, its type. */
static int loc_pick(void *arg, form *f, u16 field, char *val)
{
    lf_ctx *c;
    int ok;

    c = (lf_ctx *)arg;
    if (field == SCR_LF_TYPE) {
        ok = scr_pick_field(f, SCR_CK_LTYPE, val, MO_HINTEDIT, &c->leave);
        type_show(f, c, ok ? val : form_get(f, scr_loc_fields[field].name));
        return ok;
    }
    ok = scr_loc_field(f, &scr_loc_inside, c->id[0] != '\0' ? c->id : NULL,
                       val, MO_HINTEDIT, &c->leave);
    if (c->leave == TUI_CMD_NONE && (val == NULL || ok))
        type_again(f, c, ok ? val : form_get(f, scr_loc_fields[field].name));
    if (val == NULL)
        inside_check(f, c);
    else if (ok)
        c->inself = 0;
    return ok;
}

/* The notes (form_memo_fn); the tree opens loc_form only at level 2,
   so they can always be edited. */
static u16 loc_memo(void *arg, form *f, u16 field)
{
    u16 lines;

    lines = memo_field(f, field, tui_s.l, w_loc, ((lf_ctx *)arg)->id, 0,
                       msg_sub(MO_NOTES,
                               form_get(f, scr_loc_fields[SCR_LF_NAME].name),
                               NULL));
    tui_hint_keep(msg(MO_HINTEDIT));
    return lines;
}

static const form_ops loc_ops = { loc_save, NULL, MO_HINTEDIT };

/* The values a new form f of c starts with, as unchanged ones: inside
   location c->parent (its code when it has one, which c->parent then
   holds; the top level when it is ""), the type of the first location
   in it, else none, or for the top level LC_ROOM. */
static int loc_new(form *f, lf_ctx *c, u8 *cmd)
{
    const char *par;

    par = scr_loc_fields[SCR_LF_PARENT].name;
    strcpy(c->type, c->parent[0] == '\0' ? LC_ROOM : "");
    if (c->parent[0] != '\0' && !child_type(c->parent, c->type, cmd))
        return 0;
    form_load(f, scr_loc_fields[SCR_LF_TYPE].name, c->type);
    type_show(f, c, c->type);
    form_load(f, par, c->parent);
    if (!scr_loc_show(f, &scr_loc_inside, c->parent, 1, cmd))
        return 0;
    txt_copy(c->parent, form_get(f, par), (u16)sizeof c->parent);
    strcpy(c->orig, c->parent);
    return 1;
}

/* The form for location id, or a new one inside location in when id is
   NULL (at the top level when in is NULL too); the callers have checked
   level 2.  The ID of the location goes into saved (LINK_IDLEN + 1
   bytes) once a save has succeeded, else "".  The command to return. */
static u8 loc_form(const char *id, const char *in, char *saved)
{
    lf_ctx c;
    form *f;
    u8 cmd;
    int ok;

    txt_copy(c.id, id, (u16)sizeof c.id);
    txt_copy(c.parent, in, (u16)sizeof c.parent);
    c.type[0] = '\0';
    c.orig[0] = '\0';
    c.leave = TUI_CMD_NONE;
    c.saved = 0;
    c.inself = 0;
    saved[0] = '\0';
    c.types = scr_lt_types(&cmd);
    if (c.types == NULL)
        return cmd;
    f = form_open(&scr_loc_def, (u8)(id != NULL ? FORM_EDIT : FORM_NEW),
                  loc_pick, loc_memo, &c);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        scr_lt_types_done();
        return TUI_CMD_NONE;
    }
    if (id != NULL) {
        ok = loc_get(f, c.id, &cmd);
        if (ok) {
            txt_copy(c.orig, form_get(f, scr_loc_fields[SCR_LF_PARENT].name),
                     (u16)sizeof c.orig);
            form_set_title(f, msg_sub(MO_FTITLE,
                form_get(f, scr_loc_fields[SCR_LF_NAME].name), NULL));
            type_show(f, &c, form_get(f, scr_loc_fields[SCR_LF_TYPE].name));
        }
    } else {
        ok = loc_new(f, &c, &cmd);
    }
    if (ok) {
        tui_hint(msg(MO_HINTEDIT));
        cmd = form_loop(f, &loc_ops, &c, SV_BIT(SV_BACK));
        if (c.saved)
            strcpy(saved, c.id);
    }
    form_close(f);
    scr_lt_types_done();
    return cmd;
}

/* ---------------------------------------------------------------------
 * Contents.
 * --------------------------------------------------------------------- */
static void contents_args(void *arg, rq *q)
{
    rq_word(q, (const char *)arg);
}

u8 scr_loc_contents(const char *id)
{
    char t[SCR_IL_TITLEW + 1];
    scr_locinfo i;
    char first[LINK_LTYPELEN + 1];
    u8 cmd;

    memset(&i, 0, sizeof i);
    first[0] = '\0';
    if (!tui_req(w_locget, id, scr_loc_info, &i, NULL, &cmd) ||
        !child_type(i.id, first, &cmd))
        return cmd;
    msg_path(t, i.path, SCR_IL_TITLEW);
    return scr_item_list("CONTENTS", "LOC CONTENTS", contents_args, i.id, t,
                         MO_NOITEMS, i.id, MO_SHOW,
                         (u8)(first[0] != '\0'));
}

/* ---------------------------------------------------------------------
 * The tree.
 * --------------------------------------------------------------------- */
/* Draw the frame and the hints (kept under a message still up when keep
   is 1). */
static void lc_draw(int keep)
{
    tui_box(TUI_TOP, 0, TUI_WORKROWS, VID_COLS, TB_SINGLE, msg(MO_TITLE));
    if (keep)
        tui_hint_keep(msg(MO_HINT));
    else
        tui_hint(msg(MO_HINT));
    tui_info("");
}

/* 1 when the row is location arg, for lv_seek.  Every row has text, so
   its test never fails; it keeps the parameter, which lv_match_fn
   requires, referenced. */
static int is_loc(void *arg, const char *key, const char *text)
{
    return text[0] != '\0' && strcmp(key, (const char *)arg) == 0;
}

/* After another screen or a change, which left with cmd: draw the tree
   again (leaving a message on the hint row until the next key), then
   fetch its rows again where they were and show the pending note
   (lv_reload); or, when id is not "", fetch them from the start, seek
   location id and show the note, as a change may have moved rows between
   pages. */
static u8 lc_back(scr_lt *c, u8 cmd, const char *id)
{
    if (cmd != TUI_CMD_NONE)
        return cmd;
    lc_draw(1);
    if (id[0] == '\0') {
        lv_reload(c->v);
    } else if (lv_home(c->v) == RQ_OK) {
        lv_seek(c->v, is_loc, (void *)id, LV_SEEKMAX);
        lv_note_show();
    }
    return lv_leave(c->v);
}

/* Del on location key: delete after asking, by its full name. */
static u8 lc_del(scr_lt *c, const char *key)
{
    scr_locinfo x;
    u8 cmd;

    if (!scr_loc_get(key, &x, &cmd))
        return lc_back(c, cmd, "");
    if (tui_confirm(msg_sub(MO_DELQ, x.name, NULL)) &&
        scr_del("LOC DEL", key, 0, &cmd)) {
        lv_note(MO_DELETED, NULL, x.name);
        cmd = lc_back(c, cmd, "");
    }
    return cmd;
}

/* Move on location key: move it into one from the pick list, which opens
   on the location it is in and refuses key and those inside it, or to
   the top level; choosing the one it is in sends nothing, and one at
   the deepest level is refused as Ins refuses it. */
static u8 lc_move(scr_lt *c, const char *key)
{
    scr_locinfo x;
    scr_locinfo to;
    u8 cmd;

    if (!scr_loc_get(key, &x, &cmd))
        return lc_back(c, cmd, "");
    if (!scr_pick_loc(MO_MOVETO, MO_TOP, x.parent, key, &to)) {
        cmd = scr_pickleave;
    } else if (strcmp(to.id, x.parent) == 0) {
        cmd = TUI_CMD_NONE;
    } else if (to.depth >= LINK_MAXLOCDEPTH) {
        cmd = TUI_CMD_NONE;
        tui_msg(msg_sub(MO_TOODEEP, to.name, NULL), 1);
    } else if (loc_move(key, to.id, &cmd)) {
        lv_note(MO_MOVED, NULL, x.name);
        return lc_back(c, TUI_CMD_NONE, key);
    }
    lc_draw(1);
    return cmd;
}

/* Ins on location key, of depth depth (NULL: the tree is empty, a new
   location at the top level), and Ctrl-E (edit 1): the location form, its
   location sought afterwards; Ins on a row at the deepest level is
   refused at once, naming the location in full (LOC GET). */
static u8 lc_form(scr_lt *c, const char *key, u16 depth, int edit)
{
    char id[LINK_IDLEN + 1];
    scr_locinfo x;
    u8 cmd;

    if (!tui_allow(TUI_LVL_OPER))
        return TUI_CMD_NONE;
    if (!edit && key != NULL && depth >= LINK_MAXLOCDEPTH) {
        if (scr_loc_get(key, &x, &cmd))
            tui_msg(msg_sub(MO_TOODEEP, x.name, NULL), 1);
        return cmd;
    }
    return lc_back(c, edit ? loc_form(key, NULL, id) :
                             loc_form(NULL, key, id), id);
}

static u8 lc_run(scr_lt *c)
{
    char key[LV_KEYSZ];
    scr_locinfo x;
    u16 k;
    u8 cmd;
    int sel;
    int used;

    lc_draw(0);
    lv_home(c->v);
    cmd = lv_leave(c->v);
    while (cmd == TUI_CMD_NONE) {
        k = lv_run(c->v);
        cmd = lv_leave(c->v);
        if (cmd != TUI_CMD_NONE)
            break;
        sel = lv_selected(c->v, key);
        if (sel)
            scr_lt_selrow(c, key, &x);
        switch (k) {
        case KEY_ESC:
            return TUI_CMD_NONE;
        case KEY_ENTER:
            if (sel)
                cmd = lc_back(c, scr_loc_contents(key), "");
            break;
        case KEY_INS:
            cmd = lc_form(c, sel ? key : NULL, sel ? x.depth : 0, 0);
            break;
        case KEY_CTRL('E'):
            if (sel)
                cmd = lc_form(c, key, x.depth, 1);
            break;
        case SK(SV_MOVE):
            if (sel && tui_allow(TUI_LVL_OPER))
                cmd = lc_move(c, key);
            break;
        case KEY_DEL:
            if (sel && tui_allow(TUI_LVL_OPER))
                cmd = lc_del(c, key);
            break;
        default:
            if (scr_lt_letter(c, k)) {
                cmd = lv_leave(c->v);
                break;
            }
            cmd = tui_global(k, &used);
            if (used)
                return cmd;
            cmd = TUI_CMD_NONE;
            break;
        }
    }
    return cmd;
}

#define LC_VERBS        (SV_BIT(SV_OPEN) | SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | \
                         SV_BIT(SV_DEL) | SV_BIT(SV_MOVE) | SV_BIT(SV_BACK))

u8 scr_locs(u8 cmd)
{
    char id[LINK_IDLEN + 1];
    const char *oldtopic;
    u32 oldverbs;
    scr_lt c;

    lv_note(0, NULL, NULL);
    if (cmd == SCMD_LOC_NEW)
        return lv_note_end(loc_form(NULL, NULL, id));
    oldtopic = tui_topic("LOCS");
    if (scr_lt_open(&c, TUI_TOP + 1, 1, TUI_WORKROWS - 2, VID_COLS - 2,
                    &cmd)) {
        oldverbs = tui_record(LC_VERBS);
        cmd = lc_run(&c);
        tui_record(oldverbs);
        scr_lt_close(&c);
    }
    tui_info("");
    tui_topic(oldtopic);
    return cmd;
}

/*
 * WFORM.C - the form dialog of the Windows programs (WFORM.H).
 *
 * The template is written twice by build: first only counted, to size
 * the block, then into it.  Before the first, measure makes a dialog of
 * the same font, unseen, to measure the labels, the widest values of a
 * fixed format and the tab strip in it, which the layout then gives
 * their room.  A Windows 3.0 dialog template is packed bytes: its
 * style, the count of controls, its box, an empty menu and class name,
 * the caption and, with DS_SETFONT, the point size and face of the
 * font; then for each control its box, ID, style, a byte naming its
 * predefined class, its text and a zero byte of creation data.
 *
 * The dialog's state (wf_dlg) is on wf_run's stack, and wf_cur points
 * to it while the dialog runs, for the dialog procedure, the message
 * filter and wf_wipe.  Edit switches a dialog to FORM_EDIT by ending it
 * with WF_EDIT, so that wf_run shows the form again in a dialog built
 * for that mode, its memo as loaded: so the controls, the buttons and
 * the focus are each set once, by build and init.
 */
#define FMODEL_CALLS
#define RQERR_CLASS
#define MSG_PATHS
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "TUI.H"
#include "FMODEL.H"
#include "FORM.H"
#include "MEMOIO.H"
#include "CODETAB.H"
#include "INTL.H"
#include "SCRLFORM.H"
#include "WDLG.H"
#include "WEDIT.H"
#include "WPICK.H"
#include "WTAB.H"
#include "WFORM.H"
#include "WROWS.H"

/* A field's Windows label, WF_S_LABEL plus its message number, is above
   every other string of the program. */
#if WF_S_LABEL < MSG_MAXNUM
#error WF_S_LABEL must be at least MSG_MAXNUM
#endif

/* The layout beside WFORM.H's and WDLG.H's, in dialog units. */
#define WF_EDITH        11      /* a control of one line                */
#define WF_LINEH        8       /* a line of text                       */
#define WF_PAD          6       /* an edit control's border and margins */
#define WF_DROPW        12      /* a drop-down list's button, a code
                                   field's pick button                  */
#define WF_DROPROWS     6       /* the rows its list shows              */
#define WF_ROWGAP       2       /* blanks between a row's code and name */
#define WF_CHECKW       10      /* a check box                          */
#define WF_LGAP         4       /* between a label and its control      */
#define WF_FGAP         8       /* between a control and the next label
                                   on its row                           */
#define WF_SHARECELLS   12      /* the cells an edit control keeps when
                                   its row is short of room             */
#define WF_MEMOLINES    3       /* the least lines the memo shows       */
#define WF_UNITSMAX     255     /* units for that many or more          */
#define WF_LSLACK       2       /* pixels a label's static needs beside
                                   its text (text_w): Windows 3.0 wraps
                                   a text that fills its static exactly,
                                   and each end of a box rounds down    */
#define WF_WRAPSLACK    2       /* characters an edit control of Windows
                                   3.0 keeps free after a line's last
                                   word, else it moves the word down    */
#define WF_MSGH         16      /* the message line: two lines          */
#define WF_MSGMINW      100     /* its least width                      */
#define WF_FONTPT       8

/* The commands the message filter posts for CTRL+E, which the Edit
   button gives too, and for the location field and the code field before the
   name field, for what they show as they lose the focus: no field's
   IDs. */
#define WF_EDITID       (WF_MSGID - 1)
#define WF_TYPEDID      (WF_MSGID - 3)

/* The buttons' IDs fit build's table of bytes. */
extern char wf_ids_fit[1];
extern char wf_ids_fit[WF_EDITID <= 0xFF && WF_HELPID <= 0xFF ? 1 : 2];

/* The code lists a dialog fetches, by kind (SCR_CK_CAT, SCR_CK_LTYPE),
   and its rows' text: a code or word in its field's cells, at most
   LINK_LTYPELEN, the gap and a name. */
#define WF_NLISTS       (SCR_CK_LTYPE + 1)
#define WF_ROWLEN       (LINK_LTYPELEN + WF_ROWGAP + LINK_CODENAMELEN)

extern char wf_row_fits[1];
extern char wf_row_fits[WF_ROWLEN <= FORM_MAXVAL ? 1 : 2];

/* How where finds the location field's value: loaded with the record,
   chosen from the pick list, or typed. */
#define WF_W_LOADED     0
#define WF_W_PICKED     1
#define WF_W_TYPED      2
#define WF_W_NONE       3       /* where's result: no such location     */

/* wf_dlg's mm before the memo is loaded, and after its load failed,
   which leaves it empty and read-only; no message has these numbers. */
#define WF_M_NEW        0xFFFFu
#define WF_M_FAIL       0xFFFEu

/* Template bytes: a control's box, ID, style and class byte, which the
   predefined classes have. */
#define WF_ITEMSZ       15
#define WF_C_BUTTON     0x80
#define WF_C_EDIT       0x81
#define WF_C_STATIC     0x82
#define WF_C_COMBO      0x85
#define WF_CHILD        (WS_CHILD | WS_VISIBLE)
#define WF_LABEL        (WF_CHILD | SS_LEFT)

/* A set of access letters: a flag for each digit 0 to 9, then for each
   letter A to Z, either case (key). */
#define WF_KEYS         (10 + 26)

static const char k_font[] = "Helv";
static const char k_empty[] = "";
static const char k_lbox[] = "ComboLBox";
static const char k_zero[] = "0";
static const char k_tag[] = "TAG";

/* The bytes of a record of the template: the header, the longest, takes
   its style, count, box, menu and class (WF_ITEMSZ in all, as a
   control's fields), a caption, the point size and the face. */
#define WF_RECSZ        (WF_ITEMSZ + MSG_MAXLEN + 1 + 2 + sizeof k_font)

struct form {
    form_model m;
    u8   sent;                  /* OK sent a request (wf_sent)          */
};

typedef struct {
    form *f;
    const wf_ops *ops;
    void *arg;
    link *l;
    char *id;
    char *text;                 /* the memo's text, WF_MEMOSZ bytes     */
    HINSTANCE inst;
    HWND dlg;                   /* NULL until WM_INITDIALOG             */
    mio  memo;
    int  mi;                    /* the FT_MEMO field, -1 for none shown:
                                   then memo and text are not held      */
    u16  idsz;
    u8   level;
    u16  mm;                    /* mio_check's message on the memo
                                   loaded (MM_ or 0), or WF_M_          */
    int  li;                    /* the location field (ops->loc) and its
                                   path's, or -1 without one or a link  */
    int  pi;
    int  ni;                    /* the name field (ops->codes) and its
                                   code field, or -1 without one        */
    int  ci;
    char shown[LINK_LOCLEN + 1];        /* the value whose path shows   */
    char locid[LINK_IDLEN + 1];         /* and its LOC_ID, or ""        */
    ctab lists[WF_NLISTS];      /* the code lists, until the dialog has
                                   filled its controls                  */
    u8   have;                  /* a bit for each kind of lists fetched */
    int  dropsel;               /* the open drop-down list's row when it
                                   dropped                              */
    HWND strip;                 /* the tab strip, or NULL               */
    u8   npages;                /* the pages, 1 without a strip         */
    u8   page;                  /* the page shown                       */
    u8   top;                   /* 1 with an identity row               */
    u8   striph;                /* the strip's height                   */
    int  vx;                    /* the value column of the pages' rows  */
    int  stripy;                /* the strip's top                      */
    int  pagey;                 /* the pages' top and height            */
    int  pageh;
    u32  bare;                  /* a bit for each drop-down list whose
                                   rows show only their codes           */
    u32  cont;                  /* a bit for each field that starts a
                                   line carrying its row on (spill)      */
    u32  plain;                 /* a bit for each field whose value is
                                   text (as_text)                       */
    u8   at[FORM_MAXFIELDS];    /* each field's row on its page         */
    u8   lw[FORM_MAXFIELDS];    /* each label's width, measured         */
    u8   vw[FORM_MAXFIELDS];    /* each fixed-format value's widest
                                   text, measured, or 0 (fixed_w)       */
    u8   tw[FORM_MAXFIELDS];    /* in FORM_VIEW each free text's value,
                                   measured, else 0                     */
} wf_dlg;

typedef struct {
    int  x;
    int  y;
    int  cx;
    int  cy;
} wf_box;

typedef struct {
    HGLOBAL h;                  /* the block, or NULL while counting    */
    u16  n;                     /* bytes written                        */
} wf_tpl;

static wf_dlg *wf_cur;          /* the dialog running, or NULL          */
static HHOOK wf_next;           /* the message filter before wf_filter  */
static void (*wf_show)(HWND owner, const char *topic);  /* wf_help's   */

static const form_field *fld(wf_dlg *c, u16 i)
{
    return &c->f->m.def->fields[i];
}

static HWND ctl(wf_dlg *c, u16 i)
{
    return GetDlgItem(c->dlg, WF_ID0 + i);
}

/* The index of the field whose control is w, or -1. */
static int field_of(wf_dlg *c, HWND w)
{
    UINT i;

    i = (UINT)GetDlgCtrlID(w) - WF_ID0;
    return i < c->f->m.def->nfields ? (int)i : -1;
}

/* 1 when field i can be changed (WFORM.H). */
static int editable(wf_dlg *c, u16 i)
{
    const form_field *d;
    u8 mode;

    d = fld(c, i);
    mode = c->f->m.mode;
    if (mode == FORM_VIEW || (d->flags & FF_RO) ||
        ((d->flags & FF_NEWONLY) && mode != FORM_NEW))
        return 0;
    return d->type != FT_MEMO ||
           ((int)i == c->mi && c->ops->memo_save != NULL);
}

/* Field i's pick list (WFORM.H), WP_ of WPICK.H, or -1 for none. */
static int list_of(wf_dlg *c, u16 i)
{
    const char *n;

    n = fld(c, i)->name;
    return (int)i == c->li ? WP_LOCS :
           strncmp(n, wp_tables[SCR_CK_CAT],
                   strlen(wp_tables[SCR_CK_CAT])) == 0 ? SCR_CK_CAT :
           strcmp(n, wp_tables[SCR_CK_LTYPE]) == 0 ? SCR_CK_LTYPE :
           strncmp(n, k_tag, sizeof k_tag - 1) == 0 ? SCR_CK_TAG : -1;
}

/* The kind of field i's code list (WF_NLISTS), or -1 when it has none:
   an FT_CODE field of the categories or the location types. */
static int list_kind(wf_dlg *c, u16 i)
{
    int k;

    k = list_of(c, i);
    return fld(c, i)->type == FT_CODE && k >= 0 && k < WF_NLISTS ? k : -1;
}

/* 1 when field i has rows (WFORM.H): an FT_CHOICE field, or a code
   field whose list the dialog fetched. */
static int has_rows(wf_dlg *c, u16 i)
{
    int k;

    k = list_kind(c, i);
    return fld(c, i)->type == FT_CHOICE || (k >= 0 && (c->have >> k & 1));
}

/* The type field i is shown as (WFORM.H): FT_CHOICE, a drop-down list,
   for a field with rows that can be changed, FT_CODE, an edit control,
   for one that cannot, else the field's own. */
static u8 shown_as(wf_dlg *c, u16 i)
{
    return !has_rows(c, i) ? fld(c, i)->type :
           editable(c, i) ? FT_CHOICE : FT_CODE;
}

/* 1 when field i shows its value in a static control (texts). */
static int as_text(wf_dlg *c, u16 i)
{
    return (int)(c->plain >> i & 1);
}

/* 1 when field i is shown in an edit control. */
static int is_edit(wf_dlg *c, u16 i)
{
    return fld(c, i)->type != FT_LOGIC && shown_as(c, i) != FT_CHOICE &&
           !as_text(c, i);
}

/* 1 when field i's control, if i cannot be changed, is a read-only edit
   control, which takes the focus, rather than a disabled one. */
static int ro_edit(wf_dlg *c, u16 i)
{
    return is_edit(c, i) && fld(c, i)->type != FT_SECRET;
}

/* 1 when field i has a pick list (WFORM.H). */
static int pickable(wf_dlg *c, u16 i)
{
    return shown_as(c, i) == FT_CODE && editable(c, i) && c->l != NULL &&
           (list_of(c, i) >= 0 || c->ops->pick != NULL);
}

/* The number of field i's rows (has_rows), its blank row left out: none
   once the dialog has freed its code lists. */
static u16 rows(wf_dlg *c, u16 i)
{
    const form_field *d;

    d = fld(c, i);
    return d->type == FT_CHOICE ? form_choice_count(d->choices) :
           c->lists[list_kind(c, i)].n;
}

/* Row k of field i: its code or word into code (FORM_MAXVAL + 1 bytes)
   and its text into t (FORM_MAXVAL + 1 bytes), CP437: the code or word,
   in the field's cells (LINK_LTYPELEN at most), WF_ROWGAP blanks and its
   name, a choice's from form_choice_name; the code or word alone when
   it has no name or its drop-down list shows codes alone (bare). */
static void row(wf_dlg *c, u16 i, u16 k, char *code, char *t)
{
    char name[LINK_CODENAMELEN + 1];
    const form_field *d;
    ct_row r;
    u16 w;

    d = fld(c, i);
    name[0] = '\0';
    if (d->type != FT_CHOICE) {
        ct_get(&c->lists[list_kind(c, i)], k, &r);
        strcpy(code, r.code);
        strcpy(name, r.name);
    } else {
        form_choice_word(d->choices, k, code, FORM_MAXVAL + 1);
        if (d->dec != 0)
            msg_get(form_choice_name(d, k), name, (u16)sizeof name);
    }
    if (c->bare >> i & 1)
        name[0] = '\0';
    if (name[0] == '\0') {
        strcpy(t, code);
        return;
    }
    w = d->width < LINK_LTYPELEN ? d->width : LINK_LTYPELEN;
    memset(t, ' ', w + WF_ROWGAP);
    memcpy(t, code, strlen(code));
    strcpy(t + w + WF_ROWGAP, name);
}

/* 1 with the text of field i's row holding the value v in t
   (FORM_MAXVAL + 1 bytes), else 0. */
static int row_of(wf_dlg *c, u16 i, const char *v, char *t)
{
    char code[FORM_MAXVAL + 1];
    u16 k;

    for (k = 0; k < rows(c, i); k++) {
        row(c, i, k, code, t);
        if (strcmp(code, v) == 0)
            return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * The template.
 * --------------------------------------------------------------------- */

/* 1 when field d has no label of its own (WFORM.H). */
static int unlabelled(const form_field *d)
{
    return d->col == d->vcol;
}

/* 1 when field d only shows a value beside another (WFORM.H): local,
   read-only and without a label of its own. */
static int shows(const form_field *d)
{
    return (d->flags & (FF_LOCAL | FF_RO)) == (FF_LOCAL | FF_RO) &&
           unlabelled(d);
}

/* The field of def nearest to the left of field i on its row, or -1. */
static int left_of(const form_def *def, u16 i)
{
    const form_field *d;
    u16 k;
    int at;

    d = &def->fields[i];
    at = -1;
    for (k = 0; k < def->nfields; k++)
        if (def->fields[k].row == d->row && def->fields[k].col < d->col &&
            (at < 0 || def->fields[k].col > def->fields[at].col))
            at = (int)k;
    return at;
}

/* The code field that field i of def shows the name of (WFORM.H's
   name field, when the form has ops->codes), or -1. */
static int code_of(const form_def *def, u16 i)
{
    int k;

    k = left_of(def, i);
    return shows(&def->fields[i]) && k >= 0 &&
           def->fields[k].type == FT_CODE ? k : -1;
}

/* 1 when field i is a name field left hidden, as its code field has
   rows, which give the name (WFORM.H). */
static int hidden(wf_dlg *c, u16 i)
{
    int k;

    k = code_of(c->f->m.def, i);
    return c->ops->codes != NULL && k >= 0 && has_rows(c, (u16)k);
}

/* The characters of field i's longest row. */
static u16 widest(wf_dlg *c, u16 i)
{
    char code[FORM_MAXVAL + 1];
    char t[FORM_MAXVAL + 1];
    u16 n;
    u16 k;

    n = 0;
    for (k = 0; k < rows(c, i); k++) {
        row(c, i, k, code, t);
        if (strlen(t) > n)
            n = (u16)strlen(t);
    }
    return n;
}

/* The page of field i (wf_pages), WF_PG_TOP on the identity row; with
   no page table, 1 for the memo of a form of two pages, else 0. */
static u8 page_of(wf_dlg *c, u16 i)
{
    if (c->ops->pages != NULL)
        return c->ops->pages->page[i];
    return (u8)(c->npages > 1 && fld(c, i)->type == FT_MEMO);
}

/* 1 when field i is shown: on the identity row or the page shown, and
   not a name field left hidden. */
static int on_show(wf_dlg *c, u16 i)
{
    u8 p;

    p = page_of(c, i);
    return (p == WF_PG_TOP || p == c->page) && !hidden(c, i);
}

/* The fields of field i's row, on its page or the identity row, into
   idx in their order on it, by their columns, then the table's; their
   number. */
static int peers(wf_dlg *c, u16 i, u8 *idx)
{
    const form_field *d;
    u16 k;
    u8 p;
    int n;
    int j;

    d = c->f->m.def->fields;
    p = page_of(c, i);
    n = 0;
    for (k = 0; k < c->f->m.def->nfields; k++)
        if (page_of(c, k) == p && c->at[k] == c->at[i]) {
            for (j = n; j > 0 && d[idx[j - 1]].col > d[k].col; j--)
                idx[j] = idx[j - 1];
            idx[j] = (u8)k;
            n++;
        }
    return n;
}

/* The cells field d shows: dispw, else width, a number's with its
   separators (INTL_NUMW) but a record's ID's. */
static int cells_of(const form_field *d)
{
    int cells;

    cells = d->dispw != 0 ? d->dispw : d->width;
    if (d->type == FT_NUM && !(d->flags & FF_ID))
        cells = INTL_NUMW(cells, d->dec);
    return cells;
}

/* The width of field i's control, a code field's with its pick
   button's room: its text's, as many cells as the field shows
   (cells_of; FORM_DATEW for a date; a field with rows, its longest
   row when longer), yet no less than a fixed-format value's widest
   text (vw) while its rows keep their names, plus its border and a
   drop-down list's button; least 1 gives an edit control of more than
   WF_SHARECELLS cells only WF_SHARECELLS, but never less than vw
   (WFORM.H). */
static int width_of(wf_dlg *c, u16 i, int least)
{
    int cells;
    int w;
    u16 n;
    u8 as;

    as = shown_as(c, i);
    if (as == FT_LOGIC)
        return WF_CHECKW;
    cells = cells_of(fld(c, i));
    n = has_rows(c, i) ? widest(c, i) : 0;
    if (n > (u16)cells)
        cells = (int)n;
    if (least && as != FT_CHOICE && cells > WF_SHARECELLS)
        cells = WF_SHARECELLS;
    w = (as == FT_DATE ? FORM_DATEW : cells) * WF_CELLW;
    if (w < c->vw[i] && !(c->bare >> i & 1))
        w = c->vw[i];
    return w + WF_PAD + (as == FT_CHOICE || as == FT_CODE ? WF_DROPW : 0);
}

/* Lay out the n fields idx of a line (peers), their controls w wide:
   each one's label at lx and control at x (WFORM.H), a check box's
   label right of its box, a line that carries a row on (cont) from the
   value column; the line's right end. */
static int pack(wf_dlg *c, const u8 *idx, int n, const int *w, int *lx,
                int *x)
{
    const form_field *d;
    int lead;
    int end;
    int j;

    lead = !(c->cont >> idx[0] & 1);
    end = lead ? WD_MARGIN : c->vx - WF_FGAP;
    for (j = 0; j < n; j++) {
        d = fld(c, idx[j]);
        if (unlabelled(d)) {
            x[j] = end + WF_CELLW;
            lx[j] = x[j];
        } else if (j == 0 && lead) {
            lx[j] = WD_MARGIN;
            x[j] = c->vx;
            if (page_of(c, idx[0]) == WF_PG_TOP)
                x[j] = WD_MARGIN + c->lw[idx[0]] + WF_LGAP;
        } else {
            lx[j] = end + WF_FGAP;
            x[j] = lx[j] + c->lw[idx[j]] + WF_LGAP;
        }
        end = x[j] + w[j];
        if (d->type == FT_LOGIC && !unlabelled(d)) {
            if (j > 0)
                x[j] = lx[j];
            lx[j] = x[j] + w[j] + WF_LGAP;
            end = lx[j] + c->lw[idx[j]];
        }
    }
    return end;
}

/* The boxes of field i's label (*lb) and control (*b), the control's
   with its pick button's room (WFORM.H): its line packed (pack), the
   edit controls of a line that passes WF_MAXW cut in proportion to
   their widths, then from the right, none below its least (width_of),
   which a line that spill has left holds, so that no control passes
   the margin while its line can be cut to fit; a drop-down list or a
   code field keeping a cell beside its button; the memo alone on its
   page, filling it. */
static void place(wf_dlg *c, u16 i, wf_box *lb, wf_box *b)
{
    u8 idx[FORM_MAXFIELDS];
    int w[FORM_MAXFIELDS];
    int lx[FORM_MAXFIELDS];
    int x[FORM_MAXFIELDS];
    const form_field *d;
    i32 sum;
    int least;
    int over;
    int cut;
    int right;
    int n;
    int j;

    d = fld(c, i);
    right = WF_MAXW - WD_MARGIN;
    if (d->type == FT_MEMO) {
        b->x = WD_MARGIN;
        b->y = c->pagey;
        b->cx = right - WD_MARGIN;
        b->cy = c->pageh - (WF_ROWH - WF_EDITH);
        *lb = *b;
        return;
    }
    n = peers(c, i, idx);
    sum = 0;
    for (j = 0; j < n; j++) {
        w[j] = width_of(c, idx[j], 0);
        if (width_of(c, idx[j], 1) < w[j])
            sum += w[j];
    }
    over = pack(c, idx, n, w, lx, x) - right;
    for (j = 0; over > 0 && j < n; j++) {
        least = width_of(c, idx[j], 1);
        if (least < w[j]) {
            w[j] -= (int)(((i32)over * w[j] + sum - 1) / sum);
            if (w[j] < least)
                w[j] = least;
        }
    }
    over = pack(c, idx, n, w, lx, x) - right;
    for (j = n - 1; over > 0 && j >= 0; j--) {
        cut = w[j] - width_of(c, idx[j], 1);
        if (cut > over)
            cut = over;
        w[j] -= cut;
        over -= cut;
    }
    pack(c, idx, n, w, lx, x);
    for (j = 0; idx[j] != i; j++)
        ;
    b->x = x[j];
    b->y = page_of(c, i) == WF_PG_TOP ? WD_MARGIN :
           c->pagey + c->at[i] * WF_ROWH;
    b->cx = w[j];
    b->cy = WF_EDITH;
    lb->x = lx[j];
    lb->y = b->y + (WF_EDITH - WF_LINEH) / 2;
    lb->cx = unlabelled(d) ? 0 : c->lw[i];
    lb->cy = WF_LINEH;
    if ((shown_as(c, i) == FT_CHOICE || shown_as(c, i) == FT_CODE) &&
        b->cx < WF_CELLW + WF_PAD + WF_DROPW)
        b->cx = WF_CELLW + WF_PAD + WF_DROPW;
}

/* Lay out the pages (WFORM.H): their number, each field's line on its
   page, its row of the table's there, none carried on (spill), the
   identity row's rows counting as one, and whether the form has that
   row. */
static void paginate(wf_dlg *c)
{
    const form_def *def;
    u16 i;
    u16 k;
    u8 r;
    u8 p;

    def = c->f->m.def;
    c->npages = 1;
    c->top = 0;
    c->cont = 0;
    for (i = 0; c->ops->pages == NULL && i < def->nfields; i++)
        if (def->fields[i].type == FT_MEMO)
            c->npages = 2;
    for (i = 0; i < def->nfields; i++) {
        p = page_of(c, i);
        c->at[i] = 0;
        if (p == WF_PG_TOP) {
            c->top = 1;
            continue;
        }
        if (c->ops->pages != NULL && p >= c->npages)
            c->npages = (u8)(p + 1);
        for (r = 0; r < def->fields[i].row; r++)
            for (k = 0; k < def->nfields; k++)
                if (page_of(c, k) == p && def->fields[k].row == r) {
                    c->at[i]++;
                    break;
                }
    }
}

/* Leave the names off the rows of the drop-down lists of each row that
   would pass WF_MAXW with its edit controls at their least (width_of),
   the right-most list first, until it does not (bare). */
static void fit(wf_dlg *c)
{
    u8 idx[FORM_MAXFIELDS];
    int w[FORM_MAXFIELDS];
    int lx[FORM_MAXFIELDS];
    int x[FORM_MAXFIELDS];
    u16 i;
    int n;
    int j;
    int k;

    c->bare = 0;
    for (i = 0; i < c->f->m.def->nfields; i++) {
        n = peers(c, i, idx);
        if (idx[0] != i || fld(c, i)->type == FT_MEMO)
            continue;
        for (k = 0; k >= 0; ) {
            for (j = 0; j < n; j++)
                w[j] = width_of(c, idx[j], 1);
            k = -1;
            if (pack(c, idx, n, w, lx, x) > WF_MAXW - WD_MARGIN)
                for (j = 0; j < n; j++)
                    if (shown_as(c, idx[j]) == FT_CHOICE &&
                        !(c->bare >> idx[j] & 1))
                        k = j;
            if (k >= 0)
                c->bare |= (u32)1 << idx[k];
        }
    }
}

/* Carry the fields of each line of a page that passes WF_MAXW with its
   controls at their least (width_of), from the first that ends past
   the margin on, on to a line of their own under it (cont), the lines
   after moved down and every line then looked at again, as the new one
   may pass too; then the pages' height, as tall as the one of most
   lines, a memo's at least WF_MEMOLINES lines (WFORM.H). */
static void spill(wf_dlg *c)
{
    u8 idx[FORM_MAXFIELDS];
    int w[FORM_MAXFIELDS];
    int lx[FORM_MAXFIELDS];
    int x[FORM_MAXFIELDS];
    const form_field *d;
    u16 i;
    u16 k;
    int rows;
    int memo;
    int n;
    int j;

    for (i = 0; i < c->f->m.def->nfields; i++) {
        n = peers(c, i, idx);
        if (idx[0] != i || page_of(c, i) == WF_PG_TOP ||
            fld(c, i)->type == FT_MEMO)
            continue;
        for (j = 0; j < n; j++)
            w[j] = width_of(c, idx[j], 1);
        pack(c, idx, n, w, lx, x);
        for (j = 1; j < n; j++) {
            d = fld(c, idx[j]);
            if ((d->type == FT_LOGIC && !unlabelled(d) ?
                 lx[j] + c->lw[idx[j]] : x[j] + w[j]) >
                WF_MAXW - WD_MARGIN)
                break;
        }
        if (j == n)
            continue;
        for (k = 0; k < c->f->m.def->nfields; k++)
            if (page_of(c, k) == page_of(c, i) && c->at[k] > c->at[i])
                c->at[k]++;
        c->cont |= (u32)1 << idx[j];
        for (; j < n; j++)
            c->at[idx[j]]++;
        i = (u16)-1;
    }
    rows = 0;
    memo = 0;
    for (i = 0; i < c->f->m.def->nfields; i++)
        if (fld(c, i)->type == FT_MEMO)
            memo = 1;
        else if (page_of(c, i) != WF_PG_TOP && c->at[i] + 1 > rows)
            rows = c->at[i] + 1;
    c->pageh = rows * WF_ROWH;
    if (memo && c->pageh < WF_MEMOLINES * WF_LINEH + WF_ROWH - WF_LINEH)
        c->pageh = WF_MEMOLINES * WF_LINEH + WF_ROWH - WF_LINEH;
}

/* Set plain (as_text, WFORM.H): in FORM_VIEW each field but a check box,
   a password and the memo whose value is of a fixed format (a date, a
   number or a choice), or whose control holds as many cells as the
   value can take (its width, or its longest row) and, free text, the
   value itself as measured (tw), so that a static shows it whole; any
   other stays a read-only edit control, which scrolls. */
static void texts(wf_dlg *c)
{
    const form_field *d;
    wf_box lb;
    wf_box b;
    u16 i;
    int n;
    u8 as;

    c->plain = 0;
    for (i = 0; c->f->m.mode == FORM_VIEW && i < c->f->m.def->nfields;
         i++) {
        d = fld(c, i);
        as = shown_as(c, i);
        if (d->type == FT_LOGIC || d->type == FT_SECRET ||
            d->type == FT_MEMO)
            continue;
        place(c, i, &lb, &b);
        n = has_rows(c, i) ? (int)widest(c, i) : d->width;
        if (d->type == FT_DATE || d->type == FT_NUM ||
            d->type == FT_CHOICE ||
            (n * WF_CELLW <= b.cx - WF_PAD - (as == FT_CODE ? WF_DROPW : 0)
             && c->tw[i] < WF_UNITSMAX && c->tw[i] <= b.cx))
            c->plain |= (u32)1 << i;
    }
}

/* Append the n bytes of r. */
static void put(wf_tpl *t, const u8 *r, u16 n)
{
    BYTE FAR *p;
    u16 k;

    if (t->h != NULL) {
        p = (BYTE FAR *)GlobalLock(t->h) + t->n;
        for (k = 0; k < n; k++)
            p[k] = r[k];
        GlobalUnlock(t->h);
    }
    t->n = (u16)(t->n + n);
}

/* Append a control of box b, ID id, style st and class cls with text
   (at most MSG_MAXLEN characters, in the Windows set). */
static void item(wf_tpl *t, const wf_box *b, UINT id, DWORD st, u8 cls,
                 const char *text)
{
    u8 r[WF_RECSZ];
    u16 n;

    put_le16(r, b->x);
    put_le16(r + 2, b->y);
    put_le16(r + 4, b->cx);
    put_le16(r + 6, b->cy);
    put_le16(r + 8, id);
    put_le32(r + 10, st);
    r[14] = cls;
    n = (u16)(strlen(text) + 1);
    memcpy(r + WF_ITEMSZ, text, n);
    r[WF_ITEMSZ + n] = 0;
    put(t, r, (u16)(WF_ITEMSZ + n + 1));
}

/* The slot of ch in a set of access letters, or -1 when ch is neither
   a digit nor an ASCII letter. */
static int key(int ch)
{
    return TXT_ISDIGIT(ch) ? ch - '0' :
           TXT_ISALPHA(ch) ? TXT_UPPER(ch) - 'A' + 10 : -1;
}

/* Put ch, a digit or an ASCII letter, else nothing, into used. */
static void reserve(u8 *used, int ch)
{
    int n;

    n = key(ch);
    if (n >= 0)
        used[n] = 1;
}

/* Write t into out (MSG_MAXLEN + 1 bytes, cut there) as a control
   without SS_NOPREFIX shows it: each '&' doubled, and, when show is 1,
   an '&' before t's access letter, the first digit or ASCII letter not
   in used that starts a word, else the first not in used, if any, which
   used then holds. */
static void mark(char *out, const char *t, u8 *used, int show)
{
    int at;
    int pass;
    int k;
    int n;

    at = -1;
    for (pass = 0; show && pass < 2 && at < 0; pass++)
        for (k = 0; t[k] != '\0' && at < 0; k++) {
            n = key(t[k]);
            if (n >= 0 && !used[n] &&
                (pass > 0 || k == 0 || key(t[k - 1]) < 0))
                at = k;
        }
    if (at >= 0)
        reserve(used, t[at]);
    n = 0;
    for (k = 0; t[k] != '\0' && n < MSG_MAXLEN - 1; k++) {
        if (t[k] == '&' || k == at)
            out[n++] = '&';
        out[n++] = t[k];
    }
    out[n] = '\0';
}

/* Put into used the access letters of t, a button's text, each the
   character after a single '&', or with tabs 1 a tab strip's text
   (WTAB.H), each tab's after its single '&', else its first. */
static void reserve_text(u8 *used, const char *t, int tabs)
{
    const char *s;
    int got;

    for (;;) {
        got = 0;
        for (s = t; *s != '\0' && (!tabs || *s != WT_SEP); s++)
            if (*s == '&' && s[1] != '\0' && *++s != '&' && !got) {
                reserve(used, *s);
                got = 1;
            }
        if (tabs && !got)
            reserve(used, *t);
        if (*s == '\0')
            return;
        t = s + 1;
    }
}

/* Put into used the access letters of string sid (reserve_text). */
static void reserve_string(wf_dlg *c, u8 *used, UINT sid, int tabs)
{
    char t[MSG_MAXLEN + 1];

    if (LoadString(c->inst, sid, t, (int)sizeof t) > 0)
        reserve_text(used, t, tabs);
}

/* The string of the strip's tabs: the page table's, or WF_S_PAGES. */
static UINT tabs_of(wf_dlg *c)
{
    return c->ops->pages != NULL ? c->ops->pages->tabs : WF_S_PAGES;
}

/* The record buttons (wf_pages). */
static u8 acts(wf_dlg *c)
{
    return c->ops->pages != NULL ? c->ops->pages->nacts : 0;
}

/* 1 when the dialog has the Help button (WFORM.H). */
static int helps(wf_dlg *c)
{
    return wf_show != NULL && c->f->m.def->topic != NULL;
}

void wf_label(HINSTANCE inst, const form_field *d, char *t)
{
    if (LoadString(inst, WF_S_LABEL + d->label, t, MSG_MAXLEN + 1) == 0) {
        txt_copy(t, msg(d->label), MSG_MAXLEN + 1);
        OemToAnsi(t, t);
    }
}

/* The caption (WFORM.H) into t (MSG_MAXLEN + 1 bytes), in the Windows
   set. */
static void caption(wf_dlg *c, char *t)
{
    char name[WF_NAMESZ];
    char f[MSG_MAXLEN + 1];
    char s[MSG_MAXLEN + 1];

    if (c->ops->title != 0 && c->f->m.mode != FORM_NEW) {
        if (c->top)
            txt_copy(name, c->id, (u16)sizeof name);
        else
            wf_name(c->f, c->ops, c->id, name);
        txt_copy(t, msg_sub(c->ops->title, name, NULL), MSG_MAXLEN + 1);
    } else {
        msg_get(c->f->m.def->title, t, MSG_MAXLEN + 1);
    }
    OemToAnsi(t, t);
    if (c->f->m.mode != FORM_EDIT || c->ops->table == NULL)
        return;
    if (LoadString(c->inst, WF_S_EDITCAP, f, (int)sizeof f) == 0)
        return;
    txt_copy(s, t, (u16)(sizeof s - strlen(f)));
    wsprintf(t, f, (LPSTR)s);
}

/* Write the template's header: style st, n controls, a box cx by cy at
   0, 0, no menu or class, the caption text and the font. */
static void head(wf_tpl *t, DWORD st, u8 n, int cx, int cy,
                 const char *text)
{
    u8 r[WF_RECSZ];
    u16 len;

    len = (u16)(strlen(text) + 1);
    put_le32(r, st);
    r[4] = n;
    put_le16(r + 5, 0);
    put_le16(r + 7, 0);
    put_le16(r + 9, cx);
    put_le16(r + 11, cy);
    r[13] = 0;
    r[14] = 0;
    memcpy(r + WF_ITEMSZ, text, len);
    put_le16(r + WF_ITEMSZ + len, WF_FONTPT);
    memcpy(r + WF_ITEMSZ + len + 2, k_font, sizeof k_font);
    put(t, r, (u16)(WF_ITEMSZ + len + 2 + sizeof k_font));
}

/* Write field i's label, but the memo's, its control and its pick
   button, each hidden off the page shown, the label's access letter
   taken from used (mark); dots is the pick button's text. */
static void put_field(wf_tpl *t, wf_dlg *c, u16 i, u8 *used,
                      const char *dots)
{
    char lab[MSG_MAXLEN + 1];
    char text[MSG_MAXLEN + 1];
    const form_field *d;
    wf_box lb;
    wf_box b;
    DWORD st;
    DWORD off;
    u8 cls;

    d = fld(c, i);
    place(c, i, &lb, &b);
    off = on_show(c, i) ? 0 : WS_VISIBLE;
    if (d->type != FT_MEMO) {
        wf_label(c->inst, fld(c, i), lab);
        mark(text, lab, used, editable(c, i));
        item(t, &lb, (UINT)-1, WF_LABEL & ~off, WF_C_STATIC, text);
    }
    st = WF_CHILD | WS_BORDER | ES_AUTOHSCROLL |
         ((int)i == c->pi || shows(d) ? 0 : WS_TABSTOP);
    cls = WF_C_EDIT;
    switch (shown_as(c, i)) {
    case FT_LOGIC:
        st = WF_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX;
        cls = WF_C_BUTTON;
        break;
    case FT_CHOICE:
        st = WF_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
        cls = WF_C_COMBO;
        b.cy += WF_DROPROWS * WF_LINEH;
        break;
    case FT_MEMO:
        st = (st & ~ES_AUTOHSCROLL) | WS_VSCROLL | ES_MULTILINE |
             ES_AUTOVSCROLL;
        break;
    case FT_SECRET:
        st |= ES_PASSWORD;
        break;
    default:
        if ((d->flags & FF_UPPER) && !has_rows(c, i))
            st |= ES_UPPERCASE;
        break;
    }
    if (as_text(c, i)) {
        st = WF_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX;
        cls = WF_C_STATIC;
        b.y = lb.y;
        b.cy = WF_LINEH;
    } else if (shown_as(c, i) == FT_CODE) {
        b.cx -= WF_DROPW;
    }
    item(t, &b, WF_ID0 + i, st & ~off, cls, k_empty);
    if (pickable(c, i)) {
        b.x += b.cx;
        b.cx = WF_DROPW;
        item(t, &b, WF_PICK0 + i, (WF_CHILD | BS_PUSHBUTTON) & ~off,
             WF_C_BUTTON, dots);
    }
}

/* Write the dialog's template (WFORM.H's layout). */
static void build(wf_tpl *t, wf_dlg *c)
{
    /* The buttons, set the indexes of those shown: OK and Cancel in
       FORM_EDIT and FORM_NEW, Close and, on a form that OK saves, Edit
       in FORM_VIEW; then Help (helps). */
    static const struct {
        u16 s;                  /* the text, a string of WFSTR.H        */
        u8  id;
        u8  def;                /* 1 for the default button             */
    } buttons[] = {
        { WF_S_OK, IDOK, 1 },
        { WF_S_CANCEL, IDCANCEL, 0 },
        { WF_S_CLOSE, IDCANCEL, 1 },
        { WF_S_EDIT, WF_EDITID, 0 },
        { WF_S_HELP, WF_HELPID, 0 }
    };
    const form_def *def;
    char text[MSG_MAXLEN + 1];
    char dots[MSG_MAXLEN + 1];
    u8 used[WF_KEYS];
    u8 set[3];
    wf_box lb;
    wf_box b;
    DWORD st;
    int nb;
    int cx;
    int bottom;
    int k;
    u16 i;
    u16 n;
    u8 band;

    def = c->f->m.def;
    nb = 0;
    if (c->f->m.mode != FORM_VIEW) {
        set[nb++] = 0;
        set[nb++] = 1;
    } else {
        set[nb++] = 2;
        if (c->ops->table != NULL)
            set[nb++] = 3;
    }
    if (helps(c))
        set[nb++] = 4;
    cx = 2 * WD_MARGIN + WF_MSGMINW + nb * (WD_GAP + WD_BTNW);
    if (2 * WD_MARGIN + acts(c) * (WD_BTNW + WD_GAP) - WD_GAP > cx)
        cx = 2 * WD_MARGIN + acts(c) * (WD_BTNW + WD_GAP) - WD_GAP;
    n = (u16)(2 * def->nfields + 1 + nb + acts(c));
    for (i = 0; i < def->nfields; i++) {
        n = (u16)(n + pickable(c, i) - (def->fields[i].type == FT_MEMO));
        place(c, i, &lb, &b);
        if (!hidden(c, i) && b.x + b.cx + WD_MARGIN > cx)
            cx = b.x + b.cx + WD_MARGIN;
    }
    bottom = c->pagey + c->pageh;
    if (acts(c) > 0)
        bottom += WD_GAP + WD_BTNH;
    caption(c, text);
    head(t, WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU |
            DS_MODALFRAME | DS_SETFONT, (u8)n, cx,
         bottom + WD_GAP + WF_MSGH + WD_MARGIN, text);
    /* The letters no label takes: the tabs', the record buttons' and the
       Help button's. */
    memset(used, 0, sizeof used);
    if (c->npages > 1)
        reserve_string(c, used, tabs_of(c), 1);
    for (k = 0; k < acts(c); k++)
        reserve_string(c, used, c->ops->pages->acts + k, 0);
    if (helps(c))
        reserve_string(c, used, WF_S_HELP, 0);
    LoadString(c->inst, WF_S_PICK, dots, (int)sizeof dots);
    /* The identity row, then each page. */
    for (k = -1; k < c->npages; k++) {
        band = k < 0 ? WF_PG_TOP : (u8)k;
        for (i = 0; i < def->nfields; i++)
            if (page_of(c, i) == band)
                put_field(t, c, i, used, dots);
    }
    b.x = WD_MARGIN;
    b.y = c->pagey + c->pageh + WD_GAP;
    b.cx = WD_BTNW;
    b.cy = WD_BTNH;
    for (k = 0; k < acts(c); k++) {
        LoadString(c->inst, c->ops->pages->acts + k, text, (int)sizeof text);
        item(t, &b, WF_ACT0 + k, WF_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
             WF_C_BUTTON, text);
        b.x += WD_BTNW + WD_GAP;
    }
    b.x = WD_MARGIN;
    b.y = bottom + WD_GAP;
    b.cx = cx - 2 * WD_MARGIN - nb * (WD_GAP + WD_BTNW);
    b.cy = WF_MSGH;
    item(t, &b, WF_MSGID, WF_LABEL | SS_NOPREFIX, WF_C_STATIC, k_empty);
    b.x += b.cx + WD_GAP;
    b.cx = WD_BTNW;
    b.cy = WD_BTNH;
    for (k = 0; k < nb; k++) {
        LoadString(c->inst, buttons[set[k]].s, text, (int)sizeof text);
        st = buttons[set[k]].id == WF_EDITID && c->level < TUI_LVL_OPER ?
             WS_DISABLED : 0;
        item(t, &b, buttons[set[k]].id, WF_CHILD | WS_TABSTOP | st |
             (buttons[set[k]].def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
             WF_C_BUTTON, text);
        b.x += WD_BTNW + WD_GAP;
    }
}

/* ---------------------------------------------------------------------
 * Values and controls.
 * --------------------------------------------------------------------- */

/* The empty rows heading field d's drop-down list: one unless FF_REQ. */
static int choice_skip(const form_field *d)
{
    return (d->flags & FF_REQ) ? 0 : 1;
}

/* Show field i's value in its control, an edit control's unchanged
   from then on: a drop-down list filled with its rows, the value's
   selected; in an edit control or a static a field with rows shows the
   value's row, else the value; a password through a copy on the stack,
   wiped after; a date or number that cannot be changed as a view shows
   it (wf_view_text). */
static void show(wf_dlg *c, u16 i)
{
    char t[FORM_MAXVAL + 1];
    char code[FORM_MAXVAL + 1];
    const form_field *d;
    char *v;
    HWND e;
    int sel;
    u16 k;

    d = fld(c, i);
    v = form_val(c->f, i);
    e = ctl(c, i);
    switch (shown_as(c, i)) {
    case FT_LOGIC:
        CheckDlgButton(c->dlg, WF_ID0 + i, v[0] == 'T');
        return;
    case FT_CHOICE:
        SendMessage(e, CB_RESETCONTENT, 0, 0L);
        if (choice_skip(d))
            SendMessage(e, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)k_empty);
        sel = v[0] == '\0' && choice_skip(d) ? 0 : -1;
        for (k = 0; k < rows(c, i); k++) {
            row(c, i, k, code, t);
            OemToAnsi(t, t);
            SendMessage(e, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)t);
            if (v[0] != '\0' && strcmp(code, v) == 0)
                sel = k + choice_skip(d);
        }
        SendMessage(e, CB_SETCURSEL, (WPARAM)sel, 0L);
        return;
    case FT_MEMO:
        return;
    case FT_DATE:
    case FT_NUM:
        if (!editable(c, i))
            wf_view_text(c->f, i, t);
        else if (d->type == FT_NUM || !msg_date(t, v))
            txt_copy(t, v, (u16)sizeof t);
        break;
    default:
        if (!has_rows(c, i) || !row_of(c, i, v, t))
            txt_copy(t, v, (u16)sizeof t);
        break;
    }
    OemToAnsi(t, t);
    SetWindowText(e, t);
    if (d->type == FT_SECRET)
        txt_wipe(t, (u16)sizeof t);
    if (is_edit(c, i))
        SendMessage(e, EM_SETMODIFY, FALSE, 0L);
}

/* Make field i's control one that cannot change (WFORM.H); a static
   is one already. */
static void lock(wf_dlg *c, u16 i)
{
    if (!as_text(c, i) && (!ro_edit(c, i) || !we_attach(ctl(c, i), 1)))
        EnableWindow(ctl(c, i), FALSE);
}

/* Read field i, which can be changed, from its control into the
   model, but an edit control's only when its text has changed, so that
   a value the user has not touched never passes through AnsiToOem,
   which loses much of CP437. */
static void gather(wf_dlg *c, u16 i)
{
    char t[FORM_MAXVAL + 1];
    const form_field *d;
    char *v;
    char *s;
    char *p;
    u16 size;
    int sel;

    d = fld(c, i);
    v = form_val(c->f, i);
    size = form_valsz(d);
    switch (shown_as(c, i)) {
    case FT_LOGIC:
        if (IsDlgButtonChecked(c->dlg, WF_ID0 + i))
            strcpy(v, "T");
        else if (v[0] != '\0')
            strcpy(v, "F");
        return;
    case FT_CHOICE:
        /* No row selected keeps the value; a code is its row's text up
           to the first blank, the empty row's "", a choice's word by
           its index. */
        sel = (int)SendMessage(ctl(c, i), CB_GETCURSEL, 0, 0L);
        if (sel < 0)
            return;
        if (d->type != FT_CHOICE) {
            SendMessage(ctl(c, i), CB_GETLBTEXT, (WPARAM)sel,
                        (LPARAM)(LPSTR)t);
            t[strcspn(t, " ")] = '\0';
            AnsiToOem(t, t);
            txt_copy(v, t, size);
        } else if (sel < choice_skip(d)) {
            v[0] = '\0';
        } else {
            form_choice_word(d->choices, (u16)(sel - choice_skip(d)), v,
                             size);
        }
        return;
    case FT_MEMO:
        return;
    default:
        break;
    }
    if (SendMessage(ctl(c, i), EM_GETMODIFY, 0, 0L) == 0)
        return;
    if (d->type == FT_SECRET) {
        GetWindowText(ctl(c, i), v, (int)size);
        AnsiToOem(v, v);
        return;
    }
    GetWindowText(ctl(c, i), t, (int)sizeof t);
    AnsiToOem(t, t);
    s = t + strlen(t);
    while (s > t && s[-1] == ' ')
        s--;
    *s = '\0';
    if (d->type == FT_DATE) {
        for (s = p = t; *s != '\0'; s++)
            if (*s != '-')
                *p++ = *s;
        *p = '\0';
        if (p - t > FORM_DATELEN)
            t[FORM_DATELEN - 1] = '\0';
    }
    txt_copy(v, t, size);
}

/* Wipe every FT_SECRET field: its control (we_wipe), and its value and
   the original that a save leaves, by a load of as many blanks as the
   slot holds, then of "", so that no tail of a password stays. */
static void wipe(wf_dlg *c)
{
    char blanks[FORM_MAXVAL + 1];
    const form_field *d;
    u16 i;

    memset(blanks, ' ', FORM_MAXVAL);
    blanks[FORM_MAXVAL] = '\0';
    for (i = 0; i < c->f->m.def->nfields; i++) {
        d = fld(c, i);
        if (d->type == FT_SECRET) {
            we_wipe(ctl(c, i));
            form_load(c->f, d->name, blanks);
            form_load(c->f, d->name, k_empty);
        }
    }
}

/* Read every field that can be changed into the model. */
static void gather_all(wf_dlg *c)
{
    u16 i;

    for (i = 0; i < c->f->m.def->nfields; i++)
        if (editable(c, i))
            gather(c, i);
}

/* Wrap the memo's text t in place at MEMO_COLS (WFORM.H, OK's step
   2), an LF in place of the blanks before a word moved to the next
   line. */
static void wrap(char *t)
{
    const char *s;
    char *line;
    char *d;
    u16 b;
    u16 w;

    s = t;
    line = t;
    d = t;
    while (*s != '\0') {
        b = (u16)strspn(s, " ");
        w = (u16)strcspn(s + b, " \r\n");
        if (w == 0 || (d > line && (u16)(d - line) + b + w > MEMO_COLS)) {
            s += b;
            b = 0;
            if (w > 0) {
                *d++ = '\n';
                line = d;
            }
        }
        memmove(d, s, b + w);
        d += b + w;
        s += b + w;
        if (*s == '\r' || *s == '\n') {
            *d++ = *s++;
            line = d;
        }
    }
    *d = '\0';
}

/* 1 when the memo's text has changed. */
static int memo_changed(wf_dlg *c)
{
    return c->mi >= 0 &&
           SendMessage(ctl(c, (u16)c->mi), EM_GETMODIFY, 0, 0L) != 0;
}

/* ---------------------------------------------------------------------
 * Messages and failures.
 * --------------------------------------------------------------------- */

/* Show page p (WFORM.H): the controls of the others hidden, the dialog
   painted again for their wells' frames (WDLG.H's look), and the focus,
   when it was on one of them, moved to the first control after the
   strip that Tab stops on. */
static void turn(wf_dlg *c, u8 p)
{
    HWND was;
    HWND e;
    u16 i;
    int sw;

    was = GetFocus();
    c->page = p;
    for (i = 0; i < c->f->m.def->nfields; i++) {
        if (page_of(c, i) == WF_PG_TOP || hidden(c, i))
            continue;
        sw = page_of(c, i) == p ? SW_SHOW : SW_HIDE;
        e = ctl(c, i);
        if (fld(c, i)->type != FT_MEMO)
            ShowWindow(GetWindow(e, GW_HWNDPREV), sw);
        ShowWindow(e, sw);
        if (pickable(c, i))
            ShowWindow(GetDlgItem(c->dlg, WF_PICK0 + i), sw);
    }
    InvalidateRect(c->dlg, NULL, TRUE);
    if (was != NULL && !IsWindowVisible(was))
        SendMessage(c->dlg, WM_NEXTDLGCTL,
                    (WPARAM)GetNextDlgTabItem(c->dlg, c->strip, FALSE), 1L);
}

/* Give the focus to the control e, an edit control's text selected, as
   TAB gives it, a field's page shown first. */
static void focus(wf_dlg *c, HWND e)
{
    int i;
    u8 p;

    i = field_of(c, e);
    p = i >= 0 ? page_of(c, (u16)i) : WF_PG_TOP;
    if (c->strip != NULL && p != WF_PG_TOP && p != c->page) {
        SendMessage(c->strip, WTM_SETCUR, p, 0L);
        turn(c, p);
    }
    SendMessage(c->dlg, WM_NEXTDLGCTL, (WPARAM)e, 1L);
}

/* Put text (CP437) on the message line, and the focus on field i when
   it is 0 or more. */
static void say(wf_dlg *c, int i, const char *text)
{
    char t[RQE_TEXTLEN + 1];

    txt_copy(t, text, (u16)sizeof t);
    OemToAnsi(t, t);
    SetDlgItemText(c->dlg, WF_MSGID, t);
    if (i >= 0)
        focus(c, ctl(c, (u16)i));
}

/* Put the memo limit that mm, mio_check's message on the memo's new
   text, names (WFORM.H, OK's step 2) on the message line and the focus
   on the memo. */
static void say_limit(wf_dlg *c, u16 mm)
{
    char f[MSG_MAXLEN + 1];
    char t[MSG_MAXLEN + TXT_U32LEN + 1];
    UINT lim;

    lim = mm == MM_TOOMANY ? WF_S_LINES :
          mm == MM_TOOLONG ? WF_S_BYTES : WF_S_LINE;
    LoadString(c->inst, lim, f, (int)sizeof f);
    wsprintf(t, f, lim == WF_S_LINES ? MEMO_MAXLINES :
                   lim == WF_S_BYTES ? LINK_MAXMEMO : MEMO_COLS);
    AnsiToOem(t, t);
    say(c, c->mi, t);
}

/* Show text (CP437) in a message box over w, the dialog or its owner,
   captioned as w is (wd_say), with icon (an MB_ICON value). */
static void box(HWND w, UINT icon, const char *text)
{
    char t[RQE_TEXTLEN + 1];

    txt_copy(t, text, (u16)sizeof t);
    OemToAnsi(t, t);
    wd_say(w, icon, t);
}

/* The end of a dialog by act, the action of a failure that leaves
   (RQE_LOGIN, RQE_QUIT or RQE_LOST): WF_LOGIN, WF_QUIT or WF_LOST. */
static int gone(u8 act)
{
    return act == RQE_LOGIN ? WF_LOGIN : act == RQE_QUIT ? WF_QUIT : WF_LOST;
}

/* End the dialog by act, as gone says. */
static void leave(wf_dlg *c, u8 act)
{
    EndDialog(c->dlg, gone(act));
}

/* Show the failure res (WFORM.H); 1 when the dialog has been ended. */
static int failed(wf_dlg *c, const rq_res *res)
{
    rq_res r;
    rqe e;
    int i;

    if (c->f->m.partial) {
        c->f->m.partial = 0;
        box(c->dlg, MB_ICONEXCLAMATION, msg(MF_PARTIAL));
    }
    memcpy(&r, res, sizeof r);
    i = form_err_field(c->f, &r);
    if (i >= 0 && r.code == RQ_ENOTFOUND)
        r.code = RQ_EARG;
    rqe_class(&e, &r, c->level);
    if (e.show == RQE_LINE && r.code != RQ_ENOTFOUND) {
        if (i < 0 && r.code == RQ_EDUP)
            i = form_key(c->f);
        say(c, i, e.text);
        return 0;
    }
    box(c->dlg, WD_FAILICON(e.show), e.text);
    if (r.code == RQ_ENOTFOUND)
        EndDialog(c->dlg, WF_GONE);
    else if (e.act != RQE_STAY)
        leave(c, e.act);
    else
        return 0;
    return 1;
}

/* ---------------------------------------------------------------------
 * The dialog.
 * --------------------------------------------------------------------- */

/* The owner's busy note, on or off. */
static void busy(wf_dlg *c, int on)
{
    c->ops->busy(c->arg, c->dlg, on);
}

/* Show the memo of a saved record in its control e as the dialog opens,
   loaded the first time, saying what the load made of it when that
   leaves it read-only (WFORM.H); 1 when it is to be read-only, -1 when
   a failure has ended the dialog, else 0. */
static int memo_show(wf_dlg *c, HWND e)
{
    char num[TXT_U32LEN + 1];
    rq_res res;
    int rc;

    if (c->mm == WF_M_NEW) {
        busy(c, 1);
        rc = c->ops->memo_load(&c->memo, c->l, c->ops->table, c->id,
                               &res);
        busy(c, 0);
        if (rc != RQ_OK) {
            c->mm = WF_M_FAIL;
            return failed(c, &res) ? -1 : 1;
        }
        c->mm = mio_check(&c->memo);
    }
    if (c->mm == WF_M_FAIL)
        return 1;
    mio_text(&c->memo, c->text, WF_MEMOSZ);
    SetWindowText(e, c->text);
    SendMessage(e, EM_SETMODIFY, FALSE, 0L);
    if (c->mm == 0 || c->mm == MM_CHANGED)
        return 0;
    txt_u32(num, MEMO_MAXLINES);
    say(c, -1, msg_sub(c->mm, num, NULL));
    return 1;
}

/* Put v in the location field, as loaded or as a change as how says
   (where). */
static void set_loc(wf_dlg *c, int how, const char *v)
{
    if (how == WF_W_LOADED)
        form_load(c->f, c->ops->loc->loc->name, v);
    else
        form_put(c->f, c->ops->loc->loc->name, v);
    show(c, (u16)c->li);
}

/* The message refusing the location x, found as how says (WF_W_), as
   the location field's in a location's own form (WFORM.H), or 0. */
static u16 refusal(wf_dlg *c, int how, const scr_locinfo *x)
{
    if (!(c->ops->flags & WF_O_SELF) || how == WF_W_LOADED)
        return 0;
    if (x->depth >= LINK_MAXLOCDEPTH)
        return MO_TOODEEP;
    if (how == WF_W_TYPED && c->f->m.mode != FORM_NEW &&
        (strcmp(x->id, c->id) == 0 ||
         wp_within(c->l, x->parent, c->id, c->ops->busy, c->arg, c->dlg)))
        return MO_INSELF;
    return 0;
}

/* The cells of the path field of location field d. */
static u16 path_w(const scr_locf *d)
{
    return d->path->dispw != 0 ? d->path->dispw : d->path->width;
}

int wf_loc_asks(form *f, const scr_locf *d)
{
    char t[FORM_MAXVAL + 1];
    const char *val;

    val = form_get(f, d->loc->name);
    t[0] = '\0';
    if ((val[0] == '\0' && !d->filter) || strcmp(val, k_zero) == 0)
        msg_get(d->none, t, (u16)(path_w(d) + 1));
    else if (val[0] != '\0')
        return 1;
    form_load(f, d->path->name, t);
    return 0;
}

int wf_loc_get(form *f, link *l, const scr_locf *d, scr_locinfo *x,
               rq_res *res)
{
    char t[FORM_MAXVAL + 1];
    rq q;
    int rc;

    memset(x, 0, sizeof *x);
    rq_begin(&q, wp_w_locget);
    rq_word(&q, form_get(f, d->loc->name));
    rc = rq_send(l, &q, scr_loc_info, x, res);
    msg_path(t, x->path, path_w(d));
    form_load(f, d->path->name, t);
    return rc;
}

/* Show in the path field the path of the location field's value, as
   WFORM.H says, found as how says (WF_W_): none's text, "" (any
   location), or the path LOC GET gives, after which a location's code,
   where it has one, replaces a value loaded or chosen, as it was; "0"
   in a record's form becomes blank.  A value that names no location, or
   one refused (refusal), is said on the message line and shows "", and
   stays to be looked up again; a failed request is shown as WFORM.H
   says; anything else clears the line, but as the dialog opens.  1,
   WF_W_NONE for a value naming no location or refused, or 0 once a
   failure has ended the dialog. */
static int where(wf_dlg *c, int how)
{
    const scr_locf *d;
    const char *val;
    scr_locinfo x;
    rq_res res;
    int rc;
    int r;
    int said;
    u16 m;

    r = 1;
    said = 0;
    d = c->ops->loc;
    val = form_val(c->f, (u16)c->li);
    if (!d->filter && strcmp(val, k_zero) == 0)
        set_loc(c, how, k_empty);
    c->locid[0] = '\0';
    if (wf_loc_asks(c->f, d)) {
        busy(c, 1);
        rc = wf_loc_get(c->f, c->l, d, &x, &res);
        busy(c, 0);
        if (rc == RQ_ENOTFOUND) {
            say(c, -1, msg_sub(MO_NOSUCH, val, NULL));
            r = WF_W_NONE;
            said = 1;
        } else if (rc != RQ_OK) {
            if (failed(c, &res))
                return 0;
            said = 1;
        } else {
            if (how != WF_W_TYPED && x.code[0] != '\0')
                set_loc(c, how, x.code);
            m = refusal(c, how, &x);
            if (m != 0) {
                say(c, -1, msg_sub(m, x.name, NULL));
                form_load(c->f, d->path->name, k_empty);
                r = WF_W_NONE;
                said = 1;
            } else {
                txt_copy(c->locid, x.id, (u16)sizeof c->locid);
            }
        }
    }
    if (!said && how != WF_W_LOADED)
        SetDlgItemText(c->dlg, WF_MSGID, k_empty);
    txt_copy(c->shown, r == 1 ? val : k_empty, (u16)sizeof c->shown);
    show(c, (u16)c->pi);
    return r;
}

/* Show in the name field the name of the code in its code field
   (ops->codes), none for no code. */
static void named(wf_dlg *c)
{
    char t[LINK_CODENAMELEN + 1];
    const char *v;

    v = form_val(c->f, (u16)c->ci);
    t[0] = '\0';
    if (v[0] != '\0')
        ct_name(c->ops->codes, v, t);
    form_load(c->f, fld(c, (u16)c->ni)->name, t);
    show(c, (u16)c->ni);
}

/* The location field or the name field's code field after it lost the
   focus, or a row chosen in that code field's drop-down list: the name
   of the code typed or chosen (named), and the path of a
   location typed that the path field does not show yet (where), unless
   the focus went to a dialog over this one, a pick list's, which holds
   it disabled, or to none, the dialog ending; OK looks the location up
   in any case. */
static void typed(wf_dlg *c)
{
    if (!IsWindowEnabled(c->dlg))
        return;
    if (c->ni >= 0 && editable(c, (u16)c->ci)) {
        gather(c, (u16)c->ci);
        named(c);
    }
    if (c->li < 0 || !editable(c, (u16)c->li))
        return;
    gather(c, (u16)c->li);
    if (strcmp(form_val(c->f, (u16)c->li), c->shown) != 0)
        where(c, WF_W_TYPED);
}

/* Give the FT_MEMO field's control, if any, OEM_FIXED_FONT and a width
   that shows lines of MEMO_COLS characters, as stored, so that it wraps
   them where OK does (WFORM.H); the dialog grows when it would not hold
   it. */
static void memo_fit(wf_dlg *c)
{
    TEXTMETRIC tm;
    RECT w;
    RECT r;
    POINT p;
    HWND e;
    HDC dc;
    int cx;
    int over;
    u16 i;

    for (i = 0; i < c->f->m.def->nfields; i++)
        if (fld(c, i)->type == FT_MEMO)
            break;
    if (i == c->f->m.def->nfields)
        return;
    e = ctl(c, i);
    SendMessage(e, WM_SETFONT, (WPARAM)GetStockObject(OEM_FIXED_FONT),
                FALSE);
    dc = GetDC(e);
    SelectObject(dc, GetStockObject(OEM_FIXED_FONT));
    GetTextMetrics(dc, &tm);
    ReleaseDC(e, dc);
    /* Its frame, scroll bar and margins kept, its text wide enough for
       lines of MEMO_COLS characters. */
    GetWindowRect(e, &w);
    SendMessage(e, EM_GETRECT, 0, (LPARAM)(LPRECT)&r);
    cx = w.right - w.left - (r.right - r.left) +
         (MEMO_COLS + WF_WRAPSLACK) * tm.tmAveCharWidth;
    SetWindowPos(e, NULL, 0, 0, cx, w.bottom - w.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    /* Its right edge WD_MARGIN inside the dialog's at least. */
    p.x = w.left;
    p.y = w.top;
    ScreenToClient(c->dlg, &p);
    SetRect(&r, 0, 0, WD_MARGIN, 0);
    MapDialogRect(c->dlg, &r);
    over = p.x + cx + r.right;
    GetClientRect(c->dlg, &r);
    over -= r.right;
    if (over > 0) {
        GetWindowRect(c->dlg, &w);
        SetWindowPos(c->dlg, NULL, 0, 0, w.right - w.left + over,
                     w.bottom - w.top, SWP_NOMOVE | SWP_NOZORDER);
    }
}

/* Free the code lists, once their rows are in the controls. */
static void free_lists(wf_dlg *c)
{
    int k;

    for (k = 0; k < WF_NLISTS; k++)
        if (c->have >> k & 1)
            ct_close(&c->lists[k]);
}

/* Fill the controls, freeing the code lists after, and load the memo;
   FALSE when the focus has been set, on the first field of the page
   shown that can be changed, else on the first anywhere, in FORM_VIEW
   on the strip, or Close without one, TRUE to leave it to Windows. */
static BOOL init(wf_dlg *c)
{
    const form_field *d;
    HWND first;
    HWND any;
    HWND e;
    u16 i;
    int ro;
    int r;

    first = NULL;
    any = NULL;
    for (i = 0; i < c->f->m.def->nfields; i++) {
        d = fld(c, i);
        e = ctl(c, i);
        ro = !editable(c, i) ||
             ((d->type == FT_SECRET || d->type == FT_MEMO) &&
              !we_attach(e, 0));
        if (is_edit(c, i) && !has_rows(c, i))
            SendMessage(e, EM_LIMITTEXT,
                        d->type == FT_DATE ? FORM_DATEW :
                        d->type == FT_MEMO ? LINK_MAXMEMO :
                        d->type == FT_SECRET && d->width > LINK_PWMAX ?
                        LINK_PWMAX : d->width, 0L);
        show(c, i);
        if ((int)i == c->mi && c->f->m.mode != FORM_NEW) {
            r = memo_show(c, e);
            if (r < 0)
                break;
            ro |= r;
        }
        if (ro)
            lock(c, i);
        else if (any == NULL)
            any = e;
        if (!ro && first == NULL && page_of(c, i) == c->page)
            first = e;
    }
    free_lists(c);
    if (i < c->f->m.def->nfields)
        return TRUE;
    if (c->ni >= 0)
        named(c);
    if (c->li >= 0 && !where(c, WF_W_LOADED))
        return TRUE;
    if (first == NULL)
        first = any;
    if (first == NULL && c->f->m.mode == FORM_VIEW)
        first = c->strip != NULL ? c->strip : GetDlgItem(c->dlg, IDCANCEL);
    if (first == NULL)
        return TRUE;
    focus(c, first);
    return FALSE;
}

/* After an ADD that succeeded: the new ID shown, the caption, and the
   FF_NEWONLY fields locked and shown again, as fields that cannot be
   changed are. */
static void added(wf_dlg *c)
{
    char t[MSG_MAXLEN + 1];
    int i;

    if (c->ops->idfield != NULL) {
        form_load(c->f, c->ops->idfield, c->id);
        i = form_find(c->f, c->ops->idfield);
        if (i >= 0)
            show(c, (u16)i);
    }
    caption(c, t);
    SetWindowText(c->dlg, t);
    for (i = 0; i < c->f->m.def->nfields; i++)
        if (fld(c, (u16)i)->flags & FF_NEWONLY) {
            lock(c, (u16)i);
            show(c, (u16)i);
        }
}

/* Send the memo, its new text taken (WFORM.H, OK's step 4); 1 once it
   is saved, else 0 with the failure shown. */
static int memo_store(wf_dlg *c)
{
    char a[TXT_U32LEN + 1];
    char b[TXT_U32LEN + 1];
    rq_res res;
    u16 done;
    u16 total;
    u16 m;
    int rc;

    busy(c, 1);
    rc = c->ops->memo_save(&c->memo, c->l, c->ops->table, c->id, &done,
                           &total, &res);
    busy(c, 0);
    if (rc == RQ_OK) {
        SendMessage(ctl(c, (u16)c->mi), EM_SETMODIFY, FALSE, 0L);
        return 1;
    }
    m = mio_failmsg(rc, done, total, &res);
    if (m != 0) {
        txt_u32(a, done);
        txt_u32(b, total);
        box(c->dlg, MB_ICONEXCLAMATION, msg_sub(m, a, b));
    }
    if (m != MM_OLDSRV)
        failed(c, &res);
    return 0;
}

/* The location field's value of a new record, which its screen may have
   preset as loaded, made a change, so that the ADD carries it. */
static void carry(wf_dlg *c)
{
    char v[LINK_LOCLEN + 1];
    const char *n;

    n = fld(c, (u16)c->li)->name;
    form_get_text(c->f, n, v, (u16)sizeof v);
    if (v[0] != '\0') {
        form_load(c->f, n, k_empty);
        form_put(c->f, n, v);
    }
}

/* Add tag to the tags that field i holds, as a change (ct_tags_add),
   clearing the message line, or say ME_TAGFULL there when it does not
   fit.  Its own frame keeps the copy off the stack of the owner's pick
   list. */
static void add_tag(wf_dlg *c, u16 i, const char *tag)
{
    char t[FORM_MAXVAL + 1];
    int ok;

    txt_copy(t, form_val(c->f, i), (u16)sizeof t);
    ok = ct_tags_add(t, fld(c, i)->width, tag);
    if (ok)
        form_put(c->f, fld(c, i)->name, t);
    say(c, -1, ok ? k_empty : msg(ME_TAGFULL));
}

/* The pick list of field i (WFORM.H), on the field's value, a location
   field's when its path shows it: the choice put in the field as a
   change, a location's with its path, a tag's added to the tags it
   holds (ct_tags_add), a failure shown, and the focus back on the
   control while the dialog stays; 1 when a row was chosen, else 0.
   find is the owner's pick list's (wf_ops' pick), 1 for OK's look-up
   of the value, which, taken as it is, leaves the control untouched. */
static int pick(wf_dlg *c, u16 i, int find)
{
    char code[WP_CODESZ];
    const form_field *d;
    const char *at;
    rq_res res;
    int kind;
    int r;
    u8 act;

    d = fld(c, i);
    kind = list_of(c, i);
    gather(c, i);
    at = form_val(c->f, i);
    act = RQE_STAY;
    if (kind == WP_LOCS)
        at = strcmp(at, c->shown) == 0 ? c->locid : k_empty;
    if (kind < 0)
        r = c->ops->pick(c->arg, c->dlg, c->l, c->level, at, find, code,
                         &act);
    else
        r = wp_pick(c->dlg, c->l, c->level, (u8)kind,
                    msg((u16)(kind == WP_LOCS ? MO_PICK :
                              kind == SCR_CK_TAG ? ME_TAGS : ME_CATS + kind)),
                    kind == WP_LOCS ? c->ops->loc->none : 0, at,
                    (c->ops->flags & WF_O_SELF) && c->f->m.mode != FORM_NEW ?
                    c->id : NULL, c->ops->busy, c->arg, code, &res, &act);
    if (r == WP_FAIL && failed(c, &res))
        return 0;
    if (act != RQE_STAY) {
        leave(c, act);
        return 0;
    }
    if (r == WP_ENOMEM)
        say(c, -1, msg(MT_NOMEM));
    if (r == WP_OK && find && strcmp(code, at) == 0)
        return 1;
    if (r == WP_OK) {
        if (kind == WP_LOCS && !c->ops->loc->filter &&
            strcmp(code, k_zero) == 0)
            code[0] = '\0';
        if (kind == SCR_CK_TAG)
            add_tag(c, i, code);
        else
            form_put(c->f, d->name, code);
        show(c, i);
        if ((int)i == c->ci)
            named(c);
        if (kind == WP_LOCS && !where(c, WF_W_PICKED))
            return 0;
    }
    focus(c, ctl(c, i));
    return r == WP_OK;
}

/* 1 when field i takes its value from the owner's pick list (ops->pick)
   and holds one, for OK to look up there first (WFORM.H, OK's step
   0). */
static int to_find(wf_dlg *c, u16 i)
{
    return pickable(c, i) && fld(c, i)->type == FT_CODE &&
           list_of(c, i) < 0 && form_val(c->f, i)[0] != '\0';
}

/* OK in FORM_EDIT or FORM_NEW (WFORM.H's steps). */
static void ok(wf_dlg *c)
{
    rq_res res;
    u16 note;
    u16 mf;
    u16 mm;
    u16 i;
    int bad;
    int rc;
    u8 was;

    gather_all(c);
    for (i = 0; i < c->f->m.def->nfields; i++)
        if (to_find(c, i) && !pick(c, i, 1))
            return;
    if (c->li >= 0 && strcmp(form_val(c->f, (u16)c->li), c->shown) != 0) {
        bad = where(c, WF_W_TYPED);
        if (bad != 1) {
            if (bad == WF_W_NONE)
                focus(c, ctl(c, (u16)c->li));
            return;
        }
    }
    bad = form_check(c->f, &mf);
    if (bad < 0 && c->ops->check != NULL)
        bad = c->ops->check(c->arg, c->f, &mf);
    if (bad >= 0) {
        say(c, bad, mf != 0 ? msg(mf) : k_empty);
        return;
    }
    if (memo_changed(c)) {
        GetWindowText(ctl(c, (u16)c->mi), c->text, WF_MEMOSZ);
        wrap(c->text);
        mio_set_text(&c->memo, c->text);
        mm = mio_check(&c->memo);
        if (mm != 0) {
            say_limit(c, mm);
            return;
        }
    }
    SetDlgItemText(c->dlg, WF_MSGID, k_empty);
    if (c->ops->table != NULL || c->ops->send != NULL) {
        was = c->f->m.mode;
        note = 0;
        if (was == FORM_NEW && c->ops->table != NULL && c->li >= 0)
            carry(c);
        c->f->sent = 1;
        busy(c, 1);
        if (c->ops->table == NULL)
            rc = c->ops->send(c->arg, c->f, c->l, c->id, &res);
        else if (c->ops->save != NULL)
            rc = c->ops->save(c->f, c->l, c->ops->table, c->id, c->idsz,
                              &res, &note);
        else
            rc = form_save(c->f, c->l, c->ops->table, c->id, c->idsz, &res);
        busy(c, 0);
        wipe(c);
        if (was == FORM_NEW && c->f->m.mode != FORM_NEW)
            added(c);
        if (rc != RQ_OK) {
            if (note != 0)
                box(c->dlg, MB_ICONEXCLAMATION, msg(note));
            failed(c, &res);
            return;
        }
    }
    if (memo_changed(c) && !memo_store(c))
        return;
    EndDialog(c->dlg, WF_OK);
}

/* Edit in FORM_VIEW (WFORM.H): the dialog ended with WF_EDIT, or, below
   TUI_LVL_OPER, INV's refusal (rqe_deny) said. */
static void edit(wf_dlg *c)
{
    const char *no;

    if (c->f->m.mode != FORM_VIEW || c->ops->table == NULL)
        return;
    no = rqe_deny(c->level, TUI_LVL_OPER);
    if (no != NULL)
        box(c->dlg, MB_ICONEXCLAMATION, no);
    else
        EndDialog(c->dlg, WF_EDIT);
}

/* Record button i or its key (WFORM.H): the dialog ended with WF_ACT +
   i, or, for a new or changed record, WF_S_SAVEFIRST on the message
   line. */
static void scope(wf_dlg *c, int i)
{
    char t[MSG_MAXLEN + 1];

    if (c->f->m.mode != FORM_NEW) {
        gather_all(c);
        if (!form_dirty(c->f) && !memo_changed(c)) {
            EndDialog(c->dlg, WF_ACT + i);
            return;
        }
    }
    LoadString(c->inst, WF_S_SAVEFIRST, t, (int)sizeof t);
    SetDlgItemText(c->dlg, WF_MSGID, t);
}

/* Cancel (WFORM.H). */
static void cancel(wf_dlg *c)
{
    char q[MSG_MAXLEN + 1];

    if (c->f->m.mode != FORM_VIEW) {
        gather_all(c);
        if ((form_dirty(c->f) &&
             (c->ops->table != NULL || c->ops->memo_save != NULL)) ||
            memo_changed(c)) {
            msg_get(MT_DISCARD, q, (u16)sizeof q);
            OemToAnsi(q, q);
            if (wd_say(c->dlg, MB_YESNO | MB_ICONQUESTION, q) != IDYES)
                return;
        }
    }
    EndDialog(c->dlg, WF_CANCEL);
}

/* Make the tab strip of a dialog of pages (WFORM.H) on the page shown,
   in the dialog's font, across the dialog under the identity row, and
   after that row's controls in the Z-order and so the tab order; 0 when
   Windows made none. */
static int strip(wf_dlg *c)
{
    RECT r;
    RECT w;
    HWND after;
    u16 i;

    SetRect(&r, WD_MARGIN, c->stripy, 0, 0);
    MapDialogRect(c->dlg, &r);
    GetClientRect(c->dlg, &w);
    r.right = w.right - r.left;
    c->strip = wt_create(c->dlg, &r, WF_STRIPID, tabs_of(c),
                         (HFONT)SendMessage(c->dlg, WM_GETFONT, 0, 0L));
    if (c->strip == NULL)
        return 0;
    after = NULL;
    for (i = 0; i < c->f->m.def->nfields; i++)
        if (page_of(c, i) == WF_PG_TOP)
            after = pickable(c, i) ? GetDlgItem(c->dlg, WF_PICK0 + i) :
                    ctl(c, i);
    SetWindowPos(c->strip, after, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessage(c->strip, WTM_SETCUR, c->page, 0L);
    return 1;
}

BOOL WCALLBACK wf_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    wf_dlg *c;
    u16 i;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    c = wf_cur;
    if (c == NULL)
        return FALSE;
    if (wm == WM_INITDIALOG) {
        c->dlg = dlg;
        memo_fit(c);
        if (c->npages > 1 && !strip(c)) {
            EndDialog(dlg, WF_EFAIL);
            return TRUE;
        }
        wd_centre(dlg);
        return init(c);
    }
    if (c->dlg != dlg)
        return FALSE;
    if (wm == WM_DESTROY) {
        for (i = 0; i < c->f->m.def->nfields; i++)
            if (is_edit(c, i))
                we_detach(ctl(c, i));
        c->dlg = NULL;
        c->strip = NULL;
        return FALSE;
    }
    if (wm != WM_COMMAND)
        return FALSE;
    i = (u16)(wp - WF_PICK0);
    if (wp == WF_EDITID)
        edit(c);
    else if (wp == WF_STRIPID && HIWORD(lp) == WTN_CHANGE)
        turn(c, (u8)SendMessage(c->strip, WTM_GETCUR, 0, 0L));
    else if (wp >= WF_ACT0 && wp < (WPARAM)(WF_ACT0 + acts(c)))
        scope(c, (int)(wp - WF_ACT0));
    else if (wp == WF_HELPID)
        wf_show(dlg, c->f->m.def->topic);
    else if (wp == WF_TYPEDID)
        typed(c);
    else if (HIWORD(lp) == CBN_DROPDOWN)
        c->dropsel = (int)SendMessage((HWND)LOWORD(lp), CB_GETCURSEL, 0,
                                      0L);
    else if ((HIWORD(lp) == EN_KILLFOCUS || HIWORD(lp) == CBN_SELCHANGE) &&
             ((c->li >= 0 && wp == (WPARAM)(WF_ID0 + c->li)) ||
              (c->ci >= 0 && wp == (WPARAM)(WF_ID0 + c->ci))))
        PostMessage(dlg, WM_COMMAND, WF_TYPEDID, 0L);
    else if (i < c->f->m.def->nfields && pickable(c, i))
        pick(c, i, 0);
    else if (wp == IDOK && c->f->m.mode != FORM_VIEW)
        ok(c);
    else if (wp == IDOK || wp == IDCANCEL)
        cancel(c);
    else
        return FALSE;
    return TRUE;
}

/* 1 when a drop-down list of the dialog is open.  Windows 3.0 has no
   CB_GETDROPPEDSTATE, but an open list is a visible window of class
   ComboLBox among the desktop's, of the dialog's task. */
static int dropped(wf_dlg *c)
{
    char cls[sizeof k_lbox + 1];
    HWND w;

    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w != NULL;
         w = GetWindow(w, GW_HWNDNEXT))
        if (IsWindowVisible(w) &&
            GetWindowTask(w) == GetWindowTask(c->dlg) &&
            GetClassName(w, cls, (int)sizeof cls) > 0 &&
            txt_ieq(cls, k_lbox))
            return 1;
    return 0;
}

/* The control of the first field (last 0) or of the last (last 1) shown
   whose control takes the focus by Tab, or NULL: not a path or name
   field, nor one of a page not shown. */
static HWND edge(wf_dlg *c, int last)
{
    HWND e;
    u16 n;
    u16 k;

    n = c->f->m.def->nfields;
    for (k = 0; k < n; k++) {
        e = ctl(c, last ? (u16)(n - 1 - k) : k);
        if (IsWindowVisible(e) && IsWindowEnabled(e) &&
            (GetWindowLong(e, GWL_STYLE) & WS_TABSTOP))
            return e;
    }
    return NULL;
}

/* The field of a page not shown whose label's access letter is ch, or
   -1.  The dialog manager of Windows 3.0 finds the letter of a hidden
   label too, and moves the focus to the first control after it that is
   shown, or clicks it, a button. */
static int behind(wf_dlg *c, int ch)
{
    char t[MSG_MAXLEN + 1];
    const char *s;
    u16 i;

    for (i = 0; key(ch) >= 0 && i < c->f->m.def->nfields; i++) {
        if (on_show(c, i) || hidden(c, i) || fld(c, i)->type == FT_MEMO)
            continue;
        GetWindowText(GetWindow(ctl(c, i), GW_HWNDPREV), t, (int)sizeof t);
        for (s = t; *s != '\0'; s++)
            if (*s == '&' && *++s != '&') {
                if (key(*s) == key(ch))
                    return (int)i;
                break;
            }
    }
    return -1;
}

/* The message filter while a form dialog runs: the tab strip's keys go
   to wt_key first (WTAB.H), then the access letter of a field on a page
   not shown (behind) shows the page and moves the focus to the field;
   Esc and Enter in an open drop-down list close it, Esc on the row it
   had when it dropped; CTRL+E typed in the dialog in FORM_VIEW goes to
   it as WF_EDITID, F4 in a field with a pick list as its button's
   command, ALT+Down being F4 here as in a drop-down list, and PgUp and
   PgDn but in the memo and an open list move the focus (WFORM.H), in
   place of the key. */
LRESULT WCALLBACK wf_filter(int code, WPARAM wp, LPARAM lp)
{
    const MSG FAR *m;
    wf_dlg *c;
    HWND e;
    UINT k;
    int i;

    m = (const MSG FAR *)lp;
    c = wf_cur;
    if (code != MSGF_DIALOGBOX || c == NULL || c->dlg == NULL ||
        (m->hwnd != c->dlg && !IsChild(c->dlg, m->hwnd)))
        return DefHookProc(code, wp, lp, &wf_next);
    if (c->strip != NULL && wt_key(c->strip, m->message, m->wParam))
        return 1;
    if (m->message == WM_SYSCHAR ||
        (m->message == WM_CHAR &&
         !(SendMessage(m->hwnd, WM_GETDLGCODE, 0, 0L) & DLGC_WANTCHARS))) {
        i = behind(c, (int)m->wParam);
        if (i < 0)
            return DefHookProc(code, wp, lp, &wf_next);
        focus(c, ctl(c, (u16)i));
        return 1;
    }
    k = m->message == WM_KEYDOWN ? m->wParam :
        m->message == WM_SYSKEYDOWN && m->wParam == VK_DOWN ? VK_F4 : 0;
    if (k == 0)
        return DefHookProc(code, wp, lp, &wf_next);
    i = field_of(c, m->hwnd);
    if ((k == VK_ESCAPE || k == VK_RETURN) && dropped(c)) {
        if (k == VK_ESCAPE) {
            SendMessage(m->hwnd, CB_SETCURSEL, (WPARAM)c->dropsel, 0L);
            if (i >= 0 && i == c->ci)
                PostMessage(c->dlg, WM_COMMAND, WF_TYPEDID, 0L);
        }
        SendMessage(m->hwnd, CB_SHOWDROPDOWN, FALSE, 0L);
    } else if (k == 'E' && GetKeyState(VK_CONTROL) < 0 &&
               c->f->m.mode == FORM_VIEW) {
        PostMessage(c->dlg, WM_COMMAND, WF_EDITID, 0L);
    } else if (k == VK_F4 && i >= 0 && pickable(c, (u16)i)) {
        PostMessage(c->dlg, WM_COMMAND, WF_PICK0 + i, 0L);
    } else if ((k == VK_PRIOR || k == VK_NEXT) &&
               (i < 0 || fld(c, (u16)i)->type != FT_MEMO) && !dropped(c)) {
        e = edge(c, k == VK_NEXT);
        if (e != NULL)
            focus(c, e);
    } else {
        return DefHookProc(code, wp, lp, &wf_next);
    }
    return 1;
}

/* Fetch the code lists of the dialog's code fields, a request for each
   kind, over owner as the dialog is not yet shown (WFORM.H); RQE_STAY,
   or the action of a failure that leaves, said in a message box over
   owner, the lists then freed. */
static u8 fetch(wf_dlg *c, HWND owner)
{
    rq_res res;
    rqe e;
    u16 i;
    int k;
    int rc;
    u8 tried;

    c->have = 0;
    tried = 0;
    for (i = 0; c->l != NULL && i < c->f->m.def->nfields; i++) {
        k = list_kind(c, i);
        if (k < 0 || (tried >> k & 1))
            continue;
        tried |= (u8)(1 << k);
        if (!ct_open(&c->lists[k]))
            continue;
        c->ops->busy(c->arg, owner, 1);
        rc = ct_load(&c->lists[k], c->l, wp_tables[k], &res);
        c->ops->busy(c->arg, owner, 0);
        if (rc == RQ_OK) {
            c->have |= (u8)(1 << k);
            continue;
        }
        ct_close(&c->lists[k]);
        rqe_class(&e, &res, c->level);
        if (e.act != RQE_STAY) {
            box(owner, WD_FAILICON(e.show), e.text);
            free_lists(c);
            return e.act;
        }
    }
    return RQE_STAY;
}

/* Fetch c's code lists, lay its pages out for them (paginate, fit,
   spill, texts), build the template of its dialog for its mode and run
   it over owner with the procedure proc (thunk); its end, WF_,
   or -1 when it could not be made. */
static int dialog(wf_dlg *c, HWND owner, FARPROC proc)
{
    wf_tpl t;
    int r;
    u8 act;

    act = fetch(c, owner);
    if (act != RQE_STAY)
        return gone(act);
    paginate(c);
    fit(c);
    spill(c);
    texts(c);
    t.h = NULL;
    t.n = 0;
    build(&t, c);
    t.h = GlobalAlloc(GMEM_MOVEABLE, t.n);
    r = -1;
    if (t.h != NULL) {
        t.n = 0;
        build(&t, c);
        r = DialogBoxIndirectParam(c->inst, t.h, owner, (DLGPROC)proc, 0L);
        GlobalFree(t.h);
    }
    free_lists(c);
    return r;
}

/* The pixels a static control of Windows 3.0 needs for text t in dc's
   font on one line: as it breaks the text into words, it measures each
   word with the blanks after it on its own, a bold font's overhang
   counted for each. */
static int text_w(HDC dc, const char *t)
{
    const char *s;
    int w;

    for (w = 0; *t != '\0'; t = s) {
        for (s = t; *s != '\0' && *s != ' '; s++)
            ;
        while (*s == ' ')
            s++;
        w += (int)LOWORD(GetTextExtent(dc, t, (int)(s - t)));
    }
    return w;
}

/* The wider of w and the pixels of ch in dc's font. */
static int wider(int w, HDC dc, char ch)
{
    int n;

    n = (int)LOWORD(GetTextExtent(dc, &ch, 1));
    return n > w ? n : w;
}

/* The pixels of the widest character a date or a number shows in dc's
   font: a digit, '-', or a separator of wrow_intl's formats, the
   decimal and thousands ones and the short date's. */
static int digit_w(HDC dc)
{
    const char *p;
    int w;
    int k;

    w = wider(0, dc, '-');
    for (k = 0; k < 10; k++)
        w = wider(w, dc, (char)('0' + k));
    w = wider(w, dc, wrow_intl.dec != '\0' ? wrow_intl.dec : '.');
    if (wrow_intl.thou != '\0')
        w = wider(w, dc, wrow_intl.thou);
    for (p = wrow_intl.date; *p != '\0'; p++)
        if (!TXT_ISALPHA(*p))
            w = wider(w, dc, *p);
    return w;
}

/* The pixels of the widest text field i shows when its value has a
   fixed format, in dc's font, dw the widest character of a date or a
   number (digit_w), else 0: FORM_DATEW characters for a date, as many
   as its cells for a number (cells_of), and a choice's longest row,
   with its name.  Free text, a code list's rows too, which name the
   user's codes, may be cut (WFORM.H). */
static int fixed_w(wf_dlg *c, HDC dc, u16 i, int dw)
{
    char code[FORM_MAXVAL + 1];
    char t[FORM_MAXVAL + 1];
    const form_field *d;
    int w;
    int n;
    u16 k;

    d = fld(c, i);
    switch (d->type) {
    case FT_DATE:
        return FORM_DATEW * dw;
    case FT_NUM:
        return cells_of(d) * dw;
    case FT_CHOICE:
        w = 0;
        for (k = 0; k < rows(c, i); k++) {
            row(c, i, k, code, t);
            OemToAnsi(t, t);
            n = (int)LOWORD(GetTextExtent(dc, t, (int)strlen(t)));
            if (n > w)
                w = n;
        }
        return w;
    default:
        return 0;
    }
}

/* px pixels of a static's text in dialog units, rounded up, at bu
   pixels for 4 units, with the WF_LSLACK it needs beside them;
   WF_UNITSMAX for that many or more. */
static u8 units(int px, int bu)
{
    long n;

    n = ((long)(px + WF_LSLACK) * 4 + bu - 1) / bu;
    return (u8)(n < WF_UNITSMAX ? n : WF_UNITSMAX);
}

/* The pixels of field i's value in dc's font as a static shows free text
   in FORM_VIEW, else 0: a value of FT_CHAR or FT_CODE as it is. */
static int value_w(wf_dlg *c, HDC dc, u16 i)
{
    char t[FORM_MAXVAL + 1];
    u8 type;

    type = fld(c, i)->type;
    if (c->f->m.mode != FORM_VIEW || (type != FT_CHAR && type != FT_CODE))
        return 0;
    txt_copy(t, form_val(c->f, i), (u16)sizeof t);
    OemToAnsi(t, t);
    return text_w(dc, t);
}

/* Measure each field's label (lw), each fixed-format value's widest
   text (vw, fixed_w), each free text's value in FORM_VIEW (tw, value_w)
   and the strip's height (striph) in dialog units,
   rounded up, in a dialog of the form dialog's font made unseen over
   owner and destroyed, with proc, its base units from MapDialogRect;
   0, or WF_EFAIL when Windows made no dialog. */
static int measure(wf_dlg *c, HWND owner, FARPROC proc)
{
    char t[MSG_MAXLEN + 1];
    wf_tpl tp;
    RECT u;
    RECT r;
    HWND dlg;
    HFONT font;
    HFONT old;
    HDC dc;
    u16 i;
    int dw;
    int px;

    tp.h = GlobalAlloc(GMEM_MOVEABLE, WF_RECSZ);
    if (tp.h == NULL)
        return WF_EFAIL;
    tp.n = 0;
    head(&tp, WS_POPUP | DS_SETFONT, 0, 0, 0, k_empty);
    dlg = CreateDialogIndirect(c->inst, (LPCSTR)GlobalLock(tp.h), owner,
                               (DLGPROC)proc);
    GlobalUnlock(tp.h);
    GlobalFree(tp.h);
    if (dlg == NULL)
        return WF_EFAIL;
    SetRect(&u, 0, 0, 4, 8);
    MapDialogRect(dlg, &u);
    font = (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0L);
    dc = GetDC(dlg);
    old = (HFONT)SelectObject(dc, font);
    dw = digit_w(dc);
    for (i = 0; i < c->f->m.def->nfields; i++) {
        wf_label(c->inst, fld(c, i), t);
        c->lw[i] = units(text_w(dc, t), u.right);
        px = fixed_w(c, dc, i, dw);
        c->vw[i] = px > 0 ? units(px, u.right) : 0;
        px = value_w(c, dc, i);
        c->tw[i] = px > 0 ? units(px, u.right) : 0;
    }
    SelectObject(dc, old);
    ReleaseDC(dlg, dc);
    SetRectEmpty(&r);
    if (c->npages > 1)
        wt_create(dlg, &r, WF_STRIPID, tabs_of(c), font);
    c->striph = (u8)((r.bottom * 8 + u.bottom - 1) / u.bottom);
    DestroyWindow(dlg);
    return 0;
}

/* The value column of the pages' rows, the strip's top and the pages'
   top (WFORM.H), once the labels are measured. */
static void frame(wf_dlg *c)
{
    const form_field *d;
    u8 idx[FORM_MAXFIELDS];
    u16 i;
    int w;

    c->vx = 0;
    for (i = 0; i < c->f->m.def->nfields; i++) {
        d = fld(c, i);
        if (page_of(c, i) == WF_PG_TOP || d->type == FT_MEMO)
            continue;
        peers(c, i, idx);
        if (idx[0] != i || unlabelled(d) || d->type == FT_LOGIC)
            continue;
        w = (d->vcol - d->col) * WF_CELLW;
        if (c->lw[i] + WF_LGAP > w)
            w = c->lw[i] + WF_LGAP;
        if (w > c->vx)
            c->vx = w;
    }
    c->vx += WD_MARGIN;
    c->stripy = WD_MARGIN + (c->top ? WF_ROWH + WD_GAP : 0);
    c->pagey = c->stripy + (c->npages > 1 ? c->striph + WD_GAP : 0);
}

/* ---------------------------------------------------------------------
 * The interface.
 * --------------------------------------------------------------------- */

/* The index of f's FT_MEMO field when ops shows its memo (memo_load),
   else -1. */
static int memo_index(form *f, const wf_ops *ops)
{
    u16 i;

    if (ops->memo_load != NULL)
        for (i = 0; i < f->m.def->nfields; i++)
            if (f->m.def->fields[i].type == FT_MEMO)
                return (int)i;
    return -1;
}

int wf_open(form **out, const form_def *def, u8 mode)
{
    *out = form_new(def, mode, (u16)sizeof(struct form));
    return *out != NULL ? 0 : WF_EFAIL;
}

int wf_run(form *f, HWND owner, link *l, u8 level, const wf_ops *ops,
           void *arg, char *id, u16 idsz)
{
    wf_dlg c;
    FARPROC proc;
    FARPROC filter;
    int r;
    int k;
    u16 i;

    if (wf_cur != NULL)
        return WF_EFAIL;
    c.f = f;
    c.ops = ops;
    c.arg = arg;
    c.l = l;
    c.id = id;
    c.idsz = idsz;
    c.level = level;
    c.dlg = NULL;
    c.text = NULL;
    c.inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    c.mm = WF_M_NEW;
    c.mi = memo_index(f, ops);
    c.li = -1;
    c.pi = -1;
    c.ni = -1;
    c.ci = -1;
    c.shown[0] = '\0';
    c.locid[0] = '\0';
    c.strip = NULL;
    c.page = 0;
    c.bare = 0;
    c.plain = 0;
    paginate(&c);
    for (i = 0; ops->codes != NULL && i < f->m.def->nfields; i++) {
        k = code_of(f->m.def, i);
        if (k >= 0) {
            c.ni = (int)i;
            c.ci = k;
        }
    }
    if (ops->loc != NULL && l != NULL) {
        c.li = form_find(f, ops->loc->loc->name);
        c.pi = form_find(f, ops->loc->path->name);
        if (c.pi < 0)
            c.li = -1;
        if (c.li < 0)
            c.pi = -1;
    }
    if (c.mi >= 0 && mio_open(&c.memo) == RQ_OK)
        c.text = (char *)malloc(WF_MEMOSZ);
    proc = MakeProcInstance((FARPROC)wf_proc, c.inst);
    filter = MakeProcInstance((FARPROC)wf_filter, c.inst);
    r = WF_EFAIL;
    if (proc != NULL && filter != NULL && (c.mi < 0 || c.text != NULL) &&
        measure(&c, owner, proc) == 0) {
        frame(&c);
        wf_next = SetWindowsHook(WH_MSGFILTER, (HOOKPROC)filter);
        wf_cur = &c;
        while ((r = dialog(&c, owner, proc)) == WF_EDIT)
            f->m.mode = FORM_EDIT;
        wf_cur = NULL;
        UnhookWindowsHook(WH_MSGFILTER, (HOOKPROC)filter);
    }
    if (filter != NULL)
        FreeProcInstance(filter);
    if (proc != NULL)
        FreeProcInstance(proc);
    free(c.text);
    if (c.mi >= 0)
        mio_close(&c.memo);
    return r < 0 ? WF_EFAIL : r;
}

void wf_help(void (*show)(HWND owner, const char *topic))
{
    wf_show = show;
}

void wf_wipe(void)
{
    if (wf_cur != NULL && wf_cur->dlg != NULL)
        wipe(wf_cur);
}

int wf_sent(form *f)
{
    return f->sent;
}

const char *wf_topic(void)
{
    return wf_cur != NULL ? wf_cur->f->m.def->topic : NULL;
}

const char *wf_field(void)
{
    HWND w;
    int i;

    w = GetFocus();
    if (wf_cur == NULL || wf_cur->dlg == NULL || GetParent(w) != wf_cur->dlg)
        return NULL;
    i = GetDlgCtrlID(w) - WF_ID0;
    if (i >= FORM_MAXFIELDS)
        i -= FORM_MAXFIELDS;            /* a pick button, WF_PICK0 + i  */
    return i >= 0 && i < wf_cur->f->m.def->nfields ?
           fld(wf_cur, (u16)i)->name : NULL;
}

void wf_view_text(form *f, u16 i, char *t)
{
    const form_field *d;
    const char *v;
    int k;

    d = &f->m.def->fields[i];
    v = form_val(f, i);
    k = d->type == FT_CHOICE && d->dec != 0 ?
        form_choice_find(d->choices, v) : -1;
    if (k >= 0)
        msg_get(form_choice_name(d, (u16)k), t, FORM_MAXVAL + 1);
    else if (d->type == FT_NUM && !(d->flags & FF_ID))
        intl_num(&wrow_intl, t, v, FORM_MAXVAL);
    else if (d->type != FT_DATE || !intl_date(&wrow_intl, t, v))
        txt_copy(t, v, FORM_MAXVAL + 1);
}

void wf_name(form *f, const wf_ops *ops, const char *key, char *t)
{
    const char *const *n;
    const char *v;
    u16 len;

    txt_copy(t, (ops->flags & WF_O_NOKEY) ? k_empty : key, WF_NAMESZ);
    for (n = ops->names; n != NULL && *n != NULL; n++) {
        v = form_get(f, *n);
        len = (u16)strlen(t);
        if (v[0] != '\0' && len + 2 < WF_NAMESZ) {
            if (len > 0)
                t[len++] = ' ';
            txt_copy(t + len, v, (u16)(WF_NAMESZ - len));
        }
    }
}

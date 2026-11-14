/*
 * SCRRFORM.C - the report and label forms, the opening of a run's
 * layout and SCREEN job, and the reading of the file a SCREEN run
 * writes (SCRRFORM.H).
 *
 * Compile-time checks here are pairs of declarations of one array, the
 * second of size 2 when the condition fails: they then disagree, which
 * every compiler refuses.
 */
#define MSG_PATHS
#include <stddef.h>
#include <string.h>
#define COMPAT_FIND
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "FMODEL.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "PRINT.H"
#include "RPT.H"
#include "RPTRUN.H"
#include "SCRLFORM.H"
#include "SCRSFORM.H"
#include "SCRRFORM.H"

#define RR_VCOL2        36              /* the second column's values   */
#define RR_FILEDISP     32              /* cells of the file field      */
#define RR_OUTW         7               /* "PRINTER"                    */
#define RV_OFFSZ        4               /* bytes of a line's offset     */

/* The five report commands are the five RPT_ kinds in the same order. */
extern char rr_cmds_fit[1];
extern char rr_cmds_fit[(SCMD_RPT_VALUE - SCMD_RPT_LIST ==
                         RPT_VALUE - RPT_LIST &&
                         SCMD_RPT_LOANS - SCMD_RPT_LIST ==
                         RPT_LOANS - RPT_LIST &&
                         SCMD_LBL_ITEM - SCMD_RPT_LIST ==
                         RPT_ILABEL - RPT_LIST &&
                         SCMD_LBL_BIN - SCMD_RPT_LIST ==
                         RPT_BLABEL - RPT_LIST) ? 1 : 2];

/* The file name and the path fit a form value, the path and the tags
   the box, and the names of the works states lie after their label
   within a u8 (form_field's dec). */
extern char rr_file_fits[1];
extern char rr_file_fits[(SCR_RP_FILEW <= FORM_MAXVAL &&
                          RR_VCOL2 + SCR_RP_WHEREW <= SCR_POPW - 2 &&
                          RR_VCOL2 + LINK_TAGFILT <= SCR_POPW - 2 &&
                          MQ_C_WQ > MQ_L_WORKS &&
                          MQ_C_WQ - MQ_L_WORKS <= 0xFF) ? 1 : 2];

/* The offsets fit a far block, and the least of them the most. */
extern char rv_offs_fit[1];
extern char rv_offs_fit[(SCR_RV_MAXLINES * (long)RV_OFFSZ <=
                         (long)CMEM_MAXBLK &&
                         SCR_RV_MINLINES <= SCR_RV_MAXLINES &&
                         sizeof(u32) == RV_OFFSZ) ? 1 : 2];

const char scr_rpt_k_out[] = "OUT";
const char scr_rpt_k_file[] = "FILE";
const char scr_rpt_k_where[] = "WHERE";
const char scr_rpt_k_reports[] = "REPORTS";
const char scr_rpt_k_labels[] = "LABELS";
/* The other field names (a filter's is its key, rrun_k_ of RPTRUN.H);
   each literal is kept once. */
static const char k_item[] = "ITEM_ID";
static const char k_below[] = "BELOW";

static const char k_order_list[] = "LOC ID MFR";
static const char k_status_list[] = "ALL A D";
static const char k_all[] = "ALL";
static const char k_out_list[] = "SCREEN PRINTER FILE";

/* The fields of all five forms, each at its column in the box; a kind
   names the ones its form shows, in the order the cursor visits them,
   and their rows (rr_kinds), and scr_rpt_form copies those into the
   form's table. */
#define RF_CAT          0
#define RF_LOC          1
#define RF_ORDER        2
#define RF_STATUS       3
#define RF_OVERDUE      4
#define RF_ASOF         5
#define RF_ITEM         6
#define RF_BELOW        7
#define RF_WHERE        8
#define RF_OUT          9
#define RF_FILE         10
#define RF_WORKS        11
#define RF_TAG          12

static const form_field rr_fields[] = {
    { rrun_k_cat, MQ_L_CAT, 0, 1, 11, FT_CODE, LINK_CATLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { rrun_k_loc, MQ_L_LOC, 0, 26, RR_VCOL2, FT_CODE, LINK_LOCLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { rrun_k_order, MQ_L_ORDER, 1, 1, 11, FT_CHOICE, RRUN_WORDLEN, 0,
      FF_LOCAL | FF_REQ, 0, k_order_list },
    { rrun_k_status, MQ_L_STATUS, 1, 26, RR_VCOL2, FT_CHOICE, RRUN_WORDLEN, 0,
      FF_LOCAL | FF_REQ, 0, k_status_list },
    { rrun_k_overdue, MQ_L_OVERDUE, 0, 1, 11, FT_LOGIC, 1, 0, FF_LOCAL, 0,
      NULL },
    { rrun_k_asof, MQ_L_ASOF, 0, 26, RR_VCOL2, FT_DATE, FORM_DATEW, 0,
      FF_LOCAL, 0, NULL },
    { k_item, MQ_L_ITEM, 1, 1, 11, FT_NUM, LINK_IDLEN, 0,
      FF_LOCAL | FF_NONNEG | FF_ID, 0, NULL },
    { k_below, MQ_L_BELOW, 2, 1, 11, FT_LOGIC, 1, 0, FF_LOCAL, 0, NULL },
    { scr_rpt_k_where, MQ_L_WHERE, 2, 26, RR_VCOL2, FT_CHAR, SCR_RP_WHEREW,
      0, FF_LOCAL | FF_RO, 0, NULL },
    { scr_rpt_k_out, MQ_L_OUT, 3, 1, 11, FT_CHOICE, RR_OUTW, 0,
      FF_LOCAL | FF_REQ, 0, k_out_list },
    { scr_rpt_k_file, MQ_L_FILE, 3, 26, RR_VCOL2, FT_CHAR, SCR_RP_FILEW, 0,
      FF_LOCAL, RR_FILEDISP, NULL },
    { rrun_k_works, MQ_L_WORKS, 0, 1, 11, FT_CHOICE, SCR_WORKSW,
      MQ_C_WQ - MQ_L_WORKS, FF_LOCAL | FF_UPPER, 0, scr_works_filt },
    { rrun_k_tag, MQ_L_TAG, 0, 26, RR_VCOL2, FT_CODE, LINK_TAGFILT, 0,
      FF_LOCAL, 0, NULL }
};

/* The box of every form; scr_rpt_form copies it and puts in the title,
   the fields, the height their rows need and, for labels, the help
   topic of the kind. */
static const form_def rr_def = {
    0, scr_rpt_k_reports, SCR_POPTOP, SCR_POPLEFT, 0, SCR_POPW, 0, NULL
};

/* A kind's fields in the order the cursor visits them, the first the
   one most often set, each on its row of the kind's own, its column the
   field's: rows without holes, and the location's path right under
   it. */
typedef struct {
    u16  title;
    u8   n;                     /* fields                               */
    u8   f[SCR_RP_MAXF];        /* RF_ numbers                          */
    u8   row[SCR_RP_MAXF];      /* and their rows                       */
} rr_kind;

/* By kind - RPT_LIST. */
static const rr_kind rr_kinds[RPT_BLABEL - RPT_LIST + 1] = {
    { MR_T_LIST, 10, { RF_CAT, RF_LOC, RF_BELOW, RF_WHERE, RF_ORDER,
                       RF_STATUS, RF_WORKS, RF_TAG, RF_OUT, RF_FILE },
      { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4 } },
    { MR_T_VALUE, 8, { RF_CAT, RF_LOC, RF_BELOW, RF_WHERE, RF_WORKS,
                       RF_TAG, RF_OUT, RF_FILE },
      { 0, 0, 1, 1, 2, 2, 3, 3 } },
    { MQ_T_LOANS, 4, { RF_OVERDUE, RF_ASOF, RF_OUT, RF_FILE },
      { 0, 0, 1, 1 } },
    { MQ_T_ILABEL, 9, { RF_CAT, RF_LOC, RF_BELOW, RF_WHERE, RF_WORKS,
                        RF_TAG, RF_ITEM, RF_OUT, RF_FILE },
      { 0, 0, 1, 1, 2, 2, 3, 4, 4 } },
    { MQ_T_BLABEL, 5, { RF_LOC, RF_BELOW, RF_WHERE, RF_OUT, RF_FILE },
      { 0, 1, 1, 2, 2 } }
};

const scr_locf scr_rpt_loc = {
    &rr_fields[RF_LOC], &rr_fields[RF_WHERE], MO_NOLOC, 1
};
const scr_locf scr_rpt_bloc = {
    &rr_fields[RF_LOC], &rr_fields[RF_WHERE], MO_ALLLOC, 0
};

/* ---------------------------------------------------------------------
 * The forms and runs.
 * --------------------------------------------------------------------- */

void scr_rpt_form(form_def *def, form_field *fields, u8 kind)
{
    const rr_kind *kd;
    u8 k;

    kd = &rr_kinds[kind - RPT_LIST];
    memcpy(def, &rr_def, sizeof *def);
    if (kind >= RPT_ILABEL)
        def->topic = scr_rpt_k_labels;
    def->title = kd->title;
    def->h = (u8)(kd->row[kd->n - 1] + 3);
    def->nfields = kd->n;
    def->fields = fields;
    for (k = 0; k < kd->n; k++) {
        memcpy(&fields[k], &rr_fields[kd->f[k]], sizeof *fields);
        fields[k].row = kd->row[k];
    }
}

void scr_rpt_defaults(form *f)
{
    form_load(f, rrun_k_order, rrun_k_loc);
    form_load(f, rrun_k_status, k_all);
    form_load(f, scr_rpt_k_out, "SCREEN");
    form_load(f, rrun_k_overdue, "F");
    form_load(f, k_below, "F");
}

void scr_rpt_values(rrun *u, form *f, char *file)
{
    char v[RRUN_WORDLEN + 1];

    form_get_text(f, rrun_k_order, u->order, (u16)sizeof u->order);
    form_get_text(f, rrun_k_status, u->status, (u16)sizeof u->status);
    if (strcmp(u->status, k_all) == 0)
        u->status[0] = '\0';
    form_get_text(f, rrun_k_cat, u->cat, (u16)sizeof u->cat);
    form_get_text(f, rrun_k_tag, u->tag, (u16)sizeof u->tag);
    ct_tags_bare(u->tag);
    form_get_text(f, rrun_k_works, u->works, (u16)sizeof u->works);
    form_get_text(f, rrun_k_loc, u->loc, (u16)sizeof u->loc);
    form_get_text(f, k_item, u->item, (u16)sizeof u->item);
    form_get_text(f, k_below, v, (u16)sizeof v);
    u->below = (u8)(v[0] == 'T');
    form_get_text(f, rrun_k_overdue, v, (u16)sizeof v);
    u->overdue = (u8)(v[0] == 'T');
    form_get_text(f, rrun_k_asof, u->date, (u16)sizeof u->date);
    form_get_text(f, scr_rpt_k_file, file, SCR_RP_FILEW + 1);
}

u16 scr_rpt_file_bad(const char *file)
{
    if (file[0] == '\0')
        return MQ_NEEDFILE;
    return (u16)(txt_outfile_ok(file, SCR_RP_FILEW) ? 0 : MQ_BADFILE);
}

/* Append t to the subtitle sub (RPT_SUBLEN characters at most), two
   spaces after what it holds already. */
static void sub_cat(char *sub, const char *t)
{
    size_t n;

    n = strlen(sub);
    if (n > 0 && n + 2 <= RPT_SUBLEN) {
        strcat(sub, "  ");
        n += 2;
    }
    strncat(sub, t, RPT_SUBLEN - n);
}

/* Append the label m and the value v to the subtitle sub, as sub_cat;
   nothing when v is "". */
static void sub_add(char *sub, u16 m, const char *v)
{
    char t[RPT_SUBLEN + 2];
    size_t n;

    if (v[0] == '\0')
        return;
    n = msg_get(m, t, RPT_SUBLEN);
    t[n++] = ' ';
    t[n] = '\0';
    strncat(t, v, RPT_SUBLEN - n);
    sub_cat(sub, t);
}

int scr_rpt_layout(rrun *u, const rpt_geom *g, const char *path)
{
    char sub[RPT_SUBLEN + 1];
    char st[RPT_SUBLEN + 1];
    char loc[RPT_SUBLEN + 1];
    size_t n;
    int below;

    sub[0] = '\0';
    if (u->kind != RPT_LIST && u->kind != RPT_VALUE)
        return rrun_layout(u, g, sub);
    sub_add(sub, MQ_L_CAT, u->cat);
    /* The filters after the location, the works state by its name. */
    st[0] = '\0';
    sub_add(st, MQ_L_STATUS, u->status);
    sub_add(st, MQ_L_TAG, u->tag);
    if (u->works[0] != '\0')
        sub_add(st, MQ_L_WORKS,
                msg((u16)(MQ_C_WQ +
                          form_choice_num(scr_works_filt, u->works))));
    loc[0] = '\0';
    if (path[0] != '\0') {
        /* The path takes the room the other filters leave. */
        below = u->below && strcmp(u->loc, "0") != 0;
        n = strlen(sub) + 2 + strlen(msg(MQ_L_LOC)) + 1 +
            (st[0] != '\0' ? 2 + strlen(st) : 0) +
            (below ? strlen(msg_sub(MQ_ANDBELOW, "", NULL)) : 0);
        msg_path(loc, path, (u16)(n + MSG_PATHCUTW < RPT_SUBLEN ?
                                  RPT_SUBLEN - n : MSG_PATHCUTW + 1));
        if (below)
            txt_copy(loc, msg_sub(MQ_ANDBELOW, loc, NULL), (u16)sizeof loc);
    }
    sub_add(sub, MQ_L_LOC, loc);
    if (st[0] != '\0')
        sub_cat(sub, st);
    return rrun_layout(u, g, sub);
}

/* The SCREEN output's file name number n (0 to SCR_RP_SCRTRY - 1) of
   station into d (SCR_RP_SCRSZ bytes): INVR, four hex digits of a
   checksum of the station name XOR n, then .$$$. */
static void scratch_name(char *d, u16 n, const char *station)
{
    const char *p;
    u16 h;
    int i;

    h = 0;
    for (p = station; *p != '\0'; p++)
        h = (u16)((u16)(h << 5) + (u16)(h >> 11) + (u8)*p);
    h ^= n;
    strcpy(d, "INVR0000.$$$");
    for (i = 7; i >= 4; i--) {
        d[i] = txt_hexdig[h & 15];
        h = (u16)(h >> 4);
    }
}

int scr_rpt_screen(rrun *u, char *scratch, const char *station,
                   int *cferr)
{
    u16 n;
    int err;

    n = 0;
    do {
        scratch_name(scratch, n, station);
        err = pr_open(&u->p, PR_NEWFILE, PR_PLAIN, 0, scratch, NULL, NULL,
                      cferr);
    } while (*cferr == CF_EEXIST && ++n < SCR_RP_SCRTRY);
    return err;
}

void scr_rpt_failed(const rrun *u, int rc, rq_res *res)
{
    const char *f;

    if (rc != RQ_ENOTFOUND)
        return;
    if (u->kind == RPT_ILABEL && rrun_one(u))
        f = k_item;
    else if (u->loc[0] != '\0' && strcmp(u->loc, "0") != 0)
        f = rrun_k_loc;
    else
        return;
    res->code = RQ_EARG;
    strcpy(res->field, f);
    msg_get(MQ_NOTFOUND, res->text, (u16)sizeof res->text);
}

/* ---------------------------------------------------------------------
 * The viewer's file.
 * --------------------------------------------------------------------- */

int scr_rv_open(scr_rview *v, const char *path, u32 lines)
{
    u32 pos;
    u16 want;
    u16 i;
    int start;
    int rc;

    pos = 0;
    start = 1;
    want = lines > SCR_RV_MAXLINES ? (u16)SCR_RV_MAXLINES : (u16)lines;
    if (want == 0)
        want = 1;
    rc = cf_open(path, CF_READ, &v->h);
    if (rc != 0)
        return rc;
    for (;;) {
        v->b = cmem_alloc((u16)(want * (u16)RV_OFFSZ));
        if (v->b != NULL || want <= SCR_RV_MINLINES)
            break;
        want = want / 2 > SCR_RV_MINLINES ? (u16)(want / 2) :
                                            (u16)SCR_RV_MINLINES;
    }
    v->n = 0;
    v->cut = 0;
    rc = v->b == NULL ? CF_ENOMEM : cf_seek(v->h, 0);
    while (rc == 0 && !v->cut) {
        rc = cf_read(v->h, v->buf, SCR_RV_CHUNK, &v->got);
        if (rc != 0 || v->got == 0)
            break;
        for (i = 0; i < v->got; i++, pos++) {
            if (start || v->buf[i] == '\f') {
                if (v->n >= want) {
                    v->cut = 1;
                    break;
                }
                cmem_put(v->b, (u16)(v->n * (u16)RV_OFFSZ), &pos, RV_OFFSZ);
                v->n++;
            }
            start = v->buf[i] == '\f' || v->buf[i] == '\n';
        }
    }
    if (rc != 0)
        scr_rv_close(v);
    v->next = v->n;
    return rc;
}

/* A line runs to its LF, or up to an FF that does not begin it, which
   the next line then begins; the read goes on from line next, the line
   after the one read last. */
int scr_rv_line(scr_rview *v, u16 i, char *line)
{
    u32 off;
    u16 k;
    u16 len;
    int kind;
    u8 shown;
    char ch;

    k = 0;
    kind = SCR_RV_END;
    shown = 1;
    if (i < v->n && i != v->next) {
        cmem_get(v->b, (u16)(i * (u16)RV_OFFSZ), &off, RV_OFFSZ);
        v->at = v->got;
        v->next = cf_seek(v->h, off) == 0 ? i : v->n;
    }
    if (i < v->n && i == v->next) {
        v->next++;
        kind = SCR_RV_TEXT;
        for (len = 0;; len++) {
            if (v->at == v->got) {
                v->at = 0;
                if (cf_read(v->h, v->buf, SCR_RV_CHUNK, &v->got) != 0)
                    v->got = 0;
                if (v->got == 0)
                    break;
            }
            ch = v->buf[v->at];
            if (ch == '\f') {
                if (len == 0) {
                    v->at++;
                    kind = SCR_RV_RULE;
                    k = RPT_WIDTH;
                    memset(line, TF_SH, k);
                }
                break;
            }
            v->at++;
            if (ch == '\n')
                break;
            if (ch == '\r')
                shown = 0;
            if (shown && k < RPT_WIDTH)
                line[k++] = ch;
        }
    }
    line[k] = '\0';
    return kind;
}

void scr_rv_close(scr_rview *v)
{
    cmem_free(v->b);
    cf_close(v->h);
}
